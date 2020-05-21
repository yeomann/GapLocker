//| Take --------      MT5 & MT4 plugins, applications, and services
//| profit ------      for medium-sized brokers.
//| techno ------
//| logy --------      www.takeprofit.technology
//| 
//| This product was developed by Takeprofit Technology.
//| All rights reserved. Distribution and use of this file is prohibited unless explicitly granted by written agreement.

#pragma once

#include "stdafx.h"
#include "pluginbase/license.h"
#include "pluginbase/mt5Plugin.h"
#include "SettingsReader.h"

// Need to subscribe/unsubscribe for each interface with IMTServerAPI::TickSubscribe / IMTServerAPi::TickUnsubscribe in CPluginInstance::Start() and CPluginInstance::Stop()
class CPluginInstance : public pluginbase::Mt5Plugin,
    public IMTServerPlugin,
    public IMTConPluginSink
{

private:
    IMTServerAPI* serverApi;
    CMTThread         thread;

    std::atomic<bool> isThreadActive;

    PluginSettings userSettings;

    enum { LOOP_DELAY_IN_MILLISECONDS = 50 };

    MUTEX();

public:
    CPluginInstance() :
        serverApi(nullptr)
    {
        SAFE_BEGIN_ALWAYS();
        SAFE_END_ALWAYS();
    }

    ~CPluginInstance()
    {
        SAFE_BEGIN_ALWAYS();
        Stop();
        SAFE_END_ALWAYS();
    }

    void Release()
    {
        SAFE_BEGIN_ALWAYS();
        delete this;
        SAFE_END_ALWAYS();
    }

    MTAPIRES Start(IMTServerAPI* api)
    {
        SAFE_BEGIN();

        if (!api)
        {
            return(MT_RET_ERR_PARAMS);
        }

        serverApi = api;

        MTAPIRES retcode;

        pluginbase::Mt5Plugin::Initialize(serverApi);

        LOG_FILE() << "Plugin starting...";

        if ((retcode = serverApi->PluginSubscribe(this)) != MT_RET_OK)
        {
            throw std::exception("Cannot subscribe for plugin updates", retcode);
        }

        if ((retcode = ThreadStart()) != MT_RET_OK)
        {
            throw std::exception("Cannot start service thread", retcode);
        }

        pluginbase::Mt5Plugin::FlushLoggers();
        
        return MT_RET_OK;

        SAFE_END(MT_RET_ERROR);
    }

    MTAPIRES Stop()
    {
        SAFE_BEGIN_ALWAYS();

        if (serverApi)
        {
            ThreadStop();

            serverApi->PluginUnsubscribe(this);

            serverApi = nullptr;
            LOG_FILE() << "Plugin stopped";
        }
		
		 pluginbase::Mt5Plugin::Deinitialize();

        SAFE_END_ALWAYS(MT_RET_OK);
    }

    MTAPIRES ThreadStart()
    {
        METHOD_BEGIN();

        if (serverApi == nullptr)
        {
            return(MT_RET_ERR_PARAMS);
        }

        bool isReadedAllParams = SettingsReader::Read(serverApi, userSettings);
        if (!isReadedAllParams)
        {
            return (MT_RET_ERR_PARAMS);
        }

        LOG_FILE() << "Thread-loop starting...";
        isThreadActive = true;

        if (!thread.Start(&ThreadWrapper, this, 64 * 1024))
        {
            LOG_ERROR() << "Failed to start service thread";
            return(MT_RET_ERROR);
        }

        return MT_RET_OK;

        METHOD_END();
    }

    void OnPluginUpdate(const IMTConPlugin* plugin)
    {
        METHOD_BEGIN();

        SettingsReader::Read(serverApi, userSettings);
        SettingsReader::Print(userSettings);

        METHOD_END();
    }

private:

    void ThreadStop()
    {
        METHOD_BEGIN();

        LOG_FILE() << "Thread-loop stopped";

        isThreadActive = false;
        thread.Shutdown();

        METHOD_END();
    }

    void Thread()
    {
        METHOD_BEGIN();
        METHOD_NO_DEADLOCK_NAME(perform_service_loop_worker);

        LOG_FILE() << "Starting loop...";

        unsigned int counter = 0;

        while (isThreadActive)
        {
            MAGIC_SLEEP(LOOP_DELAY_IN_MILLISECONDS);

            if (isThreadActive)
            {
                counter++;

                // each five minutes
                //if (counter % (5 * 60 * (1000 / LOOP_DELAY_IN_MILLISECONDS)) == 0)
                //{
                //}
            }
        }

        METHOD_END();
    }

    static unsigned int __stdcall ThreadWrapper(void* lpParam)
    {
        SAFE_BEGIN();

        CPluginInstance* plugin = static_cast<CPluginInstance*>(lpParam);

        if (plugin != nullptr)
        {
            plugin->Thread();
        }

        SAFE_END(0);
    }
};