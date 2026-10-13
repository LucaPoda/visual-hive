#pragma once

#include <RtAudio.h>
#include <mutex>
#include <vector>
#include <complex>
#include <string>
#include <deque>

struct AudioMetrics {
    float rms = 0.0f;
    float peak = 0.0f;
    float spectralFlux = 0.0f;
    float bpm = 0.0f;
    
    // Feature KNN - 6 Bande di equalizzazione (Normalizzate 0.0 - 1.0)
    float bandSubBass = 0.0f;  // 20-60 Hz
    float bandBass = 0.0f;     // 60-250 Hz
    float bandLowMid = 0.0f;   // 250-500 Hz
    float bandMid = 0.0f;      // 500-2000 Hz
    float bandHighMid = 0.0f;  // 2000-4000 Hz
    float bandHigh = 0.0f;     // 4000+ Hz
    
    float deltaIntensity = 0.0f; // Progression coefficient
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

    // DSP State
    unsigned int sampleRate = 44100;
    std::vector<float> previousMagnitudes;
    std::vector<double> peakTimes;
    double lastOnsetEnergy = 0.0;
    
    unsigned int currentDeviceId = 0;
    
    // Stabilizzazione e Normalizzazione
    float smoothedDelta = 0.0f;
    float maxBandEnergies[6] = {0.001f, 0.001f, 0.001f, 0.001f, 0.001f, 0.001f}; 
    float maxFlux = 0.001f; // Per normalizzare lo Spectral Flux

    // Struttura per lo storico istantaneo
    struct InstantBands {
        float subBass, bass, lowMid, mid, highMid, high;
        float rms;
        float flux;
    };
    
    // Buffer circolare per mantenere gli ultimi ~10 secondi di audio
    std::deque<InstantBands> bandHistory;
};