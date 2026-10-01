#!/bin/bash
#
# Builds the PixInsight update-repository directory for the signed packages:
# dist/repo/updates.xri (signed) + the packages themselves, ready to be
# published at the repository URL that users enter in PixInsight
# (Resources > Updates > Manage Repositories).
#
#   scripts/make_repo.sh [--package=FILE]... [--base-url=URL] [--notes=FILE]
#                        [--out=DIR] [--core-versions=[PLATFORM=]A:B]... [--no-sign]
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
# --core-versions  PixInsight core version range (default 1.9.4:1.9.5), either
#             for every platform:            --core-versions=1.9.4:1.9.5
#             or for one of them:            --core-versions=windows-x64=1.9.5:1.9.5
#             Repeatable; a platform without an override uses the default.
#             A module requires a core whose API version is at least that of
#             the PCL it was built against, so a module built against a newer
#             PCL than another platform's needs a narrower range here.
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
# Per-platform overrides as "platform=range" lines. macOS still ships bash 3.2,
# which has no associative arrays, so this is a plain newline-separated list.
CORE_VERSIONS_OVERRIDES=""
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
        --core-versions=*)
            _cv="${arg#*=}"
            # A bare range is the default for every platform; "platform=range"
            # overrides one. The range itself contains colons, so the platform
            # is separated with "=", not ":".
            case "$_cv" in
                *=*) CORE_VERSIONS_OVERRIDES="$CORE_VERSIONS_OVERRIDES
${_cv%%=*}=${_cv#*=}" ;;
                *)   CORE_VERSIONS="$_cv" ;;
            esac
            ;;
        --no-sign)         SIGN=0 ;;
        --xssk-file=*)     XSSK="${arg#*=}" ;;
        --pixinsight=*)    PI="${arg#*=}" ;;
        -h|--help)         sed -n '2,29p' "$0"; exit 0 ;;
        *)                 fail "unknown option: $arg" ;;
    esac
done

# Which kind of shell host this is. Windows has two bash flavours and they
# differ in both the mount prefix and the path-translation tool, so they cannot
# be lumped together: Git Bash sees C: as /c, WSL as /mnt/c.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) HOST_KIND=msys ;;
    Darwin)               HOST_KIND=macos ;;
    Linux)
        if grep -qi microsoft /proc/version 2>/dev/null || [ -n "${WSL_DISTRO_NAME:-}" ]
        then HOST_KIND=wsl
        else HOST_KIND=unix
        fi
        ;;
    *)                    HOST_KIND=unix ;;
esac

# PixInsight.exe is a native Windows program: under WSL it will not understand
# a /mnt/e/... argument, and under Git Bash the automatic MSYS translation is
# easy to defeat with an =-joined option, so paths handed to it are converted
# explicitly here.
native_path() {
    case "$HOST_KIND" in
        wsl)  wslpath -w "$1" ;;
        msys) cygpath -w "$1" ;;
        *)    printf '%s\n' "$1" ;;
    esac
}

if [ -z "$PI" ]; then
    case "$HOST_KIND" in
        msys) PI="/c/Program Files/PixInsight/bin/PixInsight.exe" ;;
        wsl)  PI="/mnt/c/Program Files/PixInsight/bin/PixInsight.exe" ;;
        *)    PI="/Applications/PixInsight/PixInsight.app/Contents/MacOS/PixInsight" ;;
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
platform_label()  { case "$1" in macosx-arm64) echo "macOS (Apple Silicon)" ;; windows-x64) echo "Windows (x64)" ;; esac; }

# The --core-versions override for a platform, or the global default.
platform_core_versions() {
    local found
    found="$(printf '%s\n' "$CORE_VERSIONS_OVERRIDES" | sed -n "s/^$1=\(.*\)$/\1/p" | tail -1)"
    if [ -n "$found" ]; then echo "$found"; else echo "$CORE_VERSIONS"; fi
}

# "1.9.4:1.9.5" -> "1.9.4 or 1.9.5"; "1.9.5:1.9.5" -> "1.9.5". Used for the
# default release notes so they cannot contradict the version attributes.
core_versions_prose() {
    local lo="${1%%:*}" hi="${1##*:}"
    if [ "$lo" = "$hi" ]; then echo "$lo"; else echo "$lo or $hi"; fi
}

VERSION=""
PLATFORM_BLOCKS=""
REQUIREMENTS=""
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

    PKG_CORE="$(platform_core_versions "$PLATFORM")"
    echo "  $PLATFORM  $FILE  sha1=$SHA1  core=$PKG_CORE"
    REQUIREMENTS="$REQUIREMENTS, PixInsight $(core_versions_prose "$PKG_CORE") on $(platform_label "$PLATFORM")"
    PLATFORM_BLOCKS="$PLATFORM_BLOCKS
   <platform os=\"$(platform_os "$PLATFORM")\" arch=\"$(platform_arch "$PLATFORM")\" version=\"$PKG_CORE\">
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
    # REQUIREMENTS was accumulated per package, so the prose always agrees with
    # the version attributes emitted above. It starts with ", ".
    NOTES_HTML="<p>Repatch $VERSION: content-aware fill (PatchMatch) for PixInsight, with a healing-brush interface and a mask-image mode. Requires ${REQUIREMENTS#, }.</p>"
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
xml_is_well_formed() {
    if command -v xmllint >/dev/null 2>&1; then
        xmllint --noout "$1" 2>/dev/null
        return
    fi
    if command -v python3 >/dev/null 2>&1; then
        python3 -c 'import sys,xml.etree.ElementTree as E; E.parse(sys.argv[1])' "$1" 2>/dev/null
        return
    fi
    # Git Bash has `powershell`; WSL reaches it as `powershell.exe` over interop.
    local ps
    for ps in powershell powershell.exe pwsh pwsh.exe; do
        if command -v "$ps" >/dev/null 2>&1; then
            "$ps" -NoProfile -Command \
                "try { [xml](Get-Content -Raw '$(native_path "$1")') | Out-Null; exit 0 } catch { exit 1 }" \
                >/dev/null 2>&1
            return
        fi
    done
    return 2   # no validator available
}

# The `|| xml_status=$?` form keeps set -e from aborting on a validation
# failure, and captures the 2 that means "no validator available".
xml_status=0
xml_is_well_formed "$XRI" || xml_status=$?
case "$xml_status" in
    0) ;;
    2) echo "WARNING: no XML validator (xmllint, python3 or powershell) found; skipping validation of $XRI" ;;
    *) fail "generated $XRI is not well-formed XML (check --notes)" ;;
esac

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
    "$PI" --sign-xml-file="$(native_path "$XRI")" \
          --xssk-file="$(native_path "$XSSK")" \
          --xssk-password="$XSSK_PASSWORD" --no-splash || true
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
