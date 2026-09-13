#!/usr/bin/env python3
"""Pair saved executables on Arena firing/restart without frame presentation."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--trials", type=int, default=5)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.trials < 3:
        parser.error("use at least three paired trials")
    root = Path(__file__).resolve().parent.parent
    binaries = {"before": args.before.resolve(), "after": args.after.resolve()}
    hashes = {version: digest(path) for version, path in binaries.items()}
    content = hashlib.sha256()
    for path in sorted((root / "content").rglob("*.json")):
        content.update(str(path.relative_to(root)).encode())
        content.update(path.read_bytes())
    env = {key: value for key, value in os.environ.items()
           if not key.startswith("CBLUEPRINT_BENCH_")}
    env.update(CBLUEPRINT_BENCH_NO_PROFILE="1", MallocNanoZone="0")
    pattern = re.compile(
        r"^(SLJIT|BYTECODE) Arena behavior_p50_ms=([0-9.]+) "
        r"behavior_p95_ms=([0-9.]+) simulation_p50_ms=([0-9.]+) "
        r"simulation_p95_ms=([0-9.]+)$", re.M)
    keys = ("behavior_p50_ms", "behavior_p95_ms", "simulation_p50_ms", "simulation_p95_ms")
    samples = []
    for trial in range(args.trials):
        for version in (("before", "after") if trial % 2 == 0 else ("after", "before")):
            log = subprocess.check_output([binaries[version], "--compiled-arena"],
                                          cwd=root, env=env, text=True, stderr=subprocess.STDOUT)
            rows = pattern.findall(log)
            if len(rows) != 2 or {row[0] for row in rows} != {"SLJIT", "BYTECODE"}:
                raise ValueError("missing or duplicate backend measurements")
            if log.count("zero oracle calls") != 2:
                raise ValueError("missing compiled execution assertions")
            metrics = {row[0]: dict(zip(keys, map(float, row[1:]))) for row in rows}
            if any(not math.isfinite(value) or value < 0
                   for values in metrics.values() for value in values.values()):
                raise ValueError("invalid measurement")
            samples.append(dict(trial=trial, version=version, log=log, metrics=metrics))
            print(trial, version, metrics, flush=True)
    summary = {backend: {version: {key: statistics.median(
        sample["metrics"][backend][key] for sample in samples if sample["version"] == version)
        for key in keys} for version in binaries} for backend in ("SLJIT", "BYTECODE")}
    if hashes != {version: digest(path) for version, path in binaries.items()}:
        raise ValueError("an executable changed during measurement")
    args.output.write_text(json.dumps(dict(
        binary_sha256=hashes, content_sha256=content.hexdigest(), trials=args.trials,
        profiling=False, scenario="--compiled-arena: 180 fixed steps, 30 warmup, firing and restart; hidden native Scene host without presentation",
        summary=summary, samples=samples), indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
