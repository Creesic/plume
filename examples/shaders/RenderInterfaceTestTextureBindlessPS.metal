#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct TextureConstants {
    float4 colorAdd;
    uint textureIndex;
};

struct TextureResources {
    constant float* _padding0 [[id(0)]];  // Maintain padding/alignment
    sampler textureSampler [[id(1)]];
    array<texture2d<float>, 2048> textureArray [[id(2)]];
};

struct PixelOutput {
    float4 color [[color(0)]];
};

struct PixelInput {
    float2 texCoord [[user(locn0)]];
};

fragment PixelOutput PSMain(
    PixelInput input [[stage_in]],
    constant TextureResources& resources [[buffer(0)]],
    constant TextureConstants& constants [[buffer(8)]]
) {
    PixelOutput output;
    
    // Sample from the texture array using the provided index
    float3 sampledColor = resources.textureArray[constants.textureIndex]
        .sample(resources.textureSampler, input.texCoord, level(0.0))
        .xyz;
    
    // Combine sampled color with the color offset
    output.color = float4(sampledColor, 1.0) + constants.colorAdd;
    
    return output;
}

