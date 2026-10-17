// RuntimeState.cpp
#include "RuntimeState.h"
#include <opencv2/core/utility.hpp> 
#include <stdexcept>
#include <algorithm>

RuntimeState::RuntimeState(AssetManager& assetManager) 
    : isBounceActive(false),
      isAnimating(false),
      strobeFrameToggle(false),
      isAutoMode(false),           
      currentSongDynamicTarget(BUILD),
      isBlackout(false), // o true se vuoi il proiettore oscurato
      currentEventPhase(AMBIENT),
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

    // 1. CUE viene caricato con il Background e Foreground di default
    cueBackground = defaultBg.value();
    cueForeground = defaultFg.value();

    if (cueBackground.has_value() && !cueBackground->open()) {
        std::cerr << "Failed to open default cue background.\n";
    }
    if (cueForeground.has_value() && !cueForeground->open()) {
        std::cerr << "Failed to open default cue foreground.\n";
    }

    // 2. LIVE rimane vuoto: inizializzato a nero (#000000) e senza foreground
    Background blackBg;
    blackBg.set_source(SOLID_COLOR, "#000000");
    blackBg.set_force_no_foreground(true);
    blackBg.open();
    activeBackground = blackBg;

    activeForeground = Foreground(); // Nessun foreground caricato
    
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

void RuntimeState::triggerAutoSwitch(const AssetsConfig& assets) {
    std::vector<Background> candidates;

    for (const auto& [key, bg] : assets.get_backgrounds()) {
        if (bg.get_source() == activeBackground.get_source()) continue;
        if (!bg.get_is_enabled_global()) continue;
        if (bg.get_event_phase() != currentEventPhase) continue;
        if (bg.get_song_dynamic() != currentSongDynamicTarget) continue;
        
        if (!activePalettes.empty()) {
            bool paletteFound = false;
            for (const auto& activePalette : activePalettes) {
                if (bg.get_primary_color() == activePalette || bg.get_secondary_color() == activePalette) {
                    paletteFound = true;
                    break;
                }
            }
            if (!paletteFound) continue;
        }

        candidates.push_back(bg);
    }

    if (candidates.empty()) return;
    
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> distr(0, candidates.size() - 1);
    switchBackground(candidates[distr(gen)]);
}

void RuntimeState::transitionCueToLive() {
    if (cueBackground.has_value()) {
        switchBackground(cueBackground.value());
    }
    
    if (cueForeground.has_value()) {
        activeForeground = cueForeground.value();
    }
}

bool RuntimeState::hasActiveCue() const {
    return cueBackground.has_value() || cueForeground.has_value();
}