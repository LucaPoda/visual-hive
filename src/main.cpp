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
#include "AudioAnalyzer.h"

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

    // Inizializza e avvia il thread DSP Audio
    AudioAnalyzer audioAnalyzer;
    audioAnalyzer.start();

    // Wait for a moment to ensure the audio thread is running and capturing data
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    try {
        RuntimeState state(assetManager);
        GraphicsManager graphics(config, targetDisplay);

        std::cout << "[INFO] System initialized. Starting loop.\n";

        while (!graphics.shouldCloseUI()) {
            
            // --- 1. START FRAME ---
            graphics.beginDashboard();

            // --- 1.5 LEGGI AUDIO DSP ---
            AudioMetrics audioData = audioAnalyzer.getMetrics();

            // --- PANNELLO IMGUI AUDIO ---
            ImGui::Begin("Audio DSP Analysis");
            ImGui::Text("BPM Rilevati (Bassi): %.1f", audioData.bpm);
            ImGui::ProgressBar(audioData.rms * 5.0f, ImVec2(-1.0f, 0.0f), "RMS"); // Moltiplicatore per visibilità
            ImGui::ProgressBar(audioData.spectralFlux, ImVec2(-1.0f, 0.0f), "Spectral Flux");
            ImGui::End();

            // --- 2. INPUT HANDLING (Gestito SOLO tramite ImGui) ---
            ImGuiIO& io = ImGui::GetIO();
            
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) break;
            if (ImGui::IsKeyPressed(ImGuiKey_Enter)) state.transitionCueToLive();

            if (ImGui::IsKeyPressed(ImGuiKey_M)) {
                state.isAutoMode = !state.isAutoMode;
                if (state.isAutoMode) state.lastSwitchBeat = link->captureAppSessionState().beatAtTime(std::chrono::microseconds(link->clock().micros()), 4); 
            }
            
            if (ImGui::IsKeyPressed(ImGuiKey_J)) state.currentEnergyTarget = LOW;
            if (ImGui::IsKeyPressed(ImGuiKey_K)) state.currentEnergyTarget = MID;
            if (ImGui::IsKeyPressed(ImGuiKey_L)) state.currentEnergyTarget = HIGH;
            
            if (ImGui::IsKeyPressed(ImGuiKey_Z)) { 
                state.linkEnabled = !state.linkEnabled;
                link->enable(state.linkEnabled);
                if (!state.linkEnabled) state.queuedForeground = std::nullopt;
            }
            
            if (ImGui::IsKeyPressed(ImGuiKey_X)) { 
                manualSync(link);
                auto syncNow = std::chrono::microseconds(link->clock().micros());
                auto syncTimeline = link->captureAppSessionState();
                state.lastSwitchBeat = syncTimeline.beatAtTime(syncNow, 4);
                state.nextSwitchDuration = 32;
            }
            
            if (ImGui::IsKeyPressed(ImGuiKey_B)) { 
                state.isBounceActive = !state.isBounceActive;
            }

            // Selezione Asset tramite tasti numerici e alfabetici
            auto handleAssetMapping = [&](char keyChar) {
                bool isShiftPressed = io.KeyShift;

                auto bg = assetManager.getBackroundByPressedKey(keyChar);
                auto fg = assetManager.getForegroundByPressedKey(keyChar);

                if (bg.has_value()) {
                    if (isShiftPressed) state.switchCueBackground(bg.value());
                    else state.switchBackground(bg.value());
                } 
                if (fg.has_value()) {
                    if (isShiftPressed) state.switchCueForeground(fg.value());
                    else state.switchForeground(fg.value());
                }
            };

            // Scansione tasti numerici (0-9)
            for (int i = ImGuiKey_0; i <= ImGuiKey_9; i++) {
                if (ImGui::IsKeyPressed((ImGuiKey)i, false)) {
                    handleAssetMapping('0' + (i - ImGuiKey_0)); 
                }
            }
            // Scansione tasti alfabetici (A-Z)
            for (int i = ImGuiKey_A; i <= ImGuiKey_Z; i++) {
                if (ImGui::IsKeyPressed((ImGuiKey)i, false)) {
                    handleAssetMapping('a' + (i - ImGuiKey_A)); 
                }
            }

            // --- 3. ABLETON LINK & LOGICA AUTO-MODE ---
            auto now = std::chrono::microseconds(link->clock().micros());
            auto timeline = link->captureAppSessionState();
            double currentBeat = timeline.beatAtTime(now, 4);
            double tempo = timeline.tempo();

            if (state.isAutoMode) {
                if (currentBeat - state.lastSwitchBeat >= state.nextSwitchDuration) {
                    state.triggerAutoSwitch(assetManager.getAssets(), currentBeat);
                }
            }

            // --- 4. GRAPHICS PIPELINE ---
            double scale = graphics.calculateBounceScale(state, currentBeat, tempo, now);

            cv::Mat outputFrame = graphics.composeFrame(state, assetManager, scale);
            cv::Mat previewFrame;
            
            if (state.hasActiveCue()) {
                previewFrame = graphics.composeCueFrame(state, assetManager, scale);
            } else {
                previewFrame = outputFrame; 
            }

            // Applica strobo usando la barra spaziatrice letta da ImGui
            bool isSpacePressed = ImGui::IsKeyDown(ImGuiKey_Space);
            outputFrame = graphics.applyStrobeEffect(outputFrame, state, tempo, isSpacePressed);

            // --- 5. END FRAME (Dashboard Rendering) ---
            graphics.endDashboard(outputFrame, previewFrame, state, tempo, audioAnalyzer);            
            
            // --- 6. AGGIORNA PROIETTORE ---
            // Renderizza direttamente il frame sulla GPU usando la seconda finestra GLFW
            graphics.showLive(outputFrame);
            
            // Nota: graphics.enforceFramePacing() è stato rimosso definitivamente
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