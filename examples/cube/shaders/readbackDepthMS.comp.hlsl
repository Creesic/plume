//
// Compute shader depth readback test - MSAA version
//
// This shader uses Texture2DMS<float> to read from a multisampled DEPTH texture.
// This exactly replicates the rt64 pictograph bug setup:
// - D32_FLOAT depth texture with MSAA
// - Read via compute shader using Texture2DMS<float>
//
// OBSERVED BEHAVIOR:
// - Apple Silicon (Metal): TBD
// - Intel Mac (Metal): TBD - expected to fail/return zeros
//

struct ReadbackConstants {
    uint2 resolution;
    uint2 sampleCoord;
};

[[vk::push_constant]] ConstantBuffer<ReadbackConstants> gConstants : register(b0, space0);
RWBuffer<uint> gOutput : register(u0, space0);
Texture2DMS<float> gInput : register(t0, space1);  // MSAA depth texture

[numthreads(8, 8, 1)]
void CSMain(uint2 coord : SV_DispatchThreadID) {
    if ((coord.x < gConstants.resolution.x) && (coord.y < gConstants.resolution.y)) {
        uint dstIndex = coord.y * gConstants.resolution.x + coord.x;
        uint2 samplePos = gConstants.sampleCoord + coord;

        // Load sample 0 from the MSAA depth texture
        float depth = gInput.Load(samplePos, 0);

        // Convert depth to uint (scale to 0-65535 range like rt64 does)
        uint depthU16 = uint(saturate(depth) * 65535.0f);
        
        gOutput[dstIndex] = depthU16;
    }
}
