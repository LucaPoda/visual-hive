#include "ConfigManager.h"
#include <iostream>

using json = nlohmann::json;

ConfigManager::ConfigManager(const std::string& configFilePath) {
    std::ifstream configFile(configFilePath);
    if (!configFile.is_open()) {
        std::cerr << "Error: Could not open config file at " << configFilePath << std::endl;
        // Set default values if config file is not found
        config.assetsDir = "assets";
        config.keyMappingFile = "config/key_mapping.csv";
        config.windowName = "visual-hive Output";
        config.foregroundWidth = 200;
        return;
    }

    json data;
    configFile >> data;
    configFile.close();

    // Parse values from the JSON object
    if (data.count("paths")) {
        config.assetsDir = data["paths"].value("assets_directory", "assets");
        config.keyMappingFile = data["paths"].value("key_mapping_file", "config/key_mapping.csv");
    }

    if (data.count("display")) {
        config.windowName = data["display"].value("window_name", "visual-hive Output");
        config.foregroundWidth = data["display"].value("foreground_width", 200);
    }
}

const AppConfig& ConfigManager::getConfig() const {
    return config;
}
