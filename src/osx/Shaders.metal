// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

#import "ShaderTypes.h"

typedef struct
{
    float4 pos [[position]];
    float2 uv;
} RasterizerData;

vertex RasterizerData vertexShader(uint vertexID [[vertex_id]],
                                   constant VantageVertex * vertexArray [[buffer(VantageVertexInputIndexVertices)]],
                                   constant Uniforms * uniforms [[buffer(VantageVertexInputIndexUniforms)]])

{
    RasterizerData out;

    float2 rawPos = vertexArray[vertexID].pos.xy * uniforms->vertexScale + uniforms->vertexOffset;
    float4 pos = vector_float4(rawPos.x, rawPos.y, 0.0, 1.0);

    // transform
    pos.x = (pos.x * 2.0) - 1.0;
    pos.y = (pos.y * -2.0) + 1.0;

    out.pos = pos; //vector_float4((pixelSpacePosition.x * 2.0) - 1.0, (pixelSpacePosition.y * -2.0) + 1.0, 0.0, 1.0);
    out.uv = vertexArray[vertexID].uv;

    return out;
}

fragment float4 fragmentShader(RasterizerData in [[stage_in]],
                               texture2d<half> colorTexture [[texture(VantageTextureIndexMain)]],
                               constant Uniforms * uniforms [[buffer(VantageVertexInputIndexUniforms)]])
{
    constexpr sampler pointSampler(mag_filter::nearest, min_filter::nearest);
    constexpr sampler linearSampler(mag_filter::linear, min_filter::linear);


    float2 uv = in.uv * uniforms->uvScale + uniforms->uvOffset;
    float4 colorSample;
    if(uniforms->linear == 1) {
        colorSample = float4(colorTexture.sample(linearSampler, uv)) * uniforms->color;
    } else {
        colorSample = float4(colorTexture.sample(pointSampler, uv)) * uniforms->color;
    }
    colorSample.rgb *= uniforms->overrange;
    return float4(colorSample);
}
