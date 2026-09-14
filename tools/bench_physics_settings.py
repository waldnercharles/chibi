#!/usr/bin/env python3
"""Measure authored Box2D settings in the rendered Arena combat workload."""
import argparse
import json
import os
from pathlib import Path
import re
import statistics
import subprocess

from bench_arena_scale import digest
from bench_arena_scene_scale import PHASE, STATE, COMBAT, PHYSICS


CASES = {
    "default": {"workers": 4},  # Fixed pre-tuning baseline, independent of Auto.
    "auto": {},
    "sensor_sweeps_off": {"workers": 4},
    "substeps_2": {"substeps": 2},
    "substeps_1": {"substeps": 1},
    "workers_8": {"workers": 8},
    "workers_10": {"workers": 10},
    "workers_14": {"workers": 14},
    "sleep_off": {"sleep": False},
    "solid_ccd_off": {"continuous": False},
    "contact_15hz": {"contact_hertz": 15},
    "damping_1": {"contact_damping": 1},
    "force_drive": {"force_drive": True},
    "workers_8_substeps_2": {"workers": 8, "substeps": 2},
    "workers_8_substeps_1": {"workers": 8, "substeps": 1},
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build-performance/test_suite"))
    parser.add_argument("--cases", nargs="+", choices=CASES, default=list(CASES))
    parser.add_argument("--count", type=int, default=20000)
    parser.add_argument("--frames", type=int, default=240)
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not 100 <= args.count <= 20000 or not 240 <= args.frames <= 1800 or args.trials < 3:
        parser.error("use 100..20000 bodies, 240..1800 frames and at least 3 trials")
    binary = args.binary.resolve()
    root = Path(__file__).resolve().parent.parent
    cache = (binary.parent / "CMakeCache.txt").read_text()
    if "CHIBI_ENABLE_SANITIZERS:BOOL=OFF" not in cache or "CMAKE_BUILD_TYPE:STRING=Release" not in cache:
        parser.error("use a Release build without sanitizers")
    sha = digest(binary)
    env = {k: v for k, v in os.environ.items() if not k.startswith(("CBLUEPRINT_BENCH_", "CHIBI_ARENA_"))}
    env.update(MallocNanoZone="0", CBLUEPRINT_BENCH_COMBAT="1", CBLUEPRINT_BENCH_PHYSICS_PROFILE="1")
    samples, states = [], {}
    for trial in range(args.trials):
        order = args.cases if trial % 2 == 0 else list(reversed(args.cases))
        for name in order:
            settings = {} if name == "auto" else {"workers": 4, **CASES[name]}
            env["CBLUEPRINT_BENCH_PHYSICS_SETTINGS"] = json.dumps(settings)
            env.pop("CBLUEPRINT_BENCH_NO_SENSOR_SWEEPS", None)
            if name == "sensor_sweeps_off":
                env["CBLUEPRINT_BENCH_NO_SENSOR_SWEEPS"] = "1"
            run = subprocess.run([str(binary), "--bench-arena-scene-scale", str(args.count), str(args.frames), "0"],
                                 cwd=root, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            log = run.stdout
            if name == "sensor_sweeps_off" and not re.search(r"^scene_scale_sensor_sweeps disabled_prototypes=[1-9]\d*$", log, re.M):
                raise ValueError("sensor sweep override was not applied")
            if run.returncode:
                raise RuntimeError(f"{name} failed ({run.returncode}):\n{log}")
            state, combat, phases, physics = STATE.findall(log), COMBAT.findall(log), PHASE.findall(log), PHYSICS.findall(log)
            if len(state) != 1 or len(combat) != 1 or len(phases) != 6 or len(physics) != 1:
                raise ValueError(f"missing acceptance output: {name}\n{log}")
            signature = (state[0][4], combat[0])
            if states.setdefault(name, signature) != signature:
                raise ValueError(f"non-deterministic gameplay within {name}")
            if not re.search(r"^scene_scale_restart .*oracle_calls=0$", log, re.M):
                raise ValueError("missing restart / compiled execution acceptance")
            workers = re.search(r"^scene_scale_executor workers=(\d+) external=0$", log, re.M)
            if not workers:
                raise ValueError("missing native worker budget")
            metrics = {f"{n}_{p}": float(v) for n, median, tail in phases for p, v in (("p50_ms", median), ("p95_ms", tail))}
            detail = {k: float(v) for k, v in re.findall(r"(\w+)=([0-9.]+)", physics[0])}
            samples.append(dict(case=name, trial=trial, settings=settings, workers=int(workers[1]),
                                metrics=metrics, physics=detail, checksum=state[0][4], combat=combat[0], log=log))
            print(f"trial={trial + 1} case={name} workers={workers[1]} physics={metrics['physics_p50_ms']:.3f} frame={metrics['wall_p50_ms']:.3f}", flush=True)
    if sha != digest(binary):
        raise ValueError("executable changed during measurements")
    summary = {name: {kind: {key: statistics.median(s[kind][key] for s in samples if s['case'] == name)
                            for key in samples[0][kind]} for kind in ('metrics', 'physics')} for name in args.cases}
    for name in args.cases:
        summary[name]['matches_default'] = states[name] == states.get('default')
    args.output.write_text(json.dumps(dict(binary_sha256=sha, count=args.count, frames=args.frames,
        warmups=30, trials=args.trials, physics_profiling=True, samples=samples, summary=summary), indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
