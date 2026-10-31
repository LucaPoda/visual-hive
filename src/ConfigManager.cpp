// ConfigManager.cpp
#include "ConfigManager.h"
#include <iostream>
#include <filesystem>

using json = nlohmann::json;

ConfigManager::ConfigManager(const std::string& configFilePath) : configFilePath(configFilePath) {
    if (!std::filesystem::exists(this->configFilePath) && std::filesystem::exists("build/" + configFilePath)) {
        this->configFilePath = "build/" + configFilePath;
    }

    std::ifstream configFile(this->configFilePath);
    if (!configFile.is_open()) {
        std::cerr << "Error: Could not open config file at " << this->configFilePath << std::endl;
        config.assetsDir = "assets";
        config.keyMappingFile = "config/key_mapping.csv";
        config.assetsConfigFile = "config/assets_config.json";
        config.windowName = "visual-hive Output";
        config.phraseLength = 4;
        config.default_bpm = 125.0;
        
        config.audioSampleRate = 44100;
        config.audioBufferFrames = 512;
        config.debounceVerse = 4; // Tuning ottimizzato e reattivo
        config.debounceBuild = 3;
        config.debounceChorus = 2;
        config.knnHistorySize = 30;
        config.strobeDurationMs = 50;
        config.knnKNeighbors = 21;
        config.uiWindowWidth = 1920;
        config.uiWindowHeight = 1080;
        
        config.bpmCalibration.slope = 1.0f;
        config.bpmCalibration.intercept = 0.0f;
        return;
    }

    json data;
    configFile >> data;
    configFile.close();

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

    if (data.count("advanced")) {
        config.audioSampleRate = data["advanced"].value("audioSampleRate", 44100);
        config.audioBufferFrames = data["advanced"].value("audioBufferFrames", 512);
        config.debounceVerse = data["advanced"].value("debounceVerse", 4);
        config.debounceBuild = data["advanced"].value("debounceBuild", 3);
        config.debounceChorus = data["advanced"].value("debounceChorus", 2);
        config.knnHistorySize = data["advanced"].value("knnHistorySize", 30);
        config.strobeDurationMs = data["advanced"].value("strobeDurationMs", 50);
        config.knnKNeighbors = data["advanced"].value("knnKNeighbors", 21);
    } else {
        config.audioSampleRate = data.value("audioSampleRate", 44100);
        config.audioBufferFrames = data.value("audioBufferFrames", 512);
        config.debounceVerse = data.value("debounceVerse", 4);
        config.debounceBuild = data.value("debounceBuild", 3);
        config.debounceChorus = data.value("debounceChorus", 2);
        config.knnHistorySize = data.value("knnHistorySize", 30);
        config.strobeDurationMs = data.value("strobeDurationMs", 50);
        config.knnKNeighbors = data.value("knnKNeighbors", 21);
    }

    if (data.count("bpm_calibration")) {
        config.bpmCalibration.slope = data["bpm_calibration"].value("slope", 1.0f);
        config.bpmCalibration.intercept = data["bpm_calibration"].value("intercept", 0.0f);
        if (data["bpm_calibration"].count("points") && data["bpm_calibration"]["points"].is_array()) {
            config.bpmCalibration.points.clear();
            for (const auto& p : data["bpm_calibration"]["points"]) {
                AppConfig::BpmCalibrationPoint pt;
                pt.raw = p.value("raw", 0.0f);
                pt.actual = p.value("actual", 0.0f);
                if (pt.raw > 0.0f && pt.actual > 0.0f) {
                    config.bpmCalibration.points.push_back(pt);
                }
            }
        }
    }

    if (data.count("osc")) {
        config.osc.enabled = data["osc"].value("enabled", true);
        config.osc.ip = data["osc"].value("ip", "127.0.0.1");
        config.osc.port = data["osc"].value("port", 7700);
        config.osc.bpmAddress = data["osc"].value("bpm_address", "/bpm");
        config.osc.tapAddress = data["osc"].value("tap_address", "/tap");
        config.osc.sendTapOnBeat = data["osc"].value("send_tap_on_beat", true);
        config.osc.pulseDurationMs = data["osc"].value("pulse_duration_ms", 35);
    }

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
                int r = value[0].get<int>();
                int g = value[1].get<int>();
                int b = value[2].get<int>();
                config.colorMappings[key] = cv::Scalar(b, g, r);
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

    data["bpm_calibration"]["slope"] = config.bpmCalibration.slope;
    data["bpm_calibration"]["intercept"] = config.bpmCalibration.intercept;
    data["bpm_calibration"]["points"] = json::array();
    for (const auto& pt : config.bpmCalibration.points) {
        data["bpm_calibration"]["points"].push_back({
            {"raw", pt.raw},
            {"actual", pt.actual}
        });
    }

    data["osc"]["enabled"] = config.osc.enabled;
    data["osc"]["ip"] = config.osc.ip;
    data["osc"]["port"] = config.osc.port;
    data["osc"]["bpm_address"] = config.osc.bpmAddress;
    data["osc"]["tap_address"] = config.osc.tapAddress;
    data["osc"]["send_tap_on_beat"] = config.osc.sendTapOnBeat;
    data["osc"]["pulse_duration_ms"] = config.osc.pulseDurationMs;

    std::ofstream configFileOut(configFilePath);
    if (!configFileOut.is_open()) return false;
    
    configFileOut << data.dump(4);
    return true;
}

bool ConfigManager::saveBpmCalibration(float slope, float intercept, const std::vector<AppConfig::BpmCalibrationPoint>& points) {
    config.bpmCalibration.slope = slope;
    config.bpmCalibration.intercept = intercept;
    config.bpmCalibration.points = points;
    return saveConfig();
}

bool ConfigManager::saveOscConfig(bool enabled, const std::string& ip, int port,
                                   const std::string& bpmAddress, const std::string& tapAddress,
                                   bool sendTapOnBeat, int pulseDurationMs) {
    config.osc.enabled = enabled;
    config.osc.ip = ip;
    config.osc.port = port;
    config.osc.bpmAddress = bpmAddress;
    config.osc.tapAddress = tapAddress;
    config.osc.sendTapOnBeat = sendTapOnBeat;
    config.osc.pulseDurationMs = pulseDurationMs;
    return saveConfig();
}