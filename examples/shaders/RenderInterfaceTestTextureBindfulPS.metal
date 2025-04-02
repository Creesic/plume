#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct PushConstants
{
    float4 colorAdd;
};

struct FragmentOutput
{
    float4 color [[color(0)]];
};

struct FragmentInput
{
    float2 texCoord [[user(locn0)]];
};

fragment FragmentOutput PSMain(FragmentInput in [[stage_in]], sampler textureSampler [[sampler(1)]], texture2d<float> colorTexture [[texture(2)]], constant PushConstants& constants [[buffer(8)]])
{
    FragmentOutput out = {};
    out.color = float4(colorTexture.sample(textureSampler, in.texCoord, level(0.0)).xyz, 1.0) + constants.colorAdd;
    return out;
} 