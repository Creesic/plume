# Plume Nix overlays
# Provides custom packages for shader compilation with Metal support
final: prev: {
  # Apple's Metal Shader Converter (wraps system installation)
  metal-shader-converter = final.callPackage ../pkgs/metal-shader-converter.nix { };

  # DXC with Metal codegen support
  directx-shader-compiler-metal = final.callPackage ../pkgs/directx-shader-compiler-metal.nix {
    inherit (final) metal-shader-converter;
  };
}
