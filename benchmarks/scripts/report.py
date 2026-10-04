"""Aggregate hit ratios, save CSV summaries and draw figures."""

import argparse
import csv
import hashlib
import json
import os
import sys
from collections import defaultdict
from pathlib import Path
from statistics import mean, stdev
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parent.parent
NAMES = {"uniform": "Равномерная", "normal": "Нормальная", "zipf": "Ципфа", "hotspot": "Hotspot",
         "scan": "Скользящий scan", "hot_scan": "Горячий набор + проход",
         "cyclic": "Цикл", "streams": "Несколько потоков"}
LEVEL_BELADY_METRICS = ("l1_belady_hit_rate", "l2_belady_hit_rate", "l3_belady_hit_rate")
ACCESS_TIME_METRIC = "access_time_score"


def aggregate(rows, metric="score"):
    groups = defaultdict(list)
    for row in rows:
        if row[metric] != "":
            key = (row["case"], row["family"], int(row["capacity"]),
                   row["l1"], row["l2"], row["l3"])
            groups[key].append(float(row[metric]))
    by_capacity = defaultdict(list)
    for (case, family, capacity, *triple), values in groups.items():
        avg = mean(values)
        by_capacity[(family, capacity, tuple(triple))].append(avg)
    capacity_scores = {key: mean(values) for key, values in by_capacity.items()}
    families = defaultdict(list)
    for (family, capacity, triple), value in capacity_scores.items():
        families[(family, triple)].append(value)
    family_scores = {key: mean(values) for key, values in families.items()}
    totals = defaultdict(list)
    for (family, triple), value in family_scores.items():
        totals[triple].append(value)
    ranking = sorted(((triple, mean(values)) for triple, values in totals.items()),
                     key=lambda item: (-item[1], item[0]))
    return family_scores, ranking


def write_csv(path, rows):
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def label(triple):
    return " → ".join(triple)


def top_table(rows, ranking, metric, seeds):
    values_by_triple = defaultdict(list)
    for seed in seeds:
        sample = [row for row in rows if int(row["seed"]) in (0, seed)]
        for triple, value in aggregate(sample, metric)[1]:
            values_by_triple[triple].append(value)
    table = []
    for rank_number, (triple, value) in enumerate(ranking[:10], 1):
        seed_values = values_by_triple[triple]
        table.append({"rank": rank_number, "l1": triple[0], "l2": triple[1], "l3": triple[2],
                      metric: value,
                      "seed_stddev": stdev(seed_values) if len(seed_values) > 1 else 0.0,
                      "seed_min": min(seed_values), "seed_max": max(seed_values)})
    return table


def figures(output, family_scores, level_family_scores, ranking, level_rankings,
            access_time_ranking, workload_families):
    cache = ROOT.parent / "build-bench" / "matplotlib"
    cache.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("MPLCONFIGDIR", str(cache))
    deps = ROOT.parent / "build-bench" / "python-deps"
    if deps.exists():
        sys.path.insert(0, str(deps))
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np

    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10,
                         "axes.spines.top": False, "axes.spines.right": False})
    directory = output / "figures"
    directory.mkdir(exist_ok=True)

    def save(fig, name):
        fig.savefig(directory / f"{name}.png", dpi=160, bbox_inches="tight")
        plt.close(fig)

    def ranking_figure(data, xlabel, title, name, color):
        fig, ax = plt.subplots(figsize=(10, 6), layout="constrained")
        top = data[:10][::-1]
        positions = np.arange(len(top))
        bars = ax.barh(positions, [value for _, value in top], height=0.6, color=color)
        ax.bar_label(bars, fmt="%.2f", padding=3, fontsize=8)
        ax.set_yticks(positions, [label(triple) for triple, _ in top])
        ax.set(xlim=(0, 105), xlabel=xlabel, title=title)
        ax.grid(axis="x", alpha=0.2)
        save(fig, name)

    ranking_figure(ranking, "Попадания относительно Belady, %",
                   "Десять лучших троек по общему проценту хитов", "ranking_belady", "#236a9f")
    level_values = [dict(level_ranking) for level_ranking in level_rankings]
    selected = access_time_ranking[:10][::-1]
    positions = np.arange(len(selected))
    fig, ax = plt.subplots(figsize=(11, 6), layout="constrained")
    left = np.zeros(len(selected))
    colors = ("#3b82b4", "#e09f3e", "#6a994e")
    for index, (name, color) in enumerate(zip(("L1", "L2", "L3"), colors)):
        values = np.array([level_values[index][triple] for triple, _ in selected])
        bars = ax.barh(positions, values, left=left, height=0.6, label=name, color=color)
        labels = [f"{value:.1f}" if value >= 3 else "" for value in values]
        ax.bar_label(bars, labels=labels, label_type="center", fontsize=8, color="white")
        left += values
    ax.set_yticks(positions, [label(triple) for triple, _ in selected])
    totals = np.array([sum(level_values[index][triple] for index in range(3))
                       for triple, _ in selected])
    for position, (total, (_, access_score)) in enumerate(zip(totals, selected)):
        ax.text(total + 0.5, position, f"T={access_score:.1f}", va="center", fontsize=8)
    ax.set(xlim=(0, 110), xlabel="Попадания относительно Belady, %",
           title="Десять лучших троек с учётом времени доступа 1:3:10")
    ax.legend(title="Уровень", loc="center left", bbox_to_anchor=(1.01, 0.5))
    ax.grid(axis="x", alpha=0.2)
    save(fig, "ranking_access_time_levels")

    target = access_time_ranking[0][0]
    workloads = workload_families[::-1]
    positions = np.arange(len(workloads))
    fig, ax = plt.subplots(figsize=(11, 6), layout="constrained")
    left = np.zeros(len(workloads))
    for index, (name, color) in enumerate(zip(("L1", "L2", "L3"), colors)):
        values = np.array([level_family_scores[index][(family, target)]
                           for family in workloads])
        bars = ax.barh(positions, values, left=left, height=0.6, label=name, color=color)
        labels = [f"{value:.1f}" if value >= 3 else "" for value in values]
        ax.bar_label(bars, labels=labels, label_type="center", fontsize=8, color="white")
        left += values
    ax.set_yticks(positions, [NAMES[family] for family in workloads])
    ax.set(xlim=(0, 105), xlabel="Попадания относительно Belady, %",
           title=f"{label(target)}: состав попаданий по нагрузкам")
    ax.legend(title="Уровень", loc="center left", bbox_to_anchor=(1.01, 0.5))
    ax.grid(axis="x", alpha=0.2)
    save(fig, "best_access_time_workload_levels")

    families = workload_families
    selected = access_time_ranking[:10]
    values = np.array([[family_scores[(f, t)] for f in families] for t, _ in selected])
    fig, ax = plt.subplots(figsize=(12, 6), layout="constrained")
    picture = ax.imshow(values, aspect="auto", vmin=0, vmax=100, cmap="viridis")
    ax.set_yticks(range(len(selected)), [label(t) for t, _ in selected], fontsize=8)
    ax.set_xticks(range(len(families)), [NAMES[f] for f in families], rotation=35, ha="right")
    ax.set_title("Лучшие по времени доступа: результаты по нагрузкам")
    for i in range(values.shape[0]):
        for j in range(values.shape[1]):
            ax.text(j, i, f"{values[i, j]:.1f}", ha="center", va="center", fontsize=7,
                    color="white" if values[i, j] < 50 else "black")
    fig.colorbar(picture, ax=ax, shrink=0.5, label="% от Belady")
    save(fig, "workloads")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, default=ROOT / "results")
    args = parser.parse_args()
    output = args.results.resolve()
    with (output / "metadata.csv").open(encoding="utf-8", newline="") as stream:
        metadata = {row["key"]: json.loads(row["value"]) for row in csv.DictReader(stream)}
    if not metadata["complete"]:
        raise ValueError("Experiment is incomplete")
    with (output / "runs.csv").open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != 64 * metadata["cases"]:
        raise ValueError("Incomplete result table")
    by_trace = defaultdict(list)
    for row in rows:
        by_trace[row["trace_file"]].append(row)
        hits, ideal = int(row["hits"]), int(row["belady_hits"])
        level_hits = [int(row[f"l{i}_hits"]) for i in range(1, 4)]
        if not 0 <= hits <= ideal <= int(row["requests"]):
            raise ValueError("Invalid hit counts in runs.csv")
        if any(value < 0 for value in level_hits) or sum(level_hits) != hits:
            raise ValueError("Invalid per-level hit counts in runs.csv")
        if ideal == 0:
            if row["score"] != "":
                raise ValueError("Score must be empty when Belady has no hits")
            for metric in LEVEL_BELADY_METRICS:
                row[metric] = ""
            row[ACCESS_TIME_METRIC] = ""
            continue
        expected = 100 * hits / ideal
        if abs(expected - float(row["score"])) > 1e-8:
            raise ValueError("Invalid score in runs.csv")
        for index, metric in enumerate(LEVEL_BELADY_METRICS, 1):
            row[metric] = f"{100 * int(row[f'l{index}_hits']) / ideal:.10f}"
        weighted_hits = (int(row["l1_hits"]) + int(row["l2_hits"]) / 3
                         + int(row["l3_hits"]) / 10)
        row[ACCESS_TIME_METRIC] = f"{100 * weighted_hits / ideal:.10f}"
    policies = {"LFU", "ARC", "2Q", "LIRS"}
    for trace_rows in by_trace.values():
        triples = {(r["l1"], r["l2"], r["l3"]) for r in trace_rows}
        if len(trace_rows) != 64 or len(triples) != 64 or any(set(t) - policies for t in triples):
            raise ValueError("Incomplete triple set")
    families, ranking = aggregate(rows)
    workload_families = list(dict.fromkeys(
        item["family"] for item in metadata["suite"]["scenarios"]))
    actual_families = {family for family, _ in families}
    if len(ranking) != 64 or actual_families != set(workload_families):
        raise ValueError("Incomplete workload set")
    level_aggregates = [aggregate(rows, metric) for metric in LEVEL_BELADY_METRICS]
    level_family_scores = [result[0] for result in level_aggregates]
    level_rankings = [result[1] for result in level_aggregates]
    _, access_time_ranking = aggregate(rows, ACCESS_TIME_METRIC)
    seeds = metadata["suite"]["seeds"]
    top_belady = top_table(rows, ranking, "score", seeds)
    top_access_time = top_table(rows, access_time_ranking, ACCESS_TIME_METRIC, seeds)
    manifest_fields = ["case", "family", "parameters", "seed", "requests", "capacity", "trace_file", "trace_sha256"]
    write_csv(output / "traces.csv", [{key: group[0][key] for key in manifest_fields} for group in by_trace.values()])
    write_csv(output / "top10_belady.csv", top_belady)
    write_csv(output / "top10_access_time.csv", top_access_time)
    figures(output, families, level_family_scores, ranking, level_rankings,
            access_time_ranking, workload_families)
    metadata["analysis_source_sha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    metadata["analysis_python"] = sys.version
    metadata["matplotlib"] = sys.modules["matplotlib"].__version__
    metadata["numpy"] = sys.modules["numpy"].__version__
    metadata["analysis_finished_utc"] = datetime.now(timezone.utc).isoformat()
    write_csv(output / "metadata.csv", [dict(key=key, value=json.dumps(value, ensure_ascii=False))
                                        for key, value in metadata.items()])
    chosen = ranking[0]
    chosen_access_time = access_time_ranking[0]
    print(f"Belady leader: {' -> '.join(chosen[0])}; score={chosen[1]:.4f}%")
    print(f"Access-time leader: {' -> '.join(chosen_access_time[0])}; "
          f"score={chosen_access_time[1]:.4f}%")


if __name__ == "__main__":
    main()
