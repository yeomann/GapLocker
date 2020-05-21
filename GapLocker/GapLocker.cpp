//| Take --------      MT5 & MT4 plugins, applications, and services
//| profit ------      for medium-sized brokers.
//| techno ------
//| logy --------      www.takeprofit.technology
//| 
//| This product was developed by Takeprofit Technology.
//| All rights reserved. Distribution and use of this file is prohibited unless explicitly granted by written agreement.


#include "stdafx.h"
#include "PluginInstance.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    return(TRUE);
}

MTAPIENTRY MTAPIRES MTServerAbout(MTPluginInfo& info)
{
    info.version = PLUGIN_VERSION;
    info.version_api = MTServerAPIVersion;
    wcscpy(info.name, L"" PLUGIN_NAME);
    wcscpy(info.copyright, L"" COPYRIGHT);
    wcscpy(info.description, L"This is MetaTrader 5 Server API plugin");

    return(MT_RET_OK);
}

MTAPIENTRY MTAPIRES MTServerCreate(UINT apiversion, IMTServerPlugin** plugin)
{
    if (!plugin)
    {
        return(MT_RET_ERR_PARAMS);
    }

    if (((*plugin) = new(std::nothrow) CPluginInstance()) == NULL)
    {
        return(MT_RET_ERR_MEM);
    }

    return(MT_RET_OK);
}
