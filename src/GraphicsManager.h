// GraphicsManager.h
#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <chrono>
#include <map>

#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_GLCOREARB

#include <GLFW/glfw3.h>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "ConfigManager.h"
#include "PlatformSpecificCode.h"
#include "AssetManager.h"
#include "RuntimeState.h"
#include "AudioAnalyzer.h"
#include "KNNClassifier.h" // Per la struct KNNPrediction

class GraphicsManager {
private:
    std::string windowName;
    int displayWidth;
    int displayHeight;
    int strobeDurationMs;

    GLFWwindow* uiWindow;
    GLFWwindow* projectorWindow; 
    
    GLuint liveTextureID = 0;
    GLuint cueTextureID = 0;
    GLuint projectorTexID = 0;   

    std::map<std::string, GLuint> thumbnailCache;
    GLuint getOrCreateThumbnail(const std::string& key, const cv::Mat& firstFrame, bool isGrayscale = false);

    GLuint quadVAO, quadVBO, shaderProgram;
    void setupProjectorQuad();

    cv::Mat scaleToFit(const cv::Mat& src, int targetWidth, int targetHeight, const cv::Scalar& bgColor = cv::Scalar(0, 0, 0));
    void UpdateGLTexture(const cv::Mat& mat, GLuint& textureID);

public:
    GraphicsManager(const AppConfig& config, const DisplayInfo& display);
    ~GraphicsManager();

    void createProjectorWindow(int monitorIndex);

    double calculateBounceScale(RuntimeState& state, bool isBeat, double tempo, std::chrono::microseconds now);

    cv::Mat composeFrame(RuntimeState& state, AssetManager& assetMgr, double scale);
    cv::Mat composeCueFrame(RuntimeState& state, AssetManager& assetMgr, double scale);
    
    cv::Mat applyStrobeEffect(cv::Mat composedFrame, RuntimeState& state, double bpm, bool isSpacePressed);
    
    void beginDashboard();
    
    void endDashboard(const cv::Mat& liveFrame, const cv::Mat& cueFrame, RuntimeState& state, double bpm, AudioAnalyzer& audioAnalyzer, KNNPrediction pred, AssetManager& assetMgr);    
    
    void showLive(const cv::Mat& frame, bool isBlackout);
    
    bool shouldCloseUI() const;
};