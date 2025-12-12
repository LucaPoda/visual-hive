#include "GraphicsManager.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

GraphicsManager::GraphicsManager(const AppConfig& config, const DisplayInfo& display) 
    : windowName(config.windowName), displayWidth(display.width), displayHeight(display.height) 
{
    cv::namedWindow(windowName, cv::WINDOW_NORMAL);
    cv::setWindowProperty(windowName, cv::WND_PROP_FULLSCREEN, cv::WINDOW_FREERATIO);
    cv::moveWindow(windowName, display.x, display.y);
    cv::resizeWindow(windowName, displayWidth, displayHeight);
}

GraphicsManager::~GraphicsManager() {
    cv::destroyAllWindows();
}

cv::Mat GraphicsManager::scaleToFit(const cv::Mat& src, int targetWidth, int targetHeight, const cv::Scalar& bgColor) {
    if (src.empty()) return cv::Mat(targetHeight, targetWidth, CV_8UC3, bgColor);

    int srcWidth = src.cols;
    int srcHeight = src.rows;
    if (srcWidth == 0 || srcHeight == 0) return cv::Mat(targetHeight, targetWidth, src.type(), bgColor);

    double srcAspectRatio = static_cast<double>(srcWidth) / srcHeight;
    double targetAspectRatio = static_cast<double>(targetWidth) / targetHeight;

    int newWidth, newHeight;
    if (srcAspectRatio > targetAspectRatio) {
        newWidth = targetWidth;
        newHeight = static_cast<int>(newWidth / srcAspectRatio);
    } else {
        newHeight = targetHeight;
        newWidth = static_cast<int>(newHeight * srcAspectRatio);
    }

    cv::Mat resizedFrame;
    cv::resize(src, resizedFrame, cv::Size(newWidth, newHeight), 0, 0, cv::INTER_LINEAR);

    cv::Mat canvas(targetHeight, targetWidth, src.type(), bgColor);
    int xOffset = (targetWidth - newWidth) / 2;
    int yOffset = (targetHeight - newHeight) / 2;
    resizedFrame.copyTo(canvas(cv::Rect(xOffset, yOffset, newWidth, newHeight)));

    return canvas;
}

double GraphicsManager::calculateBounceScale(RuntimeState& state, double currentBeat, double tempo, std::chrono::microseconds now) {
    double scale = 1.0;

    if (state.isBounceActive || state.isAnimating) {
        // Check if we hit a new beat
        if (std::floor(currentBeat) > state.lastBeat) {
            state.lastBeat = std::floor(currentBeat);
            state.isAnimating = true;
            state.animationStartTime = now;
        }

        if (state.isAnimating) {
            auto elapsed = now - state.animationStartTime;
            double beatDuration = (60.0 / tempo) * 1000000.0; // microseconds
            double progress = static_cast<double>(elapsed.count()) / beatDuration;

            if (progress < 1.0) {
                // Sine wave bounce: 1.0 -> 1.2 -> 1.0
                scale = 1.0 + 0.2 * std::sin((progress + 0.5) * (M_PI));
            } else {
                state.isAnimating = false;
            }
        }
    }
    return scale;
}

cv::Mat GraphicsManager::composeFrame(RuntimeState& state, AssetManager& assetMgr, double scale) {
    cv::Mat frame = state.activeBackground.get_next_frame();
    cv::Mat outputFrame = scaleToFit(frame, displayWidth, displayHeight);

    // Blend foreground
    outputFrame = assetMgr.blend(
        outputFrame, 
        state.activeForeground.get_next_frame(), 
        displayWidth, 
        displayHeight, 
        state.activeForeground.get_scale() * scale, 
        state.activeBackground.get_foreground_color()
    );

    return outputFrame;
}

cv::Mat GraphicsManager::applyStrobeEffect(cv::Mat composedFrame, RuntimeState& state, double bpm) {
    if (!isSpaceDown()) {
        state.nextStrobeTime = std::nullopt; 
        return composedFrame;
    }

    auto now = std::chrono::steady_clock::now();

    if (!state.nextStrobeTime.has_value()) {
        state.nextStrobeTime = now;
    }

    if (now >= state.nextStrobeTime.value()) {
        state.strobeFrameToggle = !state.strobeFrameToggle;
        double duration = 2000.0 / bpm; 
        state.nextStrobeTime = now + std::chrono::milliseconds(static_cast<long long>(duration));
    }

    if (state.strobeFrameToggle) {
        return cv::Mat(displayHeight, displayWidth, CV_8UC3, cv::Scalar(255, 255, 255));
    }

    return composedFrame;
}

void GraphicsManager::show(const cv::Mat& frame) {
    cv::imshow(windowName, frame);
}

int GraphicsManager::enforceFramePacing(RuntimeState& state) {
    long long currentTick = cv::getTickCount();
    double elapsedTime_ms = (currentTick - state.lastFrameTime) * 1000.0 / cv::getTickFrequency();
    
    double fps = state.activeBackground.get_fps();
    if (fps <= 0) fps = 30.0;

    int delay_ms = static_cast<int>(1000.0 / fps - elapsedTime_ms);
    if (delay_ms < 1) delay_ms = 1;

    int key = cv::waitKey(delay_ms);
    state.lastFrameTime = cv::getTickCount(); // Update for next loop
    return key;
}