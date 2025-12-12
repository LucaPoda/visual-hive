#pragma once

#include <chrono>
#include <optional>
#include <iostream>
#include "AssetManager.h"

// Forward declaration if needed, or include strictly what is necessary
struct RuntimeState {
    // Animation Flags
    bool isBounceActive;
    bool isAnimating;
    bool strobeFrameToggle;
    bool linkEnabled;

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
};