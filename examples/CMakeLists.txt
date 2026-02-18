set(CMAKE_CXX_STANDARD 17)

# Enable SDL Vulkan integration (Linux only)
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(PLUME_SDL_VULKAN_ENABLED ON CACHE BOOL "Enable SDL Vulkan integration" FORCE)
endif()

# Find SDL2 (required for examples)
if (WIN32)
    set(SDL2_INCLUDE_DIRS "${SDL2_DIR}/include")
    set(SDL2_BINDIR "${SDL2_DIR}/lib/x64")
    set(SDL2_LIBRARIES "SDL2" "SDL2main")
    link_directories("${SDL2_BINDIR}")
else()
    find_package(SDL2 REQUIRED)
endif()

# Initialize shader compilation
include(${CMAKE_SOURCE_DIR}/examples/cmake/PlumeShaders.cmake)
plume_shaders_init()

# Add example subdirectories
add_subdirectory(triangle)
add_subdirectory(cube)
