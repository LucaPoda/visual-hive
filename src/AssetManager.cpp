#include "AssetManager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

namespace fs = std::filesystem;

// Constructor now takes the AppConfig object
AssetManager::AssetManager(const AppConfig& config)
    : appConfig(config) {
    // Default color, in case no mapping is found
    activeForegroundColor = cv::Scalar(255, 255, 255); // White
}

// Main initialization function
void AssetManager::initializeAssets() {
    // Step 1: Scan the assets directory to find all visuals
    scanAssets();

    // Step 2: Try to load the key mapping from a file
    if (!loadKeyMapping()) {
        std::cout << "Key mapping file not found or invalid. Starting interactive assignment.\n";
        // Step 3: If loading fails, prompt the user for interactive assignment
        interactiveKeyAssignment();
        // Step 4: Save the new mapping for future use
        saveKeyMapping(appConfig.keyMappingFile);
    }
}

// Return the vector of loaded assets
const std::vector<VisualAsset>& AssetManager::getAssets() const {
    return assets;
}

// Save the key mapping to disk
void AssetManager::saveKeyMapping() const {
    saveKeyMapping(appConfig.keyMappingFile);
}

// Set the active foreground color
void AssetManager::setActiveForegroundColor(const cv::Scalar& color) {
    activeForegroundColor = color;
}

// Helper to scan directories for visual assets
void AssetManager::scanAssets() {
    assets.clear();
    fs::path backgroundsPath = fs::path(appConfig.assetsDir) / "backgrounds";
    fs::path foregroundsPath = fs::path(appConfig.assetsDir) / "foregrounds";

    // Scan backgrounds (videos)
    if (fs::exists(backgroundsPath) && fs::is_directory(backgroundsPath)) {
        for (const auto& entry : fs::directory_iterator(backgroundsPath)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                std::string extension = entry.path().extension().string();
                if (extension == ".mp4" || extension == ".mov") {
                    assets.push_back({entry.path().string(), BACKGROUND, 0, 100.0});
                }
            }
        }
    }

    // Scan foregrounds (images)
    if (fs::exists(foregroundsPath) && fs::is_directory(foregroundsPath)) {
        for (const auto& entry : fs::directory_iterator(foregroundsPath)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                std::string extension = entry.path().extension().string();
                if (extension == ".png" || extension == ".jpg") {
                    double scale = 100.0;
                    if (appConfig.foregroundScales.count(filename)) {
                        scale = appConfig.foregroundScales.at(filename);
                    }
                    assets.push_back({entry.path().string(), FOREGROUND, 0, scale});
                }
            }
        }
    }
}

// Helper to load key mapping from a CSV file
bool AssetManager::loadKeyMapping() {
    std::ifstream file(appConfig.keyMappingFile);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    std::map<std::string, char> path_to_key;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string key_str, type_str, path;
        std::getline(ss, key_str, ',');
        std::getline(ss, type_str, ',');
        std::getline(ss, path);
        if (key_str.length() == 1) {
            path_to_key[path] = key_str[0];
        }
    }
    file.close();

    // Assign keys to the loaded assets
    bool allAssigned = true;
    for (auto& asset : assets) {
        if (path_to_key.count(asset.path)) {
            asset.key = path_to_key[asset.path];
        } else {
            asset.key = 0; // Not found in mapping file
            allAssigned = false;
        }
    }

    return allAssigned;
}

// Helper for interactive key assignment
void AssetManager::interactiveKeyAssignment() {
    cv::namedWindow("Key Assignment", cv::WINDOW_NORMAL);
    
    // Keep track of used keys to prevent duplicates
    std::map<char, bool> usedKeys;

    for (auto& asset : assets) {
        std::cout << "Displaying '" << fs::path(asset.path).filename().string() << "'. "
                  << "Press a key to assign it: ";

        // Display the asset and get a key press
        char key = displayAndGetKey("Key Assignment", asset);

        if (key == -1) { // Window closed
            std::cerr << "Window closed during key assignment. Exiting.\n";
            exit(1);
        }

        // Check for duplicate keys
        if (usedKeys.count(key)) {
            std::cout << "Key '" << key << "' is already assigned. Please try again.\n";
            asset.key = displayAndGetKey("Key Assignment", asset); // Re-prompt
            usedKeys[key] = true;
        } else {
            asset.key = key;
            usedKeys[key] = true;
        }

        std::cout << "Assigned key '" << asset.key << "'.\n";
    }

    cv::destroyWindow("Key Assignment");
}

// Helper to save key mapping to a CSV file
void AssetManager::saveKeyMapping(const std::string& mappingFilePath) const {
    std::ofstream file(mappingFilePath);
    if (!file.is_open()) {
        std::cerr << "Error: Could not save key mapping file.\n";
        return;
    }

    for (const auto& asset : assets) {
        std::string typeStr = (asset.type == BACKGROUND) ? "BACKGROUND" : "FOREGROUND";
        file << asset.key << "," << typeStr << "," << asset.path << "\n";
    }

    file.close();
    std::cout << "Key mapping saved to " << mappingFilePath << "\n";
}

// Public method to blend foreground with alpha channel onto a background
cv::Mat AssetManager::blend(const cv::Mat& background, const cv::Mat& foreground, int screenWidth, int screenHeight, double foregroundScalePercent) {
    if (background.empty() || foreground.empty()) {
        return background;
    }

    cv::Mat blended = background.clone();
    cv::Mat resizedForeground;
    
    // Calculate the target width based on the screen width and scale percentage
    int targetWidth = static_cast<int>(screenWidth * (foregroundScalePercent / 100.0));
    
    // Calculate new height to maintain aspect ratio
    double aspectRatio = static_cast<double>(foreground.rows) / foreground.cols;
    int newHeight = static_cast<int>(targetWidth * aspectRatio);

    // Check if the calculated dimensions are too big for the screen
    bool scaleAdjusted = false;
    if (targetWidth > screenWidth || newHeight > screenHeight) {
        std::cout << "Warning: Foreground scale is too large. Adjusting to fit screen." << std::endl;
        
        // Recalculate dimensions to fit the screen while maintaining aspect ratio
        if (targetWidth > screenWidth) {
            targetWidth = screenWidth;
            newHeight = static_cast<int>(targetWidth * aspectRatio);
        }
        if (newHeight > screenHeight) {
            newHeight = screenHeight;
            targetWidth = static_cast<int>(newHeight / aspectRatio);
        }
        
        // Calculate the maximum possible scale
        double maxScaleWidth = (static_cast<double>(screenWidth) / foreground.cols) * 100.0;
        double maxScaleHeight = (static_cast<double>(screenHeight) / foreground.rows) * 100.0;
        double maxScale = std::min(maxScaleWidth, maxScaleHeight);
        
        std::cout << "Suggestion: To prevent this, set foreground_scale_percent to a value less than or equal to " << maxScale << "%." << std::endl;
        scaleAdjusted = true;
    }

    // Resize the foreground image
    cv::resize(foreground, resizedForeground, cv::Size(targetWidth, newHeight));

    // Calculate position to center the foreground at the bottom of the screen
    int xOffset = (blended.cols - targetWidth) / 2;
    int yOffset = (blended.rows - newHeight) / 2;
    
    // Create the ROI on the background image where the foreground will be placed
    cv::Rect roi(xOffset, yOffset, targetWidth, newHeight);
    
    // Ensure the ROI is completely within the background image bounds
    cv::Rect safeRoi = roi & cv::Rect(0, 0, blended.cols, blended.rows);
    
    if (safeRoi.empty()) {
        // The foreground image is completely outside the background, do nothing.
        return blended;
    }

    // Adjust the resizedForeground to match the clamped ROI dimensions if needed
    cv::Mat adjustedForeground = resizedForeground(cv::Rect(
        safeRoi.x - roi.x,
        safeRoi.y - roi.y,
        safeRoi.width,
        safeRoi.height
    ));

    if (adjustedForeground.empty()) {
        return blended;
    }

    if (adjustedForeground.channels() == 4) {
        // Split the foreground into BGR and Alpha channels
        std::vector<cv::Mat> channels;
        cv::split(adjustedForeground, channels);
        cv::Mat bgr = adjustedForeground.clone();
        cv::Mat alpha = channels[3];

        // Create a new foreground image with a single, custom color
        cv::Mat solidColorForeground(adjustedForeground.size(), CV_8UC3, activeForegroundColor);

        // Copy the solid color to the ROI using the alpha channel as a mask
        solidColorForeground.copyTo(blended(safeRoi), alpha);
    } else {
        // For images without an alpha channel, just copy them directly
        adjustedForeground.copyTo(blended(safeRoi));
    }

    return blended;
}


// Helper to display a visual and get a key press
char AssetManager::displayAndGetKey(const std::string& windowName, const VisualAsset& asset) {
    if (asset.type == BACKGROUND) {
        cv::VideoCapture cap(asset.path);
        if (!cap.isOpened()) {
            std::cerr << "Error: Could not open video file " << asset.path << "\n";
            return -1;
        }
        cv::Mat frame;
        cap >> frame;
        if (!frame.empty()) {
            cv::imshow(windowName, frame);
        }
        cap.release();
    } else { // FOREGROUND
        cv::Mat image = cv::imread(asset.path, cv::IMREAD_UNCHANGED);
        if (!image.empty()) {
            cv::imshow(windowName, image);
        }
    }
    return cv::waitKey(0);
}
