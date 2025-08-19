// VideoPlayerFacade_objc.h
#ifndef VIDEO_PLAYER_FACADE_OBJC_H
#define VIDEO_PLAYER_FACADE_OBJC_H

// Forward declare the Objective-C classes
@class MTKView;
@class MetalViewDelegate;

// This struct is used to hold the Objective-C member variables
// to keep the main C++ header clean.
struct VideoPlayerFacadeObjcMembers {
    __strong MetalViewDelegate* delegate;
    __strong MTKView* mtkView;
};

#endif // VIDEO_PLAYER_FACADE_OBJC_H