# Ray tracing "Hello Triangle" example

add_executable(plume_raytracing
    main.cpp
)

target_link_libraries(plume_raytracing PRIVATE plume ${SDL2_LIBRARIES})

# Platform-specific libraries
if(APPLE)
    target_link_libraries(plume_raytracing PRIVATE "-framework Metal -framework QuartzCore -framework CoreGraphics -framework Foundation -framework IOKit")
elseif(WIN32)
    target_link_libraries(plume_raytracing PRIVATE d3d12 dxgi)
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(X11 REQUIRED)
    target_include_directories(plume_raytracing PUBLIC ${X11_INCLUDE_DIR} ${X11_Xrandr_INCLUDE_PATH})
    target_link_libraries(plume_raytracing PRIVATE ${X11_LIBRARIES} ${X11_Xrandr_LIB})
endif()

target_include_directories(plume_raytracing PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/examples
    ${SDL2_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/shaders
)

# Ray tracing shaders are compiled differently per platform:
# - D3D12: HLSL -> DXIL (library shader)
# - Vulkan: HLSL -> SPIR-V (library shader)
# - Metal: HLSL -> DXIL -> metallib (via Metal Shader Converter)

# For D3D12: Compile as DXIL library
if(WIN32)
    plume_compile_library_shader(plume_raytracing
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/raytracing.hlsl"
        rtShaders
        SHADER_MODEL 6_3
    )
endif()

# For Metal: HLSL -> DXIL -> metallib via Metal Shader Converter
if(APPLE)
    plume_compile_rt_shader_metal(plume_raytracing
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/raytracing.hlsl"
        rtShadersMetal
    )
endif()

# Set output directory
set_target_properties(plume_raytracing PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
