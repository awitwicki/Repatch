#!/bin/bash
#
# Assembles a PixInsight update package from the built module and the
# documentation page.
#
#   scripts/package.sh [--version=X.Y.Z] [--out=DIR] [--module-dir=DIR]
#
# The archive mirrors the PixInsight installation root, which is the layout
# the PixInsight updater (updates.xri, type="module") unpacks into
# /Applications/PixInsight:
#
#   bin/Repatch-pxm.dylib
#   bin/Repatch-pxm.xsgn          (only when the module has been signed)
#   doc/tools/Repatch/Repatch.html
#   doc/tools/Repatch/images/...
#
# Writes <out>/Repatch-<version>-macosx-arm64[-unsigned].tar.gz plus a .sha1
# file (the updater's <package sha1="..."> attribute). The version comes from
# --version, else from an exact git tag vX.Y.Z on HEAD, else from the
# MODULE_VERSION_* defines in src/module/RepatchModule.cpp.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION=""
OUT="$ROOT/dist"
MODULE_DIR="$ROOT/bin/macosx/arm64"

fail() { printf '\033[0;31m[ERROR]\033[0m %s\n' "$1" >&2; exit 1; }

for arg in "$@"; do
    case "$arg" in
        --version=*)    VERSION="${arg#*=}" ;;
        --out=*)        OUT="${arg#*=}" ;;
        --module-dir=*) MODULE_DIR="${arg#*=}" ;;
        -h|--help)      sed -n '2,20p' "$0"; exit 0 ;;
        *)              fail "unknown option: $arg" ;;
    esac
done

DYLIB="$MODULE_DIR/Repatch-pxm.dylib"
XSGN="$MODULE_DIR/Repatch-pxm.xsgn"
DOC="$ROOT/doc/tools/Repatch"

[ -f "$DYLIB" ]            || fail "module not found: $DYLIB (run ./build.sh first)"
[ -f "$DOC/Repatch.html" ] || fail "documentation page not found: $DOC/Repatch.html"
file "$DYLIB" | grep -q arm64 || fail "module is not an arm64 binary: $(file "$DYLIB")"

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
NAME="Repatch-$VERSION-macosx-arm64"
[ "$SIGNED" = 1 ] || NAME="$NAME-unsigned"
ARCHIVE="$OUT/$NAME.tar.gz"

STAGE="$(mktemp -d -t repatch-package)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/bin" "$STAGE/doc/tools" "$OUT"
OUT="$(cd "$OUT" && pwd)"   # absolute: tar runs from inside $STAGE
ARCHIVE="$OUT/$NAME.tar.gz"
cp -p "$DYLIB" "$STAGE/bin/"
[ "$SIGNED" = 1 ] && cp -p "$XSGN" "$STAGE/bin/"
cp -Rp "$DOC" "$STAGE/doc/tools/Repatch"
find "$STAGE" -name .DS_Store -delete
chmod -R a+rX "$STAGE"
# File mtimes are preserved from the sources; directories get the module's
# mtime, and owner ids are fixed, so the same inputs give the same archive.
find "$STAGE" -type d -exec touch -r "$DYLIB" {} +

rm -f "$ARCHIVE"
# gzip -n leaves the timestamp out of the gzip header.
( cd "$STAGE" && COPYFILE_DISABLE=1 tar --uid 0 --gid 0 --numeric-owner -cf - bin doc | gzip -n -9 > "$ARCHIVE" )
shasum -a 1 "$ARCHIVE" | cut -d' ' -f1 > "$ARCHIVE.sha1"

echo "Package: $ARCHIVE ($(du -h "$ARCHIVE" | cut -f1))"
echo "sha1:    $(cat "$ARCHIVE.sha1")"
tar -tzf "$ARCHIVE" | sed 's/^/  /'
if [ "$SIGNED" = 0 ]; then
    echo "WARNING: $XSGN not found; the package is UNSIGNED and PixInsight will refuse to install it."
    echo "         Sign locally with ./sign_module.sh, then run this script again."
fi
