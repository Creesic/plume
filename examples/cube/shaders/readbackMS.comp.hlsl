//
// Compute shader readback test - MSAA version
//
// This shader uses Texture2DMS to read from a multisampled texture.
// This replicates the rt64 pictograph bug where an MSAA shader is used
// but a non-MSAA (resolved) texture is bound.
//
// EXPECTED BEHAVIOR:
// - On Apple Silicon: Reads from wrong pixel offset (appears to show "future" colors)
// - On Intel: May return zeros or garbage
//

struct ReadbackConstants {
    uint2 resolution;
    uint2 sampleCoord;
};

[[vk::push_constant]] ConstantBuffer<ReadbackConstants> gConstants : register(b0, space0);
RWBuffer<uint> gOutput : register(u0, space0);
Texture2DMS<float4> gInput : register(t0, space1);

[numthreads(8, 8, 1)]
void CSMain(uint2 coord : SV_DispatchThreadID) {
    if ((coord.x < gConstants.resolution.x) && (coord.y < gConstants.resolution.y)) {
        uint dstIndex = coord.y * gConstants.resolution.x + coord.x;
        uint2 samplePos = gConstants.sampleCoord + coord;

        // Load sample 0 - when non-MSAA texture is bound, behavior is undefined
        float4 color = gInput.Load(samplePos, 0);

        // Pack as BGRA
        uint b = uint(saturate(color.b) * 255.0f);
        uint g = uint(saturate(color.g) * 255.0f);
        uint r = uint(saturate(color.r) * 255.0f);
        uint a = uint(saturate(color.a) * 255.0f);
        uint packed = b | (g << 8) | (r << 16) | (a << 24);

        gOutput[dstIndex] = packed;
    }
}
