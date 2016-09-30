//
//  BMD memory
//

#include <thread>
#include <iostream>
#include <cstring>
#include "BMDMemory.h"

int main(int argc, const char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Too few arguments" << std::endl;

        const char* exe = argc >= 1 ? argv[0] : "bmdmemory";
        std::cerr << "Usage: " << exe << " <name> [-instance=<instance>] [-video_mode <video mode>] [-video_connection <video connection>] [-video_format <video format>] [-audio_connection <audio connection>] [-memory_size <memory size>]" << std::endl;

        return 1;
    }

    std::string name = argv[1];

    int32_t instance = 0;
    int32_t videoMode = 0;
    int32_t videoConnection = 0;
    int32_t videoFormat = 0;
    int32_t audioConnection = 0;
    uint32_t sharedMemorySize = 128 * 1024 * 1024; // 128 MiB

    for (int i = 2; i < argc; ++i)
    {
        if (strcmp(argv[i], "-instance") == 0)
        {
            if (argc > i + 1)
                instance = atoi(argv[++i]);
            else
                std::cerr << "Invalid argument" << std::endl;
        }
        else if (strcmp(argv[i], "-video_mode") == 0)
        {
            if (argc > i + 1)
                videoMode = atoi(argv[++i]);
            else
                std::cerr << "Invalid argument" << std::endl;
        }
        else if (strcmp(argv[i], "-video_connection") == 0)
        {
            if (argc > i + 1)
                videoConnection = atoi(argv[++i]);
            else
                std::cerr << "Invalid argument" << std::endl;
        }
        else if (strcmp(argv[i], "-video_format") == 0)
        {
            if (argc > i + 1)
                videoFormat = atoi(argv[++i]);
            else
                std::cerr << "Invalid argument" << std::endl;
        }
        else if (strcmp(argv[i], "-audio_connection") == 0)
        {
            if (argc > i + 1)
                audioConnection = atoi(argv[++i]);
            else
                std::cerr << "Invalid argument" << std::endl;
        }
        else if (strcmp(argv[i], "-memory_size") == 0)
        {
            if (argc > i + 1)
                sharedMemorySize = static_cast<uint32_t>(atoi(argv[++i])) * 1024 * 1024;
            else
                std::cerr << "Invalid argument" << std::endl;
        }
    }

    BMDMemory bmdMemory(name,
                        instance,
                        videoMode,
                        videoConnection,
                        videoFormat,
                        audioConnection,
                        sharedMemorySize);

    if (!bmdMemory.run())
    {
        return 1;
    }

    for (;;)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}
