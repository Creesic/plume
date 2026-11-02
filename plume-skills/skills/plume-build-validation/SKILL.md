---
name: plume-build-validation
description: ALWAYS use when building, configuring, testing, or validating Plume. Covers CMake options, submodules, examples, CI matrix behavior, SDL2, shader-tool fetching, and local verification commands.
---

# Plume Build And Validation

## Source Of Truth

Read:

- `CMakeLists.txt`
- `.github/workflows/validate.yml`
- `examples/CMakeLists.txt`
- `examples/cmake/PlumeShaders.cmake`
- `examples/cmake/modules/PlumeDXC.cmake`
- `examples/cmake/modules/PlumeSpirvCross.cmake`
- `examples/cmake/modules/PlumeSDL2.cmake`

## Submodules

Plume depends on recursive submodules under `contrib/`:

- `D3D12MemoryAllocator`
- `Vulkan-Headers`
- `VulkanMemoryAllocator`
- `volk`
- `metal-cpp`

Check with:

```sh
git submodule status --recursive
```

If headers are missing, initialize submodules before debugging compile errors.

## CMake Options

- `PLUME_BUILD_EXAMPLES` - builds example applications.
- `PLUME_SDL_VULKAN_ENABLED` - enables SDL Vulkan integration; default ON only on Linux when examples force it.
- `PLUME_D3D12_AGILITY_SDK_ENABLED` - Windows-only DirectX Agility SDK path.
- `PLUME_APPLE_RETINA_ENABLED` - Apple-only backing scale behavior.

## Fast Local Validation

For a library-only build:

```sh
cmake -S . -B build/codex-plume -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/codex-plume --target plume -j 8
```

For examples:

```sh
cmake -S . -B build/codex-plume-examples -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPLUME_BUILD_EXAMPLES=ON
cmake --build build/codex-plume-examples --target plume_triangle -j 8
```

Examples require SDL2. On Apple, shader examples also use DXC, SPIRV-Cross, `xcrun metal`, and `xcrun metallib`.

## CI Parity

The GitHub workflow builds Debug and Release across Linux, Windows, Intel macOS, and Apple Silicon macOS. It builds `plume` and `plume_triangle`, not every example target. When local validation is impossible for a platform backend, explain which platform path remains unverified.

## Validation Discipline

- Build the touched backend whenever possible.
- If touching public API, compile at least one backend and inspect all backend compile surfaces.
- If touching examples or shader CMake, build `plume_triangle`.
- If changing shader output names or formats, verify generated include names expected by examples.
