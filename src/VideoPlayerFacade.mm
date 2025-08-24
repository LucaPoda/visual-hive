// VideoPlayerFacade.mm
// This file contains the full implementation of the Metal view delegate for rendering
// and the main AppKit loop.

#include "VideoPlayerFacade.h"
#include "AssetManager.h"
#include <iostream>
#include <thread>
#include <memory>
#include <optional>
#include "EventQueue.h"

// Include necessary Apple frameworks for Metal and the UI
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <AppKit/AppKit.h>

// Include OpenCV headers
#include <opencv2/opencv.hpp>
#include <dispatch/dispatch.h>

// --- C++ Class for the Visualization Black Box ---
// The FrameQueue class is defined here, nested within the implementation,
// to be visible to all parts of the file that need it.
class VideoPlayerFacade::FrameQueue {
public:
    void push(const cv::Mat& frame) {
        std::unique_lock<std::mutex> lock(_mutex);
        _queue.push(frame.clone());
        _cond.notify_one();
    }

    bool pop(cv::Mat& frame) {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_queue.empty()) {
            return false;
        }
        frame = _queue.front();
        _queue.pop();
        return true;
    }

private:
    std::queue<cv::Mat> _queue;
    std::mutex _mutex;
    std::condition_variable _cond;
};


// Define the struct for Objective-C members here, so it is a complete type
// before you use it in the VideoPlayerFacade constructor.
struct VideoPlayerFacade::ObjcMembers {
    __strong id<NSWindowDelegate> windowDelegate;
    __strong MTKView* mtkView;
    __strong MetalViewDelegate* mtkDelegate;
    __strong NSWindow* window;
    __strong id keyboardEventMonitor;
};

VideoPlayerFacade::VideoPlayerFacade() :
    _frameQueue(new VideoPlayerFacade::FrameQueue()),
    _objcMembers(new VideoPlayerFacade::ObjcMembers()),
    _eventQueue(new EventQueue()) {
}

VideoPlayerFacade::~VideoPlayerFacade() {
    delete _frameQueue;
    delete _objcMembers;
    delete _eventQueue;
}

bool VideoPlayerFacade::isRunning() {
    return _isRunning.load();
}

EventQueue* VideoPlayerFacade::getEventQueue() {
    return _eventQueue;
}

std::shared_ptr<Background> VideoPlayerFacade::getActiveBackground() {
    std::lock_guard<std::mutex> lock(_assetMutex);
    return _activeBackgroundAsset;
}

void VideoPlayerFacade::setActiveBackground(std::shared_ptr<Background> bg) {
    std::lock_guard<std::mutex> lock(_assetMutex);
    _activeBackgroundAsset = bg;
}

std::shared_ptr<Foreground> VideoPlayerFacade::getActiveForeground() {
    std::lock_guard<std::mutex> lock(_assetMutex);
    return _activeForegroundAsset;
}

void VideoPlayerFacade::setActiveForeground(std::shared_ptr<Foreground> fg) {
    std::lock_guard<std::mutex> lock(_assetMutex);
    _activeForegroundAsset = fg;
}

std::optional<std::shared_ptr<Background>> VideoPlayerFacade::getQueuedBackground() {
    std::lock_guard<std::mutex> lock(_assetMutex);
    return _queuedBackgroundAsset;
}

void VideoPlayerFacade::setQueuedBackground(std::shared_ptr<Background> bg) {
    std::lock_guard<std::mutex> lock(_assetMutex);
    _queuedBackgroundAsset = bg;
}

void VideoPlayerFacade::clearQueuedBackground() {
    std::lock_guard<std::mutex> lock(_assetMutex);
    _queuedBackgroundAsset.reset();
}

std::optional<std::shared_ptr<Foreground>> VideoPlayerFacade::getQueuedForeground() {
    std::lock_guard<std::mutex> lock(_assetMutex);
    return _queuedForegroundAsset;
}

void VideoPlayerFacade::setQueuedForeground(std::shared_ptr<Foreground> fg) {
    std::lock_guard<std::mutex> lock(_assetMutex);
    _queuedForegroundAsset = fg;
}

void VideoPlayerFacade::clearQueuedForeground() {
    std::lock_guard<std::mutex> lock(_assetMutex);
    _queuedForegroundAsset.reset();
}

// --- Metal Shaders (written in Metal Shading Language) ---
const char* SHADER_SOURCE = R"(
#include <metal_stdlib>
using namespace metal;

struct Vertex {
    packed_float2 position;
    packed_float2 texCoords;
};

struct TextureVertexOut {
    float4 position [[position]];
    float2 texCoords;
};

vertex TextureVertexOut vertex_shader(
    const device Vertex* vertices [[buffer(0)]],
    unsigned int vertexID [[vertex_id]])
{
    TextureVertexOut out;
    out.position = float4(vertices[vertexID].position, 0.0, 1.0);
    // Removed the vertical flip, as the user requested.
    out.texCoords = vertices[vertexID].texCoords;
    return out;
}

fragment half4 fragment_shader(
    TextureVertexOut in [[stage_in]],
    texture2d<half> texture [[texture(0)]],
    sampler sampler_2d [[sampler(0)]])
{
    half4 color = texture.sample(sampler_2d, in.texCoords);
    return color;
}
)";

// --- Private C++ Class for Metal Rendering Logic ---
class VideoRenderer {
public:
    VideoRenderer(id<MTLDevice> device);
    ~VideoRenderer();
    void updateTextureWithFrame(const cv::Mat& frame);
    void render(MTKView* view);

private:
    id<MTLDevice> _device;
    id<MTLCommandQueue> _commandQueue;
    id<MTLRenderPipelineState> _pipelineState;
    id<MTLBuffer> _vertexBuffer;
    id<MTLTexture> _videoTexture;
};

VideoRenderer::VideoRenderer(id<MTLDevice> device) : _device(device) {
    _commandQueue = [_device newCommandQueue];

    NSError* error = nullptr;
    id<MTLLibrary> library = [_device newLibraryWithSource:[NSString stringWithUTF8String:SHADER_SOURCE]
                                                   options:nil
                                                     error:&error];
    if (error) {
        std::cerr << "Failed to create Metal library: " << [[error localizedDescription] UTF8String] << std::endl;
        return;
    }

    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"vertex_shader"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"fragment_shader"];

    MTLRenderPipelineDescriptor* pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.label = @"VideoRendererPipeline";
    pipelineDescriptor.vertexFunction = vertexFunction;
    pipelineDescriptor.fragmentFunction = fragmentFunction;
    pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    _pipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
    if (error) {
        std::cerr << "Failed to create pipeline state: " << [[error localizedDescription] UTF8String] << std::endl;
        return;
    }

    float vertices[] = {
        -1.0f,  -1.0f,    0.0f, 1.0f,
         1.0f,  -1.0f,    1.0f, 1.0f,
        -1.0f,   1.0f,    0.0f, 0.0f,
        -1.0f,   1.0f,    0.0f, 0.0f,
         1.0f,  -1.0f,    1.0f, 1.0f,
         1.0f,   1.0f,    1.0f, 0.0f
    };

    _vertexBuffer = [_device newBufferWithBytes:vertices
                                        length:sizeof(vertices)
                                       options:MTLResourceCPUCacheModeDefaultCache];
}

VideoRenderer::~VideoRenderer() {
    // ARC manages _device, _commandQueue, _pipelineState, etc.
    // The only thing to manage is the C++-side memory.
}

void VideoRenderer::updateTextureWithFrame(const cv::Mat& frame) {
    if (frame.empty()) {
        return;
    }

    // Convert the frame to BGRA format for compatibility with Metal
    cv::Mat bgraFrame;
    if (frame.channels() == 3) {
        cv::cvtColor(frame, bgraFrame, cv::COLOR_BGR2BGRA);
    } else if (frame.channels() == 4) {
        bgraFrame = frame;
    } else {
        std::cerr << "Unsupported frame format." << std::endl;
        return;
    }

    cv::Mat flippedFrame = bgraFrame;
    if (!_videoTexture || _videoTexture.width != flippedFrame.cols || _videoTexture.height != flippedFrame.rows) {
        // ARC automatically handles the release of the old texture
        MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                                   width:flippedFrame.cols
                                                                                                  height:flippedFrame.rows
                                                                                               mipmapped:NO];
        textureDescriptor.usage = MTLTextureUsageShaderRead;
        _videoTexture = [_device newTextureWithDescriptor:textureDescriptor];
        // The textureDescriptor is automatically released by ARC
    }
    
    NSUInteger bytesPerRow = 4 * flippedFrame.cols;
    MTLRegion region = MTLRegionMake2D(0, 0, flippedFrame.cols, flippedFrame.rows);
    [_videoTexture replaceRegion:region
                      mipmapLevel:0
                        withBytes:flippedFrame.data
                      bytesPerRow:bytesPerRow];
}

void VideoRenderer::render(MTKView* view) {
    if (!_videoTexture) {
        return;
    }

    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    commandBuffer.label = @"FrameCommand";

    MTLRenderPassDescriptor* renderPassDescriptor = view.currentRenderPassDescriptor;
    if (!renderPassDescriptor) {
        return;
    }

    id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
    renderEncoder.label = @"RenderEncoder";

    [renderEncoder setRenderPipelineState:_pipelineState];
    [renderEncoder setVertexBuffer:_vertexBuffer offset:0 atIndex:0];
    [renderEncoder setFragmentTexture:_videoTexture atIndex:0];

    [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];

    [renderEncoder endEncoding];

    [commandBuffer presentDrawable:view.currentDrawable];
    [commandBuffer commit];
}

// --- Objective-C++ View and Delegate for AppKit and MetalKit Integration ---
@interface MetalViewDelegate : NSObject <MTKViewDelegate>
@property (nonatomic, assign) VideoPlayerFacade* player;
@property (nonatomic, assign) VideoRenderer* renderer; // Use assign for C++ objects
@property (nonatomic, assign) VideoPlayerFacade::FrameQueue* frameQueue;
@end

@implementation MetalViewDelegate
@synthesize player = _player;
@synthesize renderer = _renderer;
@synthesize frameQueue = _frameQueue;

- (void)dealloc {
    delete _renderer;
    _renderer = nullptr;
    [super dealloc];
}

- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size {
}

- (void)drawInMTKView:(nonnull MTKView *)view {
    cv::Mat frame;
    if (_frameQueue && _frameQueue->pop(frame)) {
        _renderer->updateTextureWithFrame(frame);
    }
    
    _renderer->render(view);
}
@end

// NSWindowDelegate implementation to handle window close events.
@interface WindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) std::atomic<bool>* isRunning;
@property (nonatomic, strong) id keyboardEventMonitor;
@end

@implementation WindowDelegate
- (void)dealloc {
    if (_keyboardEventMonitor) {
        // Fix the warning by casting the id to a recognized event monitor type.
        [NSEvent removeGlobalMonitor: (id)_keyboardEventMonitor];
    }
    [super dealloc];
}
- (void)windowWillClose:(NSNotification *)notification {
    // Set the flag to false to stop the main loop.
    *_isRunning = false;
    // Post a message to quit the application to ensure the run loop terminates.
    [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeSystemDefined
                                        location:NSMakePoint(0, 0)
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:0
                                           data1:0
                                           data2:0]
               atStart:YES];
}
@end

// The rest of your C++ code
void VideoPlayerFacade::pushFrame(const cv::Mat& frame) {
    _frameQueue->push(frame);
}

void VideoPlayerFacade::stopVisualization() {
    _isRunning.store(false);
}

void VideoPlayerFacade::runAppKitLoop(const DisplayInfo& displayInfo) {
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        NSRect windowRect = NSMakeRect(displayInfo.x, displayInfo.y, displayInfo.width, displayInfo.height);
        
        NSWindowStyleMask windowStyle = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable;
        
        NSWindow* window = [[NSWindow alloc] initWithContentRect:windowRect
                                                       styleMask:windowStyle
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        [window setLevel:NSFloatingWindowLevel];
        
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            std::cerr << "Metal is not supported on this device." << std::endl;
            return;
        }

        MTKView* mtkView = [[MTKView alloc] initWithFrame:windowRect device:device];
        [mtkView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        [mtkView setPaused:NO];
        [mtkView setEnableSetNeedsDisplay:NO];
        [mtkView setClearColor:MTLClearColorMake(0.0, 0.0, 0.0, 1.0)];

        _objcMembers->mtkDelegate = [[MetalViewDelegate alloc] init];
        _objcMembers->mtkDelegate.player = this;
        _objcMembers->mtkDelegate.renderer = new VideoRenderer(device);
        _objcMembers->mtkDelegate.frameQueue = _frameQueue;
        
        _objcMembers->mtkView = mtkView;
        [_objcMembers->mtkView setDelegate:_objcMembers->mtkDelegate];
        [window setContentView:_objcMembers->mtkView];
        
        _objcMembers->windowDelegate = [[WindowDelegate alloc] init];
        ((WindowDelegate*)_objcMembers->windowDelegate).isRunning = &_isRunning;

        // Use a global event monitor to reliably capture keyboard input.
        // This is a more robust solution than relying on the first responder chain.
        // Capture a raw C++ 'this' pointer. This is safe as the block's lifetime is tied to the main run loop,
        // which ensures 'this' remains valid for the duration of the block's existence.
        if (AXIsProcessTrusted() == NO) {
            // Not trusted, so we need to ask the user to grant permission.
            // This will open the Privacy & Security pane.
            CFURLRef url = (__bridge CFURLRef)[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"];
            AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)@{
                (__bridge NSString *)kAXTrustedCheckOptionPrompt: @YES
            });
            
            NSAlert *alert = [[NSAlert alloc] init];
            [alert setMessageText:@"Accessibility Access Required"];
            [alert setInformativeText:@"This application needs Accessibility permissions to monitor keyboard events. Please grant access in System Settings > Privacy & Security > Accessibility."];
            [alert runModal];
        }
        
        _objcMembers->keyboardEventMonitor = [NSEvent addGlobalMonitorForEventsMatchingMask: NSEventMaskKeyDown | NSEventMaskKeyUp
            handler:^(NSEvent* event) {
                bool isKeyDown = event.type == NSEventTypeKeyDown;
                NSString* characters = [event charactersIgnoringModifiers];
                if ([characters length] > 0) {
                    unichar keyChar = [characters characterAtIndex:0];
                    Event keyEvent = { AppEventType::Keyboard, (int)keyChar, 0, isKeyDown };
                    this->getEventQueue()->push(keyEvent);
                }
        }];
        
        ((WindowDelegate*)_objcMembers->windowDelegate).keyboardEventMonitor = _objcMembers->keyboardEventMonitor;
        [window setDelegate:_objcMembers->windowDelegate];
        
        [window makeKeyAndOrderFront:nil];
        
        [NSApp run];
    }
}
