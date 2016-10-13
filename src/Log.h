//
//  rtmp_relay
//

#pragma once

#include <sstream>

class Log
{
public:
    enum class Level
    {
        OFF,
        ERR,
        WARN,
        INFO,
        ALL
    };

    static Level threshold;

    Log()
    {
    }

    Log(Level pLevel): level(pLevel)
    {
    }

    ~Log();

    template <typename T> Log& operator << (T val)
    {
        s << val;

        return *this;
    }

private:
    Level level = Level::INFO;
    std::stringstream s;
};
