#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct PushConstants
{
    float4 colorAdd;
    uint textureIndex;
};

struct FragmentOutput
{
    float4 color [[color(0)]];
};

struct FragmentInput
{
    float2 texCoord [[user(locn0)]];
};

fragment FragmentOutput PSMain(FragmentInput in [[stage_in]], sampler textureSampler [[sampler(1)]], texture2d_array<float> textures [[texture(2)]], constant PushConstants& constants [[buffer(8)]])
{
    FragmentOutput out = {};
    uint textureIdx = constants.textureIndex;
    out.color = float4(textures.sample(textureSampler, in.texCoord, textureIdx, level(0.0)).xyz, 1.0) + constants.colorAdd;
    return out;
} 