# Metal Shader Converter package
# This wraps Apple's Metal Shader Converter which must be installed on the system.
# Download from: https://developer.apple.com/metal/shader-converter/
# The installer places files in /usr/local/{bin,lib,include}
#
# Since Apple requires authentication to download, we reference the system installation.
{ lib
, stdenv
}:

stdenv.mkDerivation {
  pname = "metal-shader-converter";
  version = "3.0.6";  # Update this when upgrading

  # No source - we're wrapping system binaries
  dontUnpack = true;
  dontBuild = true;

  installPhase = ''
    runHook preInstall

    # Verify system installation exists
    if [ ! -f "/usr/local/lib/libmetalirconverter.dylib" ]; then
      echo "Error: Metal Shader Converter not found at /usr/local/lib/libmetalirconverter.dylib"
      echo "Please download and install from: https://developer.apple.com/metal/shader-converter/"
      exit 1
    fi

    if [ ! -f "/usr/local/include/metal_irconverter/metal_irconverter.h" ]; then
      echo "Error: Metal Shader Converter headers not found at /usr/local/include/metal_irconverter/"
      echo "Please download and install from: https://developer.apple.com/metal/shader-converter/"
      exit 1
    fi

    # Create output directories
    mkdir -p $out/bin
    mkdir -p $out/lib
    mkdir -p $out/include/metal_irconverter
    mkdir -p $out/include/metal_irconverter_runtime

    # Copy binaries
    cp /usr/local/bin/metal-shaderconverter $out/bin/ || true
    cp /usr/local/bin/metal-tt $out/bin/ || true

    # Copy library
    cp /usr/local/lib/libmetalirconverter.dylib $out/lib/

    # Copy headers
    cp -r /usr/local/include/metal_irconverter/* $out/include/metal_irconverter/
    cp -r /usr/local/include/metal_irconverter_runtime/* $out/include/metal_irconverter_runtime/ || true

    # Fix library install name to point to our output
    install_name_tool -id "$out/lib/libmetalirconverter.dylib" "$out/lib/libmetalirconverter.dylib"

    runHook postInstall
  '';

  meta = with lib; {
    description = "Apple's Metal Shader Converter - converts DXIL to Metal IR";
    homepage = "https://developer.apple.com/metal/shader-converter/";
    license = licenses.unfree;  # Apple proprietary
    platforms = platforms.darwin;
    maintainers = [ ];
  };
}
