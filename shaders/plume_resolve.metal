//
// plume
//
// Copyright (c) 2024 renderbag and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file for details.
//

#include <metal_stdlib>
using namespace metal;

struct ResolveParams {
    uint2 dstOffset;
    uint2 srcOffset;
    uint2 resolveSize;
};

kernel void msaaResolve(
    texture2d_ms<float> source [[texture(0)]],
    texture2d<float, access::write> destination [[texture(1)]],
    constant ResolveParams& params [[buffer(0)]],
    uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= params.resolveSize.x || gid.y >= params.resolveSize.y) return;

    uint2 dstPos = gid + params.dstOffset;
    uint2 srcPos = gid + params.srcOffset;

    float4 color = float4(0);
    for (uint s = 0; s < source.get_num_samples(); s++) {
        color += source.read(srcPos, s);
    }
    color /= float(source.get_num_samples());

    destination.write(color, dstPos);
}
