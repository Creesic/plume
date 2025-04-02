#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct PushConstants
{
    float value;
};

struct CustomStruct
{
    packed_float3 point3D;
    packed_float2 size2D;
};

struct FragmentOutput
{
    float4 color [[color(0)]];
};

kernel void CSMain(texture_buffer<float, access::write> output [[texture(1)]], device CustomStruct* structuredBase [[buffer(2)]], device CustomStruct* structuredOffset [[buffer(3)]], device uint* byteAddress [[buffer(4)]], constant PushConstants& constants [[buffer(8)]])
{
    output.write(float4(sqrt(constants.value)), uint(0u));
    CustomStruct baseData = structuredBase[0];
    output.write(float4((((baseData.point3D[0] + baseData.point3D[1]) + baseData.point3D[2]) + baseData.size2D[0]) + baseData.size2D[1]), uint(1u));
    structuredBase[0] = CustomStruct{ float3(baseData.point3D) + float3(1.0), float2(baseData.size2D) + float2(1.0) };
    CustomStruct offsetData = structuredOffset[0];
    output.write(float4((((offsetData.point3D[0] + offsetData.point3D[1]) + offsetData.point3D[2]) + offsetData.size2D[0]) + offsetData.size2D[1]), uint(2u));
    structuredOffset[0] = CustomStruct{ float3(offsetData.point3D) + float3(1.0), float2(offsetData.size2D) + float2(1.0) };
    uint rawValue = byteAddress[4];
    float floatValue = as_type<float>(rawValue);
    output.write(float4(floatValue), uint(3u));
    byteAddress[4] = as_type<uint>(floatValue + 1.0);
} 