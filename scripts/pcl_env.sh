#!/bin/bash
# Exports the PCL build environment (PCL README conventions) for macOS arm64.
# Usage: source scripts/pcl_env.sh [PCL_DIR]
# Default PCL_DIR: ../PCL relative to the repository root.

_repatch_scripts_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -n "${1:-}" ]; then
    PCLDIR="$(cd "$1" && pwd)"
elif [ -z "${PCLDIR:-}" ]; then
    PCLDIR="$(cd "$_repatch_scripts_dir/../../PCL" 2>/dev/null && pwd)"
fi
if [ -z "$PCLDIR" ] || [ ! -d "$PCLDIR/include/pcl" ]; then
    echo "pcl_env.sh: PCL not found (PCLDIR='$PCLDIR'); pass the path as an argument" >&2
    return 1 2>/dev/null || exit 1
fi

export PCLDIR
export PCLINCDIR="$PCLDIR/include"
export PCLSRCDIR="$PCLDIR/src"
export PCLLIBDIR64="$PCLDIR/lib/macosx/arm64"
export PCLLIBDIR="$PCLLIBDIR64"
export PCLBINDIR64="$PCLDIR/bin"
export PCLBINDIR="$PCLBINDIR64"
mkdir -p "$PCLLIBDIR64" "$PCLBINDIR64"
