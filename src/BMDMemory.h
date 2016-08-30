//
//  BMD memory
//

#pragma once

#include <sys/mman.h>
#include <semaphore.h>
#include "DeckLinkAPI.h"

class BMDMemory: public IDeckLinkInputCallback
{
public:
    BMDMemory(const std::string& pName);
    virtual ~BMDMemory();

    bool run(int32_t videoMode);

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, LPVOID*) { return E_NOINTERFACE; }
    virtual ULONG STDMETHODCALLTYPE AddRef();
    virtual ULONG STDMETHODCALLTYPE  Release();
    virtual HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(BMDVideoInputFormatChangedEvents events, IDeckLinkDisplayMode* newDisplayMode, BMDDetectedVideoInputFormatFlags flags);
    virtual HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame,
                                                             IDeckLinkAudioInputPacket* audioFrame);

protected:
    void writeMetaData();

    struct Memory
    {
        sem_t sem;
        uint8_t metaData[128];
        uint8_t videoData[1024 * 1024 * 40]; // 40MiB
        uint8_t audioData[1024 * 1024 * 40]; // 40MiB
    };

    std::string name;
    int sharedMemoryFd = -1;
    Memory* sharedMemory = reinterpret_cast<Memory*>(MAP_FAILED);

    ULONG refCount;
    std::mutex dataMutex;

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
    BMDTimeValue timeScale = 0;
    BMDFieldDominance fieldDominance;

    BMDAudioSampleRate audioSampleRate = bmdAudioSampleRate48kHz;
    BMDAudioSampleType audioSampleDepth = bmdAudioSampleType16bitInteger;
    uint32_t audioChannels = 2;
};
