#pragma once

#include <chrono>
#include <optional>
#include <iostream>
#include <random>
#include <vector>
#include <string>
#include "AssetManager.h"

struct RuntimeState {
    // Animation Flags
    bool isBounceActive;
    bool isAnimating;
    bool strobeFrameToggle;

    // Auto Mode & ML State
    bool isAutoMode = false;         // 'M' attiva/disattiva
    SongDynamic currentSongDynamicTarget = BUILD; // 'J', 'K', 'L' impostano manualmente

    // New State Variables
    bool isBlackout = false;
    EventPhase currentEventPhase = AMBIENT;
    int colorMode = 0; // 0 = Color Mode, 1 = Palette Mode
    std::vector<std::string> activePalettes;

    // Timing
    std::chrono::microseconds animationStartTime;
    long long lastFrameTime;
    
    // Strobe specific
    std::optional<std::chrono::steady_clock::time_point> nextStrobeTime;

    // Active Assets
    Background activeBackground;
    Foreground activeForeground;
    std::optional<Foreground> queuedForeground;

    int captureFailCount = 0;

    // CUE (Preview) Assets
    std::optional<Background> cueBackground;
    std::optional<Foreground> cueForeground;

    // Constructor
    RuntimeState(AssetManager& assetManager);
    
    // Asset Management
    void switchBackground(const Background& newBg);
    void switchForeground(const Foreground& newFg);
    
    // CUE Methods
    void switchCueBackground(const Background& newBg);
    void switchCueForeground(const Foreground& newFg);
    
    // Auto Switch
    void triggerAutoSwitch(const AssetsConfig& assets);
    void transitionCueToLive();

    // Helper
    bool hasActiveCue() const;
};