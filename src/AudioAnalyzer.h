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
    // Metriche operative di sistema
    float peakL = 0.0f;
    float peakR = 0.0f;
    float bpm = 0.0f;
    float rawBpm = 0.0f;
    bool isBeat = false;

    // Feature Temporali (Volume e Impulsività)
    float rms = 0.0f;
    float zcr = 0.0f;              
    float crestFactor = 0.0f;      

    // Feature Spettrali (Timbro e Forma d'onda)
    float spectralFlux = 0.0f;
    float spectralCentroid = 0.0f; 
    float spectralSpread = 0.0f;   
    float spectralRolloff = 0.0f;  

    // Bande di Frequenza (Densità di energia)
    float bandSubBass = 0.0f;      
    float bandBass = 0.0f;         
    float bandLowMid = 0.0f;       
    float bandMid = 0.0f;          
    float bandHighMid = 0.0f;      
    float bandPresence = 0.0f;     
    float bandBrilliance = 0.0f;   
};

struct AudioDeviceInfo {
    unsigned int id;
    std::string name;
};

class OscManager;

class AudioAnalyzer {
public:
    AudioAnalyzer();
    ~AudioAnalyzer();

    void setOscManager(OscManager* osc) { oscManager = osc; }

    bool start(unsigned int deviceId = 0, unsigned int configSampleRate = 44100, unsigned int configBufferFrames = 512);
    void stop();
    AudioMetrics getMetrics();
    AudioMetrics getLatestMetrics() const;

    std::vector<AudioDeviceInfo> getInputDevices();
    unsigned int getCurrentDeviceId() const { return currentDeviceId; }

    void startRecording(const std::string& baseFilename);
    void stopRecording();
    bool isRecordingActive() const { return isRecording; }
    std::string getCurrentRecordFilename() const { return currentRecordFile; }
    std::string getRecordingTimeFormatted() const;

    struct BpmPoint {
        float raw = 0.0f;
        float actual = 0.0f;
    };

    // Gestione e Calibrazione BPM con Regressione Lineare (m * raw + q)
    float getRawBpm() const { return lastRawBpm.load(); }
    float getBpmSlope() const { return bpmSlope.load(); }
    float getBpmIntercept() const { return bpmIntercept.load(); }
    void setCalibrationLine(float slope, float intercept) { bpmSlope.store(slope); bpmIntercept.store(intercept); }
    
    std::vector<BpmPoint> getCalibrationPoints();
    void setCalibrationPoints(const std::vector<BpmPoint>& pts);
    void addCalibrationPoint(float raw, float actual);
    void removeCalibrationPoint(size_t index);
    void clearCalibrationPoints();
    void computeRegression();

private:
    static int audioCallback(void* outputBuffer, void* inputBuffer, unsigned int nBufferFrames,
                             double streamTime, RtAudioStreamStatus status, void* userData);
    
    void processBuffer(const float* buffer, unsigned int numFrames, double streamTime, float peakL, float peakR);

    RtAudio adc;
    std::mutex metricsMutex;
    AudioMetrics currentMetrics;

    std::unique_ptr<BTrack> btrack;
    std::vector<double> btrackBuffer;

    // Membri Calibrazione BPM Lineare
    std::atomic<float> bpmSlope{1.0f};
    std::atomic<float> bpmIntercept{0.0f};
    std::atomic<float> lastRawBpm{0.0f};
    std::mutex pointsMutex;
    std::vector<BpmPoint> calibrationPoints;

    OscManager* oscManager = nullptr;

    unsigned int sampleRate = 44100;
    std::vector<float> previousMagnitudes;
    unsigned int currentDeviceId = 0;
    
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