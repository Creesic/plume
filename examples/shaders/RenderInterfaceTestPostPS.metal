#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct TextureResources {
    constant float* _padding0 [[id(0)]];  // Maintain padding/alignment
    texture2d<float> sourceTexture [[id(1)]];
    sampler textureSampler [[id(2)]];
};

struct PixelOutput {
    float4 color [[color(0)]];
};

struct PixelInput {
    float2 texCoord [[user(locn0)]];
};

fragment PixelOutput PSMain(
    PixelInput input [[stage_in]],
    constant TextureResources& resources [[buffer(0)]]
) {
    PixelOutput output;
    
    // Sample from source texture and output with alpha = 1
    float3 sampledColor = resources.sourceTexture
        .sample(resources.textureSampler, input.texCoord, level(0.0))
        .xyz;
    
    output.color = float4(sampledColor, 1.0);
    return output;
}

