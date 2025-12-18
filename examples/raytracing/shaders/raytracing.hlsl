// Ray tracing "Hello Triangle" shader
// Compiles to DXIL for D3D12 and Metal via metal-shaderconverter

// Output texture
[[vk::binding(0)]]
RWTexture2D<float4> outputTexture : register(u0);

// Acceleration structure
[[vk::binding(1)]]
RaytracingAccelerationStructure scene : register(t1);

// Camera constants
[[vk::binding(2)]]
cbuffer CameraConstants : register(b2)
{
    float4x4 viewInverse;
    float4x4 projInverse;
    uint width;
    uint height;
    uint frameIndex;
    uint padding;
};

// Ray payload structure
struct RayPayload
{
    float3 color;
    uint depth;
};

// Built-in triangle intersection attributes
struct TriangleAttributes
{
    float2 barycentrics;
};

// Generate a ray for a given pixel
inline void generateCameraRay(uint2 pixelCoord, out float3 origin, out float3 direction)
{
    float2 pixelCenter = float2(pixelCoord) + 0.5;
    float2 uv = pixelCenter / float2(width, height);
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y; // Flip Y for Vulkan/Metal convention

    float4 target = mul(projInverse, float4(ndc.x, ndc.y, 1.0, 1.0));
    target.xyz /= target.w;

    origin = mul(viewInverse, float4(0, 0, 0, 1)).xyz;
    direction = normalize(mul(viewInverse, float4(target.xyz, 0)).xyz);
}

[shader("raygeneration")]
void RayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim = DispatchRaysDimensions().xy;

    float3 origin;
    float3 direction;
    generateCameraRay(launchIndex, origin, direction);

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayPayload payload;
    payload.color = float3(0, 0, 0);
    payload.depth = 0;

    TraceRay(
        scene,              // Acceleration structure
        RAY_FLAG_NONE,      // Ray flags
        0xFF,               // Instance inclusion mask
        0,                  // Hit group index (SBT offset)
        0,                  // Hit group stride (multiplier)
        0,                  // Miss shader index
        ray,
        payload
    );

    outputTexture[launchIndex] = float4(payload.color, 1.0);
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in TriangleAttributes attribs)
{
    // Compute barycentric coordinates for coloring
    float3 barycentrics = float3(
        1.0 - attribs.barycentrics.x - attribs.barycentrics.y,
        attribs.barycentrics.x,
        attribs.barycentrics.y
    );

    // Color based on barycentrics (RGB triangle)
    payload.color = barycentrics;
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
    // Background gradient (sky color)
    float2 uv = float2(DispatchRaysIndex().xy) / float2(DispatchRaysDimensions().xy);
    float3 topColor = float3(0.5, 0.7, 1.0);    // Light blue
    float3 bottomColor = float3(1.0, 1.0, 1.0); // White
    payload.color = lerp(bottomColor, topColor, uv.y);
}
