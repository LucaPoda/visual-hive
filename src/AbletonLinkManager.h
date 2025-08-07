#pragma once

#include "ConfigManager.h"
#include <ableton/Link.hpp>

extern int PHRASE_LENGTH;
extern int BPM;

ableton::Link * loadAbletonLink(const AppConfig & config);
