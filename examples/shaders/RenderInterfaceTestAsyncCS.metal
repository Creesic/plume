#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct ComputeConstants {
    float value;
};

struct CustomStruct {
    packed_float3 position;  // 3D point
    packed_float2 size;      // 2D size
};

struct StructuredBuffer {
    CustomStruct elements[1];
};

struct ByteAddressBuffer {
    uint elements[1];
};

struct ComputeResources {
    constant float* _padding0 [[id(0)]];  // Maintain padding/alignment
    texture_buffer<float, access::write> outputBuffer [[id(1)]];
    device StructuredBuffer* structuredBufferBase [[id(2)]];
    device StructuredBuffer* structuredBufferOffset [[id(3)]];
    device ByteAddressBuffer* byteAddressBuffer [[id(4)]];
};

kernel void CSMain(
    constant ComputeResources& resources [[buffer(0)]],
    constant ComputeConstants& constants [[buffer(8)]]
) {
    // Write square root of input value to first output slot
    resources.outputBuffer.write(float4(sqrt(constants.value)), uint(0));
    
    // Process base structured buffer
    CustomStruct baseStruct = resources.structuredBufferBase->elements[0];
    
    // Sum all components and write to second output slot
    float baseSum = dot(float3(baseStruct.position), float3(1.0)) + 
                   dot(float2(baseStruct.size), float2(1.0));
    resources.outputBuffer.write(float4(baseSum), uint(1));
    
    // Update base structured buffer by adding 1 to all components
    resources.structuredBufferBase->elements[0] = CustomStruct{
        float3(baseStruct.position) + float3(1.0),
        float2(baseStruct.size) + float2(1.0)
    };
    
    // Process offset structured buffer
    CustomStruct offsetStruct = resources.structuredBufferOffset->elements[0];
    
    // Sum all components and write to third output slot
    float offsetSum = dot(float3(offsetStruct.position), float3(1.0)) + 
                     dot(float2(offsetStruct.size), float2(1.0));
    resources.outputBuffer.write(float4(offsetSum), uint(2));
    
    // Update offset structured buffer by adding 1 to all components
    resources.structuredBufferOffset->elements[0] = CustomStruct{
        float3(offsetStruct.position) + float3(1.0),
        float2(offsetStruct.size) + float2(1.0)
    };
    
    // Process byte address buffer
    uint rawValue = resources.byteAddressBuffer->elements[4];
    float floatValue = as_type<float>(rawValue);
    
    // Write to fourth output slot and increment
    resources.outputBuffer.write(float4(floatValue), uint(3));
    resources.byteAddressBuffer->elements[4] = as_type<uint>(floatValue + 1.0);
}

