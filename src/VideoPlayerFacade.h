// VideoPlayerFacade.h
#ifndef VIDEO_PLAYER_FACADE_H
#define VIDEO_PLAYER_FACADE_H

#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <opencv2/opencv.hpp>
#include "PlatformSpecificCode.h"

// Forward declare the Objective-C classes.
// Note: This file is a C++ header, so these should be wrapped in an Obj-C-specific context
// to prevent C++ compilers from seeing them. A better approach is to use a separate .h file
// but for now, let's make this one work by wrapping in an #ifdef __OBJC__ block.
#ifdef __OBJC__
@class MTKView;
@class MetalViewDelegate;
#else
// C++ compatible forward declarations for Objective-C types
typedef struct MTKView MTKView;
typedef struct MetalViewDelegate MetalViewDelegate;
#endif

class VideoPlayerFacade {
public:
    VideoPlayerFacade();
    ~VideoPlayerFacade();

    void pushFrame(const cv::Mat& frame);
    void stopVisualization();
    void runAppKitLoop(const DisplayInfo& displayInfo);

    bool isRunning();

    // Move FrameQueue to the public section
    class FrameQueue;

private:
    FrameQueue* _frameQueue;
    std::atomic<bool> _isRunning{true};

    // Use Objective-C pointer types here, as this is an Objective-C++ class.
    // The previous errors were from a C++ file trying to parse these.
    // Assuming your main.cpp is a C++ file, the fix is to use an opaque pointer
    // and handle the Objective-C types only in the .mm file.
    // Let's use the struct-based approach to fix this cleanly.
    struct ObjcMembers;
    ObjcMembers* _objcMembers;
};

#endif // VIDEO_PLAYER_FACADE_H