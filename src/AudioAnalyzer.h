// AudioAnalyzer.h
#pragma once

#include <RtAudio.h>
#include <mutex>
#include <vector>
#include <complex>
#include <string>
#include <deque>
#include <memory>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <fstream>
#include "BTrack.h" 

struct AudioMetrics {
    float bpm = 0.0f;
    bool isBeat = false;
    
    // Metering
    float peak = 0.0f;  // Mantenuto per compatibilità (max tra L e R)
    float peakL = 0.0f; // Canale Sinistro
    float peakR = 0.0f; // Canale Destro
    
    // Le 5 Feature "Dream Team"
    float rms = 0.0f;          
    float bandSubBass = 0.0f;  
    float bandBass = 0.0f;     
    float bandHighMid = 0.0f;  
    float spectralFlux = 0.0f;
    
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

    bool start(unsigned int deviceId = 0, unsigned int configSampleRate = 44100, unsigned int configBufferFrames = 512);
    void stop();
    AudioMetrics getMetrics();

    std::vector<AudioDeviceInfo> getInputDevices();
    unsigned int getCurrentDeviceId() const { return currentDeviceId; }

    // --- METODI RECORDING ---
    void startRecording(const std::string& baseFilename);
    void stopRecording();
    bool isRecordingActive() const { return isRecording; }
    std::string getCurrentRecordFilename() const { return currentRecordFile; }
    std::string getRecordingTimeFormatted() const;

private:
    static int audioCallback(void* outputBuffer, void* inputBuffer, unsigned int nBufferFrames,
                             double streamTime, RtAudioStreamStatus status, void* userData);
    
    // Nuova firma per accettare i picchi L e R
    void processBuffer(const float* buffer, unsigned int numFrames, double streamTime, float peakL, float peakR);

    RtAudio adc;
    std::mutex metricsMutex;
    AudioMetrics currentMetrics;

    std::unique_ptr<BTrack> btrack;
    std::vector<double> btrackBuffer;

    unsigned int sampleRate = 44100;
    std::vector<float> previousMagnitudes;
    unsigned int currentDeviceId = 0;
    
    // Tracker Locali per aggregazione
    float maxBandEnergies[6] = {0.001f, 0.001f, 0.001f, 0.001f, 0.001f, 0.001f}; 

    // --- AUTO-SCALER GLOBALI (Picchi Assoluti) ---
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

    // --- VARIABILI RECORDING ---
    std::atomic<bool> isRecording{false};
    std::atomic<bool> stopWriter{false};
    std::mutex recordMutex;
    std::condition_variable recordCV;
    std::queue<std::vector<float>> recordQueue;
    std::thread writerThread;
    
    std::string recordBasePath;
    std::string currentRecordFile;
    int recordPartIndex = 1;
    uint32_t currentFileSamples = 0;
    uint32_t totalRecordedSamples = 0;
    std::ofstream currentWavFile;
    
    void writerLoop();
    void openNextWavFile();
    void closeCurrentWavFile();
};