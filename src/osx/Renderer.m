// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#import <ModelIO/ModelIO.h>
#import <simd/simd.h>

#import "Renderer.h"
#import "ShaderTypes.h"
#import "VantageView.h"
#import "vantage.h"

#import "colorist/colorist.h"

static const NSUInteger kMaxBuffersInFlight = 3;
static const int MACOS_SDR_WHITE_NITS = 200;

@implementation Renderer {
    // Vantage
    VantageView * view_;
    Vantage * V;

    // Renderer state
    CGSize windowSize_;

    // Metal
    dispatch_semaphore_t inFlightSemaphore_;
    id<MTLDevice> device_;
    id<MTLCommandQueue> commandQueue_;
    id<MTLRenderPipelineState> pipelineState_;
    id<MTLTexture> metalPreparedImage_;
    id<MTLTexture> metalFontImage_;
    id<MTLTexture> metalFillImage_;
    id<MTLBuffer> vertices_;
    NSUInteger vertexCount_;

    // Awful hacks, don't look at this!
    bool windowTitleSet_;
}

- (nonnull instancetype)initMetal:(nonnull MTKView *)view
{
    self = [super init];
    if (!self) {
        return self;
    }

    // --------------------------------------------------------------------------------------------
    // Stash off values

    view_ = (VantageView *)view;
    device_ = view_.device;
    windowTitleSet_ = false;

    // --------------------------------------------------------------------------------------------
    // Init Vantage

    V = view_.V;

    vantagePlatformSetLinear(V, 1);
    vantagePlatformSetHDRActive(V, 1);

    // --------------------------------------------------------------------------------------------
    // Init Metal

    inFlightSemaphore_ = dispatch_semaphore_create(kMaxBuffersInFlight);

    CAMetalLayer * metalLayer = (CAMetalLayer *)view.layer;
    metalLayer.wantsExtendedDynamicRangeContent = YES;
    metalLayer.pixelFormat = MTLPixelFormatRGBA16Float;
    CGColorSpaceRef colorspace;
    colorspace = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearITUR_2020);
    metalLayer.colorspace = colorspace;
    CGColorSpaceRelease(colorspace);
    CAEDRMetadata * edrMetadata = [CAEDRMetadata HDR10MetadataWithMinLuminance:0
                                                                  maxLuminance:10000.0f
                                                            opticalOutputScale:MACOS_SDR_WHITE_NITS];
    metalLayer.EDRMetadata = edrMetadata;
    view.colorPixelFormat = MTLPixelFormatRGBA16Float;
    view.sampleCount = 1;

    id<MTLLibrary> defaultLibrary = [device_ newDefaultLibrary];
    id<MTLFunction> vertexFunction = [defaultLibrary newFunctionWithName:@"vertexShader"];
    id<MTLFunction> fragmentFunction = [defaultLibrary newFunctionWithName:@"fragmentShader"];

    MTLRenderPipelineDescriptor * pipelineStateDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineStateDescriptor.label = @"Vantage Pipeline";
    pipelineStateDescriptor.vertexFunction = vertexFunction;
    pipelineStateDescriptor.fragmentFunction = fragmentFunction;
    pipelineStateDescriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat;
    pipelineStateDescriptor.colorAttachments[0].blendingEnabled = YES;
    pipelineStateDescriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    pipelineStateDescriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    pipelineStateDescriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineStateDescriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipelineStateDescriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineStateDescriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    NSError * error = NULL;
    pipelineState_ = [device_ newRenderPipelineStateWithDescriptor:pipelineStateDescriptor error:&error];
    NSAssert(pipelineState_, @"Failed to created pipeline state, error %@", error);

    commandQueue_ = [device_ newCommandQueue];

    // clang-format off
    static const VantageVertex rawVertices[] =
    {
        { { 0.0f,  1.0f },  { 0.f, 1.f } },
        { { 1.0f,  1.0f },  { 1.f, 1.f } },
        { { 1.0f,  0.0f },  { 1.f, 0.f } },

        { { 0.0f,  1.0f },  { 0.f, 1.f } },
        { { 1.0f,  0.0f },  { 1.f, 0.f } },
        { { 0.0f,  0.0f },  { 0.f, 0.f } },
    };
    // clang-format on

    vertices_ = [device_ newBufferWithBytes:rawVertices length:sizeof(rawVertices) options:MTLResourceStorageModeShared];
    vertexCount_ = sizeof(rawVertices) / sizeof(VantageVertex);

    {
        uint32_t fillContent = 0xffffffff;

        MTLTextureDescriptor * textureDescriptor = [[MTLTextureDescriptor alloc] init];
        textureDescriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
        textureDescriptor.width = 1;
        textureDescriptor.height = 1;

        metalFillImage_ = [device_ newTextureWithDescriptor:textureDescriptor];
        MTLRegion region = MTLRegionMake2D(0, 0, textureDescriptor.width, textureDescriptor.height);
        [metalFillImage_ replaceRegion:region mipmapLevel:0 slice:0 withBytes:&fillContent bytesPerRow:4 bytesPerImage:0];
    }

    {
        MTLTextureDescriptor * textureDescriptor = [[MTLTextureDescriptor alloc] init];
        textureDescriptor.pixelFormat = MTLPixelFormatRGBA16Unorm;
        textureDescriptor.width = V->imageFont_->width;
        textureDescriptor.height = V->imageFont_->height;

        metalFontImage_ = [device_ newTextureWithDescriptor:textureDescriptor];
        MTLRegion region = MTLRegionMake2D(0, 0, textureDescriptor.width, textureDescriptor.height);
        [metalFontImage_ replaceRegion:region
                           mipmapLevel:0
                                 slice:0
                             withBytes:V->imageFont_->pixels
                           bytesPerRow:8 * V->imageFont_->width
                         bytesPerImage:0];
    }

    [self _prepareNotifications];
    return self;
}

- (void)_prepareNotifications
{
    NSNotificationCenter * center = [NSNotificationCenter defaultCenter];
    [center addObserver:self selector:@selector(openDocument:) name:@"openDocument" object:nil];
    [center addObserver:self selector:@selector(resetImagePosition:) name:@"resetImagePosition" object:nil];
    [center addObserver:self selector:@selector(previousImage:) name:@"previousImage" object:nil];
    [center addObserver:self selector:@selector(nextImage:) name:@"nextImage" object:nil];
    [center addObserver:self selector:@selector(toggleSRGB:) name:@"toggleSRGB" object:nil];
    [center addObserver:self selector:@selector(showOverlay:) name:@"showOverlay" object:nil];
    [center addObserver:self selector:@selector(hideOverlay:) name:@"hideOverlay" object:nil];
    [center addObserver:self selector:@selector(diffCurrentImageAgainst:) name:@"diffCurrentImageAgainst" object:nil];
    [center addObserver:self selector:@selector(showImage1:) name:@"showImage1" object:nil];
    [center addObserver:self selector:@selector(showImage2:) name:@"showImage2" object:nil];
    [center addObserver:self selector:@selector(showDiff:) name:@"showDiff" object:nil];
    [center addObserver:self selector:@selector(adjustThresholdM1:) name:@"adjustThresholdM1" object:nil];
    [center addObserver:self selector:@selector(adjustThresholdM5:) name:@"adjustThresholdM5" object:nil];
    [center addObserver:self selector:@selector(adjustThresholdM50:) name:@"adjustThresholdM50" object:nil];
    [center addObserver:self selector:@selector(adjustThresholdM500:) name:@"adjustThresholdM500" object:nil];
    [center addObserver:self selector:@selector(diffIntensityOriginal:) name:@"diffIntensityOriginal" object:nil];
    [center addObserver:self selector:@selector(diffIntensityBright:) name:@"diffIntensityBright" object:nil];
    [center addObserver:self selector:@selector(diffIntensityDiffOnly:) name:@"diffIntensityDiffOnly" object:nil];
    [center addObserver:self selector:@selector(adjustThresholdP1:) name:@"adjustThresholdP1" object:nil];
    [center addObserver:self selector:@selector(adjustThresholdP5:) name:@"adjustThresholdP5" object:nil];
    [center addObserver:self selector:@selector(adjustThresholdP50:) name:@"adjustThresholdP50" object:nil];
    [center addObserver:self selector:@selector(adjustThresholdP500:) name:@"adjustThresholdP500" object:nil];
    [center addObserver:self selector:@selector(refresh:) name:@"refresh" object:nil];
    [center addObserver:self selector:@selector(sequenceRewind20:) name:@"sequenceRewind20" object:nil];
    [center addObserver:self selector:@selector(sequenceAdvance20:) name:@"sequenceAdvance20" object:nil];
    [center addObserver:self selector:@selector(sequenceRewind5:) name:@"sequenceRewind5" object:nil];
    [center addObserver:self selector:@selector(sequenceAdvance5:) name:@"sequenceAdvance5" object:nil];
    [center addObserver:self selector:@selector(sequencePreviousFrame:) name:@"sequencePreviousFrame" object:nil];
    [center addObserver:self selector:@selector(sequenceNextFrame:) name:@"sequenceNextFrame" object:nil];
    [center addObserver:self selector:@selector(sequenceGotoFrame:) name:@"sequenceGotoFrame" object:nil];
}

- (void)openDocument:(NSNotification *)notification
{
    NSOpenPanel * openDlg = [NSOpenPanel openPanel];
    [openDlg setCanChooseFiles:YES];
    [openDlg setAllowsMultipleSelection:YES];
    [openDlg setCanChooseDirectories:NO];
    if ([openDlg runModal] == NSModalResponseOK) {
        NSArray * urls = [openDlg URLs];
        if ([urls count] > 0) {
            NSString * fileScheme = @"file";
            NSURL * url = [urls objectAtIndex:0];
            if ([url.scheme isEqualToString:fileScheme]) {
                [self loadImage:url.path diff:nil];
            }
        }
    }
}

- (void)resetImagePosition:(NSNotification *)notification
{
    vantageResetImagePos(V);
}

- (void)previousImage:(NSNotification *)notification
{
    vantageLoad(V, -1);
}

- (void)nextImage:(NSNotification *)notification
{
    vantageLoad(V, 1);
}

- (void)toggleSRGB:(NSNotification *)notification
{
    vantageToggleSrgbHighlight(V);
}

- (void)showOverlay:(NSNotification *)notification
{
    vantageKickOverlay(V);
}

- (void)hideOverlay:(NSNotification *)notification
{
    vantageKillOverlay(V);
}

- (void)diffCurrentImageAgainst:(NSNotification *)notification
{
    NSOpenPanel * openDlg = [NSOpenPanel openPanel];
    [openDlg setCanChooseFiles:YES];
    [openDlg setAllowsMultipleSelection:YES];
    [openDlg setCanChooseDirectories:NO];
    if ([openDlg runModal] == NSModalResponseOK) {
        NSArray * urls = [openDlg URLs];
        if ([urls count] > 0) {
            NSString * fileScheme = @"file";
            NSURL * url = [urls objectAtIndex:0];
            if ([url.scheme isEqualToString:fileScheme]) {
                char * currentFilename = NULL;
                dsCopy(&currentFilename, V->filenames_[V->imageFileIndex_]);
                vantageLoadDiff(V, currentFilename, [url.path UTF8String]);
                dsDestroy(&currentFilename);
            }
        }
    }
}

- (void)showImage1:(NSNotification *)notification
{
    vantageSetDiffMode(V, DIFFMODE_SHOW1);
}

- (void)showImage2:(NSNotification *)notification
{
    vantageSetDiffMode(V, DIFFMODE_SHOW2);
}

- (void)showDiff:(NSNotification *)notification
{
    vantageSetDiffMode(V, DIFFMODE_SHOWDIFF);
}

- (void)adjustThresholdM1:(NSNotification *)notification
{
    vantageAdjustThreshold(V, -1);
}

- (void)adjustThresholdM5:(NSNotification *)notification
{
    vantageAdjustThreshold(V, -5);
}

- (void)adjustThresholdM50:(NSNotification *)notification
{
    vantageAdjustThreshold(V, -50);
}

- (void)adjustThresholdM500:(NSNotification *)notification
{
    vantageAdjustThreshold(V, -500);
}

- (void)diffIntensityOriginal:(NSNotification *)notification
{
    vantageSetDiffIntensity(V, DIFFINTENSITY_ORIGINAL);
}

- (void)diffIntensityBright:(NSNotification *)notification
{
    vantageSetDiffIntensity(V, DIFFINTENSITY_BRIGHT);
}

- (void)diffIntensityDiffOnly:(NSNotification *)notification
{
    vantageSetDiffIntensity(V, DIFFINTENSITY_DIFFONLY);
}

- (void)adjustThresholdP1:(NSNotification *)notification
{
    vantageAdjustThreshold(V, 1);
}

- (void)adjustThresholdP5:(NSNotification *)notification
{
    vantageAdjustThreshold(V, 5);
}

- (void)adjustThresholdP50:(NSNotification *)notification
{
    vantageAdjustThreshold(V, 50);
}

- (void)adjustThresholdP500:(NSNotification *)notification
{
    vantageAdjustThreshold(V, 500);
}

- (void)refresh:(NSNotification *)notification
{
    vantageRefresh(V);
}

- (void)sequenceRewind20:(NSNotification *)notification
{
    vantageSetVideoFrameIndexPercentOffset(V, -20);
}

- (void)sequenceAdvance20:(NSNotification *)notification
{
    vantageSetVideoFrameIndexPercentOffset(V, 20);
}

- (void)sequenceRewind5:(NSNotification *)notification
{
    vantageSetVideoFrameIndexPercentOffset(V, -5);
}

- (void)sequenceAdvance5:(NSNotification *)notification
{
    vantageSetVideoFrameIndexPercentOffset(V, 5);
}

- (void)sequencePreviousFrame:(NSNotification *)notification
{
    vantageSetVideoFrameIndex(V, V->imageVideoFrameIndex_ - 1);
}

- (void)sequenceNextFrame:(NSNotification *)notification
{
    vantageSetVideoFrameIndex(V, V->imageVideoFrameIndex_ + 1);
}

- (void)sequenceGotoFrame:(NSNotification *)notification
{
    int lastFrameIndex = V->imageVideoFrameCount_ - 1;
    if (lastFrameIndex < 0) {
        lastFrameIndex = 0;
    }
    char * informativeTextString = NULL;
    dsConcatf(&informativeTextString, "(0-based, Last Frame Index: %d)", lastFrameIndex);

    char * defaultValueString = NULL;
    dsConcatf(&defaultValueString, "%d", V->imageVideoFrameIndex_);

    NSAlert * alert = [NSAlert new];
    alert.messageText = @"Goto Frame Index:";
    alert.informativeText = [[NSString alloc] initWithUTF8String:informativeTextString];
    alert.alertStyle = NSAlertStyleInformational;
    alert.delegate = nil;
    [alert addButtonWithTitle:@"Goto Frame"];
    [alert addButtonWithTitle:@"Cancel"];

    NSTextField * input = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 200, 24)];
    [input setStringValue:[[NSString alloc] initWithUTF8String:defaultValueString]];
    [alert setAccessoryView:input];

    NSInteger button = [alert runModal];
    if (button == NSAlertFirstButtonReturn) {
        int typedFrameIndex = atoi([[input stringValue] UTF8String]);
        vantageSetVideoFrameIndex(V, typedFrameIndex);
    }

    dsDestroy(&informativeTextString);
    dsDestroy(&defaultValueString);
}

- (void)openFile:(NSNotification *)notification
{
    NSDictionary * userInfo = notification.userInfo;
    if (userInfo) {
        NSString * filename = userInfo[@"filename"];
        [self loadImage:filename diff:nil];
    }
}

- (void)loadImage:(nonnull NSString *)path diff:(nullable NSString *)diffPath
{
    if (diffPath == nil) {
        vantageFileListClear(V);

        char * cpath = NULL;
        dsCopy(&cpath, [path UTF8String]);
        char * lastSlash = strrchr(cpath, '/');
        if (lastSlash != NULL) {
            *lastSlash = 0;
            char * cdir = NULL;
            dsCopy(&cdir, cpath);
            *lastSlash = '/';
            NSString * nsDir = [NSString stringWithUTF8String:cdir];
            NSArray * files = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:nsDir error:NULL];
            int fileCount = [files count];
            if (fileCount > 0) {
                NSArray * sortedFiles = [files sortedArrayUsingSelector:@selector(caseInsensitiveCompare:)];

                char * fullFilename = NULL;
                for (int i = 0; i < fileCount; ++i) {
                    NSString * filename = [sortedFiles objectAtIndex:i];
                    dsCopy(&fullFilename, cdir);
                    dsConcat(&fullFilename, "/");
                    dsConcat(&fullFilename, [filename UTF8String]);
                    if (vantageIsImageFile(fullFilename)) {
                        vantageFileListAppend(V, fullFilename);
                    }
                }
                dsDestroy(&fullFilename);
            }
            dsDestroy(&cdir);
        }
        if (daSize(&V->filenames_) > 1) {
            // Find our original path in there
            V->imageFileIndex_ = -1;
            for (int i = 0; i < daSize(&V->filenames_); ++i) {
                if (!strcmp(V->filenames_[i], cpath)) {
                    V->imageFileIndex_ = i;
                    break;
                }
            }
            if (V->imageFileIndex_ == -1) {
                // Somehow the original file wasn't found, tack it onto the end of the list
                vantageFileListAppend(V, cpath);
                V->imageFileIndex_ = (int)(daSize(&V->filenames_) - 1);
            }
        } else {
            // Give up and just put the original path in there
            vantageFileListAppend(V, [path UTF8String]);
        }
        dsDestroy(&cpath);

        vantageLoad(V, 0);
    } else {
        vantageLoadDiff(V, [path UTF8String], [diffPath UTF8String]);
    }
}

- (void)mouseDown:(nonnull NSEvent *)event
{
    NSPoint eventLocation = [event locationInWindow];
    int x = (int)eventLocation.x;
    int y = windowSize_.height - (int)eventLocation.y;
    vantageMouseLeftDown(V, x, y);
}

- (void)mouseUp:(nonnull NSEvent *)event
{
    NSPoint eventLocation = [event locationInWindow];
    int x = (int)eventLocation.x;
    int y = windowSize_.height - (int)eventLocation.y;
    vantageMouseLeftUp(V, x, y);

    NSInteger clickCount = [event clickCount];
    if (clickCount > 1) {
        vantageMouseLeftDoubleClick(V, x, y);
    }
}

- (void)mouseDragged:(nonnull NSEvent *)event
{
    NSPoint eventLocation = [event locationInWindow];
    int x = (int)eventLocation.x;
    int y = windowSize_.height - (int)eventLocation.y;
    vantageMouseMove(V, x, y);
}

- (void)rightMouseDown:(nonnull NSEvent *)event
{
    NSPoint eventLocation = [event locationInWindow];
    int x = (int)eventLocation.x;
    int y = windowSize_.height - (int)eventLocation.y;
    vantageMouseSetPos(V, x, y);
}

- (void)rightMouseDragged:(nonnull NSEvent *)event
{
    NSPoint eventLocation = [event locationInWindow];
    int x = (int)eventLocation.x;
    int y = windowSize_.height - (int)eventLocation.y;
    vantageMouseSetPos(V, x, y);
}

- (void)scrollWheel:(nonnull NSEvent *)event
{
    NSPoint eventLocation = [event locationInWindow];
    int x = (int)eventLocation.x;
    int y = windowSize_.height - (int)eventLocation.y;
    CGFloat delta = [event deltaY];
    // NSLog(@"delta %f", delta);
    vantageMouseWheel(V, x, y, delta);
}

- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size
{
    windowSize_ = view_.bounds.size;
    vantagePlatformSetSize(V, windowSize_.width, windowSize_.height); // lies
    // NSLog(@"Size changed to %d x %d", (int)windowSize_.width, (int)windowSize_.height);
}

- (void)drawInMTKView:(nonnull MTKView *)view
{
    // --------------------------------------------------------------------------------------------
    // Update Vantage

    if (V->imageDirty_) {
        V->imageDirty_ = 0;

        metalPreparedImage_ = nil;
        if (V->preparedImage_) {
            MTLTextureDescriptor * textureDescriptor = [[MTLTextureDescriptor alloc] init];
            textureDescriptor.pixelFormat = MTLPixelFormatRGBA16Unorm;
            textureDescriptor.width = V->preparedImage_->width;
            textureDescriptor.height = V->preparedImage_->height;

            metalPreparedImage_ = [device_ newTextureWithDescriptor:textureDescriptor];
            MTLRegion region = MTLRegionMake2D(0, 0, textureDescriptor.width, textureDescriptor.height);
            [metalPreparedImage_ replaceRegion:region
                                   mipmapLevel:0
                                         slice:0
                                     withBytes:V->preparedImage_->pixels
                                   bytesPerRow:8 * V->preparedImage_->width
                                 bytesPerImage:0];
        }
    }

    vantageRender(V);

    // --------------------------------------------------------------------------------------------
    // Render

    dispatch_semaphore_wait(inFlightSemaphore_, DISPATCH_TIME_FOREVER);
    id<MTLCommandBuffer> commandBuffer = [commandQueue_ commandBuffer];
    commandBuffer.label = @"VantageRender";
    __block dispatch_semaphore_t block_sema = inFlightSemaphore_;
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
      dispatch_semaphore_signal(block_sema);
    }];

    MTLRenderPassDescriptor * renderPassDescriptor = view.currentRenderPassDescriptor;
    if (renderPassDescriptor != nil) {
        id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
        renderEncoder.label = @"VantageRenderEncoder";

        int blitCount = daSize(&V->blits_);
        for (int blitIndex = 0; blitIndex < blitCount; ++blitIndex) {
            Blit * blit = &V->blits_[blitIndex];
            if (blit->mode == BM_IMAGE) {
                if (metalPreparedImage_ == nil) {
                    continue;
                }
            }

            id<MTLTexture> texture = nil;
            switch (blit->mode) {
                case BM_IMAGE:
                    texture = metalPreparedImage_;
                    break;
                case BM_TEXT:
                    texture = metalFontImage_;
                    break;
                case BM_FILL:
                    texture = metalFillImage_;
                    break;
            }

            Uniforms uniforms;
            uniforms.color.x = blit->color.r;
            uniforms.color.y = blit->color.g;
            uniforms.color.z = blit->color.b;
            uniforms.color.w = blit->color.a;
            uniforms.uvScale.x = blit->sw;
            uniforms.uvScale.y = blit->sh;
            uniforms.uvOffset.x = blit->sx;
            uniforms.uvOffset.y = blit->sy;
            uniforms.vertexScale.x = blit->dw;
            uniforms.vertexScale.y = blit->dh;
            uniforms.vertexOffset.x = blit->dx;
            uniforms.vertexOffset.y = blit->dy;
            uniforms.overrange = 10000.0f / (float)MACOS_SDR_WHITE_NITS;

            [renderEncoder setRenderPipelineState:pipelineState_];
            [renderEncoder setCullMode:MTLCullModeNone];
            [renderEncoder setVertexBuffer:vertices_ offset:0 atIndex:VantageVertexInputIndexVertices];
            [renderEncoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:VantageVertexInputIndexUniforms];
            [renderEncoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:VantageVertexInputIndexUniforms];
            [renderEncoder setFragmentTexture:texture atIndex:VantageTextureIndexMain];
            [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:vertexCount_];
        }

        [renderEncoder endEncoding];
        [commandBuffer presentDrawable:view.currentDrawable];
    }

    [commandBuffer commit];

    // Awful hack
    if (!windowTitleSet_) {
        NSString * title = [[NSString alloc] initWithUTF8String:VANTAGE_WINDOW_TITLE];
        view_.window.title = title;
        windowTitleSet_ = true;
    }
}

@end
