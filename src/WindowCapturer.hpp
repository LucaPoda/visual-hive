#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <memory>
#include <mutex>

// Forward declaration of the internal implementation struct
// This keeps Objective-C types out of the header
struct CapturerImpl;

class WindowCapturer {
public:
    WindowCapturer();
    ~WindowCapturer();

    /**
     * Starts capturing a window with the given name (partial match).
     * Returns true if successful.
     */
    bool startCapture(const std::string& windowName);

    /**
     * Stops the capture stream and releases resources.
     */
    void stopCapture();

    /**
     * Returns the most recent frame captured.
     * Returns an empty cv::Mat if no new frame is available or capture is stopped.
     * Thread-safe.
     */
    bool getLatestFrame(cv::Mat& outFrame);

private:
    CapturerImpl* impl; // Pointer to the hidden Objective-C implementation
};