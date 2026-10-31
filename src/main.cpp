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
#include <array>

#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_GLCOREARB
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

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
    if (!fs::exists("datasets")) {
        fs::create_directory("datasets");
    }

    ConfigManager configManager("config/config.json");
    const AppConfig& config = configManager.getConfig();
    
    knnClassifier.setK(config.knnKNeighbors);
    
    DisplayInfo targetDisplay = selectTargetDisplay();
    
    GraphicsManager graphics(config, targetDisplay);

    graphics.beginDashboard();
    int display_w, display_h;
    glfwGetFramebufferSize(graphics.getUIWindow(), &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui::SetNextWindowPos(ImVec2(display_w * 0.5f, display_h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Loading", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "Inizializzazione Sistema...");
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(graphics.getUIWindow());
    glfwPollEvents(); 

    AssetManager assetManager(config);
    assetManager.initializeAssets();

    if (fs::exists("datasets/knn_dataset.csv")) {
        knnClassifier.loadDataset("datasets/knn_dataset.csv");
    }

    AudioAnalyzer audioAnalyzer;
    audioAnalyzer.start(0, config.audioSampleRate, config.audioBufferFrames);

    std::cout << "[INFO] Waiting for audio warmup (2 seconds)...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    int beatsSinceLastChange = 0;

    try {
        RuntimeState state(assetManager);
        std::cout << "[INFO] System initialized. Starting loop.\n";

        // --- TIMERS E ACCUMULATORI FUORI DAL CICLO WHILE ---
        auto lastAudioTime = std::chrono::steady_clock::now();
        double lastAiUpdateTime = glfwGetTime(); 
        
        // Accumulatore dei picchi per il frame corrente (Max Pooling)
        AudioMetrics peakAccumulator;
        auto resetAccumulator = [&peakAccumulator]() { peakAccumulator = AudioMetrics(); };
        
        // Buffer per le ultime 8 finestre da 1/8 di nota (1 battuta intera)
        std::deque<AudioMetrics> beatHistoryBuffer;

        while (!graphics.shouldCloseUI()) {
            
            graphics.beginDashboard();

            AudioMetrics audioData = audioAnalyzer.getMetrics();
            
            if (audioData.isBeat) {
                beatsSinceLastChange++;
            }

            // 1. MAX POOLING: Tratteniamo i picchi massimi estratti a 86 Hz
            peakAccumulator.rms = std::max(peakAccumulator.rms, audioData.rms);
            peakAccumulator.zcr = std::max(peakAccumulator.zcr, audioData.zcr);
            peakAccumulator.crestFactor = std::max(peakAccumulator.crestFactor, audioData.crestFactor);
            peakAccumulator.spectralFlux = std::max(peakAccumulator.spectralFlux, audioData.spectralFlux);
            peakAccumulator.spectralCentroid = std::max(peakAccumulator.spectralCentroid, audioData.spectralCentroid);
            peakAccumulator.spectralSpread = std::max(peakAccumulator.spectralSpread, audioData.spectralSpread);
            peakAccumulator.spectralRolloff = std::max(peakAccumulator.spectralRolloff, audioData.spectralRolloff);
            peakAccumulator.bandSubBass = std::max(peakAccumulator.bandSubBass, audioData.bandSubBass);
            peakAccumulator.bandBass = std::max(peakAccumulator.bandBass, audioData.bandBass);
            peakAccumulator.bandLowMid = std::max(peakAccumulator.bandLowMid, audioData.bandLowMid);
            peakAccumulator.bandMid = std::max(peakAccumulator.bandMid, audioData.bandMid);
            peakAccumulator.bandHighMid = std::max(peakAccumulator.bandHighMid, audioData.bandHighMid);
            peakAccumulator.bandPresence = std::max(peakAccumulator.bandPresence, audioData.bandPresence);
            peakAccumulator.bandBrilliance = std::max(peakAccumulator.bandBrilliance, audioData.bandBrilliance);

            KNNPrediction pred;
            pred.probabilities.fill(0.0f);

            double currentAppTime = glfwGetTime();

            // 2. TIMING DINAMICO BPM-LOCKED (2x BPM = 1/8 di nota)
            float currentBpm = (audioData.bpm > 30.0f) ? audioData.bpm : 128.0f;
            double aiUpdateInterval = 60.0 / (currentBpm * 2.0); 

            if (currentAppTime - lastAiUpdateTime >= aiUpdateInterval) {
                lastAiUpdateTime = currentAppTime;

                peakAccumulator.bpm = currentBpm;
                peakAccumulator.peakL = audioData.peakL;
                peakAccumulator.peakR = audioData.peakR;

                // Aggiungiamo i picchi di questo 1/8 di nota al buffer (Max 8 elementi)
                beatHistoryBuffer.push_front(peakAccumulator);
                if (beatHistoryBuffer.size() > 8) beatHistoryBuffer.pop_back();

                // 3. MEDIA ESPONENZIALE SULLE 8 FINESTRE (Decadimento)
                AudioMetrics smoothedMetrics = peakAccumulator; // Copia per bpm e picchi assoluti
                smoothedMetrics.rms = 0; smoothedMetrics.zcr = 0; smoothedMetrics.crestFactor = 0;
                smoothedMetrics.spectralFlux = 0; smoothedMetrics.spectralCentroid = 0;
                smoothedMetrics.spectralSpread = 0; smoothedMetrics.spectralRolloff = 0;
                smoothedMetrics.bandSubBass = 0; smoothedMetrics.bandBass = 0; smoothedMetrics.bandLowMid = 0;
                smoothedMetrics.bandMid = 0; smoothedMetrics.bandHighMid = 0; smoothedMetrics.bandPresence = 0;
                smoothedMetrics.bandBrilliance = 0;

                float weightSum = 0.0f;
                float currentWeight = 1.0f;
                const float decayFactor = 0.65f; // Il campione precedente vale il 65% di quello attuale

                for (const auto& sample : beatHistoryBuffer) {
                    smoothedMetrics.rms += sample.rms * currentWeight;
                    smoothedMetrics.zcr += sample.zcr * currentWeight;
                    smoothedMetrics.crestFactor += sample.crestFactor * currentWeight;
                    smoothedMetrics.spectralFlux += sample.spectralFlux * currentWeight;
                    smoothedMetrics.spectralCentroid += sample.spectralCentroid * currentWeight;
                    smoothedMetrics.spectralSpread += sample.spectralSpread * currentWeight;
                    smoothedMetrics.spectralRolloff += sample.spectralRolloff * currentWeight;
                    smoothedMetrics.bandSubBass += sample.bandSubBass * currentWeight;
                    smoothedMetrics.bandBass += sample.bandBass * currentWeight;
                    smoothedMetrics.bandLowMid += sample.bandLowMid * currentWeight;
                    smoothedMetrics.bandMid += sample.bandMid * currentWeight;
                    smoothedMetrics.bandHighMid += sample.bandHighMid * currentWeight;
                    smoothedMetrics.bandPresence += sample.bandPresence * currentWeight;
                    smoothedMetrics.bandBrilliance += sample.bandBrilliance * currentWeight;
                    
                    weightSum += currentWeight;
                    currentWeight *= decayFactor;
                }

                // Normalizzazione per la somma dei pesi
                smoothedMetrics.rms /= weightSum;
                smoothedMetrics.zcr /= weightSum;
                smoothedMetrics.crestFactor /= weightSum;
                smoothedMetrics.spectralFlux /= weightSum;
                smoothedMetrics.spectralCentroid /= weightSum;
                smoothedMetrics.spectralSpread /= weightSum;
                smoothedMetrics.spectralRolloff /= weightSum;
                smoothedMetrics.bandSubBass /= weightSum;
                smoothedMetrics.bandBass /= weightSum;
                smoothedMetrics.bandLowMid /= weightSum;
                smoothedMetrics.bandMid /= weightSum;
                smoothedMetrics.bandHighMid /= weightSum;
                smoothedMetrics.bandPresence /= weightSum;
                smoothedMetrics.bandBrilliance /= weightSum;

                // L'AI estrae le metriche normalizzate (e alimenta il Debugger istantaneamente)
                AudioMetrics normalizedMetrics = state.normalizeAndGetMetrics(smoothedMetrics);

                // La predizione ora avviene sempre ed usa "normalizedMetrics"
                pred = knnClassifier.predict(normalizedMetrics);
                state.rawProbabilities = pred.probabilities;
                
                state.historyBuffer.push_back(state.rawProbabilities);
                if (state.historyBuffer.size() > 32) state.historyBuffer.erase(state.historyBuffer.begin());

                SongDynamic prevTarget = state.currentSongDynamicTarget;
                
                state.updateAiStateMachine(smoothedMetrics, pred);

                // L'esecuzione (cambio visual) avviene SOLO in Auto Mode
                if (state.isAutoMode) {
                    if (prevTarget != state.currentSongDynamicTarget) {
                        state.triggerAutoSwitch(assetManager.getAssets());
                    } 
                    else if (state.masterPhraseBeats >= 32.0f) {
                        if (state.beatsSinceLastChange >= 24) state.triggerAutoSwitch(assetManager.getAssets()); 
                        state.masterPhraseBeats -= 32.0f; 
                    }
                }

                // Reset per la prossima finestra di 1/8 di nota
                resetAccumulator();
            }
            
            ImGuiIO& io = ImGui::GetIO();
            
            // --- GESTIONE SILENZIO E AUTO-MODE ---
            bool audioActive = (audioData.peakL > 0.001f || audioData.peakR > 0.001f);
            auto nowTime = std::chrono::steady_clock::now();

            if (audioActive) {
                lastAudioTime = nowTime; 
            }

            if (state.isAutoMode) {
                if (std::chrono::duration_cast<std::chrono::seconds>(nowTime - lastAudioTime).count() >= 60) {
                    state.isAutoMode = false;
                    std::cout << "[WARNING] Auto Mode disattivato (Assenza di segnale audio per 60 secondi).\n";
                }
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) break;
            if (ImGui::IsKeyPressed(ImGuiKey_Enter)) state.transitionCueToLive();

            if (ImGui::IsKeyPressed(ImGuiKey_M)) {
                if (state.isAutoMode) {
                    state.isAutoMode = false;
                } else {
                    if (audioActive) {
                        state.isAutoMode = true;
                        beatsSinceLastChange = 0;
                        state.triggerAutoSwitch(assetManager.getAssets()); // <- CORREZIONE: Cambio immediato
                    } else {
                        std::cout << "[WARNING] Impossibile attivare Auto Mode: Nessun audio rilevato.\n";
                    }
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

                // <- CORREZIONE: Cue sui background disabilitato in Auto Mode
                if (bg.has_value() && !state.isAutoMode) {
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

            graphics.endDashboard(outputFrame, previewFrame, state, audioData.bpm, audioAnalyzer, pred, assetManager);            
            graphics.showLive(outputFrame, state.isBlackout);
        }

    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}