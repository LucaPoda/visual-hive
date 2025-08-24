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

void manualSync(std::shared_ptr<ableton::Link> link) {
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

bool isNearMultiple(double value, double displacement, const double divisor, double tolerance) {
    // We get the remainder of the value divided by the divisor.
    double mod = std::fmod(value, divisor);
    double remainder = mod - displacement;
    // std::cout << "BEAT " << mod << " - " << displacement << " = " << remainder << " OF " << divisor << std::endl;
    // We check if the remainder is close to zero or the divisor.
    // The second condition handles cases where the remainder is slightly less than the divisor,
    // for example 31.999999999999996 is near 32.
    return std::abs(remainder) < tolerance || std::abs(divisor - remainder) < tolerance;
}

bool isAnimating = false;
std::chrono::microseconds animationStartTime;

bool isPlaying = false;

void videoProcessingThread(std::shared_ptr<VideoPlayerFacade> player, const DisplayInfo targetDisplay) {
    // Check if the player is validå
    if (!player) {
        std::cerr << "Player pointer is null. Exiting processing thread." << std::endl;
        return;
    }

    // Load application configuration from JSON file
    ConfigManager configManager("config/config.json");
    const AppConfig& config = configManager.getConfig();

    std::shared_ptr<ableton::Link> link = loadAbletonLink(config);

    // 2. Initialize Asset Manager with config data
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
    
    // Store assets in the facade for safe access
    player->setActiveBackground(activeBackgroundAsset);
    player->setActiveForeground(activeForegroundAsset);

    activeBackgroundAsset->open();
    activeForegroundAsset->open();

    bool strobeFrameToggle = false;
    std::chrono::steady_clock::time_point* nextStrobeTime = nullptr;

    long long lastFrameTime = cv::getTickCount();
    
    // Define the beat interval for CUE changes
    const double cueBeatInterval = 32.0;
    double lastCueBeat = 0.0;
    double displacement = 0.0;

    // The main loop to continuously monitor the Link session and process events.
    double lastBeat = 0.0;
    while (player->isRunning()) {
        auto now = std::chrono::microseconds(link->clock().micros());
        auto timeline = link->captureAppSessionState();
        double currentBeat = timeline.beatAtTime(now, cueBeatInterval);

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
                    case 'r':
                        std::cout << "Before sync: Current beat is " << currentBeat << std::endl;
                        std::cout << "---" << std::endl;
                        std::cout << "User presses sync key on the beat..." << std::endl;
                        manualSync(link);
                        displacement = std::fmod(currentBeat, cueBeatInterval);
                        
                        std::cout << "---" << std::endl;
                        
                        auto timeline_after = link->captureAppSessionState();
                        auto now_after = std::chrono::microseconds(link->clock().micros());
                        double beat_after = timeline_after.beatAtTime(now_after, 4);
                        std::cout << "After sync: Current beat is " << beat_after << std::endl;
                        std::cout << "---" << std::endl;
                        // Other key bindings will go here
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
            if (isNearMultiple(currentBeat, displacement, cueBeatInterval, 0.1)) {
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
                // std::cout << "BEAT " << currentInterval << " OF " << cueBeatInterval << std::endl;
            }
        }

        // --- Frame Generation ---
        cv::Mat frame = activeBackgroundAsset->get_next_frame();
        
        // Apply effects
        
        double scale = 1.0;
        if (player->isBounceActive.load() || scale != 1.0) {
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
                    scale = 1.0 + 0.1 * std::sin((progress + 0.5) * (M_PI));
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

        cv::Mat outputFrame = scaleToFit(frame, targetDisplay.width, targetDisplay.height); // Placeholder resolution
        outputFrame = assetManager.blend(outputFrame, activeForegroundAsset->get_next_frame(), targetDisplay.width, targetDisplay.height, activeForegroundAsset->get_scale()* scale, activeBackgroundAsset->get_foreground_color());
        
        if (player->isStrobeActive.load()) {
            if (!nextStrobeTime) {
                auto t = std::chrono::steady_clock::now();
                nextStrobeTime = &t;
            }

            auto now = std::chrono::steady_clock::now();
            
            if (now >= *nextStrobeTime) {
                strobeFrameToggle = !strobeFrameToggle;
                double beatDurationMs = 6000.0 / timeline.tempo();
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
        
        // Prevent the thread from running too fast
        double fps = activeBackgroundAsset->get_fps();
        if (fps <= 0) fps = 30.0;
        
        long long currentTick = cv::getTickCount();
        double elapsedTime_ms = (currentTick - lastFrameTime) * 1000.0 / cv::getTickFrequency();
        int delay_ms = static_cast<int>(1000.0 / fps - elapsedTime_ms);
        if (delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
        lastFrameTime = cv::getTickCount();

        // Print Link info
        double BPM = timeline.tempo();
        std::cout << "LINK: " << link->numPeers() << " | BPM: " << std::fixed << std::setprecision(2) << BPM << std::flush << "\r";
    }

    link->enable(false);
    std::cout << "\nStopping Ableton Link..." << std::endl;
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
