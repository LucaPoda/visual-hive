// ConfigManager.cpp
#include "ConfigManager.h"
#include <iostream>

using json = nlohmann::json;

ConfigManager::ConfigManager(const std::string& configFilePath) : configFilePath(configFilePath) {
    std::ifstream configFile(configFilePath);
    if (!configFile.is_open()) {
        std::cerr << "Error: Could not open config file at " << configFilePath << std::endl;
        // Set default values if config file is not found
        config.assetsDir = "assets";
        config.keyMappingFile = "config/key_mapping.csv";
        config.assetsConfigFile = "config/assets_config.json";
        config.windowName = "visual-hive Output";
        config.phraseLength = 4;
        config.default_bpm = 125.0;
        
        // Fallback per i nuovi parametri
        config.audioSampleRate = 44100;
        config.audioBufferFrames = 512;
        config.debounceVerse = 25;
        config.debounceBuild = 15;
        config.debounceChorus = 6;
        config.knnHistorySize = 30;
        config.strobeDurationMs = 50;
        config.knnKNeighbors = 21;
        config.uiWindowWidth = 1920;
        config.uiWindowHeight = 1080;
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
        config.uiWindowWidth = data["display"].value("uiWindowWidth", 1920);
        config.uiWindowHeight = data["display"].value("uiWindowHeight", 1080);
    } else {
        config.uiWindowWidth = 1920;
        config.uiWindowHeight = 1080;
    }
    
    if (data.count("ableton_link")) {
        config.phraseLength = data["ableton_link"].value("phrase_length", 4);
        config.default_bpm = data["ableton_link"].value("default_bpm", 125.0);
    }

    // Lettura dei nuovi parametri (con fallback)
    if (data.count("advanced")) {
        config.audioSampleRate = data["advanced"].value("audioSampleRate", 44100);
        config.audioBufferFrames = data["advanced"].value("audioBufferFrames", 512);
        config.debounceVerse = data["advanced"].value("debounceVerse", 25);
        config.debounceBuild = data["advanced"].value("debounceBuild", 15);
        config.debounceChorus = data["advanced"].value("debounceChorus", 6);
        config.knnHistorySize = data["advanced"].value("knnHistorySize", 30);
        config.strobeDurationMs = data["advanced"].value("strobeDurationMs", 50);
        config.knnKNeighbors = data["advanced"].value("knnKNeighbors", 21);
    } else {
        config.audioSampleRate = data.value("audioSampleRate", 44100);
        config.audioBufferFrames = data.value("audioBufferFrames", 512);
        config.debounceVerse = data.value("debounceVerse", 25);
        config.debounceBuild = data.value("debounceBuild", 15);
        config.debounceChorus = data.value("debounceChorus", 6);
        config.knnHistorySize = data.value("knnHistorySize", 30);
        config.strobeDurationMs = data.value("strobeDurationMs", 50);
        config.knnKNeighbors = data.value("knnKNeighbors", 21);
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

bool ConfigManager::saveConfig() const {
    std::ifstream configFileIn(configFilePath);
    json data;
    if (configFileIn.is_open()) {
        configFileIn >> data;
        configFileIn.close();
    }

    // Aggiornamento struttura JSON
    data["paths"]["assets_directory"] = config.assetsDir;
    data["paths"]["key_mapping_file"] = config.keyMappingFile;
    data["paths"]["assets_config_file"] = config.assetsConfigFile;

    data["display"]["window_name"] = config.windowName;
    data["display"]["uiWindowWidth"] = config.uiWindowWidth;
    data["display"]["uiWindowHeight"] = config.uiWindowHeight;

    data["ableton_link"]["phrase_length"] = config.phraseLength;
    data["ableton_link"]["default_bpm"] = config.default_bpm;

    data["advanced"]["audioSampleRate"] = config.audioSampleRate;
    data["advanced"]["audioBufferFrames"] = config.audioBufferFrames;
    data["advanced"]["debounceVerse"] = config.debounceVerse;
    data["advanced"]["debounceBuild"] = config.debounceBuild;
    data["advanced"]["debounceChorus"] = config.debounceChorus;
    data["advanced"]["knnHistorySize"] = config.knnHistorySize;
    data["advanced"]["strobeDurationMs"] = config.strobeDurationMs;
    data["advanced"]["knnKNeighbors"] = config.knnKNeighbors;

    std::ofstream configFileOut(configFilePath);
    if (!configFileOut.is_open()) return false;
    
    configFileOut << data.dump(4);
    return true;
}