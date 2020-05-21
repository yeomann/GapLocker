#pragma once

#include "stdafx.h"
#include "Models/PluginSettings.h"

class SettingsReader
{
public:

    static bool Read(IMTServerAPI* serverApi, PluginSettings& userSettings)
    {
        METHOD_BEGIN();

        auto plugin = WIMTConPlugin(serverApi);

        MTAPIRES retcode;

        if ((retcode = serverApi->PluginCurrent(plugin)) != MT_RET_OK)
        {
            LOG_ERROR() << "Cannot get current plugin, ret = " << retcode;
            return(MT_RET_ERR_PARAMS);
        }

        auto emptyParam = WIMTConPluginParam(serverApi);

        userSettings.Status = tryReadStrFromParam(plugin, "Status", emptyParam, "Running"); //std::nullopt

        auto debugLogsStr = tryReadStrFromParam(plugin, "DebugLogs", emptyParam, "true");
        if (debugLogsStr.empty())
        {
            LOG_ERROR() << "Cannot get DebugLogs param";
            return false;
        }

        userSettings.DebugLogs = parseBoolField(debugLogsStr);

        userSettings.Groups = tryReadStrFromParam(plugin, "Groups", emptyParam, "");

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
    static std::string tryReadStrFromParam(WIMTConPlugin & plugin, std::string paramName, WIMTConPluginParam & param, std::optional<std::string> defaultValue)
    {
        METHOD_BEGIN();

        MTAPIRES retcode;
        if ((retcode = plugin->ParameterGet(pluginbase::tools::StringToWide(paramName).c_str(), param)) != MT_RET_OK)
        {
            LOG_ERROR() << "Cannot get " << paramName << " parameter, ret = " << retcode;

            if (!defaultValue.has_value())
            {
                LOG_ERROR() << "Default value is not set for param " << paramName;
                return "";
            }

            param->Clear();
            param->Type(IMTConParam::ParamType::TYPE_STRING);
            param->Name(pluginbase::tools::StringToWide(paramName).c_str());
            param->ValueString(pluginbase::tools::StringToWide(defaultValue.value()).c_str());

            if ((retcode = plugin->ParameterAdd(param)) != MT_RET_OK)
            {
                LOG_ERROR() << "Cannot add new " << paramName << " parameter with value " << defaultValue.value() << ", ret = " << retcode;
                return "";
            }

            LOG_FILE() << "New parameter " << paramName << " with default value " << defaultValue.value() << " has been added.";

            return defaultValue.value();
        }

        auto resultStr = pluginbase::tools::WideToString(param->ValueString());
        LOG_FILE() << paramName << " = " << resultStr;
        return resultStr;

        METHOD_END();
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
};