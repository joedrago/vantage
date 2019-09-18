// ---------------------------------------------------------------------------
//                         Copyright Joe Drago 2019.
//         Distributed under the Boost Software License, Version 1.0.
//            (See accompanying file LICENSE_1_0.txt or copy at
//                  http://www.boost.org/LICENSE_1_0.txt)
// ---------------------------------------------------------------------------

Texture2D texture0: register ( t0 );
SamplerState sampler0: register ( s0 );

cbuffer cb0: register (b0)
{
    float4x4 transform;
    float4 color;
    float4 texOffsetScale;
};

struct VS_INPUT
{
    float4 Pos: POSITION;
    float2 Tex: TEXCOORD0;
};

struct PS_INPUT
{
    float4 Pos: SV_POSITION;
    float2 Tex: TEXCOORD0;
};

PS_INPUT VS(VS_INPUT input)
{
    PS_INPUT output = (PS_INPUT)0;
    output.Pos = mul(transform, input.Pos);
    output.Tex = input.Tex;
    return output;
}

float4 PS(PS_INPUT input) : SV_Target
{
    return texture0.Sample(sampler0, (input.Tex * texOffsetScale.zw) + texOffsetScale.xy) * color;
}
