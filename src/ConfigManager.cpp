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
        config.assetsConfigFile = "config/assets_config.json";
        config.windowName = "visual-hive Output";
        return;
    }

    json data;
    configFile >> data;
    configFile.close();

    // Parse values from the JSON object
    if (data.count("paths")) {
        config.assetsDir = data["paths"].value("assets_directory", "assets");
        config.keyMappingFile = data["paths"].value("key_mapping_file", "config/key_mapping.csv");
        config.assetsConfigFile = data["paths"].value("assets_config_file", "config/assets_config.json");
    }

    if (data.count("display")) {
        config.windowName = data["display"].value("window_name", "visual-hive Output");
    }
    
    // Load the assets configuration
    loadAssetsConfig(config.assetsConfigFile);
}

void ConfigManager::loadAssetsConfig(const std::string& assetsConfigFilePath) {
    std::ifstream assetsFile(assetsConfigFilePath);
    if (!assetsFile.is_open()) {
        std::cerr << "Error: Could not open assets config file at " << assetsConfigFilePath << std::endl;
        return;
    }
    
    json data;
    assetsFile >> data;
    assetsFile.close();
    
    if (data.count("color_mappings")) {
        for (auto& [key, value] : data["color_mappings"].items()) {
            if (value.is_array() && value.size() == 3) {
                // Read colors as RGB from JSON and convert to BGR for OpenCV
                int r = value[0].get<int>();
                int g = value[1].get<int>();
                int b = value[2].get<int>();
                config.colorMappings[key] = cv::Scalar(b, g, r); // Store as BGR
            }
        }
    }
    
    if (data.count("foreground_scales")) {
        for (auto& [key, value] : data["foreground_scales"].items()) {
            if (value.is_number()) {
                config.foregroundScales[key] = value.get<double>();
            }
        }
    }
}

const AppConfig& ConfigManager::getConfig() const {
    return config;
}
