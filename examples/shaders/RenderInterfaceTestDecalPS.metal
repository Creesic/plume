#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct FragmentOutput
{
    float4 color [[color(0)]];
};

struct FragmentInput
{
    float2 texCoord [[user(locn0)]];
};

fragment FragmentOutput PSMain(FragmentInput in [[stage_in]], texture2d_ms<float> depthTexture [[texture(0)]], float4 fragCoord [[position]], uint sampleID [[sample_id]])
{
    FragmentOutput out = {};
    fragCoord.xy += get_sample_position(sampleID) - 0.5;
    if (abs(fragCoord.z - depthTexture.read(uint2(int2(floor(fragCoord.xy))), int(sampleID)).x) > 9.9999999747524270787835121154785e-07)
    {
        discard_fragment();
    }
    out.color = float4(1.0, in.texCoord, 1.0);
    return out;
} 