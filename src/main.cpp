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
    #include "VideoPlayerFacade.h" // Include the new library's header
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
    auto now = std::chrono::microseconds(link->clock().micros());
    auto timeline = link->captureAppSessionState();
    double currentBeat = timeline.beatAtTime(now, 4);
    double phaseOffset = std::fmod(currentBeat, 1.0);
    double correctedBeat = currentBeat - phaseOffset;
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

void videoProcessingThread(std::shared_ptr<VideoPlayerFacade> player, const DisplayInfo targetDisplay) {
    // Load application configuration from JSON file
    ConfigManager configManager("config/config.json");
    const AppConfig& config = configManager.getConfig();

    ableton::Link * link = loadAbletonLink(config);

    // 2. Initialize Asset Manager with config data
    AssetManager assetManager(config);
    assetManager.initializeAssets();

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
        return;
    }

    if (defaultForegroundAsset.has_value()) {
        activeForegroundAsset = defaultForegroundAsset.value();
    } 
    else {
        std::cerr << "No default foreground video found. Exiting.\n";
        return;
    }

    activeBackgroundAsset.open();
    activeForegroundAsset.open();
    
    bool strobeFrameToggle = false;
    
    bool linkEnabled = true;
    // Assuming a 4-beat phrase
    std::chrono::steady_clock::time_point* nextStrobeTime = nullptr;
    std::chrono::microseconds _lastVideoFrameTime;
    // The main loop to continuously monitor the Link session.
    double lastBeat = 0.0;
    while (player->isRunning()) {
        auto now = std::chrono::microseconds(link->clock().micros());
        double fps = activeBackgroundAsset.get_fps();
        if (fps <= 0) {
            fps = 30.0; // Default to 30 FPS if not available
        }
        std::chrono::microseconds frameDuration(static_cast<long long>(1000000.0 / fps));
        if (now < _lastVideoFrameTime + frameDuration) {
            continue;
        }
        
        _lastVideoFrameTime = now;

        size_t peers = link->numPeers();
        auto timeline = link->captureAppSessionState();
        double currentBeat = timeline.beatAtTime(now, 4);
        
        cv::Mat frame = activeBackgroundAsset.get_next_frame();
        
        double scale = 1.0;
        if (isBounceActive || scale != 1.0) {
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
            }
        }
        
        cv::Mat outputFrame = scaleToFit(frame, targetDisplay.width, targetDisplay.height);
        outputFrame = assetManager.blend(outputFrame, activeForegroundAsset.get_next_frame(), targetDisplay.width, targetDisplay.height, activeForegroundAsset.get_scale() * scale, activeBackgroundAsset.get_foreground_color());

        bool strobeEffectEnabled = isSpaceDown();
        if (strobeEffectEnabled) {
            if (!nextStrobeTime) {
                auto t = std::chrono::steady_clock::now();
                nextStrobeTime = &t;
            }

            auto now = std::chrono::steady_clock::now();
            
            if (now >= *nextStrobeTime) {
                strobeFrameToggle = !strobeFrameToggle;
                double beatDurationMs = 2000.0 / timeline.tempo();
                auto new_t = now + std::chrono::milliseconds(static_cast<long long>(beatDurationMs));
                nextStrobeTime = &new_t;
            }

            if (strobeFrameToggle) {
                cv::Mat whiteFrame(targetDisplay.height, targetDisplay.width, CV_8UC3, cv::Scalar(255, 255, 255));
                player->pushFrame(whiteFrame);
            } else {
                player->pushFrame(outputFrame);
            }
        } else {
            player->pushFrame(outputFrame);
        }

        long long currentTick = cv::getTickCount();

        // char key = cv::waitKey(delay_ms);
        char key = 0;        
        if (key == 27) {
            break;
        }
        
        if (key > 0) {
            if (key == 'l') {
                linkEnabled = !linkEnabled;
                link->enable(linkEnabled);
                std::cout << "Ableton Link " << (linkEnabled ? "enabled" : "disabled") << ".\n";
                if (!linkEnabled) {
                    queuedForegroundAsset = std::nullopt;
                }
            }
            else if (key == 'r') {
                auto timeline_before = link->captureAppSessionState();
                auto now_before = std::chrono::microseconds(link->clock().micros());
                double beat_before = timeline_before.beatAtTime(now_before, 4);
                std::cout << "Before sync: Current beat is " << beat_before << std::endl;
                std::cout << "---" << std::endl;
                std::cout << "User presses sync key on the beat..." << std::endl;
                manualSync(link);
                std::cout << "---" << std::endl;
                
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
                else if (fg.has_value()) {
                    if (activeForegroundAsset.get_foreground_path() == fg.value().get_foreground_path()) {
                        continue;
                    }
                    queuedForegroundAsset = fg;
                    std::cout << "Queued foreground: " << queuedForegroundAsset->get_foreground_path() << "\n";
                }
            }
        }
    
        double BPM = timeline.tempo();
        std::cout << "LINK: " << peers << " | BPM: " << std::fixed << std::setprecision(2) << BPM << std::flush << "\r";
    }

    cv::destroyAllWindows();
    link->enable(false);
    delete link;
    link = nullptr;
    std::cout << "\nStopping Ableton Link..." << std::endl;
    std::cout << "Link session stopped." << std::endl;
}

int main(int argc, char *argv[]) {
    // Create the player object using a smart pointer.
    // std::shared_ptr ensures the object is not deleted until all copies are gone.

    const DisplayInfo targetDisplay = selectTargetDisplay();
    auto player = std::make_shared<VideoPlayerFacade>();

    // Pass a copy of the shared pointer to the processing thread.
    std::thread processingThread([player, targetDisplay]() {
        videoProcessingThread(player, targetDisplay);
    });

    // Run the main app loop on the main thread.
    player->runAppKitLoop(targetDisplay);

    // The main thread unblocks. Wait for the processing thread to finish.
    processingThread.join();

    // When all threads are finished, the shared_ptr goes out of scope and
    // automatically cleans up the VideoPlayerFacade object.

    return 0;
}
