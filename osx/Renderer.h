//
//  Renderer.h
//  Vantage
//
//  Created by Joe Drago on 9/5/19.
//  Copyright © 2019 Joe Drago. All rights reserved.
//

#import <MetalKit/MetalKit.h>

// Our platform independent renderer class.   Implements the MTKViewDelegate protocol which
//   allows it to accept per-frame update and drawable resize callbacks.
@interface Renderer : NSObject <MTKViewDelegate>

-(nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)view;
- (void)loadImage:(nonnull NSString *)path;

@end

