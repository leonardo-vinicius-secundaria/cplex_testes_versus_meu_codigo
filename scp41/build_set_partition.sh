#!/bin/bash
# Compila os solvers de Set Partition: CPLEX e DLX.

set -e

# Ajuste se seu CPLEX estiver em outro diretorio.
CPLEX="/home/leo/CPLEX_Studio2211"

if [ ! -d "$CPLEX" ]; then
    echo "ERRO: CPLEX nao encontrado em $CPLEX"
    exit 1
fi

g++ -O2 -std=c++17 -DIL_STD \
    -I"$CPLEX/cplex/include" \
    -I"$CPLEX/concert/include" \
    cplex_set_partition.cpp \
    -L"$CPLEX/cplex/lib/x86-64_linux/static_pic" \
    -L"$CPLEX/concert/lib/x86-64_linux/static_pic" \
    -lconcert -lilocplex -lcplex -lm -lpthread -ldl \
    -o cplex_set_partition

g++ -O2 -std=c++17 dlx_set_partition.cpp -o dlx_set_partition

echo "OK: cplex_set_partition e dlx_set_partition compilados"
