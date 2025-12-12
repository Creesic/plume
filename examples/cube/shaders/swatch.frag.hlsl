// Swatch fragment shader
// Outputs the sampled color from push constants

[[vk::push_constant]]
struct Constants {
    float2 offset;   // NDC offset for quad position
    float2 size;     // NDC size of quad
    float4 color;    // Sampled color to display
} constants;

struct PSInput {
    float4 position : SV_POSITION;
};

float4 PSMain(PSInput input) : SV_TARGET {
    return constants.color;
}
