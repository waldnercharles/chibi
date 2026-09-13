#!/usr/bin/env python3
"""Pair native executables on a rendered, prepopulated Arena Scene."""
import argparse
import json
import os
from pathlib import Path
import re
import statistics
import subprocess

from bench_arena_scale import digest


PHASE = re.compile(r"^scene_scale_phase name=(\w+) p50_ms=([0-9.]+) p95_ms=([0-9.]+)$", re.M)
STATE = re.compile(r"^scene_scale backend=SLJIT entities=(\d+) frames=(\d+) profile=([01]) "
                   r"draws=(\d+) checksum=(-?[0-9.]+) oracle_calls=0$", re.M)
PHYSICS = re.compile(r"^scene_scale_physics (.+)$", re.M)
COMBAT = re.compile(r"^scene_scale_combat (.+)$", re.M)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, default=Path("build-performance/test_suite"))
    parser.add_argument("--counts", type=int, nargs="+", default=[1000, 5000])
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--trials", type=int, default=5)
    parser.add_argument("--profile", action="store_true")
    parser.add_argument("--combat", action="store_true", help="fire at nearby enemies, collect XP and restart")
    parser.add_argument("--executor", type=Path, help="load an executor only for the after runs")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--serial-before", action="store_true", help="force serial physics in the before executable")
    parser.add_argument("--expect-after-workers", type=int, help="assert the after run's reported worker budget")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.trials < 3 or not 30 <= args.frames <= 1800 or any(not 1 <= n <= 20000 for n in args.counts):
        parser.error("use at least three trials, 30..1800 frames and 1..20000 enemies")
    if args.combat and (args.frames < 240 or any(n < 100 for n in args.counts)):
        parser.error("combat requires at least 240 frames and 100 initial enemies")
    root = Path(__file__).resolve().parent.parent
    binaries = {"before": args.before.resolve(), "after": args.after.resolve()}
    for path in binaries.values():
        cache = (path.parent / "CMakeCache.txt").read_text()
        if "CHIBI_ENABLE_SANITIZERS:BOOL=OFF" not in cache or not re.search(
                r"^CMAKE_BUILD_TYPE:STRING=(Release|RelWithDebInfo)$", cache, re.M):
            parser.error("use optimized builds without sanitizers")
    hashes = {v: digest(p) for v, p in binaries.items()}
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("CHIBI_ARENA_", "CBLUEPRINT_BENCH_"))}
    env["MallocNanoZone"] = "0"
    if args.combat:
        env["CBLUEPRINT_BENCH_COMBAT"] = "1"
    samples, expected, expected_combat = [], {}, {}
    for count in args.counts:
        for trial in range(args.trials):
            for version in (("before", "after") if trial % 2 == 0 else ("after", "before")):
                env.pop("CBLUEPRINT_BENCH_SERIAL_PHYSICS", None)
                if args.serial_before and version == "before":
                    env["CBLUEPRINT_BENCH_SERIAL_PHYSICS"] = "1"
                if args.executor and version == "after":
                    env["CBLUEPRINT_BENCH_EXECUTOR"] = str(args.executor.resolve())
                    env["CBLUEPRINT_BENCH_WORKERS"] = str(args.workers)
                else:
                    env.pop("CBLUEPRINT_BENCH_EXECUTOR", None)
                    env.pop("CBLUEPRINT_BENCH_WORKERS", None)
                result = subprocess.run([binaries[version], "--bench-arena-scene-scale",
                    str(count), str(args.frames), str(int(args.profile))],
                    cwd=root, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                log = result.stdout
                if result.returncode:
                    raise RuntimeError(f"{version} failed ({result.returncode}):\n{log}")
                configured = re.findall(r"^scene_scale_executor workers=(\d+) external=([01])$", log, re.M)
                if len(configured) != 1:
                    raise ValueError("missing executor status")
                workers, external = map(int, configured[0])
                if external != int(bool(args.executor) and version == "after"):
                    raise ValueError("wrong executor source")
                if args.executor and version == "after" and workers != args.workers:
                    raise ValueError("requested external worker budget was not active")
                if args.serial_before and version == "before" and workers != 1:
                    raise ValueError("serial baseline was not active")
                if args.expect_after_workers and version == "after" and workers != args.expect_after_workers:
                    raise ValueError("expected default worker budget was not active")
                states, phases, physics = STATE.findall(log), PHASE.findall(log), PHYSICS.findall(log)
                if len(states) != 1 or len(phases) != 6 or len(physics) != 1:
                    raise ValueError("missing or duplicate measurements")
                state = states[0]
                if tuple(map(int, state[:3])) != (count, args.frames, int(args.profile)) or int(state[3]) < count:
                    raise ValueError("wrong population, sampling window, profiling or rendered geometry count")
                if state[4] != expected.setdefault(count, state[4]):
                    raise ValueError("simulation snapshot differs across executables/trials")
                combat = COMBAT.findall(log)
                if len(combat) != int(args.combat):
                    raise ValueError("wrong combat scenario or missing outcome")
                if args.combat:
                    if combat[0] != expected_combat.setdefault(count, combat[0]):
                        raise ValueError("combat outcome or per-frame state trace differs")
                    if not re.search(r"^scene_scale_restart reset_ms=[0-9.]+ workers=\d+ oracle_calls=0$", log, re.M):
                        raise ValueError("missing restart acceptance")
                metrics = {f"{name}_{p}": float(value) for name, median, tail in phases
                           for p, value in (("p50_ms", median), ("p95_ms", tail))}
                detail = {k: float(v) for k, v in re.findall(r"(\w+)=([0-9.]+)", physics[0])}
                extra = {}
                for name in ("cold", "first_fire", "spikes", "restart"):
                    match = re.findall(rf"^scene_scale_{name} (.+)$", log, re.M)
                    if match:
                        extra[name] = {k: float(v) for k, v in re.findall(r"(\w+)=([0-9.]+)", match[0])}
                samples.append({"count": count, "trial": trial, "version": version,
                                "metrics": metrics, "physics": detail, "workers": workers,
                                "combat": combat[0] if combat else None, **extra, "log": log})
                print(f"count={count} trial={trial + 1} {version} "
                      f"physics={metrics['physics_p50_ms']:.3f} "
                      f"wall={metrics['wall_p50_ms']:.3f}", flush=True)
    if hashes != {v: digest(p) for v, p in binaries.items()}:
        raise ValueError("binary changed during measurement")
    summary = {}
    for count in args.counts:
        summary[str(count)] = {v: {kind: {key: statistics.median(s[kind][key] for s in samples
            if s['count'] == count and s['version'] == v) for key in samples[0][kind]}
            for kind in ('metrics', 'physics')} for v in binaries}
    args.output.write_text(json.dumps({"binary_sha256": hashes, "frames": args.frames,
        "warmup_frames": 30, "trials": args.trials, "profile": args.profile,
        "executor": str(args.executor) if args.executor else None,
        "external_workers": args.workers if args.executor else None,
        "serial_before": args.serial_before,
        "combat": args.combat,
        "workload": "Arena3D at 1280x720; 22-unit enemy grid; all authored attachments and physics; " +
                    ("nearest-enemy aim, held fire, kills, XP and restart" if args.combat else "no firing"),
        "overrides": "Prepopulate the wave and its counters; player health raised to retain the sample window",
        "summary": summary, "samples": samples}, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
