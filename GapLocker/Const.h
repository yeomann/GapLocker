//| Take --------      MT5 & MT4 plugins, applications, and services
//| profit ------      for medium-sized brokers.
//| techno ------
//| logy --------      www.takeprofit.technology
//| 
//| This product was developed by Takeprofit Technology.
//| All rights reserved. Distribution and use of this file is prohibited unless explicitly granted by written agreement.

#pragma once

#include <pluginbase/copyright.h>

#define PLUGIN_VERSION      106
#define PLUGIN_NAME         "GapLocker"
#define PLUGIN_COPYRIGHT COPYRIGHT

struct PluginCfg
{
    char name[32];
    char value[128];
    int reserved[16];
};