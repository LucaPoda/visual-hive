#include <iostream>
#include <vector>
#include <string>
#include <optional>
#include <algorithm> // For std::min and std::max
#include <filesystem>
#include <thread>
#include <chrono>
#include <ableton/Link.hpp>

// Platform-specific headers
#ifdef _WIN32
    #include <windows.h>
#else // macOS
    #include <ApplicationServices/ApplicationServices.h>
    #include <Carbon/Carbon.h>
#endif

// For OpenCV
#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/videoio/videoio.hpp>

// Include the new libraries
#include "ConfigManager.h"
#include "AssetManager.h"
#include "PlatformSpecificCode.h"
#include "AbletonLinkManager.h"

namespace fs = std::filesystem;

// Function to resize a frame to fit within a target resolution while maintaining aspect ratio
cv::Mat scaleToFit(const cv::Mat& src, int targetWidth, int targetHeight, const cv::Scalar& bgColor = cv::Scalar(0, 0, 0)) {
    if (src.empty()) {
        return cv::Mat(targetHeight, targetWidth, CV_8UC3, bgColor);
    }

    int srcWidth = src.cols;
    int srcHeight = src.rows;

    if (srcWidth == 0 || srcHeight == 0) {
        return cv::Mat(targetHeight, targetWidth, src.type(), bgColor);
    }

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

void manualSync(ableton::Link * link) {
    // 1. Capture the current time from Link's internal clock.
    // This is the precise moment the user pressed the sync key.
    auto now = std::chrono::microseconds(link->clock().micros());

    // 2. Get the current beat value of the Link session at the time of the key press.
    auto timeline = link->captureAppSessionState();
    double currentBeat = timeline.beatAtTime(now, 4);

    // 3. Calculate the phase offset. This is the fractional part of the beat.
    // We want to correct for this offset. For example, if the beat is 3.4,
    // the phase is 0.4, and we need to subtract that to get to a whole beat (3.0).
    double phaseOffset = std::fmod(currentBeat, 1.0);
    
    // 4. Determine the new, corrected beat value.
    // Subtracting the offset aligns the beat perfectly to the downbeat.
    double correctedBeat = currentBeat - phaseOffset;

    // 5. Use forceBeatAtTime to align the Link timeline.
    // We force the timeline to be at 'correctedBeat' at the exact time the key was pressed ('now').
    timeline.forceBeatAtTime(correctedBeat, now, 4);

    std::cout << "Manual sync triggered." << std::endl;
    std::cout << "Current beat was: " << currentBeat << std::endl;
    std::cout << "Phase offset was: " << phaseOffset << std::endl;
    std::cout << "Timeline forced to beat " << correctedBeat << " at time " << now.count() << " microseconds." << std::endl;
}

bool isBounceActive = false;
bool isAnimating = false;
std::chrono::microseconds animationStartTime;

bool isPlaying = false;

int main() {
    // Load application configuration from JSON file
    ConfigManager configManager("config/config.json");
    const AppConfig& config = configManager.getConfig();

    ableton::Link * link = loadAbletonLink(config);

    // 1. Detect Screens
    DisplayInfo targetDisplay = selectTargetDisplay();

    // 2. Initialize Asset Manager with config data
    AssetManager assetManager(config);
    assetManager.initializeAssets();
    
    // Create and Position OpenCV Window using window name from config
    cv::namedWindow(config.windowName, cv::WINDOW_NORMAL); 
    
    cv::setWindowProperty(config.windowName, cv::WND_PROP_FULLSCREEN, cv::WINDOW_FREERATIO);

    cv::moveWindow(config.windowName, targetDisplay.x, targetDisplay.y);
    cv::resizeWindow(config.windowName, targetDisplay.width, targetDisplay.height);
    
    std::optional<Background> defaultBackgroundAsset = assetManager.getDefaultBackground();
    std::optional<Foreground> defaultForegroundAsset = assetManager.getDefaultForeground();
    std::optional<Foreground> queuedForegroundAsset = std::nullopt;

    Background activeBackgroundAsset;
    Foreground activeForegroundAsset;
    
    if (defaultBackgroundAsset.has_value()) {
        activeBackgroundAsset = defaultBackgroundAsset.value();
    } 
    else {
        std::cerr << "No default background video found. Exiting.\n";
        return 1;
    }

    if (defaultForegroundAsset.has_value()) {
        activeForegroundAsset = defaultForegroundAsset.value();
    } 
    else {
        std::cerr << "No default foreground video found. Exiting.\n";
        return 1;
    }

    activeBackgroundAsset.open();
    activeForegroundAsset.open();

    long long lastFrameTime = cv::getTickCount();
    
    bool strobeFrameToggle = false;
    
    bool linkEnabled = true;
    // Assuming a 4-beat phrase
    std::chrono::steady_clock::time_point* nextStrobeTime = nullptr;

    // The main loop to continuously monitor the Link session.
    double lastBeat = 0.0;
    while (true) {
        // todo: capire come sfruttare questo valore per capire se rekordbox è connesso
        size_t peers = link->numPeers();

        auto now = std::chrono::microseconds(link->clock().micros());
        auto timeline = link->captureAppSessionState();
        // 2. Calculate the current beat.
        double currentBeat = timeline.beatAtTime(now, 4);

        // Read next frame from the current video
        cv::Mat frame = activeBackgroundAsset.get_next_frame();
        
        double scale = 1.0;
        if (isBounceActive || scale != 1.0) { // if bounce is not active continue the animation until default position is reached, then stop
            if (std::floor(currentBeat) > lastBeat) {
                lastBeat = std::floor(currentBeat);
                
                isAnimating = true;
                animationStartTime = now;
            }
            
            if (isAnimating) {
                auto elapsed = now - animationStartTime;
                double beatDuration = (60.0 / timeline.tempo()) * 1000000.0; // in microseconds
                double progress = static_cast<double>(elapsed.count()) / beatDuration;
                
                if (progress < 1.0) {
                    // Calculate the scaling factor for the bounce (e.g., from 1.0 to 1.2 and back)
                    scale = 1.0 + 0.2 * std::sin((progress + 0.5) * (M_PI));
                } else {
                    // Animation is over, use a scale of 1.0
                    double scale = 1.0;
                    isAnimating = false;
                }
            } else {
                // No animation, use a scale of 1.0
                scale = 1.0;
                // You can print or pass this value to your visualization code.
            }
        }
        
        // Scale the video frame to fit the display, maintaining aspect ratio
        cv::Mat outputFrame = scaleToFit(frame, targetDisplay.width, targetDisplay.height);
        
        // Blend the current foreground image on top if one is active
        outputFrame = assetManager.blend(outputFrame, activeForegroundAsset.get_next_frame(), targetDisplay.width, targetDisplay.height, activeForegroundAsset.get_scale() * scale, activeBackgroundAsset.get_foreground_color());

        bool strobeEffectEnabled = isSpaceDown();

        // Apply strobe effect if enabled
        if (strobeEffectEnabled) {
            if (!nextStrobeTime) {
                auto t = std::chrono::steady_clock::now();
                nextStrobeTime = &t;
            }

            auto now = std::chrono::steady_clock::now();
            
            if (now >= *nextStrobeTime) {
                strobeFrameToggle = !strobeFrameToggle;
                
                // Calcola la durata di un beat in millisecondi
                double beatDurationMs = 2000.0 / BPM;
                
                // Imposta il momento del prossimo flash, assicurando la sincronizzazione con il tempo corrente
                auto new_t = now + std::chrono::milliseconds(static_cast<long long>(beatDurationMs));
                nextStrobeTime = &new_t;
            }

            if (strobeFrameToggle) {
                cv::Mat whiteFrame(targetDisplay.height, targetDisplay.width, CV_8UC3, cv::Scalar(255, 255, 255)); // White screen
                cv::imshow(config.windowName, whiteFrame);
            } else {
                cv::imshow(config.windowName, outputFrame);
            }
            
        } else {
            cv::imshow(config.windowName, outputFrame);
        }

        // Calculate delay to maintain FPS
        long long currentTick = cv::getTickCount();
        double elapsedTime_ms = (currentTick - lastFrameTime) * 1000.0 / cv::getTickFrequency();
        double fps = activeBackgroundAsset.get_fps();
        if (fps <= 0) 
            fps = 30.0;

        int delay_ms = static_cast<int>(1000.0 / fps - elapsedTime_ms);
        
        if (delay_ms < 1) {
            delay_ms = 1;
        }

        char key = cv::waitKey(delay_ms);
        lastFrameTime = cv::getTickCount();
        
        // Handle key presses
        if (key == 27) { // or ESC
            break;
        }

        // Check for key presses to switch visuals
        if (key > 0) {
            if (key == 'l') { // Toggle Link on/off
                // todo: non attiva/disattiva LINK ma permette di settare manualmente i BPM o tornare a usare quelli del link
                linkEnabled = !linkEnabled;
                link->enable(linkEnabled);
                std::cout << "Ableton Link " << (linkEnabled ? "enabled" : "disabled") << ".\n";
                // Clear any pending changes when disabling link
                if (!linkEnabled) {
                    queuedForegroundAsset = std::nullopt;
                }
            }
            else if (key == 'r') {
                // Show the state of the timeline before the manual sync.
                auto timeline_before = link->captureAppSessionState();
                auto now_before = std::chrono::microseconds(link->clock().micros());
                double beat_before = timeline_before.beatAtTime(now_before, 4);
                std::cout << "Before sync: Current beat is " << beat_before << std::endl;
                std::cout << "---" << std::endl;

                // --- Manual Sync Trigger ---
                // This is where you would call the function from your key press event handler.
                // For this example, we'll just call it directly.
                std::cout << "User presses sync key on the beat..." << std::endl;
                manualSync(link);
                std::cout << "---" << std::endl;
                
                // Show the state of the timeline immediately after the manual sync.
                // The beat value should now be a whole number.
                auto timeline_after = link->captureAppSessionState();
                auto now_after = std::chrono::microseconds(link->clock().micros());
                double beat_after = timeline_after.beatAtTime(now_after, 4);
                std::cout << "After sync: Current beat is " << beat_after << std::endl;
                std::cout << "---" << std::endl;
            }
            else if (key == 'b') {
                isBounceActive = !isBounceActive;
            }
            else {
                std::optional<Background> bg = assetManager.getBackroundByPressedKey(key); 
                std::optional<Foreground> fg = assetManager.getForegroundByPressedKey(key); 

                if (bg.has_value()) {
                    // skip if the selected background is the current one
                    if (activeBackgroundAsset.get_source() == bg.value().get_source()) {
                        continue;
                    }
                    
                    activeBackgroundAsset.close();

                    if (bg->open()) {
                        activeBackgroundAsset = bg.value();

                        if (queuedForegroundAsset.has_value()) {
                            activeForegroundAsset = queuedForegroundAsset.value();
                            std::cout << "Switched to foreground: " << activeForegroundAsset.get_foreground_path() << "\n";
                            
                        }
                    } else {
                        std::cerr << "Error: Could not open video " << bg.value().get_background_path().value() << "\n";
                    }
                }
                else if (fg.has_value()) { // foreground
                    if (activeForegroundAsset.get_foreground_path() == fg.value().get_foreground_path()) {
                        continue;
                    }
                    queuedForegroundAsset = fg;

                    std::cout << "Queued foreground: " << queuedForegroundAsset->get_foreground_path() << "\n";
                }
            }
        }
    
        std::cout << "LINK: " << peers << " | BPM: " << std::fixed << std::setprecision(2) << BPM << std::flush << "\r";
    }

    cv::destroyAllWindows();

    // Disable the Link session on exit.
    link->enable(false);
    delete link;
    link = nullptr;
    std::cout << "\nStopping Ableton Link..." << std::endl;
    std::cout << "Link session stopped." << std::endl;

    return 0;
}