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

        std::cout << "System initialized. Starting loop.\n";

        // 4. Main Loop
        while (true) {
            // --- Ableton Link Data ---
            size_t peers = link->numPeers();
            auto now = std::chrono::microseconds(link->clock().micros());
            auto timeline = link->captureAppSessionState();
            double currentBeat = timeline.beatAtTime(now, 4);
            double tempo = timeline.tempo();

            // --- Graphics Pipeline ---
            // 1. Calculate animation scale (Bounce)
            double scale = graphics.calculateBounceScale(state, currentBeat, tempo, now);

            // 2. Compose Frame (Get BG -> Resize -> Blend FG)
            cv::Mat outputFrame = graphics.composeFrame(state, assetManager, scale);

            // 3. Apply Strobe (if Space is held)
            outputFrame = graphics.applyStrobeEffect(outputFrame, state, tempo);

            // 4. Render
            graphics.show(outputFrame);

            // 5. FPS Control & Input
            int key = graphics.enforceFramePacing(state);

            // --- Input Handling ---
            if (key == 27) break; // ESC

            if (key > 0) {
                if (key == 'l') { // Toggle Link
                    state.linkEnabled = !state.linkEnabled;
                    link->enable(state.linkEnabled);
                    std::cout << "Ableton Link " << (state.linkEnabled ? "enabled" : "disabled") << ".\n";
                    if (!state.linkEnabled) state.queuedForeground = std::nullopt;
                }
                else if (key == 'r') { // Manual Sync
                    manualSync(link);
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
                        std::cout << "Queued foreground: " << fg->get_foreground_path() << "\n";
                    }
                }
            }
            
            // Console Status
            std::cout << "LINK: " << peers << " | BPM: " << std::fixed << std::setprecision(2) << tempo << std::flush << "\r";
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