//
//  BMD memory
//

#include <cstdlib>
#include <cstring>
#include <thread>
#include <iostream>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "BMDMemory.h"

static void signalHandler(int signo)
{
    switch(signo)
    {
        case SIGTERM:
            // shutdown the server
            exit(EXIT_SUCCESS);
            break;
    }
}

static int daemonize(const char* lock_file)
{
    // drop to having init() as parent
    int i, lfp, pid = fork();
    char str[256] = {0};
    if (pid < 0)
    {
        std::cerr << "Failed to fork process" << std::endl;
        exit(EXIT_FAILURE);
    }
    if (pid > 0) exit(EXIT_SUCCESS); // parent process

    setsid();

    for (i = getdtablesize(); i>=0; i--)
        close(i);

    i = open("/dev/null", O_RDWR);
    dup(i); // stdout
    dup(i); // stderr
    umask(027);

    lfp = open(lock_file, O_RDWR|O_CREAT|O_EXCL, 0640);

    if (lfp < 0)
    {
        std::cerr << "Failed to open lock file" << std::endl;
        exit(EXIT_FAILURE);
    }

    if (lockf(lfp, F_TLOCK, 0) < 0)
    {
        std::cerr << "Failed to lock the file" << std::endl;
        exit(EXIT_SUCCESS);
    }

    sprintf(str, "%d\n", getpid());
    write(lfp, str, strlen(str)); // record pid to lockfile

    // ignore child terminate signal
    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
    {
        std::cerr << "Failed to ignore SIGCHLD" << std::endl;
        exit(EXIT_FAILURE);
    }

    // hangup signal
    if (signal(SIGHUP, signalHandler) == SIG_ERR)
    {
        std::cerr << "Failed to capure SIGHUP" << std::endl;
        exit(EXIT_FAILURE);
    }

    // software termination signal from kill
    if (signal(SIGTERM, signalHandler) == SIG_ERR)
    {
        std::cerr << "Failed to capure SIGTERM" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "Daemon started, pid: " << getpid() << std::endl;
    
    return EXIT_SUCCESS;
}

int main(int argc, const char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Too few arguments" << std::endl;

        const char* exe = argc >= 1 ? argv[0] : "bmdmemory";
        std::cerr << "Usage: " << exe << " <name> [--instance=<instance>] [--video_mode <video mode>] [--video_connection <video connection>] [--video_format <video format>] [--audio_connection <audio connection>] [--memory_size <memory size>] [--daemon]" << std::endl;

        return 1;
    }

    std::string name = argv[1];

    int32_t instance = 0;
    int32_t videoMode = 0;
    int32_t videoConnection = 0;
    int32_t videoFormat = 0;
    int32_t audioConnection = 0;
    bool daemon = false;

    for (int i = 2; i < argc; ++i)
    {
        if (strcmp(argv[i], "--instance") == 0)
        {
            if (++i < argc)
                instance = atoi(argv[i]);
            else
                std::cerr << "Invalid argument" << std::endl;
        }
        else if (strcmp(argv[i], "--video_mode") == 0)
        {
            if (++i < argc)
                videoMode = atoi(argv[i]);
            else
                std::cerr << "Invalid argument" << std::endl;
        }
        else if (strcmp(argv[i], "--video_connection") == 0)
        {
            if (++i < argc)
                videoConnection = atoi(argv[i]);
            else
                std::cerr << "Invalid argument" << std::endl;
        }
        else if (strcmp(argv[i], "--video_format") == 0)
        {
            if (++i < argc)
                videoFormat = atoi(argv[i]);
            else
                std::cerr << "Invalid argument" << std::endl;
        }
        else if (strcmp(argv[i], "--audio_connection") == 0)
        {
            if (++i < argc)
                audioConnection = atoi(argv[i]);
            else
                std::cerr << "Invalid argument" << std::endl;
        }
        else if (strcmp(argv[i], "--daemon") == 0)
        {
            daemon = true;
        }
    }

    if (daemon && daemonize("/var/run/bmdmemory.pid") == -1)
    {
        std::cerr << "Failed to start daemon" << std::endl;
        return EXIT_FAILURE;
    }

    BMDMemory bmdMemory(name,
                        instance,
                        videoMode,
                        videoConnection,
                        videoFormat,
                        audioConnection);

    if (!bmdMemory.run())
    {
        return EXIT_FAILURE;
    }

    for (;;)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return EXIT_SUCCESS;
}
