//
// plume
//
// Copyright (c) 2024 renderbag and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file for details.
//

#include "plume_copy_layout.h"

#include <algorithm>
#include <cassert>

namespace plume {
    NormalizedTextureToBufferCopy NormalizeTextureToBufferCopy(
        RenderFormat format, uint32_t width, uint32_t height, uint32_t depth,
        uint32_t rowWidth, uint32_t rowPitch, uint64_t offset,
        uint32_t mipLevel, uint32_t arrayIndex)
    {
        assert(format != RenderFormat::UNKNOWN);
        assert(width > 0);
        assert(height > 0);
        assert(depth > 0);

        const uint32_t blockWidth = RenderFormatBlockWidth(format);
        const uint32_t normalizedRowWidth = std::max(rowWidth, width);
        const uint32_t horizontalBlocks =
            (normalizedRowWidth + blockWidth - 1) / blockWidth;
        const uint32_t minimumRowPitch =
            horizontalBlocks * RenderFormatSize(format);
        const uint32_t normalizedRowPitch =
            (rowPitch > 0) ? rowPitch : minimumRowPitch;
        assert(normalizedRowPitch >= minimumRowPitch);

        const uint32_t bufferImageHeight =
            ((height + blockWidth - 1) / blockWidth) * blockWidth;
        const uint32_t verticalBlocks = bufferImageHeight / blockWidth;

        NormalizedTextureToBufferCopy copy;
        copy.format = format;
        copy.width = width;
        copy.height = height;
        copy.depth = depth;
        copy.rowWidth = normalizedRowWidth;
        copy.rowPitch = normalizedRowPitch;
        copy.bufferImageHeight = bufferImageHeight;
        copy.bytesPerImage =
            static_cast<uint64_t>(normalizedRowPitch) * verticalBlocks;
        copy.offset = offset;
        copy.mipLevel = mipLevel;
        copy.arrayIndex = arrayIndex;
        return copy;
    }
}
