#!/bin/bash
# ============================================================
#  Roda o DLX em todas as instâncias da pasta instancias/
#
#  Uso:
#    ./run_dlx_all.sh [time_limit_segundos] [workers]
#
#  Exemplos:
#    ./run_dlx_all.sh                # padrão: 60s, 1 worker (single-thread)
#    ./run_dlx_all.sh 60 8           # 60s por instância, 8 workers paralelos
#    ./run_dlx_all.sh 60 4           # 60s, 4 workers (recomendado)
#
#  Pré-requisito: compilar primeiro com:
#    g++ -O2 -std=c++17 -pthread dlx_scp.cpp -o dlx_scp
# ============================================================

TL=${1:-60}
WORKERS=${2:-1}

if [ ! -f "./dlx_scp" ]; then
    echo "ERRO: binario dlx_scp nao encontrado."
    echo "Compile primeiro com:"
    echo "  g++ -O2 -std=c++17 dlx_scp.cpp -o dlx_scp"
    exit 1
fi

mkdir -p RESULTADOS

echo "============================================"
echo " DLX — rodando todas as instancias"
echo " Time-limit por instancia: ${TL}s"
echo " Workers paralelos: $WORKERS"
echo "============================================"
echo ""

TOTAL=0
OK=0
OPTIMAL=0

for INST in instancias/*.txt; do
    NAME=$(basename "$INST" .txt)
    TOTAL=$((TOTAL + 1))

    printf "%-10s ... " "$NAME"

    OUT=$(timeout $((TL + 10))s ./dlx_scp "$INST" $TL --workers $WORKERS 2>&1)
    EXIT=$?

    if [ $EXIT -eq 124 ]; then
        echo "TIMEOUT (>${TL}s)"
        continue
    fi

    COST=$(echo "$OUT" | grep -oP 'Custo minimo\s+:\s+\K[0-9]+' | head -1)
    SETS=$(echo "$OUT" | grep -oP 'Num\. conjuntos:\s+\K[0-9]+' | head -1)
    TIME=$(echo "$OUT" | grep -oP 'Tempo de parede.*:\s+\K[0-9\.]+' | head -1)

    if echo "$OUT" | grep -q 'OTIMA provada'; then
        STATUS="Optimal"
        OPTIMAL=$((OPTIMAL + 1))
    elif [ -n "$COST" ]; then
        STATUS="Feasible"
    else
        STATUS="ERRO"
    fi

    if [ -n "$COST" ]; then
        OK=$((OK + 1))
        printf "custo=%-6s  conjuntos=%-4s  tempo=%-8ss  %s\n" \
               "$COST" "$SETS" "$TIME" "$STATUS"
        # Salva saída completa
        echo "$OUT" > "RESULTADOS/${NAME}_dlx.log"
    else
        echo "ERRO (sem resultado)"
    fi
done

echo ""
echo "============================================"
echo " Concluido: $OK/$TOTAL instancias resolvidas"
echo " Optimal provados: $OPTIMAL/$TOTAL"
echo " Logs salvos em RESULTADOS/*_dlx.log"
echo "============================================"
