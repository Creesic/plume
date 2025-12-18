cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    # 10.15 because of our use of supportsFamily()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS deployment target")
endif()

project(plume)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/examples/cmake/modules/PlumeFileToC.cmake)

string(COMPARE EQUAL ${CMAKE_SYSTEM_NAME} "Linux" IS_LINUX)

# Project options
include(CMakeDependentOption)
cmake_dependent_option(PLUME_SDL_VULKAN_ENABLED "Enable SDL Vulkan integration" OFF IS_LINUX OFF)
cmake_dependent_option(PLUME_D3D12_AGILITY_SDK_ENABLED "Enable D3D12 Agility SDK" OFF WIN32 OFF)
cmake_dependent_option(PLUME_APPLE_RETINA_ENABLED "Enable Apple Retina display support" ON APPLE ON)
option(PLUME_BUILD_EXAMPLES "Build example applications" OFF)

# Windows-specific definitions
if(WIN32)
    add_compile_definitions(NOMINMAX)
endif()

# Enable SDL Vulkan support
if(PLUME_SDL_VULKAN_ENABLED)
    if (NOT TARGET SDL2::SDL2)
        find_package(SDL2 REQUIRED)
    else()
        set(SDL2_INCLUDE_DIRS "$<TARGET_PROPERTY:SDL2::SDL2,INTERFACE_INCLUDE_DIRECTORIES>")
    endif()

    message(STATUS "Plume - Building with SDL2_INCLUDE_DIRS: ${SDL2_INCLUDE_DIRS}")
endif()

# Enable D3D12 Agility SDK support
if(PLUME_D3D12_AGILITY_SDK_ENABLED)
    find_package(directx-headers CONFIG REQUIRED)
    find_package(directx12-agility CONFIG REQUIRED)
endif()

# Print status messages
message(STATUS "Plume - Building with backends: Vulkan=1 Metal=${APPLE} D3D12=${WIN32}")
message(STATUS "Plume - SDL Vulkan integration: ${PLUME_SDL_VULKAN_ENABLED}")
message(STATUS "Plume - D3D12 Agility SDK: ${PLUME_D3D12_AGILITY_SDK_ENABLED}")
message(STATUS "Plume - Building examples: ${PLUME_BUILD_EXAMPLES}")

# Basic source files that are always included
set(PLUME_SOURCES
    plume_vulkan.cpp
    plume_vulkan.h
)

# Platform-specific files
if(APPLE)
    list(APPEND PLUME_SOURCES
        plume_metal.cpp
        plume_metal.h
        plume_apple.mm
        plume_apple.h
    )
elseif(WIN32)
    list(APPEND PLUME_SOURCES
        plume_d3d12.cpp
        plume_d3d12.h
        ${CMAKE_CURRENT_SOURCE_DIR}/contrib/D3D12MemoryAllocator/src/D3D12MemAlloc.cpp
    )
endif()

# Create target
add_library(plume STATIC ${PLUME_SOURCES})

# Include directories
target_include_directories(plume PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/contrib/volk
    ${CMAKE_CURRENT_SOURCE_DIR}/contrib/Vulkan-Headers/include
    ${CMAKE_CURRENT_SOURCE_DIR}/contrib/VulkanMemoryAllocator/include
)

if(WIN32)
    target_include_directories(plume PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/contrib/D3D12MemoryAllocator/include
    )
endif()

if(PLUME_SDL_VULKAN_ENABLED)
    target_compile_definitions(plume PUBLIC PLUME_SDL_VULKAN_ENABLED)
    target_include_directories(plume PUBLIC ${SDL2_INCLUDE_DIRS})
endif()

if(PLUME_D3D12_AGILITY_SDK_ENABLED)
    target_compile_definitions(plume PUBLIC PLUME_D3D12_AGILITY_SDK_ENABLED)
    target_compile_definitions(plume PRIVATE D3D12MA_USING_DIRECTX_HEADERS)
    target_link_libraries(plume PRIVATE Microsoft::DirectX-Headers Microsoft::DirectX-Guids Microsoft::DirectX12-Agility)
endif()

# Platform-specific includes and shader compilation
if(APPLE)
    if(PLUME_APPLE_RETINA_ENABLED)
        target_compile_definitions(plume PRIVATE PLUME_APPLE_RETINA_ENABLED)
    endif()

    target_include_directories(plume PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/contrib/metal-cpp
    )

    plume_build_file_to_c()
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")

    function(plume_compile_internal_metal TARGET_NAME SHADER_SOURCE OUTPUT_NAME)
        set(IR_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.ir")
        set(METALLIB_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.metallib")
        set(C_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.metal.c")
        set(H_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.metal.h")

        if(CMAKE_OSX_DEPLOYMENT_TARGET)
            set(METAL_VERSION_FLAG "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
        else()
            set(METAL_VERSION_FLAG "")
        endif()

        add_custom_command(
            OUTPUT "${IR_OUTPUT}"
            COMMAND xcrun -sdk macosx metal ${METAL_VERSION_FLAG} -o "${IR_OUTPUT}" -c "${SHADER_SOURCE}"
            DEPENDS "${SHADER_SOURCE}"
            COMMENT "Compiling Metal shader ${OUTPUT_NAME} to IR"
            VERBATIM
        )

        add_custom_command(
            OUTPUT "${METALLIB_OUTPUT}"
            COMMAND xcrun -sdk macosx metallib "${IR_OUTPUT}" -o "${METALLIB_OUTPUT}"
            DEPENDS "${IR_OUTPUT}"
            COMMENT "Linking ${OUTPUT_NAME} to metallib"
            VERBATIM
        )

        add_custom_command(
            OUTPUT "${C_OUTPUT}" "${H_OUTPUT}"
            COMMAND plume_file_to_c "${METALLIB_OUTPUT}" "${OUTPUT_NAME}BlobMSL" "${C_OUTPUT}" "${H_OUTPUT}"
            DEPENDS "${METALLIB_OUTPUT}" plume_file_to_c
            COMMENT "Generating C header for Metal shader ${OUTPUT_NAME}"
            VERBATIM
        )

        target_sources(${TARGET_NAME} PRIVATE "${C_OUTPUT}")
        target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_BINARY_DIR}")
    endfunction()

    plume_compile_internal_metal(plume "${CMAKE_CURRENT_SOURCE_DIR}/shaders/plume_clear.metal" plume_clear)
    plume_compile_internal_metal(plume "${CMAKE_CURRENT_SOURCE_DIR}/shaders/plume_resolve.metal" plume_resolve)
endif()

# Add examples if requested
if(PLUME_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
