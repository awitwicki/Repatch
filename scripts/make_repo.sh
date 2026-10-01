#!/bin/bash
#
# Builds the PixInsight update-repository directory for a signed package:
# dist/repo/updates.xri (signed) + the package itself, ready to be published
# at the repository URL that users enter in PixInsight
# (Resources > Updates > Manage Repositories).
#
#   scripts/make_repo.sh [--package=FILE] [--base-url=URL] [--notes=FILE]
#                        [--out=DIR] [--no-sign] [--xssk-file=PATH] [--pixinsight=PATH]
#
# --package   signed package from scripts/package.sh; default: the newest
#             dist/Repatch-*-macosx-arm64.tar.gz (unsigned packages are refused)
# --base-url  where updates.xri and the package will be served from;
#             PixInsight fetches <base-url>updates.xri (default: GitHub Pages
#             of this repository)
# --notes     HTML fragment (<p>...</p>) with the release notes
# --no-sign   skip signing (PixInsight rejects unsigned repositories unless
#             Security/AllowUnsignedRepositories is enabled)
#
# Signing runs PixInsight --sign-xml-file with the .xssk key; the password is
# read from the terminal with echo disabled and never stored.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PACKAGE=""
BASE_URL="https://awitwicki.github.io/Repatch/"
NOTES=""
OUT="$ROOT/dist/repo"
SIGN=1
XSSK="$ROOT/pikey.xssk"
PI="/Applications/PixInsight/PixInsight.app/Contents/MacOS/PixInsight"

fail() { printf '\033[0;31m[ERROR]\033[0m %s\n' "$1" >&2; exit 1; }

for arg in "$@"; do
    case "$arg" in
        --package=*)    PACKAGE="${arg#*=}" ;;
        --base-url=*)   BASE_URL="${arg#*=}" ;;
        --notes=*)      NOTES="${arg#*=}" ;;
        --out=*)        OUT="${arg#*=}" ;;
        --no-sign)      SIGN=0 ;;
        --xssk-file=*)  XSSK="${arg#*=}" ;;
        --pixinsight=*) PI="${arg#*=}" ;;
        -h|--help)      sed -n '2,22p' "$0"; exit 0 ;;
        *)              fail "unknown option: $arg" ;;
    esac
done

if [ -z "$PACKAGE" ]; then
    PACKAGE="$(ls -t "$ROOT"/dist/Repatch-*-macosx-arm64.tar.gz 2>/dev/null | grep -v -- '-unsigned' | head -1 || true)"
    [ -n "$PACKAGE" ] || fail "no signed package in $ROOT/dist (run scripts/dev_sign_module.sh and scripts/package.sh first)"
fi
[ -f "$PACKAGE" ] || fail "package not found: $PACKAGE"
case "$(basename "$PACKAGE")" in
    *-unsigned.tar.gz) fail "$(basename "$PACKAGE") is an unsigned package; sign the module and re-run scripts/package.sh" ;;
esac
tar -tzf "$PACKAGE" | grep -q '^bin/Repatch-pxm.xsgn$' || fail "the package does not contain bin/Repatch-pxm.xsgn"
[ "${BASE_URL: -1}" = "/" ] || BASE_URL="$BASE_URL/"

FILE="$(basename "$PACKAGE")"
VERSION="$(printf '%s' "$FILE" | sed -n 's/^Repatch-\(.*\)-macosx-arm64\.tar\.gz$/\1/p')"
[ -n "$VERSION" ] || fail "cannot read the version from the package name: $FILE"
if [ -f "$PACKAGE.sha1" ]; then SHA1="$(cat "$PACKAGE.sha1")"; else SHA1="$(shasum -a 1 "$PACKAGE" | cut -d' ' -f1)"; fi
DATE="$(date -u +%Y%m%d)"
META="$DATE-repatch-$VERSION"

if [ -n "$NOTES" ]; then
    [ -f "$NOTES" ] || fail "notes file not found: $NOTES"
    NOTES_HTML="$(cat "$NOTES")"
else
    NOTES_HTML="<p>Repatch $VERSION: content-aware fill (PatchMatch) for PixInsight, with a healing-brush interface and a mask-image mode. Requires PixInsight 1.9.4 or 1.9.5 on macOS (Apple Silicon).</p>"
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
   </metadata>
   <platform os="macosx" arch="arm64" version="1.9.4:1.9.5">
      <package fileName="$FILE" serverURL="$BASE_URL" sha1="$SHA1" type="module" metadata="$META">
         <remove>
            bin/Repatch-pxm.dylib, bin/Repatch-pxm.xsgn
         </remove>
      </package>
   </platform>
</xri>
EOF
xmllint --noout "$XRI" 2>/dev/null || fail "generated $XRI is not well-formed XML (check --notes)"
cp -p "$PACKAGE" "$OUT/$FILE"
[ -f "$PACKAGE.sha1" ] && cp -p "$PACKAGE.sha1" "$OUT/$FILE.sha1"

if [ "$SIGN" = 1 ]; then
    [ -f "$XSSK" ] || fail "signing key not found: $XSSK (use --xssk-file=PATH or --no-sign)"
    [ -x "$PI" ]   || fail "PixInsight executable not found: $PI (use --pixinsight=PATH)"
    [ -t 0 ]       || fail "the password must be typed interactively; run this script from a terminal"
    read -r -s -p "Password for $(basename "$XSSK"): " XSSK_PASSWORD
    echo
    [ -n "$XSSK_PASSWORD" ] || fail "empty password"
    echo "Signing $XRI"
    "$PI" --sign-xml-file="$XRI" --xssk-file="$XSSK" --xssk-password="$XSSK_PASSWORD" --no-splash
    unset XSSK_PASSWORD
    if grep -q '<Signature' "$XRI"; then
        echo "Signature embedded in $XRI"
    elif ls "$XRI".xsgn "$OUT"/updates.xsgn >/dev/null 2>&1; then
        echo "Signature written next to $XRI: $(ls "$XRI".xsgn "$OUT"/updates.xsgn 2>/dev/null | tr '\n' ' ')"
    else
        fail "signing did not add a signature to $XRI"
    fi
fi

echo
echo "Repository directory: $OUT"
ls -la "$OUT" | tail -n +2 | awk '{print "  " $5 "\t" $9}'
echo
echo "Publish every file in $OUT at $BASE_URL"
echo "Repository URL for PixInsight (Resources > Updates > Manage Repositories): $BASE_URL"
