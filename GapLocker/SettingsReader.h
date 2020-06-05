#pragma once

#include "stdafx.h"
#include "Models/PluginSettings.h"
#include "Symbol.h"
#include <regex>

class SettingsReader
{
public:

    static bool Read(IMTServerAPI* serverApi, PluginSettings& pluginSettings)
    {
        METHOD_BEGIN();

        auto plugin = WIMTConPlugin(serverApi);

        MTAPIRES retcode;

        if ((retcode = serverApi->PluginCurrent(plugin)) != MT_RET_OK)
        {
            LOG_ERROR() << "Cannot get current plugin, ret = " << retcode;
            return(MT_RET_ERR_PARAMS);
        }

        pluginSettings.Symbols.clear();

        //get number of parameters      
        auto paramCount = plugin->ParameterTotal();

        for (int i = 0; i < paramCount; i++) 
        {
            MTAPIRES retcode;
            auto param = WIMTConPluginParam(serverApi);
            if (retcode = plugin->ParameterNext(i, param) != MT_RET_OK)
            {
                LOG_ERROR() << "Cannot get " << i + 1 << " parameter, ret = " << retcode;
                continue;
            }

            std::string name = pluginbase::tools::WideToString(param->Name());
            
            if (name == "Groups") 
            {
                pluginSettings.Groups = pluginbase::tools::WideToString(param->Value());
                continue;
            }

            if (name == "Status")
            {
                pluginSettings.Status = pluginbase::tools::WideToString(param->Value());
                continue;
            }

            if (name == "SkipDays")
            {
                std::string value = pluginbase::tools::WideToString(param->Value());
                std::vector<std::string> arr;
                boost::split(arr, value, [&](char c) { return c == ','; });
                for (auto& day : arr)
                {
                    try
                    {
                        pluginSettings.SkipDays.push_back(dayNameToGregWeekday(day));
                        LOG_FILE() << "Added new skip day " << day;
                    }
                    catch (...)
                    {
                        LOG_FILE() << "Cannot parse '" << name << "' value [ " << day << " ]. Skip";
                    }
                }
                continue;
            }

            if (name == "DebugLogs")
            {
                pluginSettings.DebugLogs = parseBoolField(pluginbase::tools::WideToString(param->Value()));
                continue;
            }

            parseAndCreateSymbol(pluginSettings, param, name);
        }

        // Update plugin config
        retcode = serverApi->PluginAdd(plugin);
        if (retcode != MT_RET_OK && retcode != MT_RET_OK_NONE)
        {
            // when method call without updated params
            if (retcode != MT_RET_ERROR)
            {
                LOG_ERROR() << "Cannot update plugin config, ret = " << retcode;
            }

            return false;
        }

        LOG_FILE() << "Plugin configuration has been successfully updated";
        Print(pluginSettings);

        return true;

        METHOD_END();
    }

    static void Print(PluginSettings& pluginSettings)
    {
        METHOD_BEGIN();

        LOG_FILE() << "[PluginSettings] Status: " << pluginSettings.Status
            << "; DebugLogs: " << pluginSettings.DebugLogs
            << "; Groups: " << pluginSettings.Groups;

        METHOD_END();
    }

private:
    static void parseAndCreateSymbol(PluginSettings& pluginSettings, WIMTConPluginParam& param, std::string name)
    {
        try 
        {
            std::string value = pluginbase::tools::WideToString(param->Value());
            //check value
            std::regex regexstr("(0\\d|1\\d|2[0-3]):([0-5]\\d)\\s*-\\s*(0\\d|1\\d|2[0-3]):([0-5]\\d)\s*;\\s*(\\d+)");
            std::smatch matchResults;
            if (!regex_match(value, matchResults, regexstr))
            {
                LOG_FILE() << "Cannot parse '" << name << "' symbol value [" << value << "]. Skip";
                return;
            }

            auto symbol = std::make_shared<Symbol>();
            symbol->Name = name;
            symbol->BeginTimeOffset = std::stoi(matchResults[1].str()) * SECONDS_IN_HOUR + std::stoi(matchResults[2].str()) * SECONDS_IN_MINUTE;
            symbol->EndTimeOffset = std::stoi(matchResults[3].str()) * SECONDS_IN_HOUR + std::stoi(matchResults[4].str())* SECONDS_IN_MINUTE;

            if (symbol->BeginTimeOffset > symbol->EndTimeOffset)
                symbol->EndTimeOffset += SECONDS_IN_DAY;

            symbol->Points = std::stoi(matchResults[5].str());

            pluginSettings.Symbols[name] = symbol;

            LOG_FILE() << "Added new symbol '" << name << "' with parameters Time = " << matchResults[1].str() << ":" << matchResults[2].str() 
                << " - " << matchResults[3].str() << ":" << matchResults[4].str() << " (startOffset = " << symbol->BeginTimeOffset 
                << ", endOffset = " << symbol->EndTimeOffset << ") and Points = " << symbol->Points;
        }
        catch (const std::exception & ex)
        {
            LOG_FILE() << ex.what();
        }
    }

    static bool parseBoolField(std::string field)
    {
        METHOD_BEGIN();
        boost::to_lower(field);
        boost::trim(field);

        if (field == "true" || field == "1")
        {
            return true;
        }

        if (field == "false" || field == "0")
        {
            return false;
        }

        LOG_ERROR() << "Can't decide whether '" << field << "' should disable the rule. Assuming `enabled`.";
        return false;
        METHOD_END();
    }

    static boost::gregorian::greg_weekday dayNameToGregWeekday(const std::string& day)
    {
        METHOD_BEGIN();

        if (day == "SUN")
        {
            return boost::gregorian::Sunday;
        }
        else if (day == "MON")
        {
            return boost::gregorian::Monday;
        }
        else if (day == "TUE")
        {
            return boost::gregorian::Tuesday;
        }
        else if (day == "WED")
        {
            return boost::gregorian::Wednesday;
        }
        else if (day == "THU")
        {
            return boost::gregorian::Thursday;
        }
        else if (day == "FRI")
        {
            return boost::gregorian::Friday;
        }
        else if (day == "SAT")
        {
            return boost::gregorian::Saturday;
        }

        throw std::runtime_error("Field contains unrecognized day format");

        METHOD_END();
    }
};