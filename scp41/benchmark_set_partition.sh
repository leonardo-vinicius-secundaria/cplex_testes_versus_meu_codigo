#!/bin/bash
# Benchmark comparativo CPLEX vs DLX para Set Partition em instancias OR-Library.
#
# Uso:
#   ./benchmark_set_partition.sh [TIME_LIMIT_SEC]
#
# Le instancias de ./instancias/ e grava:
#   RESULTADOS/benchmark_set_partition.csv
#   RESULTADOS_DETALHE/<inst>_partition_cplex.log
#   RESULTADOS_DETALHE/<inst>_partition_dlx.log

set -u

TIMEOUT_SEC=${1:-60}
INSTDIR="instancias"
DETDIR="RESULTADOS_DETALHE"
LOG="RESULTADOS/benchmark_set_partition.csv"

INSTANCES_4=(scp41 scp42 scp43 scp44 scp45 scp46 scp47 scp48 scp49 scp410)
INSTANCES_5=(scp51 scp52 scp53 scp54 scp55 scp56 scp57 scp58 scp59 scp510)
INSTANCES_6=(scp61 scp62 scp63 scp64 scp65)
INSTANCES_E=(scpe1 scpe2 scpe3 scpe4 scpe5)
INSTANCES=("${INSTANCES_4[@]}" "${INSTANCES_5[@]}" "${INSTANCES_6[@]}" "${INSTANCES_E[@]}")

mkdir -p RESULTADOS "$DETDIR" "$INSTDIR"
echo "instance,cplex_cost,cplex_sets,cplex_wall,cplex_status,cplex_valid,dlx_cost,dlx_sets,dlx_wall,dlx_status,dlx_valid" > "$LOG"

if [ ! -x ./cplex_set_partition ]; then
    echo "ERRO: ./cplex_set_partition nao encontrado ou nao executavel."
    echo "Compile antes com: ./build_set_partition.sh"
    exit 1
fi

if [ ! -x ./dlx_set_partition ]; then
    echo "ERRO: ./dlx_set_partition nao encontrado ou nao executavel."
    echo "Compile antes com: ./build_set_partition.sh"
    exit 1
fi

# Baixa instancias que nao existem.
for inst in "${INSTANCES[@]}"; do
    if [ ! -f "$INSTDIR/${inst}.txt" ]; then
        echo "Baixando ${inst}.txt ..."
        curl -sS --max-time 30 -o "$INSTDIR/${inst}.txt" \
            "https://people.brunel.ac.uk/~mastjjb/jeb/orlib/files/${inst}.txt" \
            || { echo "FALHA download de $inst"; continue; }
    fi
done

for inst in "${INSTANCES[@]}"; do
    INSTFILE="$INSTDIR/${inst}.txt"
    if [ ! -f "$INSTFILE" ]; then
        echo "$inst,,,,SKIP,,,-,-,-,SKIP," >> "$LOG"
        continue
    fi

    printf "%-8s | " "$inst"

    # CPLEX -----
    OUT=$(timeout $((TIMEOUT_SEC + 5))s ./cplex_set_partition "$INSTFILE" "$TIMEOUT_SEC" 2>&1)
    EXIT=$?
    echo "$OUT" > "$DETDIR/${inst}_partition_cplex.log"

    if [ $EXIT -eq 124 ]; then
        CC="-"; CS="-"; CT=">${TIMEOUT_SEC}"; CST="TIMEOUT"; CV="-"
    else
        CC=$(echo "$OUT" | grep -oP 'Valor obj\.\s+:\s+\K[0-9]+(\.[0-9]+)?' | head -1)
        CS=$(echo "$OUT" | grep -oP 'Conjuntos sel\.:\s+\K[0-9]+' | head -1)
        CT=$(echo "$OUT" | grep -oP 'Tempo \(s\)\s+:\s+\K[0-9\.]+' | head -1)
        CST=$(echo "$OUT" | grep -oP 'Status CPLEX\s+:\s+\K\w+' | head -1)
        CV=$(echo "$OUT" | grep -oP 'Particao valida:\s+\K(SIM|NAO)' | head -1)
        : "${CC:=-}"; : "${CS:=-}"; : "${CT:=-}"; : "${CST:=-}"; : "${CV:=-}"
    fi
    printf "CPLEX: %7s (sets=%4s, t=%7ss, %s, valid=%s) | " "$CC" "$CS" "$CT" "$CST" "$CV"

    # DLX -----
    OUT=$(timeout $((TIMEOUT_SEC + 5))s ./dlx_set_partition "$INSTFILE" "$TIMEOUT_SEC" 2>&1)
    EXIT=$?
    echo "$OUT" > "$DETDIR/${inst}_partition_dlx.log"

    if [ $EXIT -eq 124 ]; then
        DC="-"; DS="-"; DT=">${TIMEOUT_SEC}"; DST="TIMEOUT"; DV="-"
    else
        DC=$(echo "$OUT" | grep -oP 'Custo minimo\s+:\s+\K[0-9]+' | head -1)
        DS=$(echo "$OUT" | grep -oP 'Num\. conjuntos:\s+\K[0-9]+' | head -1)
        DT=$(echo "$OUT" | grep -oP 'Tempo de parede.*:\s+\K[0-9\.]+' | head -1)
        DV=$(echo "$OUT" | grep -oP 'Particao valida:\s+\K(SIM|NAO)' | head -1)

        if echo "$OUT" | grep -q 'INVIAVEL'; then
            DST="Infeasible"
        elif echo "$OUT" | grep -q 'OTIMA provada'; then
            DST="Optimal"
        elif [ -n "$DC" ]; then
            DST="Feasible"
        else
            DST="ERROR"
        fi

        : "${DC:=-}"; : "${DS:=-}"; : "${DT:=-}"; : "${DV:=-}"
    fi
    printf "DLX: %7s (sets=%4s, t=%7ss, %s, valid=%s)\n" "$DC" "$DS" "$DT" "$DST" "$DV"

    echo "$inst,$CC,$CS,$CT,$CST,$CV,$DC,$DS,$DT,$DST,$DV" >> "$LOG"
done

echo
echo "=========================================="
echo "Benchmark Set Partition salvo em: $LOG"
echo "=========================================="
if command -v column >/dev/null 2>&1; then
    column -t -s, "$LOG"
else
    cat "$LOG"
fi
