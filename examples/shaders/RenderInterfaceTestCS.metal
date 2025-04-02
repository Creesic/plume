#pragma clang diagnostic ignored "-Wmissing-prototypes"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

template <typename ImageT>
void imageFence(ImageT img) { img.fence(); }

struct PushConstants
{
    float4 multiply;
    uint2 resolution;
};

kernel void CSMain(texture2d<float> blueNoiseTexture [[texture(1)]], sampler noiseSampler [[sampler(2)]], texture2d<float, access::read_write> targetTexture [[texture(16)]], constant PushConstants& constants [[buffer(8)]], uint3 threadId [[thread_position_in_grid]])
{
    do
    {
        if (any(threadId.xy >= constants.resolution))
        {
            break;
        }
        imageFence(targetTexture);
        targetTexture.write(targetTexture.read(uint2(threadId.xy)) * (float4(blueNoiseTexture.sample(noiseSampler, (float2(threadId.xy) / float2(constants.resolution)), level(0.0)).xyz, 1.0) * constants.multiply), uint2(threadId.xy));
        break;
    } while(false);
} 