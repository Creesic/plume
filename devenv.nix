{
  pkgs,
  lib,
  ...
}:
{
  # https://devenv.sh/basics/

  # https://devenv.sh/packages/
  packages =
    with pkgs;
    [
      #
      # Common development tools
      #
      cmake
      ninja
      pkg-config
      git

      # Development tools (system compiler will be used)
      lldb

      # Graphics development dependencies
      vulkan-headers
      vulkan-loader
      vulkan-tools
      spirv-tools
      spirv-headers
      glslang

      # Platform-specific shader compilers
      shaderc

      # SDL2 for examples (required on all platforms)
      SDL2
    ]
    ++ lib.optionals pkgs.stdenv.isLinux [
      #
      # Linux specific packages
      #
      clang
      clang-tools
      libGL
      libxkbcommon
      wayland
      wayland-protocols
      mesa
      xorg.libX11
      xorg.libXrandr
      xorg.libXi
      vulkan-validation-layers
    ]
    ++ lib.optionals pkgs.stdenv.isDarwin (
      with pkgs.darwin.apple_sdk_12_3;
      [
        #
        # macOS specific packages (using SDK 12.3)
        #
        frameworks.Cocoa
        frameworks.IOKit
        frameworks.CoreFoundation
        frameworks.Metal
        frameworks.MetalKit
        frameworks.QuartzCore
        frameworks.AppKit
      ]
    );

  # Environment variables
  env = {
    # CMake build type for development
    CMAKE_BUILD_TYPE = "Debug";

    # Help CMake find SDL2
    SDL2_ROOT = "${pkgs.SDL2}";
    CMAKE_PREFIX_PATH = "${pkgs.SDL2}";
  } // lib.optionalAttrs pkgs.stdenv.isLinux {
    # Vulkan SDK path for validation layers (Linux only)
    VK_LAYER_PATH = "${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d";
    VK_INSTANCE_LAYERS = "VK_LAYER_KHRONOS_validation";
  };

  # https://devenv.sh/scripts/
  scripts = {
    configure.exec = ''
      echo "Configuring build with CMake..."
      ${if pkgs.stdenv.isDarwin then ''
        # Use system toolchain by setting explicit paths
        export SDKROOT="/Applications/Xcode-16.2.0.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
        export DEVELOPER_DIR="/Applications/Xcode-16.2.0.app/Contents/Developer"
        # Clear ALL Nix environment variables that interfere with system toolchain
        unset CPATH LIBRARY_PATH NIX_CFLAGS_COMPILE NIX_LDFLAGS NIX_CPPFLAGS NIX_CXXSTDLIB_COMPILE \
              CMAKE_PREFIX_PATH PKG_CONFIG_PATH CPLUS_INCLUDE_PATH C_INCLUDE_PATH
        echo "Using SDK: $SDKROOT"
        # Use system compiler with deployment target for maximumFramesPerSecond support
        cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPLUME_BUILD_EXAMPLES=ON \
          -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
          -DCMAKE_OSX_SYSROOT="$SDKROOT" \
          -DCMAKE_C_COMPILER="/usr/bin/clang" \
          -DCMAKE_CXX_COMPILER="/usr/bin/clang++" \
          -DCMAKE_CXX_FLAGS="-stdlib=libc++" \
          -DCMAKE_EXE_LINKER_FLAGS="-stdlib=libc++"
      '' else ''
        cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPLUME_BUILD_EXAMPLES=ON
      ''}
    '';

    build.exec = ''
      echo "Building project..."
      ${if pkgs.stdenv.isDarwin then ''
        # Ensure same environment as configure
        export SDKROOT="/Applications/Xcode-16.2.0.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
        export DEVELOPER_DIR="/Applications/Xcode-16.2.0.app/Contents/Developer"
        unset CPATH LIBRARY_PATH NIX_CFLAGS_COMPILE NIX_LDFLAGS NIX_CPPFLAGS NIX_CXXSTDLIB_COMPILE \
              CMAKE_PREFIX_PATH PKG_CONFIG_PATH CPLUS_INCLUDE_PATH C_INCLUDE_PATH
      '' else ""}
      cmake --build build
    '';

    clean.exec = ''
      echo "Cleaning build directory..."
      rm -rf build build-xcode
    '';

    run-example.exec = ''
      echo "Running example application..."
      ./build/bin/plume_example
    '';
  };

  # https://devenv.sh/languages/
  languages = {
    c.enable = true;
    cplusplus.enable = true;
  };
}
