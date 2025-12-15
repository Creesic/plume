# PlumeSpirvCross.cmake
# Fetches pre-built SPIRV-Cross and builds our spirv_cross_msl tool

include(FetchContent)

# Fetch SPIRV-Cross pre-built binaries and build our tool
function(plume_fetch_spirv_cross)
    if(TARGET plume_spirv_cross_msl)
        return()
    endif()

    FetchContent_Declare(
        plume_spirv_cross
        GIT_REPOSITORY https://github.com/renderbag/spriv-cross-bin.git
        GIT_TAG main
    )
    FetchContent_MakeAvailable(plume_spirv_cross)

    # Determine library path based on platform/architecture
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
        set(SPIRV_CROSS_ARCH "x64")
    else()
        set(SPIRV_CROSS_ARCH "arm64")
    endif()

    set(SPIRV_CROSS_LIB_DIR "${plume_spirv_cross_SOURCE_DIR}/lib/${SPIRV_CROSS_ARCH}")
    set(SPIRV_CROSS_INCLUDE_DIR "${plume_spirv_cross_SOURCE_DIR}/include")

    # Build our custom spirv_cross_msl tool
    set(SPIRV_CROSS_MSL_SOURCE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../tools/spirv_cross_msl.cpp")

    if(NOT EXISTS "${SPIRV_CROSS_MSL_SOURCE}")
        message(FATAL_ERROR "plume spirv_cross_msl.cpp not found at ${SPIRV_CROSS_MSL_SOURCE}")
    endif()

    add_executable(plume_spirv_cross_msl ${SPIRV_CROSS_MSL_SOURCE})
    target_include_directories(plume_spirv_cross_msl PRIVATE ${SPIRV_CROSS_INCLUDE_DIR})

    # Link against pre-built static libraries
    # Order matters: msl depends on glsl depends on core
    if(WIN32)
        target_link_libraries(plume_spirv_cross_msl PRIVATE
            "${SPIRV_CROSS_LIB_DIR}/spirv-cross-msl.lib"
            "${SPIRV_CROSS_LIB_DIR}/spirv-cross-glsl.lib"
            "${SPIRV_CROSS_LIB_DIR}/spirv-cross-core.lib"
        )
    else()
        target_link_libraries(plume_spirv_cross_msl PRIVATE
            "${SPIRV_CROSS_LIB_DIR}/libspirv-cross-msl.a"
            "${SPIRV_CROSS_LIB_DIR}/libspirv-cross-glsl.a"
            "${SPIRV_CROSS_LIB_DIR}/libspirv-cross-core.a"
        )
    endif()

    set_target_properties(plume_spirv_cross_msl PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plume_tools"
    )

    if(APPLE)
        set_target_properties(plume_spirv_cross_msl PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        )
    endif()
endfunction()
