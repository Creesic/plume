---
name: plume-debugging
description: ALWAYS use when debugging Plume rendering output, backend crashes, validation errors, captures, blank windows, wrong colors, missing geometry, descriptor issues, shader failures, or performance triage.
---

# Plume Debugging

## First Questions

Before changing code, identify:

- Backend: D3D12, Vulkan, or Metal.
- Platform and build type.
- Example or integrating app.
- Expected output and actual output.
- Whether validation layers, GPU capture, or logs are available.
- Whether the issue reproduces in `examples/triangle` or only a richer path.

## Fast Triage

Check these first:

1. Did the backend interface creation return non-null?
2. Does `RenderInterfaceCapabilities::shaderFormat` match the shader blob passed to `createShader`?
3. Was the swap-chain image acquired successfully?
4. Was the texture transitioned to `COLOR_WRITE` before rendering and `PRESENT` before present?
5. Do framebuffer attachment formats match pipeline render-target formats?
6. Are descriptor indices contiguous descriptor indices rather than sparse binding numbers?
7. Are viewport and scissor non-empty?

## Backend Debug Hooks

- Metal implements `RenderDevice::beginCapture` and `endCapture` with `MTL::CaptureManager`.
- Vulkan and D3D12 capture methods currently assert.
- Vulkan validation requires platform Vulkan layers and instance/device setup.
- D3D12 debugging should use the D3D12 debug layer and DXGI messages where available.
- Metal validation environment variables must be set before `MTL::Device` creation.

## Metal Debugging

Useful environment variables:

```sh
MTL_DEBUG_LAYER=1
MTL_SHADER_VALIDATION=1
MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1
```

For CLI capture tools, check availability first:

```sh
which gpucapture
which gpudebug
```

If unavailable, use Xcode GPU frame capture. Plume also exposes programmatic Metal capture through the public `RenderDevice` methods.

## Rendering Symptom Router

- Clear color only, no geometry: pipeline, vertex input, culling/front face, viewport/scissor, vertex buffer binding.
- Wrong colors: render target format, sRGB/gamma assumptions, blend state, shader blob mismatch.
- Missing textures: descriptor index, texture view format/dimension, layout transition, resource residency/useResource on Metal.
- Flicker or stale tiles: barriers, render-pass/encoder transitions, load/store actions, swap-chain resize.
- Crash on shutdown: backend object lifetime, in-flight command buffers, Metal retained drawable/resource release order.

## Evidence To Collect

- Exact build command and target.
- Runtime log output.
- Screenshot or capture path.
- `.gputrace`, RenderDoc, PIX, or Vulkan validation output if available.
- `git diff --stat` for local changes before debugging a regression.
