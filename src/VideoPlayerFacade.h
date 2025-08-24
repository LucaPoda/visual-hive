// VideoPlayerFacade.h
#ifndef VIDEO_PLAYER_FACADE_H
#define VIDEO_PLAYER_FACADE_H

#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <opencv2/opencv.hpp>
#include <memory>
#include <optional>
#include "EventQueue.h" // Include the new header
#include "PlatformSpecificCode.h"

// Forward declare Objective-C types
#ifdef __OBJC__
@class MTKView;
@class MetalViewDelegate;
#else
typedef struct MTKView MTKView;
typedef struct MetalViewDelegate MetalViewDelegate;
#endif

// Forward declare the Asset classes to allow shared pointers
class Background;
class Foreground;

class VideoPlayerFacade {
public:
    VideoPlayerFacade();
    ~VideoPlayerFacade();

    void pushFrame(const cv::Mat& frame);
    void stopVisualization();
    void runAppKitLoop(const DisplayInfo& displayInfo);
    bool isRunning();
    
    // Getter for the event queue
    EventQueue* getEventQueue();

    // Getters and setters for thread-safe access to assets
    std::shared_ptr<Background> getActiveBackground();
    void setActiveBackground(std::shared_ptr<Background> bg);

    std::shared_ptr<Foreground> getActiveForeground();
    void setActiveForeground(std::shared_ptr<Foreground> fg);

    // Methods for cueing assets
    std::optional<std::shared_ptr<Background>> getQueuedBackground();
    void setQueuedBackground(std::shared_ptr<Background> bg);
    void clearQueuedBackground();

    std::optional<std::shared_ptr<Foreground>> getQueuedForeground();
    void setQueuedForeground(std::shared_ptr<Foreground> fg);
    void clearQueuedForeground();
    
    // Atomic flags for thread-safe state
    std::atomic<bool> isStrobeActive{false};
    std::atomic<bool> isBounceActive{false};
    std::atomic<bool> isCueActive{false};
    
    class FrameQueue;
    FrameQueue* _frameQueue;

private:
    std::atomic<bool> _isRunning{true};
    DisplayInfo _displayInfo;

    // Thread-safe event queue
    EventQueue* _eventQueue;
    
    // Shared pointers for assets
    std::shared_ptr<Background> _activeBackgroundAsset;
    std::shared_ptr<Foreground> _activeForegroundAsset;
    
    // Optional shared pointers for queued assets
    std::optional<std::shared_ptr<Background>> _queuedBackgroundAsset;
    std::optional<std::shared_ptr<Foreground>> _queuedForegroundAsset;
    
    std::mutex _assetMutex;

    // Use a struct to hold Objective-C members
    struct ObjcMembers;
    ObjcMembers* _objcMembers;
};

#endif // VIDEO_PLAYER_FACADE_H
