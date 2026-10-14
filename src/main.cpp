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
#include "KNNClassifier.h" // Assicurati di avere il file KNNClassifier.h creato in precedenza

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

// Definisco l'istanza globale (o referenziabile esternamente) per ImGui in GraphicsManager
KNNClassifier knnClassifier(21); 

int main() {
    // 1. Initialization
    ConfigManager configManager("config/config.json");
    const AppConfig& config = configManager.getConfig();
    DisplayInfo targetDisplay = selectTargetDisplay();
    
    // 2. Core Subsystems
    AssetManager assetManager(config);
    assetManager.initializeAssets();

    // Inizializza Dataset KNN (se esiste)
    knnClassifier.loadDataset("knn_dataset.csv");

    // Inizializza e avvia il thread DSP Audio
    AudioAnalyzer audioAnalyzer;
    audioAnalyzer.start();

    std::cout << "[INFO] Waiting for audio warmup (2 seconds)...\n";
    // Attesa per il warmup dell'audio
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Variabili per la logica di Phrasing Musicale
    // Variabili per la logica di Phrasing Musicale
    int beatsSinceLastChange = 0;
    
    std::deque<EnergyLevel> labelHistory;
    const size_t HISTORY_SIZE = 30; // Finestra temporale (circa 0.5 sec) per lo smoothing

    // Variabili per il sistema di debouncing e stabilizzazione delle predizioni KNN
    EnergyLevel stablePrediction = MID; 
    EnergyLevel candidatePrediction = MID;
    int consecutiveFrames = 0;
    int stableFramesCount = 0; // Accumulatore di "Fiducia"
    
    // Quanti frame servono per confermare una transizione verso una specifica label?
    std::map<EnergyLevel, int> baseDebounce = {
        {LOW, 25},  // LOW richiede conferme lunghe (~800ms) per evitare falsi positivi nei break vocali
        {MID, 15},  // MID intermedio (~500ms)
        {HIGH, 6}   // HIGH reattivo (~200ms) per beccare i drop in tempo reale
    };

    const int REQUIRED_CONSECUTIVE_FRAMES = 5; // ~330ms a 30fps: latenza bassa, ma zero glitch

    try {
        RuntimeState state(assetManager);
        GraphicsManager graphics(config, targetDisplay);

        std::cout << "[INFO] System initialized. Starting loop.\n";

        while (!graphics.shouldCloseUI()) {
            
            // --- 1. START FRAME ---
            graphics.beginDashboard();

            // --- 2. LEGGI AUDIO DSP E AGGIORNA PHRASING ---
            AudioMetrics audioData = audioAnalyzer.getMetrics();
            
            if (audioData.isBeat) {
                beatsSinceLastChange++;
            }

            // --- 3. INFERENZA KNN E SISTEMA DI FIDUCIA ---
            KNNPrediction pred = { MANUAL, 0, 0, 0 };
            if (state.isAutoMode) {
                pred = knnClassifier.predict(audioData);
                
                std::optional<EnergyLevel> highConfLabel = std::nullopt;
                if (pred.confLow >= 0.90f) highConfLabel = LOW;
                else if (pred.confMid >= 0.90f) highConfLabel = MID;
                else if (pred.confHigh >= 0.90f) highConfLabel = HIGH;

                if (highConfLabel.has_value()) {
                    if (highConfLabel.value() == candidatePrediction) {
                        consecutiveFrames++;
                    } else {
                        candidatePrediction = highConfLabel.value();
                        consecutiveFrames = 1;
                    }

                    // Calcolo della tolleranza dinamica basata sulla "Fiducia"
                    int requiredFrames = baseDebounce[candidatePrediction];
                    
                    // Se siamo in uno stato stabile da più di 3 secondi (es. 90 frame),
                    // il sistema si fida molto del prossimo segnale forte e dimezza il tempo di reazione
                    if (stableFramesCount > 90) {
                        requiredFrames = std::max(3, requiredFrames / 2); 
                    }

                    if (consecutiveFrames >= requiredFrames) {
                        stablePrediction = candidatePrediction;
                        consecutiveFrames = 0; // Ha confermato, azzera il debouncer
                    }
                } else {
                    // Sfarfallio di confidenza: si azzera la candidatura
                    consecutiveFrames = 0;
                }

                // Aggiorna l'accumulatore di fiducia
                if (stablePrediction == state.currentEnergyTarget) {
                    stableFramesCount++;
                } else {
                    stableFramesCount = 0; // Il target è appena cambiato o sta per cambiare
                }

                if (beatsSinceLastChange >= 32) {
                    // Tempo Massimo superato (32 beat): forza il cambio mantenendo la stessa label
                    state.triggerAutoSwitch(assetManager.getAssets()); 
                    beatsSinceLastChange = 0;
                } 
                else if (beatsSinceLastChange >= 4) {
                    // Tempo Minimo superato (8 beat): cambia se l'intensità stabilizzata è cambiata
                    if (stablePrediction != state.currentEnergyTarget) {
                        state.currentEnergyTarget = stablePrediction;
                        state.triggerAutoSwitch(assetManager.getAssets());
                        beatsSinceLastChange = 0;
                        
                        // Per evitare di ricaricare grafiche a ripetizione sui border-line,
                        // resettiamo il counter del debouncer dopo un cambio riuscito
                        consecutiveFrames = 0; 
                    }
                }
            }

            // --- 5. INPUT HANDLING IMGUI ---
            ImGuiIO& io = ImGui::GetIO();
            
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) break;
            if (ImGui::IsKeyPressed(ImGuiKey_Enter)) state.transitionCueToLive();

            if (ImGui::IsKeyPressed(ImGuiKey_M)) {
                state.isAutoMode = !state.isAutoMode;
                if (state.isAutoMode) beatsSinceLastChange = 0; // Reset contatore all'attivazione
            }
            
            if (ImGui::IsKeyPressed(ImGuiKey_J)) state.currentEnergyTarget = LOW;
            if (ImGui::IsKeyPressed(ImGuiKey_K)) state.currentEnergyTarget = MID;
            if (ImGui::IsKeyPressed(ImGuiKey_L)) state.currentEnergyTarget = HIGH;
            
            if (ImGui::IsKeyPressed(ImGuiKey_B)) { 
                state.isBounceActive = !state.isBounceActive;
            }

            // Selezione Asset manuale
            auto handleAssetMapping = [&](char keyChar) {
                bool isShiftPressed = io.KeyShift;
                auto bg = assetManager.getBackroundByPressedKey(keyChar);
                auto fg = assetManager.getForegroundByPressedKey(keyChar);

                if (bg.has_value()) {
                    if (isShiftPressed) state.switchCueBackground(bg.value());
                    else {
                        state.switchBackground(bg.value());
                        beatsSinceLastChange = 0; // Reset phraser manuale
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

            // --- 6. GRAPHICS PIPELINE ---
            auto now = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()).time_since_epoch();
            
            // Bounce Animazione
            double scale = graphics.calculateBounceScale(state, audioData.isBeat, audioData.bpm, std::chrono::microseconds(now));

            // Composizione
            cv::Mat outputFrame = graphics.composeFrame(state, assetManager, scale);
            cv::Mat previewFrame;
            
            if (state.hasActiveCue()) {
                previewFrame = graphics.composeCueFrame(state, assetManager, scale);
            } else {
                previewFrame = outputFrame; 
            }

            // Strobo
            bool isSpacePressed = ImGui::IsKeyDown(ImGuiKey_Space);
            outputFrame = graphics.applyStrobeEffect(outputFrame, state, audioData.bpm, isSpacePressed);

            // --- 7. RENDER DASHBOARD E PROIETTORE ---
            graphics.endDashboard(outputFrame, previewFrame, state, audioData.bpm, audioAnalyzer, pred);            
            graphics.showLive(outputFrame);
        }

    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}