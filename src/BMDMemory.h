//
//  BMD memory
//

#pragma once

#include <sys/mman.h>
#include <semaphore.h>
#include "DeckLinkAPI.h"

class InputCallback;

class BMDMemory
{
public:
    BMDMemory(const std::string& pName);
    virtual ~BMDMemory();

    bool run(int32_t videoMode);

protected:
    bool videoInputFormatChanged(BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode* newDisplayMode,
                                 BMDDetectedVideoInputFormatFlags);

    bool videoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame,
                                IDeckLinkAudioInputPacket* audioFrame);
    
    void writeMetaData();

    sem_t* sem = SEM_FAILED;

    std::string name;
    std::string semName;
    int sharedMemoryFd = -1;
    void* sharedMemory = MAP_FAILED;
    uint8_t* metaData = nullptr;
    uint8_t* videoData = nullptr;
    uint8_t* audioData = nullptr;

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
