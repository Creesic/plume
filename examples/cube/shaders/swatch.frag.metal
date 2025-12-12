#include <metal_stdlib>
using namespace metal;

// Swatch fragment shader
// Outputs the sampled color from push constants

struct Constants {
    float2 offset;   // NDC offset for quad position
    float2 size;     // NDC size of quad
    float4 color;    // Sampled color to display
};

struct PSInput {
    float4 position [[position]];
};

fragment float4 PSMain(PSInput in [[stage_in]],
                       constant Constants& constants [[buffer(8)]]) {
    return constants.color;
}
