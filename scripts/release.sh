#!/bin/bash
#
# Makes a complete release from one Mac, without a Windows machine:
#
#   1. takes the unsigned modules that CI built for the current commit
#      (Repatch-pxm.dylib from the macOS job, Repatch-pxm.dll from the Windows
#      job), downloaded by hand from the run's page on GitHub (Actions ->
#      the build run for this commit -> Artifacts);
#   2. signs both with the developer key, asking for its password once;
#   3. packages both (scripts/package.sh);
#   4. writes the signed update repository (scripts/make_repo.sh).
#
#   scripts/release.sh [--notes=FILE] [--core-versions=A:B] [--modules-dir=DIR]
#                      [--xssk-file=PATH] [--pixinsight=PATH]
#
# --notes          HTML fragment with the release notes (passed to make_repo.sh)
# --core-versions  PixInsight core range for every platform (default: the
#                  make_repo.sh default)
# --modules-dir    where the two artifacts were downloaded (default ~/Downloads).
#                  Download Repatch-module-macosx-arm64-<sha> and
#                  Repatch-module-windows-x64-<sha>, where <sha> is the full
#                  hash of HEAD. Each may be left as the .zip GitHub serves, or
#                  as the folder Safari unpacks it into; both carry the commit
#                  in their name, which is checked against HEAD. As a last
#                  resort, loose Repatch-pxm.dylib and Repatch-pxm.dll files in
#                  DIR are taken too, with a warning (nothing ties them to HEAD).
# --xssk-file      signing key (default: pikey.xssk, the developer key; a
#                  local test key only works on machines that registered it)
# --pixinsight     PixInsight executable
#
# Requirements: a clean working tree whose HEAD has been pushed and built by
# CI (the packages take the documentation page from the working tree, so it
# must match the commit the modules were built from); PixInsight closed. The
# password is read with echo disabled and never stored.
#
# Afterwards: copy dist/repo/* to the GitHub Pages repository (removing the
# packages that are no longer offered), push the tag and create the GitHub
# release on the web; the script prints the steps.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NOTES=""
CORE_VERSIONS=""
MODULES_DIR="$HOME/Downloads"
XSSK="$ROOT/pikey.xssk"
PI="/Applications/PixInsight/PixInsight.app/Contents/MacOS/PixInsight"

info() { printf '\033[0;34m[INFO]\033[0m %s\n' "$1"; }
fail() { printf '\033[0;31m[ERROR]\033[0m %s\n' "$1" >&2; exit 1; }

for arg in "$@"; do
    case "$arg" in
        --notes=*)         NOTES="${arg#*=}" ;;
        --core-versions=*) CORE_VERSIONS="${arg#*=}" ;;
        --modules-dir=*)   MODULES_DIR="${arg#*=}" ;;
        --xssk-file=*)     XSSK="${arg#*=}" ;;
        --pixinsight=*)    PI="${arg#*=}" ;;
        -h|--help)         sed -n '2,38p' "$0"; exit 0 ;;
        *)                 fail "unknown option: $arg" ;;
    esac
done

[ "$(uname -s)" = Darwin ] || fail "run this script on macOS"
[ -f "$XSSK" ] || fail "signing key not found: $XSSK (use --xssk-file=PATH)"
[ -x "$PI" ]   || fail "PixInsight executable not found: $PI (use --pixinsight=PATH)"
[ -t 0 ]       || fail "the password must be typed interactively; run this script from a terminal"
[ -z "$NOTES" ] || [ -f "$NOTES" ] || fail "notes file not found: $NOTES"
[ -z "$NOTES" ] || NOTES="$(cd "$(dirname "$NOTES")" && pwd)/$(basename "$NOTES")"

cd "$ROOT"
SHA="$(git rev-parse HEAD)"
# Untracked files do not matter (dist/, keys); tracked changes do.
git diff --quiet HEAD -- || fail "the working tree has uncommitted changes; commit and push them first"

MAC_DIR="$ROOT/bin/macosx/arm64"
WIN_DIR="$ROOT/bin/windows/x64"
mkdir -p "$MAC_DIR" "$WIN_DIR"
rm -f "$MAC_DIR/Repatch-pxm.dylib" "$MAC_DIR/Repatch-pxm.xsgn" \
      "$WIN_DIR/Repatch-pxm.dll"   "$WIN_DIR/Repatch-pxm.xsgn"

# --- 1. modules ---------------------------------------------------------------
[ -d "$MODULES_DIR" ] || fail "modules directory not found: $MODULES_DIR"
MODULES_DIR="$(cd "$MODULES_DIR" && pwd)"

# take_module <platform> <file> <destination dir>
take_module() {
    local artifact="Repatch-module-$1-$SHA" file="$2" dest="$3"
    if [ -f "$MODULES_DIR/$artifact.zip" ]; then
        info "unzipping $artifact.zip"
        unzip -o -q -j "$MODULES_DIR/$artifact.zip" "$file" -d "$dest" \
            || fail "$artifact.zip does not contain $file"
    elif [ -f "$MODULES_DIR/$artifact/$file" ]; then
        info "taking $artifact/$file"
        cp -p "$MODULES_DIR/$artifact/$file" "$dest/"
    elif [ -f "$MODULES_DIR/$file" ]; then
        printf '\033[0;33m[WARN]\033[0m taking loose %s; it cannot be checked against HEAD %s\n' \
            "$MODULES_DIR/$file" "$SHA"
        cp -p "$MODULES_DIR/$file" "$dest/"
    else
        local other
        other="$(ls -d "$MODULES_DIR"/Repatch-module-"$1"-* 2>/dev/null | head -1 || true)"
        [ -z "$other" ] || fail "found $(basename "$other"), but HEAD is $SHA; download the artifact of the run for HEAD"
        fail "$artifact(.zip) not found in $MODULES_DIR; download it from the build run of this commit (Actions -> run -> Artifacts)"
    fi
}
take_module macosx-arm64 Repatch-pxm.dylib "$MAC_DIR"
take_module windows-x64  Repatch-pxm.dll   "$WIN_DIR"
file "$MAC_DIR/Repatch-pxm.dylib" | grep -q arm64  || fail "Repatch-pxm.dylib is not an arm64 binary"
file "$WIN_DIR/Repatch-pxm.dll"   | grep -q x86-64 || fail "Repatch-pxm.dll is not an x86-64 binary"

# --- 2. signatures ------------------------------------------------------------
set +x
read -r -s -p "Password for $(basename "$XSSK"): " XSSK_PASSWORD
echo
[ -n "$XSSK_PASSWORD" ] || fail "empty password"

for MODULE in "$MAC_DIR/Repatch-pxm.dylib" "$WIN_DIR/Repatch-pxm.dll"; do
    info "signing $MODULE"
    "$PI" --sign-module-file="$MODULE" --xssk-file="$XSSK" \
          --xssk-password="$XSSK_PASSWORD" --no-splash || true
    [ -f "${MODULE%.*}.xsgn" ] || { unset XSSK_PASSWORD; fail "signing did not produce ${MODULE%.*}.xsgn (wrong password, or PixInsight still running?)"; }
done

# --- 3. packages --------------------------------------------------------------
mkdir -p "$ROOT/dist"
rm -f "$ROOT"/dist/Repatch-*.tar.gz "$ROOT"/dist/Repatch-*.tar.gz.sha1
bash scripts/package.sh --platform=macosx-arm64 --module-dir="$MAC_DIR" --out="$ROOT/dist"
bash scripts/package.sh --platform=windows-x64  --module-dir="$WIN_DIR" --out="$ROOT/dist"
PACKAGES=( "$ROOT"/dist/Repatch-*-macosx-arm64.tar.gz "$ROOT"/dist/Repatch-*-windows-x64.tar.gz )
for p in "${PACKAGES[@]}"; do
    [ -f "$p" ] || { unset XSSK_PASSWORD; fail "package missing: $p"; }
done

# --- 4. update repository -----------------------------------------------------
rm -rf "$ROOT/dist/repo"
REPO_ARGS=( "--package=${PACKAGES[0]}" "--package=${PACKAGES[1]}" )
[ -z "$NOTES" ]         || REPO_ARGS+=( "--notes=$NOTES" )
[ -z "$CORE_VERSIONS" ] || REPO_ARGS+=( "--core-versions=$CORE_VERSIONS" )
REPATCH_XSSK_PASSWORD="$XSSK_PASSWORD" bash scripts/make_repo.sh \
    "${REPO_ARGS[@]}" "--xssk-file=$XSSK" "--pixinsight=$PI"
unset XSSK_PASSWORD

VERSION="$(basename "${PACKAGES[0]}" | sed 's/^Repatch-\(.*\)-macosx-arm64\.tar\.gz$/\1/')"
cat <<EOF

Release $VERSION is ready in dist/repo. To publish:

  cd ../Repatch-pages
  git rm -q --ignore-unmatch 'Repatch-*.tar.gz' 'Repatch-*.tar.gz.sha1'
  cp "$ROOT"/dist/repo/* .
  git add -A && git commit -m "Repatch $VERSION" && git push
  cd "$ROOT"

  git tag v$VERSION $SHA && git push origin v$VERSION

Then on GitHub: Releases -> Draft a new release -> tag v$VERSION, title
"Repatch $VERSION", attach the four files dist/Repatch-$VERSION-*.tar.gz and
dist/Repatch-$VERSION-*.tar.gz.sha1, publish.
EOF
