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

    enum { LOOP_DELAY_IN_MILLISECONDS = 50, TIMEOUT_CHECK_STATE = 50, MILLISECONDS_IN_SEC = 1000 };

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
        
        if (LOCK(); !SettingsReader::Read(serverApi, pluginSettings))
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
        SAFE_BEGIN();

        if (serverApi == nullptr)
        {
            return;
        }
            
        LOCK();
        SettingsReader::Read(serverApi, pluginSettings);

        SAFE_END();
    }

    void OnTick(
        LPCWSTR symbol,
        const MTTickShort& tick
    ) override
    {
        SAFE_BEGIN();
        LOCK();
        
        std::string s = pluginbase::tools::WideToString(symbol);
        auto symbolObj = pluginSettings.Symbols.find(s);
        if (symbolObj == pluginSettings.Symbols.end())
            return;

        auto smb = symbolObj->second;

        time_t currentSessionStart = smb->GetStartTimeWithOffset(tick.datetime);
        time_t currentSessionEnd = smb->GetEndTimeWithOffset(currentSessionStart);

        if (currentSessionStart > tick.datetime)
        {
            currentSessionStart -= SECONDS_IN_DAY;
            currentSessionEnd -= SECONDS_IN_DAY;
        }

        if (currentSessionStart <= tick.datetime && currentSessionEnd > tick.datetime)
        {
            //check start
            bool newSession = smb->SessionStartInfo != std::nullopt && smb->SessionStartInfo->datetime < currentSessionStart;
            if (smb->SessionStartInfo == std::nullopt || newSession)
            {
                if (newSession)
                    LOG_FILE() << "Current session has ended.";

                smb->SessionStartInfo = tick;
                LOG_FILE() << "Session Started";

                if (tick.datetime <= currentSessionStart + SECONDS_IN_MINUTE * 10 && smb->SessionEndInfo != std::nullopt)
                {
                    std::optional<double> buyPrice = std::nullopt;
                    std::optional<double> sellPrice = std::nullopt;

                    getPricesForLockingPosition(smb, buyPrice, sellPrice);

                    if (buyPrice.has_value() || sellPrice.has_value())
                    {
                        threadPool.push([this, buyPrice, sellPrice, s, tick]() {
                            openLockPositions(buyPrice, sellPrice, s, tick.datetime * 1000);
                            });
                    }
                }
            }

            //set end
            smb->SessionEndInfo = tick; 
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

    void getPricesForLockingPosition(std::shared_ptr<Symbol> symbol, std::optional<double> &buyPrice, std::optional<double> &sellPrice) const
    {
        METHOD_BEGIN();
        
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

        const auto threshold = symbol->Points * wrapper->Point();
        if (std::abs(bidClose - bidOpen) >= threshold)
        {
            sellPrice = bidClose > bidOpen ? bidClose - threshold : bidClose + threshold;
        }

        //check ask for creation lock-sell position
        double askOpen = symbol->SessionStartInfo->ask;
        double askClose = symbol->SessionEndInfo->ask;

        if (std::abs(askClose - askOpen) >= threshold)
        {
            buyPrice = askClose > askOpen ? askClose - threshold : askClose + threshold;
        }
        
        return;

        METHOD_END();
    }

    void openLockPositions(std::optional<double> buyPrice, std::optional<double> sellPrice, const std::string& symbol, INT64 time) const
    {
        METHOD_BEGIN();

        auto positions = getPositionsBySymbol(symbol);
        if (positions.size() == 0)
        {
            LOG_FILE() << "No opened positions has been found after the gap.";
            return;
        }

        LOG_FILE() << "Start creating lock positions for symbol '" << symbol << "' with buyPrice = " << buyPrice.value_or(0.0) << " and sellPrice = " << sellPrice.value_or(0.0);

        //create orders
        auto orders = CreateOrderArray(positions, buyPrice, sellPrice, time);
        if (orders.size() == 0)
        {
            LOG_ERROR() << "No orders for symbol '" << symbol << "' has been created. Skip.";
            return;
        }

        //create deals
        CreateDealArray(orders);

        //fix positions
        if (!fixPositions(orders))
        {
            LOG_ERROR() << "Can't fix positions for symbol '" << symbol << "'. Skip.";
            return;
        }

        LOG_FILE() << "Creating lock positions for symbol '" << symbol << "' has been finished successfully.";
            
        METHOD_END();
    }

    std::vector<WIMTPosition> getPositionsBySymbol(const std::string& symbol) const
    {
        METHOD_BEGIN();

        //get open positions by groups
        WIMTPositionArray allPositions(serverApi);
        std::vector<WIMTPosition> positions;
        MTAPIRES retcode;

        std::string groups;
        {
            LOCK();
            groups = pluginSettings.Groups;
        }

        if ((retcode = serverApi->PositionGetByGroup(pluginbase::tools::StringToWide(groups).c_str(), allPositions)) != MT_RET_OK)
        {
            LOG_ERROR() << "Cannot get open positions for group mask '" << groups << "' from server: " << retcode;
            return positions;
        }

        LOG_FILE() << "Positions detected: " << allPositions->Total();

        for (int i = 0; i < allPositions->Total(); i++)
        {
            if (pluginbase::tools::WideToString(allPositions->Next(i)->Symbol()) == symbol) 
            {
                positions.emplace_back(serverApi);
                positions.back()->Assign(allPositions->Next(i));
            }
        }
      
        return positions;

        METHOD_END();
    }

    class OrderExtended
    {
    public:
            OrderExtended(const std::shared_ptr<WIMTOrder> order, double rateProfit)
            {
                Order = order;
                RateProfit = rateProfit;
            }

            std::shared_ptr<WIMTOrder> Order;
            double RateProfit;
    };

    std::vector<OrderExtended> CreateOrderArray(const std::vector<WIMTPosition> &positions, std::optional<double> buyPrice, std::optional<double> sellPrice, INT64 time) const
    {
        METHOD_BEGIN();

        //create orders array
        std::vector<OrderExtended> extendedOrders;
        IMTOrder::EnOrderType action;
        double price;

        for (const auto& position : positions)
        {
            if (position->Action() == IMTPosition::EnPositionAction::POSITION_BUY && sellPrice.has_value())
            {
                action = IMTOrder::EnOrderType::OP_SELL;
                price = sellPrice.value();
            }
            else if (position->Action() == IMTPosition::EnPositionAction::POSITION_SELL && buyPrice.has_value())
            {
                action = IMTOrder::EnOrderType::OP_BUY;
                price = buyPrice.value();
            }
            else
            {
                LOG_FILE() << "Skip creating order for position '" << position->Position() << "' with reason: no price detected.";
                continue;
            }

            std::shared_ptr<WIMTOrder> order = std::make_shared<WIMTOrder>(serverApi);
            (*order)->Login(position->Login());
            (*order)->Symbol(position->Symbol());
            (*order)->Type(action);
            (*order)->Digits(position->Digits());
            (*order)->DigitsCurrency(position->DigitsCurrency());
            (*order)->ContractSize(position->ContractSize());
            (*order)->VolumeInitial(position->Volume());
            (*order)->VolumeCurrent(0);
            (*order)->PriceOrder(price);
            (*order)->PriceCurrent(price);
            (*order)->PriceSL(0);
            (*order)->PriceTP(0);
            (*order)->RateMargin(position->RateMargin());
            (*order)->TypeFill(IMTOrder::EnOrderFilling::ORDER_FILL_RETURN);
            (*order)->TimeSetupMsc(time);
            (*order)->TimeDoneMsc(time);
            (*order)->ReasonSet(IMTOrder::EnOrderReason::ORDER_REASON_DEALER);
            (*order)->StateSet(IMTOrder::EnOrderState::ORDER_STATE_FILLED);
            (*order)->Comment(L"Open Gap");
            
            //try to create on server
            int errors = 0;
            while (errors <= 10)
            {
                MTAPIRES retcode = serverApi->HistoryAdd(*order);
                if (retcode == MT_RET_ERR_NETWORK || retcode == MT_RET_ERR_FREQUENT || retcode == MT_RET_REQUEST_TOO_MANY || retcode == MT_RET_REQUEST_TIMEOUT)
                {
                    //wait 1 sec and just try again
                    MAGIC_SLEEP(MILLISECONDS_IN_SEC);
                    errors++;
                    continue;
                }

                if (retcode != MT_RET_OK && retcode != MT_RET_OK_NONE)
                {
                    LOG_ERROR() << "Problems with creating orders. Status: " << retcode;
                    errors++;
                    continue;
                }

                LOG_FILE() << "Locking order " << (*order)->Order() << " for position " << position->Position() << " has been created";
                extendedOrders.emplace_back(order, position->RateProfit());
                break;
            }

            if (errors > 10)
                LOG_ERROR() << "Can't create lock order for position " << position->Position() << ". Skip creating locking position";
        }

        return extendedOrders;

        METHOD_END();
    }

    void CreateDealArray(const std::vector<OrderExtended> & extendedOrders) const
    {
        METHOD_BEGIN();

        //create deals
        for (const auto& extendedOrder : extendedOrders)
        {
            const auto& order = *extendedOrder.Order;
            WIMTDeal deal(serverApi);
            deal->Login(order->Login());
            deal->Symbol(order->Symbol());
            deal->Action(order->Type());
            deal->Volume(order->VolumeInitial());
            deal->Price(order->PriceOrder());
            deal->Digits(order->Digits());
            deal->DigitsCurrency(order->DigitsCurrency());
            deal->ContractSize(order->ContractSize());
            deal->TimeMsc(order->TimeSetupMsc());
            deal->RateMargin(order->RateMargin());
            deal->Order(order->Order());
            deal->PositionID(order->Order());
            deal->Entry(IMTDeal::EnDealEntry::ENTRY_OUT);
            deal->ReasonSet(order->Reason());
            deal->Comment(L"Open Gap");
            deal->RateProfit(extendedOrder.RateProfit);

            //try to create on server
            int errors = 0;
            while (errors <= 10)
            {
                MTAPIRES retcode = serverApi->DealAdd(deal);
                if (retcode == MT_RET_ERR_NETWORK || retcode == MT_RET_ERR_FREQUENT || retcode == MT_RET_REQUEST_TOO_MANY || retcode == MT_RET_REQUEST_TIMEOUT)
                {
                    //wait 1 sec and just try again
                    MAGIC_SLEEP(MILLISECONDS_IN_SEC);
                    errors++;
                    continue;
                }

                if (retcode != MT_RET_OK && retcode != MT_RET_OK_NONE)
                {
                    LOG_ERROR() << "Problems with creating deals. Status: " << retcode;
                    errors++;
                    continue;
                }

                LOG_FILE() << "Deal " << deal->Deal() << " with position id " << deal->PositionID() << " has been created";
                break;
            }

            if (errors > 10)
                LOG_ERROR() << "Can't create lock deal for order and position " << order->Order() << ". Skip.";
        }

        METHOD_END();
    }

    bool fixPositions(const std::vector<OrderExtended>& extendedOrders) const
    {
        METHOD_BEGIN();

        WIMTPositionArray positions(serverApi);

        std::set<UINT64> logins;
        for (const auto& extendedOrder : extendedOrders)
        {
            logins.insert((*extendedOrder.Order)->Login());
        }

        for (auto login : logins)
        {
            int errors = 0;
            while (errors <= 10) 
            {
                MTAPIRES retcode = serverApi->PositionFix(login, positions);
                if (retcode == MT_RET_OK)
                {
                    LOG_FILE() << "Positions for login " << login << " has been fixed";
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