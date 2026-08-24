#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
DLX_DIR="$SCRIPT_DIR/../dlx"
BIN_DIR="$SCRIPT_DIR/bin"
CXX_BIN=${CXX:-g++}

mkdir -p "$BIN_DIR"

echo "Compilando DLX com -O3 -DNDEBUG -std=c++17..."
"$CXX_BIN" -O3 -DNDEBUG -std=c++17 \
    "$DLX_DIR/dlx.cpp" \
    "$DLX_DIR/chvatal.cpp" \
    "$DLX_DIR/common-functions.cpp" \
    "$DLX_DIR/instance.cpp" \
    -o "$BIN_DIR/dlx"

echo "DLX compilado em: $BIN_DIR/dlx"
