#pragma clang diagnostic ignored "-Wmissing-prototypes"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

// Helper function for image synchronization
template <typename ImageT>
void imageBarrier(ImageT image) { image.fence(); }

// Constants passed through push constants
struct ComputeConstants {
    float4 multiply;
    uint2 resolution;
};

// Descriptor set 0 resources
struct TextureResources {
    constant float* _padding0 [[id(0)]];  // Maintain padding/alignment
    texture2d<float> blueNoiseTexture [[id(1)]];
    sampler textureSampler [[id(2)]];
};

// Descriptor set 1 resources
struct RenderTargetResources {
    // Maintain padding slots for alignment/compatibility
    constant float* _padding0 [[id(0)]];
    constant float* _padding1 [[id(1)]];
    constant float* _padding2 [[id(2)]];
    constant float* _padding3 [[id(3)]];
    constant float* _padding4 [[id(4)]];
    constant float* _padding5 [[id(5)]];
    constant float* _padding6 [[id(6)]];
    constant float* _padding7 [[id(7)]];
    constant float* _padding8 [[id(8)]];
    constant float* _padding9 [[id(9)]];
    constant float* _padding10 [[id(10)]];
    constant float* _padding11 [[id(11)]];
    constant float* _padding12 [[id(12)]];
    constant float* _padding13 [[id(13)]];
    constant float* _padding14 [[id(14)]];
    constant float* _padding15 [[id(15)]];
    texture2d<float, access::read_write> targetTexture [[id(16)]];
};

kernel void CSMain(
    constant TextureResources& textures [[buffer(0)]],
    constant RenderTargetResources& renderTargets [[buffer(1)]],
    constant ComputeConstants& constants [[buffer(8)]],
    uint3 globalID [[thread_position_in_grid]]
) {
    // Early exit if outside resolution bounds
    if (any(globalID.xy >= constants.resolution)) {
        return;
    }
    
    // Ensure memory coherency
    imageBarrier(renderTargets.targetTexture);
    
    // Calculate UV coordinates for blue noise sampling
    float2 uv = float2(globalID.xy) / float2(constants.resolution);
    
    // Sample blue noise and apply multiplication
    float4 blueNoise = float4(
        textures.blueNoiseTexture.sample(textures.textureSampler, uv, level(0.0)).xyz,
        1.0
    );
    
    // Read current value, apply modification, and write back
    uint2 pixelCoord = uint2(globalID.xy);
    float4 currentValue = renderTargets.targetTexture.read(pixelCoord);
    float4 newValue = currentValue * (blueNoise * constants.multiply);
    renderTargets.targetTexture.write(newValue, pixelCoord);
}

