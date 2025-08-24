#pragma once

#include "ConfigManager.h"
#include <ableton/Link.hpp>

extern int PHRASE_LENGTH;
extern int BPM;

std::shared_ptr<ableton::Link> loadAbletonLink(const AppConfig& config);
