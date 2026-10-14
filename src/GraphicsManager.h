#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <chrono>

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

    GLFWwindow* uiWindow;
    GLFWwindow* projectorWindow; 
    
    GLuint liveTextureID = 0;
    GLuint cueTextureID = 0;
    GLuint projectorTexID = 0;   

    GLuint quadVAO, quadVBO, shaderProgram;
    void setupProjectorQuad();

    cv::Mat scaleToFit(const cv::Mat& src, int targetWidth, int targetHeight, const cv::Scalar& bgColor = cv::Scalar(0, 0, 0));
    void UpdateGLTexture(const cv::Mat& mat, GLuint& textureID);

public:
    GraphicsManager(const AppConfig& config, const DisplayInfo& display);
    ~GraphicsManager();

    double calculateBounceScale(RuntimeState& state, bool isBeat, double tempo, std::chrono::microseconds now);

    cv::Mat composeFrame(RuntimeState& state, AssetManager& assetMgr, double scale);
    cv::Mat composeCueFrame(RuntimeState& state, AssetManager& assetMgr, double scale);
    
    cv::Mat applyStrobeEffect(cv::Mat composedFrame, RuntimeState& state, double bpm, bool isSpacePressed);
    
    void beginDashboard();
    
    // Firma aggiornata per ricevere le predizioni KNN
    void endDashboard(const cv::Mat& liveFrame, const cv::Mat& cueFrame, RuntimeState& state, double bpm, AudioAnalyzer& audioAnalyzer, KNNPrediction pred);    
    
    void showLive(const cv::Mat& frame);
    
    bool shouldCloseUI() const;
};