//
//  BMD memory
//

#pragma once

#include <sys/mman.h>
#include "DeckLinkAPI.h"

class InputCallback;

class BMDMemory
{
public:
    BMDMemory(const std::string& pName,
              int32_t pInstance,
              int32_t pVideoMode,
              int32_t pVideoConnection,
              int32_t pVideoFormat,
              int32_t pAudioConnection);
    virtual ~BMDMemory();

    bool run();

protected:
    bool videoInputFormatChanged(BMDVideoInputFormatChangedEvents,
                                 IDeckLinkDisplayMode* newDisplayMode,
                                 BMDDetectedVideoInputFormatFlags);

    bool videoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame,
                                IDeckLinkAudioInputPacket* audioFrame);
    
    void writeMetaData();

    std::string name;
    int32_t instance = 0;
    int32_t videoMode = 0;
    int32_t videoConnection = 0;
    int32_t videoFormat = 0;
    int32_t audioConnection = 0;

    int sharedMemoryFd = -1;
    void* sharedMemory = MAP_FAILED;
    const uint32_t sharedMemorySize = 128 * 1024 * 1024; // 128 MiB

    const uint32_t headerSize;

    uint32_t currentMetaDataOffset = 0;
    uint32_t currentVideoDataOffset = 0;
    uint32_t currentAudioDataOffset = 0;

    const uint32_t metaDataOffset;
    const uint32_t metaDataSize;

    const uint32_t videoDataOffset;
    const uint32_t videoDataSize;

    const uint32_t audioDataOffset;
    const uint32_t audioDataSize;

    InputCallback* inputCallback = nullptr;

    IDeckLink* deckLink = nullptr;
    IDeckLinkInput* deckLinkInput = nullptr;
    IDeckLinkDisplayModeIterator* displayModeIterator = nullptr;
    IDeckLinkDisplayMode* displayMode = nullptr;
    IDeckLinkConfiguration* deckLinkConfiguration = nullptr;

    BMDDisplayMode selectedDisplayMode = bmdModeNTSC;
    BMDPixelFormat pixelFormat = bmdFormat8BitYUV;
    long width;
    long height;
    BMDTimeValue frameDuration = 0;
    BMDTimeScale timeScale = 0;
    BMDFieldDominance fieldDominance;

    BMDAudioSampleRate audioSampleRate = bmdAudioSampleRate48kHz;
    BMDAudioSampleType audioSampleDepth = bmdAudioSampleType16bitInteger;
    uint32_t audioChannels = 2;
};
