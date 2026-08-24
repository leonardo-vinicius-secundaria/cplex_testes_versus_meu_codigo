#!/usr/bin/env python3

import csv
import re
import statistics
import sys
from collections import defaultdict
from decimal import Decimal, InvalidOperation
from pathlib import Path


def numeric_values(rows, field):
    values = []
    for row in rows:
        value = row.get(field, "").strip()
        if not value:
            continue
        try:
            values.append(float(value))
        except ValueError:
            pass
    return values


def median_text(rows, field, decimals=9):
    values = numeric_values(rows, field)
    if not values:
        return ""
    return f"{statistics.median(values):.{decimals}f}"


def distinct(rows, field):
    return sorted({row.get(field, "").strip() for row in rows if row.get(field, "").strip()})


def joined(rows, field):
    return "|".join(distinct(rows, field))


def normalized_numbers(values):
    result = set()
    for value in values:
        try:
            result.add(Decimal(value).normalize())
        except InvalidOperation:
            result.add(value)
    return result


def speedup(numerator, denominator):
    if not numerator or not denominator:
        return ""
    top = float(numerator)
    bottom = float(denominator)
    if bottom <= 0.0:
        return ""
    return f"{top / bottom:.3f}"


def cost_agreement(cplex_rows, dlx_rows):
    cplex_costs = distinct(cplex_rows, "cost")
    dlx_costs = distinct(dlx_rows, "cost")
    if not cplex_costs or not dlx_costs:
        return "NOT_COMPARED"
    if normalized_numbers(cplex_costs) != normalized_numbers(dlx_costs):
        return "MISMATCH"

    cplex_status = set(distinct(cplex_rows, "status"))
    dlx_status = set(distinct(dlx_rows, "status"))
    if cplex_status == {"Optimal"} and dlx_status == {"Optimal"}:
        return "MATCH_OPTIMAL"
    return "MATCH_INCUMBENT"


def instance_sort_key(name):
    numbered = re.fullmatch(r"scp([456])(\d+)", name)
    if numbered:
        return (0, int(numbered.group(1)), int(numbered.group(2)))
    lettered = re.fullmatch(r"scp([a-z]+)(\d+)", name)
    if lettered:
        return (1, lettered.group(1), int(lettered.group(2)))
    return (2, name, 0)


def build_summary(rows):
    grouped = defaultdict(lambda: defaultdict(list))
    for row in rows:
        grouped[row["instance"]][row["solver"]].append(row)

    summary = []
    for instance in sorted(grouped, key=instance_sort_key):
        cplex_rows = grouped[instance].get("cplex", [])
        dlx_rows = grouped[instance].get("dlx", [])

        cplex_prep = median_text(cplex_rows, "preparation_wall_s")
        dlx_prep = median_text(dlx_rows, "preparation_wall_s")
        cplex_core = median_text(cplex_rows, "core_wall_s")
        dlx_core = median_text(dlx_rows, "core_wall_s")
        cplex_total = median_text(cplex_rows, "total_after_read_wall_s")
        dlx_total = median_text(dlx_rows, "total_after_read_wall_s")
        cplex_process = median_text(cplex_rows, "process_wall_s")
        dlx_process = median_text(dlx_rows, "process_wall_s")

        summary.append({
            "instance": instance,
            "cost_agreement": cost_agreement(cplex_rows, dlx_rows),
            "cplex_status": joined(cplex_rows, "status"),
            "dlx_status": joined(dlx_rows, "status"),
            "cplex_cost": joined(cplex_rows, "cost"),
            "dlx_cost": joined(dlx_rows, "cost"),
            "cplex_sets": joined(cplex_rows, "sets"),
            "dlx_sets": joined(dlx_rows, "sets"),
            "cplex_runs": str(len(cplex_rows)),
            "dlx_runs": str(len(dlx_rows)),
            "cplex_prep_median_s": cplex_prep,
            "dlx_prep_median_s": dlx_prep,
            "cplex_core_median_s": cplex_core,
            "dlx_core_median_s": dlx_core,
            "cplex_total_after_read_median_s": cplex_total,
            "dlx_total_after_read_median_s": dlx_total,
            "dlx_speedup_total_x": speedup(cplex_total, dlx_total),
            "cplex_process_median_s": cplex_process,
            "dlx_process_median_s": dlx_process,
            "dlx_speedup_process_x": speedup(cplex_process, dlx_process),
            "cplex_max_rss_median_kb": median_text(cplex_rows, "max_rss_kb", 1),
            "dlx_max_rss_median_kb": median_text(dlx_rows, "max_rss_kb", 1),
            "cplex_nodes_median": median_text(cplex_rows, "nodes", 1),
            "dlx_nodes_median": median_text(dlx_rows, "nodes", 1),
        })
    return summary


def write_csv(path, summary):
    if not summary:
        raise ValueError("o CSV de execucoes nao possui linhas")
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)


def write_markdown(path, summary):
    columns = [
        ("instance", "Instancia"),
        ("cost_agreement", "Custos"),
        ("cplex_cost", "CPLEX custo"),
        ("dlx_cost", "DLX custo"),
        ("cplex_total_after_read_median_s", "CPLEX total med. (s)"),
        ("dlx_total_after_read_median_s", "DLX total med. (s)"),
        ("dlx_speedup_total_x", "Speedup DLX"),
        ("cplex_process_median_s", "CPLEX processo med. (s)"),
        ("dlx_process_median_s", "DLX processo med. (s)"),
    ]
    lines = [
        "# Resumo DLX x CPLEX",
        "",
        "A metrica principal e o total interno apos a leitura. Speedup DLX = tempo CPLEX / tempo DLX; valores maiores que 1 favorecem o DLX.",
        "",
        "| " + " | ".join(title for _, title in columns) + " |",
        "| " + " | ".join("---" for _ in columns) + " |",
    ]
    for row in summary:
        lines.append("| " + " | ".join(row[key] or "-" for key, _ in columns) + " |")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main():
    if len(sys.argv) != 4:
        print("Uso: summarize.py runs.csv resumo.csv resumo.md", file=sys.stderr)
        return 2

    runs_path = Path(sys.argv[1])
    csv_path = Path(sys.argv[2])
    markdown_path = Path(sys.argv[3])
    with runs_path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))

    summary = build_summary(rows)
    write_csv(csv_path, summary)
    write_markdown(markdown_path, summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
