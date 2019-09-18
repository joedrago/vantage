// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#import "Renderer.h"
#import "vantage.h"

@interface VantageView : MTKView

@property(assign) Renderer * renderer;
@property(assign) Vantage * V;

- (void)allowDragAndDrop;

@end
