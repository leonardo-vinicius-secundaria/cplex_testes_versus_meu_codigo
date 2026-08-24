#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/common.sh"

if [[ $# -ne 5 ]]; then
    echo "Uso: $0 <dlx|cplex> <instancia.txt> <limite_s> <repeticao> <posicao>" >&2
    exit 2
fi

SOLVER=$1
INSTANCE=$2
TIME_LIMIT=$3
REPETITION=$4
POSITION=$5
BIN_DIR=${BIN_DIR:-"$SCRIPT_DIR/bin"}
DETAIL_DIR=${DETAIL_DIR:-"$SCRIPT_DIR/resultados/avulso/logs"}
RUNS_CSV=${RUNS_CSV:-"$SCRIPT_DIR/resultados/avulso/runs.csv"}
HARD_GRACE_SECONDS=${HARD_GRACE_SECONDS:-15}

if [[ $SOLVER != dlx && $SOLVER != cplex ]]; then
    echo "ERRO: solver deve ser 'dlx' ou 'cplex'." >&2
    exit 2
fi
if [[ ! -f $INSTANCE ]]; then
    echo "ERRO: instancia nao encontrada: $INSTANCE" >&2
    exit 2
fi
if [[ ! $TIME_LIMIT =~ ^[0-9]+([.][0-9]+)?$ || $TIME_LIMIT == 0 ]]; then
    echo "ERRO: limite de tempo deve ser positivo." >&2
    exit 2
fi
if [[ ! -x "$BIN_DIR/$SOLVER" ]]; then
    echo "ERRO: binario nao encontrado: $BIN_DIR/$SOLVER" >&2
    exit 2
fi
if [[ ! -x /usr/bin/time ]]; then
    echo "ERRO: /usr/bin/time nao esta disponivel." >&2
    exit 2
fi

mkdir -p "$DETAIL_DIR" "$(dirname -- "$RUNS_CSV")"
if [[ ! -f $RUNS_CSV ]]; then
    initialize_runs_csv "$RUNS_CSV"
fi

INSTANCE_NAME=$(basename -- "$INSTANCE" .txt)
RUN_ID=$(printf '%s_rep%02d_pos%s_%s' "$INSTANCE_NAME" "$REPETITION" "$POSITION" "$SOLVER")
LOG_FILE="$DETAIL_DIR/$RUN_ID.log"
TIME_FILE="$DETAIL_DIR/$RUN_ID.time"
HARD_TIMEOUT=$(awk -v limit="$TIME_LIMIT" -v grace="$HARD_GRACE_SECONDS" \
    'BEGIN { printf "%.3f", limit + grace }')

RUN_PREFIX=()
if [[ -n ${CPU_CORE:-} ]]; then
    if ! command -v taskset >/dev/null 2>&1; then
        echo "ERRO: CPU_CORE foi definido, mas taskset nao esta instalado." >&2
        exit 2
    fi
    RUN_PREFIX=(taskset -c "$CPU_CORE")
fi

export LC_ALL=C
export OMP_NUM_THREADS=1
export OPENBLAS_NUM_THREADS=1
export MKL_NUM_THREADS=1

set +e
PROCESS_START=${EPOCHREALTIME:-$(date +%s.%N)}
/usr/bin/time \
    -f 'wall_s=%e\nuser_s=%U\nsystem_s=%S\nmax_rss_kb=%M' \
    -o "$TIME_FILE" \
    -- "${RUN_PREFIX[@]}" timeout --signal=TERM --kill-after=2s "${HARD_TIMEOUT}s" \
    "$BIN_DIR/$SOLVER" "$INSTANCE" "$TIME_LIMIT" \
    > "$LOG_FILE" 2>&1
EXIT_CODE=$?
PROCESS_END=${EPOCHREALTIME:-$(date +%s.%N)}
set -e

if [[ $SOLVER == dlx ]]; then
    STATUS=$(extract_label "Status DLX" "$LOG_FILE")
    COST=$(extract_label "Custo minimo" "$LOG_FILE")
    SETS=$(extract_label "Numero de conjuntos" "$LOG_FILE")
    CORE_WALL=$(extract_label "Tempo busca (s)" "$LOG_FILE")
    CORE_CPU=$(extract_label "CPU busca (s)" "$LOG_FILE")
    NODES=$(extract_label "Nos explorados" "$LOG_FILE")
    BEST_BOUND=$(extract_label "LB dual factivel" "$LOG_FILE")
    GAP=""
    VALID=""
else
    STATUS=$(extract_label "Status CPLEX" "$LOG_FILE")
    TERMINATION=$(extract_label "Fim CPLEX" "$LOG_FILE")
    if [[ -n $TERMINATION ]]; then
        STATUS=$TERMINATION
    fi
    COST=$(extract_label "Valor obj." "$LOG_FILE")
    SETS=$(extract_label "Conjuntos sel." "$LOG_FILE")
    CORE_WALL=$(extract_label "Tempo solve (s)" "$LOG_FILE")
    CORE_CPU=$(extract_label "CPU solve (s)" "$LOG_FILE")
    NODES=$(extract_label "Nos CPLEX" "$LOG_FILE")
    BEST_BOUND=$(extract_label "Melhor bound" "$LOG_FILE")
    GAP=$(extract_label "MIP gap (%)" "$LOG_FILE")
    VALID=$(extract_label "Solucao valida" "$LOG_FILE")
fi

PREPARATION_WALL=$(extract_label "Tempo preparacao apos leitura (s)" "$LOG_FILE")
TOTAL_WALL=$(extract_label "Tempo total apos leitura (s)" "$LOG_FILE")
PREPARATION_CPU=$(extract_label "CPU preparacao apos leitura (s)" "$LOG_FILE")
TOTAL_CPU=$(extract_label "CPU total apos leitura (s)" "$LOG_FILE")
PROCESS_WALL=$(awk -v start="$PROCESS_START" -v end="$PROCESS_END" \
    'BEGIN { printf "%.9f", end - start }')
PROCESS_USER=$(extract_metric user_s "$TIME_FILE")
PROCESS_SYSTEM=$(extract_metric system_s "$TIME_FILE")
MAX_RSS=$(extract_metric max_rss_kb "$TIME_FILE")

if [[ $EXIT_CODE -eq 124 || $EXIT_CODE -eq 137 ]]; then
    STATUS="ExternalTimeout"
elif [[ $EXIT_CODE -ne 0 ]]; then
    STATUS="Error"
elif [[ -z $STATUS ]]; then
    STATUS="Unknown"
fi

csv_row "$RUNS_CSV" \
    "$INSTANCE_NAME" "$REPETITION" "$POSITION" "$SOLVER" "$EXIT_CODE" \
    "$STATUS" "$COST" "$SETS" "$PREPARATION_WALL" "$CORE_WALL" \
    "$TOTAL_WALL" "$PREPARATION_CPU" "$CORE_CPU" "$TOTAL_CPU" \
    "$PROCESS_WALL" "$PROCESS_USER" "$PROCESS_SYSTEM" "$MAX_RSS" \
    "$NODES" "$BEST_BOUND" "$GAP" "$VALID" "$LOG_FILE"

printf '%-9s rep=%-2s pos=%s | %-5s | custo=%-8s total=%-11ss processo=%-8ss status=%s\n' \
    "$INSTANCE_NAME" "$REPETITION" "$POSITION" "$SOLVER" \
    "${COST:--}" "${TOTAL_WALL:--}" "${PROCESS_WALL:--}" "$STATUS"
