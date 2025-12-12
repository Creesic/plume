// Swatch vertex shader
// Renders a small quad in the corner showing the sampled color

struct VSOutput {
    float4 position : SV_POSITION;
};

// Push constants for quad position/size and color
[[vk::push_constant]]
struct Constants {
    float2 offset;   // NDC offset for quad position
    float2 size;     // NDC size of quad
    float4 color;    // Sampled color to display
} constants;

// Quad vertices using vertex ID
// Renders a quad as two triangles (6 vertices)
VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Generate quad vertices (two triangles)
    // Triangle 1: 0, 1, 2  Triangle 2: 2, 1, 3
    // 0--1
    // |\ |
    // | \|
    // 2--3
    float2 quadVerts[6] = {
        float2(0, 0), float2(1, 0), float2(0, 1),  // Triangle 1
        float2(0, 1), float2(1, 0), float2(1, 1)   // Triangle 2
    };

    float2 pos = quadVerts[vertexID];

    // Scale and offset to final position
    pos = pos * constants.size + constants.offset;

    output.position = float4(pos, 0.0, 1.0);

    return output;
}
