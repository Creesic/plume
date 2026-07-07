---
name: plume-architecture
description: ALWAYS use when getting familiar with Plume, planning a feature, explaining the RHI design, or changing public API contracts. Covers the public render interface, type descriptors, builders, backend creation functions, ownership rules, and where to start in the codebase.
---

# Plume Architecture

## Scope

Plume is a low-level C++17 rendering hardware interface. It exposes a common public API over D3D12, Vulkan, and Metal, with a "bring your own shader compiler" model and `std::unique_ptr` ownership for most API objects.

## Start Here

Read these files before changing architecture or public contracts:

- `README.md` - project intent and stability caveats.
- `CMakeLists.txt` - backend selection, options, platform-specific source inclusion.
- `plume_render_interface.h` - public virtual API.
- `plume_render_interface_types.h` - enums, flags, descriptors, capabilities.
- `plume_render_interface_builders.h` - descriptor set and pipeline layout builders.
- Backend headers: `plume_d3d12.h`, `plume_vulkan.h`, `plume_metal.h`.

## Public Object Model

The normal flow is:

1. Create a backend interface through `CreateD3D12Interface`, `CreateVulkanInterface`, or `CreateMetalInterface`.
2. Create a `RenderDevice`.
3. Create queues, resources, shaders, samplers, pipeline layouts, pipelines, framebuffers, and swap chains from the device or queue.
4. Record commands through `RenderCommandList`.
5. Submit through `RenderCommandQueue`.

Most objects are returned as `std::unique_ptr`. Do not introduce shared ownership in the public API unless a cross-backend lifetime requirement forces it.

## Backend Boundaries

- Public types must stay backend-neutral.
- Backend implementations may cache native objects, resource states, descriptor metadata, and capability flags.
- D3D12-specific public hooks already exist where needed, for example root descriptors.
- Metal and Vulkan intentionally assert for root descriptors.
- Metal ray tracing is currently unimplemented; D3D12 and Vulkan have ray tracing paths.

## Change Rules

- When adding a public method or descriptor field, update all backends or explicitly document unsupported behavior with capability flags.
- Prefer adding capability bits in `RenderDeviceCapabilities` or `RenderInterfaceCapabilities` over assuming all backends support a feature.
- Keep backend translation helpers local to the backend `.cpp` file unless the public API needs the concept.
- Use the examples to understand intended API use before inventing a new pattern.

## Useful Example Paths

- `examples/triangle/main.cpp` - minimal swap chain, pipeline, vertex buffer, draw loop.
- `examples/cube/main.cpp` - descriptors, textures, upload/copy, and more complete rendering flow.
