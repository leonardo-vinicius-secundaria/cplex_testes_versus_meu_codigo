#!/bin/bash
# Compila cplex_scp.cpp no WSL Ubuntu
set -e

cd "$(dirname "$0")"

# Permite sobrescrever no comando:
#   CPLEX="/opt/ibm/ILOG/CPLEX_Studio129" ./build_cplex.sh
CPLEX=${CPLEX:-}

if [ -z "$CPLEX" ]; then
    for candidate in \
        "/home/leo/CPLEX_Studio2211" \
        "/opt/ibm/ILOG/CPLEX_Studio129"
    do
        if [ -d "$candidate" ]; then
            CPLEX="$candidate"
            break
        fi
    done
fi

if [ ! -d "$CPLEX" ]; then
    echo "ERRO: CPLEX nao encontrado."
    echo "Informe o caminho assim:"
    echo '  CPLEX="/opt/ibm/ILOG/CPLEX_Studio129" ./build_cplex.sh'
    exit 1
fi

g++ -O2 -std=c++17 -DIL_STD \
    -I"$CPLEX/cplex/include" \
    -I"$CPLEX/concert/include" \
    cplex_scp.cpp \
    -L"$CPLEX/cplex/lib/x86-64_linux/static_pic" \
    -L"$CPLEX/concert/lib/x86-64_linux/static_pic" \
    -lconcert -lilocplex -lcplex -lm -lpthread -ldl \
    -o cplex_scp

echo "OK: cplex_scp compilado"
echo "CPLEX usado: $CPLEX"
