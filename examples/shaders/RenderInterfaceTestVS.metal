#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct VertexOutput {
    float2 texCoord [[user(locn0)]];
    float4 position [[position]];
};

struct VertexInput {
    float2 position [[attribute(0)]];
    float2 texCoord [[attribute(1)]];
};

vertex VertexOutput VSMain(VertexInput input [[stage_in]]) {
    VertexOutput output;
    
    // Transform 2D position to clip space
    float4 clipPosition = float4(input.position, 0.5, 1.0);
    clipPosition.y = -input.position.y;  // Initial Y-flip for coordinate system
    
    // Pass through texture coordinates
    output.texCoord = input.texCoord;
    output.position = clipPosition;
    
    // Final Y-flip for Metal's coordinate system
    output.position.y = -output.position.y;
    
    return output;
}

