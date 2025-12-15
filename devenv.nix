{
  pkgs,
  lib,
  inputs,
  ...
}:
{
  # https://devenv.sh/basics/

  # Custom overlays for Metal shader compilation support
  overlays = [
    (import ./nix/overlays/default.nix)
  ];

  # https://devenv.sh/packages/
  packages =
    (with pkgs; [
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

      # Math library for examples
      glm
    ])
    # macOS specific packages
    ++ lib.optionals pkgs.stdenv.isDarwin [
      # DXC with Metal support
      pkgs.directx-shader-compiler-metal
      # MoltenVK for Vulkan on macOS
      pkgs.moltenvk
      pkgs.vulkan-validation-layers
    ]
    # Standard DXC for other platforms
    ++ lib.optionals (!pkgs.stdenv.isDarwin) [
      pkgs.directx-shader-compiler
    ]
    # SDL2 from pinned nixpkgs (real SDL2, not sdl2-compat)
    ++ [ inputs.nixpkgs-sdl2.legacyPackages.${pkgs.system}.SDL2 ]
    ++ lib.optionals pkgs.stdenv.isLinux (with pkgs; [
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
    ])
    # Note: On macOS, we use the system SDK via Xcode, so we don't need
    # to include Darwin frameworks from nixpkgs here.
    ;

  # Environment variables
  env = {
    # CMake build type for development
    CMAKE_BUILD_TYPE = "Debug";

    # Help CMake find SDL2 and GLM (using pinned SDL2)
    SDL2_ROOT = "${inputs.nixpkgs-sdl2.legacyPackages.${pkgs.system}.SDL2}";
    CMAKE_PREFIX_PATH = "${inputs.nixpkgs-sdl2.legacyPackages.${pkgs.system}.SDL2}:${pkgs.glm}";
  } // lib.optionalAttrs pkgs.stdenv.isLinux {
    # Vulkan environment for Linux
    VK_LAYER_PATH = "${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d";
    VK_INSTANCE_LAYERS = "VK_LAYER_KHRONOS_validation";
  } // lib.optionalAttrs pkgs.stdenv.isDarwin {
    # Vulkan environment for macOS (MoltenVK from nixpkgs)
    VK_ICD_FILENAMES = "${pkgs.moltenvk}/share/vulkan/icd.d/MoltenVK_icd.json";
    VK_LAYER_PATH = "${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d";
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
        echo "Using MoltenVK from nix: ${pkgs.moltenvk}"
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

    run-triangle.exec = ''
      echo "Running triangle example..."
      ${if pkgs.stdenv.isDarwin then ''
        export VK_ICD_FILENAMES="${pkgs.moltenvk}/share/vulkan/icd.d/MoltenVK_icd.json"
        export VK_LAYER_PATH="${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d"
        export DYLD_LIBRARY_PATH="${pkgs.vulkan-loader}/lib:''${DYLD_LIBRARY_PATH:-}"
      '' else ""}
      ./build/bin/plume_triangle
    '';

    run-cube.exec = ''
      echo "Running cube example..."
      ${if pkgs.stdenv.isDarwin then ''
        export VK_ICD_FILENAMES="${pkgs.moltenvk}/share/vulkan/icd.d/MoltenVK_icd.json"
        export VK_LAYER_PATH="${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d"
        export DYLD_LIBRARY_PATH="${pkgs.vulkan-loader}/lib:''${DYLD_LIBRARY_PATH:-}"
      '' else ""}
      ./build/bin/plume_cube
    '';
  };

  # https://devenv.sh/languages/
  languages = {
    c.enable = true;
    cplusplus.enable = true;
  };
}
