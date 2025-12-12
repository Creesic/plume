//
// Compute shader readback test (Metal)
//
// This shader reads from a 2D texture and writes to a buffer.
// It's designed to replicate the pictograph issue on Intel/AMD GPUs.
//

#include <metal_stdlib>
using namespace metal;

struct ReadbackConstants {
    uint2 resolution;
    uint2 sampleCoord;
};

// For RWBuffer<uint> (formatted buffer), Metal uses a texture buffer
struct ComputeArgs0 {
    texture_buffer<uint, access::write> gOutput [[id(0)]];
};

struct ComputeArgs1 {
    texture2d<float, access::read> gInput [[id(0)]];
};

kernel void CSMain(
    uint2 coord [[thread_position_in_grid]],
    constant ReadbackConstants& gConstants [[buffer(8)]],
    constant ComputeArgs0& args0 [[buffer(0)]],
    constant ComputeArgs1& args1 [[buffer(1)]]
) {
    if ((coord.x < gConstants.resolution.x) && (coord.y < gConstants.resolution.y)) {
        uint dstIndex = coord.y * gConstants.resolution.x + coord.x;

        // Sample from the input texture at the sample coordinate offset by our thread position
        uint2 samplePos = gConstants.sampleCoord + coord;
        float4 color = args1.gInput.read(samplePos);

        // Convert to BGRA8 packed format (same as B8G8R8A8_UNORM)
        uint b = uint(saturate(color.b) * 255.0f);
        uint g = uint(saturate(color.g) * 255.0f);
        uint r = uint(saturate(color.r) * 255.0f);
        uint a = uint(saturate(color.a) * 255.0f);

        // Pack as BGRA (matches the swapchain format)
        uint packed = b | (g << 8) | (r << 16) | (a << 24);
        args0.gOutput.write(packed, dstIndex);
    }
}
