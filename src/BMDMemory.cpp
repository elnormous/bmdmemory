//
//  BMD memory
//

#include <functional>
#include <mutex>
#include <iostream>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <limits.h>
#include <cstring>
#include "BMDMemory.h"

static const uint32_t MEMORY_SIZE = 64 * 1024 * 1024; // 64 MiB
static const uint32_t METADATA_OFFSET = NAME_MAX + 1;
static const uint32_t VIDEO_OFFSET = METADATA_OFFSET + 128;
static const uint32_t AUDIO_OFFSET = VIDEO_OFFSET + 40 * 1024 * 1024; // 40 MiB

class InputCallback:public IDeckLinkInputCallback
{
public:
    InputCallback(const std::function<bool(BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode*, BMDDetectedVideoInputFormatFlags)>& pVideoInputFormatChangeCallback,
                  const std::function<bool(IDeckLinkVideoInputFrame*, IDeckLinkAudioInputPacket*)>& pVideoInputFrameArriveCallback):
        videoInputFormatChangeCallback(pVideoInputFormatChangeCallback), videoInputFrameArriveCallback(pVideoInputFrameArriveCallback)
    {
    }

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, LPVOID*) { return E_NOINTERFACE; }

    virtual ULONG STDMETHODCALLTYPE AddRef()
    {
        std::lock_guard<std::mutex> lock(dataMutex);

        refCount++;
        
        return refCount;
    }

    virtual ULONG STDMETHODCALLTYPE Release()
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

    virtual HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(BMDVideoInputFormatChangedEvents changeEvents, IDeckLinkDisplayMode* newDisplayMode,
                                                              BMDDetectedVideoInputFormatFlags formatFlags)
    {
        if (!videoInputFormatChangeCallback(changeEvents, newDisplayMode, formatFlags))
        {
            return S_FALSE;
        }

        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame,
                                                             IDeckLinkAudioInputPacket* audioFrame)
    {
        if (!videoInputFrameArriveCallback(videoFrame, audioFrame))
        {
            return S_FALSE;
        }

        return S_OK;
    }

private:
    ULONG refCount;
    std::mutex dataMutex;

    std::function<bool(BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode*, BMDDetectedVideoInputFormatFlags)> videoInputFormatChangeCallback;
    std::function<bool(IDeckLinkVideoInputFrame*, IDeckLinkAudioInputPacket*)> videoInputFrameArriveCallback;
};

BMDMemory::BMDMemory(const std::string& pName,
                     int32_t pInstance,
                     int32_t pVideoMode,
                     int32_t pVideoConnection,
                     int32_t pVideoFormat,
                     int32_t pAudioConnection):
    name(pName),
    instance(pInstance),
    videoMode(pVideoMode),
    videoConnection(pVideoConnection),
    videoFormat(pVideoFormat),
    audioConnection(pAudioConnection)
{
    semName = name + "_sem";
}

BMDMemory::~BMDMemory()
{
    if (inputCallback) inputCallback->Release();

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
        if (munmap(sharedMemory, MEMORY_SIZE) == -1)
        {
            std::cerr << "Failed to unmap shared memory\n";
        }
    }

    if (shm_unlink(name.c_str()) == -1)
    {
        std::cerr << "Failed to delete shared memory\n";
    }
}

bool BMDMemory::run()
{
    sem_unlink(semName.c_str());
    shm_unlink(name.c_str());

    if ((sharedMemoryFd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR , S_IRUSR | S_IWUSR)) == -1)
    {
        std::cerr << "Failed to create shared memory\n";
        return false;
    }

    if (ftruncate(sharedMemoryFd, MEMORY_SIZE) == -1)
    {
        std::cerr << "Failed to resize shared memory\n";
        return false;
    }

    sharedMemory = mmap(nullptr, MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, sharedMemoryFd, 0);

    if (sharedMemory == MAP_FAILED)
    {
        std::cerr << "Failed to map shared memory\n";
        return false;
    }

    memset(sharedMemory, 0, MEMORY_SIZE);

    // copy the name of the semaphore
    memcpy(sharedMemory, semName.c_str(), semName.length());

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

    int instanceCount = 0;

    while ((result = deckLinkIterator->Next(&deckLink)) == S_OK)
    {
        if (instance == instanceCount)
        {
            // found instance
            break;
        }
        else
        {
            //deckLink->Release();
            instanceCount++;
        }
    }

    if (result != S_OK)
    {
        std::cerr << "Failed to get DeckLink PCI card\n";
        return false;
    }

    if (instance != instanceCount ||
        !deckLink)
    {
        std::cerr << "DeckLink PCI card not found\n";
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

    switch (audioConnection)
    {
        case 1:
            result = deckLinkConfiguration->SetInt(bmdDeckLinkConfigAudioInputConnection,
                                                   bmdAudioConnectionAnalog);
            break;
        case 2:
            result = deckLinkConfiguration->SetInt(bmdDeckLinkConfigAudioInputConnection,
                                                   bmdAudioConnectionEmbedded);
            break;
        default:
            // do not change it
            break;
    }

    if (result != S_OK)
    {
        std::cerr << "Failed to set declick audio configuration" << "\n";
        return false;
    }

    switch (videoConnection)
    {
        case 1:
            result = deckLinkConfiguration->SetInt(bmdDeckLinkConfigVideoInputConnection,
                                                   bmdVideoConnectionComposite);
            break;
        case 2:
            result = deckLinkConfiguration->SetInt(bmdDeckLinkConfigVideoInputConnection,
                                                   bmdVideoConnectionComponent);
            break;
        case 3:
            result = deckLinkConfiguration->SetInt(bmdDeckLinkConfigVideoInputConnection,
                                                   bmdVideoConnectionHDMI);
            break;
        case 4:
            result = deckLinkConfiguration->SetInt(bmdDeckLinkConfigVideoInputConnection,
                                                   bmdVideoConnectionSDI);
            break;
        default:
            // do not change it
            break;
    }
    
    if (result != S_OK)
    {
        std::cerr << "Failed to set declick video configuration" << "\n";
        return false;
    }

    inputCallback = new InputCallback(std::bind(&BMDMemory::videoInputFormatChanged, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                                      std::bind(&BMDMemory::videoInputFrameArrived, this, std::placeholders::_1, std::placeholders::_2));

    deckLinkInput->SetCallback(inputCallback);

    result = deckLinkInput->GetDisplayModeIterator(&displayModeIterator);
    if (result != S_OK)
    {
        std::cerr << "Failed to obtain the video output display mode iterator - result = " << result << "\n";
        return false;
    }

    int displayModeCount = 0;

    while ((result = displayModeIterator->Next(&displayMode)) == S_OK)
    {
        if (videoMode == displayModeCount)
        {
            // found video mode
            break;
        }
        else
        {
            displayMode->Release();
            displayModeCount++;
        }
    }

    if (result != S_OK)
    {
        std::cerr << "Failed to get display mode\n";
        return false;
    }

    if (displayModeCount != videoMode ||
        !displayMode)
    {
        std::cerr << "Failed to find display mode\n";
        return false;
    }

    selectedDisplayMode = displayMode->GetDisplayMode();

    width = displayMode->GetWidth();
    height = displayMode->GetHeight();
    displayMode->GetFrameRate(&frameDuration, &timeScale);
    fieldDominance = displayMode->GetFieldDominance();

    std::cout << "width: " << width << ", height: " << height << ", frameDuration: " << frameDuration << ", timeScale: " << timeScale << "\n";

    metaData = static_cast<uint8_t*>(sharedMemory) + METADATA_OFFSET;
    videoData = static_cast<uint8_t*>(sharedMemory) + VIDEO_OFFSET;
    audioData = static_cast<uint8_t*>(sharedMemory) + AUDIO_OFFSET;

    switch (videoFormat)
    {
        case 0: pixelFormat = bmdFormat8BitYUV; break;
        case 1: pixelFormat = bmdFormat10BitYUV; break;
        case 2: pixelFormat = bmdFormat8BitARGB; break;
        case 3: pixelFormat = bmdFormat10BitRGB; break;
        case 4: pixelFormat = bmdFormat8BitBGRA; break;
    }

    result = deckLinkInput->EnableVideoInput(selectedDisplayMode, pixelFormat, 0);
    if (result != S_OK)
    {
        std::cerr << "Failed to enable video input\n";
        return false;
    }

    result = deckLinkInput->EnableAudioInput(audioSampleRate,
                                             audioSampleDepth,
                                             audioChannels);

    std::cout << "audioSampleRate: " << audioSampleRate << ", audioSampleDepth: " << audioSampleDepth << ", audioChannels: " << audioChannels << "\n";

    if (result != S_OK)
    {
        std::cerr << "Failed to enable audio input\n";
        return false;
    }

    writeMetaData();

    result = deckLinkInput->StartStreams();
    if (result != S_OK)
    {
        std::cerr << "Failed to start streaming\n";
        return false;
    }

    std::cout << "Streaming started\n";

    return true;
}

void BMDMemory::writeMetaData()
{
    sem_wait(sem);

    uint32_t outPixelFormat = 0;

    switch (pixelFormat)
    {
        case bmdFormat8BitYUV: outPixelFormat = 0; break;
        case bmdFormat10BitYUV: outPixelFormat = 1; break;
        case bmdFormat8BitARGB: outPixelFormat = 2; break;
        case bmdFormat10BitRGB: outPixelFormat = 3; break;
        case bmdFormat12BitRGB: outPixelFormat = 4; break;
        case bmdFormat12BitRGBLE: outPixelFormat = 5; break;
        case bmdFormat10BitRGBXLE: outPixelFormat = 6; break;
        case bmdFormat10BitRGBX: outPixelFormat = 7; break;
    }

    uint32_t outWidth = static_cast<uint32_t>(width);
    uint32_t outHeight = static_cast<uint32_t>(height);

    uint32_t outFrameDuration = static_cast<uint32_t>(frameDuration);
    uint32_t outTimeScale = static_cast<uint32_t>(timeScale);

    uint32_t outFieldDominance = 0;

    switch (fieldDominance)
    {
        case bmdUnknownFieldDominance: outFieldDominance = 0; break;
        case bmdLowerFieldFirst: outFieldDominance = 1; break;
        case bmdUpperFieldFirst: outFieldDominance = 2; break;
        case bmdProgressiveFrame: outFieldDominance = 3; break;
        case bmdProgressiveSegmentedFrame: outFieldDominance = 4; break;
    }

    uint32_t outAudioSampleRate = audioSampleRate;
    uint32_t outAudioSampleDepth = audioSampleDepth;
    uint32_t outAudioChannels = audioChannels;

    uint32_t offset = 0;

    memcpy(metaData + offset, &outPixelFormat, sizeof(outPixelFormat));
    offset += sizeof(outPixelFormat);

    memcpy(metaData + offset, &outWidth, sizeof(outWidth));
    offset += sizeof(outWidth);

    memcpy(metaData + offset, &outHeight, sizeof(outHeight));
    offset += sizeof(outHeight);

    memcpy(metaData + offset, &outFrameDuration, sizeof(outFrameDuration)); // numerator
    offset += sizeof(outFrameDuration);

    memcpy(metaData + offset, &outTimeScale, sizeof(outTimeScale)); // denumerator
    offset += sizeof(outTimeScale);

    memcpy(metaData + offset, &outFieldDominance, sizeof(outFieldDominance));
    offset += sizeof(outFieldDominance);

    memcpy(metaData + offset, &outAudioSampleRate, sizeof(outAudioSampleRate));
    offset += sizeof(outAudioSampleRate);

    memcpy(metaData + offset, &outAudioSampleDepth, sizeof(outAudioSampleDepth));
    offset += sizeof(outAudioSampleDepth);

    memcpy(metaData + offset, &outAudioChannels, sizeof(outAudioChannels));
    offset += sizeof(outAudioChannels);

    sem_post(sem);
}

bool BMDMemory::videoInputFormatChanged(BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode* newDisplayMode,
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

bool BMDMemory::videoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame,
                                       IDeckLinkAudioInputPacket* audioFrame)
{
    if (videoFrame)
    {
        if (videoFrame->GetFlags() & static_cast<BMDFrameFlags>(bmdFrameHasNoInputSource))
        {
            return S_OK;
        }
        else
        {
            sem_wait(sem);

            void* frameData;
            videoFrame->GetBytes(&frameData);

            BMDTimeValue duration;
            BMDTimeValue timestamp;
            videoFrame->GetStreamTime(&timestamp, &duration, timeScale);

            uint32_t outDuration = static_cast<uint32_t>(duration);
            uint64_t outTimestamp = static_cast<uint64_t>(timestamp);

            uint32_t frameWidth = static_cast<uint32_t>(videoFrame->GetWidth());
            uint32_t frameHeight = static_cast<uint32_t>(videoFrame->GetHeight());
            uint32_t stride = static_cast<uint32_t>(videoFrame->GetRowBytes());

            uint32_t offset = 0;

            memcpy(videoData + offset, &outDuration, sizeof(outDuration));
            offset += sizeof(outDuration);

            memcpy(videoData + offset, &outTimestamp, sizeof(outTimestamp));
            offset += sizeof(outTimestamp);

            memcpy(videoData + offset, &frameWidth, sizeof(frameWidth));
            offset += sizeof(frameWidth);

            memcpy(videoData + offset, &frameHeight, sizeof(frameHeight));
            offset += sizeof(frameHeight);

            memcpy(videoData + offset, &stride, sizeof(stride));
            offset += sizeof(stride);

            uint32_t dataSize = frameHeight * stride;
            memcpy(videoData + offset, &dataSize, sizeof(dataSize));
            offset += sizeof(dataSize);

            memcpy(videoData + offset, frameData, dataSize);

            sem_post(sem);
        }
    }

    if (audioFrame)
    {
        sem_wait(sem);

        void* frameData;

        audioFrame->GetBytes(&frameData);

        BMDTimeValue timestamp;
        audioFrame->GetPacketTime(&timestamp, audioSampleRate);

        uint64_t outTimestamp = static_cast<uint64_t>(timestamp);
        uint32_t sampleFrameCount = static_cast<uint32_t>(audioFrame->GetSampleFrameCount());

        uint32_t offset = 0;

        memcpy(audioData + offset, &outTimestamp, sizeof(outTimestamp));
        offset += sizeof(outTimestamp);

        memcpy(audioData + offset, &sampleFrameCount, sizeof(sampleFrameCount));
        offset += sizeof(sampleFrameCount);

        uint32_t dataSize = sampleFrameCount * audioChannels * (audioSampleDepth / 8);
        memcpy(audioData + offset, &dataSize, sizeof(dataSize));
        offset += sizeof(dataSize);

        memcpy(audioData + offset, frameData, dataSize);

        sem_post(sem);
    }

    return S_OK;
}
