# PlumeCombineRTMetallibs.cmake
# Builds the combine_rt_metallibs tool for packaging Metal RT shader libraries

# Build the combine_rt_metallibs tool for the host system
function(plume_build_combine_rt_metallibs)
    if(TARGET plume_combine_rt_metallibs)
        return()
    endif()

    # Find the source file relative to this module
    set(TOOL_SOURCE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/combine_rt_metallibs.cpp")

    if(NOT EXISTS "${TOOL_SOURCE}")
        message(FATAL_ERROR "plume combine_rt_metallibs.cpp not found at ${TOOL_SOURCE}")
    endif()

    add_executable(plume_combine_rt_metallibs ${TOOL_SOURCE})
    set_target_properties(plume_combine_rt_metallibs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plume_tools"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )

    if(APPLE)
        set_target_properties(plume_combine_rt_metallibs PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        )
    endif()
endfunction()
