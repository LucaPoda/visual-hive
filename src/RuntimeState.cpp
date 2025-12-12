#include "RuntimeState.h"
#include <opencv2/core/utility.hpp> // For cv::getTickCount
#include <stdexcept>

RuntimeState::RuntimeState(AssetManager& assetManager) 
    : isBounceActive(false),
      isAnimating(false),
      strobeFrameToggle(false),
      linkEnabled(true),
      isAutoMode(false),           // Default OFF
      currentEnergyTarget(LOW),    // Default LOW
      lastSwitchBeat(0.0),
      nextSwitchDuration(32),      // Start with 32 beats
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

    // IMPORTANT: Background is a copy, but open() needs to modify the internal capture object.
    // However, the object is inside the 'activeBackground' instance now.
    // We must call open() on the *member variable*, not the input const reference.
    if (activeBackground.open()) {
        // Apply queued foreground if waiting
        if (queuedForeground.has_value()) {
            activeForeground = queuedForeground.value();
            queuedForeground = std::nullopt;
        }
    }
}

void RuntimeState::queueForeground(const Foreground& newFg) {
    if (activeForeground.get_foreground_path() != newFg.get_foreground_path()) {
        queuedForeground = newFg;
    }
}

void RuntimeState::triggerAutoSwitch(const AssetsConfig& assets, double currentBeat) {
    std::vector<Background> candidates;

    for (const auto& [key, bg] : assets.get_backgrounds()) {
        // 1. Don't repeat current
        if (bg.get_source() == activeBackground.get_source()) continue;

        // 2. Only pick assets matching current energy level
        if (bg.get_energy_level() == currentEnergyTarget) {
            candidates.push_back(bg);
        }
    }

    // Fallback: If no candidates (e.g., config error), just do nothing rather than picking something random/wrong
    if (candidates.empty()) return;

    // 3. Random Selection
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> distr(0, candidates.size() - 1);
    
    Background selected = candidates[distr(gen)];
    
    // 4. Switch
    switchBackground(selected);

    // 5. Update Timers
    lastSwitchBeat = currentBeat;
    
    // Random duration: 32, 64, 96, 128
    int durations[] = {32, 64};
    std::uniform_int_distribution<> durDist(0, 1);
    nextSwitchDuration = durations[durDist(gen)];
    if (currentEnergyTarget == HIGH)
        nextSwitchDuration = durations[0];

    std::cout << "[AUTO] Switched to " << selected.get_source() 
              << " (" << (currentEnergyTarget == LOW ? "LOW" : (currentEnergyTarget == MID ? "MID" : "HIGH")) 
              << "). Next in " << nextSwitchDuration << " beats.\n";
}