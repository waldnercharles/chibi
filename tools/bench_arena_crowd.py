#!/usr/bin/env python3
"""Compare saved native executables on the rendered Arena3D crowd workload."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess
import os


CROWD = re.compile(
    r"^Crowd 40\.\.40 enemies: (\d+) samples, simulation ([0-9.]+) ms, "
    r"behaviors ([0-9.]+) ms, physics ([0-9.]+) ms$", re.M)
PROFILE = re.compile(
    r"^Crowd behavior (.+) / (.+): ([0-9.]+) exec ms, ([0-9.]+) setup ms "
    r"per frame, ([0-9.]+) starts, ([0-9.]+) resumes$", re.M)
WALL = re.compile(
    r"arena play review PASS: 1920x1080, 1800 fixed 1/60 frames, 180 warm-up "
    r"frames, (\d+) active timing samples; host wall frame p50 ([0-9.]+) ms, "
    r"p95 ([0-9.]+) ms, p99 ([0-9.]+) ms .*?peaks (\d+) enemies, "
    r"(\d+) projectiles, (\d+) particles, (\d+) beam shards, (\d+) draws; "
    r"(\d+) kills, player travel ([0-9.]+), final encounter (\d+)")
SCOPES = re.compile(r"^Editor CPU scopes \(mean ms\): (.+)$", re.M)


def fingerprint(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse(log):
    crowd, wall, scopes = CROWD.findall(log), WALL.findall(log), SCOPES.findall(log)
    if len(crowd) != 1 or len(wall) != 1 or len(scopes) != 1:
        raise ValueError("missing or duplicate Arena measurements")
    count, simulation, behavior, physics = crowd[0]
    if int(count) < 300:
        raise ValueError("insufficient samples at the authored 40-enemy cap")
    state = wall[0]
    if state[4] != "40" or state[5] != "0" or state[9] != "0" or state[11] != "0":
        raise ValueError("crowd gameplay assertions did not hold")
    metrics = dict(zip(("simulation_ms", "behavior_ms", "physics_ms"),
                       map(float, (simulation, behavior, physics))))
    metrics.update(zip(("wall_p50_ms", "wall_p95_ms", "wall_p99_ms"),
                       map(float, state[1:4])))
    profiles = {}
    for asset, event, execution, setup, starts, resumes in PROFILE.findall(log):
        key = f"{asset}/{event}"
        if key in profiles:
            raise ValueError("duplicate behavior profile")
        profiles[key] = dict(zip(("execution_ms", "setup_ms", "starts", "resumes"),
                                 map(float, (execution, setup, starts, resumes))))
    if "EnemyBehavior/Tick" not in profiles:
        raise ValueError("missing enemy Tick profile")
    metrics["profile_unassigned_ms"] = metrics["behavior_ms"] - sum(
        p["execution_ms"] + p["setup_ms"] for p in profiles.values())
    return {"crowd_samples": int(count), "active_samples": int(state[0]),
            "metrics": metrics, "profiles": profiles,
            "editor_scopes_ms": {k: float(v) for k, v in
                                 re.findall(r"([a-z ]+) ([0-9.]+)", scopes[0])},
            "gameplay": list(state[4:]), "log": log}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, default=Path("build-performance/test_suite"))
    parser.add_argument("--trials", type=int, default=5)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.trials < 3:
        parser.error("use at least three paired trials")
    root = Path(__file__).resolve().parent.parent
    binaries = {"before": args.before.resolve(), "after": args.after.resolve()}
    hashes = {k: fingerprint(p) for k, p in binaries.items()}
    # Do not allow capture I/O or unrelated review overrides into the timings.
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("CHIBI_ARENA_", "CBLUEPRINT_BENCH_"))}
    env["MallocNanoZone"] = "0"
    content = hashlib.sha256()
    for path in sorted((root / "content").rglob("*.json")):
        content.update(str(path.relative_to(root)).encode())
        content.update(path.read_bytes())
    samples, expected = [], None
    for trial in range(args.trials):
        for version in (("before", "after") if trial % 2 == 0 else ("after", "before")):
            log = subprocess.check_output([binaries[version], "--arena-crowd-review"],
                                          cwd=root, env=env, text=True,
                                          stderr=subprocess.STDOUT)
            sample = parse(log)
            state = (sample["crowd_samples"], sample["active_samples"], sample["gameplay"])
            if expected is None:
                expected = state
            if state != expected:
                raise ValueError("different gameplay or sampling window across trials")
            samples.append({"trial": trial, "version": version, **sample})
            print(f"trial={trial + 1}/{args.trials} {version} "
                  f"behavior_ms={sample['metrics']['behavior_ms']:.3f} "
                  f"simulation_ms={sample['metrics']['simulation_ms']:.3f}", flush=True)
    if hashes != {k: fingerprint(p) for k, p in binaries.items()}:
        raise ValueError("executables changed during measurement")
    summary = {}
    for metric in samples[0]["metrics"]:
        values = {v: [s["metrics"][metric] for s in samples if s["version"] == v]
                  for v in binaries}
        summary[metric] = {v: {"median": statistics.median(x), "min": min(x), "max": max(x)}
                           for v, x in values.items()}
        summary[metric]["median_paired_delta"] = statistics.median(
            b - a for a, b in zip(values["before"], values["after"]))
    profiles = {}
    for key in set.intersection(*(set(s["profiles"]) for s in samples)):
        profiles[key] = {v: {metric: statistics.median(
            s["profiles"][key][metric] for s in samples if s["version"] == v)
            for metric in ("execution_ms", "setup_ms", "starts", "resumes")}
            for v in binaries}
    report = {"binary_sha256": hashes, "content_sha256": content.hexdigest(),
              "trials": args.trials, "profiling": True, "rendered": True,
              "workload": "Arena3D authored 40-enemy cap, moving without firing",
              "summary": summary, "profiles": profiles, "samples": samples}
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
