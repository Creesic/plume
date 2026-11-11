---
name: plume-synchronization
description: ALWAYS use when changing or debugging Plume barriers, texture layout transitions, buffer access, command-list stage tracking, queue submission, semaphores, fences, query pools, or visual corruption likely caused by synchronization. Do NOT trigger for creating or binding buffers, textures, or descriptor sets — use `plume-resources-descriptors` for that.
---

# Plume Synchronization

## Public API

The common sync surface is:

- `RenderCommandList::barriers`
- `RenderBarrierStage` and `RenderBarrierStages`
- `RenderBufferBarrier`
- `RenderTextureBarrier`
- `RenderBufferAccess`
- `RenderTextureLayout`
- `RenderCommandSemaphore`
- `RenderCommandFence`
- `RenderQueryPool`

Read `plume_render_interface.h` and `plume_render_interface_types.h` before backend work.

## Backend Models

- D3D12 tracks `D3D12_RESOURCE_STATES` on buffers/textures and emits `D3D12_RESOURCE_BARRIER`.
- Vulkan tracks image layouts and stage flags, then emits `vkCmdPipelineBarrier`.
- Metal tracks Plume stages on resources, ends encoders around barriers, and uses `MTL::Fence` slots for graphics, compute, and copy stage ordering.

## Metal Caveat

Plume's Metal backend is a Metal 3 style metal-cpp implementation, not a Metal 4 `MTL4CommandBuffer` implementation. Do not apply Metal 4 queue barrier APIs directly unless the task is explicitly to migrate Plume to Metal 4.

Current Metal barrier files and methods:

- `MetalCommandList::barriers`
- `MetalCommandList::setBarrier`
- `barrierWait` / `barrierUpdate`
- `endOtherEncoders`
- `checkActiveRenderEncoder`, `checkActiveComputeEncoder`, `checkActiveBlitEncoder`

## Debug Strategy

For suspected sync issues:

1. Reproduce on the affected backend and note whether corruption is deterministic, frame-dependent, or resize-dependent.
2. Trace the public layout/access transition at the call site.
3. Trace each backend's previous-state and next-state update.
4. Check encoder or render-pass boundaries.
5. For Metal, verify `endOtherEncoders` and pending clears are handled before barriers.
6. For Vulkan, verify old/new layouts and aspect masks.
7. For D3D12, verify state transitions and UAV barriers.

## Common Plume Issues

- Missing transition to `COLOR_WRITE` before framebuffer binding.
- Missing transition to `PRESENT` before presenting.
- Wrong stage mask when switching between graphics, compute, and copy.
- Texture layout updated in one backend but ignored or not represented in another.
- Query pool timestamp writes without the required backend counter/fence support.
