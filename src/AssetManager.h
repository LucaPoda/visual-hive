#pragma once

#include <string>
#include <vector>
#include <map>
#include <opencv2/opencv.hpp>

// Enum to differentiate between visual types
enum VisualType {
    BACKGROUND,
    FOREGROUND
};

// A struct to hold information about each visual asset
struct VisualAsset {
    std::string path;
    VisualType type;
    char key;
};

// The AssetManager class to handle all visual asset logic
class AssetManager {
private:
    std::string assetsDir;
    std::string mappingFile;
    std::vector<VisualAsset> assets;

public:
    AssetManager(const std::string& assetsDir, const std::string& mappingFile);

    // Public API for the main application
    void initializeAssets();
    const std::vector<VisualAsset>& getAssets() const;
    void saveKeyMapping() const;
    
    // Public method to blend a foreground onto a background with new sizing logic
    cv::Mat blend(const cv::Mat& background, const cv::Mat& foreground);

private:
    // Internal helper functions
    void scanAssets();
    bool loadKeyMapping();
    void interactiveKeyAssignment();
    void saveKeyMapping(const std::string& mappingFilePath) const;
    
    // Helper to display a visual and get a key press
    char displayAndGetKey(const std::string& windowName, const VisualAsset& asset);
};
