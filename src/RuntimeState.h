#pragma once

#include <chrono>
#include <optional>
#include <iostream>
#include <random>
#include "AssetManager.h"

// Forward declaration if needed, or include strictly what is necessary
struct RuntimeState {
    // Animation Flags
    bool isBounceActive;
    bool isAnimating;
    bool strobeFrameToggle;
    bool linkEnabled;

    // Auto Mode State
    bool isAutoMode = false;         // 'm' toggles this
    EnergyLevel currentEnergyTarget = MID; // 'j', 'k', 'l' sets this
    double lastSwitchBeat = 0.0;     // When did we last switch?
    int nextSwitchDuration = 32;     // How many beats until next switch?

    // Timing & Beat Tracking
    double lastBeat;
    std::chrono::microseconds animationStartTime;
    long long lastFrameTime;
    
    // Strobe specific
    std::optional<std::chrono::steady_clock::time_point> nextStrobeTime;

    // Active Assets
    Background activeBackground;
    Foreground activeForeground;
    std::optional<Foreground> queuedForeground;

    // Constructor
    RuntimeState(AssetManager& assetManager);
    
    // Helper methods
    void switchBackground(const Background& newBg);
    void queueForeground(const Foreground& newFg);
    void triggerAutoSwitch(const AssetsConfig& assets, double currentBeat);
};