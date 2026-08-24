#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
CPLEX_SOURCE="$SCRIPT_DIR/../cplex/cplex.cpp"
BIN_DIR="$SCRIPT_DIR/bin"
CXX_BIN=${CXX:-g++}

if [[ $# -ge 1 ]]; then
    CPLEX_ROOT=$1
elif [[ -n ${CPLEX_HOME:-} ]]; then
    CPLEX_ROOT=$CPLEX_HOME
else
    CPLEX_ROOT=""
    for candidate in \
        "${HOME}/CPLEX_Studio129" \
        "${HOME}/CPLEX_Studio2211" \
        /opt/ibm/ILOG/CPLEX_Studio2211 \
        /opt/ibm/ILOG/CPLEX_Studio129
    do
        if [[ -f "$candidate/cplex/include/ilcplex/ilocplex.h" ]]; then
            CPLEX_ROOT=$candidate
            break
        fi
    done
fi

if [[ -z $CPLEX_ROOT || ! -f "$CPLEX_ROOT/cplex/include/ilcplex/ilocplex.h" ]]; then
    echo "ERRO: CPLEX Studio nao encontrado." >&2
    echo "Use: CPLEX_HOME=/caminho/CPLEX_Studio ./build_cplex.sh" >&2
    exit 1
fi

CPLEX_LIB="$CPLEX_ROOT/cplex/lib/x86-64_linux/static_pic"
CONCERT_LIB="$CPLEX_ROOT/concert/lib/x86-64_linux/static_pic"
if [[ ! -d $CPLEX_LIB || ! -d $CONCERT_LIB ]]; then
    echo "ERRO: bibliotecas static_pic nao encontradas em $CPLEX_ROOT" >&2
    exit 1
fi

mkdir -p "$BIN_DIR"

echo "Compilando CPLEX com -O3 -DNDEBUG -std=c++17 em: $CPLEX_ROOT"
"$CXX_BIN" -O3 -DNDEBUG -std=c++17 -DIL_STD -Wno-ignored-attributes \
    -I"$CPLEX_ROOT/cplex/include" \
    -I"$CPLEX_ROOT/concert/include" \
    "$CPLEX_SOURCE" \
    -L"$CPLEX_LIB" \
    -L"$CONCERT_LIB" \
    -lconcert -lilocplex -lcplex -lm -lpthread -ldl \
    -o "$BIN_DIR/cplex"

printf '%s\n' "$CPLEX_ROOT" > "$BIN_DIR/cplex_home.txt"
echo "CPLEX compilado em: $BIN_DIR/cplex"
