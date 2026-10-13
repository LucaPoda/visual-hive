#pragma once

#include <RtAudio.h>
#include <mutex>
#include <vector>
#include <complex>
#include <string>
#include <deque>
#include <memory>
#include "BTrack.h" // Inclusione della libreria BTrack

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
    bool isBeat = false;         // Flag istantaneo che vale true solo nel frame del battito
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

    // --- BTrack ---
    std::unique_ptr<BTrack> btrack;
    std::vector<double> btrackBuffer; // BTrack richiede array di double

    // DSP State
    unsigned int sampleRate = 44100;
    std::vector<float> previousMagnitudes;
    
    unsigned int currentDeviceId = 0;
    
    // Stabilizzazione e Normalizzazione
    float smoothedDelta = 0.0f;
    float maxBandEnergies[6] = {0.001f, 0.001f, 0.001f, 0.001f, 0.001f, 0.001f}; 
    float maxFlux = 0.001f;

    struct InstantBands {
        float subBass, bass, lowMid, mid, highMid, high;
        float rms;
        float flux;
    };
    
    std::deque<InstantBands> bandHistory;
};