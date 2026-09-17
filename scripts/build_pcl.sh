#!/bin/bash
# Builds PCL's third-party static libraries and libPCL-pxi.a for macOS arm64
# using PCL's own makefiles. The generated makefiles hard-code Xcode.app's SDK
# path, which does not exist on machines with only the Command Line Tools, so
# each makefile is copied to makefile-arm64.local with the SDK path rewritten
# to `xcrun --show-sdk-path`. The originals are never modified.
#
# Usage: scripts/build_pcl.sh [PCL_DIR]
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=pcl_env.sh
source "$here/pcl_env.sh" "${1:-}"

SDK="$(xcrun --show-sdk-path)"
NPROC="$(sysctl -n hw.ncpu)"
XCODE_SDK="/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"

build_with_patched_makefile() {
    local dir="$1"
    sed "s#${XCODE_SDK}#${SDK}#g" "$dir/makefile-arm64" > "$dir/makefile-arm64.local"
    mkdir -p "$dir/arm64/Release"
    ( cd "$dir" && make -j "$NPROC" -f makefile-arm64.local )
}

for lib in cminpack lcms lz4 RFC6234 zlib zstd; do
    if [ -f "$PCLLIBDIR64/lib${lib}-pxi.a" ]; then
        echo "[pcl] lib${lib}-pxi.a present"
    else
        echo "[pcl] building lib${lib}-pxi.a"
        build_with_patched_makefile "$PCLSRCDIR/3rdparty/$lib/macosx/g++"
    fi
done

if [ -f "$PCLLIBDIR64/libPCL-pxi.a" ]; then
    echo "[pcl] libPCL-pxi.a present"
else
    echo "[pcl] building libPCL-pxi.a (this takes a few minutes)"
    build_with_patched_makefile "$PCLSRCDIR/pcl/macosx/g++"
fi

echo "[pcl] libraries in $PCLLIBDIR64:"
ls -la "$PCLLIBDIR64"
