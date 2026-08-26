#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

case ${DLX_VARIANT:-v1} in
    v1) "$SCRIPT_DIR/build_dlx.sh" ;;
    v2) "$SCRIPT_DIR/build_dlx_v2.sh" ;;
    *) echo "ERRO: DLX_VARIANT aceita somente 'v1' ou 'v2'." >&2; exit 2 ;;
esac
if [[ $# -ge 1 ]]; then
    "$SCRIPT_DIR/build_cplex.sh" "$1"
else
    "$SCRIPT_DIR/build_cplex.sh"
fi
