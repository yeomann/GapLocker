#pragma once

#include "stdafx.h"

struct PluginSettings
{
    struct Symbol
    {
        std::string Name;
        double Points;
        //to do
    };

    std::string Groups;
    std::string SessionTime;
    std::map<int, std::shared_ptr<Symbol>> Symbols;
    std::string SkipDays;

    //needed?
    std::string Status;
    bool DebugLogs;
};