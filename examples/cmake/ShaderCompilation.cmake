# Shader compilation functions for Plume
# Using DXC for HLSL compilation to SPIR-V, DXIL, and Metal

# Find DXC in PATH (provided by nix/devenv with Metal support)
find_program(DXC_EXECUTABLE dxc)
if(NOT DXC_EXECUTABLE)
    message(FATAL_ERROR "DXC not found in PATH. Please ensure directx-shader-compiler is installed.")
endif()
message(STATUS "Found DXC: ${DXC_EXECUTABLE}")

# Common DXC options
set(DXC_COMMON_OPTS "-I${CMAKE_SOURCE_DIR}")
set(DXC_DXIL_OPTS "-Wno-ignored-attributes")
set(DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
set(DXC_METAL_OPTS "-metal")
set(DXC_RT_OPTS "-D" "RT_SHADER" "-T" "lib_6_3" "-fspv-target-env=vulkan1.1spirv1.4" "-fspv-extension=SPV_KHR_ray_tracing" "-fspv-extension=SPV_EXT_descriptor_indexing")

# Function to compile HLSL using DXC with common parameters
function(build_shader_dxc_impl TARGET_NAME SHADER_SOURCE SHADER_TYPE OUTPUT_NAME OUTPUT_FORMAT ENTRY_POINT)
    # Create unique output names based on format
    if(OUTPUT_FORMAT STREQUAL "spirv")
        set(OUTPUT_EXT "spv")
        set(BLOB_SUFFIX "SPIRV")
        set(FORMAT_FLAGS ${DXC_SPV_OPTS})
    elseif(OUTPUT_FORMAT STREQUAL "dxil")
        set(OUTPUT_EXT "dxil")
        set(BLOB_SUFFIX "DXIL")
        set(FORMAT_FLAGS ${DXC_DXIL_OPTS})
    elseif(OUTPUT_FORMAT STREQUAL "metal")
        set(OUTPUT_EXT "metallib")
        set(BLOB_SUFFIX "MSL")
        set(FORMAT_FLAGS ${DXC_METAL_OPTS})
    else()
        message(FATAL_ERROR "Unknown output format: ${OUTPUT_FORMAT}")
    endif()

    set(SHADER_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.hlsl.${OUTPUT_EXT}")
    set(C_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.hlsl.${OUTPUT_FORMAT}.c")
    set(H_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.hlsl.${OUTPUT_FORMAT}.h")

    # Create output directory
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")

    # Determine the shader options based on type
    if(SHADER_TYPE STREQUAL "vertex")
        set(SHADER_PROFILE "vs_6_0")
        # Only invert Y for SPIR-V (Vulkan), not for Metal or DXIL
        if(OUTPUT_FORMAT STREQUAL "spirv")
            set(DXC_EXTRA_ARGS "-fvk-invert-y")
        else()
            set(DXC_EXTRA_ARGS "")
        endif()
    elseif(SHADER_TYPE STREQUAL "fragment")
        set(SHADER_PROFILE "ps_6_0")
        set(DXC_EXTRA_ARGS "")
    elseif(SHADER_TYPE STREQUAL "compute")
        set(SHADER_PROFILE "cs_6_0")
        set(DXC_EXTRA_ARGS "")
    elseif(SHADER_TYPE STREQUAL "ray")
        set(SHADER_PROFILE "lib_6_3")
        set(DXC_EXTRA_ARGS ${DXC_RT_OPTS})
    else()
        message(FATAL_ERROR "Unknown shader type: ${SHADER_TYPE}")
    endif()

    set(BLOB_NAME "${OUTPUT_NAME}Blob${BLOB_SUFFIX}")

    # Compile using DXC
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${DXC_EXECUTABLE} ${DXC_COMMON_OPTS} -E ${ENTRY_POINT} -T ${SHADER_PROFILE} ${FORMAT_FLAGS} ${DXC_EXTRA_ARGS}
                -Fo ${SHADER_OUTPUT} ${SHADER_SOURCE}
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling ${SHADER_TYPE} shader ${SHADER_SOURCE} to ${OUTPUT_FORMAT} using DXC"
    )

    # Generate C header
    add_custom_command(
        OUTPUT "${C_OUTPUT}" "${H_OUTPUT}"
        COMMAND file_to_c ${SHADER_OUTPUT} "${BLOB_NAME}" "${C_OUTPUT}" "${H_OUTPUT}"
        DEPENDS ${SHADER_OUTPUT} file_to_c
        COMMENT "Generating C header for ${OUTPUT_FORMAT} shader ${OUTPUT_NAME}"
    )

    # Add the generated source file to the target
    target_sources(${TARGET_NAME} PRIVATE "${C_OUTPUT}")

    # Make sure the target can find the generated header
    target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_BINARY_DIR}")
endfunction()

# Function to compile HLSL to SPIR-V using DXC
function(build_shader_spirv_impl TARGET_NAME SHADER_SOURCE SHADER_TYPE OUTPUT_NAME ENTRY_POINT)
    build_shader_dxc_impl(${TARGET_NAME} ${SHADER_SOURCE} ${SHADER_TYPE} ${OUTPUT_NAME} "spirv" ${ENTRY_POINT})
endfunction()

# Function to compile HLSL to DXIL using DXC
function(build_shader_dxil_impl TARGET_NAME SHADER_SOURCE SHADER_TYPE OUTPUT_NAME ENTRY_POINT)
    build_shader_dxc_impl(${TARGET_NAME} ${SHADER_SOURCE} ${SHADER_TYPE} ${OUTPUT_NAME} "dxil" ${ENTRY_POINT})
endfunction()

# Function to compile HLSL to Metal using DXC with -metal flag
function(build_shader_metal_impl TARGET_NAME SHADER_SOURCE SHADER_TYPE OUTPUT_NAME ENTRY_POINT)
    build_shader_dxc_impl(${TARGET_NAME} ${SHADER_SOURCE} ${SHADER_TYPE} ${OUTPUT_NAME} "metal" ${ENTRY_POINT})
endfunction()

# Compile a shader based on its type
# For HLSL files: compiles to SPIR-V (all platforms), DXIL (Windows), and Metal (macOS)
function(compile_shader TARGET_NAME SHADER_SOURCE SHADER_TYPE OUTPUT_NAME ENTRY_POINT)
    # Get the file extension to determine the shader language
    get_filename_component(SHADER_EXT ${SHADER_SOURCE} EXT)

    # Only HLSL is supported now (Metal shaders are generated from HLSL)
    if(SHADER_SOURCE MATCHES ".*\\.hlsl$")
        # Compile HLSL shader to SPIR-V using DXC (all platforms for Vulkan)
        build_shader_spirv_impl(${TARGET_NAME} ${SHADER_SOURCE} ${SHADER_TYPE} ${OUTPUT_NAME} ${ENTRY_POINT})

        # Also compile to DXIL on Windows
        if(WIN32)
            build_shader_dxil_impl(${TARGET_NAME} ${SHADER_SOURCE} ${SHADER_TYPE} ${OUTPUT_NAME} ${ENTRY_POINT})
        endif()

        # Also compile to Metal on macOS
        if(APPLE)
            build_shader_metal_impl(${TARGET_NAME} ${SHADER_SOURCE} ${SHADER_TYPE} ${OUTPUT_NAME} ${ENTRY_POINT})
        endif()
    elseif(SHADER_EXT MATCHES ".*\\.metal$")
        # Legacy Metal shader support - warn that this is deprecated
        message(WARNING "Direct .metal shader compilation is deprecated. Please use HLSL shaders instead.")
        message(WARNING "Skipping ${SHADER_SOURCE} - convert to HLSL for cross-platform support.")
    else()
        message(WARNING "Unsupported shader extension ${SHADER_EXT} for ${SHADER_SOURCE} - only .hlsl files are supported")
    endif()
endfunction()

function(file_to_c_header INPUT_FILE OUTPUT_FILE VARIABLE_NAME)
    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
        COMMAND ${CMAKE_BINARY_DIR}/bin/file_to_c ${INPUT_FILE} ${OUTPUT_FILE} ${VARIABLE_NAME} ${VARIABLE_NAME}Size
        DEPENDS ${INPUT_FILE} file_to_c
        COMMENT "Converting ${INPUT_FILE} to C header"
    )
endfunction()
