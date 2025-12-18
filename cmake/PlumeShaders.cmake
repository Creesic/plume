# PlumeShaders.cmake
# Public shader compilation API for Plume RHI
#
# Architecture:
#   Layer 1 (Primitives): Single-operation functions for each tool
#   Layer 2 (Pipelines):  Platform-aware composition of primitives
#   Layer 3 (Public API): User-facing functions with nice defaults
#
# Usage:
#   include(path/to/plume/cmake/PlumeShaders.cmake)
#   plume_shaders_init()
#
#   plume_compile_vertex_shader(my_target shaders/main.vert.hlsl mainVert VSMain)
#   plume_compile_pixel_shader(my_target shaders/main.frag.hlsl mainFrag PSMain)
#   plume_compile_compute_shader(my_target shaders/compute.hlsl computeShader CSMain)
#   plume_compile_rt_shader(my_target shaders/rt.hlsl rtShaders)
#
# Output:
#   Stage shaders compile to:
#     - SPIR-V (all platforms): {OUTPUT_NAME}BlobSPIRV in shaders/{OUTPUT_NAME}.hlsl.spirv.h
#     - DXIL (Windows only): {OUTPUT_NAME}BlobDXIL in shaders/{OUTPUT_NAME}.hlsl.dxil.h
#     - Metal (Apple only): {OUTPUT_NAME}BlobMSL in shaders/{OUTPUT_NAME}.hlsl.metal.h
#
#   RT library shaders compile to:
#     - Windows: {OUTPUT_NAME}BlobDXIL in shaders/{OUTPUT_NAME}.hlsl.dxil.h
#     - Linux:   {OUTPUT_NAME}BlobSPIRV in shaders/{OUTPUT_NAME}.hlsl.spirv.h
#     - Apple:   {OUTPUT_NAME}BlobMetalLib in shaders/{OUTPUT_NAME}.metallib.h

include("${CMAKE_CURRENT_LIST_DIR}/modules/PlumeFileToC.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/modules/PlumeDXC.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/modules/PlumeSpirvCross.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/modules/PlumeRootSignature.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/modules/PlumeCombineRTMetallibs.cmake")

# ============================================================================
# Initialization
# ============================================================================

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

    # Build helper tools (Apple RT shaders)
    if(APPLE)
        plume_build_generate_root_signature()
        plume_build_combine_rt_metallibs()
    endif()

    plume_build_file_to_c()

    # Create output directory
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")
endfunction()

# ============================================================================
# Layer 1: Primitives - Single-operation functions for each tool
# ============================================================================

# Run DXC to compile HLSL to SPIRV or DXIL
# Arguments:
#   TARGET       - CMake target (for dependencies)
#   SOURCE       - HLSL source file
#   OUTPUT       - Output binary file path
#   PROFILE      - Shader profile (vs_6_0, ps_6_0, lib_6_3, etc.)
#   ENTRY_POINT  - Entry point name (empty string for libraries)
#   FORMAT       - Output format: "spirv" or "dxil"
# Options:
#   SPIRV_RT     - Add SPIRV ray tracing extensions (vulkan1.1spirv1.4 + SPV_KHR_ray_tracing)
#   INVERT_Y     - Add -fvk-invert-y for vertex shaders
#   INCLUDE_DIRS - Additional include directories
#   EXTRA_ARGS   - Additional DXC arguments
function(_plume_dxc TARGET SOURCE OUTPUT PROFILE ENTRY_POINT FORMAT)
    cmake_parse_arguments(ARG "SPIRV_RT;INVERT_Y" "" "INCLUDE_DIRS;EXTRA_ARGS" ${ARGN})

    plume_get_dxc_command(DXC_CMD)

    # Base format flags
    if(FORMAT STREQUAL "spirv")
        if(ARG_SPIRV_RT)
            # SPIRV with ray tracing extensions
            set(FORMAT_FLAGS "-spirv" "-fspv-target-env=vulkan1.1spirv1.4"
                "-fspv-extension=SPV_KHR_ray_tracing"
                "-fspv-extension=SPV_EXT_descriptor_indexing"
                "-fvk-use-dx-layout")
        else()
            # Standard SPIRV
            set(FORMAT_FLAGS ${PLUME_DXC_SPV_OPTS})
        endif()
    elseif(FORMAT STREQUAL "dxil")
        set(FORMAT_FLAGS ${PLUME_DXC_DXIL_OPTS})
    else()
        message(FATAL_ERROR "_plume_dxc: Unknown format '${FORMAT}'. Use 'spirv' or 'dxil'.")
    endif()

    # Type-specific flags
    set(TYPE_FLAGS "")
    if(ARG_INVERT_Y AND FORMAT STREQUAL "spirv")
        list(APPEND TYPE_FLAGS "-fvk-invert-y")
    endif()

    # Include directories
    set(INCLUDE_FLAGS "")
    foreach(DIR ${ARG_INCLUDE_DIRS})
        list(APPEND INCLUDE_FLAGS "-I${DIR}")
    endforeach()

    # Entry point args (libraries don't have entry points)
    if(ENTRY_POINT STREQUAL "")
        set(ENTRY_ARGS "")
    else()
        set(ENTRY_ARGS "-E" "${ENTRY_POINT}")
    endif()

    add_custom_command(
        OUTPUT "${OUTPUT}"
        COMMAND ${DXC_CMD}
            ${PLUME_DXC_COMMON_OPTS}
            ${INCLUDE_FLAGS}
            ${ENTRY_ARGS}
            -T ${PROFILE}
            ${FORMAT_FLAGS}
            ${TYPE_FLAGS}
            ${ARG_EXTRA_ARGS}
            -Fo "${OUTPUT}"
            "${SOURCE}"
        DEPENDS "${SOURCE}"
        COMMENT "DXC: ${SOURCE} -> ${FORMAT}"
        VERBATIM
    )
endfunction()

# Run spirv-cross to convert SPIRV to Metal source
# Arguments:
#   TARGET    - CMake target (for dependencies)
#   INPUT     - SPIRV binary file
#   OUTPUT    - Metal source file path
function(_plume_spirv_cross TARGET INPUT OUTPUT)
    add_custom_command(
        OUTPUT "${OUTPUT}"
        COMMAND plume_spirv_cross_msl "${INPUT}" "${OUTPUT}"
        DEPENDS "${INPUT}" plume_spirv_cross_msl
        COMMENT "SPIRV-Cross: ${INPUT} -> Metal"
        VERBATIM
    )
endfunction()

# Run Metal compiler to create metallib from source
# Arguments:
#   TARGET    - CMake target (for dependencies)
#   INPUT     - Metal source file
#   OUTPUT    - Metallib output file
function(_plume_metal_compile TARGET INPUT OUTPUT)
    get_filename_component(OUTPUT_DIR "${OUTPUT}" DIRECTORY)
    get_filename_component(OUTPUT_NAME "${OUTPUT}" NAME_WE)
    set(IR_FILE "${OUTPUT_DIR}/${OUTPUT_NAME}.ir")

    # Get deployment target
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(VERSION_FLAG "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    else()
        set(VERSION_FLAG "")
    endif()

    # Compile to IR
    add_custom_command(
        OUTPUT "${IR_FILE}"
        COMMAND xcrun -sdk macosx metal ${VERSION_FLAG} -o "${IR_FILE}" -c "${INPUT}"
        DEPENDS "${INPUT}"
        COMMENT "Metal: ${INPUT} -> IR"
        VERBATIM
    )

    # Link to metallib
    add_custom_command(
        OUTPUT "${OUTPUT}"
        COMMAND xcrun -sdk macosx metallib "${IR_FILE}" -o "${OUTPUT}"
        DEPENDS "${IR_FILE}"
        COMMENT "Metallib: ${IR_FILE} -> ${OUTPUT}"
        VERBATIM
    )
endfunction()

# Run metal-shaderconverter to convert DXIL to Metal (for RT shaders)
# Arguments:
#   TARGET         - CMake target (for dependencies)
#   INPUT          - DXIL binary file
#   OUTPUT         - Metallib output file
#   ROOT_SIGNATURE - Root signature JSON file
# Options:
#   SYNTHESIZE_DISPATCH - Generate indirect ray dispatch kernel
function(_plume_metal_shader_converter TARGET INPUT OUTPUT ROOT_SIGNATURE)
    cmake_parse_arguments(ARG "SYNTHESIZE_DISPATCH" "" "" ${ARGN})

    find_program(METAL_SHADER_CONVERTER metal-shaderconverter
        PATHS /usr/local/bin ENV PATH
        DOC "Apple Metal Shader Converter"
    )
    if(NOT METAL_SHADER_CONVERTER)
        message(FATAL_ERROR "metal-shaderconverter not found. Install from: https://developer.apple.com/metal/shader-converter/")
    endif()

    set(SYNTH_FLAGS "")
    if(ARG_SYNTHESIZE_DISPATCH)
        set(SYNTH_FLAGS "--synthesize-indirect-ray-dispatch" "--synthesize-indirect-intersection-function")
    endif()

    add_custom_command(
        OUTPUT "${OUTPUT}"
        COMMAND ${METAL_SHADER_CONVERTER}
            "${INPUT}"
            -o "${OUTPUT}"
            --deployment-os=macOS
            --minimum-gpu-family=Metal3
            --root-signature=${ROOT_SIGNATURE}
            ${SYNTH_FLAGS}
        DEPENDS "${INPUT}" "${ROOT_SIGNATURE}"
        COMMENT "MetalShaderConverter: ${INPUT} -> ${OUTPUT}"
        VERBATIM
    )
endfunction()

# Embed binary file as C header
# Arguments:
#   TARGET    - CMake target to add source to
#   INPUT     - Binary file to embed
#   VAR_NAME  - C variable name for the data
#   C_OUTPUT  - Output .c file path
#   H_OUTPUT  - Output .h file path
# Options:
#   TEXT      - Embed as text (char[]) instead of binary (uint8_t[])
function(_plume_embed TARGET INPUT VAR_NAME C_OUTPUT H_OUTPUT)
    cmake_parse_arguments(ARG "TEXT" "" "" ${ARGN})

    if(ARG_TEXT)
        set(TEXT_FLAG "--text")
    else()
        set(TEXT_FLAG "")
    endif()

    add_custom_command(
        OUTPUT "${C_OUTPUT}" "${H_OUTPUT}"
        COMMAND plume_file_to_c "${INPUT}" "${VAR_NAME}" "${C_OUTPUT}" "${H_OUTPUT}" ${TEXT_FLAG}
        DEPENDS "${INPUT}" plume_file_to_c
        COMMENT "Embed: ${INPUT} -> ${VAR_NAME}"
        VERBATIM
    )

    target_sources(${TARGET} PRIVATE "${C_OUTPUT}")
    target_include_directories(${TARGET} PRIVATE "${CMAKE_BINARY_DIR}")
endfunction()

# Generate root signature JSON from HLSL shader reflection (for Metal RT)
# Arguments:
#   TARGET    - CMake target (for dependencies)
#   SOURCE    - HLSL source file
#   OUTPUT    - Root signature JSON output file
# Options:
#   INCLUDE_DIRS - Additional include directories
#   EXTRA_ARGS   - Additional DXC arguments
function(_plume_generate_root_signature TARGET SOURCE OUTPUT)
    cmake_parse_arguments(ARG "" "" "INCLUDE_DIRS;EXTRA_ARGS" ${ARGN})

    plume_get_dxc_command(DXC_CMD)

    get_filename_component(OUTPUT_DIR "${OUTPUT}" DIRECTORY)
    get_filename_component(OUTPUT_NAME "${OUTPUT}" NAME_WE)
    set(REFLECTION_FILE "${OUTPUT_DIR}/${OUTPUT_NAME}_reflection.txt")

    # Include directories
    set(INCLUDE_FLAGS "")
    foreach(DIR ${ARG_INCLUDE_DIRS})
        list(APPEND INCLUDE_FLAGS "-I${DIR}")
    endforeach()

    add_custom_command(
        OUTPUT "${OUTPUT}"
        COMMAND ${DXC_CMD}
            ${PLUME_DXC_COMMON_OPTS}
            ${INCLUDE_FLAGS}
            -T lib_6_3
            -D RT_SHADER
            ${ARG_EXTRA_ARGS}
            -Fc "${REFLECTION_FILE}"
            "${SOURCE}"
        COMMAND plume_generate_root_signature "${REFLECTION_FILE}" "${OUTPUT}"
        DEPENDS "${SOURCE}" plume_generate_root_signature
        COMMENT "RootSig: ${SOURCE}"
        VERBATIM
    )
endfunction()

# ============================================================================
# Layer 2: Pipelines - Platform-aware composition of primitives
# ============================================================================

# Get shader profile from stage type
function(_plume_get_profile TYPE SHADER_MODEL OUT_VAR)
    if(TYPE STREQUAL "vertex")
        set(${OUT_VAR} "vs_${SHADER_MODEL}" PARENT_SCOPE)
    elseif(TYPE STREQUAL "pixel" OR TYPE STREQUAL "fragment")
        set(${OUT_VAR} "ps_${SHADER_MODEL}" PARENT_SCOPE)
    elseif(TYPE STREQUAL "compute")
        set(${OUT_VAR} "cs_${SHADER_MODEL}" PARENT_SCOPE)
    elseif(TYPE STREQUAL "geometry")
        set(${OUT_VAR} "gs_${SHADER_MODEL}" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Unknown shader type: ${TYPE}")
    endif()
endfunction()

# Compile a stage shader (vertex, pixel, compute, geometry) to all platform formats
# This is the main pipeline for regular shaders.
#
# Arguments:
#   TARGET      - CMake target to add shader to
#   SOURCE      - HLSL source file
#   TYPE        - Shader type: vertex, pixel, compute, geometry
#   OUTPUT_NAME - Base name for output files
#   ENTRY_POINT - Shader entry point function
# Options:
#   SPEC_CONSTANTS - Skip DXIL, only SPIRV + Metal (for specialization constants)
#   SHADER_MODEL   - Shader model version (default: 6_0)
#   INCLUDE_DIRS   - Additional include directories
#   EXTRA_ARGS     - Additional DXC arguments
#   OUTPUT_DIR     - Custom output directory
function(_plume_compile_stage_shader TARGET SOURCE TYPE OUTPUT_NAME ENTRY_POINT)
    cmake_parse_arguments(ARG "SPEC_CONSTANTS" "SHADER_MODEL;OUTPUT_DIR" "INCLUDE_DIRS;EXTRA_ARGS" ${ARGN})

    # Defaults
    if(NOT ARG_SHADER_MODEL)
        set(ARG_SHADER_MODEL "6_0")
    endif()
    if(ARG_OUTPUT_DIR)
        set(OUT_DIR "${ARG_OUTPUT_DIR}")
    else()
        set(OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    endif()
    file(MAKE_DIRECTORY "${OUT_DIR}")

    # Get profile
    _plume_get_profile(${TYPE} ${ARG_SHADER_MODEL} PROFILE)

    # Type-specific flags
    set(DXC_OPTS "")
    if(TYPE STREQUAL "vertex")
        list(APPEND DXC_OPTS INVERT_Y)
    endif()
    if(ARG_INCLUDE_DIRS)
        list(APPEND DXC_OPTS INCLUDE_DIRS ${ARG_INCLUDE_DIRS})
    endif()
    if(ARG_EXTRA_ARGS)
        list(APPEND DXC_OPTS EXTRA_ARGS ${ARG_EXTRA_ARGS})
    endif()

    # === SPIRV (always) ===
    set(SPIRV_FILE "${OUT_DIR}/${OUTPUT_NAME}.hlsl.spv")
    _plume_dxc(${TARGET} "${SOURCE}" "${SPIRV_FILE}" ${PROFILE} ${ENTRY_POINT} "spirv" ${DXC_OPTS})
    _plume_embed(${TARGET} "${SPIRV_FILE}" "${OUTPUT_NAME}BlobSPIRV"
        "${OUT_DIR}/${OUTPUT_NAME}.hlsl.spirv.c"
        "${OUT_DIR}/${OUTPUT_NAME}.hlsl.spirv.h")

    # === DXIL (Windows, unless SPEC_CONSTANTS) ===
    if(WIN32 AND NOT ARG_SPEC_CONSTANTS)
        set(DXIL_FILE "${OUT_DIR}/${OUTPUT_NAME}.hlsl.dxil")
        _plume_dxc(${TARGET} "${SOURCE}" "${DXIL_FILE}" ${PROFILE} ${ENTRY_POINT} "dxil" ${DXC_OPTS})
        _plume_embed(${TARGET} "${DXIL_FILE}" "${OUTPUT_NAME}BlobDXIL"
            "${OUT_DIR}/${OUTPUT_NAME}.hlsl.dxil.c"
            "${OUT_DIR}/${OUTPUT_NAME}.hlsl.dxil.h")
    endif()

    # === Metal (Apple, via SPIRV-Cross) ===
    if(APPLE AND TARGET plume_spirv_cross_msl)
        set(METAL_SOURCE "${OUT_DIR}/${OUTPUT_NAME}.hlsl.metal")
        set(METALLIB_FILE "${OUT_DIR}/${OUTPUT_NAME}.hlsl.metallib")

        _plume_spirv_cross(${TARGET} "${SPIRV_FILE}" "${METAL_SOURCE}")
        _plume_metal_compile(${TARGET} "${METAL_SOURCE}" "${METALLIB_FILE}")
        _plume_embed(${TARGET} "${METALLIB_FILE}" "${OUTPUT_NAME}BlobMSL"
            "${OUT_DIR}/${OUTPUT_NAME}.hlsl.metal.c"
            "${OUT_DIR}/${OUTPUT_NAME}.hlsl.metal.h")
    endif()
endfunction()

# Compile a library shader (RT or general) for the current platform
# Libraries contain multiple exported functions without a single entry point.
#
# Arguments:
#   TARGET      - CMake target to add shader to
#   SOURCE      - HLSL source file
#   OUTPUT_NAME - Base name for output files
# Options:
#   RAYTRACING   - Enable ray tracing extensions for SPIRV
#   SHADER_MODEL - Shader model version (default: 6_3)
#   INCLUDE_DIRS - Additional include directories
#   EXTRA_ARGS   - Additional DXC arguments
#   OUTPUT_DIR   - Custom output directory
function(_plume_compile_library_shader_impl TARGET SOURCE OUTPUT_NAME)
    cmake_parse_arguments(ARG "RAYTRACING" "SHADER_MODEL;OUTPUT_DIR" "INCLUDE_DIRS;EXTRA_ARGS" ${ARGN})

    # Defaults
    if(NOT ARG_SHADER_MODEL)
        set(ARG_SHADER_MODEL "6_3")
    endif()
    if(ARG_OUTPUT_DIR)
        set(OUT_DIR "${ARG_OUTPUT_DIR}")
    else()
        set(OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    endif()
    file(MAKE_DIRECTORY "${OUT_DIR}")

    set(PROFILE "lib_${ARG_SHADER_MODEL}")

    # Common DXC options
    set(DXC_OPTS "")
    if(ARG_INCLUDE_DIRS)
        list(APPEND DXC_OPTS INCLUDE_DIRS ${ARG_INCLUDE_DIRS})
    endif()
    if(ARG_EXTRA_ARGS)
        list(APPEND DXC_OPTS EXTRA_ARGS ${ARG_EXTRA_ARGS})
    endif()

    if(WIN32)
        # === Windows: DXIL library (D3D12) ===
        set(DXIL_FILE "${OUT_DIR}/${OUTPUT_NAME}.hlsl.dxil")
        _plume_dxc(${TARGET} "${SOURCE}" "${DXIL_FILE}" ${PROFILE} "" "dxil" ${DXC_OPTS})
        _plume_embed(${TARGET} "${DXIL_FILE}" "${OUTPUT_NAME}BlobDXIL"
            "${OUT_DIR}/${OUTPUT_NAME}.hlsl.dxil.c"
            "${OUT_DIR}/${OUTPUT_NAME}.hlsl.dxil.h")

        # === Windows: SPIRV library (Vulkan) ===
        set(SPIRV_FILE "${OUT_DIR}/${OUTPUT_NAME}.hlsl.spv")
        if(ARG_RAYTRACING)
            list(APPEND DXC_OPTS SPIRV_RT)
        endif()
        _plume_dxc(${TARGET} "${SOURCE}" "${SPIRV_FILE}" ${PROFILE} "" "spirv" ${DXC_OPTS})
        _plume_embed(${TARGET} "${SPIRV_FILE}" "${OUTPUT_NAME}BlobSPIRV"
            "${OUT_DIR}/${OUTPUT_NAME}.hlsl.spirv.c"
            "${OUT_DIR}/${OUTPUT_NAME}.hlsl.spirv.h")

    elseif(APPLE)
        # === Apple: DXIL -> Metal via metal-shaderconverter ===
        # This path is for RT shaders; regular libraries don't make sense on Metal
        if(ARG_RAYTRACING)
            _plume_compile_rt_metal(${TARGET} "${SOURCE}" ${OUTPUT_NAME}
                OUTPUT_DIR "${OUT_DIR}"
                INCLUDE_DIRS ${ARG_INCLUDE_DIRS}
                EXTRA_ARGS ${ARG_EXTRA_ARGS})
        endif()

    else()
        # === Linux: SPIRV with optional RT extensions ===
        set(SPIRV_FILE "${OUT_DIR}/${OUTPUT_NAME}.hlsl.spv")
        if(ARG_RAYTRACING)
            list(APPEND DXC_OPTS SPIRV_RT)
        endif()
        _plume_dxc(${TARGET} "${SOURCE}" "${SPIRV_FILE}" ${PROFILE} "" "spirv" ${DXC_OPTS})
        _plume_embed(${TARGET} "${SPIRV_FILE}" "${OUTPUT_NAME}BlobSPIRV"
            "${OUT_DIR}/${OUTPUT_NAME}.hlsl.spirv.c"
            "${OUT_DIR}/${OUTPUT_NAME}.hlsl.spirv.h")
    endif()
endfunction()

# Compile RT shader to Metal (Apple only)
# Handles the complex Metal RT pipeline: DXIL -> root signature -> visible functions + dispatch
#
# Arguments:
#   TARGET      - CMake target to add shader to
#   SOURCE      - HLSL source file
#   OUTPUT_NAME - Base name for output files
# Options:
#   INCLUDE_DIRS - Additional include directories
#   EXTRA_ARGS   - Additional DXC arguments
#   OUTPUT_DIR   - Custom output directory
function(_plume_compile_rt_metal TARGET SOURCE OUTPUT_NAME)
    cmake_parse_arguments(ARG "" "OUTPUT_DIR" "INCLUDE_DIRS;EXTRA_ARGS" ${ARGN})

    if(NOT APPLE)
        return()
    endif()

    # Check for metal-shaderconverter
    find_program(METAL_SHADER_CONVERTER metal-shaderconverter
        PATHS /usr/local/bin ENV PATH
        DOC "Apple Metal Shader Converter"
    )
    if(NOT METAL_SHADER_CONVERTER)
        message(WARNING "metal-shaderconverter not found. RT shaders will not be compiled for Metal. "
                        "Install from: https://developer.apple.com/metal/shader-converter/")
        return()
    endif()

    if(ARG_OUTPUT_DIR)
        set(OUT_DIR "${ARG_OUTPUT_DIR}")
    else()
        set(OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    endif()
    file(MAKE_DIRECTORY "${OUT_DIR}")

    # Step 1: Compile HLSL to DXIL
    set(DXIL_FILE "${OUT_DIR}/${OUTPUT_NAME}.dxil")
    set(DXC_OPTS "")
    if(ARG_INCLUDE_DIRS)
        list(APPEND DXC_OPTS INCLUDE_DIRS ${ARG_INCLUDE_DIRS})
    endif()
    if(ARG_EXTRA_ARGS)
        list(APPEND DXC_OPTS EXTRA_ARGS "-D" "RT_SHADER" ${ARG_EXTRA_ARGS})
    else()
        list(APPEND DXC_OPTS EXTRA_ARGS "-D" "RT_SHADER")
    endif()
    _plume_dxc(${TARGET} "${SOURCE}" "${DXIL_FILE}" "lib_6_3" "" "dxil" ${DXC_OPTS})

    # Step 2: Generate root signature
    set(ROOT_SIG_FILE "${OUT_DIR}/${OUTPUT_NAME}_root_signature.json")
    _plume_generate_root_signature(${TARGET} "${SOURCE}" "${ROOT_SIG_FILE}"
        INCLUDE_DIRS ${ARG_INCLUDE_DIRS}
        EXTRA_ARGS ${ARG_EXTRA_ARGS})

    # Step 3a: Convert to visible functions
    set(VISIBLE_FUNCS_METALLIB "${OUT_DIR}/${OUTPUT_NAME}_functions.metallib")
    _plume_metal_shader_converter(${TARGET} "${DXIL_FILE}" "${VISIBLE_FUNCS_METALLIB}" "${ROOT_SIG_FILE}")

    # Step 3b: Convert to dispatch kernel
    set(DISPATCH_METALLIB "${OUT_DIR}/${OUTPUT_NAME}_dispatch.metallib")
    _plume_metal_shader_converter(${TARGET} "${DXIL_FILE}" "${DISPATCH_METALLIB}" "${ROOT_SIG_FILE}"
        SYNTHESIZE_DISPATCH)

    # Step 4: Combine both metallibs
    set(COMBINED_METALLIB "${OUT_DIR}/${OUTPUT_NAME}.metallib")
    add_custom_command(
        OUTPUT "${COMBINED_METALLIB}"
        COMMAND plume_combine_rt_metallibs "${VISIBLE_FUNCS_METALLIB}" "${DISPATCH_METALLIB}" "${COMBINED_METALLIB}" "${ROOT_SIG_FILE}"
        DEPENDS "${VISIBLE_FUNCS_METALLIB}" "${DISPATCH_METALLIB}" plume_combine_rt_metallibs
        COMMENT "Combine RT metallibs: ${OUTPUT_NAME}"
        VERBATIM
    )

    # Step 5: Embed as C header
    _plume_embed(${TARGET} "${COMBINED_METALLIB}" "${OUTPUT_NAME}BlobMetalLib"
        "${OUT_DIR}/${OUTPUT_NAME}.metallib.c"
        "${OUT_DIR}/${OUTPUT_NAME}.metallib.h")
endfunction()

# Compile native Metal shader to metallib (for handwritten .metal files)
#
# Arguments:
#   TARGET      - CMake target to add shader to
#   SOURCE      - Metal source file
#   OUTPUT_NAME - Base name for output files
# Options:
#   OUTPUT_DIR  - Custom output directory
function(_plume_compile_native_metal TARGET SOURCE OUTPUT_NAME)
    cmake_parse_arguments(ARG "" "OUTPUT_DIR" "" ${ARGN})

    if(NOT APPLE)
        return()
    endif()

    if(ARG_OUTPUT_DIR)
        set(OUT_DIR "${ARG_OUTPUT_DIR}")
    else()
        set(OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    endif()
    file(MAKE_DIRECTORY "${OUT_DIR}")

    set(METALLIB_FILE "${OUT_DIR}/${OUTPUT_NAME}.metallib")
    _plume_metal_compile(${TARGET} "${SOURCE}" "${METALLIB_FILE}")
    _plume_embed(${TARGET} "${METALLIB_FILE}" "${OUTPUT_NAME}BlobMSL"
        "${OUT_DIR}/${OUTPUT_NAME}.metal.c"
        "${OUT_DIR}/${OUTPUT_NAME}.metal.h")
endfunction()

# ============================================================================
# Layer 3: Public API - User-facing functions
# ============================================================================

# Compile a shader and add it to a target
# Usage: plume_compile_shader(TARGET SOURCE TYPE OUTPUT_NAME ENTRY_POINT [options])
#   TARGET      - CMake target to add shader to
#   SOURCE      - Path to shader source file (.hlsl or .metal)
#   TYPE        - Shader type: vertex, pixel, compute, geometry
#   OUTPUT_NAME - Base name for output files (e.g., "mainVert")
#   ENTRY_POINT - Shader entry point function name (e.g., "VSMain")
#
# Options:
#   SPEC_CONSTANTS     - Only compile SPIRV + Metal (no DXIL)
#   SHADER_MODEL <ver> - Shader model version (default: 6_0)
#   INCLUDE_DIRS <dirs> - Additional include directories
#   EXTRA_ARGS <args>  - Additional DXC arguments
#   OUTPUT_DIR <dir>   - Custom output directory
function(plume_compile_shader TARGET_NAME SHADER_SOURCE SHADER_TYPE OUTPUT_NAME ENTRY_POINT)
    get_filename_component(EXT "${SHADER_SOURCE}" EXT)

    if(EXT MATCHES "\\.metal$")
        if(APPLE)
            _plume_compile_native_metal(${TARGET_NAME} "${SHADER_SOURCE}" ${OUTPUT_NAME} ${ARGN})
        endif()
    elseif(EXT MATCHES "\\.hlsl$")
        _plume_compile_stage_shader(${TARGET_NAME} "${SHADER_SOURCE}" ${SHADER_TYPE} ${OUTPUT_NAME} ${ENTRY_POINT} ${ARGN})
    else()
        message(WARNING "Unsupported shader extension '${EXT}' for ${SHADER_SOURCE}. Use .hlsl or .metal")
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

# Compile a native Metal shader (Apple only, no-op on other platforms)
# Use this for handwritten .metal files, not for cross-compiled HLSL
# Usage: plume_compile_metal_shader(TARGET SOURCE OUTPUT_NAME)
function(plume_compile_metal_shader TARGET_NAME SHADER_SOURCE OUTPUT_NAME)
    if(APPLE)
        _plume_compile_native_metal(${TARGET_NAME} "${SHADER_SOURCE}" ${OUTPUT_NAME} ${ARGN})
    endif()
endfunction()

# Compile a ray tracing shader library
# Usage: plume_compile_rt_shader(TARGET SOURCE OUTPUT_NAME [options])
#   TARGET      - CMake target to add shader to
#   SOURCE      - Path to HLSL shader source file
#   OUTPUT_NAME - Base name for output files
#
# Options:
#   SHADER_MODEL <ver>  - Shader model version (default: 6_3)
#   INCLUDE_DIRS <dirs> - Additional include directories
#   EXTRA_ARGS <args>   - Additional DXC arguments
#   OUTPUT_DIR <dir>    - Custom output directory
# Compile a ray tracing shader library
#
# Outputs:
#   Windows: {OUTPUT_NAME}BlobDXIL in shaders/{OUTPUT_NAME}.hlsl.dxil.h
#            {OUTPUT_NAME}BlobSPIRV in shaders/{OUTPUT_NAME}.hlsl.spirv.h
#   Apple:   {OUTPUT_NAME}BlobMetalLib in shaders/{OUTPUT_NAME}.metallib.h
#   Linux:   {OUTPUT_NAME}BlobSPIRV in shaders/{OUTPUT_NAME}.hlsl.spirv.h
function(plume_compile_rt_shader TARGET_NAME SHADER_SOURCE OUTPUT_NAME)
    _plume_compile_library_shader_impl(${TARGET_NAME} "${SHADER_SOURCE}" ${OUTPUT_NAME} RAYTRACING ${ARGN})
endfunction()

# Compile a general library shader (non-RT)
# Usage: plume_compile_library_shader(TARGET SOURCE OUTPUT_NAME [options])
#   TARGET      - CMake target to add shader to
#   SOURCE      - Path to HLSL shader source file
#   OUTPUT_NAME - Base name for output files
#
# Options:
#   SHADER_MODEL <ver>  - Shader model version (default: 6_3)
#   INCLUDE_DIRS <dirs> - Additional include directories
#   EXTRA_ARGS <args>   - Additional DXC arguments
#   OUTPUT_DIR <dir>    - Custom output directory
#
# Output:
#   Windows: {OUTPUT_NAME}BlobDXIL in shaders/{OUTPUT_NAME}.hlsl.dxil.h
#            {OUTPUT_NAME}BlobSPIRV in shaders/{OUTPUT_NAME}.hlsl.spirv.h
#   Linux:   {OUTPUT_NAME}BlobSPIRV in shaders/{OUTPUT_NAME}.hlsl.spirv.h
function(plume_compile_library_shader TARGET_NAME SHADER_SOURCE OUTPUT_NAME)
    _plume_compile_library_shader_impl(${TARGET_NAME} "${SHADER_SOURCE}" ${OUTPUT_NAME} ${ARGN})
endfunction()

# Preprocess a shader header file and embed it as text
# Useful for runtime shader compilation where you need the preprocessed source
# Usage: plume_preprocess_shader(TARGET SOURCE OUTPUT_NAME [options])
#   VAR_NAME - Optional variable name for the embedded data (defaults to OUTPUT_NAME)
function(plume_preprocess_shader TARGET_NAME SHADER_SOURCE OUTPUT_NAME)
    cmake_parse_arguments(ARG "" "OUTPUT_DIR;VAR_NAME" "INCLUDE_DIRS" ${ARGN})

    get_filename_component(SHADER_NAME "${SHADER_SOURCE}" NAME)

    if(ARG_OUTPUT_DIR)
        set(OUT_DIR "${ARG_OUTPUT_DIR}")
    else()
        set(OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    endif()
    file(MAKE_DIRECTORY "${OUT_DIR}")

    if(ARG_VAR_NAME)
        set(VAR_NAME "${ARG_VAR_NAME}")
    else()
        set(VAR_NAME "${OUTPUT_NAME}")
    endif()

    set(PREPROCESSED_OUTPUT "${OUT_DIR}/${OUTPUT_NAME}.rw")

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

    _plume_embed(${TARGET_NAME} "${PREPROCESSED_OUTPUT}" "${VAR_NAME}Text"
        "${OUT_DIR}/${OUTPUT_NAME}.rw.c"
        "${OUT_DIR}/${OUTPUT_NAME}.rw.h"
        TEXT)
endfunction()
