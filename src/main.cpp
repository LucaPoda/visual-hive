// main.cpp
#include <iostream>
#include <vector>
#include <string>
#include <optional>
#include <algorithm>
#include <filesystem>
#include <thread>
#include <chrono>
#include <deque>

// Internal Headers
#include "ConfigManager.h"
#include "AssetManager.h"
#include "PlatformSpecificCode.h"
#include "RuntimeState.h"
#include "GraphicsManager.h"
#include "AudioAnalyzer.h"
#include "KNNClassifier.h" 

namespace fs = std::filesystem;

std::string getSongDynamicLabel(SongDynamic level) {
    switch (level) {
        case VERSE: return "VERSE";
        case BUILD: return "BUILD";
        case CHORUS: return "CHORUS";
        case MANUAL: return "MANUAL";
        default: return "???";
    }
}

KNNClassifier knnClassifier(21); 

int main() {
    ConfigManager configManager("config/config.json");
    const AppConfig& config = configManager.getConfig();
    
    knnClassifier.setK(config.knnKNeighbors);
    
    DisplayInfo targetDisplay = selectTargetDisplay();
    
    AssetManager assetManager(config);
    assetManager.initializeAssets();

    knnClassifier.loadDataset("knn_dataset.csv");

    AudioAnalyzer audioAnalyzer;
    audioAnalyzer.start(0, config.audioSampleRate, config.audioBufferFrames);

    std::cout << "[INFO] Waiting for audio warmup (2 seconds)...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    int beatsSinceLastChange = 0;
    
    std::deque<SongDynamic> labelHistory;
    const size_t HISTORY_SIZE = config.knnHistorySize; 

    SongDynamic stablePrediction = BUILD; 
    SongDynamic candidatePrediction = BUILD;
    int consecutiveFrames = 0;
    int stableFramesCount = 0; 
    
    std::map<SongDynamic, int> baseDebounce = {
        {VERSE, config.debounceVerse},  
        {BUILD, config.debounceBuild},  
        {CHORUS, config.debounceChorus}   
    };

    const int REQUIRED_CONSECUTIVE_FRAMES = 5; 

    try {
        RuntimeState state(assetManager);
        GraphicsManager graphics(config, targetDisplay);

        std::cout << "[INFO] System initialized. Starting loop.\n";

        while (!graphics.shouldCloseUI()) {
            
            graphics.beginDashboard();

            AudioMetrics audioData = audioAnalyzer.getMetrics();
            
            if (audioData.isBeat) {
                beatsSinceLastChange++;
            }

            KNNPrediction pred = { MANUAL, 0, 0, 0 };
            if (state.isAutoMode) {
                pred = knnClassifier.predict(audioData);
                
                std::optional<SongDynamic> highConfLabel = std::nullopt;
                if (pred.confVerse >= 0.90f) highConfLabel = VERSE;
                else if (pred.confBuild >= 0.90f) highConfLabel = BUILD;
                else if (pred.confChorus >= 0.90f) highConfLabel = CHORUS;

                if (highConfLabel.has_value()) {
                    if (highConfLabel.value() == candidatePrediction) {
                        consecutiveFrames++;
                    } else {
                        candidatePrediction = highConfLabel.value();
                        consecutiveFrames = 1;
                    }

                    int requiredFrames = baseDebounce[candidatePrediction];
                    
                    if (stableFramesCount > 90) {
                        requiredFrames = std::max(3, requiredFrames / 2); 
                    }

                    if (consecutiveFrames >= requiredFrames) {
                        stablePrediction = candidatePrediction;
                        consecutiveFrames = 0; 
                    }
                } else {
                    consecutiveFrames = 0;
                }

                if (stablePrediction == state.currentSongDynamicTarget) {
                    stableFramesCount++;
                } else {
                    stableFramesCount = 0; 
                }

                if (beatsSinceLastChange >= 32) {
                    state.triggerAutoSwitch(assetManager.getAssets()); 
                    beatsSinceLastChange = 0;
                } 
                else if (beatsSinceLastChange >= 4) {
                    if (stablePrediction != state.currentSongDynamicTarget) {
                        state.currentSongDynamicTarget = stablePrediction;
                        state.triggerAutoSwitch(assetManager.getAssets());
                        beatsSinceLastChange = 0;
                        
                        consecutiveFrames = 0; 
                    }
                }
            }

            ImGuiIO& io = ImGui::GetIO();
            
            bool audioActive = (audioData.peak > 0.001f);
            
            if (state.isAutoMode && !audioActive) {
                state.isAutoMode = false;
                std::cout << "[WARNING] Auto Mode disattivato (Segnale audio assente).\n";
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) break;
            if (ImGui::IsKeyPressed(ImGuiKey_Enter)) state.transitionCueToLive();

            if (ImGui::IsKeyPressed(ImGuiKey_M)) {
                if (audioActive) {
                    state.isAutoMode = !state.isAutoMode;
                    if (state.isAutoMode) beatsSinceLastChange = 0; 
                } else {
                    std::cout << "[WARNING] Impossibile attivare Auto Mode: Nessun audio rilevato.\n";
                }
            }
            
            if (ImGui::IsKeyPressed(ImGuiKey_J)) state.currentSongDynamicTarget = VERSE;
            if (ImGui::IsKeyPressed(ImGuiKey_K)) state.currentSongDynamicTarget = BUILD;
            if (ImGui::IsKeyPressed(ImGuiKey_L)) state.currentSongDynamicTarget = CHORUS;
            
            if (ImGui::IsKeyPressed(ImGuiKey_B)) { 
                state.isBounceActive = !state.isBounceActive;
            }

            auto handleAssetMapping = [&](char keyChar) {
                bool isShiftPressed = io.KeyShift;
                auto bg = assetManager.getBackroundByPressedKey(keyChar);
                auto fg = assetManager.getForegroundByPressedKey(keyChar);

                if (bg.has_value()) {
                    if (isShiftPressed) state.switchCueBackground(bg.value());
                    else {
                        state.switchBackground(bg.value());
                        beatsSinceLastChange = 0; 
                    }
                } 
                if (fg.has_value()) {
                    if (isShiftPressed) state.switchCueForeground(fg.value());
                    else state.switchForeground(fg.value());
                }
            };

            for (int i = ImGuiKey_0; i <= ImGuiKey_9; i++) {
                if (ImGui::IsKeyPressed((ImGuiKey)i, false)) handleAssetMapping('0' + (i - ImGuiKey_0)); 
            }
            for (int i = ImGuiKey_A; i <= ImGuiKey_Z; i++) {
                if (ImGui::IsKeyPressed((ImGuiKey)i, false)) handleAssetMapping('a' + (i - ImGuiKey_A)); 
            }

            auto now = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()).time_since_epoch();
            
            double scale = graphics.calculateBounceScale(state, audioData.isBeat, audioData.bpm, std::chrono::microseconds(now));

            cv::Mat outputFrame = graphics.composeFrame(state, assetManager, scale);
            cv::Mat previewFrame;
            
            if (state.hasActiveCue()) {
                previewFrame = graphics.composeCueFrame(state, assetManager, scale);
            } else {
                previewFrame = outputFrame; 
            }

            bool isSpacePressed = ImGui::IsKeyDown(ImGuiKey_Space);
            outputFrame = graphics.applyStrobeEffect(outputFrame, state, audioData.bpm, isSpacePressed);

            graphics.endDashboard(outputFrame, previewFrame, state, audioData.bpm, audioAnalyzer, pred);            
            graphics.showLive(outputFrame);
        }

    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}