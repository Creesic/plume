#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct VertexOutput
{
    float2 texCoord [[user(locn0)]];
    float4 position [[position]];
};

struct VertexInput
{
    float2 position [[attribute(0)]];
    float2 texCoord [[attribute(1)]];
};

vertex VertexOutput VSMain(VertexInput in [[stage_in]])
{
    VertexOutput out = {};
    float4 _21 = float4(in.position, 0.5, 1.0);
    _21.y = -in.position.y;
    out.position = _21;
    out.texCoord = in.texCoord;
    out.position.y = -(out.position.y);    // Invert Y-axis for Metal
    return out;
} 