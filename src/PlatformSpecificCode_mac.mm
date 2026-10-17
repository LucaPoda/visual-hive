#include "PlatformSpecificCode.h"
#include <mutex>
#include <iostream>

#ifdef __APPLE__
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

// Global state for the C++ bridge
std::mutex frameMutex;
cv::Mat currentLatestFrame;
SCStream* captureStream = nil;

// Delegate class to receive frames asynchronously from macOS
@interface StreamOutputDelegate : NSObject <SCStreamOutput>
@end

@implementation StreamOutputDelegate
- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type {
    if (type != SCStreamOutputTypeScreen) return;

    CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!pixelBuffer) return;

    // Lock the buffer to read raw memory
    CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    void* baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);
    size_t width = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);

    // ScreenCaptureKit outputs BGRA
    cv::Mat rawMat(height, width, CV_8UC4, baseAddress, bytesPerRow);
    cv::Mat bgrMat;
    cv::cvtColor(rawMat, bgrMat, cv::COLOR_BGRA2BGR);

    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

    // Lock our C++ mutex and update the frame so the Render thread can grab it
    std::lock_guard<std::mutex> lock(frameMutex);
    currentLatestFrame = bgrMat.clone(); 
}
@end

StreamOutputDelegate* streamDelegate = nil;

// --- C++ API IMPLEMENTATIONS ---

std::vector<WindowInfo> getOpenWindows() {
    std::vector<WindowInfo> windows;
    
    // Create a pointer to the vector so the block can modify the original object
    std::vector<WindowInfo>* windowsPtr = &windows;
    
    // Create a semaphore to block the C++ thread until the async Obj-C callback finishes
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

    [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                           onScreenWindowsOnly:NO
                                             completionHandler:^(SCShareableContent *content, NSError *error) {
        if (content) {
            for (SCWindow *window in content.windows) {
                // Filter out invisible or nameless background windows
                if (window.title != nil && window.title.length > 0) {
                    WindowInfo info;
                    info.id = window.windowID;
                    info.name = std::string([window.title UTF8String]);
                    info.ownerName = std::string([window.owningApplication.applicationName UTF8String]);
                    
                    // Use the pointer to push the info into the original vector
                    windowsPtr->push_back(info);
                }
            }
        }
        dispatch_semaphore_signal(semaphore); // Unblock C++
    }];

    dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
    return windows;
}

void startWindowCapture(int windowID) {
    if (captureStream != nil) {
        stopWindowCapture(); // Clean up existing streams first
    }

    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    __block SCWindow* targetWindow = nil;

    // We have to fetch the windows again to get the actual SCWindow object
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                           onScreenWindowsOnly:NO
                                             completionHandler:^(SCShareableContent *content, NSError *error) {
        if (content) {
            for (SCWindow *window in content.windows) {
                if (window.windowID == windowID) {
                    targetWindow = window;
                    break;
                }
            }
        }
        dispatch_semaphore_signal(semaphore);
    }];

    dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);

    if (!targetWindow) {
        std::cerr << "[ERROR] Window ID " << windowID << " not found for capture.\n";
        return;
    }

    // Configure the capture to only grab this specific window
    SCContentFilter *filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:targetWindow];
    SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
    
    // Scale up for Retina displays
    config.width = targetWindow.frame.size.width; 
    config.height = targetWindow.frame.size.height;
    config.showsCursor = NO;
    config.pixelFormat = kCVPixelFormatType_32BGRA;
    config.ignoreShadowsSingleWindow = YES;

    streamDelegate = [[StreamOutputDelegate alloc] init];
    captureStream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:nil];
    
    NSError *error = nil;
    // Push the frames to a high-priority background queue
    [captureStream addStreamOutput:streamDelegate type:SCStreamOutputTypeScreen sampleHandlerQueue:dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0) error:&error];

    [captureStream startCaptureWithCompletionHandler:^(NSError *error) {
        if (error) {
            std::cerr << "[ERROR] Failed to start ScreenCaptureKit stream.\n";
        } else {
            std::cout << "[INFO] ScreenCaptureKit stream started successfully.\n";
        }
    }];
}

void stopWindowCapture() {
    if (captureStream) {
        [captureStream stopCaptureWithCompletionHandler:nil];
        captureStream = nil;
        streamDelegate = nil;
    }
}

cv::Mat getLatestWindowFrame() {
    std::lock_guard<std::mutex> lock(frameMutex);
    // If no frame has arrived yet, this returns an empty Mat, 
    // which your GraphicsManager's `scaleToFit` already knows how to handle safely.
    return currentLatestFrame.clone(); 
}

std::vector<std::string> openMultipleFilesDialog(const std::string& title, const std::vector<std::string>& allowedExtensions) {
    std::vector<std::string> selectedFiles;
    
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setTitle:[NSString stringWithUTF8String:title.c_str()]];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:YES];
        
        if (!allowedExtensions.empty()) {
            NSMutableArray<NSString*>* nsExtensions = [NSMutableArray arrayWithCapacity:allowedExtensions.size()];
            for (const auto& ext : allowedExtensions) {
                [nsExtensions addObject:[NSString stringWithUTF8String:ext.c_str()]];
            }
            [panel setAllowedFileTypes:nsExtensions];
        }
        
        if ([panel runModal] == NSModalResponseOK) {
            for (NSURL* url in [panel URLs]) {
                selectedFiles.push_back(std::string([[url path] UTF8String]));
            }
        }
    }
    
    return selectedFiles;
}

#endif // __APPLE__