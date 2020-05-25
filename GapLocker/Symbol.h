#pragma once
#include "stdafx.h"

struct Symbol
{
    std::string Name;
    int Points;
    time_t BeginTimeOffset;
    time_t EndTimeOffset;

    std::optional<MTTickShort> SessionStartInfo;
    std::optional<MTTickShort> SessionEndInfo;

    Symbol() : Name(""), Points(0), BeginTimeOffset(0), EndTimeOffset(SECONDS_IN_DAY), SessionStartInfo(std::nullopt), SessionEndInfo(std::nullopt) {}

    time_t GetTimeWithOffset(time_t offset, time_t current) 
    {
        auto t = gmtime(&current);

        t->tm_hour = 0;
        t->tm_min = 0;
        t->tm_sec = 0;

        return mktime(t) + offset;
    }
};
