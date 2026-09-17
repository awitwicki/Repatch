#!/bin/bash
#
# Repatch build script (macOS, Apple Silicon).
#
#   ./build.sh [--pcl-path=DIR] [--no-tests] [--no-module] [--debug] [--clean]
#
# 1. checks the toolchain (clang++, cmake, make, xcrun)
# 2. builds PCL's static libraries from ../PCL once (scripts/build_pcl.sh)
# 3. configures and builds with CMake (core, tests, CLI, module)
# 4. runs the unit tests and a CLI smoke run on a synthetic image
# 5. verifies bin/macosx/arm64/Repatch-pxm.dylib
#
# Signing is a separate step: ./sign_module.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PCL_PATH=""
RUN_TESTS=1
BUILD_MODULE=1
BUILD_TYPE=Release
CLEAN=0

info()    { printf '\033[0;34m[INFO]\033[0m %s\n' "$1"; }
success() { printf '\033[0;32m[OK]\033[0m %s\n' "$1"; }
fail()    { printf '\033[0;31m[ERROR]\033[0m %s\n' "$1" >&2; exit 1; }

for arg in "$@"; do
    case "$arg" in
        --pcl-path=*) PCL_PATH="${arg#*=}" ;;
        --no-tests)   RUN_TESTS=0 ;;
        --no-module)  BUILD_MODULE=0 ;;
        --debug)      BUILD_TYPE=Debug ;;
        --clean)      CLEAN=1 ;;
        -h|--help)    sed -n '2,14p' "$0"; exit 0 ;;
        *)            fail "unknown option: $arg (see --help)" ;;
    esac
done

for tool in clang++ cmake make xcrun codesign; do
    command -v "$tool" >/dev/null 2>&1 || fail "required tool not found: $tool (install the Xcode Command Line Tools)"
done
[ "$(uname -s)" = "Darwin" ] || fail "this build script supports macOS only"
[ "$(uname -m)" = "arm64" ] || info "warning: host is $(uname -m); the module is built for arm64"
info "clang: $(clang++ --version | head -1)"
info "cmake: $(cmake --version | head -1)"

CMAKE_ARGS=( -DCMAKE_BUILD_TYPE="$BUILD_TYPE" )
if [ "$BUILD_MODULE" = 1 ]; then
    # shellcheck source=scripts/pcl_env.sh
    source "$ROOT/scripts/pcl_env.sh" "$PCL_PATH" || fail "PCL not found; pass --pcl-path=DIR"
    info "PCL: $PCLDIR"
    bash "$ROOT/scripts/build_pcl.sh" "$PCLDIR"
    CMAKE_ARGS+=( -DREPATCH_BUILD_MODULE=ON -DPCL_DIR="$PCLDIR" )
else
    CMAKE_ARGS+=( -DREPATCH_BUILD_MODULE=OFF )
fi

BUILD_DIR="$ROOT/build"
if [ "$CLEAN" = 1 ]; then
    info "removing $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

info "configuring ($BUILD_TYPE)"
cmake -S "$ROOT" -B "$BUILD_DIR" "${CMAKE_ARGS[@]}"
info "building"
cmake --build "$BUILD_DIR" -j "$(sysctl -n hw.ncpu)"

if [ "$RUN_TESTS" = 1 ]; then
    info "running unit tests"
    ( cd "$BUILD_DIR" && ctest --output-on-failure )
    info "CLI smoke run"
    "$BUILD_DIR/repatch-cli" --synth 256 256 "$BUILD_DIR/smoke_in.fits" "$BUILD_DIR/smoke_mask.fits"
    "$BUILD_DIR/repatch-cli" "$BUILD_DIR/smoke_in.fits" "$BUILD_DIR/smoke_mask.fits" "$BUILD_DIR/smoke_out.fits" --seed 1
    success "tests and smoke run passed"
fi

if [ "$BUILD_MODULE" = 1 ]; then
    OUT="$ROOT/bin/macosx/arm64/Repatch-pxm.dylib"
    [ -f "$OUT" ] || fail "module not found at $OUT"
    file "$OUT" | grep -q arm64 || fail "module is not an arm64 binary: $(file "$OUT")"
    nm -gU "$OUT" | grep -q InstallPixInsightModule || fail "module does not export InstallPixInsightModule"
    success "module: $OUT ($(du -h "$OUT" | cut -f1))"
    echo
    echo "Next steps:"
    echo "  1. ./sign_module.sh                      (prompts for the .xssk password)"
    echo "  2. Once per machine: PixInsight > Edit > Local Signing Identity..., select the"
    echo "     .xssk, enter its password, tick 'Make the local signing identity persistent'"
    echo "     (otherwise install fails with 'Unknown code signing identity')"
    echo "  3. PixInsight > Process > Modules > Install Modules..., browse to"
    echo "     $ROOT/bin/macosx/arm64  and click Search, then Install"
    echo "  4. Optional, for the dialog's Browse Documentation button: scripts/install_docs.sh"
fi
