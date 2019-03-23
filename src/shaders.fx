Texture2D texture0: register ( t0 );
SamplerState sampler0: register ( s0 );

cbuffer cb0: register (b0)
{
    float4x4 transform;
    float4 params;
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

// ST.2084 (PQ)
float3 PQ_OETF(float3 color)
{
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;

    float3 Lm1 = pow(color, m1);
    float3 X = (c1 + c2 * Lm1) / (1 + c3 * Lm1);
    float3 E = pow(X, m2);
    return E;
}

float4 PS(PS_INPUT input) : SV_Target
{
    const float sdrWhiteLevel = 300.0;
    bool hdrActive = (params[0] > 0);
    bool forceSDR = (params[1] > 0);
    bool tonemap = (params[2] > 0);

    // This fragment shader assumes the input data is BT.2020, 10000 nits, 2.2 gamma

    float4 sampledColor = texture0.Sample(sampler0, input.Tex);
    return sampledColor;

    // float4 linearColor = pow(abs(sampledColor), 2.2);
    // float3 outColor = linearColor.rgb;

    // if (!hdrActive || forceSDR) {
    //     // Crush it down to BT.709
    //     float3x3 bt2020_to_bt709 = {
    //         1.6604910021, -0.5876411388, -0.0728498633,
    //         -0.1245504745, 1.1328998971, -0.0083494226,
    //         -0.0181507634, -0.1005788980, 1.1187296614
    //     };
    //     outColor = mul(bt2020_to_bt709, outColor);
    //     outColor = max(outColor, 0); // remove negative channels

    //     // Scale it up to SDR output levels
    //     outColor *= (10000 / sdrWhiteLevel);
    //     if (tonemap) {
    //         // Tonemap (Reinhard): x / (x+1)
    //         outColor = outColor / (outColor + 1);
    //     }
    //     outColor = clamp(outColor, 0, 1);

    //     if (hdrActive) {
    //         // Scale it back for display purposes
    //         outColor *= (sdrWhiteLevel / 10000);
    //     }
    // }

    // // Clamp
    // outColor = clamp(outColor, 0, 1);

    // outColor = PQ_OETF(outColor);
    // return float4(outColor.r, outColor.g, outColor.b, sampledColor.a);
}
