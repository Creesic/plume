#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct ColorConstants {
    float4 colorAdd;
    uint textureIndex;
};

struct PixelOutput {
    float4 color [[color(0)]];
};

struct PixelInput {
    float2 texCoord [[user(locn0)]];
};

fragment PixelOutput PSMain(
    PixelInput input [[stage_in]],
    constant ColorConstants& constants [[buffer(8)]]
) {
    PixelOutput output;
    
    // Check if we should use the provided color or generate one from UV coordinates
    const float COLOR_THRESHOLD = 0.001;
    float4 finalColor;
    
    if (length(constants.colorAdd) > COLOR_THRESHOLD) {
        // Use the provided color if it's not close to zero
        finalColor = constants.colorAdd;
    } else {
        // Generate a color from UV coordinates
        finalColor = float4(input.texCoord, 1.0, 0.5);
    }
    
    output.color = finalColor;
    return output;
}

