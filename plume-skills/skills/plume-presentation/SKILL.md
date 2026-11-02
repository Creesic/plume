---
name: plume-presentation
description: ALWAYS use when working with Plume swap chains, native windows, present/acquire flow, resize, vsync, display timing, drawable or swapchain textures, and platform window glue.
---

# Plume Presentation

## Public API

Start with:

- `RenderWindow`
- `RenderSwapChainDesc`
- `RenderSwapChain`
- `RenderCommandQueue::createSwapChain`

These are in `plume_render_interface_types.h` and `plume_render_interface.h`.

## Platform Window Types

`RenderWindow` changes by platform:

- Windows: `HWND`
- Android: `ANativeWindow*`
- Linux without SDL Vulkan integration: X11 display and window.
- SDL Vulkan integration: `SDL_Window*`
- Apple: `{ void* window, void* view }`, where the view slot is used with the Metal layer.

## Backend Presentation Paths

- D3D12: `D3D12SwapChain`, DXGI swap chain, waitable object support.
- Vulkan: `VulkanSwapChain`, platform surfaces, `vkAcquireNextImageKHR`, `vkQueuePresentKHR`.
- Metal: `MetalSwapChain`, `CA::MetalLayer`, `CA::MetalDrawable`, retained drawable wrappers.
- Apple window attributes and refresh rate: `plume_apple.mm` and `CocoaWindow`.

## Frame Flow In Examples

The example loop:

1. Acquire the next swap-chain texture.
2. Transition it to `COLOR_WRITE`.
3. Bind framebuffer, viewport, scissor, pipeline, resources.
4. Draw.
5. Transition it to `PRESENT`.
6. Submit command list and signal release semaphore.
7. Present.

Use `examples/triangle/main.cpp` as the minimal reference.

## Resize Guidance

- Recreate framebuffers after swap-chain resize.
- Keep drawable/image size and framebuffer size in sync.
- On Apple, `CocoaWindow` applies the backing scale factor when `PLUME_APPLE_RETINA_ENABLED` is set.
- Treat zero-sized swap chains as empty and avoid rendering into them.

## Vsync And Timing

- Public methods: `setVsyncEnabled`, `isVsyncEnabled`, `getRefreshRate`.
- Metal uses `CA::MetalLayer` behavior and cached refresh rate through `CocoaWindow`.
- D3D12 and Vulkan use their native present modes and present wait support.
- Guard present-wait use with `RenderDeviceCapabilities::presentWait`.
