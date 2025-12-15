# PlumeShaders.cmake
# Public shader compilation API for Plume RHI
#
# Usage:
#   include(path/to/plume/cmake/PlumeShaders.cmake)
#   plume_shaders_init()
#
#   plume_compile_vertex_shader(my_target shaders/main.vert.hlsl mainVert VSMain)
#   plume_compile_pixel_shader(my_target shaders/main.frag.hlsl mainFrag PSMain)
#   plume_compile_compute_shader(my_target shaders/compute.hlsl computeShader CSMain)
#
# Bring your own DXC/SPIRV-Cross (set before calling plume_shaders_init):
#   set(PLUME_DXC_EXECUTABLE "/path/to/dxc")
#   set(PLUME_DXC_LIB_DIR "/path/to/lib")  # macOS/Linux only
#
# Output:
#   HLSL shaders compile to:
#     - SPIR-V (all platforms): {OUTPUT_NAME}BlobSPIRV in shaders/{OUTPUT_NAME}.hlsl.spirv.h
#     - DXIL (Windows only): {OUTPUT_NAME}BlobDXIL in shaders/{OUTPUT_NAME}.hlsl.dxil.h
#     - Metal (Apple only): {OUTPUT_NAME}BlobMSL in shaders/{OUTPUT_NAME}.metal.h (via SPIR-V cross-compilation)

include("${CMAKE_CURRENT_LIST_DIR}/modules/PlumeFileToC.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/modules/PlumeDXC.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/modules/PlumeSpirvCross.cmake")

# Initialize shader compilation infrastructure
# Call this once before using other plume_compile_* functions
#
# If you want to provide your own tools, set these before calling:
#   PLUME_DXC_EXECUTABLE - Path to DXC binary
#   PLUME_DXC_LIB_DIR - Path to DXC libraries (macOS/Linux only)
#   PLUME_SPIRV_CROSS_LIB_DIR - Path to spirv-cross static libraries
#   PLUME_SPIRV_CROSS_INCLUDE_DIR - Path to spirv-cross headers
function(plume_shaders_init)
    # Fetch DXC if not already provided
    if(NOT DEFINED PLUME_DXC_EXECUTABLE)
        plume_fetch_dxc()
    endif()

    # Fetch/build spirv-cross on Apple if not already provided
    if(APPLE AND NOT TARGET plume_spirv_cross_msl)
        plume_fetch_spirv_cross()
    endif()

    plume_build_file_to_c()

    # Create output directory
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")
endfunction()

# Internal: Compile HLSL to a specific format (spirv or dxil)
function(_plume_compile_hlsl_impl TARGET_NAME SHADER_SOURCE SHADER_TYPE OUTPUT_NAME OUTPUT_FORMAT ENTRY_POINT)
    plume_get_dxc_command(DXC_CMD)

    if(OUTPUT_FORMAT STREQUAL "spirv")
        set(OUTPUT_EXT "spv")
        set(BLOB_SUFFIX "SPIRV")
        set(FORMAT_FLAGS ${PLUME_DXC_SPV_OPTS})
    elseif(OUTPUT_FORMAT STREQUAL "dxil")
        set(OUTPUT_EXT "dxil")
        set(BLOB_SUFFIX "DXIL")
        set(FORMAT_FLAGS ${PLUME_DXC_DXIL_OPTS})
    else()
        message(FATAL_ERROR "Unknown output format: ${OUTPUT_FORMAT}")
    endif()

    set(SHADER_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.hlsl.${OUTPUT_EXT}")
    set(C_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.hlsl.${OUTPUT_FORMAT}.c")
    set(H_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.hlsl.${OUTPUT_FORMAT}.h")

    # Determine shader profile and extra args based on type
    if(SHADER_TYPE STREQUAL "vertex")
        set(SHADER_PROFILE "vs_6_0")
        set(DXC_EXTRA_ARGS "-fvk-invert-y")
    elseif(SHADER_TYPE STREQUAL "pixel" OR SHADER_TYPE STREQUAL "fragment")
        set(SHADER_PROFILE "ps_6_0")
        set(DXC_EXTRA_ARGS "")
    elseif(SHADER_TYPE STREQUAL "compute")
        set(SHADER_PROFILE "cs_6_0")
        set(DXC_EXTRA_ARGS "")
    elseif(SHADER_TYPE STREQUAL "ray")
        set(SHADER_PROFILE "lib_6_3")
        set(DXC_EXTRA_ARGS ${PLUME_DXC_RT_OPTS})
    else()
        message(FATAL_ERROR "Unknown shader type: ${SHADER_TYPE}. Use: vertex, pixel/fragment, compute, or ray")
    endif()

    set(BLOB_NAME "${OUTPUT_NAME}Blob${BLOB_SUFFIX}")

    # Compile using DXC
    add_custom_command(
        OUTPUT "${SHADER_OUTPUT}"
        COMMAND ${DXC_CMD} ${PLUME_DXC_COMMON_OPTS} -E ${ENTRY_POINT} -T ${SHADER_PROFILE}
                ${FORMAT_FLAGS} ${DXC_EXTRA_ARGS} -Fo "${SHADER_OUTPUT}" "${SHADER_SOURCE}"
        DEPENDS "${SHADER_SOURCE}"
        COMMENT "Compiling ${SHADER_TYPE} shader ${OUTPUT_NAME} to ${OUTPUT_FORMAT}"
        VERBATIM
    )

    # Generate C header
    add_custom_command(
        OUTPUT "${C_OUTPUT}" "${H_OUTPUT}"
        COMMAND plume_file_to_c "${SHADER_OUTPUT}" "${BLOB_NAME}" "${C_OUTPUT}" "${H_OUTPUT}"
        DEPENDS "${SHADER_OUTPUT}" plume_file_to_c
        COMMENT "Generating C header for ${OUTPUT_NAME} ${OUTPUT_FORMAT}"
        VERBATIM
    )

    target_sources(${TARGET_NAME} PRIVATE "${C_OUTPUT}")
    target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_BINARY_DIR}")
endfunction()

# Internal: Compile SPIR-V to Metal via spirv-cross
function(_plume_compile_spirv_to_metal_impl TARGET_NAME SPIRV_FILE OUTPUT_NAME)
    set(METAL_SOURCE "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.metal")
    set(IR_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.ir")
    set(METALLIB_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.metallib")
    set(C_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.metal.c")
    set(H_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.metal.h")

    # Get deployment target for Metal compilation
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(METAL_VERSION_FLAG "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    else()
        set(METAL_VERSION_FLAG "")
    endif()

    # Convert SPIR-V to Metal source
    add_custom_command(
        OUTPUT "${METAL_SOURCE}"
        COMMAND plume_spirv_cross_msl "${SPIRV_FILE}" "${METAL_SOURCE}"
        DEPENDS "${SPIRV_FILE}" plume_spirv_cross_msl
        COMMENT "Converting ${OUTPUT_NAME} SPIR-V to Metal"
        VERBATIM
    )

    # Compile Metal to IR
    add_custom_command(
        OUTPUT "${IR_OUTPUT}"
        COMMAND xcrun -sdk macosx metal ${METAL_VERSION_FLAG} -o "${IR_OUTPUT}" -c "${METAL_SOURCE}"
        DEPENDS "${METAL_SOURCE}"
        COMMENT "Compiling Metal shader ${OUTPUT_NAME} to IR"
        VERBATIM
    )

    # Link IR to metallib
    add_custom_command(
        OUTPUT "${METALLIB_OUTPUT}"
        COMMAND xcrun -sdk macosx metallib "${IR_OUTPUT}" -o "${METALLIB_OUTPUT}"
        DEPENDS "${IR_OUTPUT}"
        COMMENT "Linking ${OUTPUT_NAME} to metallib"
        VERBATIM
    )

    # Generate C header
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

# Internal: Compile native Metal shader to metallib (for handwritten .metal files)
function(_plume_compile_metal_impl TARGET_NAME SHADER_SOURCE OUTPUT_NAME)
    set(IR_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.ir")
    set(METALLIB_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.metallib")
    set(C_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.metal.c")
    set(H_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.metal.h")

    # Get deployment target for Metal compilation
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(METAL_VERSION_FLAG "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    else()
        set(METAL_VERSION_FLAG "")
    endif()

    # Compile Metal to IR
    add_custom_command(
        OUTPUT "${IR_OUTPUT}"
        COMMAND xcrun -sdk macosx metal ${METAL_VERSION_FLAG} -o "${IR_OUTPUT}" -c "${SHADER_SOURCE}"
        DEPENDS "${SHADER_SOURCE}"
        COMMENT "Compiling Metal shader ${OUTPUT_NAME} to IR"
        VERBATIM
    )

    # Link IR to metallib
    add_custom_command(
        OUTPUT "${METALLIB_OUTPUT}"
        COMMAND xcrun -sdk macosx metallib "${IR_OUTPUT}" -o "${METALLIB_OUTPUT}"
        DEPENDS "${IR_OUTPUT}"
        COMMENT "Linking ${OUTPUT_NAME} to metallib"
        VERBATIM
    )

    # Generate C header
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

# ============================================================================
# Public API
# ============================================================================

# Compile a shader and add it to a target
# Usage: plume_compile_shader(TARGET SOURCE TYPE OUTPUT_NAME ENTRY_POINT)
#   TARGET      - CMake target to add shader to
#   SOURCE      - Path to shader source file (.hlsl or .metal)
#   TYPE        - Shader type: vertex, pixel, compute, or ray
#   OUTPUT_NAME - Base name for output files (e.g., "mainVert")
#   ENTRY_POINT - Shader entry point function name (e.g., "VSMain")
function(plume_compile_shader TARGET_NAME SHADER_SOURCE SHADER_TYPE OUTPUT_NAME ENTRY_POINT)
    get_filename_component(SHADER_EXT "${SHADER_SOURCE}" EXT)

    if(SHADER_EXT MATCHES "\\.metal$")
        if(APPLE)
            _plume_compile_metal_impl(${TARGET_NAME} "${SHADER_SOURCE}" ${OUTPUT_NAME})
        endif()
    elseif(SHADER_EXT MATCHES "\\.hlsl$")
        # Always compile to SPIR-V
        _plume_compile_hlsl_impl(${TARGET_NAME} "${SHADER_SOURCE}" ${SHADER_TYPE} ${OUTPUT_NAME} "spirv" ${ENTRY_POINT})

        # Compile to DXIL on Windows
        if(WIN32)
            _plume_compile_hlsl_impl(${TARGET_NAME} "${SHADER_SOURCE}" ${SHADER_TYPE} ${OUTPUT_NAME} "dxil" ${ENTRY_POINT})
        endif()

        # Compile SPIR-V to Metal on Apple (if spirv-cross is available)
        if(APPLE AND TARGET plume_spirv_cross_msl)
            set(SPIRV_FILE "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.hlsl.spv")
            _plume_compile_spirv_to_metal_impl(${TARGET_NAME} "${SPIRV_FILE}" ${OUTPUT_NAME})
        endif()
    else()
        message(WARNING "Unsupported shader extension '${SHADER_EXT}' for ${SHADER_SOURCE}. Use .hlsl or .metal")
    endif()
endfunction()

# Compile a vertex shader
function(plume_compile_vertex_shader TARGET_NAME SHADER_SOURCE OUTPUT_NAME ENTRY_POINT)
    plume_compile_shader(${TARGET_NAME} "${SHADER_SOURCE}" "vertex" ${OUTPUT_NAME} ${ENTRY_POINT})
endfunction()

# Compile a pixel/fragment shader
function(plume_compile_pixel_shader TARGET_NAME SHADER_SOURCE OUTPUT_NAME ENTRY_POINT)
    plume_compile_shader(${TARGET_NAME} "${SHADER_SOURCE}" "pixel" ${OUTPUT_NAME} ${ENTRY_POINT})
endfunction()

# Compile a compute shader
function(plume_compile_compute_shader TARGET_NAME SHADER_SOURCE OUTPUT_NAME ENTRY_POINT)
    plume_compile_shader(${TARGET_NAME} "${SHADER_SOURCE}" "compute" ${OUTPUT_NAME} ${ENTRY_POINT})
endfunction()

# Compile a ray tracing shader
function(plume_compile_ray_shader TARGET_NAME SHADER_SOURCE OUTPUT_NAME ENTRY_POINT)
    plume_compile_shader(${TARGET_NAME} "${SHADER_SOURCE}" "ray" ${OUTPUT_NAME} ${ENTRY_POINT})
endfunction()

# Compile a native Metal shader (Apple only, no-op on other platforms)
# Use this for handwritten .metal files, not for cross-compiled HLSL
# Usage: plume_compile_metal_shader(TARGET SOURCE OUTPUT_NAME)
function(plume_compile_metal_shader TARGET_NAME SHADER_SOURCE OUTPUT_NAME)
    if(APPLE)
        _plume_compile_metal_impl(${TARGET_NAME} "${SHADER_SOURCE}" ${OUTPUT_NAME})
    endif()
endfunction()
