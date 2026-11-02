---
name: plume-resources-descriptors
description: ALWAYS use when working with Plume buffers, textures, texture views, pools, descriptor sets, resource flags, heap types, capability bits, or resource lifetime and residency behavior. Covers descriptor set and pipeline layout creation via `RenderDescriptorSetBuilder`/`RenderPipelineLayoutBuilder`. Do NOT trigger for barrier/layout-transition or fence/semaphore behavior on an already-created resource — use `plume-synchronization` for that. Do NOT trigger for graphics/compute/raytracing pipeline state creation itself — use `plume-shaders-pipelines` for that.
---

# Plume Resources And Descriptors

## Public API

Start with:

- `RenderBufferDesc`, `RenderTextureDesc`, `RenderTextureViewDesc`, `RenderPoolDesc`
- `RenderDescriptorRange`, `RenderDescriptorSetDesc`, `RenderPipelineLayoutDesc`
- `RenderDescriptorSetBuilder`, `RenderPipelineLayoutBuilder`
- `RenderBufferFlag`, `RenderTextureFlag`, `RenderHeapType`, `RenderDescriptorRangeType`

These live in `plume_render_interface_types.h` and `plume_render_interface_builders.h`.

## Descriptor Index Rule

`RenderDescriptorSet::setBuffer`, `setTexture`, `setSampler`, and `setAccelerationStructure` take descriptor indices into the contiguous descriptor set allocation. They are not necessarily shader binding numbers. Builders return the descriptor index base for each range; use those values instead of guessing.

## Backend Implementations

- D3D12: descriptor heap allocation and SRV/UAV/CBV setup in `D3D12DescriptorSet`.
- Vulkan: descriptor set layout and pool handling in `VulkanDescriptorSetLayout` and `VulkanDescriptorSet`.
- Metal: argument-buffer layout and descriptor resource tracking in `MetalDescriptorSetLayout`, `MetalDescriptorSet`, and `MetalPipelineLayout::bindDescriptorSets`.

## Metal-Specific Notes

Metal uses argument buffers. When residency sets are unavailable, Plume tracks resources from descriptor sets and calls `useResource` on active encoders. When residency sets are available, `MetalDevice::addResource` and `removeResource` update a device-level residency set guarded by `resourcesMutex`.

Be careful with:

- `useArgumentBuffersTier2`
- `useDirectBufferAddresses`
- `gpuAddressableResources`
- fallback `useResource` calls
- `commandBufferWithUnretainedReferences`, which raises the bar for explicit lifetime management

## Resource Change Checklist

1. Check public flags and descriptor defaults.
2. Update all backend create paths.
3. Update format/view mapping helpers if a new format or view type is involved.
4. Check resource state or layout tracking.
5. Add or update capabilities if support is conditional.
6. Build at least one backend and inspect the others for compile breakage.

## Common Mistakes

- Passing sparse binding numbers to `set*` instead of contiguous descriptor indices.
- Creating formatted or structured buffer descriptors without the required view metadata.
- Adding a texture usage flag in only one backend.
- Forgetting Metal resource residency or fallback `useResource` tracking for new resource-bearing descriptor types.
- Assuming resource pools are real on Metal; `MetalPool` currently creates resources directly and prints that pools are not implemented.
