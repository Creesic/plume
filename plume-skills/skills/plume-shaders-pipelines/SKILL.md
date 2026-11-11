---
name: plume-shaders-pipelines
description: ALWAYS use when working with Plume shader formats, shader compilation CMake, embedded shader blobs, specialization constants, input layouts, or graphics/compute/raytracing pipeline creation and pipeline-state bugs. Do NOT trigger for descriptor set or pipeline layout construction itself (`RenderPipelineLayoutDesc`/builders) — use `plume-resources-descriptors` for that.
---

# Plume Shaders And Pipelines

## Public API

Read:

- `RenderShaderFormat`
- `RenderShader`
- `RenderPipeline`
- `RenderPipelineLayout`
- `RenderComputePipelineDesc`
- `RenderGraphicsPipelineDesc`
- `RenderRaytracingPipelineDesc`
- `RenderPipelineProgram`
- `RenderSpecConstant`

## Shader Format Contract

Each backend advertises the shader format it expects through `RenderInterfaceCapabilities::shaderFormat`:

- D3D12: `DXIL`
- Vulkan: `SPIRV`
- Metal: `METAL`

Examples choose the embedded shader blob based on this capability. Do not hard-code one format in cross-backend example or library code.

## Example Shader Tooling

Read:

- `examples/cmake/PlumeShaders.cmake`
- `examples/cmake/modules/PlumeDXC.cmake`
- `examples/cmake/modules/PlumeSpirvCross.cmake`
- `examples/cmake/tools/file_to_c.cpp`
- `examples/cmake/tools/spirv_cross_msl.cpp`

The current HLSL flow compiles:

- SPIR-V for Vulkan.
- DXIL for Windows when spec constants are not used.
- SPIR-V to MSL source to metallib on Apple.
- Embedded C arrays for example binaries.

## Pipeline Change Checklist

1. Update public descriptor fields only when all backends can consume them or capability-gate them.
2. Check D3D12, Vulkan, and Metal pipeline constructors.
3. Verify format, blend, depth, stencil, cull, front-face, topology, input slot, and input element mappings.
4. Keep spec constant handling consistent.
5. Verify shader entry point names used by examples.
6. Build `plume_triangle` if example shader generation or pipeline setup changed.

## Backend Notes

- Metal creates `MTL::Function` from a metallib and entry-point name, then creates Metal pipeline states.
- Vulkan creates `VkShaderModule` from SPIR-V and builds graphics/compute/ray tracing pipelines.
- D3D12 consumes DXIL bytecode and builds graphics/compute/ray tracing state.
- Ray tracing is not implemented on Metal in this checkout.

## Common Mistakes

- Adding a format in public enums without mapping it in all backend format helpers.
- Treating HLSL semantic names as enough for Vulkan/Metal; Plume also carries explicit locations.
- Forgetting `layoutDesc.allowInputLayout = true` when a graphics pipeline needs vertex input.
- Changing shader blob names without updating generated include references in examples.
