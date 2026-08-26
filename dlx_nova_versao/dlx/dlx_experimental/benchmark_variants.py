#!/usr/bin/env python3

import argparse
import csv
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
from collections import defaultdict
from datetime import datetime
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_INSTANCE_DIR = (SCRIPT_DIR / "../../instancias_300_3000").resolve()
RESULTS_DIR = SCRIPT_DIR / "resultados"

VARIANTS = [
    ("fixing_local", "dlx_fixing_local"),
    ("strong_branching", "dlx_strong_branching"),
    ("bound_residual", "dlx_residual_bound"),
    ("ordenacao", "dlx_candidate_ordering"),
    ("candidato_unico", "dlx_unit_propagation"),
]
BASELINE = ("dlx_v2_sem_restart", "dlx_v2")

PATTERNS = {
    "status": re.compile(r"Status DLX:\s*(\S+)"),
    "cost": re.compile(r"Custo minimo:\s*(\d+)"),
    "nodes": re.compile(r"Nos explorados\s*:\s*(\d+)"),
    "preparation": re.compile(
        r"Tempo preparacao apos leitura \(s\)\s*:\s*([0-9.]+)"
    ),
    "search": re.compile(r"Tempo busca \(s\)\s*:\s*([0-9.]+)"),
    "total": re.compile(
        r"Tempo total apos leitura \(s\)\s*:\s*([0-9.]+)"
    ),
}


def parse_arguments():
    parser = argparse.ArgumentParser(
        description=(
            "Compara as cinco variantes experimentais do DLX sem restart."
        )
    )
    parser.add_argument(
        "time_limit",
        nargs="?",
        type=float,
        default=100.0,
        help="limite interno do search por execução, em segundos (padrão: 100)",
    )
    parser.add_argument(
        "--instances",
        nargs="+",
        default=[ "scpa3", "scpa4", "scpa5"],
        help="nomes das instâncias sem .txt",
    )
    parser.add_argument(
        "--instance-dir",
        type=Path,
        default=DEFAULT_INSTANCE_DIR,
        help=f"diretório das instâncias (padrão: {DEFAULT_INSTANCE_DIR})",
    )
    parser.add_argument(
        "--repetitions",
        type=int,
        default=1,
        help="repetições por combinação (padrão: 1)",
    )
    parser.add_argument(
        "--core",
        type=int,
        help="fixa todas as execuções neste núcleo usando taskset",
    )
    parser.add_argument(
        "--include-baseline",
        action="store_true",
        help="inclui também o DLX V2 sem restart sem feature adicional",
    )
    parser.add_argument(
        "--no-build",
        action="store_true",
        help="não executa make antes do benchmark",
    )
    parser.add_argument(
        "--external-grace",
        type=float,
        default=60.0,
        help="tolerância externa além do limite interno (padrão: 60 s)",
    )
    args = parser.parse_args()

    if args.time_limit <= 0:
        parser.error("time_limit deve ser positivo")
    if args.repetitions <= 0:
        parser.error("--repetitions deve ser positivo")
    if args.external_grace <= 0:
        parser.error("--external-grace deve ser positivo")
    if args.core is not None and args.core < 0:
        parser.error("--core não pode ser negativo")

    return args


def extract(pattern_name, output, converter=None):
    match = PATTERNS[pattern_name].search(output)
    if not match:
        return None
    value = match.group(1)
    return converter(value) if converter else value


def format_number(value, digits=6):
    if value is None:
        return "-"
    return f"{value:.{digits}f}"


def median_or_none(values):
    valid = [value for value in values if value is not None]
    return statistics.median(valid) if valid else None


def build_markdown(records, arguments, command_text):
    grouped = defaultdict(list)
    for record in records:
        grouped[(record["instance"], record["variant"])].append(record)

    lines = [
        "# Benchmark das variantes DLX sem restart",
        "",
        f"- Limite interno do search: {arguments.time_limit:g} s",
        f"- Repetições: {arguments.repetitions}",
        f"- Núcleo: {arguments.core if arguments.core is not None else 'não fixado'}",
        f"- Comando: `{command_text}`",
        "",
        "| Instância | Variante | Status | Custo | Total med. (s) | Busca med. (s) | Processo med. (s) | Nós med. |",
        "|---|---|---|---:|---:|---:|---:|---:|",
    ]

    instance_order = {name: index for index, name in enumerate(arguments.instances)}
    variant_order = {
        name: index
        for index, (name, _) in enumerate(
            ([BASELINE] if arguments.include_baseline else []) + VARIANTS
        )
    }

    for key in sorted(
        grouped,
        key=lambda item: (instance_order[item[0]], variant_order[item[1]]),
    ):
        instance, variant = key
        group = grouped[key]
        statuses = sorted({record["status"] for record in group})
        costs = [record["cost"] for record in group if record["cost"] is not None]
        cost = min(costs) if costs else None
        total = median_or_none([record["total"] for record in group])
        search = median_or_none([record["search"] for record in group])
        process = median_or_none([record["process_wall"] for record in group])
        nodes = median_or_none([record["nodes"] for record in group])

        lines.append(
            "| "
            + " | ".join(
                [
                    instance,
                    variant,
                    "/".join(statuses),
                    str(cost) if cost is not None else "-",
                    format_number(total),
                    format_number(search),
                    format_number(process),
                    f"{nodes:.0f}" if nodes is not None else "-",
                ]
            )
            + " |"
        )

    return "\n".join(lines) + "\n"


def main():
    args = parse_arguments()
    instance_dir = args.instance_dir.resolve()
    variants = ([BASELINE] if args.include_baseline else []) + VARIANTS

    if args.core is not None and shutil.which("taskset") is None:
        print("Erro: --core foi informado, mas taskset não está disponível.", file=sys.stderr)
        return 2

    instance_paths = {}
    for instance in args.instances:
        path = instance_dir / f"{instance}.txt"
        if not path.is_file():
            print(f"Erro: instância não encontrada: {path}", file=sys.stderr)
            return 2
        instance_paths[instance] = path

    if not args.no_build:
        print("Compilando base e variantes...", flush=True)
        build = subprocess.run(["make", "-j2"], cwd=SCRIPT_DIR)
        if build.returncode != 0:
            return build.returncode

    for _, executable_name in variants:
        executable = SCRIPT_DIR / "bin" / executable_name
        if not executable.is_file():
            print(f"Erro: executável não encontrado: {executable}", file=sys.stderr)
            return 2

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_dir = RESULTS_DIR / f"{timestamp}_{os.getpid()}"
    output_dir.mkdir(parents=True, exist_ok=False)

    records = []
    total_runs = len(args.instances) * len(variants) * args.repetitions
    current_run = 0
    external_timeout = args.time_limit + args.external_grace

    for repetition in range(1, args.repetitions + 1):
        for instance in args.instances:
            for variant, executable_name in variants:
                current_run += 1
                executable = SCRIPT_DIR / "bin" / executable_name
                command = [
                    str(executable),
                    str(instance_paths[instance]),
                    f"{args.time_limit:g}",
                ]
                if args.core is not None:
                    command = ["taskset", "-c", str(args.core)] + command

                print(
                    f"[{current_run:02d}/{total_runs:02d}] "
                    f"{instance} | {variant} | repetição {repetition}",
                    flush=True,
                )

                start = time.perf_counter()
                externally_timed_out = False
                try:
                    completed = subprocess.run(
                        command,
                        cwd=SCRIPT_DIR,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                        text=True,
                        timeout=external_timeout,
                    )
                    output = completed.stdout
                    exit_code = completed.returncode
                except subprocess.TimeoutExpired as error:
                    externally_timed_out = True
                    output = error.stdout or ""
                    if isinstance(output, bytes):
                        output = output.decode(errors="replace")
                    exit_code = 124
                process_wall = time.perf_counter() - start

                status = extract("status", output)
                if externally_timed_out:
                    status = "ExternalTimeout"
                elif exit_code != 0:
                    status = status or f"Exit{exit_code}"
                else:
                    status = status or "Unknown"

                record = {
                    "instance": instance,
                    "variant": variant,
                    "repetition": repetition,
                    "status": status,
                    "cost": extract("cost", output, int),
                    "nodes": extract("nodes", output, int),
                    "preparation": extract("preparation", output, float),
                    "search": extract("search", output, float),
                    "total": extract("total", output, float),
                    "process_wall": process_wall,
                    "exit_code": exit_code,
                }
                records.append(record)

                log_path = output_dir / (
                    f"{instance}__{variant}__rep{repetition}.log"
                )
                log_path.write_text(output, encoding="utf-8")

                print(
                    f"    status={status} custo={record['cost']} "
                    f"total={format_number(record['total'])}s "
                    f"processo={process_wall:.6f}s",
                    flush=True,
                )

    csv_path = output_dir / "runs.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=list(records[0].keys()))
        writer.writeheader()
        writer.writerows(records)

    command_text = " ".join(sys.argv)
    markdown = build_markdown(records, args, command_text)
    markdown_path = output_dir / "resumo.md"
    markdown_path.write_text(markdown, encoding="utf-8")

    print("\n" + markdown, end="")
    print(f"Resultados completos: {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
