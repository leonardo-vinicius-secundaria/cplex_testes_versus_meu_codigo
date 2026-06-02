#!/bin/bash
# Compila cplex_scp.cpp no WSL Ubuntu
set -e

#CPLEX="/home/micaelsv/CPLEX_Studio129"
#CPLEX="/home/leo/CPLEX_Studio2211" #PC DO RIAN CPLEX
CPLEX="/home/leo/CPLEX_Studio2211"

if [ ! -d "$CPLEX" ]; then
    echo "ERRO: CPLEX nao encontrado em $CPLEX"
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
