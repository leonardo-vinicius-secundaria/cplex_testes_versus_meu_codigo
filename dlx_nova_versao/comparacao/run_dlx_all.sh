#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
SOLVERS=dlx exec "$SCRIPT_DIR/benchmark.sh" "$@"
