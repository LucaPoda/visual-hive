#include "RuntimeState.h"
#include "Logger.h"
#include <opencv2/core/utility.hpp> 
#include <stdexcept>
#include <chrono>
#include <algorithm>

RuntimeState::RuntimeState(AssetManager& assetManager, const AppConfig* config) 
    : isBounceActive(false),
      isAnimating(false),
      strobeFrameToggle(false),
      isAutoMode(false),           
      currentSongDynamicTarget(BUILD),
      isBlackout(false),
      currentEventPhase(AMBIENT),
      useAnyEventPhase(false),
      colorMode(0),
      animationStartTime(0),
      lastFrameTime(0),
      nextStrobeTime(std::nullopt),
      queuedForeground(std::nullopt),
      showReasoningWindow(false)
{
    if (config) {
        debounceVerse = config->debounceVerse > 0 ? config->debounceVerse : 16;
        debounceBuild = config->debounceBuild > 0 ? config->debounceBuild : 12;
        debounceChorus = config->debounceChorus > 0 ? config->debounceChorus : 8;
    }
    currentPenaltyBeats = debounceBuild;

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

    // --- ATTIVAZIONE DI TUTTE LE PALETTE DI DEFAULT ---
    
    // 1. Aggiungiamo tutti i colori base (Color Mode 0)
    const char* baseColors[] = {"White", "Red", "Green", "Blue", "Cyan", "Magenta", "Yellow"};
    for (int i = 0; i < 7; ++i) {
        activePalettes.push_back(baseColors[i]);
    }

    // 2. Aggiungiamo dinamicamente tutte le palette combinate lette dagli asset (Palette Mode 1)
    for (const auto& [key, bg] : assetManager.getAssets().get_backgrounds()) {
        if (!bg.get_primary_color().empty()) {
            std::string palKey = bg.get_primary_color() + (bg.get_secondary_color().empty() ? "" : "-" + bg.get_secondary_color());
            if (std::find(activePalettes.begin(), activePalettes.end(), palKey) == activePalettes.end()) {
                activePalettes.push_back(palKey);
            }
        }
    }

    rawProbabilities.fill(0.0f);
    emaFeatures.fill(0.0f);
}

RuntimeState::~RuntimeState() {
    stopAiLogging();
}

void RuntimeState::startAiLogging(const std::string& filename) {
    if (aiLogFile.is_open()) return;
    
    aiLogFile.open(filename, std::ios::out | std::ios::app);
    if (aiLogFile.is_open()) {
        aiLogFile << "Timestamp_ms,"
                  << "KNN_V_RAW,KNN_V_MEL,KNN_B_RAW,KNN_B_MEL,KNN_D_RAW,KNN_D_MEL,KNN_BREAK,KNN_BRDG,"
                  << "Raw_Low,Raw_Mid,Raw_High,"
                  << "Smooth_Low,Smooth_Mid,Smooth_High,"
                  << "RMS_Value,Bridge_to_LOW,Bridge_to_HIGH," // <-- NUOVE COLONNE
                  << "Label_Output_AI,Label_Expected_Manual\n";
        isLoggingEnabled = true;
    }
}
void RuntimeState::stopAiLogging() {
    if (aiLogFile.is_open()) {
        aiLogFile.close();
    }
    isLoggingEnabled = false;
}

void RuntimeState::logAiStep() {
    if (!isLoggingEnabled || !aiLogFile.is_open()) return;

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    aiLogFile << now << ",";
    
    for(int i = 0; i < 8; i++) aiLogFile << rawProbabilities[i] << ",";
    for(int i = 0; i < 3; i++) aiLogFile << targetProbabilities[i] << ",";
    for(int i = 0; i < 3; i++) aiLogFile << smoothedScores[i] << ",";
    
    // ---> NUOVI DATI DI LOG <---
    float bridgeLowPercent  = rawProbabilities[BRIDGE] * (1.0f - bridgeToHighFactor);
    float bridgeHighPercent = rawProbabilities[BRIDGE] * bridgeToHighFactor;
    
    aiLogFile << lastRmsUsed << "," 
              << bridgeLowPercent << "," 
              << bridgeHighPercent << ",";
    
    const char* finalLabel = (currentSongDynamicTarget == VERSE) ? "LOW" : (currentSongDynamicTarget == BUILD) ? "MID" : "HIGH";
    aiLogFile << finalLabel << ",";
    
    const char* expectedLabel = (expectedSongDynamicTarget == VERSE) ? "LOW" : (expectedSongDynamicTarget == BUILD) ? "MID" : "HIGH";
    aiLogFile << expectedLabel << "\n";
}

void RuntimeState::switchBackground(const Background& newBg) {
    if (activeBackground.get_source() == newBg.get_source()) return;
    
    std::cout << "Switch Background: " << newBg.get_source() << std::endl;

    activeBackground.close();
    activeBackground = newBg;

    if (activeBackground.open()) {
        if (queuedForeground.has_value()) {
            activeForeground = queuedForeground.value();
            queuedForeground = std::nullopt;
        }
    }
    fractionalBeats = 0.0f;
    beatsSinceLastChange = 0;
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
        if (bg.get_song_dynamic() == MANUAL) continue;
        
        if (!useAnyEventPhase && bg.get_event_phase() != currentEventPhase) continue;
        if (bg.get_song_dynamic() != currentSongDynamicTarget) continue;
        
        if (!activePalettes.empty()) {
            bool valid = false;
            if (colorMode == 0) { 
                bool primOk = bg.get_primary_color().empty() || std::find(activePalettes.begin(), activePalettes.end(), bg.get_primary_color()) != activePalettes.end();
                bool secOk = bg.get_secondary_color().empty() || std::find(activePalettes.begin(), activePalettes.end(), bg.get_secondary_color()) != activePalettes.end();
                
                if (bg.get_primary_color().empty() && bg.get_secondary_color().empty()) valid = false;
                else valid = (primOk && secOk);
            } else { 
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

    if (isAutoMode) {
        isAutoMode = false;
        LOG_INFO("RuntimeState", "Transizione CUE -> LIVE: inviati BG e FG in Live. Auto Mode DISATTIVATA.");
    } else {
        LOG_INFO("RuntimeState", "Transizione CUE -> LIVE: inviati BG e FG in Live.");
    }
}

void RuntimeState::applyCueForegroundToLive() {
    if (cueForeground.has_value()) {
        activeForeground = cueForeground.value();
        LOG_INFO("RuntimeState", "Applica Foreground: inviato solo FG in Live. Auto Mode rimane attiva.");
    }
}

void RuntimeState::activateAutoModeImmediate(const AssetsConfig& assets) {
    isAutoMode = true;
    
    // Determina immediatamente il livello corrente secondo il modello AI
    int bestIdx = 0;
    float maxScore = smoothedScores[0];
    if (smoothedScores[1] > maxScore) { bestIdx = 1; maxScore = smoothedScores[1]; }
    if (smoothedScores[2] > maxScore) { bestIdx = 2; maxScore = smoothedScores[2]; }
    
    currentSongDynamicTarget = (bestIdx == 0) ? VERSE : (bestIdx == 1) ? BUILD : CHORUS;
    previousDynamicTarget = currentSongDynamicTarget;
    beatsSinceTargetChange = 0.0f;
    fractionalBeats = 0.0f;
    beatsSinceLastChange = 0;
    fractionalWaitCounter = 0.0f;
    
    std::string levelName = (currentSongDynamicTarget == VERSE) ? "VERSE (LOW)" :
                            (currentSongDynamicTarget == BUILD) ? "BUILD (MID)" : "CHORUS (HIGH)";
    LOG_INFO("AutoMode", "Attivazione immediata Auto Mode su livello rilevato: " + levelName);
    
    triggerAutoSwitch(assets);
}

bool RuntimeState::hasActiveCue() const {
    return cueBackground.has_value() || cueForeground.has_value();
}

AudioMetrics RuntimeState::normalizeAndGetMetrics(const AudioMetrics& incomingMetrics) {
    // 1. LETTURA DATI
    std::array<float, 14> raw = {
        incomingMetrics.rms, incomingMetrics.zcr, incomingMetrics.crestFactor, incomingMetrics.spectralFlux,
        incomingMetrics.spectralCentroid, incomingMetrics.spectralSpread, incomingMetrics.spectralRolloff,
        incomingMetrics.bandSubBass, incomingMetrics.bandBass, incomingMetrics.bandLowMid, incomingMetrics.bandMid,
        incomingMetrics.bandHighMid, incomingMetrics.bandPresence, incomingMetrics.bandBrilliance
    };

    // 2. AGGIORNAMENTO ROLLING WINDOW (Z-Score)
    featureHistoryWindow.push_back(raw);
    for (int i = 0; i < 14; i++) {
        featureSums[i] += raw[i];
        featureSqSums[i] += (double)raw[i] * raw[i];
    }

    if (featureHistoryWindow.size() > MAX_HISTORY_FRAMES) {
        auto oldest = featureHistoryWindow.front();
        for (int i = 0; i < 14; i++) {
            featureSums[i] -= oldest[i];
            featureSqSums[i] -= (double)oldest[i] * oldest[i];
        }
        featureHistoryWindow.pop_front();
    }

    size_t count = featureHistoryWindow.size();
    isBufferReady = (count > 0); // <- CORREZIONE: Ora parte dal primo frame utile
    bufferFillPercentage = static_cast<float>(count) / MAX_HISTORY_FRAMES;

    // 3. CALCOLO Z-SCORE NORMALIZATION
    std::array<float, 14> zScoreFeatures = {0};
    if (isBufferReady) {
        for (int i = 0; i < 14; i++) {
            double mean = featureSums[i] / count;
            double variance = (featureSqSums[i] / count) - (mean * mean);
            if (variance < 1e-9) variance = 1e-9; 
            double stddev = std::sqrt(variance);
            
            float z = static_cast<float>((raw[i] - mean) / stddev);
            zScoreFeatures[i] = std::clamp((z + 3.0f) / 6.0f, 0.0f, 1.0f);
        }
    }

    // 4. ASSEGNAZIONE EMA (Per la visualizzazione UI)
    for (int i = 0; i < 14; i++) {
        emaFeatures[i] = zScoreFeatures[i]; 
    }

    // 5. RESTITUZIONE METRICHE NORMALIZZATE AL KNN
    AudioMetrics normMetrics = incomingMetrics;
    normMetrics.rms = emaFeatures[0];
    normMetrics.zcr = emaFeatures[1];
    normMetrics.crestFactor = emaFeatures[2];
    normMetrics.spectralFlux = emaFeatures[3];
    normMetrics.spectralCentroid = emaFeatures[4];
    normMetrics.spectralSpread = emaFeatures[5];
    normMetrics.spectralRolloff = emaFeatures[6];
    normMetrics.bandSubBass = emaFeatures[7];
    normMetrics.bandBass = emaFeatures[8];
    normMetrics.bandLowMid = emaFeatures[9];
    normMetrics.bandMid = emaFeatures[10];
    normMetrics.bandHighMid = emaFeatures[11];
    normMetrics.bandPresence = emaFeatures[12];
    normMetrics.bandBrilliance = emaFeatures[13];

    return normMetrics;
}

void RuntimeState::updateAiStateMachine(const AudioMetrics& incomingMetrics, const KNNPrediction& pred) {
    long long currentTicks = cv::getTickCount();
    double dt = (currentTicks - lastTimeTicks) / cv::getTickFrequency();
    lastTimeTicks = currentTicks;
    if (dt > 0.5) dt = 0.5;

    float bps = (incomingMetrics.bpm > 30.0f) ? (incomingMetrics.bpm / 60.0f) : (128.0f / 60.0f);
    float beatDelta = bps * dt;
    fractionalBeats += beatDelta;
    beatsSinceLastChange = static_cast<int>(fractionalBeats);
    masterPhraseBeats += beatDelta;

    for (int i = 0; i < 9; i++) {
        rawProbabilities[i] = pred.probabilities[i];
    }

    // 5. Calcolo CONFIDENZA
    float sumSq = 0.0f;
    for (int i = 0; i < 9; i++) sumSq += rawProbabilities[i] * rawProbabilities[i];
    currentConfidence = std::clamp((sumSq - (1.0f/9.0f)) / (1.0f - (1.0f/9.0f)), 0.0f, 1.0f);

    // 6. Dynamic Routing BRIDGE e Raggruppamento
    lastRmsUsed = emaFeatures[0]; 
    bridgeToHighFactor = std::clamp((lastRmsUsed - 0.40f) / 0.30f, 0.0f, 1.0f);
    
    targetProbabilities[0] = rawProbabilities[EMPTY_PAD] + rawProbabilities[LIGHT_RHYTHM] + rawProbabilities[VOCAL_AMBIENT] + (rawProbabilities[BRIDGE] * (1.0f - bridgeToHighFactor));
    targetProbabilities[1] = rawProbabilities[BUILD_SNARE] + rawProbabilities[BUILD_ACID] + rawProbabilities[BUILD_MELODIC];
    targetProbabilities[2] = rawProbabilities[DROP_RAW] + rawProbabilities[DROP_MELODIC] + (rawProbabilities[BRIDGE] * bridgeToHighFactor);

    // 7. Normalizzazione Probabilita' (senza cancellazione distruttiva)
    float targetSum = targetProbabilities[0] + targetProbabilities[1] + targetProbabilities[2];
    if (targetSum > 0.0001f) {
        targetProbabilities[0] /= targetSum;
        targetProbabilities[1] /= targetSum;
        targetProbabilities[2] /= targetSum;
    }

    beatsSinceTargetChange += beatDelta;

    // 8. Filtro Asimmetrico Modulato Reattivo
    // Attacco rapido e rilascio controllato per cogliere all'istante i cambi musicali (1-2 battute)
    // senza cadere in buchi momentanei o rimbalzi caotici
    float base_attack[3]  = {0.35f, 0.45f, 0.65f}; 
    float base_release[3] = {0.12f, 0.12f, 0.12f}; 
    for(int i = 0; i < 3; i++) {
        float effectiveConf = std::clamp(currentConfidence, 0.40f, 1.0f);
        float alpha = (targetProbabilities[i] > smoothedScores[i]) 
                      ? (base_attack[i] * effectiveConf) 
                      : (base_release[i] * effectiveConf);
        smoothedScores[i] += alpha * (targetProbabilities[i] - smoothedScores[i]);
    }

    // 9. Bypass Intelligente su Drop ad alta energia (da Build a Chorus)
    bool forceImmediateChange = false;
    if (currentSongDynamicTarget == BUILD && targetProbabilities[2] >= 0.70f && currentConfidence > 0.55f) {
        smoothedScores[2] = 0.95f; 
        forceImmediateChange = true;
    }

    // 10. OUTPUT CON DEBOUNCING E GUARDIA ANTI-OSCILLAZIONE (ANTI PING-PONG)
    float min_activation_threshold = 0.50f; 
    int bestIdx = 0;
    float maxScore = smoothedScores[0];
    if (smoothedScores[1] > maxScore) { bestIdx = 1; maxScore = smoothedScores[1]; }
    if (smoothedScores[2] > maxScore) { bestIdx = 2; maxScore = smoothedScores[2]; }

    SongDynamic candidate = currentSongDynamicTarget;
    SongDynamic newWinner = (bestIdx == 0) ? VERSE : (bestIdx == 1) ? BUILD : CHORUS;

    // Protezione anti-saltellamento: se il contendente coincide con il target abbandonato da meno di 8 beat,
    // richiediamo una soglia piu' alta per evitare rimbalzi
    bool isRecentBounce = (newWinner == previousDynamicTarget && beatsSinceTargetChange < 8.0f);
    float effectiveActivationThreshold = isRecentBounce ? 0.70f : min_activation_threshold;

    // Determina i beat di conferma richiesti per la transizione verso newWinner
    int requiredBeats = debounceBuild;
    if (newWinner == VERSE) {
        requiredBeats = debounceVerse;
    } else if (newWinner == BUILD) {
        requiredBeats = debounceBuild;
    } else if (newWinner == CHORUS) {
        // Se veniamo da un build e c'e' una vera esplosione energetica, switch rapido (2 beat)
        if (currentSongDynamicTarget == BUILD && (targetProbabilities[2] >= 0.70f || forceImmediateChange)) {
            requiredBeats = 2;
        } else {
            requiredBeats = debounceChorus;
        }
    }
    currentPenaltyBeats = requiredBeats;

    if (newWinner == currentSongDynamicTarget) {
        fractionalWaitCounter = std::max(0.0f, fractionalWaitCounter - beatDelta);
    } else {
        if (maxScore >= effectiveActivationThreshold) {
            if (newWinner != lastWinner) {
                fractionalWaitCounter = 0.0f; // Nuovo contendente: parte da zero
            }
            fractionalWaitCounter += beatDelta;

            if (fractionalWaitCounter >= currentPenaltyBeats || forceImmediateChange) {
                candidate = newWinner; 
                fractionalWaitCounter = 0.0f;
                if (candidate == CHORUS) {
                    masterPhraseBeats = 0.0f;
                }
            }
        } else {
            fractionalWaitCounter = std::max(0.0f, fractionalWaitCounter - beatDelta * 1.5f);
        }
    }
    
    lastWinner = newWinner; 
    if (candidate != currentSongDynamicTarget) {
        previousDynamicTarget = currentSongDynamicTarget;
        currentSongDynamicTarget = candidate;
        beatsSinceTargetChange = 0.0f;
        std::string dynStr = (currentSongDynamicTarget == VERSE) ? "VERSE" :
                             (currentSongDynamicTarget == BUILD) ? "BUILD" : "CHORUS";
        LOG_INFO("AutoMode", "AI Target cambiato in: " + dynStr);
    }
    
    logAiStep();
}