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
    BMDMemory(const std::string& pName,
              int32_t pInstance,
              int32_t pVideoMode,
              int32_t pVideoConnection,
              int32_t pVideoFormat,
              int32_t pAudioConnection);
    virtual ~BMDMemory();

    bool run();

protected:
    bool videoInputFormatChanged(BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode* newDisplayMode,
                                 BMDDetectedVideoInputFormatFlags);

    bool videoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame,
                                IDeckLinkAudioInputPacket* audioFrame);
    
    void writeMetaData();

    sem_t* sem = SEM_FAILED;

    std::string name;
    int32_t instance = 0;
    int32_t videoMode = 0;
    int32_t videoConnection = 0;
    int32_t videoFormat = 0;
    int32_t audioConnection = 0;

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
