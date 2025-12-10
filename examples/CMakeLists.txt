set(CMAKE_CXX_STANDARD 17)

# Enable SDL Vulkan integration
set(SDL_VULKAN_ENABLED ON CACHE BOOL "Enable SDL Vulkan integration" FORCE)

# Find SDL2 (required for examples)
find_package(SDL2 REQUIRED)

# Add tools directory first (needed for shader compilation)
add_subdirectory(tools)

# Include custom ShaderCompilation module
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(ShaderCompilation)

# Create shaders directory for the examples
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)

# Add example subdirectories
add_subdirectory(triangle)
add_subdirectory(cube)

# Set up a custom command to copy the shader files to the bin directory
add_custom_target(copy_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/shaders ${CMAKE_BINARY_DIR}/bin/shaders
    COMMENT "Copying shader files to bin directory"
)
