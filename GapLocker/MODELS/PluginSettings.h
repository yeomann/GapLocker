#pragma once

#include "stdafx.h"

struct PluginSettings
{
    struct Symbol
    {
        std::string Name;
        int Points;
        time_t BeginTimeOffset;
        time_t EndTimeOffset;

        Symbol() : Name(""), Points(0), BeginTimeOffset(0), EndTimeOffset(SECONDS_IN_DAY) {}
    };

    std::string Groups;
    time_t SessionBeginTimeOffset;
    time_t SessionEndTimeOffset;
    std::map<int, std::shared_ptr<Symbol>> Symbols;
    std::vector<boost::gregorian::greg_weekday> SkipDays;

    //needed?
    std::string Status;
    bool DebugLogs = false;
};