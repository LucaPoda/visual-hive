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
    std::string assetsDir = "assets";
    std::string keyMappingFile = "config/key_mapping.csv";
    std::string assetsConfigFile = "config/assets_config.json";
    std::string recordingsDir = "recordings";
    std::string knnDatasetFile = "datasets/new_dataset_14_feature.csv";
    
    std::string windowName = "Visual Hive Live";
    double previewFps = 30.0;
    bool autoModeStart = false;
    bool blackoutOnStart = false;

    std::map<std::string, cv::Scalar> colorMappings;
    std::map<std::string, double> foregroundScales;
    int phraseLength = 4;
    double default_bpm = 120.0;
    
    // Parametri estratti
    unsigned int audioSampleRate = 44100;
    unsigned int audioBufferFrames = 512;
    int debounceVerse = 4;
    int debounceBuild = 3;
    int debounceChorus = 2;
    size_t knnHistorySize = 30;
    int strobeDurationMs = 50;
    int knnKNeighbors = 21;
    int uiWindowWidth = 1920;
    int uiWindowHeight = 1080;

    // Calibrazione BPM (Retta di regressione e punti)
    struct BpmCalibrationPoint {
        float raw = 0.0f;
        float actual = 0.0f;
    };

    struct BpmCalibrationConfig {
        float slope = 1.0f;
        float intercept = 0.0f;
        float minBpmThreshold = 110.0f;
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
    void setConfig(const AppConfig& newConfig) { config = newConfig; }
    const std::string& getConfigFilePath() const { return configFilePath; }
    
    // Metodo per salvare la configurazione
    bool saveConfig() const;
    bool saveFullConfig(const AppConfig& newConfig) { config = newConfig; return saveConfig(); }
    bool saveBpmCalibration(float slope, float intercept, const std::vector<AppConfig::BpmCalibrationPoint>& points);
    bool saveOscConfig(bool enabled, const std::string& ip, int port,
                       const std::string& bpmAddress, const std::string& tapAddress,
                       bool sendTapOnBeat, int pulseDurationMs);
    
private:
    void loadAssetsConfig(const std::string& assetsConfigFilePath);
};