#include "RuntimeState.h"
#include <opencv2/core/utility.hpp> 
#include <stdexcept>

RuntimeState::RuntimeState(AssetManager& assetManager) 
    : isBounceActive(false),
      isAnimating(false),
      strobeFrameToggle(false),
      linkEnabled(true),
      isAutoMode(false),           
      currentEnergyTarget(MID),    
      lastSwitchBeat(0.0),
      nextSwitchDuration(32),      
      lastBeat(0.0),
      animationStartTime(0),
      lastFrameTime(0),
      nextStrobeTime(std::nullopt),
      queuedForeground(std::nullopt)
{
    auto defaultBg = assetManager.getDefaultBackground();
    auto defaultFg = assetManager.getDefaultForeground();

    if (!defaultBg || !defaultFg) {
        throw std::runtime_error("Missing default background or foreground assets.");
    }

    activeBackground = defaultBg.value();
    activeForeground = defaultFg.value();

    if (!activeBackground.open()) {
        std::cerr << "Failed to open default background.\n";
    }
    if (!activeForeground.open()) {
        std::cerr << "Failed to open default foreground.\n";
    }
    
    lastFrameTime = cv::getTickCount();
}

void RuntimeState::switchBackground(const Background& newBg) {
    if (activeBackground.get_source() == newBg.get_source()) return;
    
    activeBackground.close();
    activeBackground = newBg;

    if (activeBackground.open()) {
        if (queuedForeground.has_value()) {
            activeForeground = queuedForeground.value();
            queuedForeground = std::nullopt;
        }
    }
}

void RuntimeState::switchForeground(const Foreground& newFg) {
    if (activeForeground.get_foreground_path() == newFg.get_foreground_path()) return;
    
    activeForeground = newFg;
    activeForeground.open();
}

void RuntimeState::switchCueBackground(const Background& newBg) {
    if (cueBackground.has_value() && cueBackground->get_source() == newBg.get_source()) return;
    
    if (cueBackground.has_value()) {
        cueBackground->close();
    }
    cueBackground = newBg;
    cueBackground->open();
}

void RuntimeState::switchCueForeground(const Foreground& newFg) {
    if (cueForeground.has_value() && cueForeground->get_foreground_path() == newFg.get_foreground_path()) return;
    
    cueForeground = newFg;
    cueForeground->open();
}

void RuntimeState::triggerAutoSwitch(const AssetsConfig& assets, double currentBeat) {
    std::vector<Background> candidates;

    for (const auto& [key, bg] : assets.get_backgrounds()) {
        if (bg.get_source() == activeBackground.get_source()) continue;
        if (bg.get_energy_level() == currentEnergyTarget) {
            candidates.push_back(bg);
        }
    }

    if (candidates.empty()) return;

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> distr(0, candidates.size() - 1);
    
    Background selected = candidates[distr(gen)];
    switchBackground(selected);
    lastSwitchBeat = currentBeat;
    
    int durations[] = {32, 64};
    std::uniform_int_distribution<> durDist(0, 1);
    nextSwitchDuration = durations[durDist(gen)];
    if (currentEnergyTarget == HIGH)
        nextSwitchDuration = durations[0];
}

void RuntimeState::transitionCueToLive() {
    if (cueBackground.has_value()) {
        switchBackground(cueBackground.value());
        cueBackground = std::nullopt; 
    }
    
    if (cueForeground.has_value()) {
        activeForeground = cueForeground.value();
        cueForeground = std::nullopt; 
    }
}

bool RuntimeState::hasActiveCue() const {
    return cueBackground.has_value() || cueForeground.has_value();
}