// ConfigManager.h
#pragma once

#include <string>
#include <fstream>
#include <map>
#include <vector>
#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp> // nlohmann/json library

// Struct to hold all the application's configuration parameters
struct AppConfig {
    std::string assetsDir;
    std::string keyMappingFile;
    std::string assetsConfigFile;
    std::string windowName;
    std::map<std::string, cv::Scalar> colorMappings;
    std::map<std::string, double> foregroundScales;
    int phraseLength;
    double default_bpm;
    
    // Parametri estratti
    unsigned int audioSampleRate;
    unsigned int audioBufferFrames;
    int debounceVerse;
    int debounceBuild;
    int debounceChorus;
    size_t knnHistorySize;
    int strobeDurationMs;
    int knnKNeighbors;
    int uiWindowWidth;
    int uiWindowHeight;

    // Calibrazione BPM (Retta di regressione e punti)
    struct BpmCalibrationPoint {
        float raw = 0.0f;
        float actual = 0.0f;
    };

    struct BpmCalibrationConfig {
        float slope = 1.0f;
        float intercept = 0.0f;
        std::vector<BpmCalibrationPoint> points;
    };

    BpmCalibrationConfig bpmCalibration;

    // Configurazione Trasmissione OSC (QLC+ Lights Sync)
    struct OscConfig {
        bool enabled = true;
        std::string ip = "127.0.0.1";
        int port = 7700;
        std::string bpmAddress = "/bpm";
        std::string tapAddress = "/tap";
        bool sendTapOnBeat = true;
        int pulseDurationMs = 35;
    };

    OscConfig osc;
};

class ConfigManager {
private:
    AppConfig config;
    std::string configFilePath;

public:
    ConfigManager(const std::string& configFilePath);
    
    // Getter for the loaded configuration
    const AppConfig& getConfig() const;
    AppConfig& getMutableConfig() { return config; }
    const std::string& getConfigFilePath() const { return configFilePath; }
    
    // Metodo per salvare la configurazione
    bool saveConfig() const;
    bool saveBpmCalibration(float slope, float intercept, const std::vector<AppConfig::BpmCalibrationPoint>& points);
    bool saveOscConfig(bool enabled, const std::string& ip, int port,
                       const std::string& bpmAddress, const std::string& tapAddress,
                       bool sendTapOnBeat, int pulseDurationMs);
    
private:
    void loadAssetsConfig(const std::string& assetsConfigFilePath);
};