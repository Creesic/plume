#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct VertexOutput
{
    float2 texCoord [[user(locn0)]];
    float4 position [[position]];
};

vertex VertexOutput VSMain(uint vertexID [[vertex_id]])
{
    VertexOutput out = {};
    float2 texCoord = float2((vertexID == 2u) ? 2.0 : 0.0, (vertexID == 1u) ? 2.0 : 0.0);
    float2 position = fma(texCoord, float2(2.0, 2.0), float2(-1.0, -1.0));
    
    out.position = float4(position, 1.0, 1.0);
    out.texCoord = texCoord;
    out.position.y = -(out.position.y);    // Single Y-axis inversion for Metal
    return out;
} 