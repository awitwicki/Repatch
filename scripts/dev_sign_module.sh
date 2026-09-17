#!/bin/bash
#
# Signs the Repatch module with a PixInsight developer key (.xssk).
#
#   ./sign_module.sh [--xssk-file=PATH] [--module-file=PATH] [--pixinsight=PATH]
#
# The key password is read from the terminal with echo disabled. It is passed
# only to the PixInsight signing command and is never written to disk or
# printed. Close PixInsight before running this script.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
XSSK="$ROOT/../pikey.xssk"
MODULE="$ROOT/bin/macosx/arm64/Repatch-pxm.dylib"
PI="/Applications/PixInsight/PixInsight.app/Contents/MacOS/PixInsight"

fail() { printf '\033[0;31m[ERROR]\033[0m %s\n' "$1" >&2; exit 1; }

for arg in "$@"; do
    case "$arg" in
        --xssk-file=*)   XSSK="${arg#*=}" ;;
        --module-file=*) MODULE="${arg#*=}" ;;
        --pixinsight=*)  PI="${arg#*=}" ;;
        -h|--help)       sed -n '2,10p' "$0"; exit 0 ;;
        *)               fail "unknown option: $arg" ;;
    esac
done

[ -f "$XSSK" ]   || fail "signing key not found: $XSSK (use --xssk-file=PATH)"
[ -f "$MODULE" ] || fail "module not found: $MODULE (run ./build.sh first)"
[ -x "$PI" ]     || fail "PixInsight executable not found: $PI (use --pixinsight=PATH)"
[ -t 0 ]         || fail "the password must be typed interactively; run this script from a terminal"

XSGN="${MODULE%.dylib}.xsgn"
rm -f "$XSGN"

set +x
read -r -s -p "Password for $(basename "$XSSK"): " XSSK_PASSWORD
echo
[ -n "$XSSK_PASSWORD" ] || fail "empty password"

echo "Signing $MODULE"
"$PI" --sign-module-file="$MODULE" --xssk-file="$XSSK" --xssk-password="$XSSK_PASSWORD" --no-splash
unset XSSK_PASSWORD

[ -f "$XSGN" ] || fail "signing did not produce $XSGN"
echo "Signature written: $XSGN"
echo "If PixInsight has not yet registered this key's identity (once per machine):"
echo "  Edit > Local Signing Identity..., select $(basename "$XSSK"), enter its password,"
echo "  tick 'Make the local signing identity persistent', OK."
echo "Install from PixInsight: Process > Modules > Install Modules..., directory $(dirname "$MODULE")"
