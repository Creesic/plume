# PlumeInternalShaders.cmake
# Internal shader compilation for Plume's built-in shaders (clear, resolve)

# Build the file_to_c tool for the host system
function(plume_build_file_to_c)
    if(TARGET plume_file_to_c)
        return()
    endif()

    set(FILE_TO_C_SOURCE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/tools/file_to_c.cpp")

    add_executable(plume_file_to_c ${FILE_TO_C_SOURCE})
    set_target_properties(plume_file_to_c PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plume_tools"
    )

    if(APPLE)
        set_target_properties(plume_file_to_c PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        )
    endif()
endfunction()

# Compile Metal shaders to embedded C headers
function(plume_compile_metal_shaders TARGET_NAME)
    if(NOT APPLE)
        return()
    endif()

    # Build the file_to_c tool
    plume_build_file_to_c()

    # Get the shader source directory
    set(SHADER_DIR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../shaders")
    set(OUTPUT_DIR "${CMAKE_BINARY_DIR}/plume_shaders")

    # Create output directory
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")

    # List of shaders to compile
    set(PLUME_METAL_SHADERS
        plume_clear
        plume_resolve
    )

    set(GENERATED_SOURCES "")
    set(GENERATED_HEADERS "")

    foreach(SHADER_NAME ${PLUME_METAL_SHADERS})
        set(SHADER_SOURCE "${SHADER_DIR}/${SHADER_NAME}.metal")
        set(IR_OUTPUT "${OUTPUT_DIR}/${SHADER_NAME}.ir")
        set(METALLIB_OUTPUT "${OUTPUT_DIR}/${SHADER_NAME}.metallib")
        set(C_OUTPUT "${OUTPUT_DIR}/${SHADER_NAME}.metallib.c")
        set(H_OUTPUT "${OUTPUT_DIR}/${SHADER_NAME}.metallib.h")

        # Compile Metal to IR
        add_custom_command(
            OUTPUT ${IR_OUTPUT}
            COMMAND xcrun -sdk macosx metal -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET} -o ${IR_OUTPUT} -c ${SHADER_SOURCE}
            DEPENDS ${SHADER_SOURCE}
            COMMENT "Compiling ${SHADER_NAME}.metal to IR"
            VERBATIM
        )

        # Link IR to metallib
        add_custom_command(
            OUTPUT ${METALLIB_OUTPUT}
            COMMAND xcrun -sdk macosx metallib ${IR_OUTPUT} -o ${METALLIB_OUTPUT}
            DEPENDS ${IR_OUTPUT}
            COMMENT "Linking ${SHADER_NAME} to metallib"
            VERBATIM
        )

        # Generate C header with embedded data
        add_custom_command(
            OUTPUT ${C_OUTPUT} ${H_OUTPUT}
            COMMAND plume_file_to_c ${METALLIB_OUTPUT} "${SHADER_NAME}_metallib" ${C_OUTPUT} ${H_OUTPUT}
            DEPENDS ${METALLIB_OUTPUT} plume_file_to_c
            COMMENT "Generating embedded header for ${SHADER_NAME}"
            VERBATIM
        )

        list(APPEND GENERATED_SOURCES ${C_OUTPUT})
        list(APPEND GENERATED_HEADERS ${H_OUTPUT})
    endforeach()

    # Add generated sources to the target
    target_sources(${TARGET_NAME} PRIVATE ${GENERATED_SOURCES})

    # Add include directory for generated headers
    target_include_directories(${TARGET_NAME} PRIVATE "${OUTPUT_DIR}")
endfunction()
