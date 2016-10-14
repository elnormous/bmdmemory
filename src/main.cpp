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
#include "Log.h"

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
        Log(Log::Level::ERR) << "Failed to fork process";
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
        Log(Log::Level::ERR) << "Failed to open lock file";
        exit(EXIT_FAILURE);
    }

    if (lockf(lfp, F_TLOCK, 0) < 0)
    {
        Log(Log::Level::ERR) << "Failed to lock the file";
        exit(EXIT_SUCCESS);
    }

    sprintf(str, "%d\n", getpid());
    write(lfp, str, strlen(str)); // record pid to lockfile

    // ignore child terminate signal
    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
    {
        Log(Log::Level::ERR) << "Failed to ignore SIGCHLD";
        exit(EXIT_FAILURE);
    }

    // hangup signal
    if (signal(SIGHUP, signalHandler) == SIG_ERR)
    {
        Log(Log::Level::ERR) << "Failed to capure SIGHUP";
        exit(EXIT_FAILURE);
    }

    // software termination signal from kill
    if (signal(SIGTERM, signalHandler) == SIG_ERR)
    {
        Log(Log::Level::ERR) << "Failed to capure SIGTERM";
        exit(EXIT_FAILURE);
    }

    Log(Log::Level::INFO) << "Daemon started, pid: " << getpid();
    
    return EXIT_SUCCESS;
}

static int killDaemon(const char* lockFile)
{
    char pidStr[11];
    memset(pidStr, 0, sizeof(pidStr));

    int lfp = open(lockFile, O_RDONLY);

    if (lfp == -1)
    {
        Log(Log::Level::ERR) << "Failed to open lock file";
        return 0;
    }

    read(lfp, pidStr, sizeof(pidStr));

    pid_t pid = atoi(pidStr);

    if (kill(pid, SIGTERM) != 0)
    {
        Log(Log::Level::ERR) << "Failed to kill daemon";
        return 0;
    }

    close(lfp);

    Log(Log::Level::INFO) << "Daemon killed";
    
    return pid;
}

int main(int argc, const char* argv[])
{
    if (argc < 2)
    {
        Log(Log::Level::ERR) << "Too few arguments";

        const char* exe = argc >= 1 ? argv[0] : "bmdmemory";
        Log(Log::Level::INFO) << "Usage: " << exe << " <name> [--instance=<instance>] [--video_mode <video mode>] [--video_connection <video connection>] [--video_format <video format>] [--audio_connection <audio connection>] [--memory_size <memory size>] [--daemon] [--killdaemon]";

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
                Log(Log::Level::ERR) << "Invalid argument";
        }
        else if (strcmp(argv[i], "--video_mode") == 0)
        {
            if (++i < argc)
                videoMode = atoi(argv[i]);
            else
                Log(Log::Level::ERR) << "Invalid argument";
        }
        else if (strcmp(argv[i], "--video_connection") == 0)
        {
            if (++i < argc)
                videoConnection = atoi(argv[i]);
            else
                Log(Log::Level::ERR) << "Invalid argument";
        }
        else if (strcmp(argv[i], "--video_format") == 0)
        {
            if (++i < argc)
                videoFormat = atoi(argv[i]);
            else
                Log(Log::Level::ERR) << "Invalid argument";
        }
        else if (strcmp(argv[i], "--audio_connection") == 0)
        {
            if (++i < argc)
                audioConnection = atoi(argv[i]);
            else
                Log(Log::Level::ERR) << "Invalid argument";
        }
        else if (strcmp(argv[i], "--daemon") == 0)
        {
            daemon = true;
        }
        else if (strcmp(argv[i], "--kill-daemon") == 0)
        {
            if (killDaemon("/var/run/bmdmemory.pid"))
            {
                return EXIT_SUCCESS;
            }
            else
            {
                return EXIT_FAILURE;
            }
        }
    }

    if (daemon && daemonize("/var/run/bmdmemory.pid") == -1)
    {
        Log(Log::Level::ERR) << "Failed to start daemon";
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
