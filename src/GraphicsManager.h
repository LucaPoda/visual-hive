#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <chrono>
#include "ConfigManager.h"
#include "PlatformSpecificCode.h" // For DisplayInfo
#include "AssetManager.h"
#include "RuntimeState.h"

class GraphicsManager {
private:
    std::string windowName;
    int displayWidth;
    int displayHeight;

    // Helper: Resize frame to fit maintaining aspect ratio
    cv::Mat scaleToFit(const cv::Mat& src, int targetWidth, int targetHeight, const cv::Scalar& bgColor = cv::Scalar(0, 0, 0));

public:
    GraphicsManager(const AppConfig& config, const DisplayInfo& display);
    ~GraphicsManager();

    double calculateBounceScale(RuntimeState& state, double currentBeat, double tempo, std::chrono::microseconds now);
    
    cv::Mat composeFrame(RuntimeState& state, AssetManager& assetMgr, double scale);
    
    cv::Mat applyStrobeEffect(cv::Mat composedFrame, RuntimeState& state, double bpm);
    
    void show(const cv::Mat& frame);
    
    int enforceFramePacing(RuntimeState& state);
};