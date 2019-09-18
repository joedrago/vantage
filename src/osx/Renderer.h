// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#import <MetalKit/MetalKit.h>

@interface Renderer : NSObject <MTKViewDelegate>

- (nonnull instancetype)initMetal:(nonnull MTKView *)view;
- (void)loadImage:(nonnull NSString *)path diff:(nullable NSString *)diffPath;

- (void)mouseDown:(nonnull NSEvent *)event;
- (void)mouseUp:(nonnull NSEvent *)event;
- (void)mouseDragged:(nonnull NSEvent *)event;
- (void)rightMouseDown:(nonnull NSEvent *)event;
- (void)rightMouseDragged:(nonnull NSEvent *)event;
- (void)scrollWheel:(nonnull NSEvent *)event;

// - (bool)keyUp:(nonnull NSEvent *)event;

@end
