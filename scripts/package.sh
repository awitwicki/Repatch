#!/bin/bash
#
# Assembles a PixInsight update package from the built module and the
# documentation page.
#
#   scripts/package.sh [--version=X.Y.Z] [--out=DIR] [--module-dir=DIR]
#                      [--platform=macosx-arm64|windows-x64]
#
# The archive mirrors the PixInsight installation root, which is the layout
# the PixInsight updater (updates.xri, type="module") unpacks into the
# installation directory:
#
#   bin/Repatch-pxm.dylib         (macosx-arm64)  or  bin/Repatch-pxm.dll (windows-x64)
#   bin/Repatch-pxm.xsgn          (only when the module has been signed)
#   doc/tools/Repatch/Repatch.html
#   doc/tools/Repatch/images/...
#
# --platform defaults to the host: macosx-arm64 on macOS, windows-x64 under
# Git Bash / MSYS on Windows. On Windows run this script from Git Bash.
#
# Writes <out>/Repatch-<version>-<platform>[-unsigned].tar.gz plus a .sha1
# file (the updater's <package sha1="..."> attribute). The version comes from
# --version, else from an exact git tag vX.Y.Z on HEAD, else from the
# MODULE_VERSION_* defines in src/module/RepatchModule.cpp.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION=""
OUT="$ROOT/dist"
MODULE_DIR=""
PLATFORM=""

fail() { printf '\033[0;31m[ERROR]\033[0m %s\n' "$1" >&2; exit 1; }

for arg in "$@"; do
    case "$arg" in
        --version=*)    VERSION="${arg#*=}" ;;
        --out=*)        OUT="${arg#*=}" ;;
        --module-dir=*) MODULE_DIR="${arg#*=}" ;;
        --platform=*)   PLATFORM="${arg#*=}" ;;
        -h|--help)      sed -n '2,24p' "$0"; exit 0 ;;
        *)              fail "unknown option: $arg" ;;
    esac
done

if [ -z "$PLATFORM" ]; then
    case "$(uname -s)" in
        Darwin)               PLATFORM="macosx-arm64" ;;
        MINGW*|MSYS*|CYGWIN*) PLATFORM="windows-x64" ;;
        Linux)
            # WSL packages the Windows build: this project has no Linux target,
            # and bin/windows/x64 is what a Windows checkout actually contains.
            if grep -qi microsoft /proc/version 2>/dev/null || [ -n "${WSL_DISTRO_NAME:-}" ]; then
                PLATFORM="windows-x64"
            else
                fail "cannot infer the target platform on Linux; pass --platform="
            fi
            ;;
        *) fail "cannot infer the target platform on $(uname -s); pass --platform=" ;;
    esac
fi

# Per-platform: module file name, default build output directory, and the
# signature `file` must report for the binary.
case "$PLATFORM" in
    macosx-arm64)
        MODULE_NAME="Repatch-pxm.dylib"
        DEFAULT_MODULE_DIR="$ROOT/bin/macosx/arm64"
        ARCH_PATTERN="arm64"
        ;;
    windows-x64)
        MODULE_NAME="Repatch-pxm.dll"
        DEFAULT_MODULE_DIR="$ROOT/bin/windows/x64"
        ARCH_PATTERN="x86-64"
        ;;
    *)
        fail "unsupported --platform=$PLATFORM (expected macosx-arm64 or windows-x64)"
        ;;
esac
[ -n "$MODULE_DIR" ] || MODULE_DIR="$DEFAULT_MODULE_DIR"

MODULE="$MODULE_DIR/$MODULE_NAME"
XSGN="$MODULE_DIR/Repatch-pxm.xsgn"
DOC="$ROOT/doc/tools/Repatch"

[ -f "$MODULE" ]           || fail "module not found: $MODULE (build it first)"
[ -f "$DOC/Repatch.html" ] || fail "documentation page not found: $DOC/Repatch.html"
file "$MODULE" | grep -q "$ARCH_PATTERN" || fail "module is not a $PLATFORM binary: $(file "$MODULE")"

if [ -z "$VERSION" ]; then
    tag="$(git -C "$ROOT" describe --tags --exact-match --match 'v*' 2>/dev/null || true)"
    if [ -n "$tag" ]; then
        VERSION="${tag#v}"
    else
        src="$ROOT/src/module/RepatchModule.cpp"
        v() { sed -n "s/^#define MODULE_VERSION_$1[[:space:]]*\([0-9]*\).*/\1/p" "$src"; }
        VERSION="$(v MAJOR).$(v MINOR).$(v REVISION)"
        [ "$VERSION" != ".." ] || fail "could not read MODULE_VERSION_* from $src"
    fi
fi

SIGNED=1
[ -f "$XSGN" ] || SIGNED=0
NAME="Repatch-$VERSION-$PLATFORM"
[ "$SIGNED" = 1 ] || NAME="$NAME-unsigned"

# A template ending in XXXXXX works with both GNU and BSD mktemp; `-t prefix`
# is BSD-only and fails under Git Bash.
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/repatch-package.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/bin" "$STAGE/doc/tools" "$OUT"
OUT="$(cd "$OUT" && pwd)"   # absolute: tar runs from inside $STAGE
ARCHIVE="$OUT/$NAME.tar.gz"
cp -p "$MODULE" "$STAGE/bin/"
[ "$SIGNED" = 1 ] && cp -p "$XSGN" "$STAGE/bin/"
cp -Rp "$DOC" "$STAGE/doc/tools/Repatch"
find "$STAGE" -name .DS_Store -delete
chmod -R a+rX "$STAGE"
# File mtimes are preserved from the sources; directories get the module's
# mtime, and owner ids are fixed, so the same inputs give the same archive.
find "$STAGE" -type d -exec touch -r "$MODULE" {} +

rm -f "$ARCHIVE"
# Fixing the owner ids needs different options per tar flavour: macOS ships
# bsdtar (--uid/--gid), Git Bash ships GNU tar (--owner/--group). GNU tar also
# accepts --sort=name, which removes the readdir order from the result; bsdtar
# has no equivalent, so there the directory walk order is relied upon as before.
if tar --version 2>/dev/null | grep -qi 'GNU tar'; then
    TAR_REPRO=( --owner=0 --group=0 --numeric-owner --sort=name )
else
    TAR_REPRO=( --uid 0 --gid 0 --numeric-owner )
fi
# gzip -n leaves the timestamp out of the gzip header.
( cd "$STAGE" && COPYFILE_DISABLE=1 tar "${TAR_REPRO[@]}" -cf - bin doc | gzip -n -9 > "$ARCHIVE" )
shasum -a 1 "$ARCHIVE" | cut -d' ' -f1 > "$ARCHIVE.sha1"

echo "Platform: $PLATFORM"
echo "Package: $ARCHIVE ($(du -h "$ARCHIVE" | cut -f1))"
echo "sha1:    $(cat "$ARCHIVE.sha1")"
tar -tzf "$ARCHIVE" | sed 's/^/  /'
if [ "$SIGNED" = 0 ]; then
    echo "WARNING: $XSGN not found; the package is UNSIGNED and PixInsight will refuse to install it."
    echo "         Sign the module first (scripts/dev_sign_module.sh or scripts/dev_sign_module.ps1),"
    echo "         then run this script again."
fi
