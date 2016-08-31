//
//  BMD memory
//

#include <thread>
#include <iostream>
#include "BMDMemory.h"

int main(int argc, const char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Too few arguments" << std::endl;

        const char* exe = argc >= 1 ? argv[0] : "bmdmemory";
        std::cerr << "Usage: " << exe << " <name> [video mode]" << std::endl;

        return 1;
    }

    std::string name = argv[1];

    int32_t videoMode = 1;

    if (argc >= 3)
    {
        videoMode = atoi(argv[2]);
    }

    BMDMemory bmdMemory(name);

    if (!bmdMemory.run(videoMode))
    {
        return 1;
    }

    for (;;)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}
