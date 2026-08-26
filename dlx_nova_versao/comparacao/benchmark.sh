#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
INSTANCE_DIR=${INSTANCE_DIR:-"$SCRIPT_DIR/../instancias"}
TIME_LIMIT=${1:-600}
REPETITIONS=${2:-3}
SOLVERS=${SOLVERS:-"dlx cplex"}
DLX_VARIANT=${DLX_VARIANT:-v1}

if [[ $# -ge 1 ]]; then shift; fi
if [[ $# -ge 1 ]]; then shift; fi

if [[ ! $TIME_LIMIT =~ ^[0-9]+([.][0-9]+)?$ || $TIME_LIMIT == 0 ]]; then
    echo "ERRO: TIME_LIMIT deve ser positivo." >&2
    exit 2
fi
if [[ ! $REPETITIONS =~ ^[1-9][0-9]*$ ]]; then
    echo "ERRO: REPETITIONS deve ser inteiro positivo." >&2
    exit 2
fi

case $DLX_VARIANT in
    v1) DLX_BINARY=dlx ;;
    v2) DLX_BINARY=dlx_v2 ;;
    *) echo "ERRO: DLX_VARIANT aceita somente 'v1' ou 'v2'." >&2; exit 2 ;;
esac

declare -a ENABLED_SOLVERS=()
for solver in $SOLVERS; do
    case $solver in
        dlx|cplex) ENABLED_SOLVERS+=("$solver") ;;
        *) echo "ERRO: SOLVERS aceita somente 'dlx' e/ou 'cplex'." >&2; exit 2 ;;
    esac
done
if [[ ${#ENABLED_SOLVERS[@]} -eq 0 ]]; then
    echo "ERRO: nenhum solver selecionado." >&2
    exit 2
fi

shopt -s nullglob
declare -a INSTANCE_FILES=()
if [[ $# -eq 0 ]]; then
    INSTANCE_FILES=("$INSTANCE_DIR"/*.txt)
else
    for specification in "$@"; do
        if [[ -f $specification ]]; then
            INSTANCE_FILES+=("$specification")
            continue
        fi

        pattern=$specification
        if [[ $pattern != *.txt ]]; then
            pattern="${pattern}.txt"
        fi
        matches=("$INSTANCE_DIR"/$pattern)
        if [[ ${#matches[@]} -eq 0 ]]; then
            echo "AVISO: nenhuma instancia corresponde a '$specification'." >&2
        else
            INSTANCE_FILES+=("${matches[@]}")
        fi
    done
fi
shopt -u nullglob

if [[ ${#INSTANCE_FILES[@]} -eq 0 ]]; then
    echo "ERRO: nenhuma instancia encontrada em $INSTANCE_DIR" >&2
    exit 2
fi
mapfile -t INSTANCE_FILES < <(printf '%s\n' "${INSTANCE_FILES[@]}" | awk '!seen[$0]++' | sort -V)

if [[ ${SKIP_BUILD:-0} != 1 ]]; then
    for solver in "${ENABLED_SOLVERS[@]}"; do
        build_target=$solver
        if [[ $solver == dlx ]]; then
            build_target=$DLX_BINARY
        fi
        "$SCRIPT_DIR/build_${build_target}.sh"
    done
else
    for solver in "${ENABLED_SOLVERS[@]}"; do
        binary=$solver
        if [[ $solver == dlx ]]; then
            binary=$DLX_BINARY
        fi
        if [[ ! -x "$SCRIPT_DIR/bin/$binary" ]]; then
            echo "ERRO: binario ausente com SKIP_BUILD=1: bin/$binary" >&2
            exit 2
        fi
    done
fi

RUN_STAMP=$(date +%Y%m%d_%H%M%S)_$$
RESULT_DIR=${RESULT_DIR:-"$SCRIPT_DIR/resultados/$RUN_STAMP"}
DETAIL_DIR="$RESULT_DIR/logs"
RUNS_CSV="$RESULT_DIR/runs.csv"
SUMMARY_CSV="$RESULT_DIR/resumo.csv"
SUMMARY_MD="$RESULT_DIR/resumo.md"
if [[ -e $RUNS_CSV ]]; then
    echo "ERRO: $RUNS_CSV ja existe; escolha outro RESULT_DIR." >&2
    exit 2
fi
mkdir -p "$DETAIL_DIR"
source "$SCRIPT_DIR/common.sh"
initialize_runs_csv "$RUNS_CSV"

{
    echo "data_inicio=$(date --iso-8601=seconds)"
    echo "hostname=$(hostname)"
    echo "kernel=$(uname -srmo)"
    echo "compilador=$(${CXX:-g++} --version | head -n 1)"
    echo "flags_comuns=-O3 -DNDEBUG -std=c++17"
    echo "cplex_threads=1"
    echo "time_limit_core_s=$TIME_LIMIT"
    echo "hard_grace_s=${HARD_GRACE_SECONDS:-15}"
    echo "repeticoes=$REPETITIONS"
    echo "solvers=$SOLVERS"
    echo "dlx_variant=$DLX_VARIANT"
    echo "dlx_binary=$DLX_BINARY"
    if [[ -x "$SCRIPT_DIR/bin/$DLX_BINARY" ]]; then
        echo "dlx_binary_sha256=$(sha256sum "$SCRIPT_DIR/bin/$DLX_BINARY" | awk '{print $1}')"
    fi
    echo "cpu_core=${CPU_CORE:-nao_fixado}"
    echo "metrica_principal=tempo total interno apos leitura"
    echo "metrica_processo=relogio externo do inicio ao fim do processo, incluindo leitura"
    echo "cpu_e_rss_processo=/usr/bin/time"
    if [[ -f "$SCRIPT_DIR/bin/cplex_home.txt" ]]; then
        echo "cplex_home=$(<"$SCRIPT_DIR/bin/cplex_home.txt")"
    fi
    printf 'instancias='
    printf '%s ' "${INSTANCE_FILES[@]}"
    printf '\n'
} > "$RESULT_DIR/metadata.txt"

echo
echo "Benchmark justo DLX x CPLEX"
echo "Variante DLX: ${DLX_VARIANT^^} (bin/$DLX_BINARY)"
echo "Instancias : ${#INSTANCE_FILES[@]}"
echo "Repeticoes : $REPETITIONS"
echo "Limite core: ${TIME_LIMIT}s por solver"
echo "Resultados : $RESULT_DIR"
echo

instance_index=0
for instance in "${INSTANCE_FILES[@]}"; do
    for ((repetition = 1; repetition <= REPETITIONS; repetition++)); do
        if [[ ${#ENABLED_SOLVERS[@]} -eq 2 ]]; then
            if (( (instance_index + repetition) % 2 == 0 )); then
                ORDER=(dlx cplex)
            else
                ORDER=(cplex dlx)
            fi
        else
            ORDER=("${ENABLED_SOLVERS[0]}")
        fi

        position=0
        for solver in "${ORDER[@]}"; do
            position=$((position + 1))
            BIN_DIR="$SCRIPT_DIR/bin" \
            DETAIL_DIR="$DETAIL_DIR" \
            RUNS_CSV="$RUNS_CSV" \
            DLX_BINARY="$DLX_BINARY" \
            HARD_GRACE_SECONDS=${HARD_GRACE_SECONDS:-15} \
            CPU_CORE=${CPU_CORE:-} \
                "$SCRIPT_DIR/run_one.sh" "$solver" "$instance" "$TIME_LIMIT" "$repetition" "$position"
        done
    done
    instance_index=$((instance_index + 1))
done

python3 "$SCRIPT_DIR/summarize.py" "$RUNS_CSV" "$SUMMARY_CSV" "$SUMMARY_MD"

echo
echo "Concluido. Arquivos principais:"
echo "  execucoes: $RUNS_CSV"
echo "  resumo   : $SUMMARY_CSV"
echo "  leitura  : $SUMMARY_MD"
echo
sed -n '1,200p' "$SUMMARY_MD"
