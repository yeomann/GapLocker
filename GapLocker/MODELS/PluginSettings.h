#pragma once

#include "stdafx.h"
#include "Symbol.h"

struct PluginSettings
{
    std::string Groups;
    std::map<std::string, std::shared_ptr<Symbol>> Symbols;
    std::vector<boost::gregorian::greg_weekday> SkipDays;

    //needed?
    std::string Status;
    bool DebugLogs = false;
};