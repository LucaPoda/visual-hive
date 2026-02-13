#include "WindowCapturer.hpp"

#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

#include <iostream>
#include <mutex>

#include <iostream>

// --- Internal Implementation Struct ---
struct CapturerImpl {
    // 1. Initialize pointers to nil to prevent ARC from releasing garbage!
    SCStream* stream = nil;
    
    // We keep the delegate strong here to ensure it survives
    id delegate = nil; 
    
    // Custom queue so we don't block the main thread
    dispatch_queue_t captureQueue;

    std::mutex frameMutex;
    cv::Mat latestFrame;
    bool isRunning = false;

    CapturerImpl() {
        // Create a serial queue for frame processing
        captureQueue = dispatch_queue_create("com.visualhive.capture", DISPATCH_QUEUE_SERIAL);
    }

    ~CapturerImpl() {
        // Release the queue when done
        // (ARC handles ObjC objects, but dispatch_queues depend on OS version)
        // Modern macOS handles dispatch_queue_t with ARC automatically.
    }
};

// --- Objective-C Delegate ---
@interface StreamDelegate : NSObject <SCStreamOutput, SCStreamDelegate>
{
    @public
    CapturerImpl* cppImpl;
}
@end

@implementation StreamDelegate

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type {
    // 1. Filter out non-screen data immediately
    if (type != SCStreamOutputTypeScreen || !sampleBuffer) return;

    CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!pixelBuffer) return;

    CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    
    void* baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);
    size_t width = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);

    // --- DEBUG START ---
    // Print stats once every ~60 frames (approx 1 sec)
    std::cout << "[WindowCapturer]" 
                << " Size: " << width << "x" << height 
                << " | Addr: " << baseAddress 
                << " | BytesRow: " << bytesPerRow << std::endl;
    // --- DEBUG END ---

    if (baseAddress) {
        std::lock_guard<std::mutex> lock(cppImpl->frameMutex);
        
        // SCK returns BGRA. Map it and clone deeply.
        // Note: SCK buffers often have padding, so 'bytesPerRow' is crucial!
        cv::Mat rawFrame(height, width, CV_8UC4, baseAddress, bytesPerRow);
        
        // Ensure the copy happens
        if (!rawFrame.empty()) {
             rawFrame.copyTo(cppImpl->latestFrame);
        } else {
             std::cerr << "[WindowCapturer] Error: rawFrame wrapper is empty!" << std::endl;
        }
    } else {
        std::cerr << "[WindowCapturer] Error: baseAddress is NULL (Locked but empty?)" << std::endl;
    }

    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
    std::cerr << "[WindowCapturer] Stream stopped with error: " 
              << error.localizedDescription.UTF8String << std::endl;
}

@end

// --- C++ Class Implementation ---

WindowCapturer::WindowCapturer() : impl(new CapturerImpl()) {}

WindowCapturer::~WindowCapturer() {
    stopCapture();
    delete impl;
}

bool WindowCapturer::startCapture(const std::string& windowName) {
    if (impl->isRunning) return true;

    // Use a semaphore to make this async API behave synchronously for your C++ app
    dispatch_semaphore_t sema = dispatch_semaphore_create(0);
    __block bool success = false;

    // 1. Get Content
    [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent *content, NSError *error) {
        if (error) {
            std::cerr << "[WindowCapturer] Error fetching content: " << error.localizedDescription.UTF8String << std::endl;
            dispatch_semaphore_signal(sema);
            return;
        }

        // 2. Find the Window (Inside the block!)
        NSString* searchName = [NSString stringWithUTF8String:windowName.c_str()];
        SCWindow* foundWindow = nil;
        
        for (SCWindow* window in content.windows) {
            NSString* title = window.title ? window.title : @"";
            NSString* appName = window.owningApplication.applicationName ? window.owningApplication.applicationName : @"";
            
            if ([appName containsString:searchName] || [title containsString:searchName]) {
                foundWindow = window;
                break;
            }
        }

        if (!foundWindow) {
            std::cerr << "[WindowCapturer] Window not found: " << windowName << std::endl;
            dispatch_semaphore_signal(sema);
            return;
        }

        // 3. Find the Display (Crucial for macOS 14+)
        SCDisplay* foundDisplay = content.displays.firstObject; // Default backup
        for (SCDisplay* display in content.displays) {
            if (CGRectIntersectsRect(display.frame, foundWindow.frame)) {
                foundDisplay = display;
                break;
            }
        }

        if (!foundDisplay) {
             std::cerr << "[WindowCapturer] Display not found for window." << std::endl;
             dispatch_semaphore_signal(sema);
             return;
        }

        std::cout << "[WindowCapturer] Initializing stream for: " << (foundWindow.title ? foundWindow.title.UTF8String : "Untitled") << std::endl;

        // 4. Create Filter & Config (Still inside the block, so objects are valid!)
        SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:foundDisplay includingWindows:@[foundWindow]];
        
        SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
        config.width = (size_t)foundWindow.frame.size.width * 2;
        config.height = (size_t)foundWindow.frame.size.height * 2;
        config.pixelFormat = kCVPixelFormatType_32BGRA;
        config.showsCursor = NO;
        config.queueDepth = 3;

        // 5. Setup Delegate
        StreamDelegate* delegateObj = [[StreamDelegate alloc] init];
        delegateObj->cppImpl = impl;
        impl->delegate = delegateObj; // Keep it alive

        // 6. Create & Start Stream
        impl->stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:nil];
        
        NSError* startError = nil;
        [impl->stream addStreamOutput:delegateObj type:SCStreamOutputTypeScreen sampleHandlerQueue:impl->captureQueue error:&startError];
        
        [impl->stream startCaptureWithCompletionHandler:^(NSError *error) {
            if (error) {
                std::cerr << "[WindowCapturer] Failed to start: " << error.localizedDescription.UTF8String << std::endl;
            } else {
                std::cout << "[WindowCapturer] Stream running!" << std::endl;
                impl->isRunning = true;
            }
        }];

        // Mark success and signal main thread to wake up
        success = true;
        dispatch_semaphore_signal(sema);
    }];

    // Main thread waits here until the block finishes setup
    dispatch_semaphore_wait(sema, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));

    return success;
}
void WindowCapturer::stopCapture() {
    if (impl->stream && impl->isRunning) {
        [impl->stream stopCaptureWithCompletionHandler:nil];
        impl->stream = nil;
        impl->delegate = nil; // Release delegate
        impl->isRunning = false;
    }
}

bool WindowCapturer::getLatestFrame(cv::Mat& outFrame) {
    std::cout << "a" << std::endl;
    // 1. Safety check: ensure implementation exists
    if (!impl) return false;

    std::cout << "b" << std::endl;
    // 2. Lock the mutex to ensure we don't read while writing
    std::lock_guard<std::mutex> lock(impl->frameMutex);
    
    std::cout << "c" << std::endl;
    // 3. Check if we actually have data
    if (impl->latestFrame.empty()) {
        std::cout << "d" << std::endl;
        return false;
    }
    
    std::cout << "e" << std::endl;
    
    // 4. Deep copy the internal buffer to the user's reference
    // usage of copyTo handles reallocation automatically if sizes differ
    impl->latestFrame.copyTo(outFrame);
    
    std::cout << "f" << std::endl;
    return true;
}