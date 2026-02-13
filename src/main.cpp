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
#include "WindowCapturer.hpp"

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

int main() {
    // 1. Initialization
    ConfigManager configManager("config/config.json");
    const AppConfig& config = configManager.getConfig();
    DisplayInfo targetDisplay = selectTargetDisplay();
    
    // 2. Core Subsystems
    ableton::Link* link = loadAbletonLink(config);
    AssetManager assetManager(config);
    assetManager.initializeAssets();

    // --- Window Capture Setup ---
    WindowCapturer capturer;
    bool isCaptureMode = false; 
    // You can set this via config or a key toggle later
    
    // Identify the target window (e.g., your browser)
    // If not found, it prints the list of available windows automatically
    if (capturer.startCapture("Arc")) {  // <-- NEW
        std::cout << "[INFO] Window capture target initialized.\n";
    }
    else {
        std::cout << "[ERROR] Failed starting capture.\n";
    }

    try {
        // 3. State & Graphics Instantiation
        // RuntimeState automatically loads default assets and sets timestamps
        RuntimeState state(assetManager);
        
        // GraphicsManager handles window creation and scaling logic
        GraphicsManager graphics(config, targetDisplay);

        std::cout << "[INFO] System initialized. Starting loop.\n";

        bool isCaptureMode = false; // Toggle variable 🔄

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

            // 3. Apply Strobe (if Space is held)
            outputFrame = graphics.applyStrobeEffect(outputFrame, state, tempo);

            // 2. The Logic Switch 🎛️
            if (isCaptureMode) {
                std::cout << "1" << std::endl;
                // Grab the live window content
                bool success = capturer.getLatestFrame(outputFrame);
                std::cout << "2" << std::endl;
                
                if (success) {
                    std::cout << "3" << std::endl;
                    // Scale it to fit the display using your GraphicsManager function
                    // We bypass composeFrame() to avoid asset blending
                    outputFrame = graphics.scaleToFit(outputFrame, targetDisplay.width, targetDisplay.height);
                    std::cout << "4" << std::endl;
                }
            }
            
            std::cout << "5" << std::endl;


            // 4. Render
            graphics.show(outputFrame);

            // 5. FPS Control & Input
            int key = graphics.enforceFramePacing(state);

            // --- Input Handling ---
            if (key == 27) break; // ESC

            if (key == '-') { // Toggle Capture Mode
                isCaptureMode = !isCaptureMode;
            }

            if (key > 0) {
                if (key == 'm') {
                    state.isAutoMode = !state.isAutoMode;
                    if (state.isAutoMode) {
                        // Reset beat counter when enabling so it doesn't switch immediately if previously old
                        state.lastSwitchBeat = currentBeat; 
                        std::cout << std::endl << "[AUTO] Enabled. Energy: " << state.currentEnergyTarget << "\n";
                    } else {
                        std::cout << std::endl << "[AUTO] Disabled.\n";
                    }
                }
                else if (key == 'j') {
                    state.currentEnergyTarget = LOW;
                    // std::cout << std::endl << "[ENERGY] Set to LOW\n";
                }
                else if (key == 'k') {
                    state.currentEnergyTarget = MID;
                    // std::cout << std::endl << "[ENERGY] Set to MID\n";
                }
                else if (key == 'l') { // Note: 'l' was used for Link toggle in previous code. You might want to remap Link to 'L' (shift+l) or another key.
                    // Assuming 'l' is now High Energy
                    state.currentEnergyTarget = HIGH;
                    // std::cout << std::endl << "[ENERGY] Set to HIGH\n";
                }
                else if (key == 'z') { // Toggle Link
                    state.linkEnabled = !state.linkEnabled;
                    link->enable(state.linkEnabled);
                    std::cout << std::endl << "[LINK] Set" << (state.linkEnabled ? "enabled" : "disabled") << ".\n";
                    if (!state.linkEnabled) state.queuedForeground = std::nullopt;
                }
                else if (key == 'x') { // Manual Sync
                    // 1. Align the Beat Grid (Link Logic)
                    manualSync(link);

                    // 2. Reset the Visual Counter (Auto-Mode Logic)
                    // We must re-capture the time because manualSync just changed it slightly
                    auto syncNow = std::chrono::microseconds(link->clock().micros());
                    auto syncTimeline = link->captureAppSessionState();
                    double syncedBeat = syncTimeline.beatAtTime(syncNow, 4);

                    // Tell the state that the "last switch" effectively happened NOW.
                    state.lastSwitchBeat = syncedBeat;
                    
                    // Force the NEXT switch to be standard 32 beats.
                    // This guarantees that 32 beats from pressing 'r', the visual will change.
                    state.nextSwitchDuration = 32;

                    std::cout << std::endl << "[AUTO] Counter reset. Next switch in 32 beats.\n";
                }
                else if (key == 'b') { // Toggle Bounce
                    state.isBounceActive = !state.isBounceActive;
                }
                else { // Asset Switching
                    auto bg = assetManager.getBackroundByPressedKey(key);
                    auto fg = assetManager.getForegroundByPressedKey(key);

                    if (bg.has_value()) {
                        state.switchBackground(bg.value());
                    } 
                    else if (fg.has_value()) {
                        state.queueForeground(fg.value());
                        // std::cout << "Queued foreground: " << fg->get_foreground_path() << "\n";
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

            if (isCaptureMode) {
                std::cout << "\n[RUNTIME] " << (isCaptureMode ? "Capturing Window" : "Normal Assets") << "\n";
            }
            else {
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