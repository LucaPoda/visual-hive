#pragma once

#include <RtAudio.h>
#include <mutex>
#include <vector>
#include <complex>
#include <string>
#include <deque>
#include <memory>
#include "BTrack.h" 

struct AudioMetrics {
    float bpm = 0.0f;
    bool isBeat = false;
    float peak = 0.0f;
    
    // Le 5 Feature "Dream Team" (Tutte Auto-Scaled 0.0 - 1.0)
    float rms = 0.0f;          // Energia Totale
    float bandSubBass = 0.0f;  // Presenza di Cassa/Sub (HIGH)
    float bandBass = 0.0f;     // Corpo del brano
    float bandHighMid = 0.0f;  // Rullate e Riser (MID)
    float spectralFlux = 0.0f; // Impatto/Transienti
    
    // Manteniamo le altre bande solo per la UI opzionale, ma non per il KNN
    float bandLowMid = 0.0f;   
    float bandMid = 0.0f;      
    float bandHigh = 0.0f;     
};

struct AudioDeviceInfo {
    unsigned int id;
    std::string name;
};

class AudioAnalyzer {
public:
    AudioAnalyzer();
    ~AudioAnalyzer();

    bool start(unsigned int deviceId = 0);
    void stop();
    AudioMetrics getMetrics();

    std::vector<AudioDeviceInfo> getInputDevices();
    unsigned int getCurrentDeviceId() const { return currentDeviceId; }

private:
    static int audioCallback(void* outputBuffer, void* inputBuffer, unsigned int nBufferFrames,
                             double streamTime, RtAudioStreamStatus status, void* userData);
    void processBuffer(const float* buffer, unsigned int numFrames, double streamTime);

    RtAudio adc;
    std::mutex metricsMutex;
    AudioMetrics currentMetrics;

    std::unique_ptr<BTrack> btrack;
    std::vector<double> btrackBuffer;

    unsigned int sampleRate = 44100;
    std::vector<float> previousMagnitudes;
    unsigned int currentDeviceId = 0;
    
    // Trackers Locali (per estrazione)
    float maxBandEnergies[6] = {0.001f, 0.001f, 0.001f, 0.001f, 0.001f, 0.001f}; 
    float localMaxFlux = 0.001f;

    // --- AUTO-SCALER GLOBALI (Volume Invariance) ---
    float globalMaxRms = 0.001f;
    float globalMaxSubBass = 0.001f;
    float globalMaxBass = 0.001f;
    float globalMaxHighMid = 0.001f;
    float globalMaxFlux = 0.001f;

    struct InstantBands {
        float subBass, bass, lowMid, mid, highMid, high;
        float rms;
        float rawFlux;     
    };
    
    std::deque<InstantBands> bandHistory;
};