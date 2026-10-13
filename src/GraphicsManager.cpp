#include "GraphicsManager.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

GraphicsManager::GraphicsManager(const AppConfig& config, const DisplayInfo& display) 
    : windowName(config.windowName), displayWidth(display.width), displayHeight(display.height) 
{
    // 1. Create a resizable window
    cv::namedWindow(windowName, cv::WINDOW_NORMAL);
    
    // 2. Move it to the target display (e.g., the external projector/screen)
    cv::moveWindow(windowName, display.x, display.y);
    
    // 3. Resize it to the display's resolution
    cv::resizeWindow(windowName, displayWidth, displayHeight);
    
    // 4. Force TRUE fullscreen (This overrides the macOS menu bar and dock!)
    cv::setWindowProperty(windowName, cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);
}

GraphicsManager::~GraphicsManager() {
    cv::destroyAllWindows();
}

cv::Mat GraphicsManager::scaleToFit(const cv::Mat& src, int targetWidth, int targetHeight, const cv::Scalar& bgColor) {
    if (src.empty()) return cv::Mat(targetHeight, targetWidth, CV_8UC3, bgColor);

    int srcWidth = src.cols;
    int srcHeight = src.rows;
    if (srcWidth == 0 || srcHeight == 0) return cv::Mat(targetHeight, targetWidth, src.type(), bgColor);

    double scaleWidth = static_cast<double>(targetWidth) / srcWidth;
    double scaleHeight = static_cast<double>(targetHeight) / srcHeight;
    double scaleFactor = scaleWidth;

    int newWidth = static_cast<int>(std::ceil(srcWidth * scaleFactor));
    int newHeight = static_cast<int>(std::ceil(srcHeight * scaleFactor));
    
    cv::Mat resizedFrame;
    cv::resize(src, resizedFrame, cv::Size(newWidth, newHeight), 0, 0, cv::INTER_LINEAR);

    int xOffset = (newWidth - targetWidth) / 2;
    int yOffset = (newHeight - targetHeight) / 2;
    
    // Safety clamp to prevent OpenCV out-of-bounds assertions
    if (xOffset < 0) xOffset = 0;
    if (yOffset < 0) yOffset = 0;

    cv::Rect cropRegion(xOffset, yOffset, targetWidth, targetHeight);    
    // 6. Return the perfectly cropped frame
    return resizedFrame(cropRegion).clone();
}

double GraphicsManager::calculateBounceScale(RuntimeState& state, double currentBeat, double tempo, std::chrono::microseconds now) {
    double scale = 1.0;

    // 1. Detect Beat Change
    // We must update lastBeat regardless of bounce state to keep sync
    if (std::floor(currentBeat) > state.lastBeat) {
        state.lastBeat = std::floor(currentBeat);
        
        // Only trigger a NEW animation if the user actually wants bounce
        if (state.isBounceActive) {
            state.isAnimating = true;
            state.animationStartTime = now;
        }
    }

    // 2. Process Animation Logic
    // This runs if an animation is currently correctly active. 
    // If user turns off bounce mid-beat, this allows the current bounce to finish gracefully 
    // before stopping (because isAnimating will remain true until progress >= 1.0).
    if (state.isAnimating) {
        auto elapsed = now - state.animationStartTime;
        double beatDuration = (60.0 / tempo) * 1000000.0; // microseconds
        double progress = static_cast<double>(elapsed.count()) / beatDuration;

        if (progress < 1.0) {
            // Sine wave bounce: 1.0 -> 1.2 -> 1.0
            scale = 1.0 + 0.2 * std::sin((progress + 0.75) * (M_PI));
        } else {
            // Animation finished
            state.isAnimating = false;
        }
    }
    
    return scale;
}

cv::Mat GraphicsManager::composeFrame(RuntimeState& state, AssetManager& assetMgr, double scale) {
    cv::Mat frame = state.activeBackground.get_next_frame();

    if (state.activeBackground.get_type() == WINDOW_CAPTURE) {
        if (frame.empty()) {
            state.captureFailCount++;
            std::cout << "[WARNING] Capture failed. Attempt " << state.captureFailCount << " to reconnect...\n";
            
            // Try to reconnect on the spot and grab a new frame immediately
            if (state.captureFailCount < 3) {
                state.activeBackground.close();
                state.activeBackground.open();
                frame = state.activeBackground.get_next_frame(); 
            }
            
            // If it STILL failed after our retries, fallback to the default config
            if (frame.empty() && state.captureFailCount >= 3) {
                std::cout << "[ERROR] Window capture lost. Falling back to default.\n";
                
                // Load your defaults (adjust getter names to match your AssetManager)
                state.activeBackground = assetMgr.getDefaultBackground().value();
                state.activeForeground = assetMgr.getDefaultForeground().value();
                
                state.captureFailCount = 0; // Reset counter
                frame = state.activeBackground.get_next_frame(); // Grab safe frame
            }
        } else {
            // Stream is healthy, reset the fail counter
            state.captureFailCount = 0;
        }
    }

    cv::Mat outputFrame = scaleToFit(frame, displayWidth, displayHeight);

    if (!state.activeBackground.get_force_no_foreground()) {
        // Only blend if allowed
        outputFrame = assetMgr.blend(
            outputFrame, 
            state.activeForeground.get_next_frame(), 
            displayWidth, 
            displayHeight, 
            state.activeForeground.get_scale() * scale, 
            state.activeBackground.get_foreground_color()
        );
    }
    return outputFrame;
}

// Aggiungi in GraphicsManager.cpp sotto composeFrame

cv::Mat GraphicsManager::composeCueFrame(RuntimeState& state, AssetManager& assetMgr, double scale) {
    // 1. Determina quale background usare per la preview
    Background& bgToUse = state.cueBackground.has_value() ? state.cueBackground.value() : state.activeBackground;
    cv::Mat bgFrame = bgToUse.get_next_frame();

    if (bgFrame.empty()) {
        // Fallback di base se il frame è vuoto (es. window capture non ancora pronto)
        bgFrame = cv::Mat(displayHeight, displayWidth, CV_8UC3, cv::Scalar(50, 50, 50));
    }

    cv::Mat outputFrame = scaleToFit(bgFrame, displayWidth, displayHeight);

    // 2. Determina quale foreground usare per la preview
    if (!bgToUse.get_force_no_foreground()) {
        Foreground& fgToUse = state.cueForeground.has_value() ? state.cueForeground.value() : state.activeForeground;
        cv::Mat fgFrame = fgToUse.get_next_frame();
        
        outputFrame = assetMgr.blend(
            outputFrame, 
            fgFrame, 
            displayWidth, 
            displayHeight, 
            fgToUse.get_scale() * scale, 
            bgToUse.get_foreground_color()
        );
    }
    
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
    cv::imshow("Laptop_Monitor_Preview", frame);
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