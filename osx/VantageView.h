//
//  VantageView.h
//  Vantage
//
//  Created by Joe Drago on 9/5/19.
//  Copyright © 2019 Joe Drago. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import "Renderer.h"

@interface VantageView : MTKView

@property (assign) Renderer *renderer;

- (void)allowDragAndDrop;

@end
