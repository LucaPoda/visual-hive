#include "PlatformSpecificCode.h"
#include <iostream>
#include <iomanip>   // For std::setw and std::left

#ifdef _WIN32
#include <windows.h>
#else // macOS
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#endif

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
    std::vector<DisplayInfo> g_displays;
    int g_displayIdCounter = 0; // To assign unique IDs

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

bool isSpaceDown() {
    bool space = false;
    #ifdef _WIN32
    strobeEffectEnabled = (GetAsyncKeyState(' ') & 0x8000) != 0;
    #elif __APPLE__
    // macOS alternative: Check if SPACE key is being held down
    space = CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, kVK_Space);
    #endif
    return space;
}