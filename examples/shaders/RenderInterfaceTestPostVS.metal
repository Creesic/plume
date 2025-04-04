#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct VertexOutput {
    float2 texCoord [[user(locn0)]];
    float4 position [[position]];
};

vertex VertexOutput VSMain(uint vertexID [[vertex_id]]) {
    VertexOutput output;
    
    // Generate UV coordinates for fullscreen triangle
    // Maps vertex indices (0,1,2) to a triangle covering the screen
    float2 uv = float2(
        (vertexID == 2u) ? 2.0 : 0.0,  // x: 0,0,2
        (vertexID == 1u) ? 2.0 : 0.0   // y: 0,2,0
    );
    
    // Convert UV to clip space coordinates (-1 to 1)
    float2 clipPos = fma(uv, float2(2.0, -2.0), float2(-1.0, 1.0));
    
    // Setup position with Y-flip for Metal's coordinate system
    float4 position = float4(clipPos, 1.0, 1.0);
    position.y = -clipPos.y;
    
    output.position = position;
    output.texCoord = uv;
    
    // Final Y-flip for Metal's coordinate system
    output.position.y = -output.position.y;
    
    return output;
}

