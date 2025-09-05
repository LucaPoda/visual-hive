#include <iostream>
#include <vector>
#include <string>
#include <optional>
#include <algorithm> // For std::min and std::max
#include <filesystem>
#include <thread>
#include <chrono>
#include <deque> // For storing tap times
#include <atomic> // For thread-safe BPM variable
#include <iomanip> // For std::setprecision

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
#include "BpmDetector.h"
#include "PlatformSpecificCode.h"

namespace fs = std::filesystem;

// Thread-safe variables for synchronization
std::atomic<bool> isSyncActive(false);
std::chrono::steady_clock::time_point lastSyncTime;

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

bool isNearMultiple(double value, const double divisor, double tolerance) {
    double mod = std::fmod(value, divisor);
    return std::abs(mod) < tolerance;
}

bool isAnimating = false;
std::chrono::steady_clock::time_point animationStartTime;

void videoProcessingThread(std::shared_ptr<VideoPlayerFacade> player, const DisplayInfo targetDisplay) {
    if (!player) {
        std::cerr << "Player pointer is null. Exiting processing thread." << std::endl;
        return;
    }

    ConfigManager configManager("config/config.json");
    const AppConfig& config = configManager.getConfig();

    AssetManager assetManager(config);
    assetManager.initializeAssets();

    std::shared_ptr<Background> activeBackgroundAsset = assetManager.getDefaultBackground();
    std::shared_ptr<Foreground> activeForegroundAsset = assetManager.getDefaultForeground();

    if (!activeBackgroundAsset) {
        std::cerr << "No default background video found. Exiting.\n";
        return;
    }
    if (!activeForegroundAsset) {
        std::cerr << "No default foreground video found. Exiting.\n";
        return;
    }

    player->setActiveBackground(activeBackgroundAsset);
    player->setActiveForeground(activeForegroundAsset);

    activeBackgroundAsset->open();
    activeForegroundAsset->open();

    bool strobeFrameToggle = false;
    std::chrono::steady_clock::time_point nextStrobeTime = std::chrono::steady_clock::now();
    
    long long lastFrameTime = cv::getTickCount();

    // Define the beat interval for CUE changes
    const double cueBeatInterval = 32.0;
    double lastCueBeat = 0.0;

    // Initialize sync time to the start of the application
    lastSyncTime = std::chrono::steady_clock::now();

    // Beat tracking variables
    double lastBeatValue = 0.0;
    
    // The main loop to continuously monitor and process events.
    while (player->isRunning()) {
        auto now = std::chrono::steady_clock::now();
        double currentBPM = *g_BPM;
        
        // Check for sync event
        if (isSyncActive.load()) {
            lastSyncTime = now;
            isSyncActive.store(false);
            lastBeatValue = 0.0; // Reset beat counter
            std::cout << "Manual sync triggered." << std::endl;
        }

        // Calculate current beat from last sync time
        double beatDurationSec = 60.0 / currentBPM;
        double elapsedSeconds = std::chrono::duration<double>(now - lastSyncTime).count();
        double currentBeat = elapsedSeconds / beatDurationSec;

        // --- Process Events ---
        Event event;
        while (player->getEventQueue()->pop(event)) {
            // Handle key down/up events
            if (event.type == AppEventType::Keyboard) {
                switch (event.keyCode) {
                    case 'b': // Bounce
                        if (event.isKeyDown) {
                            player->isBounceActive.store(!player->isBounceActive.load());
                            std::cout << "BOUNCE mode is now: " << (player->isBounceActive.load() ? "ON" : "OFF") << std::endl;
                        }
                        break;
                    case ' ': // Strobe
                        player->isStrobeActive.store(event.isKeyDown);
                        break;
                    case 'c': // CUE
                        if (event.isKeyDown) {
                            player->isCueActive.store(!player->isCueActive.load());
                            std::cout << "CUE mode is now: " << (player->isCueActive.load() ? "ON" : "OFF") << std::endl;
                        }
                        break;
                    case 'r': // Resync the beat counter
                        if (event.isKeyDown) {
                           isSyncActive.store(true);
                        }
                        break;
                }

                // --- Background/Foreground Swapping Logic ---
                std::shared_ptr<Background> newBg = assetManager.getBackroundByPressedKey(event.keyCode);
                std::shared_ptr<Foreground> newFg = assetManager.getForegroundByPressedKey(event.keyCode);

                if (newBg && event.isKeyDown) {
                    if (player->isCueActive.load()) {
                        player->setQueuedBackground(newBg);
                        std::cout << "Queued background change." << std::endl;
                    } else {
                        // Instant change
                        activeBackgroundAsset->close();
                        activeBackgroundAsset = newBg;
                        activeBackgroundAsset->open();
                        player->setActiveBackground(activeBackgroundAsset);
                    }
                }

                if (newFg) {
                    if (player->isCueActive.load()) {
                        player->setQueuedForeground(newFg);
                        std::cout << "Queued foreground change." << std::endl;
                    } else {
                        // Instant change
                        activeForegroundAsset->close();
                        activeForegroundAsset = newFg;
                        activeForegroundAsset->open();
                        player->setActiveForeground(activeForegroundAsset);
                    }
                }
            }
        }

        // Cue logic
        if (player->isCueActive.load()) {
            if (isNearMultiple(currentBeat, cueBeatInterval, 0.1)) {
                // Time to apply ther cue change
                std::shared_ptr<Background> bg = nullptr;
                if (player->getQueuedBackground().has_value()) {
                    bg = player->getQueuedBackground().value();
                }
                else {
                    bg = assetManager.getRandomBackground();
                }
                activeBackgroundAsset->close();
                activeBackgroundAsset = bg;
                activeBackgroundAsset->open();

                player->setActiveBackground(activeBackgroundAsset);
                player->clearQueuedBackground();
                std::cout << "Applying queued background change." << std::endl;

                std::shared_ptr<Foreground> fg = nullptr;
                if (player->getQueuedForeground().has_value()) {
                    fg = player->getQueuedForeground().value();
                }
                else{
                    fg = assetManager.getRandomForeground();
                }
                activeForegroundAsset->close();
                activeForegroundAsset = fg;
                activeForegroundAsset->open();
                player->setActiveForeground(activeForegroundAsset);
                player->clearQueuedForeground();
                std::cout << "Applying queued foreground change." << std::endl;
            }
            else {
                // std::cout << "BEAT " << currentBeat << " OF " << cueBeatInterval << std::endl;
            }
        }

        // --- Frame Generation and Effects ---
        cv::Mat frame = activeBackgroundAsset->get_next_frame();

        // Apply effects
        
        double scale = 1.0;
        if (player->isBounceActive.load()) {
            if (std::floor(currentBeat) > lastBeatValue) {
                lastBeatValue = std::floor(currentBeat);
                isAnimating = true;
                animationStartTime = std::chrono::steady_clock::now();
            }

            if (isAnimating) {
                auto elapsed = std::chrono::steady_clock::now() - animationStartTime;
                double progress = std::chrono::duration<double>(elapsed).count() / beatDurationSec;
                if (progress < 1.0) {
                    scale = 1.0 + 0.1 * std::sin((progress + 0.5) * (M_PI));
                } else {
                    scale = 1.0;
                    isAnimating = false;
                }
            }
        }

        cv::Mat outputFrame = scaleToFit(frame, targetDisplay.width, targetDisplay.height);
        outputFrame = assetManager.blend(outputFrame, activeForegroundAsset->get_next_frame(), targetDisplay.width, targetDisplay.height, activeForegroundAsset->get_scale() * scale, activeBackgroundAsset->get_foreground_color());

        if (player->isStrobeActive.load()) {
            if (now >= nextStrobeTime) {
                strobeFrameToggle = !strobeFrameToggle;
                double beatDurationMs = 6000.0 / currentBPM;
                nextStrobeTime = now + std::chrono::milliseconds(static_cast<long long>(beatDurationMs));
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

        double fps = activeBackgroundAsset->get_fps();
        if (fps <= 0) fps = 30.0;
        long long currentTick = cv::getTickCount();
        double elapsedTime_ms = (currentTick - lastFrameTime) * 1000.0 / cv::getTickFrequency();
        int delay_ms = static_cast<int>(1000.0 / fps - elapsedTime_ms);
        if (delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
        lastFrameTime = cv::getTickCount();
        std::cout << "BPM: " << std::fixed << std::setprecision(2) << currentBPM << " | " << std::floor(fmod(currentBeat, cueBeatInterval)) << "/" << cueBeatInterval << std::flush << "\r";
    }
}

int main(int argc, char *argv[]) {
    // Start a thread to simulate BPM changes
    std::thread bpmThread([]() {
        bpmDetectionInit();
        bpmDetectionLoop();
    });

    const DisplayInfo targetDisplay = selectTargetDisplay();
    auto player = std::make_shared<VideoPlayerFacade>();

    // Start a video processing thread
    std::thread processingThread([player, targetDisplay]() {
        videoProcessingThread(player, targetDisplay);
    });

    // Run the main app loop on the main thread.
    player->runAppKitLoop(targetDisplay);

    // Wait for the other threads to finish
    processingThread.join();
    bpmThread.join();

    return 0;
}
