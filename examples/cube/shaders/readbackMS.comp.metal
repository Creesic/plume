//
// Compute shader readback test - MSAA version (Metal)
//
// This shader uses texture2d_ms to read from a multisampled texture.
// This replicates the rt64 pictograph bug where an MSAA shader is used
// but a non-MSAA (resolved) texture is bound.
//
// EXPECTED BEHAVIOR:
// - On Apple Silicon: Reads from wrong pixel offset (appears to show "future" colors)
// - On Intel: May return zeros or garbage
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

// MSAA texture input - expects multisampled texture but we bind non-MSAA!
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

        // Load sample 0 - when non-MSAA texture is bound, behavior is undefined
        float4 color = args1.gInput.read(samplePos, 0);

        // Pack as BGRA
        uint b = uint(saturate(color.b) * 255.0f);
        uint g = uint(saturate(color.g) * 255.0f);
        uint r = uint(saturate(color.r) * 255.0f);
        uint a = uint(saturate(color.a) * 255.0f);
        uint packed = b | (g << 8) | (r << 16) | (a << 24);

        args0.gOutput.write(packed, dstIndex);
    }
}
