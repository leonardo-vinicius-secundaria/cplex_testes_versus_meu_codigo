#!/bin/bash
# Benchmark comparativo CPLEX vs DLX em instâncias OR-Library.
#
# Uso:
#   ./benchmark.sh [TIME_LIMIT_SEC]
#
# Lê instâncias de ./instancias/  e grava:
#   RESULTADOS/benchmark.csv         tabela final
#   RESULTADOS_DETALHE/<inst>_dlx.log
#   RESULTADOS_DETALHE/<inst>_cplex.log

set -u

TIMEOUT_SEC=${1:-600}
INSTDIR="instancias"
DETDIR="RESULTADOS_DETALHE"

INSTANCES=(scpa1)

mkdir -p RESULTADOS "$DETDIR" "$INSTDIR"
LOG="RESULTADOS/benchmark.csv"
echo "instance,cplex_cost,cplex_sets,cplex_wall,cplex_status,dlx_cost,dlx_sets,dlx_wall,dlx_status" > "$LOG"

# Baixa instâncias que não existem
for inst in "${INSTANCES[@]}"; do
    if [ ! -f "$INSTDIR/${inst}.txt" ]; then
        echo "Baixando ${inst}.txt ..."
        curl -sS --max-time 30 -o "$INSTDIR/${inst}.txt" \
            "https://people.brunel.ac.uk/~mastjjb/jeb/orlib/files/${inst}.txt" \
            || { echo "FALHA download de $inst"; continue; }
    fi
done

# Roda os benchmarks
for inst in "${INSTANCES[@]}"; do
    INSTFILE="$INSTDIR/${inst}.txt"
    if [ ! -f "$INSTFILE" ]; then
        echo "$inst,,,,SKIP,,,,SKIP" >> "$LOG"
        continue
    fi

    printf "%-8s | " "$inst"

    # CPLEX -----
    OUT=$(timeout $((TIMEOUT_SEC + 5))s ./cplex_scp "$INSTFILE" $TIMEOUT_SEC 2>&1)
    EXIT=$?
    echo "$OUT" > "$DETDIR/${inst}_cplex.log"
    if [ $EXIT -eq 124 ]; then
        CC="-"; CS="-"; CT=">${TIMEOUT_SEC}"; CST="TIMEOUT"
    else
        CC=$(echo "$OUT" | grep -oP 'Valor obj\.\s+:\s+\K[0-9]+' | head -1)
        CS=$(echo "$OUT" | grep -oP 'Conjuntos sel\.:\s+\K[0-9]+' | head -1)
        CT=$(echo "$OUT" | grep -oP 'Tempo \(s\)\s+:\s+\K[0-9\.]+' | head -1)
        CST=$(echo "$OUT" | grep -oP 'Status CPLEX\s+:\s+\K\w+' | head -1)
        : "${CC:=-}"; : "${CS:=-}"; : "${CT:=-}"; : "${CST:=-}"
    fi
    printf "CPLEX: %5s (sets=%4s, t=%7ss, %s) | " "$CC" "$CS" "$CT" "$CST"

    # DLX -----
    OUT=$(timeout $((TIMEOUT_SEC + 10))s ./dlx_scp "$INSTFILE" $TIMEOUT_SEC --workers ${DLX_WORKERS:-1} 2>&1)
    EXIT=$?
    echo "$OUT" > "$DETDIR/${inst}_dlx.log"
    if [ $EXIT -eq 124 ]; then
        DC="-"; DS="-"; DT=">${TIMEOUT_SEC}"; DST="TIMEOUT"
    else
        DC=$(echo "$OUT" | grep -oP 'Custo minimo\s+:\s+\K[0-9]+' | head -1)
        DS=$(echo "$OUT" | grep -oP 'Num\. conjuntos:\s+\K[0-9]+' | head -1)
        DT=$(echo "$OUT" | grep -oP 'Tempo de parede.*:\s+\K[0-9\.]+' | head -1)
        if echo "$OUT" | grep -q 'OTIMA provada'; then
            DST="Optimal"
        elif [ -n "$DC" ]; then
            DST="NotOptimal"
        else
            DST="ERROR"
        fi
        : "${DC:=-}"; : "${DS:=-}"; : "${DT:=-}"; : "${DST:=-}"
    fi
    printf "DLX: %5s (sets=%4s, t=%7ss, %s)\n" "$DC" "$DS" "$DT" "$DST"

    echo "$inst,$CC,$CS,$CT,$CST,$DC,$DS,$DT,$DST" >> "$LOG"
done

echo
echo "=========================================="
echo "Benchmark salvo em: $LOG"
echo "=========================================="
column -t -s, "$LOG"
