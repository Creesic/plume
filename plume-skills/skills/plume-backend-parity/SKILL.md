---
name: plume-backend-parity
description: ALWAYS use when a change must preserve or compare behavior across D3D12, Vulkan, and Metal. Covers enum mapping, capability flags, unsupported feature handling, feature parity, and backend-specific fallbacks.
---

# Plume Backend Parity

## Purpose

Plume's public API only works if D3D12, Vulkan, and Metal translate the same public descriptors and commands into equivalent backend behavior. Use this skill before changing any shared enum, descriptor, command-list method, resource state, or capability.

## Files To Compare

- Public contract: `plume_render_interface.h`, `plume_render_interface_types.h`
- D3D12: `plume_d3d12.h`, `plume_d3d12.cpp`
- Vulkan: `plume_vulkan.h`, `plume_vulkan.cpp`
- Metal: `plume_metal.h`, `plume_metal.cpp`
- Apple window glue: `plume_apple.h`, `plume_apple.mm`

## Parity Checklist

For any public feature:

1. Confirm descriptor fields and defaults in `plume_render_interface_types.h`.
2. Find each backend's mapping helper or constructor.
3. Check capability flags for feature support.
4. Check examples for expected use.
5. Decide whether unsupported backends should assert, return `nullptr`, expose a capability bit, or provide a fallback.

## Known Backend Differences

- Shader formats: D3D12 uses DXIL, Vulkan uses SPIR-V, Metal uses metallib/MSL through `RenderShaderFormat::METAL`.
- Ray tracing: D3D12 and Vulkan have implementation paths; Metal has stubs returning `nullptr` or no-op build-info methods.
- Root descriptors: D3D12-only; Vulkan and Metal assert.
- Capture: Metal implements `beginCapture`/`endCapture`; D3D12 and Vulkan currently assert.
- Presentation: D3D12 uses DXGI, Vulkan uses `VkSwapchainKHR`, Metal uses `CA::MetalLayer`.
- Descriptor/resource residency: Metal uses argument buffers plus either `MTL::ResidencySet` or `useResource`; D3D12 and Vulkan use their native descriptor/resource models.

## Change Guidance

- Do not silently add behavior to only one backend unless the public capability model says it is optional.
- Do not copy enum numeric values into backend enums. Use explicit mapping helpers.
- Keep all backend error messages and assertions specific enough to diagnose the unsupported feature.
- If backend behavior intentionally differs for API reasons, document the difference in the code or skill-relevant final answer.
