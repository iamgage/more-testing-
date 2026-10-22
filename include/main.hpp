#pragma once
#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "beatsaber-hook/shared/utils/logging.hpp"
#include "scotland2/shared/loader.hpp"

#ifndef MOD_ID
#define MOD_ID "twitch-bsr"
#endif
#ifndef VERSION
#define VERSION "0.1.0"
#endif

Logger& getLogger();
#define INFO(...) getLogger().info(__VA_ARGS__)
