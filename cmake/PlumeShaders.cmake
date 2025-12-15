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
# Advanced usage with extra options:
#   plume_compile_pixel_shader(my_target shaders/main.frag.hlsl mainFrag PSMain
#       EXTRA_ARGS -D MULTISAMPLING -O0
#       INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/src
#       SHADER_MODEL 6_3)
#
#   # Spec constants mode (SPIRV + Metal only, no DXIL):
#   plume_compile_pixel_shader(my_target shaders/main.frag.hlsl mainFrag PSMain SPEC_CONSTANTS)
#
#   # Library shader (DXIL only, Windows):
#   plume_compile_library_shader(my_target shaders/lib.hlsl libShader)
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
# Optional args: INCLUDE_DIRS, EXTRA_ARGS, SHADER_MODEL, OUTPUT_DIR
function(_plume_compile_hlsl_impl TARGET_NAME SHADER_SOURCE SHADER_TYPE OUTPUT_NAME OUTPUT_FORMAT ENTRY_POINT)
    # Parse optional arguments
    cmake_parse_arguments(PARSE_ARGV 6 ARG "" "SHADER_MODEL;OUTPUT_DIR" "INCLUDE_DIRS;EXTRA_ARGS")

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

    # Use custom output directory if provided
    if(ARG_OUTPUT_DIR)
        set(OUT_DIR "${ARG_OUTPUT_DIR}")
    else()
        set(OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    endif()
    file(MAKE_DIRECTORY "${OUT_DIR}")

    set(SHADER_OUTPUT "${OUT_DIR}/${OUTPUT_NAME}.hlsl.${OUTPUT_EXT}")
    set(C_OUTPUT "${OUT_DIR}/${OUTPUT_NAME}.hlsl.${OUTPUT_FORMAT}.c")
    set(H_OUTPUT "${OUT_DIR}/${OUTPUT_NAME}.hlsl.${OUTPUT_FORMAT}.h")

    # Use provided shader model or default to 6_0
    if(ARG_SHADER_MODEL)
        set(SM_VERSION "${ARG_SHADER_MODEL}")
    else()
        set(SM_VERSION "6_0")
    endif()

    # Determine shader profile and type-specific args
    if(SHADER_TYPE STREQUAL "vertex")
        set(SHADER_PROFILE "vs_${SM_VERSION}")
        set(DXC_TYPE_ARGS "-fvk-invert-y")
    elseif(SHADER_TYPE STREQUAL "pixel" OR SHADER_TYPE STREQUAL "fragment")
        set(SHADER_PROFILE "ps_${SM_VERSION}")
        set(DXC_TYPE_ARGS "")
    elseif(SHADER_TYPE STREQUAL "compute")
        set(SHADER_PROFILE "cs_${SM_VERSION}")
        set(DXC_TYPE_ARGS "")
    elseif(SHADER_TYPE STREQUAL "geometry")
        set(SHADER_PROFILE "gs_${SM_VERSION}")
        set(DXC_TYPE_ARGS "")
    elseif(SHADER_TYPE STREQUAL "ray")
        set(SHADER_PROFILE "lib_6_3")
        set(DXC_TYPE_ARGS ${PLUME_DXC_RT_OPTS})
    elseif(SHADER_TYPE STREQUAL "library")
        set(SHADER_PROFILE "lib_${SM_VERSION}")
        set(DXC_TYPE_ARGS "-D;LIBRARY")
    else()
        message(FATAL_ERROR "Unknown shader type: ${SHADER_TYPE}. Use: vertex, pixel/fragment, compute, geometry, ray, or library")
    endif()

    # Build include directory flags
    set(INCLUDE_FLAGS "")
    foreach(INCLUDE_DIR ${ARG_INCLUDE_DIRS})
        list(APPEND INCLUDE_FLAGS "-I${INCLUDE_DIR}")
    endforeach()

    set(BLOB_NAME "${OUTPUT_NAME}Blob${BLOB_SUFFIX}")

    # Build entry point args (library shaders don't have entry points)
    if(ENTRY_POINT STREQUAL "")
        set(ENTRY_POINT_ARGS "")
    else()
        set(ENTRY_POINT_ARGS "-E" "${ENTRY_POINT}")
    endif()

    # Compile using DXC
    add_custom_command(
        OUTPUT "${SHADER_OUTPUT}"
        COMMAND ${DXC_CMD} ${PLUME_DXC_COMMON_OPTS} ${INCLUDE_FLAGS} ${ENTRY_POINT_ARGS} -T ${SHADER_PROFILE}
                ${FORMAT_FLAGS} ${DXC_TYPE_ARGS} ${ARG_EXTRA_ARGS} -Fo "${SHADER_OUTPUT}" "${SHADER_SOURCE}"
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
# Optional args: OUTPUT_DIR
# Note: For HLSL sources, OUTPUT_NAME should include .hlsl suffix for proper naming
function(_plume_compile_spirv_to_metal_impl TARGET_NAME SPIRV_FILE OUTPUT_NAME)
    cmake_parse_arguments(PARSE_ARGV 3 ARG "" "OUTPUT_DIR" "")

    # Use custom output directory if provided
    if(ARG_OUTPUT_DIR)
        set(OUT_DIR "${ARG_OUTPUT_DIR}")
    else()
        set(OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    endif()
    file(MAKE_DIRECTORY "${OUT_DIR}")

    # Use OUTPUT_NAME.hlsl for naming to match RT64's expected paths
    set(METAL_SOURCE "${OUT_DIR}/${OUTPUT_NAME}.hlsl.metal")
    set(IR_OUTPUT "${OUT_DIR}/${OUTPUT_NAME}.hlsl.ir")
    set(METALLIB_OUTPUT "${OUT_DIR}/${OUTPUT_NAME}.hlsl.metallib")
    set(C_OUTPUT "${OUT_DIR}/${OUTPUT_NAME}.hlsl.metal.c")
    set(H_OUTPUT "${OUT_DIR}/${OUTPUT_NAME}.hlsl.metal.h")

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
# Usage: plume_compile_shader(TARGET SOURCE TYPE OUTPUT_NAME ENTRY_POINT [options])
#   TARGET      - CMake target to add shader to
#   SOURCE      - Path to shader source file (.hlsl or .metal)
#   TYPE        - Shader type: vertex, pixel, compute, geometry, ray, or library
#   OUTPUT_NAME - Base name for output files (e.g., "mainVert")
#   ENTRY_POINT - Shader entry point function name (e.g., "VSMain")
#
# Options:
#   SPEC_CONSTANTS    - Only compile SPIRV + Metal (no DXIL), for specialization constants
#   SHADER_MODEL <ver> - Shader model version (default: 6_0)
#   INCLUDE_DIRS <dirs> - Additional include directories for DXC
#   EXTRA_ARGS <args>  - Additional DXC arguments (e.g., -D MULTISAMPLING -O0)
#   OUTPUT_DIR <dir>   - Custom output directory (default: ${CMAKE_BINARY_DIR}/shaders)
function(plume_compile_shader TARGET_NAME SHADER_SOURCE SHADER_TYPE OUTPUT_NAME ENTRY_POINT)
    # Parse optional arguments
    cmake_parse_arguments(ARG "SPEC_CONSTANTS" "SHADER_MODEL;OUTPUT_DIR" "INCLUDE_DIRS;EXTRA_ARGS" ${ARGN})

    get_filename_component(SHADER_EXT "${SHADER_SOURCE}" EXT)

    if(SHADER_EXT MATCHES "\\.metal$")
        if(APPLE)
            _plume_compile_metal_impl(${TARGET_NAME} "${SHADER_SOURCE}" ${OUTPUT_NAME})
        endif()
    elseif(SHADER_EXT MATCHES "\\.hlsl$")
        # Build optional args to pass to impl
        set(IMPL_ARGS "")
        if(ARG_SHADER_MODEL)
            list(APPEND IMPL_ARGS SHADER_MODEL "${ARG_SHADER_MODEL}")
        endif()
        if(ARG_INCLUDE_DIRS)
            list(APPEND IMPL_ARGS INCLUDE_DIRS ${ARG_INCLUDE_DIRS})
        endif()
        if(ARG_EXTRA_ARGS)
            list(APPEND IMPL_ARGS EXTRA_ARGS ${ARG_EXTRA_ARGS})
        endif()
        if(ARG_OUTPUT_DIR)
            list(APPEND IMPL_ARGS OUTPUT_DIR "${ARG_OUTPUT_DIR}")
            set(OUT_DIR "${ARG_OUTPUT_DIR}")
        else()
            set(OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
        endif()

        # Always compile to SPIR-V
        _plume_compile_hlsl_impl(${TARGET_NAME} "${SHADER_SOURCE}" ${SHADER_TYPE} ${OUTPUT_NAME} "spirv" ${ENTRY_POINT} ${IMPL_ARGS})

        # Compile to DXIL on Windows (unless SPEC_CONSTANTS mode)
        if(WIN32 AND NOT ARG_SPEC_CONSTANTS)
            _plume_compile_hlsl_impl(${TARGET_NAME} "${SHADER_SOURCE}" ${SHADER_TYPE} ${OUTPUT_NAME} "dxil" ${ENTRY_POINT} ${IMPL_ARGS})
        endif()

        # Compile SPIR-V to Metal on Apple (if spirv-cross is available)
        if(APPLE AND TARGET plume_spirv_cross_msl)
            set(SPIRV_FILE "${OUT_DIR}/${OUTPUT_NAME}.hlsl.spv")
            _plume_compile_spirv_to_metal_impl(${TARGET_NAME} "${SPIRV_FILE}" ${OUTPUT_NAME} OUTPUT_DIR "${OUT_DIR}")
        endif()
    else()
        message(WARNING "Unsupported shader extension '${SHADER_EXT}' for ${SHADER_SOURCE}. Use .hlsl or .metal")
    endif()
endfunction()

# Compile a vertex shader
# Usage: plume_compile_vertex_shader(TARGET SOURCE OUTPUT_NAME ENTRY_POINT [options])
# Options: SPEC_CONSTANTS, SHADER_MODEL, INCLUDE_DIRS, EXTRA_ARGS (see plume_compile_shader)
function(plume_compile_vertex_shader TARGET_NAME SHADER_SOURCE OUTPUT_NAME ENTRY_POINT)
    plume_compile_shader(${TARGET_NAME} "${SHADER_SOURCE}" "vertex" ${OUTPUT_NAME} ${ENTRY_POINT} ${ARGN})
endfunction()

# Compile a pixel/fragment shader
# Usage: plume_compile_pixel_shader(TARGET SOURCE OUTPUT_NAME ENTRY_POINT [options])
# Options: SPEC_CONSTANTS, SHADER_MODEL, INCLUDE_DIRS, EXTRA_ARGS (see plume_compile_shader)
function(plume_compile_pixel_shader TARGET_NAME SHADER_SOURCE OUTPUT_NAME ENTRY_POINT)
    plume_compile_shader(${TARGET_NAME} "${SHADER_SOURCE}" "pixel" ${OUTPUT_NAME} ${ENTRY_POINT} ${ARGN})
endfunction()

# Compile a compute shader
# Usage: plume_compile_compute_shader(TARGET SOURCE OUTPUT_NAME ENTRY_POINT [options])
# Options: SPEC_CONSTANTS, SHADER_MODEL, INCLUDE_DIRS, EXTRA_ARGS (see plume_compile_shader)
function(plume_compile_compute_shader TARGET_NAME SHADER_SOURCE OUTPUT_NAME ENTRY_POINT)
    plume_compile_shader(${TARGET_NAME} "${SHADER_SOURCE}" "compute" ${OUTPUT_NAME} ${ENTRY_POINT} ${ARGN})
endfunction()

# Compile a geometry shader
# Usage: plume_compile_geometry_shader(TARGET SOURCE OUTPUT_NAME ENTRY_POINT [options])
# Options: SPEC_CONSTANTS, SHADER_MODEL, INCLUDE_DIRS, EXTRA_ARGS (see plume_compile_shader)
function(plume_compile_geometry_shader TARGET_NAME SHADER_SOURCE OUTPUT_NAME ENTRY_POINT)
    plume_compile_shader(${TARGET_NAME} "${SHADER_SOURCE}" "geometry" ${OUTPUT_NAME} ${ENTRY_POINT} ${ARGN})
endfunction()

# Compile a ray tracing shader
# Usage: plume_compile_ray_shader(TARGET SOURCE OUTPUT_NAME ENTRY_POINT [options])
# Options: SHADER_MODEL, INCLUDE_DIRS, EXTRA_ARGS (see plume_compile_shader)
function(plume_compile_ray_shader TARGET_NAME SHADER_SOURCE OUTPUT_NAME ENTRY_POINT)
    plume_compile_shader(${TARGET_NAME} "${SHADER_SOURCE}" "ray" ${OUTPUT_NAME} ${ENTRY_POINT} ${ARGN})
endfunction()

# Compile a library shader (DXIL only, Windows)
# Usage: plume_compile_library_shader(TARGET SOURCE OUTPUT_NAME [options])
# Options: SHADER_MODEL, INCLUDE_DIRS, EXTRA_ARGS, OUTPUT_DIR
function(plume_compile_library_shader TARGET_NAME SHADER_SOURCE OUTPUT_NAME)
    # Parse optional arguments
    cmake_parse_arguments(ARG "" "SHADER_MODEL;OUTPUT_DIR" "INCLUDE_DIRS;EXTRA_ARGS" ${ARGN})

    if(NOT WIN32)
        return()
    endif()

    # Build optional args to pass to impl
    set(IMPL_ARGS "")
    if(ARG_SHADER_MODEL)
        list(APPEND IMPL_ARGS SHADER_MODEL "${ARG_SHADER_MODEL}")
    else()
        list(APPEND IMPL_ARGS SHADER_MODEL "6_3")  # Library shaders default to 6_3
    endif()
    if(ARG_INCLUDE_DIRS)
        list(APPEND IMPL_ARGS INCLUDE_DIRS ${ARG_INCLUDE_DIRS})
    endif()
    if(ARG_EXTRA_ARGS)
        list(APPEND IMPL_ARGS EXTRA_ARGS ${ARG_EXTRA_ARGS})
    endif()
    if(ARG_OUTPUT_DIR)
        list(APPEND IMPL_ARGS OUTPUT_DIR "${ARG_OUTPUT_DIR}")
    endif()

    # Library shaders don't have an entry point - use empty string
    _plume_compile_hlsl_impl(${TARGET_NAME} "${SHADER_SOURCE}" "library" ${OUTPUT_NAME} "dxil" "" ${IMPL_ARGS})
endfunction()

# Compile a native Metal shader (Apple only, no-op on other platforms)
# Use this for handwritten .metal files, not for cross-compiled HLSL
# Usage: plume_compile_metal_shader(TARGET SOURCE OUTPUT_NAME)
function(plume_compile_metal_shader TARGET_NAME SHADER_SOURCE OUTPUT_NAME)
    if(APPLE)
        _plume_compile_metal_impl(${TARGET_NAME} "${SHADER_SOURCE}" ${OUTPUT_NAME})
    endif()
endfunction()

# Preprocess a shader header file and embed it as text
# Useful for runtime shader compilation where you need the preprocessed source
# Usage: plume_preprocess_shader(TARGET SOURCE OUTPUT_NAME [INCLUDE_DIRS dirs] [OUTPUT_DIR dir] [VAR_NAME name])
#   VAR_NAME - Optional variable name for the embedded data (defaults to OUTPUT_NAME)
function(plume_preprocess_shader TARGET_NAME SHADER_SOURCE OUTPUT_NAME)
    cmake_parse_arguments(ARG "" "OUTPUT_DIR;VAR_NAME" "INCLUDE_DIRS" ${ARGN})

    get_filename_component(SHADER_NAME "${SHADER_SOURCE}" NAME)

    # Use custom output directory if provided
    if(ARG_OUTPUT_DIR)
        set(OUT_DIR "${ARG_OUTPUT_DIR}")
    else()
        set(OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    endif()
    file(MAKE_DIRECTORY "${OUT_DIR}")

    # Variable name for embedded data (defaults to OUTPUT_NAME)
    if(ARG_VAR_NAME)
        set(VAR_NAME "${ARG_VAR_NAME}")
    else()
        set(VAR_NAME "${OUTPUT_NAME}")
    endif()

    set(PREPROCESSED_OUTPUT "${OUT_DIR}/${OUTPUT_NAME}.rw")
    set(C_OUTPUT "${OUT_DIR}/${OUTPUT_NAME}.rw.c")
    set(H_OUTPUT "${OUT_DIR}/${OUTPUT_NAME}.rw.h")

    # Build include directory flags
    set(INCLUDE_FLAGS "")
    foreach(INCLUDE_DIR ${ARG_INCLUDE_DIRS})
        list(APPEND INCLUDE_FLAGS "-I${INCLUDE_DIR}")
    endforeach()

    # Preprocess using C preprocessor
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            add_custom_command(
                OUTPUT "${PREPROCESSED_OUTPUT}"
                COMMAND clang -x c -E -P "${SHADER_SOURCE}" -o "${PREPROCESSED_OUTPUT}" ${INCLUDE_FLAGS}
                DEPENDS "${SHADER_SOURCE}"
                COMMENT "Preprocessing shader ${SHADER_NAME}"
                VERBATIM
            )
        else()
            add_custom_command(
                OUTPUT "${PREPROCESSED_OUTPUT}"
                COMMAND ${CMAKE_CXX_COMPILER} /Zs /EP "${SHADER_SOURCE}" ${INCLUDE_FLAGS} > "${PREPROCESSED_OUTPUT}"
                DEPENDS "${SHADER_SOURCE}"
                COMMENT "Preprocessing shader ${SHADER_NAME}"
                VERBATIM
            )
        endif()
    else()
        add_custom_command(
            OUTPUT "${PREPROCESSED_OUTPUT}"
            COMMAND ${CMAKE_CXX_COMPILER} -x c -E -P "${SHADER_SOURCE}" -o "${PREPROCESSED_OUTPUT}" ${INCLUDE_FLAGS}
            DEPENDS "${SHADER_SOURCE}"
            COMMENT "Preprocessing shader ${SHADER_NAME}"
            VERBATIM
        )
    endif()

    # Generate C header with text content (use --text for char type compatibility)
    add_custom_command(
        OUTPUT "${C_OUTPUT}" "${H_OUTPUT}"
        COMMAND plume_file_to_c "${PREPROCESSED_OUTPUT}" "${VAR_NAME}Text" "${C_OUTPUT}" "${H_OUTPUT}" --text
        DEPENDS "${PREPROCESSED_OUTPUT}" plume_file_to_c
        COMMENT "Generating C header for preprocessed shader ${OUTPUT_NAME}"
        VERBATIM
    )

    target_sources(${TARGET_NAME} PRIVATE "${C_OUTPUT}")
    target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_BINARY_DIR}")
endfunction()
