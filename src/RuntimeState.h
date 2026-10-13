#pragma once

#include <chrono>
#include <optional>
#include <iostream>
#include <random>
#include "AssetManager.h"

struct RuntimeState {
    // Animation Flags
    bool isBounceActive;
    bool isAnimating;
    bool strobeFrameToggle;
    bool linkEnabled;

    // Auto Mode State
    bool isAutoMode = false;         // 'm' toggles this
    EnergyLevel currentEnergyTarget = MID; // 'j', 'k', 'l' sets this
    double lastSwitchBeat = 0.0;     
    int nextSwitchDuration = 32;     

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

    int captureFailCount = 0;

    // CUE (Preview) Assets
    std::optional<Background> cueBackground;
    std::optional<Foreground> cueForeground;

    // Constructor
    RuntimeState(AssetManager& assetManager);
    
    // Helper methods
    void switchBackground(const Background& newBg);
    void switchForeground(const Foreground& newFg);
    
    // Metodi CUE
    void switchCueBackground(const Background& newBg);
    void switchCueForeground(const Foreground& newFg);
    
    void triggerAutoSwitch(const AssetsConfig& assets, double currentBeat);
    void transitionCueToLive();

    // Metodo helper per capire se c'è un preview caricato
    bool hasActiveCue() const;
};