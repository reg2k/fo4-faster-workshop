#pragma once
#include "f4se_common/f4se_version.h"

//-----------------------
// Plugin Information
//-----------------------
#define PLUGIN_VERSION              2
#define PLUGIN_VERSION_STRING       "1.1"
#define PLUGIN_NAME_SHORT           "faster_workshop"
#define PLUGIN_NAME_LONG            "Faster Workshop"
#define SUPPORTED_RUNTIME_VERSION   CURRENT_RELEASE_RUNTIME
#define MINIMUM_RUNTIME_VERSION     RUNTIME_VERSION_1_9_4
#define COMPATIBLE(runtimeVersion)  (runtimeVersion >= MINIMUM_RUNTIME_VERSION)

//-------------
// Addresses
//-------------
// [Count]  Location
// [6]      main.cpp
