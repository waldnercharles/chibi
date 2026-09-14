#!/usr/bin/env python3
"""Compare repeated mixed Tick bodies with prepared dispatch and generated C."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build-performance")
    parser.add_argument("--entities", type=int, nargs="+", default=[512, 4096])
    parser.add_argument("--frames", type=int, default=256)
    parser.add_argument("--trials", type=int, default=5)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.trials < 3 or args.frames < 16 or any(n < 2 for n in args.entities):
        parser.error("use at least three trials, 16 frames and two entities")
    root = Path(__file__).resolve().parent.parent
    build = (root / args.build_dir).resolve()
    binary = build / "test_suite"
    cache = (build / "CMakeCache.txt").read_text()
    if "CHIBI_ENABLE_SANITIZERS:BOOL=OFF" not in cache or not re.search(
        r"^CMAKE_BUILD_TYPE:STRING=(Release|RelWithDebInfo)$", cache, re.M
    ):
        parser.error("use an optimized build without sanitizers")
    compiler = re.search(r"^CMAKE_C_COMPILER:[^=]+=(.+)$", cache, re.M).group(1)
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    binary_hash = digest(binary)
    pattern = re.compile(r"^sequence mode=(\w+) parts=([23]) entities=(\d+) ns=([0-9.]+) checksum=(\d+) "
                         r"compile_us=([0-9.]+) rebuild_us=([0-9.]+) oracle_calls=0 allocations=0$", re.M)
    samples, checksums, logs = {}, {}, []
    modes = ("PREPARED", "SLJIT", "GENERATED_C", "BYTECODE")
    with tempfile.TemporaryDirectory(prefix="tick-sequences-") as temp:
        source, library = Path(temp) / "sequence.c", Path(temp) / "sequence.so"
        subprocess.run([binary, "--emit-sequence-c", source], cwd=root, check=True)
        source_hash = digest(source)
        subprocess.run([compiler, "-std=c23", "-O3", "-ffp-contract=off", "-shared", "-fPIC",
                        *(["-Wl,-undefined,dynamic_lookup"] if sys.platform == "darwin" else []),
                        "-I" + str(root / "src"), source, "-o", library], cwd=root, check=True)
        for trial in range(args.trials):
            for count in args.entities:
                output = subprocess.check_output([binary, "--bench-sequences", library, str(count),
                    str(args.frames), str(trial % 4)], cwd=root, text=True)
                print(f"trial={trial}\n{output}", end="", flush=True)
                logs.append({"trial": trial, "entities": count, "output": output})
                expected = {(mode, str(parts)) for mode in modes for parts in (2, 3)}
                for mode, parts, entities, ns, checksum, compile_us, rebuild_us in pattern.findall(output):
                    pair = mode, parts
                    if pair not in expected or int(entities) != count:
                        raise ValueError("unexpected or duplicate sample")
                    expected.remove(pair)
                    if checksums.setdefault((parts, count), checksum) != checksum:
                        raise ValueError("backend snapshot mismatch")
                    if not float(ns) > 0:
                        raise ValueError("invalid timing")
                    samples.setdefault(f"{parts}/{count}/{mode}", []).append({
                        "ns": float(ns), "compile_us": float(compile_us), "rebuild_us": float(rebuild_us)})
                if expected:
                    raise ValueError(f"missing samples: {expected}")
    if binary_hash != digest(binary):
        raise ValueError("executable changed during benchmark")
    summary, ratios = {}, {}
    for key, values in samples.items():
        assert len(values) == args.trials
        ns = [v["ns"] for v in values]
        summary[key] = {"median_ns": statistics.median(ns), "min_ns": min(ns), "max_ns": max(ns),
            "compile_us": statistics.median(v["compile_us"] for v in values),
            "rebuild_us": statistics.median(v["rebuild_us"] for v in values)}
    for count in args.entities:
        for parts in (2, 3):
            prefix = f"{parts}/{count}"
            times = {m: summary[f"{prefix}/{m}"]["median_ns"] for m in modes}
            ratios[prefix] = {"speedup": times["PREPARED"] / times["SLJIT"],
                             "native_c": times["SLJIT"] / times["GENERATED_C"]}
            print(prefix, ratios[prefix])
    args.output.write_text(json.dumps({"binary_sha256": binary_hash, "generated_c_sha256": source_hash,
        "compiler": subprocess.check_output([compiler, "--version"], text=True).strip(),
        "frames": args.frames, "entities": args.entities, "samples": samples, "summary": summary,
        "ratios": ratios, "logs": logs,
        "comparison": "same prepared rows and lowered bodies; two component graphs, optionally followed by a string-array graph; compilation and schedule rebuild measured separately; warmed backing arena, VM and runtime store allocations are zero"}, indent=2) + "\n")


if __name__ == "__main__":
    main()
