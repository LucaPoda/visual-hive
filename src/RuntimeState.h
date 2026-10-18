#pragma once

#include <chrono>
#include <optional>
#include <iostream>
#include <random>
#include <vector>
#include <string>
#include "AssetManager.h"

struct RuntimeState {
    bool isBounceActive;
    bool isAnimating;
    bool strobeFrameToggle;

    bool isAutoMode = false;         
    SongDynamic currentSongDynamicTarget = BUILD; 

    bool isBlackout = false;
    EventPhase currentEventPhase = AMBIENT;
    bool useAnyEventPhase = false; 
    int colorMode = 0; 
    std::vector<std::string> activePalettes;

    std::chrono::microseconds animationStartTime;
    long long lastFrameTime;
    
    std::optional<std::chrono::steady_clock::time_point> nextStrobeTime;

    Background activeBackground;
    Foreground activeForeground;
    std::optional<Foreground> queuedForeground;

    int captureFailCount = 0;

    std::optional<Background> cueBackground;
    std::optional<Foreground> cueForeground;

    RuntimeState(AssetManager& assetManager);
    
    void switchBackground(const Background& newBg);
    void switchForeground(const Foreground& newFg);
    
    void switchCueBackground(const Background& newBg);
    void switchCueForeground(const Foreground& newFg);
    
    void triggerAutoSwitch(const AssetsConfig& assets);
    void transitionCueToLive();

    bool hasActiveCue() const;
};