//
// VideoPlayerFacade.mm
// The implementation of the multi-threaded video player library.
//

#include "VideoPlayerFacade.h"
#include "VideoPlayerFacade_objc.h" // Include the new header
#include "PlatformSpecificCode.h"
#include <iostream>

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
    __strong MetalViewDelegate* delegate;
    __strong MTKView* mtkView;
};

VideoPlayerFacade::VideoPlayerFacade() :
    _frameQueue(new VideoPlayerFacade::FrameQueue()),
    _objcMembers(new VideoPlayerFacade::ObjcMembers()) {
}

VideoPlayerFacade::~VideoPlayerFacade() {
    delete _frameQueue;
    delete _objcMembers;
}

bool VideoPlayerFacade::isRunning() {
    return _isRunning.load();
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

    // Flip the frame vertically for Metal coordinate system
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
@interface MetalViewDelegate : NSObject <MTKViewDelegate, NSWindowDelegate> {
    VideoPlayerFacade::FrameQueue* _frameQueue;
}
@property (nonatomic, assign) VideoRenderer* renderer; // Use assign for C++ objects
@property (nonatomic, assign) std::atomic<bool>* isRunning;
- (void)setFrameQueue:(VideoPlayerFacade::FrameQueue*)frameQueue;
@end
@implementation MetalViewDelegate
@synthesize renderer = _renderer;

- (void)setFrameQueue:(VideoPlayerFacade::FrameQueue*)frameQueue {
    _frameQueue = frameQueue;
}

- (void)dealloc {
    // Correctly handle the C++ object's memory
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
    } else {
    }
    
    _renderer->render(view);
    
    if (!*_isRunning) {
        [NSApp stop:nil];
    }
}

- (void)windowWillClose:(NSNotification *)notification {
    // Set the atomic flag to false to stop the thread.
    *_isRunning = false;
}

@end

void VideoPlayerFacade::pushFrame(const cv::Mat& frame) {
    _frameQueue->push(frame);
}

void VideoPlayerFacade::stopVisualization() {
    _isRunning.store(false);
}

void VideoPlayerFacade::runAppKitLoop(const DisplayInfo& displayInfo) {
    @autoreleasepool {

        std::cout << "DEBUG: Starting runAppKitLoop on display: " << displayInfo.name << std::endl;

        NSApplication* application = [NSApplication sharedApplication];
        NSRect windowRect = NSMakeRect(displayInfo.x, displayInfo.y, displayInfo.width, displayInfo.height);
        
        // Use NSWindowStyleMaskBorderless for a fullscreen window.
        NSUInteger windowStyle = NSWindowStyleMaskBorderless;
        
        NSWindow* window = [[NSWindow alloc] initWithContentRect:windowRect
                                                       styleMask:windowStyle
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        std::cout << "DEBUG: NSWindow created at " << displayInfo.x << "," << displayInfo.y << " with size " << displayInfo.width << "x" << displayInfo.height << "." << std::endl;

        // Set the window level to floating, so it appears above all other windows.
        [window setLevel:NSFloatingWindowLevel];
        
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            std::cerr << "Metal is not supported on this device." << std::endl;
            return;
        }

        // Create the MetalKit view.
        MTKView* mtkView = [[MTKView alloc] initWithFrame:windowRect device:device];
        [mtkView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        [mtkView setPaused:NO];
        [mtkView setEnableSetNeedsDisplay:NO];
        [mtkView setClearColor:MTLClearColorMake(0.0, 0.0, 0.0, 1.0)];

        // Create the delegate and set it.
        _objcMembers->delegate = [[MetalViewDelegate alloc] init];
        _objcMembers->mtkView = mtkView;
        _objcMembers->delegate.renderer = new VideoRenderer(device);
        [_objcMembers->delegate setFrameQueue:_frameQueue];
        _objcMembers->delegate.isRunning = &_isRunning;

        [_objcMembers->mtkView setDelegate:_objcMembers->delegate];
        [window setContentView:_objcMembers->mtkView];

        // Make the window visible and enter the main event loop.
        [window makeKeyAndOrderFront:nil];
        
        [window setDelegate:_objcMembers->delegate];

        // This is the line that was missing. It starts the main loop.
        [NSApp run];
    }
}