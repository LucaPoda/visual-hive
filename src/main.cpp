#include <iostream>
#include <vector>
#include <string>
#include <algorithm> // For std::min and std::max
#include <iomanip>   // For std::setw and std::left
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

namespace fs = std::filesystem;

// --- Display Info Struct and Functions (Copied from previous response) ---
struct DisplayInfo {
    int id; // A simple ID for user selection
    int width;
    int height;
    std::string name; // Generic name for now
    int x; // Origin X
    int y; // Origin Y
    bool isPrimary; // Renamed from isMain for cross-platform consistency
};

// Global vector to store display info, for use with Windows callback
std::vector<DisplayInfo> g_displays;
int g_displayIdCounter = 0; // To assign unique IDs

// --- Windows-specific display enumeration callback ---
#ifdef _WIN32
BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
    MONITORINFOEX mi;
    mi.cbSize = sizeof(MONITORINFOEX);
    if (GetMonitorInfo(hMonitor, &mi)) {
        DisplayInfo info;
        info.id = ++g_displayIdCounter;
        info.width = mi.rcMonitor.right - mi.rcMonitor.left;
        info.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
        info.x = mi.rcMonitor.left;
        info.y = mi.rcMonitor.top;
        info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

        std::string deviceName(mi.szDevice);
        info.name = "Display " + std::to_string(g_displayIdCounter);
        if (info.isPrimary) {
            info.name += " (Primary)";
        }
        info.name += " (" + deviceName + ")";

        g_displays.push_back(info);
    }
    return TRUE; // Continue enumeration
}
#endif

// Function to get connected displays (platform-agnostic wrapper)
std::vector<DisplayInfo> getConnectedDisplays() {
    g_displays.clear();
    g_displayIdCounter = 0;

#ifdef _WIN32
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
#else // macOS
    CGDirectDisplayID displayIDs[10];
    CGDisplayCount displayCount;

    CGGetActiveDisplayList(10, displayIDs, &displayCount);

    for (int i = 0; i < displayCount; ++i) {
        CGDirectDisplayID displayID = displayIDs[i];
        CGRect bounds = CGDisplayBounds(displayID);
        int width = CGDisplayPixelsWide(displayID);
        int height = CGDisplayPixelsHigh(displayID);
        int x = static_cast<int>(bounds.origin.x);
        int y = static_cast<int>(bounds.origin.y);
        bool isPrimary = (displayID == CGMainDisplayID());

        DisplayInfo info;
        info.id = ++g_displayIdCounter;
        info.width = width;
        info.height = height;
        info.x = x;
        info.y = y;
        info.isPrimary = isPrimary;
        info.name = "Display " + std::to_string(info.id);
        if (isPrimary) {
            info.name += " (Primary)";
        }
        g_displays.push_back(info);
    }
#endif

    std::cout << "Detected " << g_displays.size() << " display(s):\n";
    std::cout << std::left << std::setw(5) << "ID"
              << std::setw(30) << "Name"
              << std::setw(15) << "Resolution"
              << std::setw(15) << "Position\n";
    std::cout << std::string(65, '-') << "\n";

    for (const auto& display : g_displays) {
        std::cout << std::left << std::setw(5) << display.id
                  << std::setw(30) << display.name
                  << std::setw(15) << (std::to_string(display.width) + "x" + std::to_string(display.height))
                  << std::setw(15) << ("(" + std::to_string(display.x) + "," + std::to_string(display.y) + ")\n");
    }

    return g_displays;
}

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

DisplayInfo selectTargetDisplay() {
    std::vector<DisplayInfo> displays = getConnectedDisplays();

    if (displays.empty()) {
        std::cerr << "No displays detected. Exiting.\n";
        exit(1);
    }

    int selectedDisplayId = -1;
    if (displays.size() == 1) {
        selectedDisplayId = displays[0].id;
        std::cout << "Only one display found, selecting it automatically.\n";
    } else {
        std::cout << "\nEnter the ID of the display you want to use for visuals: ";
        std::cin >> selectedDisplayId;
        if (std::cin.fail()) {
            std::cerr << "Invalid input. Please enter a number. Exiting.\n";
            exit(1);
        }
    }

    // Find the selected display info
    DisplayInfo targetDisplay;
    bool found = false;
    for (const auto& d : displays) {
        if (d.id == selectedDisplayId) {
            targetDisplay = d;
            found = true;
            break;
        }
    }

    if (!found) {
        std::cerr << "Display with ID " << selectedDisplayId << " not found. Exiting.\n";
        exit(1);
    }

    std::cout << "Selected display: " << targetDisplay.name
              << " (" << targetDisplay.width << "x" << targetDisplay.height << " pixels"
              << " at (" << targetDisplay.x << ", " << targetDisplay.y << "))\n";


    return targetDisplay;
}

double bpm = 125;
bool isPlaying = false;

int main() {

    std::cout << "Initializing Ableton Link..." << std::endl;

    // Create an Ableton Link instance with a quantum of 4 beats.
    // The quantum defines the musical grid; a value of 4 is standard for a 4/4 time signature.
    int phraseLength = 4; 
    ableton::Link link(phraseLength);

    std::cout << "Connecting to Ableton Link session..." << std::endl;
    
    // Enable the Link session to join the local network.
    link.enable(true);
    link.setTempoCallback([](double newTempo) {
        // This code will be executed whenever the tempo changes
        std::cout << "Tempo changed to: " << newTempo << " BPM\n";
        bpm = newTempo;
    });

    // Load application configuration from JSON file
    ConfigManager configManager("config/config.json");
    const AppConfig& config = configManager.getConfig();

    // 1. Detect Screens
    
    DisplayInfo targetDisplay = selectTargetDisplay();

    // 2. Initialize Asset Manager with config data
    AssetManager assetManager(config);
    assetManager.initializeAssets();
    const auto& assets = assetManager.getAssets();

    if (assets.empty()) {
        std::cerr << "No assets found in the 'assets' directory. Exiting.\n";
        return 1;
    }
    
    // Create and Position OpenCV Window using window name from config
    cv::namedWindow(config.windowName, cv::WINDOW_NORMAL); 
    
    cv::setWindowProperty(config.windowName, cv::WND_PROP_FULLSCREEN, cv::WINDOW_FREERATIO);

    cv::moveWindow(config.windowName, targetDisplay.x, targetDisplay.y);
    cv::resizeWindow(config.windowName, targetDisplay.width, targetDisplay.height);
    
    // 3. Main Loop Variables
    cv::VideoCapture cap;
    cv::Mat currentForeground;
    
    const VisualAsset* activeBackgroundAsset = nullptr;
    const VisualAsset* activeForegroundAsset = nullptr;
    
    // Set initial assets (the first background and first foreground found)
    for(const auto& asset : assets) {
        if (asset.type == BACKGROUND && activeBackgroundAsset == nullptr) {
            activeBackgroundAsset = &asset;
            cap.open(activeBackgroundAsset->path);
            if (!cap.isOpened()) {
                std::cerr << "Initial background video could not be opened.\n";
                activeBackgroundAsset = nullptr;
            }
        }
        if (asset.type == FOREGROUND && activeForegroundAsset == nullptr) {
            activeForegroundAsset = &asset;
            currentForeground = cv::imread(activeForegroundAsset->path, cv::IMREAD_UNCHANGED);
        }
    }
    
    if (activeBackgroundAsset == nullptr) {
        std::cerr << "No background video found. Exiting.\n";
        return 1;
    }
    
    // Set the initial color based on the first background
    std::string bgFilename = fs::path(activeBackgroundAsset->path).filename().string();
    if (config.colorMappings.count(bgFilename)) {
        assetManager.setActiveForegroundColor(config.colorMappings.at(bgFilename));
    } else {
        assetManager.setActiveForegroundColor(cv::Scalar(255, 255, 255)); // Default to white
    }

    long long lastFrameTime = cv::getTickCount();
    
    // Strobe effect variables
    bool strobeFrameToggle = false;
    
    // Loop through video frames
    cv::Mat frame;

    // Use a try-catch block for a clean shutdown on a keyboard interrupt.
    try {
        bool linkEnabled = true;
        // Assuming a 4-beat phrase
        const VisualAsset* queuedForegroundAsset = nullptr;
        std::chrono::steady_clock::time_point* nextStrobeTime = nullptr;

        // The main loop to continuously monitor the Link session.
        while (true) {
            // todo: capire come sfruttare questo valore per capire se rekordbox è connesso
            size_t peers = link.numPeers();

            // Read next frame from the current video
            cap >> frame;

            // Loop the video if it has ended
            if (frame.empty()) {
                cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                cap >> frame;
                if (frame.empty()) {
                    std::cerr << "Error: Could not loop video. Exiting.\n";
                    break;
                }
            }
            
            // Scale the video frame to fit the display, maintaining aspect ratio
            cv::Mat outputFrame = scaleToFit(frame, targetDisplay.width, targetDisplay.height);
            
            // Blend the current foreground image on top if one is active
            if (!currentForeground.empty() && activeForegroundAsset) {
                outputFrame = assetManager.blend(outputFrame, currentForeground, targetDisplay.width, targetDisplay.height, activeForegroundAsset->scale);
            }

            bool strobeEffectEnabled = false;

            // Check if the strobe key is being held down
            #ifdef _WIN32
            strobeEffectEnabled = (GetAsyncKeyState(' ') & 0x8000) != 0;
            #elif __APPLE__
            // macOS alternative: Check if SPACE key is being held down
            strobeEffectEnabled = CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, kVK_Space);
            #endif

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
                    double beatDurationMs = 2000.0 / bpm;
                    
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
            double fps = cap.get(cv::CAP_PROP_FPS);
            if (fps <= 0) fps = 30.0;
            int delay_ms = static_cast<int>(1000.0 / fps - elapsedTime_ms);
            
            if (delay_ms < 1) {
                delay_ms = 1;
            }

            char key = cv::waitKey(delay_ms);
            lastFrameTime = cv::getTickCount();
            
            // Handle key presses
            if (key == 27) { // 'q' or ESC
                break;
            }

            // Check for key presses to switch visuals
            if (key > 0) {
                if (key == 'l') { // Toggle Link on/off
                    // todo: non attiva/disattiva LINK ma permette di settare manualmente i BPM o tornare a usare quelli del link
                    linkEnabled = !linkEnabled;
                    link.enable(linkEnabled);
                    std::cout << "Ableton Link " << (linkEnabled ? "enabled" : "disabled") << ".\n";
                    // Clear any pending changes when disabling link
                    if (!linkEnabled) {
                        queuedForegroundAsset = nullptr;
                    }
                }
                else {
                    for (const auto& asset : assets) {
                        if (asset.key == key) {
                            if (asset.type == BACKGROUND) {
                                // skip if the selected background is the current one
                                if (activeBackgroundAsset && activeBackgroundAsset->path == asset.path) continue;
                                
                                cap.release();
                                cap.open(asset.path);
                                if (cap.isOpened()) {
                                    activeBackgroundAsset = &asset;
                                    
                                    std::string newBgFilename = fs::path(activeBackgroundAsset->path).filename().string();
                                    if (config.colorMappings.count(newBgFilename)) {
                                        assetManager.setActiveForegroundColor(config.colorMappings.at(newBgFilename));
                                    } else {
                                        assetManager.setActiveForegroundColor(cv::Scalar(255, 255, 255)); // Default to white
                                    }

                                    if (queuedForegroundAsset) {
                                        cv::Mat newForeground = cv::imread(queuedForegroundAsset->path, cv::IMREAD_UNCHANGED);
                                        if (!newForeground.empty()) {
                                            currentForeground = newForeground;
                                            activeForegroundAsset = queuedForegroundAsset;
                                            std::cout << "Switched to foreground: " << activeForegroundAsset->path << "\n";
                                        } else {
                                            std::cerr << "Error: Could not load foreground image " << asset.path << "\n";
                                        }

                                        queuedForegroundAsset = nullptr;
                                    }
                                    
                                    
                                } else {
                                    std::cerr << "Error: Could not open video " << asset.path << "\n";
                                }
                            }
                            else { // foreground
                                if (activeForegroundAsset && activeForegroundAsset->path == asset.path) continue;
                                queuedForegroundAsset = &asset;

                                std::cout << "Queued foreground: " << queuedForegroundAsset->path << "\n";
                            }
                            break;
                        }
                    }
                }
            }
        
            std::cout << "LINK: " << peers << " | BPM: " << std::fixed << std::setprecision(2) << bpm << std::flush << "\r";
        }
    } catch (...) {
        // Handle any unexpected exceptions gracefully.
        std::cout << "\nAn error occurred." << std::endl;
    }

    cap.release();
    cv::destroyAllWindows();

    // Disable the Link session on exit.
    link.enable(false);
    std::cout << "\nStopping Ableton Link..." << std::endl;
    std::cout << "Link session stopped." << std::endl;

    return 0;
}
