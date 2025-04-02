#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct spvDescriptorSetBuffer0
{
    constant float* _PadResource0 [[id(0)]];
    texture2d<float> gTexture [[id(1)]];
    sampler gSampler [[id(2)]];
};

struct FragmentOutput
{
    float4 color [[color(0)]];
};

struct FragmentInput
{
    float2 texCoord [[user(locn0)]];
};

fragment FragmentOutput PSMain(FragmentInput in [[stage_in]], 
                             constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]])
{
    FragmentOutput out = {};
    out.color = float4(spvDescriptorSet0.gTexture.sample(spvDescriptorSet0.gSampler, in.texCoord).xyz, 1.0);
    return out;
} 