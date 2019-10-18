// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef ShaderTypes_h
#define ShaderTypes_h

#include <simd/simd.h>

typedef enum VantageVertexInputIndex
{
    VantageVertexInputIndexVertices = 0,
    VantageVertexInputIndexUniforms = 1,
} VantageVertexInputIndex;

typedef enum VantageTextureIndex
{
    VantageTextureIndexMain = 0,
} VantageTextureIndex;

typedef struct
{
    vector_float2 pos;
    vector_float2 uv;
} VantageVertex;

typedef struct
{
    vector_float4 color;
    vector_float2 vertexScale;
    vector_float2 vertexOffset;
    vector_float2 uvScale;
    vector_float2 uvOffset;
    float overrange;
    int linear;
} Uniforms;

#endif
