#!/bin/bash
# ============================================================
#  Roda o CPLEX em todas as instâncias da pasta instancias/
#
#  Uso:
#    ./run_cplex_all.sh [time_limit_segundos]
#
#  Exemplos:
#    ./run_cplex_all.sh          # time-limit padrão: 3600s
#    ./run_cplex_all.sh 60       # time-limit: 60s por instância
#
#  Pré-requisito: compilar primeiro com ./build_cplex.sh
# ============================================================

TL=${1:-3600}

if [ ! -f "./cplex_scp" ]; then
    echo "ERRO: binario cplex_scp nao encontrado."
    echo "Compile primeiro com: ./build_cplex.sh"
    exit 1
fi

mkdir -p RESULTADOS

echo "============================================"
echo " CPLEX — rodando todas as instancias"
echo " Time-limit por instancia: ${TL}s"
echo "============================================"
echo ""

TOTAL=0
OK=0

for INST in instancias/*.txt; do
    NAME=$(basename "$INST" .txt)
    TOTAL=$((TOTAL + 1))

    printf "%-10s ... " "$NAME"

    OUT=$(timeout $((TL + 5))s ./cplex_scp "$INST" $TL 2>&1)
    EXIT=$?

    if [ $EXIT -eq 124 ]; then
        echo "TIMEOUT (>${TL}s)"
        continue
    fi

    COST=$(echo "$OUT" | grep -oP 'Valor obj\.\s+:\s+\K[0-9]+' | head -1)
    SETS=$(echo "$OUT" | grep -oP 'Conjuntos sel\.:\s+\K[0-9]+' | head -1)
    TIME=$(echo "$OUT" | grep -oP 'Tempo \(s\)\s+:\s+\K[0-9\.]+' | head -1)
    STATUS=$(echo "$OUT" | grep -oP 'Status CPLEX\s+:\s+\K\w+' | head -1)

    if [ -n "$COST" ]; then
        OK=$((OK + 1))
        printf "custo=%-6s  conjuntos=%-4s  tempo=%-8ss  %s\n" \
               "$COST" "$SETS" "$TIME" "$STATUS"
        # Salva saída completa
        echo "$OUT" > "RESULTADOS/${NAME}_cplex.log"
    else
        echo "ERRO (sem resultado)"
    fi
done

echo ""
echo "============================================"
echo " Concluido: $OK/$TOTAL instancias resolvidas"
echo " Logs salvos em RESULTADOS/*_cplex.log"
echo "============================================"
