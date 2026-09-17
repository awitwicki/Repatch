#!/bin/bash
#
# Installs the Repatch documentation page into PixInsight, so the dialog's
# "Browse Documentation" button finds it.
#
#   scripts/install_docs.sh [--pixinsight-dir=DIR]
#
# Copies doc/tools/Repatch/ (Repatch.html + images/) from this repository to
# <PixInsight>/doc/tools/Repatch/. That tree is owned by root, so the copy is
# done with sudo when needed (your login password may be requested). Release
# packages ship the same directory, so nothing is generated here.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PIDIR="/Applications/PixInsight"

fail() { printf '\033[0;31m[ERROR]\033[0m %s\n' "$1" >&2; exit 1; }

for arg in "$@"; do
    case "$arg" in
        --pixinsight-dir=*) PIDIR="${arg#*=}" ;;
        -h|--help)          sed -n '2,11p' "$0"; exit 0 ;;
        *)                  fail "unknown option: $arg" ;;
    esac
done

SRC="$ROOT/doc/tools/Repatch"
DST="$PIDIR/doc/tools/Repatch"

[ -f "$SRC/Repatch.html" ]  || fail "documentation page not found: $SRC/Repatch.html"
[ -d "$PIDIR/doc/tools" ]   || fail "PixInsight documentation tree not found: $PIDIR/doc/tools (use --pixinsight-dir=DIR)"

SUDO=""
if [ ! -w "$PIDIR/doc/tools" ] || { [ -d "$DST" ] && [ ! -w "$DST" ]; }; then
    echo "$PIDIR/doc/tools is owned by $(stat -f %Su "$PIDIR/doc/tools"); copying with sudo."
    SUDO="sudo"
fi

echo "Installing $SRC -> $DST"
$SUDO rm -rf "$DST"
$SUDO cp -R "$SRC" "$DST"
$SUDO chmod -R a+rX "$DST"

cmp -s "$SRC/Repatch.html" "$DST/Repatch.html" || fail "installed page differs from the repository copy"
echo "Installed: $DST/Repatch.html"
echo "Open it with the 'Browse Documentation' button of the Repatch dialog (reopen the dialog if it was open)."
