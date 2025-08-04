#pragma once

#include <string>
#include <fstream>
#include <nlohmann/json.hpp> // nlohmann/json library

// Struct to hold all the application's configuration parameters
struct AppConfig {
    std::string assetsDir;
    std::string keyMappingFile;
    std::string windowName;
    int foregroundWidth;
};

class ConfigManager {
private:
    AppConfig config;

public:
    ConfigManager(const std::string& configFilePath);
    
    // Getter for the loaded configuration
    const AppConfig& getConfig() const;
};
