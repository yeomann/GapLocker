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
#include "math.h"

#include <boost/asio/io_service.hpp>
#include <boost/bind.hpp>
#include <boost/thread/thread.hpp>

// Need to subscribe/unsubscribe for each interface with IMTServerAPI::TickSubscribe / IMTServerAPi::TickUnsubscribe in CPluginInstance::Start() and CPluginInstance::Stop()
class CPluginInstance : public pluginbase::Mt5Plugin,
    public IMTServerPlugin,
    public IMTConPluginSink,
    public IMTTickSink
{

private:
    IMTServerAPI* serverApi;
    MTServerInfo    serverInfo;
    CMTThread         thread;

    pluginbase::Threadpool< std::function<void()> > threadPool;

    std::atomic<bool> isThreadActive;

    PluginSettings pluginSettings;

    bool deb = true;

    enum { LOOP_DELAY_IN_MILLISECONDS = 50, TIMEOUT_CHECK_STATE = 50 };

    MUTEX();

public:
    CPluginInstance() :
        serverApi(nullptr),
        threadPool([](const std::function<void()>& F) { SAFE_BEGIN_NAME(Threadpool_handler_func) F(); SAFE_END(); })
    {
        SAFE_BEGIN_ALWAYS();
        ZeroMemory(&serverInfo, sizeof(serverInfo));
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
        
        try
        {
            if ((retcode = serverApi->About(serverInfo)) != MT_RET_OK)
            {
                throw std::exception("Cannot get server info", retcode);
            }

            if ((retcode = serverApi->PluginSubscribe(this)) != MT_RET_OK)
            {
                throw std::exception("Cannot subscribe for plugin updates", retcode);
            }

            if ((retcode = serverApi->TickSubscribe(this)) != MT_RET_OK)
            {
                throw std::exception("Cannot subscribe for tick updates", retcode);
            }

            if ((retcode = ThreadStart()) != MT_RET_OK)
            {
                throw std::exception("Cannot start service thread", retcode);
            }

            pluginbase::Mt5Plugin::FlushLoggers();

            return MT_RET_OK;
        }
        catch (const std::exception & ex)
        {
            LOG_FATAL() << "Plugin start failed with exception: " << ex.what();
        }

        SAFE_END(MT_RET_ERROR);
    }

    MTAPIRES Stop()
    {
        SAFE_BEGIN_ALWAYS();

        if (serverApi)
        {
            ThreadStop();

            serverApi->PluginUnsubscribe(this);
            serverApi->TickUnsubscribe(this);

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

        WIMTConPlugin plugin(serverApi);

        MTAPIRES retcode;

        if ((retcode = serverApi->PluginCurrent(plugin)) != MT_RET_OK)
        {
            return (retcode);
        }

        bool isReadedAllParams = SettingsReader::Read(serverApi, pluginSettings);
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

        if (serverApi == nullptr)
        {
            return;
        }

        SettingsReader::Read(serverApi, pluginSettings);
        //SettingsReader::Print(userSettings);

        METHOD_END();
    }

    void OnTick(
        LPCWSTR symbol,
        const MTTickShort& tick
    ) override
    {
        SAFE_BEGIN();
        LOCK();

        std::string s = pluginbase::tools::WideToString(symbol).c_str();
        auto symbolObj = pluginSettings.Symbols.find(s);
        if (symbolObj == pluginSettings.Symbols.end())
            return;

        auto smb = symbolObj->second;

        //write ticks in other file !!!
        LOG_FILE() << "[" << tick.datetime << "] New tick for '" << s << "': Bid = " << tick.bid << ", Ask = " << tick.ask;

        time_t currentSessionStart = smb->GetTimeWithOffset(smb->BeginTimeOffset, tick.datetime);
        time_t currentSessionEnd = smb->GetTimeWithOffset(smb->EndTimeOffset, tick.datetime);

        if (currentSessionStart <= tick.datetime && currentSessionEnd > tick.datetime)
        {
            //check start
        if (smb->SessionStartInfo == std::nullopt)
        {
            //for easy and fast debug: comment time check (str 214), comment 10 min check(str 233) and uncomment the following block  

            /*if (deb)
            {
                smb->SessionEndInfo = tick;
                deb = false;
                MAGIC_SLEEP(SECONDS_IN_MINUTE / 3);
                return;
            }*/
            //--------

            smb->SessionStartInfo = tick;
            LOG_FILE() << "New START for '" << s << "': Bid = " << tick.bid << ", Ask = " << tick.ask;

            if (tick.datetime <= currentSessionStart + SECONDS_IN_MINUTE * 10)
            {
                std::optional<double> buyPrice = std::nullopt;
                std::optional<double> sellPrice = std::nullopt;

                getPricesForLockingPosition(smb, buyPrice, sellPrice);

                if (buyPrice != std::nullopt)
                    threadPool.push([this, buyPrice, s, tick]() {
                    openLockPositions(buyPrice.value(), s, IMTPosition::EnPositionAction::POSITION_BUY, tick.datetime * 1000);
                        });

                if (sellPrice != std::nullopt)
                    threadPool.push([this, sellPrice, s, tick]() {
                    openLockPositions(sellPrice.value(), s, IMTPosition::EnPositionAction::POSITION_SELL, tick.datetime * 1000);
                        });           
            }

            return;
        }

        //set end
        smb->SessionEndInfo = tick;
        LOG_FILE() << "New END for '" << s << "': Bid = " << tick.bid << ", Ask = " << tick.ask;
        }
        else
        {
            //set start to null - we don't need it anymore
            if (smb->SessionStartInfo != std::nullopt)
            {
                smb->SessionStartInfo = std::nullopt;
                LOG_FILE() << "Current session has ended.";
            }
        }

        SAFE_END();
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

        auto counter = 0;

        while (isThreadActive)
        {
            counter++;
            MAGIC_SLEEP(LOOP_DELAY_IN_MILLISECONDS);

            //each minute
            if (counter % (1 * 60 * (1000 / TIMEOUT_CHECK_STATE)) == 0)
            {
                pluginbase::Mt5Plugin::PerformBackgroundTasks();
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

    void getPricesForLockingPosition(std::shared_ptr<Symbol> symbol, std::optional<double> &buyPrice, std::optional<double> &sellPrice)
    {
        METHOD_BEGIN();
        LOCK();

        //get symbol info for digits
        WIMTConSymbol wrapper(serverApi);
        MTAPIRES retcode;
        if ((retcode = serverApi->SymbolGet(pluginbase::tools::StringToWide(symbol->Name).c_str(), wrapper)) != MT_RET_OK)
        {
            LOG_ERROR() << "Cannot get symbol '" << symbol->Name <<  "' from server: " << retcode;
            return;
        }

        //check bid for creation lock-buy position
        double bidOpen = symbol->SessionStartInfo->bid;
        double bidClose = symbol->SessionEndInfo->bid;

        if (bidClose > bidOpen && bidClose - bidOpen >= symbol->Points * wrapper->Point())
            buyPrice = bidClose - symbol->Points * wrapper->Point();
        else if (bidOpen > bidClose && bidOpen - bidClose >= symbol->Points * wrapper->Point())
            buyPrice = bidClose + symbol->Points * wrapper->Point();

        //check ask for creation lock-sell position
        double askOpen = symbol->SessionStartInfo->ask;
        double askClose = symbol->SessionEndInfo->ask;

        if (askClose > askOpen && askClose - askOpen >= symbol->Points * wrapper->Point())
            sellPrice = askClose - symbol->Points * wrapper->Point();
        else if (askOpen > askClose && askOpen - askClose >= symbol->Points * wrapper->Point())
            sellPrice = askClose + symbol->Points * wrapper->Point();

        return;

        METHOD_END();
    }

    void openLockPositions(double price, std::string symbol, IMTPosition::EnPositionAction action, INT64 time)
    {
        METHOD_BEGIN();
        
        auto positions = getPositionsBySymbolAndOperation(symbol,
            (action == IMTPosition::EnPositionAction::POSITION_BUY) ? IMTPosition::EnPositionAction::POSITION_SELL
            : IMTPosition::EnPositionAction::POSITION_BUY);

        if (positions->Total() == 0)
        {
            //LOG_FILE() << "POS ZERO SIZE";
            return;
        }

        //create orders
        auto orders = CreateOrderArray(positions, action, price, time);
        if (orders->Total() == 0)
        {
            LOG_ERROR() << "Can't create order array for symbol '" << symbol << "'. Skip.";
            return;
        }

        //create deals
        if (!CreateDealArray(orders))
        {
            LOG_ERROR() << "Can't create deal array for symbol '" << symbol << "'. Skip.";
            return;
        }

        //fix positions
        if (!fixPositions(orders))
        {
            LOG_ERROR() << "Can't fix positions for symbol '" << symbol << "'. Skip.";
            return;
        }

        LOG_FILE() << "Creating lock positions for symbol '" << symbol << "' has been finished successfully.";
            
        METHOD_END();
    }

    WIMTPositionArray getPositionsBySymbolAndOperation(std::string symbol, IMTPosition::EnPositionAction action)
    {
        METHOD_BEGIN();
        LOCK();

        //get open positions by groups
        WIMTPositionArray allPositions(serverApi);
        MTAPIRES retcode;
        if ((retcode = serverApi->PositionGetByGroup(pluginbase::tools::StringToWide(pluginSettings.Groups).c_str(), allPositions)) != MT_RET_OK)
        {
            LOG_ERROR() << "Cannot get open positions for group mask '" << pluginSettings.Groups << "' from server: " << retcode;
            return allPositions;
        }

        WIMTPositionArray positions(serverApi);
        for (uint32_t i = 0; i < allPositions->Total(); i++)
        {
            if (pluginbase::tools::WideToString(allPositions->Next(i)->Symbol()) == symbol
                && allPositions->Next(i)->Action() == action)
                positions->Add(allPositions->Next(i));
        }

        return positions;

        METHOD_END();
    }

    WIMTOrderArray CreateOrderArray(WIMTPositionArray &positions, IMTPosition::EnPositionAction action, double price, INT64 time)
    {
        METHOD_BEGIN();
        LOCK();

        int errors = 0;

        //create orders array
        WIMTOrderArray orders(serverApi);
        while (errors <= 10)
        {
            for (int i = 0; i < positions->Total(); i++)
            {
                WIMTOrder order(serverApi);
                order->Login(positions->Next(i)->Login());
                order->Symbol(positions->Next(i)->Symbol());
                order->Type(action);
                order->Digits(positions->Next(i)->Digits());
                order->DigitsCurrency(positions->Next(i)->DigitsCurrency());
                order->ContractSize(positions->Next(i)->ContractSize());
                order->VolumeInitial(positions->Next(i)->Volume());
                order->VolumeCurrent(0);
                order->PriceOrder(price);
                order->PriceCurrent(price);
                order->PriceSL(0);
                order->PriceTP(0);
                order->RateMargin(positions->Next(i)->RateMargin());
                order->TypeFill(IMTOrder::EnOrderFilling::ORDER_FILL_RETURN);
                order->TimeSetupMsc(time);
                order->TimeDoneMsc(time);
                order->ReasonSet(IMTOrder::EnOrderReason::ORDER_REASON_DEALER);
                order->StateSet(IMTOrder::EnOrderState::ORDER_STATE_FILLED);
                orders->Add(order);
            }
            if (orders->Total() != positions->Total())
            {
                errors++;
                orders->Clear();
            }
            else
                break;
        }  

        //create orders on server
        while (errors <= 10) 
        {
            WIMTOrderArray currArray(serverApi);
            for (int i = 0; i < orders->Total(); i++)
            {
                if (orders->Next(i)->Order() == 0)
                    currArray->Add(orders->Next(i));
            }

            if (currArray == 0)
            {
                errors++;
                continue;
            }

            MTAPIRES* retcodes;  
            MTAPIRES retcode = serverApi->HistoryAddBatch(currArray, retcodes);

            if (retcode == MT_RET_ERR_NETWORK || retcode == MT_RET_ERR_FREQUENT || retcode == MT_RET_REQUEST_TOO_MANY || retcode == MT_RET_REQUEST_TIMEOUT)
            {
                //wait 1 sec and just try again
                MAGIC_SLEEP(SECONDS_IN_MINUTE);
                errors++;
                continue;
            }

            if (retcode != MT_RET_OK && retcode != MT_RET_OK_NONE)
            {
                LOG_ERROR() << "Problems with creating orders. Status: " << retcode;
                errors++;
                continue;
            }

            bool isOK = true;
            for (uint32_t i = 0; i < currArray->Total(); i++)
            {
                if (retcodes[i] == MT_RET_OK) 
                {
                    LOG_FILE() << "Order " << currArray->Next(i)->Order() << " has been created";
                    continue;
                }
                isOK = false;
            }

            if (isOK)
                return orders;
            errors++;
        }

        orders->Clear();
        return orders;

        METHOD_END();
    }

    bool CreateDealArray(WIMTOrderArray &orders)
    {
        METHOD_BEGIN();
        LOCK();

        int errors = 0;

        //create deals
        WIMTDealArray deals(serverApi);
        while (errors <= 10)
        {
            for (int i = 0; i < orders->Total(); i++)
            {
                WIMTDeal deal(serverApi);
                deal->Login(orders->Next(i)->Login());
                deal->Symbol(orders->Next(i)->Symbol());
                deal->Action(orders->Next(i)->Type());
                deal->Volume(orders->Next(i)->VolumeInitial());
                deal->Price(orders->Next(i)->PriceOrder());
                deal->Digits(orders->Next(i)->Digits());
                deal->DigitsCurrency(orders->Next(i)->DigitsCurrency());
                deal->ContractSize(orders->Next(i)->ContractSize());
                deal->TimeMsc(orders->Next(i)->TimeSetupMsc());
                deal->RateMargin(orders->Next(i)->RateMargin());
                deal->Order(orders->Next(i)->Order());
                deal->PositionID(orders->Next(i)->Order());
                deal->Entry(IMTDeal::EnDealEntry::ENTRY_OUT);
                deal->ReasonSet(orders->Next(i)->Reason());
                deals->Add(deal);
            }
            if (orders->Total() != deals->Total())
            {
                errors++;
                deals->Clear();
            }
            else
                break;
        }

        //create orders on server
        while (errors <= 10)
        {
            WIMTDealArray currArray(serverApi);
            for (int i = 0; i < deals->Total(); i++)
            {
                if (deals->Next(i)->Deal() == 0)
                    currArray->Add(deals->Next(i));
            }

            if (currArray == 0)
            {
                LOG_ERROR() << "Smth went wrong";
                errors++;
                continue;
            }

            MTAPIRES* retcodes;
            MTAPIRES retcode = serverApi->DealAddBatch(currArray, retcodes);

            if (retcode == MT_RET_ERR_NETWORK || retcode == MT_RET_ERR_FREQUENT || retcode == MT_RET_REQUEST_TOO_MANY || retcode == MT_RET_REQUEST_TIMEOUT)
            {
                //wait 1 sec and just try again
                MAGIC_SLEEP(SECONDS_IN_MINUTE);
                errors++;
                continue;
            }

            if (retcode != MT_RET_OK && retcode != MT_RET_OK_NONE)
            {
                LOG_ERROR() << "Problems with creating deals. Status: " << retcode;
                errors++;
                continue;
            }

            bool isOK = true;
            for (uint32_t i = 0; i < currArray->Total(); i++)
            {
                if (retcodes[i] == MT_RET_OK)
                {
                    LOG_FILE() << "Deal " << currArray->Next(i)->Deal() << " with position id " << currArray->Next(i)->PositionID() << " has been created";
                    continue;
                }
                isOK = false;
            }

            if (isOK)
                return true;

            errors++;
        }
        return false;

        METHOD_END();
    }

    bool fixPositions(WIMTOrderArray &orders)
    {
        METHOD_BEGIN();
        LOCK();

        WIMTPositionArray positions(serverApi);
        for(int i = 0; i < orders->Total(); i++)
        {
            int errors = 0;
            while (errors <= 10) 
            {
                MTAPIRES retcode = serverApi->PositionFix(orders->Next(i)->Login(), positions);
                if (retcode == MT_RET_OK)
                {
                    LOG_FILE() << "Positions for login " << orders->Next(i)->Login() << " has been fixed";
                    break;
                }

                errors++;
            }

            if (errors > 10)
                return false;
        }

        return true;

        METHOD_END();
    }
};