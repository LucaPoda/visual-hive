#include "RuntimeState.h"
#include <opencv2/core/utility.hpp> // For cv::getTickCount
#include <stdexcept>

RuntimeState::RuntimeState(AssetManager& assetManager) 
    : isBounceActive(false),
      isAnimating(false),
      strobeFrameToggle(false),
      linkEnabled(true),
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