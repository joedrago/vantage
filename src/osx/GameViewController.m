// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#import "GameViewController.h"

#import "Renderer.h"
#import "VantageView.h"

@implementation GameViewController {
    VantageView * _view;
    Renderer * _renderer;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    _view = (VantageView *)self.view;
    [_view allowDragAndDrop];
    _view.device = MTLCreateSystemDefaultDevice();
    if (!_view.device) {
        NSLog(@"Metal is not supported on this device");
        self.view = [[NSView alloc] initWithFrame:self.view.frame];
        return;
    }

    _renderer = [[Renderer alloc] initMetal:_view];
    _view.renderer = _renderer;
    [_renderer mtkView:_view drawableSizeWillChange:_view.bounds.size];
    _view.delegate = _renderer;
}

@end
