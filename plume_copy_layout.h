//
// plume
//
// Copyright (c) 2024 renderbag and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file for details.
//

#pragma once

#include "plume_render_interface_types.h"

namespace plume {
    struct NormalizedTextureToBufferCopy {
        RenderFormat format = RenderFormat::UNKNOWN;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 0;
        uint32_t rowWidth = 0;
        uint32_t rowPitch = 0;
        uint32_t bufferImageHeight = 0;
        uint64_t bytesPerImage = 0;
        uint64_t offset = 0;
        uint32_t mipLevel = 0;
        uint32_t arrayIndex = 0;
    };

    NormalizedTextureToBufferCopy NormalizeTextureToBufferCopy(
        RenderFormat format, uint32_t width, uint32_t height, uint32_t depth,
        uint32_t rowWidth, uint32_t rowPitch, uint64_t offset,
        uint32_t mipLevel, uint32_t arrayIndex);
}
