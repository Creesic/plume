//
// Compute shader readback test
//
// This shader reads from a 2D texture and writes to a buffer.
// It's designed to replicate the pictograph issue on Intel/AMD GPUs.
//

// Push constants for the readback parameters
struct ReadbackConstants {
    uint2 resolution;     // Output resolution
    uint2 sampleCoord;    // Coordinate to sample from
};

[[vk::push_constant]] ConstantBuffer<ReadbackConstants> gConstants : register(b0, space0);
RWBuffer<uint> gOutput : register(u0, space0);
Texture2D<float4> gInput : register(t0, space1);

[numthreads(8, 8, 1)]
void CSMain(uint2 coord : SV_DispatchThreadID) {
    if ((coord.x < gConstants.resolution.x) && (coord.y < gConstants.resolution.y)) {
        uint dstIndex = coord.y * gConstants.resolution.x + coord.x;

        // Sample from the input texture at the sample coordinate offset by our thread position
        uint2 samplePos = gConstants.sampleCoord + coord;
        float4 color = gInput.Load(uint3(samplePos, 0));

        // Convert to BGRA8 packed format (same as B8G8R8A8_UNORM)
        uint b = uint(saturate(color.b) * 255.0f);
        uint g = uint(saturate(color.g) * 255.0f);
        uint r = uint(saturate(color.r) * 255.0f);
        uint a = uint(saturate(color.a) * 255.0f);

        // Pack as BGRA (matches the swapchain format)
        uint packed = b | (g << 8) | (r << 16) | (a << 24);
        gOutput[dstIndex] = packed;
    }
}
