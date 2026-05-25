#!/bin/bash
# ============================================================
#  Compila cplex_scp.cpp no Linux / WSL Ubuntu
#
#  ANTES DE RODAR: ajuste CPLEX_HOME abaixo para o caminho
#  onde o CPLEX Studio está instalado na sua máquina.
#
#  Exemplos comuns:
#    /home/SEU_USUARIO/CPLEX_Studio129
#    /opt/ibm/ILOG/CPLEX_Studio129
#    /usr/local/CPLEX_Studio129
#
#  Para descobrir onde está instalado:
#    find / -name "ilocplex.h" 2>/dev/null
# ============================================================
set -e

# >>>  AJUSTE AQUI  <<<
CPLEX_HOME="/home/micaelsv/CPLEX_Studio129"

if [ ! -d "$CPLEX_HOME" ]; then
    echo ""
    echo "ERRO: CPLEX nao encontrado em: $CPLEX_HOME"
    echo ""
    echo "Edite a variavel CPLEX_HOME neste script com o caminho correto."
    echo "Para descobrir onde o CPLEX esta instalado, rode:"
    echo "  find / -name 'ilocplex.h' 2>/dev/null"
    echo ""
    exit 1
fi

echo "Compilando cplex_scp.cpp com CPLEX em: $CPLEX_HOME"

g++ -O2 -std=c++17 -DIL_STD \
    -I"$CPLEX_HOME/cplex/include" \
    -I"$CPLEX_HOME/concert/include" \
    cplex_scp.cpp \
    -L"$CPLEX_HOME/cplex/lib/x86-64_linux/static_pic" \
    -L"$CPLEX_HOME/concert/lib/x86-64_linux/static_pic" \
    -lconcert -lilocplex -lcplex -lm -lpthread -ldl \
    -o cplex_scp

echo ""
echo "OK: cplex_scp compilado com sucesso!"
echo "Para rodar: ./cplex_scp instancias/scp41.txt"
