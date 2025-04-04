#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

// Function constant to control color output
constant uint useRed_tmp [[function_constant(0)]];
constant uint useRed = is_function_constant_defined(useRed_tmp) ? useRed_tmp : 0u;

struct PixelOutput {
    float4 color [[color(0)]];
};

fragment PixelOutput PSMain() {
    PixelOutput output;
    
    // Choose between red and green based on function constant
    float4 redColor = float4(1.0, 0.0, 0.0, 1.0);   // Red
    float4 greenColor = float4(0.0, 1.0, 0.0, 1.0); // Green
    
    output.color = select(greenColor, redColor, bool4(useRed != 0u));
    return output;
}

