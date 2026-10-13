#pragma once

#include <vector>
#include <string>
#include <opencv2/opencv.hpp>

struct DisplayInfo {
    int id; // A simple ID for user selection
    int width;
    int height;
    std::string name; // Generic name for now
    int x; // Origin X
    int y; // Origin Y
    bool isPrimary; // Renamed from isMain for cross-platform consistency
};

DisplayInfo selectTargetDisplay();
bool isSpaceDown();

struct WindowInfo {
    int id;
    std::string name;
    std::string ownerName;
};

std::vector<WindowInfo> getOpenWindows();
void startWindowCapture(int windowID);
void stopWindowCapture();
cv::Mat getLatestWindowFrame();