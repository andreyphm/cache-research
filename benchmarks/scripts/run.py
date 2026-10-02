"""Generate shared traces, run the C++ implementation, save verified results."""

import argparse
import csv
import hashlib
import io
import json
import platform
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from pathlib import Path
from itertools import product

from generators import generate, scenarios

ROOT = Path(__file__).resolve().parent.parent
POLICIES = ("LFU", "ARC", "2Q", "LIRS")
DETERMINISTIC_FAMILIES = {"scan", "cyclic"}
FIELDS = ["case", "family", "parameters", "seed", "requests", "trace_file", "trace_sha256",
          "capacity", "l1_capacity", "l2_capacity", "l3_capacity",
          "l1", "l2", "l3", "hits", "l1_hits", "l2_hits", "l3_hits", "belady_hits", "score"]


def score(hits, belady_hits):
    if not 0 <= hits <= belady_hits:
        raise ValueError("Invalid hit counts")
    return 100.0 * hits / belady_hits if belady_hits else None


def execute(binary, trace, count, capacity):
    completed = subprocess.run([str(binary), "--benchmark", str(trace)], check=True,
                               capture_output=True, text=True)
    rows = list(csv.DictReader(io.StringIO(completed.stdout)))
    triples = [(row["l1"], row["l2"], row["l3"]) for row in rows]
    if len(rows) != 64 or set(triples) != set(product(POLICIES, repeat=3)):
        raise ValueError("Expected all 64 unique triples")
    if len({row["belady_hits"] for row in rows}) != 1:
        raise ValueError("Inconsistent Belady results")
    for row in rows:
        if int(row["capacity"]) != capacity:
            raise ValueError("Capacity mismatch")
        if [int(row[f"l{i}_capacity"]) for i in range(1, 4)] != [capacity, 2 * capacity, 4 * capacity]:
            raise ValueError("Level capacity mismatch")
        hits, ideal = int(row["hits"]), int(row["belady_hits"])
        level_hits = [int(row[f"l{i}_hits"]) for i in range(1, 4)]
        if any(value < 0 for value in level_hits) or sum(level_hits) != hits:
            raise ValueError("Invalid per-level hit counts")
        if not 0 <= ideal <= count:
            raise ValueError("Invalid Belady hit count")
        result = score(hits, ideal)
        row["score"] = "" if result is None else f"{result:.10f}"
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--suite", type=Path, default=ROOT / "scripts" / "suite.json")
    parser.add_argument("--output", type=Path, default=ROOT / "results")
    parser.add_argument("--data", type=Path, default=ROOT / "data")
    parser.add_argument("--jobs", type=int, default=2, help="Concurrent C++ processes")
    parser.add_argument("--families", nargs="+", help="Run only the named workload families")
    parser.add_argument("--rerun", action="store_true", help="Replace the CSV results with a new experiment")
    args = parser.parse_args()
    suite = json.loads(args.suite.read_text(encoding="utf-8"))
    if args.families:
        requested = set(args.families)
        available = {item["family"] for item in suite["scenarios"]}
        unknown = requested - available
        if unknown:
            parser.error("Unknown workload families: " + ", ".join(sorted(unknown)))
        suite["scenarios"] = [item for item in suite["scenarios"]
                              if item["family"] in requested]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if (output / "runs.csv").exists() and not args.rerun:
        parser.error(f"Results already exist in {output}; use --rerun or choose a new --output directory")
    data = args.data.resolve()
    data.mkdir(parents=True, exist_ok=True)
    if args.jobs < 1 or suite["requests_per_capacity"] < 1:
        parser.error("Require positive --jobs and requests_per_capacity")
    seeds = suite["seeds"]
    if not seeds or len(seeds) != len(set(seeds)):
        parser.error("Seeds must be non-empty and unique")
    cases = []
    for capacity in suite["capacities"]:
        if capacity < 2:
            raise ValueError("Require capacity >= 2")
        for family, parameters in scenarios(suite, capacity):
            for seed in ([0] if family in DETERMINISTIC_FAMILIES else seeds):
                cases.append((capacity, family, parameters, seed))
    metadata = {"suite": suite, "python": sys.version,
                "platform": platform.platform(), "cases": len(cases), "complete": False,
                "started_utc": datetime.now(timezone.utc).isoformat(),
                "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                "source_sha256": {path.relative_to(ROOT.parent).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                                  for folder in ("headers", "source", "benchmarks/scripts")
                                  for path in (ROOT.parent / folder).glob("*")
                                  if path.suffix in (".hpp", ".cpp", ".py", ".json")}}
    def save_metadata():
        with (output / "metadata.csv").open("w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream)
            writer.writerow(["key", "value"])
            writer.writerows((key, json.dumps(value, ensure_ascii=False)) for key, value in metadata.items())
    save_metadata()

    def run_case(case):
        capacity, family, parameters, seed = case
        count = suite["requests_per_capacity"] * capacity
        parameter_text = json.dumps(parameters, sort_keys=True, separators=(",", ":"))
        case_id = f"{family}_c{capacity}_" + hashlib.sha256(parameter_text.encode()).hexdigest()[:12]
        keys = generate(family, parameters, count, seed)
        if len(keys) != count:
            raise ValueError("Generator returned incorrect trace length")
        payload = (f"{capacity} {2 * capacity} {4 * capacity} {count}\n"
                   + " ".join(map(str, keys)) + "\n").encode()
        digest = hashlib.sha256(payload).hexdigest()
        trace = data / f"{case_id}_s{seed}_{digest[:12]}.txt"
        trace.write_bytes(payload)
        rows = execute(args.binary.resolve(), trace, count, capacity)
        for row in rows:
            row.update(case=case_id, family=family, parameters=parameter_text, seed=seed,
                       requests=count, trace_file=trace.name, trace_sha256=digest)
        return rows

    partial = output / "runs.partial.csv"
    with partial.open("w", newline="", encoding="utf-8") as stream, ThreadPoolExecutor(max_workers=args.jobs) as executor:
        writer = csv.DictWriter(stream, fieldnames=FIELDS)
        writer.writeheader()
        for index, rows in enumerate(executor.map(run_case, cases), 1):
            writer.writerows(rows)
            stream.flush()
            if index % 5 == 0 or index == len(cases):
                print(f"{index}/{len(cases)} traces: {rows[0]['family']}, C={rows[0]['capacity']}", flush=True)
    partial.replace(output / "runs.csv")
    metadata["complete"] = True
    metadata["finished_utc"] = datetime.now(timezone.utc).isoformat()
    save_metadata()


if __name__ == "__main__":
    main()
