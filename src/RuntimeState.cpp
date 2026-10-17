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
      isBlackout(false),
      currentEventPhase(AMBIENT),
      colorMode(0),
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

    cueBackground = defaultBg.value();
    cueForeground = defaultFg.value();

    if (cueBackground.has_value() && !cueBackground->open()) {
        std::cerr << "Failed to open default cue background.\n";
    }
    if (cueForeground.has_value() && !cueForeground->open()) {
        std::cerr << "Failed to open default cue foreground.\n";
    }

    Background blackBg;
    blackBg.set_source(SOLID_COLOR, "#000000");
    blackBg.set_force_no_foreground(true);
    blackBg.open();
    activeBackground = blackBg;

    activeForeground = Foreground(); 
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
        
        // Filtro Colore / Palette
        if (!activePalettes.empty()) {
            bool valid = false;
            if (colorMode == 0) { 
                // COLOR MODE: Se definiti, entrambi i colori dell'asset devono essere spuntati
                bool primOk = bg.get_primary_color().empty() || std::find(activePalettes.begin(), activePalettes.end(), bg.get_primary_color()) != activePalettes.end();
                bool secOk = bg.get_secondary_color().empty() || std::find(activePalettes.begin(), activePalettes.end(), bg.get_secondary_color()) != activePalettes.end();
                
                // Salta l'asset se non matcha le regole colore (esclude anche quelli senza colori se si cerca un colore specifico)
                if (bg.get_primary_color().empty() && bg.get_secondary_color().empty()) valid = false;
                else valid = (primOk && secOk);
                
            } else { 
                // PALETTE MODE: L'esatta combinazione "Primario-Secondario" deve essere attiva
                std::string palKey = bg.get_primary_color() + (bg.get_secondary_color().empty() ? "" : "-" + bg.get_secondary_color());
                valid = std::find(activePalettes.begin(), activePalettes.end(), palKey) != activePalettes.end();
            }
            
            if (!valid) continue;
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