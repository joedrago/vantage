// ---------------------------------------------------------------------------
//                         Copyright Joe Drago 2019.
//         Distributed under the Boost Software License, Version 1.0.
//            (See accompanying file LICENSE_1_0.txt or copy at
//                  http://www.boost.org/LICENSE_1_0.txt)
// ---------------------------------------------------------------------------

#import <ModelIO/ModelIO.h>
#import <simd/simd.h>

#import "Renderer.h"
#import "ShaderTypes.h"
#import "VantageView.h"

#import "colorist/colorist.h"

static const NSUInteger kMaxBuffersInFlight = 3;

static const size_t kAlignedUniformsSize = (sizeof(Uniforms) & ~0xFF) + 0x100;

@implementation Renderer {
    dispatch_semaphore_t _inFlightSemaphore;
    id<MTLDevice> _device;
    id<MTLCommandQueue> _commandQueue;

    id<MTLBuffer> _dynamicUniformBuffer;
    id<MTLRenderPipelineState> _pipelineState;
    id<MTLDepthStencilState> _depthState;
    id<MTLTexture> _mtlImage;
    MTLVertexDescriptor * _mtlVertexDescriptor;

    uint32_t _uniformBufferOffset;

    uint8_t _uniformBufferIndex;

    void * _uniformBufferAddress;

    matrix_float4x4 _projectionMatrix;

    CGSize _dimensions;

    MTKMesh * _mesh;
    VantageView * _view;

    bool _hdrActive;
    bool _srgbHighlight;
    NSString * _imagePath;
}

// - (void)mouseDragged:(NSEvent *)event
// {
//     NSPoint eventLocation = [event mouseLocation];
// }

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)view
{
    self = [super init];
    if (self) {
        _device = view.device;
        _inFlightSemaphore = dispatch_semaphore_create(kMaxBuffersInFlight);
        _view = (VantageView *)view;
        _hdrActive = false;
        _srgbHighlight = false;
        _imagePath = nil;
        [self _loadMetalWithView:view];
        [self _loadAssets];
        [self _prepareNotifications];
    }

    return self;
}

- (void)_loadMetalWithView:(nonnull MTKView *)view
{
    /// Load Metal state objects and initalize renderer dependent view properties

    CAMetalLayer * metalLayer = (CAMetalLayer *)view.layer;
    metalLayer.wantsExtendedDynamicRangeContent = YES;
    metalLayer.pixelFormat = MTLPixelFormatRGBA16Float;

    view.depthStencilPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    view.colorPixelFormat = MTLPixelFormatRGBA16Float; //MTLPixelFormatBGRA8Unorm_sRGB;
    view.sampleCount = 1;

    _mtlVertexDescriptor = [[MTLVertexDescriptor alloc] init];

    _mtlVertexDescriptor.attributes[VertexAttributePosition].format = MTLVertexFormatFloat3;
    _mtlVertexDescriptor.attributes[VertexAttributePosition].offset = 0;
    _mtlVertexDescriptor.attributes[VertexAttributePosition].bufferIndex = BufferIndexMeshPositions;

    _mtlVertexDescriptor.attributes[VertexAttributeTexcoord].format = MTLVertexFormatFloat2;
    _mtlVertexDescriptor.attributes[VertexAttributeTexcoord].offset = 0;
    _mtlVertexDescriptor.attributes[VertexAttributeTexcoord].bufferIndex = BufferIndexMeshGenerics;

    _mtlVertexDescriptor.layouts[BufferIndexMeshPositions].stride = 12;
    _mtlVertexDescriptor.layouts[BufferIndexMeshPositions].stepRate = 1;
    _mtlVertexDescriptor.layouts[BufferIndexMeshPositions].stepFunction = MTLVertexStepFunctionPerVertex;

    _mtlVertexDescriptor.layouts[BufferIndexMeshGenerics].stride = 8;
    _mtlVertexDescriptor.layouts[BufferIndexMeshGenerics].stepRate = 1;
    _mtlVertexDescriptor.layouts[BufferIndexMeshGenerics].stepFunction = MTLVertexStepFunctionPerVertex;

    id<MTLLibrary> defaultLibrary = [_device newDefaultLibrary];

    id<MTLFunction> vertexFunction = [defaultLibrary newFunctionWithName:@"vertexShader"];

    id<MTLFunction> fragmentFunction = [defaultLibrary newFunctionWithName:@"fragmentShader"];

    MTLRenderPipelineDescriptor * pipelineStateDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineStateDescriptor.label = @"MyPipeline";
    pipelineStateDescriptor.sampleCount = view.sampleCount;
    pipelineStateDescriptor.vertexFunction = vertexFunction;
    pipelineStateDescriptor.fragmentFunction = fragmentFunction;
    pipelineStateDescriptor.vertexDescriptor = _mtlVertexDescriptor;
    pipelineStateDescriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat;
    pipelineStateDescriptor.depthAttachmentPixelFormat = view.depthStencilPixelFormat;
    pipelineStateDescriptor.stencilAttachmentPixelFormat = view.depthStencilPixelFormat;

    NSError * error = NULL;
    _pipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineStateDescriptor error:&error];
    if (!_pipelineState) {
        NSLog(@"Failed to created pipeline state, error %@", error);
    }

    MTLDepthStencilDescriptor * depthStateDesc = [[MTLDepthStencilDescriptor alloc] init];
    depthStateDesc.depthCompareFunction = MTLCompareFunctionAlways; //MTLCompareFunctionLess;
    depthStateDesc.depthWriteEnabled = NO;
    _depthState = [_device newDepthStencilStateWithDescriptor:depthStateDesc];

    NSUInteger uniformBufferSize = kAlignedUniformsSize * kMaxBuffersInFlight;

    _dynamicUniformBuffer = [_device newBufferWithLength:uniformBufferSize options:MTLResourceStorageModeShared];

    _dynamicUniformBuffer.label = @"UniformBuffer";

    _commandQueue = [_device newCommandQueue];

    [self _toggleHDR];
}

- (void)_reload
{
    if (_imagePath != nil) {
        [self loadImage:_imagePath diff:nil];
    }
    [self updateTitle];
}

- (void)_toggleHDR
{
    _hdrActive = !_hdrActive;

    CAMetalLayer * metalLayer = (CAMetalLayer *)_view.layer;
    CGColorSpaceRef colorspace;
    if (_hdrActive) {
        colorspace = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearITUR_2020);
    } else {
        colorspace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    }
    metalLayer.colorspace = colorspace;
    CGColorSpaceRelease(colorspace);

    [self _reload];
}

- (void)_loadAssets
{
    /// Load assets into metal objects

    NSError * error = NULL;

    MTKMeshBufferAllocator * metalAllocator = [[MTKMeshBufferAllocator alloc] initWithDevice:_device];

    MDLMesh * mdlMesh = [MDLMesh newBoxWithDimensions:(vector_float3) { 2.0f, 2.0f, 0.1f }
                                             segments:(vector_uint3) { 1, 1, 1 }
                                         geometryType:MDLGeometryTypeTriangles
                                        inwardNormals:NO
                                            allocator:metalAllocator];

    MDLVertexDescriptor * mdlVertexDescriptor = MTKModelIOVertexDescriptorFromMetal(_mtlVertexDescriptor);

    mdlVertexDescriptor.attributes[VertexAttributePosition].name = MDLVertexAttributePosition;
    mdlVertexDescriptor.attributes[VertexAttributeTexcoord].name = MDLVertexAttributeTextureCoordinate;

    mdlMesh.vertexDescriptor = mdlVertexDescriptor;

    _mesh = [[MTKMesh alloc] initWithMesh:mdlMesh device:_device error:&error];

    if (!_mesh || error) {
        NSLog(@"Error creating MetalKit mesh %@", error.localizedDescription);
    }

    _mtlImage = nil;
}

- (void)loadImage:(nonnull NSString *)path diff:(nullable NSString *)diffPath
{
    clContext * C = clContextCreate(NULL);
    C->defaultLuminance = 100; // It seems that Apple uses 100 nits as their gfx white

    clImage * loadedImage = clContextRead(C, [path UTF8String], NULL, NULL);

    if (loadedImage && _srgbHighlight) {
        clImageSRGBHighlightStats stats;
        clImage * highlight = clImageCreateSRGBHighlight(C, loadedImage, C->defaultLuminance, &stats, NULL, NULL);
        if (highlight) {
            clImageDestroy(C, loadedImage);
            loadedImage = highlight;
        }
    }

    if (loadedImage) {
        int sdrWhite = 80;

        clProfilePrimaries primaries;
        clProfileCurve curve;

        int dstLuminance = 10000;
        if (_hdrActive) {
            clContextGetStockPrimaries(C, "bt2020", &primaries);
            curve.type = CL_PCT_GAMMA;
            curve.implicitScale = 1.0f;
            curve.gamma = 1.0f;
            dstLuminance = 10000;
            // imageWhiteLevel_ = sdrWhite;
            // imageHDR_ = true;
        } else {
            clContextGetStockPrimaries(C, "bt709", &primaries);
            curve.type = CL_PCT_GAMMA;
            curve.implicitScale = 1.0f;
            curve.gamma = 2.2f;
            dstLuminance = sdrWhite;
            // imageWhiteLevel_ = sdrWhite;
            // imageHDR_ = false;
        }
        clProfile * profile = clProfileCreate(C, &primaries, &curve, dstLuminance, NULL);
        clImage * convertedImage = clImageConvert(C, loadedImage, C->params.jobs, 16, profile, CL_TONEMAP_AUTO);
        clProfileDestroy(C, profile);

        clImageDestroy(C, loadedImage);
        loadedImage = convertedImage;

        MTLTextureDescriptor * textureDescriptor = [[MTLTextureDescriptor alloc] init];

        // Indicate that each pixel has a blue, green, red, and alpha channel, where each channel is
        // an 8-bit unsigned normalized value (i.e. 0 maps to 0.0 and 255 maps to 1.0)
        textureDescriptor.pixelFormat = MTLPixelFormatRGBA16Unorm;

        // Set the pixel dimensions of the texture
        textureDescriptor.width = loadedImage->width;
        textureDescriptor.height = loadedImage->height;

        // Create the texture from the device by using the descriptor
        _mtlImage = [_device newTextureWithDescriptor:textureDescriptor];

        // if(!_mtlImage || error)
        // {
        //     NSLog(@"Error creating texture %@", error.localizedDescription);
        // }

        MTLRegion region = MTLRegionMake2D(0, 0, textureDescriptor.width, textureDescriptor.height);
        [_mtlImage replaceRegion:region
                     mipmapLevel:0
                           slice:0
                       withBytes:loadedImage->pixels
                     bytesPerRow:8 * loadedImage->width
                   bytesPerImage:0];

        clImageDestroy(C, loadedImage);

        _imagePath = [[NSString alloc] initWithString:path];
    } else {
        _mtlImage = nil;
        _imagePath = nil;
    }
    clContextDestroy(C);

    [self updateTitle];
}

- (void)updateTitle
{
    NSMutableString * newTitle = [[NSMutableString alloc] initWithUTF8String:"Vantage"];
    if (_imagePath) {
        [newTitle appendString:@" - "];
        [newTitle appendString:_imagePath];
    }
    if (_srgbHighlight) {
        [newTitle appendString:@" (SRGB Highlight @ 100 nits)"];
    } else {
        if (_hdrActive) {
            [newTitle appendString:@" (HDR Render)"];
        } else {
            [newTitle appendString:@" (SDR Render)"];
        }
    }
    _view.window.title = newTitle;
}

#if 0
- (bool)keyUp:(nonnull NSEvent *)event
{
    if ([event modifierFlags] & NSEventModifierFlagNumericPad) { // arrow keys have this mask
        NSString * theArrow = [event charactersIgnoringModifiers];
        unichar keyChar = 0;
        if ([theArrow length] == 0)
            return false;
        if ([theArrow length] == 1) {
            if (keyChar == NSUpArrowFunctionKey) {
                [self _toggleHDR];
                return true;
            }
        }
    }
    return false;
}
#endif

- (void)toggleHDR:(NSNotification *)notification
{
    [self _toggleHDR];
}

- (void)toggleSRGBHighlight:(NSNotification *)notification
{
    _srgbHighlight = !_srgbHighlight;
    [self _reload];
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

- (void)openFile:(NSNotification *)notification
{
    NSDictionary * userInfo = notification.userInfo;
    if (userInfo) {
        NSString * filename = userInfo[@"filename"];
        [self loadImage:filename diff:nil];
    }
}

- (void)_prepareNotifications
{
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(openDocument:) name:@"openDocument" object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(toggleHDR:) name:@"toggleHDR" object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(toggleSRGBHighlight:)
                                                 name:@"toggleSRGBHighlight"
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(openFile:) name:@"openFile" object:nil];
}

- (void)_updateDynamicBufferState
{
    /// Update the state of our uniform buffers before rendering

    _uniformBufferIndex = (_uniformBufferIndex + 1) % kMaxBuffersInFlight;

    _uniformBufferOffset = kAlignedUniformsSize * _uniformBufferIndex;

    _uniformBufferAddress = ((uint8_t *)_dynamicUniformBuffer.contents) + _uniformBufferOffset;
}

- (void)_updateGameState
{
    /// Update any game state before encoding renderint commands to our drawable

    Uniforms * uniforms = (Uniforms *)_uniformBufferAddress;

    uniforms->projectionMatrix = _projectionMatrix;

    if ((_mtlImage != nil) && (_dimensions.width > 0) && (_dimensions.height > 0)) {
        float viewAspectRatio = (float)_dimensions.width / (float)_dimensions.height;
        float imageAspectRatio = (float)_mtlImage.width / (float)_mtlImage.height;
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        if (viewAspectRatio < imageAspectRatio) {
            scaleY = viewAspectRatio / imageAspectRatio;
        } else {
            scaleX = imageAspectRatio / viewAspectRatio;
        }
        uniforms->modelViewMatrix = matrix4x4_2d(0.0, 0.0, scaleX, scaleY);
    } else {
        uniforms->modelViewMatrix = matrix4x4_2d(0.0, 0.0, 1.0, 1.0);
    }
    if (_hdrActive) {
        uniforms->overrange = 100.0f;
    } else {
        uniforms->overrange = 1.0f;
    }
}

- (void)drawInMTKView:(nonnull MTKView *)view
{
    /// Per frame updates here

    dispatch_semaphore_wait(_inFlightSemaphore, DISPATCH_TIME_FOREVER);

    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    commandBuffer.label = @"MyCommand";

    __block dispatch_semaphore_t block_sema = _inFlightSemaphore;
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
      dispatch_semaphore_signal(block_sema);
    }];

    [self _updateDynamicBufferState];

    [self _updateGameState];

    /// Delay getting the currentRenderPassDescriptor until we absolutely need it to avoid
    ///   holding onto the drawable and blocking the display pipeline any longer than necessary
    MTLRenderPassDescriptor * renderPassDescriptor = view.currentRenderPassDescriptor;

    if ((renderPassDescriptor != nil) && (_mtlImage != nil)) {
        /// Final pass rendering code here

        id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
        renderEncoder.label = @"MyRenderEncoder";

        [renderEncoder pushDebugGroup:@"DrawBox"];

        [renderEncoder setFrontFacingWinding:MTLWindingCounterClockwise];
        [renderEncoder setCullMode:MTLCullModeBack];
        [renderEncoder setRenderPipelineState:_pipelineState];
        [renderEncoder setDepthStencilState:_depthState];

        [renderEncoder setVertexBuffer:_dynamicUniformBuffer offset:_uniformBufferOffset atIndex:BufferIndexUniforms];

        [renderEncoder setFragmentBuffer:_dynamicUniformBuffer offset:_uniformBufferOffset atIndex:BufferIndexUniforms];

        for (NSUInteger bufferIndex = 0; bufferIndex < _mesh.vertexBuffers.count; bufferIndex++) {
            MTKMeshBuffer * vertexBuffer = _mesh.vertexBuffers[bufferIndex];
            if ((NSNull *)vertexBuffer != [NSNull null]) {
                [renderEncoder setVertexBuffer:vertexBuffer.buffer offset:vertexBuffer.offset atIndex:bufferIndex];
            }
        }

        [renderEncoder setFragmentTexture:_mtlImage atIndex:TextureIndexColor];

        for (MTKSubmesh * submesh in _mesh.submeshes) {
            [renderEncoder drawIndexedPrimitives:submesh.primitiveType
                                      indexCount:submesh.indexCount
                                       indexType:submesh.indexType
                                     indexBuffer:submesh.indexBuffer.buffer
                               indexBufferOffset:submesh.indexBuffer.offset];
        }

        [renderEncoder popDebugGroup];

        [renderEncoder endEncoding];

        [commandBuffer presentDrawable:view.currentDrawable];
    }

    [commandBuffer commit];
}

- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size
{
    /// Respond to drawable size or orientation changes here

    // float aspect = size.width / (float)size.height;
    // _projectionMatrix = matrix_ortho_right_hand(0.0f, 1280.0f, 720.0f, 0.0f, -1.0f, 1.0f);
    // _projectionMatrix = matrix_perspective_right_hand(65.0f * (M_PI / 180.0f), aspect, 0.1f, 100.0f);
    _projectionMatrix = matrix4x4_translation(0.0f, 0.0f, 0.0f);
    _dimensions = size;
}

#pragma mark Matrix Math Utilities

matrix_float4x4 matrix4x4_translation(float tx, float ty, float tz)
{
    return (matrix_float4x4) { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { tx, ty, tz, 1 } } };
}

matrix_float4x4 matrix4x4_2d(float tx, float ty, float sx, float sy)
{
    return (matrix_float4x4) { { { sx, 0, 0, 0 }, { 0, sy, 0, 0 }, { 0, 0, 1, 0 }, { tx * sx, ty * sy, 0, 1 } } };
}

#if 0
static matrix_float4x4 matrix4x4_rotation(float radians, vector_float3 axis)
{
    axis = vector_normalize(axis);
    float ct = cosf(radians);
    float st = sinf(radians);
    float ci = 1 - ct;
    float x = axis.x, y = axis.y, z = axis.z;

    return (matrix_float4x4) { { { ct + x * x * ci, y * x * ci + z * st, z * x * ci - y * st, 0 },
                                 { x * y * ci - z * st, ct + y * y * ci, z * y * ci + x * st, 0 },
                                 { x * z * ci + y * st, y * z * ci - x * st, ct + z * z * ci, 0 },
                                 { 0, 0, 0, 1 } } };
}

matrix_float4x4 matrix_perspective_right_hand(float fovyRadians, float aspect, float nearZ, float farZ)
{
    float ys = 1 / tanf(fovyRadians * 0.5);
    float xs = ys / aspect;
    float zs = farZ / (nearZ - farZ);

    return (matrix_float4x4) { { { xs, 0, 0, 0 }, { 0, ys, 0, 0 }, { 0, 0, zs, -1 }, { 0, 0, nearZ * zs, 0 } } };
}

matrix_float4x4 matrix_make_rows(float m00,
                                 float m10,
                                 float m20,
                                 float m30,
                                 float m01,
                                 float m11,
                                 float m21,
                                 float m31,
                                 float m02,
                                 float m12,
                                 float m22,
                                 float m32,
                                 float m03,
                                 float m13,
                                 float m23,
                                 float m33)
{
    return (matrix_float4x4) { { { m00, m01, m02, m03 }, // each line here provides column data
                                 { m10, m11, m12, m13 },
                                 { m20, m21, m22, m23 },
                                 { m30, m31, m32, m33 } } };
}

matrix_float4x4 matrix_ortho_right_hand(float left, float right, float bottom, float top, float nearZ, float farZ)
{
    return matrix_make_rows(2 / (right - left),
                            0,
                            0,
                            (left + right) / (left - right),
                            0,
                            2 / (top - bottom),
                            0,
                            (top + bottom) / (bottom - top),
                            0,
                            0,
                            -1 / (farZ - nearZ),
                            nearZ / (nearZ - farZ),
                            0,
                            0,
                            0,
                            1);
}

#endif

@end
