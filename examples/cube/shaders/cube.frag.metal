#include <metal_stdlib>
using namespace metal;

// Cube texture fragment shader
// Samples from a cubemap texture

struct PSInput {
    float4 position [[position]];
    float3 viewDir;
};

// Argument buffer structure matching descriptor set layout
struct DescriptorSet0 {
    texturecube<float> cubeTexture [[id(0)]];
    sampler cubeSampler [[id(1)]];
};

fragment float4 PSMain(PSInput in [[stage_in]],
                       constant DescriptorSet0& descriptors [[buffer(0)]]) {
    // Normalize the view direction and sample the cubemap
    float3 dir = normalize(in.viewDir);
    return descriptors.cubeTexture.sample(descriptors.cubeSampler, dir);
}
