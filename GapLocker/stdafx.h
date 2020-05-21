//| Take --------      MT5 & MT4 plugins, applications, and services
//| profit ------      for medium-sized brokers.
//| techno ------
//| logy --------      www.takeprofit.technology
//| 
//| This product was developed by Takeprofit Technology.
//| All rights reserved. Distribution and use of this file is prohibited unless explicitly granted by written agreement.

// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently

#pragma once

#ifndef _WIN32_WINNT          // Allow use of features specific to Windows XP or later.                   
#define _WIN32_WINNT 0x0601   // Change this to the appropriate value to target other versions of Windows.
#endif

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

#define PERFORMANCE_DISABLE_PERFORMANCE_COUNTER
#define PERFORMANCE_DISABLE_QUICKFIX_INPUT_VALIDATION

#include <windows.h>
#include <limits.h>

#include <exception>
#include <string>
#include <iostream>
#include <sstream>
#include <memory>
#include <optional>

#include "Const.h"

#include <pluginbase/base.h>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/tokenizer.hpp>
#include <boost/scope_exit.hpp>
#include <boost/math/special_functions/fpclassify.hpp>
#include <boost/date_time.hpp>
#include <boost/regex.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include <pluginbase/stat.h>
#include <pluginbase/tools.h>
#include <pluginbase/threadpool.h>
#include <pluginbase/uniqueidgenerator.h>