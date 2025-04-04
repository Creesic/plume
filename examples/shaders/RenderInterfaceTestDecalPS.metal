#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct DepthResources {
    texture2d_ms<float> depthTexture [[id(0)]];
};

struct PixelOutput {
    float4 color [[color(0)]];
};

struct PixelInput {
    float2 texCoord [[user(locn0)]];
};

fragment PixelOutput PSMain(
    PixelInput input [[stage_in]],
    constant DepthResources& resources [[buffer(0)]],
    float4 fragCoord [[position]],
    uint sampleID [[sample_id]]
) {
    PixelOutput output;
    
    // Adjust fragment coordinate by sample position
    float2 adjustedCoord = fragCoord.xy + get_sample_position(sampleID) - 0.5;
    
    // Read depth from multisample texture
    float sampledDepth = resources.depthTexture
        .read(uint2(int2(floor(adjustedCoord))), int(sampleID))
        .x;
    
    // Discard fragment if depth difference is too large
    const float DEPTH_THRESHOLD = 9.9999999747524270787835121154785e-07;
    if (abs(fragCoord.z - sampledDepth) > DEPTH_THRESHOLD) {
        discard_fragment();
    }
    
    // Output color based on texture coordinates
    output.color = float4(1.0, input.texCoord, 1.0);
    return output;
}

