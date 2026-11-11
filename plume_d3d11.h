// Copyright (c) 2024 renderbag and contributors. Licensed under the MIT license.
#pragma once
#include <cstdint>

namespace plume {
    struct RenderDevice;
    using D3D11DeviceLostCallback = void (*)(const char *, int32_t);
    // Other backend types pass through unchanged. Startup checks before arming
    // the callback, so a failed attempt can follow the host's fallback policy.
    bool D3D11DeviceIsReady(RenderDevice *device);
    void SetD3D11DeviceLostCallback(RenderDevice *device, D3D11DeviceLostCallback callback);
}
