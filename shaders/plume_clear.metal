//
// plume
//
// Copyright (c) 2024 renderbag and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file for details.
//

#include <metal_stdlib>
using namespace metal;

struct DepthClearFragmentOut {
    float depth [[depth(any)]];
};

struct VertexOutput {
    float4 position [[position]];
    uint rect_index [[flat]];
};

vertex VertexOutput clearVert(uint vid [[vertex_id]],
                              uint instance_id [[instance_id]],
                              constant float2* vertices [[buffer(0)]])
{
    VertexOutput out;
    out.position = float4(vertices[vid], 0, 1);
    out.rect_index = instance_id;
    return out;
}

// Color clear fragment shader
fragment float4 clearColorFrag(VertexOutput in [[stage_in]],
                               constant float4* clearColors [[buffer(0)]])
{
    return clearColors[in.rect_index];
}

// Depth clear fragment shader
fragment DepthClearFragmentOut clearDepthFrag(VertexOutput in [[stage_in]],
                                              constant float* clearDepths [[buffer(0)]])
{
    DepthClearFragmentOut out;
    out.depth = clearDepths[in.rect_index];
    return out;
}
