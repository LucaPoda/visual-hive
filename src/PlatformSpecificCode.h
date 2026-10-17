#pragma once

#include <vector>
#include <string>
#include <opencv2/opencv.hpp>

struct DisplayInfo {
    int id; 
    int width;
    int height;
    std::string name; 
    int x; 
    int y; 
    bool isPrimary; 
};

DisplayInfo selectTargetDisplay();
bool isSpaceDown();
bool isShiftDown();

struct WindowInfo {
    int id;
    std::string name;
    std::string ownerName;
};

std::vector<WindowInfo> getOpenWindows();
void startWindowCapture(int windowID);
void stopWindowCapture();
cv::Mat getLatestWindowFrame();

// Native File Picker per Import Multiplo
std::vector<std::string> openMultipleFilesDialog(const std::string& title, const std::vector<std::string>& allowedExtensions);