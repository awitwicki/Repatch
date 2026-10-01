#!/bin/bash
#
# Builds the PixInsight update-repository directory for the signed packages:
# dist/repo/updates.xri (signed) + the packages themselves, ready to be
# published at the repository URL that users enter in PixInsight
# (Resources > Updates > Manage Repositories).
#
#   scripts/make_repo.sh [--package=FILE]... [--base-url=URL] [--notes=FILE]
#                        [--out=DIR] [--core-versions=A:B] [--no-sign]
#                        [--xssk-file=PATH] [--pixinsight=PATH]
#
# --package   signed package from scripts/package.sh; may be repeated, once
#             per platform. Default: every signed dist/Repatch-*.tar.gz.
#             updates.xri is a single catalogue for all platforms, so every
#             platform still being offered must be passed on every run --
#             a package left out disappears from the repository.
# --base-url  where updates.xri and the packages will be served from;
#             PixInsight fetches <base-url>updates.xri (default: GitHub Pages
#             of this repository)
# --notes     HTML fragment (<p>...</p>) with the release notes
# --core-versions  PixInsight core version range per platform (default
#             1.9.4:1.9.5). Note that a module requires a core whose API
#             version is at least that of the PCL it was built against.
# --no-sign   skip signing (PixInsight rejects unsigned repositories unless
#             Security/AllowUnsignedRepositories is enabled)
#
# Signing runs PixInsight --sign-xml-file with the .xssk key; the password is
# read from the terminal with echo disabled and never stored.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PACKAGES=()
BASE_URL="https://awitwicki.github.io/Repatch/"
NOTES=""
OUT="$ROOT/dist/repo"
CORE_VERSIONS="1.9.4:1.9.5"
SIGN=1
XSSK="$ROOT/pikey.xssk"
PI=""

fail() { printf '\033[0;31m[ERROR]\033[0m %s\n' "$1" >&2; exit 1; }

for arg in "$@"; do
    case "$arg" in
        --package=*)       PACKAGES+=( "${arg#*=}" ) ;;
        --base-url=*)      BASE_URL="${arg#*=}" ;;
        --notes=*)         NOTES="${arg#*=}" ;;
        --out=*)           OUT="${arg#*=}" ;;
        --core-versions=*) CORE_VERSIONS="${arg#*=}" ;;
        --no-sign)         SIGN=0 ;;
        --xssk-file=*)     XSSK="${arg#*=}" ;;
        --pixinsight=*)    PI="${arg#*=}" ;;
        -h|--help)         sed -n '2,29p' "$0"; exit 0 ;;
        *)                 fail "unknown option: $arg" ;;
    esac
done

if [ -z "$PI" ]; then
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*) PI="/c/Program Files/PixInsight/bin/PixInsight.exe" ;;
        *) PI="/Applications/PixInsight/PixInsight.app/Contents/MacOS/PixInsight" ;;
    esac
fi

if [ "${#PACKAGES[@]}" -eq 0 ]; then
    while IFS= read -r p; do
        [ -n "$p" ] && PACKAGES+=( "$p" )
    done < <(ls -t "$ROOT"/dist/Repatch-*.tar.gz 2>/dev/null | grep -v -- '-unsigned' || true)
    [ "${#PACKAGES[@]}" -gt 0 ] || fail "no signed package in $ROOT/dist (sign the module, then run scripts/package.sh)"
fi

[ "${BASE_URL: -1}" = "/" ] || BASE_URL="$BASE_URL/"

# Maps a package's platform suffix onto the updates.xri attributes and the
# module file the updater has to delete before unpacking the new one.
platform_os()     { case "$1" in macosx-arm64) echo macosx ;; windows-x64) echo windows ;; esac; }
platform_arch()   { case "$1" in macosx-arm64) echo arm64  ;; windows-x64) echo x64     ;; esac; }
platform_module() { case "$1" in macosx-arm64) echo Repatch-pxm.dylib ;; windows-x64) echo Repatch-pxm.dll ;; esac; }

VERSION=""
PLATFORM_BLOCKS=""
for PACKAGE in "${PACKAGES[@]}"; do
    [ -f "$PACKAGE" ] || fail "package not found: $PACKAGE"
    FILE="$(basename "$PACKAGE")"
    case "$FILE" in
        *-unsigned.tar.gz) fail "$FILE is an unsigned package; sign the module and re-run scripts/package.sh" ;;
    esac
    # The listing is captured rather than piped into `grep -q`: grep exits at
    # the first match and closes the pipe, tar dies of SIGPIPE with status 141,
    # and `set -o pipefail` then fails the whole check. Whether that happens is
    # a race against tar's output, so piping here fails intermittently.
    LISTING="$(tar -tzf "$PACKAGE" 2>/dev/null)"
    printf '%s\n' "$LISTING" | grep -q '^bin/Repatch-pxm.xsgn$' \
        || fail "$FILE does not contain bin/Repatch-pxm.xsgn"

    # Repatch-<version>-<platform>.tar.gz
    PKG_VERSION="$(printf '%s' "$FILE" | sed -n 's/^Repatch-\(.*\)-\(macosx-arm64\|windows-x64\)\.tar\.gz$/\1/p')"
    PLATFORM="$(printf '%s' "$FILE" | sed -n 's/^Repatch-.*-\(macosx-arm64\|windows-x64\)\.tar\.gz$/\1/p')"
    [ -n "$PKG_VERSION" ] && [ -n "$PLATFORM" ] \
        || fail "cannot read version and platform from the package name: $FILE"
    if [ -z "$VERSION" ]; then
        VERSION="$PKG_VERSION"
    elif [ "$VERSION" != "$PKG_VERSION" ]; then
        fail "packages disagree on the version: $VERSION vs $PKG_VERSION ($FILE)"
    fi

    if [ -f "$PACKAGE.sha1" ]; then SHA1="$(cat "$PACKAGE.sha1")"; else SHA1="$(shasum -a 1 "$PACKAGE" | cut -d' ' -f1)"; fi
    # The sha1 is what PixInsight verifies after download, so a stale .sha1
    # file beside the package would silently publish a broken entry.
    ACTUAL="$(shasum -a 1 "$PACKAGE" | cut -d' ' -f1)"
    [ "$SHA1" = "$ACTUAL" ] || fail "$FILE.sha1 says $SHA1 but the file hashes to $ACTUAL"

    echo "  $PLATFORM  $FILE  sha1=$SHA1"
    PLATFORM_BLOCKS="$PLATFORM_BLOCKS
   <platform os=\"$(platform_os "$PLATFORM")\" arch=\"$(platform_arch "$PLATFORM")\" version=\"$CORE_VERSIONS\">
      <package fileName=\"$FILE\" serverURL=\"$BASE_URL\" sha1=\"$SHA1\" type=\"module\" metadata=\"@@META@@\">
         <remove>
            bin/$(platform_module "$PLATFORM"), bin/Repatch-pxm.xsgn
         </remove>
      </package>
   </platform>"
done

DATE="$(date -u +%Y%m%d)"
META="$DATE-repatch-$VERSION"
PLATFORM_BLOCKS="${PLATFORM_BLOCKS//@@META@@/$META}"

if [ -n "$NOTES" ]; then
    [ -f "$NOTES" ] || fail "notes file not found: $NOTES"
    NOTES_HTML="$(cat "$NOTES")"
else
    NOTES_HTML="<p>Repatch $VERSION: content-aware fill (PatchMatch) for PixInsight, with a healing-brush interface and a mask-image mode. Requires PixInsight 1.9.4 or 1.9.5 on macOS (Apple Silicon) or Windows (x64).</p>"
fi

mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"   # absolute: PixInsight's working directory is not ours
XRI="$OUT/updates.xri"
cat > "$XRI" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<xri version="1.0">
   <description>
      <p>Repatch &#8212; content-aware fill for PixInsight. Update repository of https://github.com/awitwicki/Repatch</p>
   </description>
   <metadata id="$META" releaseDate="$DATE">
      <title>Repatch Module Version $VERSION</title>
      <description>
         $NOTES_HTML
      </description>
   </metadata>$PLATFORM_BLOCKS
</xri>
EOF

# Validate before signing: PixInsight appends <Signature> after </xri>, which
# leaves a second top-level element, so the signed file is not well-formed XML
# and cannot be checked afterwards. xmllint is not present on Windows, where
# PowerShell's XML parser does the same job.
if command -v xmllint >/dev/null 2>&1; then
    xmllint --noout "$XRI" 2>/dev/null || fail "generated $XRI is not well-formed XML (check --notes)"
elif command -v powershell >/dev/null 2>&1; then
    powershell -NoProfile -Command "try { [xml](Get-Content -Raw '$(cygpath -w "$XRI" 2>/dev/null || echo "$XRI")') | Out-Null; exit 0 } catch { exit 1 }" \
        || fail "generated $XRI is not well-formed XML (check --notes)"
else
    echo "WARNING: neither xmllint nor powershell found; skipping XML validation of $XRI"
fi

for PACKAGE in "${PACKAGES[@]}"; do
    cp -p "$PACKAGE" "$OUT/$(basename "$PACKAGE")"
    [ -f "$PACKAGE.sha1" ] && cp -p "$PACKAGE.sha1" "$OUT/$(basename "$PACKAGE").sha1"
done

if [ "$SIGN" = 1 ]; then
    [ -f "$XSSK" ] || fail "signing key not found: $XSSK (use --xssk-file=PATH or --no-sign)"
    [ -f "$PI" ]   || fail "PixInsight executable not found: $PI (use --pixinsight=PATH)"
    [ -t 0 ]       || fail "the password must be typed interactively; run this script from a terminal"
    read -r -s -p "Password for $(basename "$XSSK"): " XSSK_PASSWORD
    echo
    [ -n "$XSSK_PASSWORD" ] || fail "empty password"
    echo "Signing $XRI"
    "$PI" --sign-xml-file="$XRI" --xssk-file="$XSSK" --xssk-password="$XSSK_PASSWORD" --no-splash || true
    unset XSSK_PASSWORD
    # PixInsight is a GUI binary on Windows: it reports nothing useful on the
    # console, can exit nonzero after a successful run, and may write the file
    # a moment after exiting. The signature in the file is the only indicator.
    for _ in $(seq 60); do
        grep -q '<Signature' "$XRI" && break
        sleep 0.5
    done
    if grep -q '<Signature' "$XRI"; then
        echo "Signature embedded in $XRI"
    else
        fail "signing did not add a signature to $XRI (is the .xssk password correct, and is the key's identity registered in PixInsight: Edit > Local Signing Identity...?)"
    fi
fi

echo
echo "Repository directory: $OUT"
ls -la "$OUT" | tail -n +2 | awk '{print "  " $5 "\t" $9}'
echo
echo "Publish every file in $OUT at $BASE_URL"
echo "Repository URL for PixInsight (Resources > Updates > Manage Repositories): $BASE_URL"
