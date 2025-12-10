#include <metal_stdlib>
using namespace metal;

// Cube texture vertex shader
// Renders a fullscreen triangle and generates view directions for cubemap sampling

struct VSOutput {
    float4 position [[position]];
    float3 viewDir;
};

struct Constants {
    float4x4 invViewProj;
};

// Fullscreen triangle - no vertex buffer needed
// Uses vertex ID to generate positions
vertex VSOutput VSMain(uint vertexID [[vertex_id]],
                       constant Constants& constants [[buffer(8)]]) {
    VSOutput out;

    // Generate fullscreen triangle vertices
    // Vertex 0: (-1, -1), Vertex 1: (3, -1), Vertex 2: (-1, 3)
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    float2 ndc = uv * 2.0 - 1.0;

    // Flip Y for Metal's coordinate system
    ndc.y = -ndc.y;

    out.position = float4(ndc, 0.0, 1.0);

    // Transform NDC to world direction using inverse view-projection
    // Use near and far plane points to get a proper ray direction
    float4 nearPoint = constants.invViewProj * float4(ndc, -1.0, 1.0);
    float4 farPoint = constants.invViewProj * float4(ndc, 1.0, 1.0);

    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;

    // Ray direction from near to far plane
    out.viewDir = farPoint.xyz - nearPoint.xyz;

    return out;
}
