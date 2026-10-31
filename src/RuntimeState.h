#pragma once

#include <chrono>
#include <optional>
#include <iostream>
#include <fstream>
#include <random>
#include <vector>
#include <string>
#include <array>
#include <deque> // Aggiunto per Z-Score Normalizer
#include "AssetManager.h"
#include "KNNClassifier.h" 

struct RuntimeState {
    bool isBounceActive;
    bool isAnimating;
    bool strobeFrameToggle;

    bool isAutoMode = false;         
    SongDynamic currentSongDynamicTarget = BUILD; 

    bool isBlackout = false;
    EventPhase currentEventPhase = AMBIENT;
    bool useAnyEventPhase = false; 
    int colorMode = 0; 
    std::vector<std::string> activePalettes;

    std::chrono::microseconds animationStartTime;
    long long lastFrameTime;
    
    std::optional<std::chrono::steady_clock::time_point> nextStrobeTime;

    Background activeBackground;
    Foreground activeForeground;
    std::optional<Foreground> queuedForeground;

    int captureFailCount = 0;

    std::optional<Background> cueBackground;
    std::optional<Foreground> cueForeground;

    // --- Z-SCORE NORMALIZER (STANDARD SCALER) ---
    std::deque<std::array<float, 14>> featureHistoryWindow;
    std::array<double, 14> featureSums = {0};
    std::array<double, 14> featureSqSums = {0};
    
    // A 30 FPS, 5 minuti = 9000 frames. Sblocchiamo a 30 secondi (900 frames).
    const size_t MAX_HISTORY_FRAMES = 9000; 
    const size_t MIN_HISTORY_FRAMES = 900;  
    bool isBufferReady = false;
    float bufferFillPercentage = 0.0f;

    // --- CAMPI PER HIERARCHICAL TEMPORAL CLASSIFICATION ---
    std::array<float, 14> emaFeatures = {0};     // 14 feature totali: RMS, ZCR, Crest, Flux, Centroid, Spread, Rolloff + 7 Bande
    std::array<float, 9> rawProbabilities = {0}; // 9 label totali: EMPTY_PAD, LIGHT_RHYTHM, VOCAL_AMBIENT, BUILD_SNARE, BUILD_ACID, BUILD_MELODIC, DROP_RAW, DROP_MELODIC, BRIDGE
    
    // Accumulatore Asimmetrico
    std::array<float, 3> targetProbabilities = {1.0f, 0.0f, 0.0f}; // LOW, MID, HIGH (Grezze)
    std::array<float, 3> smoothedScores = {1.0f, 0.0f, 0.0f};      // LOW, MID, HIGH (Filtrate)

    std::vector<std::array<float, 9>> historyBuffer;
    bool showReasoningWindow = false;

    // --- NUOVE VARIABILI PER IL LOGGING ---
    bool isLoggingEnabled = false;
    SongDynamic expectedSongDynamicTarget = BUILD; 
    std::ofstream aiLogFile;
    float lastRmsUsed = 0.0f;
    float bridgeToHighFactor = 0.0f;
    float currentConfidence = 1.0f; 

    // --- SOGLIA DINAMICA E METRONOMO ---
    float fractionalBeats = 0.0f; 
    int beatsSinceLastChange = 0; 
    float currentDynamicThreshold = 0.95f; 
    long long lastTimeTicks = 0;  
    
    // Griglia ritmica assoluta sincronizzata sui drop
    float masterPhraseBeats = 0.0f;

    // ---> NUOVO: DEBOUNCING INCREMENTALE E ATTESA <---
    int penaltyStage = 0;                  // Indice di progressione della penalità (0..5)
    int currentPenaltyBeats = 0;           // Quanti beat dobbiamo aspettare per il prossimo cambio
    float fractionalPenaltyCounter = 0.0f; // Accumulatore per il raffreddamento
    SongDynamic lastWinner = BUILD;        // Tiene traccia del leader in gara per il tempo di attesa
    float fractionalWaitCounter = 0.0f;    // Tempo trascorso in attesa del cambio

    RuntimeState(AssetManager& assetManager);
    ~RuntimeState(); 
    
    void startAiLogging(const std::string& filename);
    void stopAiLogging();
    void logAiStep(); 

    void switchBackground(const Background& newBg);
    void switchForeground(const Foreground& newFg);
    
    void switchCueBackground(const Background& newBg);
    void switchCueForeground(const Foreground& newFg);
    
    void triggerAutoSwitch(const AssetsConfig& assets);
    void transitionCueToLive();

    bool hasActiveCue() const;
    
    // Esegue il filtro asimmetrico e setta il target
    AudioMetrics normalizeAndGetMetrics(const AudioMetrics& raw);
    void updateAiStateMachine(const AudioMetrics& incomingMetrics, const KNNPrediction& pred);
};