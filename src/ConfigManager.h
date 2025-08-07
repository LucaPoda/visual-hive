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
};

class ConfigManager {
private:
    AppConfig config;

public:
    ConfigManager(const std::string& configFilePath);
    
    // Getter for the loaded configuration
    const AppConfig& getConfig() const;
    
private:
    void loadAssetsConfig(const std::string& assetsConfigFilePath);
};
