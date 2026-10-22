#pragma once
#include "paper2_scotland2/shared/logger.hpp"
#include "scotland2/shared/loader.hpp"
#include "scotland2/shared/modloader.h"

#ifndef MOD_EXTERN_FUNC
#define MOD_EXTERN_FUNC extern "C" __attribute__((visibility("default")))
#endif

#ifndef MOD_ID
#define MOD_ID "twitch-bsr"
#endif
#ifndef VERSION
#define VERSION "0.1.0"
#endif

constexpr auto Logger = Paper::ConstLoggerContext(MOD_ID);
#define INFO(...) Logger.info(__VA_ARGS__)
