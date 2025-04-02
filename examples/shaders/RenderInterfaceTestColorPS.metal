#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct PushConstants
{
    float4 colorAdd;
    uint textureIndex;
};

struct FragmentOutput
{
    float4 color [[color(0)]];
};

struct FragmentInput
{
    float2 texCoord [[user(locn0)]];
};

fragment FragmentOutput PSMain(FragmentInput in [[stage_in]], constant PushConstants& constants [[buffer(8)]])
{
    FragmentOutput out = {};
    float4 _37;
    do
    {
        if (length(constants.colorAdd) > 0.001000000047497451305389404296875)
        {
            _37 = constants.colorAdd;
            break;
        }
        _37 = float4(in.texCoord, 1.0, 0.5);
        break;
    } while(false);
    out.color = _37;
    return out;
} 