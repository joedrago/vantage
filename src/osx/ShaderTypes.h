// ---------------------------------------------------------------------------
//                         Copyright Joe Drago 2019.
//         Distributed under the Boost Software License, Version 1.0.
//            (See accompanying file LICENSE_1_0.txt or copy at
//                  http://www.boost.org/LICENSE_1_0.txt)
// ---------------------------------------------------------------------------

#ifndef ShaderTypes_h
#define ShaderTypes_h

#ifdef __METAL_VERSION__
#define NS_ENUM(_type, _name) \
    enum _name : _type _name; \
    enum _name : _type
#define NSInteger metal::int32_t
#else
#import <Foundation/Foundation.h>
#endif

#include <simd/simd.h>

typedef NS_ENUM(NSInteger, BufferIndex) { BufferIndexMeshPositions = 0, BufferIndexMeshGenerics = 1, BufferIndexUniforms = 2 };

typedef NS_ENUM(NSInteger, VertexAttribute) {
    VertexAttributePosition = 0,
    VertexAttributeTexcoord = 1,
};

typedef NS_ENUM(NSInteger, TextureIndex) {
    TextureIndexColor = 0,
};

typedef struct
{
    matrix_float4x4 projectionMatrix;
    matrix_float4x4 modelViewMatrix;
    float overrange;
} Uniforms;

#endif /* ShaderTypes_h */
