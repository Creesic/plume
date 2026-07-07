# Plume Skills

Claude Code and Codex plugin skills for working on Plume, a C++17 rendering hardware interface that targets D3D12, Vulkan, and Metal. Install via Claude Code by adding this repo as a plugin marketplace (`.claude-plugin/marketplace.json` at the repo root) and installing the `plume-skills` plugin, or via Codex using `plume-skills/.codex-plugin/plugin.json`. Works the same regardless of host OS — the skills themselves reference D3D12, Vulkan, and Metal by file path and don't require any particular platform to read.

## Skills

| Skill | Use for |
|---|---|
| `plume-architecture` | Public RHI API, ownership model, major source files, feature planning |
| `plume-build-validation` | CMake configuration, submodules, examples, CI parity, local validation |
| `plume-backend-parity` | Keeping D3D12, Vulkan, and Metal behavior aligned |
| `plume-resources-descriptors` | Buffers, textures, pools, descriptor sets, pipeline layouts, capabilities |
| `plume-synchronization` | Barriers, layouts, command-list state, semaphores, fences, query pools |
| `plume-shaders-pipelines` | Shader formats, HLSL compilation, SPIR-V/MSL/DXIL blobs, pipeline creation |
| `plume-presentation` | Swap chains, windows, drawable/image acquisition, resize, vsync |
| `plume-debugging` | Rendering diagnosis, captures, validation layers, backend-specific debug paths |

## Verified Baseline

On this checkout, a macOS library-only build was verified with:

```sh
cmake -S . -B build/codex-plume -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/codex-plume --target plume -j 8
```

That built `plume_vulkan.cpp`, `plume_metal.cpp`, `plume_apple.mm`, and linked `libplume.a`.
