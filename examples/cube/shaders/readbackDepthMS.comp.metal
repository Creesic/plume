//
// Compute shader depth readback test - MSAA version (Metal)
//
// This shader uses texture2d_ms<float> to read from a multisampled DEPTH texture.
// This exactly replicates the rt64 pictograph bug setup:
// - D32_FLOAT depth texture with MSAA
// - Read via compute shader using texture2d_ms<float>
//
// OBSERVED BEHAVIOR:
// - Apple Silicon (Metal): TBD
// - Intel Mac (Metal): TBD - expected to fail/return zeros
//

#include <metal_stdlib>
using namespace metal;

struct ReadbackConstants {
    uint2 resolution;
    uint2 sampleCoord;
};

struct ComputeArgs0 {
    texture_buffer<uint, access::write> gOutput [[id(0)]];
};

// MSAA depth texture input
struct ComputeArgs1 {
    texture2d_ms<float, access::read> gInput [[id(0)]];
};

kernel void CSMain(
    uint2 coord [[thread_position_in_grid]],
    constant ReadbackConstants& gConstants [[buffer(8)]],
    constant ComputeArgs0& args0 [[buffer(0)]],
    constant ComputeArgs1& args1 [[buffer(1)]]
) {
    if ((coord.x < gConstants.resolution.x) && (coord.y < gConstants.resolution.y)) {
        uint dstIndex = coord.y * gConstants.resolution.x + coord.x;
        uint2 samplePos = gConstants.sampleCoord + coord;

        // Load sample 0 from the MSAA depth texture
        float depth = args1.gInput.read(samplePos, 0).r;

        // Convert depth to uint (scale to 0-65535 range like rt64 does)
        uint depthU16 = uint(saturate(depth) * 65535.0f);
        
        args0.gOutput.write(depthU16, dstIndex);
    }
}
