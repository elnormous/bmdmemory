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

class InputCallback:public IDeckLinkInputCallback
{
public:
    InputCallback(const std::function<bool(BMDVideoInputFormatChangedEvents,
                                           IDeckLinkDisplayMode*,
                                           BMDDetectedVideoInputFormatFlags)>& pVideoInputFormatChangeCallback,
                  const std::function<bool(IDeckLinkVideoInputFrame*, IDeckLinkAudioInputPacket*)>& pVideoInputFrameArriveCallback):
        videoInputFormatChangeCallback(pVideoInputFormatChangeCallback),
        videoInputFrameArriveCallback(pVideoInputFrameArriveCallback)
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

    virtual HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(BMDVideoInputFormatChangedEvents changeEvents,
                                                              IDeckLinkDisplayMode* newDisplayMode,
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
    audioConnection(pAudioConnection),
    headerSize(sizeof(currentMetaDataOffset) + sizeof(currentVideoDataOffset) + sizeof(currentAudioDataOffset)),
    metaDataOffset(headerSize),
    metaDataSize(100), // 100 bytes
    videoDataOffset(metaDataOffset + metaDataSize),
    videoDataSize(80 * 1024 * 1024), // 80 MiB
    audioDataOffset(videoDataOffset + videoDataSize),
    audioDataSize(sharedMemorySize - audioDataOffset)
{
}

BMDMemory::~BMDMemory()
{
    if (inputCallback) inputCallback->Release();

    if (deckLinkConfiguration) deckLinkConfiguration->Release();
    if (displayMode) displayMode->Release();
    if (displayModeIterator) displayModeIterator->Release();
    if (deckLinkInput) deckLinkInput->Release();
    if (deckLink)
    {
        deckLinkInput->StopStreams();
        deckLinkInput->DisableVideoInput();
        deckLinkInput->DisableAudioInput();
        deckLink->Release();
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
        if (munmap(sharedMemory, sharedMemorySize) == -1)
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
    shm_unlink(name.c_str());

    if ((sharedMemoryFd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR , S_IRUSR | S_IWUSR)) == -1)
    {
        std::cerr << "Failed to create shared memory\n";
        return false;
    }

    if (ftruncate(sharedMemoryFd, sharedMemorySize) == -1)
    {
        std::cerr << "Failed to resize shared memory\n";
        return false;
    }

    sharedMemory = mmap(nullptr, sharedMemorySize, PROT_READ | PROT_WRITE, MAP_SHARED, sharedMemoryFd, 0);

    if (sharedMemory == MAP_FAILED)
    {
        std::cerr << "Failed to map shared memory\n";
        return false;
    }

    // fille header with zeros
    memset(sharedMemory, 0, headerSize);

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

    BMDDisplayMode bmdDisplayMode = 0;

    switch (videoMode)
    {
        case 0: bmdDisplayMode = bmdModeNTSC; break;
        case 1: bmdDisplayMode = bmdModeNTSC2398; break;
        case 2: bmdDisplayMode = bmdModePAL; break;
        case 14: bmdDisplayMode = bmdModeNTSCp; break;
        case 15: bmdDisplayMode = bmdModePALp; break;

        case 3: bmdDisplayMode = bmdModeHD1080p2398; break;
        case 4: bmdDisplayMode = bmdModeHD1080p24; break;
        case 5: bmdDisplayMode = bmdModeHD1080p25; break;
        case 6: bmdDisplayMode = bmdModeHD1080p2997; break;
        case 7: bmdDisplayMode = bmdModeHD1080p30; break;
        case 8: bmdDisplayMode = bmdModeHD1080i50; break;
        case 9: bmdDisplayMode = bmdModeHD1080i5994; break;
        case 10: bmdDisplayMode = bmdModeHD1080i6000; break;
        case 16: bmdDisplayMode = bmdModeHD1080p50; break;
        case 17: bmdDisplayMode = bmdModeHD1080p5994; break;
        case 18: bmdDisplayMode = bmdModeHD1080p6000; break;

        case 11: bmdDisplayMode = bmdModeHD720p50; break;
        case 12: bmdDisplayMode = bmdModeHD720p5994; break;
        case 13: bmdDisplayMode = bmdModeHD720p60; break;

        case 19: bmdDisplayMode = bmdMode2k2398; break;
        case 20: bmdDisplayMode = bmdMode2k24; break;
        case 21: bmdDisplayMode = bmdMode2k25; break;

        case 22: bmdDisplayMode = bmdMode2kDCI2398; break;
        case 23: bmdDisplayMode = bmdMode2kDCI24; break;
        case 24: bmdDisplayMode = bmdMode2kDCI25; break;

        case 25: bmdDisplayMode = bmdMode4K2160p2398; break;
        case 26: bmdDisplayMode = bmdMode4K2160p24; break;
        case 27: bmdDisplayMode = bmdMode4K2160p25; break;
        case 28: bmdDisplayMode = bmdMode4K2160p2997; break;
        case 29: bmdDisplayMode = bmdMode4K2160p30; break;
        case 30: bmdDisplayMode = bmdMode4K2160p50; break;
        case 31: bmdDisplayMode = bmdMode4K2160p5994; break;
        case 32: bmdDisplayMode = bmdMode4K2160p60; break;

        case 33: bmdDisplayMode = bmdMode4kDCI2398; break;
        case 34: bmdDisplayMode = bmdMode4kDCI24; break;
        case 35: bmdDisplayMode = bmdMode4kDCI25; break;

        default: bmdDisplayMode = bmdModeUnknown; break;
    }

    while ((result = displayModeIterator->Next(&displayMode)) == S_OK)
    {
        if (bmdDisplayMode == bmdModeUnknown ||
            bmdDisplayMode == displayMode->GetDisplayMode())
        {
            // found video mode
            break;
        }
        else
        {
            displayMode->Release();
        }
    }

    if (result != S_OK)
    {
        std::cerr << "Failed to get display mode\n";
        return false;
    }

    selectedDisplayMode = displayMode->GetDisplayMode();

    width = displayMode->GetWidth();
    height = displayMode->GetHeight();
    displayMode->GetFrameRate(&frameDuration, &timeScale);
    fieldDominance = displayMode->GetFieldDominance();

    std::cout << "width: " << width << ", height: " << height << ", frameDuration: " << frameDuration << ", timeScale: " << timeScale << "\n";

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

    if (sizeof(outPixelFormat) +
        sizeof(outWidth) +
        sizeof(outHeight) +
        sizeof(outFrameDuration) +
        sizeof(outTimeScale) +
        sizeof(outFieldDominance) +
        sizeof(outAudioSampleRate) +
        sizeof(outAudioSampleDepth) +
        sizeof(outAudioChannels) +
        currentMetaDataOffset > metaDataSize ||
        currentMetaDataOffset < metaDataOffset)
    {
        currentMetaDataOffset = metaDataOffset;
    }

    uint32_t offset = currentMetaDataOffset;

    memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, &outPixelFormat, sizeof(outPixelFormat));
    offset += sizeof(outPixelFormat);

    memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, &outWidth, sizeof(outWidth));
    offset += sizeof(outWidth);

    memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, &outHeight, sizeof(outHeight));
    offset += sizeof(outHeight);

    memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, &outFrameDuration, sizeof(outFrameDuration)); // numerator
    offset += sizeof(outFrameDuration);

    memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, &outTimeScale, sizeof(outTimeScale)); // denumerator
    offset += sizeof(outTimeScale);

    memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, &outFieldDominance, sizeof(outFieldDominance));
    offset += sizeof(outFieldDominance);

    memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, &outAudioSampleRate, sizeof(outAudioSampleRate));
    offset += sizeof(outAudioSampleRate);

    memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, &outAudioSampleDepth, sizeof(outAudioSampleDepth));
    offset += sizeof(outAudioSampleDepth);

    memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, &outAudioChannels, sizeof(outAudioChannels));
    offset += sizeof(outAudioChannels);

    uint32_t* currentOffset = &reinterpret_cast<uint32_t*>(sharedMemory)[0];

    if (currentMetaDataOffset > *currentOffset)
    {
        __sync_add_and_fetch(currentOffset, currentMetaDataOffset - *currentOffset);
    }
    else
    {
        __sync_sub_and_fetch(currentOffset, *currentOffset - currentMetaDataOffset);
    }

    currentMetaDataOffset = offset;
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
    if (videoFrame && (videoFrame->GetFlags() & static_cast<BMDFrameFlags>(bmdFrameHasNoInputSource)) == 0)
    {
        void* frameData;
        videoFrame->GetBytes(&frameData);

        BMDTimeValue duration;
        BMDTimeValue timestamp;
        videoFrame->GetStreamTime(&timestamp, &duration, timeScale);

        uint64_t outTimestamp = static_cast<uint64_t>(timestamp);
        uint32_t outDuration = static_cast<uint32_t>(duration);
        uint32_t frameWidth = static_cast<uint32_t>(videoFrame->GetWidth());
        uint32_t frameHeight = static_cast<uint32_t>(videoFrame->GetHeight());
        uint32_t stride = static_cast<uint32_t>(videoFrame->GetRowBytes());
        uint32_t dataSize = frameHeight * stride;

        if (sizeof(outTimestamp) +
            sizeof(outDuration) +
            sizeof(frameWidth) +
            sizeof(frameHeight) +
            sizeof(stride) +
            sizeof(dataSize) +
            dataSize +
            headerSize +
            currentVideoDataOffset > videoDataSize ||
            currentVideoDataOffset < videoDataOffset)
        {
            currentVideoDataOffset = videoDataOffset;
        }

        uint32_t offset = currentVideoDataOffset;

        memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, &outTimestamp, sizeof(outTimestamp));
        offset += sizeof(outTimestamp);

        memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, &outDuration, sizeof(outDuration));
        offset += sizeof(outDuration);

        memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, &frameWidth, sizeof(frameWidth));
        offset += sizeof(frameWidth);

        memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, &frameHeight, sizeof(frameHeight));
        offset += sizeof(frameHeight);

        memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, &stride, sizeof(stride));
        offset += sizeof(stride);

        memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, &dataSize, sizeof(dataSize));
        offset += sizeof(dataSize);

        memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, frameData, dataSize);
        offset += dataSize;

        uint32_t* currentOffset = &reinterpret_cast<uint32_t*>(sharedMemory)[1];

        if (currentVideoDataOffset > *currentOffset)
        {
            __sync_add_and_fetch(currentOffset, currentVideoDataOffset - *currentOffset);
        }
        else
        {
            __sync_sub_and_fetch(currentOffset, *currentOffset - currentVideoDataOffset);
        }

        currentVideoDataOffset = offset;
    }

    if (audioFrame)
    {
        void* frameData;

        audioFrame->GetBytes(&frameData);

        BMDTimeValue timestamp;
        audioFrame->GetPacketTime(&timestamp, audioSampleRate);

        uint64_t outTimestamp = static_cast<uint64_t>(timestamp);
        uint32_t sampleFrameCount = static_cast<uint32_t>(audioFrame->GetSampleFrameCount());
        uint32_t dataSize = sampleFrameCount * audioChannels * (audioSampleDepth / 8);

        if (sizeof(outTimestamp) +
            sizeof(sampleFrameCount) +
            sizeof(dataSize) +
            dataSize +
            currentAudioDataOffset > audioDataSize ||
            currentAudioDataOffset < audioDataOffset)
        {
            currentAudioDataOffset = audioDataOffset;
        }

        uint32_t offset = currentAudioDataOffset;

        memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, &outTimestamp, sizeof(outTimestamp));
        offset += sizeof(outTimestamp);

        memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, &sampleFrameCount, sizeof(sampleFrameCount));
        offset += sizeof(sampleFrameCount);

        memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, &dataSize, sizeof(dataSize));
        offset += sizeof(dataSize);

        memcpy(reinterpret_cast<uint8_t*>(sharedMemory) + offset, frameData, dataSize);
        offset += dataSize;

        uint32_t* currentOffset = &reinterpret_cast<uint32_t*>(sharedMemory)[2];

        if (currentAudioDataOffset > *currentOffset)
        {
            __sync_add_and_fetch(currentOffset, currentAudioDataOffset - *currentOffset);
        }
        else
        {
            __sync_sub_and_fetch(currentOffset, *currentOffset - currentAudioDataOffset);
        }

        currentAudioDataOffset = offset;
    }

    return true;
}
