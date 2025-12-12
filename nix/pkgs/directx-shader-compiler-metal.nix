# DirectX Shader Compiler with Metal support
# Based on nixpkgs directx-shader-compiler, modified to enable Metal codegen
{ lib
, stdenv
, fetchFromGitHub
, cmake
, ninja
, python3
, git
, makeWrapper
, metal-shader-converter
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "directx-shader-compiler-metal";
  version = "1.8.2505";

  outputs = [
    "out"
    "dev"
  ];

  src = fetchFromGitHub {
    owner = "microsoft";
    repo = "DirectXShaderCompiler";
    rev = "v${finalAttrs.version}";
    hash = "sha256-o1yLn3Fp3a9KhR2ZhAr8K2Mf1neMUL0g1Zf7GQ0TgQU=";
    fetchSubmodules = true;
  };

  nativeBuildInputs = [
    cmake
    git
    ninja
    python3
    makeWrapper
  ];

  buildInputs = [
    metal-shader-converter
  ];

  # Set up paths for FindMetalIRConverter.cmake to find the Metal IR Converter
  preConfigure = ''
    export METAL_IRCONVERTER_INCLUDE_DIR="${metal-shader-converter}/include/metal_irconverter"
    export METAL_IRCONVERTER_LIB="${metal-shader-converter}/lib/libmetalirconverter.dylib"
  '';

  cmakeFlags = [
    "-C../cmake/caches/PredefinedParams.cmake"
    # Help CMake find Metal IR Converter
    "-DMETAL_IRCONVERTER_INCLUDE_DIR=${metal-shader-converter}/include/metal_irconverter"
    "-DMETAL_IRCONVERTER_LIB=${metal-shader-converter}/lib/libmetalirconverter.dylib"
  ];

  # The default install target installs heaps of LLVM stuff.
  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin $out/lib $dev/include

    # Install binaries
    mv bin/{dxc,dxv}* $out/bin/

    # Install libraries (including dxcompiler with Metal support linked in)
    mv lib/lib*.so* lib/lib*.*dylib $out/lib/ 2>/dev/null || true

    # Install headers
    cp -r $src/include/dxc $dev/include/

    # Create wrapper script to ensure Metal IR Converter library is found at runtime
    wrapProgram $out/bin/dxc \
      --prefix DYLD_LIBRARY_PATH : "${metal-shader-converter}/lib"

    runHook postInstall
  '';

  # Ensure the library path is set for the linker
  NIX_LDFLAGS = lib.optionalString stdenv.isDarwin "-L${metal-shader-converter}/lib -rpath ${metal-shader-converter}/lib";

  meta = {
    description = "HLSL compiler with Metal shader generation support";
    homepage = "https://github.com/microsoft/DirectXShaderCompiler";
    platforms = lib.platforms.darwin;  # Metal support is macOS only
    license = lib.licenses.ncsa;
  };
})
