#pragma once

#include <vector>
#include <string>
#include <map>
#include <opencv2/opencv.hpp>
#include "ConfigManager.h" // Include AppConfig

// Enum to differentiate between background and foreground assets
enum AssetType {
    BACKGROUND,
    FOREGROUND
};

// Struct to represent a visual asset
struct VisualAsset {
    std::string path;
    AssetType type;
    char key;
    double scale;
};

class AssetManager {
private:
    const AppConfig& appConfig;
    std::vector<VisualAsset> assets;
    cv::Scalar activeForegroundColor;

public:
    AssetManager(const AppConfig& config);
    void initializeAssets();
    const std::vector<VisualAsset>& getAssets() const;
    void saveKeyMapping() const;
    void setActiveForegroundColor(const cv::Scalar& color);
    
    cv::Mat blend(const cv::Mat& background, const cv::Mat& foreground, int screenWidth, int screenHeight, double foregroundScalePercent);

private:
    void scanAssets();
    bool loadKeyMapping();
    void interactiveKeyAssignment();
    void saveKeyMapping(const std::string& mappingFilePath) const;
    char displayAndGetKey(const std::string& windowName, const VisualAsset& asset);
};
