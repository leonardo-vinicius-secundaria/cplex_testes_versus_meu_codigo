#!/usr/bin/env bash

# Funcoes compartilhadas pelos executores. Este arquivo deve ser carregado
# com source; os scripts chamadores habilitam set -euo pipefail.

extract_label() {
    local label=$1
    local file=$2
    awk -v key="$label" '
        {
            line = $0
            sub(/^[[:space:]]+/, "", line)
            if (index(line, key) == 1) {
                sub(/^[^:]*:[[:space:]]*/, "", line)
                sub(/[[:space:]]+$/, "", line)
                print line
                exit
            }
        }
    ' "$file"
}

extract_metric() {
    local key=$1
    local file=$2
    awk -F= -v key="$key" '$1 == key { print $2; exit }' "$file"
}

csv_row() {
    local output=$1
    shift
    local separator=""
    local value

    for value in "$@"; do
        value=${value//\"/\"\"}
        printf '%s"%s"' "$separator" "$value" >> "$output"
        separator=","
    done
    printf '\n' >> "$output"
}

initialize_runs_csv() {
    local output=$1
    printf '%s\n' \
        'instance,repetition,position,solver,exit_code,status,cost,sets,preparation_wall_s,core_wall_s,total_after_read_wall_s,preparation_cpu_s,core_cpu_s,total_after_read_cpu_s,process_wall_s,process_user_s,process_system_s,max_rss_kb,nodes,best_bound,gap_percent,solution_valid,log_file' \
        > "$output"
}
