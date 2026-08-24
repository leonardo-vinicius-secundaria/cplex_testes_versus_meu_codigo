#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

"$SCRIPT_DIR/build_dlx.sh"
if [[ $# -ge 1 ]]; then
    "$SCRIPT_DIR/build_cplex.sh" "$1"
else
    "$SCRIPT_DIR/build_cplex.sh"
fi
