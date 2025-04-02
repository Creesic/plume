#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

constant uint useRed_tmp [[function_constant(0)]];
constant uint useRed = is_function_constant_defined(useRed_tmp) ? useRed_tmp : 0u;

struct FragmentOutput
{
    float4 color [[color(0)]];
};

fragment FragmentOutput PSMain()
{
    FragmentOutput out = {};
    out.color = select(float4(0.0, 1.0, 0.0, 1.0), float4(1.0, 0.0, 0.0, 1.0), bool4(useRed != 0u));
    return out;
} 