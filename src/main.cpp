#include <iostream>
#include <vector>
#include <string>
#include <optional>
#include <algorithm>
#include <filesystem>
#include <thread>
#include <chrono>
#include <ableton/Link.hpp>

// Internal Headers
#include "ConfigManager.h"
#include "AssetManager.h"
#include "PlatformSpecificCode.h"
#include "AbletonLinkManager.h"
#include "RuntimeState.h"
#include "GraphicsManager.h"

namespace fs = std::filesystem;

std::string getEnergyLabel(EnergyLevel level) {
    switch (level) {
        case LOW: return "LOW";
        case MID: return "MID";
        case HIGH: return "HIGH";
        case MANUAL: return "MANUAL";
        default: return "???";
    }
}

// Converte i simboli generati dallo SHIFT (Layout ITA e US) nel loro numero/carattere base
int normalizeKey(int key) {
    switch (key) {
        // Layout ITA e US
        case '!': return '1';
        case '"': return '2'; // ITA
        case 163: return '3'; // ITA (£ - Nota: a volte gestito come multi-byte, ma 163 è il Latin-1)
        case '$': return '4';
        case '%': return '5';
        case '&': return '6'; // ITA/US
        case '/': return '7'; // ITA
        case '(': return '8'; // ITA (in US è Shift+9, gestiamolo come 8 per ITA se prevale)
        case ')': return '9'; // ITA (in US è Shift+0)
        case '=': return '0'; // ITA
        // Aggiungi altri se usi layout particolari
        default: return std::tolower(key);
    }
}

int main() {
    // 1. Initialization
    ConfigManager configManager("config/config.json");
    const AppConfig& config = configManager.getConfig();
    DisplayInfo targetDisplay = selectTargetDisplay();
    
    // 2. Core Subsystems
    ableton::Link* link = loadAbletonLink(config);
    AssetManager assetManager(config);
    assetManager.initializeAssets();

    try {
        // 3. State & Graphics Instantiation
        // RuntimeState automatically loads default assets and sets timestamps
        RuntimeState state(assetManager);
        
        // GraphicsManager handles window creation and scaling logic
        GraphicsManager graphics(config, targetDisplay);

        std::cout << "[INFO] System initialized. Starting loop.\n";

        // 4. Main Loop
        while (true) {
            // --- Ableton Link Data ---
            size_t peers = link->numPeers();
            auto now = std::chrono::microseconds(link->clock().micros());
            auto timeline = link->captureAppSessionState();
            double currentBeat = timeline.beatAtTime(now, 4);
            double tempo = timeline.tempo();

            if (state.isAutoMode) {
                // Check if enough beats have passed since the last switch
                if (currentBeat - state.lastSwitchBeat >= state.nextSwitchDuration) {
                    // Access assets via a getter (you might need to add getAssets() to AssetManager)
                    state.triggerAutoSwitch(assetManager.getAssets(), currentBeat);
                }
            }

            // --- Graphics Pipeline ---
            // 1. Calculate animation scale (Bounce)
            double scale = graphics.calculateBounceScale(state, currentBeat, tempo, now);

            // 2. Compose Frame (Get BG -> Resize -> Blend FG)
            cv::Mat outputFrame = graphics.composeFrame(state, assetManager, scale);
            
            // Crea anche il frame di CUE/Preview (per ora non applica strobo)
            cv::Mat previewFrame = graphics.composeCueFrame(state, assetManager, scale);

            // 3. Apply Strobe (if Space is held) all'output Live
            outputFrame = graphics.applyStrobeEffect(outputFrame, state, tempo);

            // 4. Render (mostriamo il live sulla finestra principale e il CUE sulla preview)
            cv::imshow(config.windowName, outputFrame);
            cv::imshow("Laptop_Monitor_Preview", previewFrame);

            // 5. FPS Control & Input
            int key = graphics.enforceFramePacing(state);

            // --- Input Handling ---
            // --- Input Handling ---
            if (key == 27) break; // ESC
            
            if (key == 13) { // Tasto INVIO (Enter) - Transizione CUE -> LIVE
                state.transitionCueToLive();
                std::cout << "\n[CUE] Transizione a LIVE eseguita.\n";
            }
            else if (key > 0) {
                // Rendi la gestione comandi case-insensitive per le funzioni di sistema
                int lowerKey = std::tolower(key);
                
                if (lowerKey == 'm') {
                    state.isAutoMode = !state.isAutoMode;
                    if (state.isAutoMode) {
                        state.lastSwitchBeat = currentBeat; 
                        std::cout << std::endl << "[AUTO] Enabled. Energy: " << state.currentEnergyTarget << "\n";
                    } else {
                        std::cout << std::endl << "[AUTO] Disabled.\n";
                    }
                }
                else if (lowerKey == 'j') {
                    state.currentEnergyTarget = LOW;
                }
                else if (lowerKey == 'k') {
                    state.currentEnergyTarget = MID;
                }
                else if (lowerKey == 'l') { 
                    state.currentEnergyTarget = HIGH;
                }
                else if (lowerKey == 'z') { 
                    state.linkEnabled = !state.linkEnabled;
                    link->enable(state.linkEnabled);
                    std::cout << std::endl << "[LINK] Set " << (state.linkEnabled ? "enabled" : "disabled") << ".\n";
                    if (!state.linkEnabled) state.queuedForeground = std::nullopt;
                }
                else if (lowerKey == 'x') { 
                    manualSync(link);
                    auto syncNow = std::chrono::microseconds(link->clock().micros());
                    auto syncTimeline = link->captureAppSessionState();
                    double syncedBeat = syncTimeline.beatAtTime(syncNow, 4);
                    state.lastSwitchBeat = syncedBeat;
                    state.nextSwitchDuration = 32;
                    std::cout << std::endl << "[AUTO] Counter reset. Next switch in 32 beats.\n";
                }
                else if (lowerKey == 'b') { 
                    state.isBounceActive = !state.isBounceActive;
                }
                else { 
                    // Asset Switching: usa il check di sistema per lo Shift
                    bool isShiftPressed = isShiftDown() || std::isupper(key);
                    int assetKey = normalizeKey(key); 
                    
                    auto bg = assetManager.getBackroundByPressedKey(assetKey);
                    auto fg = assetManager.getForegroundByPressedKey(assetKey);

                    if (bg.has_value()) {
                        if (isShiftPressed) {
                            state.switchCueBackground(bg.value());
                        } else {
                            state.switchBackground(bg.value());
                        }
                    } 
                    else if (fg.has_value()) {
                        if (isShiftPressed) {
                            state.switchCueForeground(fg.value());
                        } else {
                            state.switchForeground(fg.value()); // Cambio istantaneo LIVE
                        }
                    }
                }
            }
            
            // --- Dashboard Output ---
            // Calculate beats remaining for auto switch
            int beatsRemaining = 0;
            if (state.isAutoMode) {
                beatsRemaining = state.nextSwitchDuration - static_cast<int>(currentBeat - state.lastSwitchBeat);
                if (beatsRemaining < 0) beatsRemaining = 0;
            }

            std::cout << "\r" 
                      << "[RUNTIME] LINK:" << (state.linkEnabled ? "ON" : "OFF") << " - " << peers << " - " << std::fixed << std::setprecision(1) << tempo 
                      << " | AUTO: " << (state.isAutoMode ? "ON" : "OFF");
            
            if (state.isAutoMode) {
                std::cout << " |" << std::setw(6) << std::left << getEnergyLabel(state.currentEnergyTarget) 
                          << "| NEXT IN:" << std::setw(3) << beatsRemaining;
            } else {
                std::cout << "|------|-------";
            }

            std::cout << "  BOUNCE: " << (state.isBounceActive ? "ON" : "OFF")
                      << "  BG: [" << state.activeBackground.get_key() << "]"
                      << "  FG: [" << (state.activeForeground.get_foreground_path()) << "]"
                      << "      " << std::flush; // Extra spaces to clear trailing characters
        }

    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1;
    }

    // Cleanup
    link->enable(false);
    delete link;
    return 0;
}