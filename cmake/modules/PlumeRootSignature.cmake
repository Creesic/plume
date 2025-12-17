# PlumeRootSignature.cmake
# Builds the generate_root_signature tool for creating Metal Shader Converter root signature JSON

# Build the generate_root_signature tool for the host system
function(plume_build_generate_root_signature)
    if(TARGET plume_generate_root_signature)
        return()
    endif()

    # Find the source file relative to this module
    set(GEN_ROOT_SIG_SOURCE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/generate_root_signature.cpp")

    if(NOT EXISTS "${GEN_ROOT_SIG_SOURCE}")
        message(FATAL_ERROR "plume generate_root_signature.cpp not found at ${GEN_ROOT_SIG_SOURCE}")
    endif()

    add_executable(plume_generate_root_signature ${GEN_ROOT_SIG_SOURCE})
    set_target_properties(plume_generate_root_signature PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plume_tools"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )

    if(APPLE)
        set_target_properties(plume_generate_root_signature PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        )
    endif()
endfunction()
