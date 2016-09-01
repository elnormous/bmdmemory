//
//  BMD memory
//

#include <iostream>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <cstring>
#include "BMDMemory.h"

BMDMemory::BMDMemory(const std::string& pName):
    name(pName)
{
    semName = name + "_sem";
}

BMDMemory::~BMDMemory()
{
    if (deckLinkConfiguration) deckLinkConfiguration->Release();
    if (displayMode) displayMode->Release();
    if (displayModeIterator) displayModeIterator->Release();
    if (deckLinkInput) deckLinkInput->Release();
    if (deckLink) deckLink->Release();

    if (sem != SEM_FAILED)
    {
        if (sem_close(sem) == -1)
        {
            std::cerr << "Failed to destroy semaphore\n";
        }
    }

    if (sem_unlink(semName.c_str()) == -1)
    {
        std::cerr << "Failed to delete semaphore\n";
    }

    if (sharedMemoryFd)
    {
        if (close(sharedMemoryFd) == -1)
        {
            std::cerr << "Failed to close shared memory file descriptor\n";
        }
    }

    if (sharedMemory == MAP_FAILED)
    {
        if (munmap(sharedMemory, sizeof(Memory)) == -1)
        {
            std::cerr << "Failed to unmap shared memory\n";
        }
    }

    if (shm_unlink(name.c_str()) == -1)
    {
        std::cerr << "Failed to delete shared memory\n";
    }
}

bool BMDMemory::run(int32_t videoMode)
{
    sem_unlink(semName.c_str());
    shm_unlink(name.c_str());

    if ((sharedMemoryFd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR , S_IRUSR | S_IWUSR)) == -1)
    {
        std::cerr << "Failed to create shared memory\n";
        return false;
    }

    if (ftruncate(sharedMemoryFd, sizeof(Memory)) == -1)
    {
        std::cerr << "Failed to resize shared memory\n";
        return false;
    }

    sharedMemory = reinterpret_cast<Memory*>(mmap(nullptr, sizeof(Memory), PROT_READ | PROT_WRITE, MAP_SHARED, sharedMemoryFd, 0));

    if (sharedMemory == MAP_FAILED)
    {
        std::cerr << "Failed to map shared memory\n";
        return false;
    }

    memset(sharedMemory, 0, sizeof(Memory));

    if ((sem = sem_open(semName.c_str(), O_CREAT, S_IRUSR | S_IWUSR, 1)) == SEM_FAILED)
    {
        std::cerr << "Failed to initialize semaphore " << errno << "\n";
        return false;
    }

    IDeckLinkIterator* deckLinkIterator = CreateDeckLinkIteratorInstance();

    if (!deckLinkIterator)
    {
        std::cerr << "This application requires the DeckLink drivers installed\n";
        return false;
    }

    HRESULT result;

    do
    {
        result = deckLinkIterator->Next(&deckLink);
    }
    while (result != S_OK);

    if (result != S_OK)
    {
        std::cerr << "No DeckLink PCI cards found\n";
        return false;
    }

    result = deckLink->QueryInterface(IID_IDeckLinkInput,
                                      reinterpret_cast<void**>(&deckLinkInput));
    if (result != S_OK)
    {
        return false;
    }

    result = deckLink->QueryInterface(IID_IDeckLinkConfiguration,
                                      reinterpret_cast<void**>(&deckLinkConfiguration));
    if (result != S_OK)
    {
        std::cerr << "Failed to obtain the IDeckLinkConfiguration interface - result = " << result << "\n";
        return false;
    }

    deckLinkInput->SetCallback(this);

    result = deckLinkInput->GetDisplayModeIterator(&displayModeIterator);
    if (result != S_OK)
    {
        std::cerr << "Failed to obtain the video output display mode iterator - result = " << result << "\n";
        return false;
    }

    int displayModeCount = 0;

    while (displayModeIterator->Next(&displayMode) == S_OK)
    {
        if (videoMode == -1 || videoMode == displayModeCount)
        {
            selectedDisplayMode = displayMode->GetDisplayMode();
            break;
        }
        displayModeCount++;
        displayMode->Release();
        displayMode = nullptr;
    }

    if (!displayMode)
    {
        std::cerr << "Failed to find display mode\n";
        return false;
    }

    width = displayMode->GetWidth();
    height = displayMode->GetHeight();
    displayMode->GetFrameRate(&frameDuration, &timeScale);
    fieldDominance = displayMode->GetFieldDominance();

    writeMetaData();

    result = deckLinkInput->EnableVideoInput(selectedDisplayMode, pixelFormat, 0);
    if (result != S_OK)
    {
        std::cerr << "Failed to enable video input\n";
        return false;
    }

    result = deckLinkInput->EnableAudioInput(audioSampleRate,
                                             audioSampleDepth,
                                             audioChannels);
    if (result != S_OK)
    {
        std::cerr << "Failed to enable audio input\n";
        return false;
    }

    result = deckLinkInput->StartStreams();
    if (result != S_OK)
    {
        std::cerr << "Failed to start streaming\n";
        return false;
    }

    std::cout << "Streaming started\n";
    std::cout << "width: " << width << ", height: " << height << ", frameDuration: " << frameDuration << ", timeScale: " << timeScale << "\n";

    return true;
}

ULONG BMDMemory::AddRef()
{
    std::lock_guard<std::mutex> lock(dataMutex);

    refCount++;

    return refCount;
}

ULONG BMDMemory::Release()
{
    std::lock_guard<std::mutex> lock(dataMutex);
    refCount--;

    if (refCount == 0)
    {
        delete this;
        return 0;
    }

    return refCount;
}

HRESULT BMDMemory::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode* newDisplayMode,
                                          BMDDetectedVideoInputFormatFlags)
{
    displayMode = newDisplayMode;
    width = displayMode->GetWidth();
    height = displayMode->GetHeight();
    displayMode->GetFrameRate(&frameDuration, &timeScale);
    fieldDominance = displayMode->GetFieldDominance();

    writeMetaData();

    return S_OK;
}

HRESULT BMDMemory::VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame,
                                         IDeckLinkAudioInputPacket* audioFrame)
{
    BMDTimeValue timestamp;

    if (videoFrame)
    {
        if (videoFrame->GetFlags() & static_cast<BMDFrameFlags>(bmdFrameHasNoInputSource))
        {
            return S_OK;
        }
        else
        {
            sem_wait(sem);

            BMDTimeValue duration;

            uint8_t* frameData;
            videoFrame->GetBytes(reinterpret_cast<void**>(&frameData));
            videoFrame->GetStreamTime(&timestamp, &duration, timeScale);

            long frameWidth = videoFrame->GetWidth();
            long frameHeight = videoFrame->GetHeight();
            uint32_t stride = static_cast<uint32_t>(videoFrame->GetRowBytes());

            uint32_t offset = 0;

            memcpy(sharedMemory->videoData + offset, &timestamp, sizeof(timestamp));
            offset += sizeof(timestamp);

            memcpy(sharedMemory->videoData + offset, &duration, sizeof(timestamp));
            offset += sizeof(duration);

            memcpy(sharedMemory->videoData + offset, &frameWidth, sizeof(frameWidth));
            offset += sizeof(frameWidth);

            memcpy(sharedMemory->videoData + offset, &frameHeight, sizeof(frameHeight));
            offset += sizeof(frameHeight);

            memcpy(sharedMemory->videoData + offset, &stride, sizeof(stride));
            offset += sizeof(stride);

            uint32_t dataSize = static_cast<uint32_t>(frameHeight) * stride;

            memcpy(sharedMemory->videoData + offset, frameData, dataSize);
            offset += sizeof(timestamp);

            sem_post(sem);
        }
    }

    if (audioFrame)
    {
        sem_wait(sem);

        uint8_t* frameData;

        audioFrame->GetBytes(reinterpret_cast<void**>(&frameData));
        audioFrame->GetPacketTime(&timestamp, audioSampleRate);

        long sampleFrameCount = audioFrame->GetSampleFrameCount();

        uint32_t dataSize = static_cast<uint32_t>(sampleFrameCount) * audioChannels * (audioSampleDepth / 8);

        uint32_t offset = 0;

        memcpy(sharedMemory->audioData + offset, &timestamp, sizeof(timestamp));
        offset += sizeof(timestamp);

        memcpy(sharedMemory->audioData + offset, &sampleFrameCount, sizeof(sampleFrameCount));
        offset += sizeof(sampleFrameCount);

        memcpy(sharedMemory->audioData + offset, frameData, dataSize);
        offset += sizeof(timestamp);

        sem_post(sem);
    }

    return S_OK;
}

void BMDMemory::writeMetaData()
{
    sem_wait(sem);

    uint32_t offset = 0;

    memcpy(sharedMemory->metaData + offset, &pixelFormat, sizeof(pixelFormat));
    offset += sizeof(pixelFormat);

    memcpy(sharedMemory->metaData + offset, &width, sizeof(width));
    offset += sizeof(width);

    memcpy(sharedMemory->metaData + offset, &height, sizeof(height));
    offset += sizeof(height);

    memcpy(sharedMemory->metaData + offset, &frameDuration, sizeof(frameDuration)); // numerator
    offset += sizeof(frameDuration);

    memcpy(sharedMemory->metaData + offset, &timeScale, sizeof(timeScale)); // denumerator
    offset += sizeof(timeScale);

    memcpy(sharedMemory->metaData + offset, &fieldDominance, sizeof(fieldDominance));
    offset += sizeof(fieldDominance);

    memcpy(sharedMemory->metaData + offset, &audioSampleRate, sizeof(audioSampleRate));
    offset += sizeof(audioSampleRate);

    memcpy(sharedMemory->metaData + offset, &audioSampleDepth, sizeof(audioSampleDepth));
    offset += sizeof(audioSampleDepth);

    memcpy(sharedMemory->metaData + offset, &audioChannels, sizeof(audioChannels));
    offset += sizeof(audioChannels);

    sem_post(sem);
}
