set(CMAKE_CXX_STANDARD 17)

# Enable SDL Vulkan integration (Linux only)
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(PLUME_SDL_VULKAN_ENABLED ON CACHE BOOL "Enable SDL Vulkan integration" FORCE)
endif()

# Find SDL2 (required for examples)
find_package(SDL2 REQUIRED)

# Initialize shader compilation
include(${CMAKE_SOURCE_DIR}/cmake/PlumeShaders.cmake)
plume_shaders_init()

# Add example subdirectories
add_subdirectory(triangle)
add_subdirectory(cube)

# Raytracing example requires Metal Shader Converter on Apple platforms
if(APPLE)
    if(METAL_SHADER_CONVERTER_INCLUDE)
        add_subdirectory(raytracing)
    else()
        message(STATUS "Skipping raytracing example: Metal Shader Converter not found")
    endif()
else()
    # D3D12 raytracing doesn't need special tools
    add_subdirectory(raytracing)
endif()
