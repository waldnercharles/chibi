#!/usr/bin/env python3
"""Compare serial circle physics without importing Neon-Swarm into the engine."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--neon", type=Path, required=True)
    parser.add_argument("--build", type=Path, default=Path("build-performance"))
    parser.add_argument("--count", type=int, default=20000)
    parser.add_argument("--frames", type=int, default=240)
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.count <= 20000 or args.frames < 30 or args.trials < 3:
        parser.error("use 1..20000 circles, at least 30 frames and three trials")
    root = Path(__file__).resolve().parent.parent
    build, neon = args.build.resolve(), args.neon.resolve()
    cache = (build / "CMakeCache.txt").read_text()
    if "CHIBI_ENABLE_SANITIZERS:BOOL=OFF" not in cache or not re.search(
        r"^CMAKE_BUILD_TYPE:STRING=(Release|RelWithDebInfo)$", cache, re.M
    ):
        parser.error("use an optimized native build without sanitizers")
    compiler = re.search(r"^CMAKE_C_COMPILER:\w+=(.+)$", cache, re.M)[1]
    box_source = re.search(r"^FETCHCONTENT_SOURCE_DIR_BOX2D:\w+=(.+)$", cache, re.M)
    box2d = Path(box_source[1]) if box_source else build / "_deps/box2d-src"
    library = build / "_deps/cute_framework-build/libcute.a"
    driver = root / "tools/experiments/physics_compare.c"
    source = neon / "src/physics.c"
    report = {
        "method": "Serial circle steps, radius 10, unit mass, 22-unit grid, "
        "55-unit/s pursuit, 30 warmups. Neon tracks contacts. Box2D uses four "
        "substeps and buffered contact events. Timed steps exclude body creation "
        "and common velocity input. This excludes rendering, ECS, engine contact "
        "collection and continuous sensors. Different collision response; no "
        "gameplay parity claim. Aborting thread stubs permit compiling Neon's "
        "unchanged source without its optional thread dependency; pool is NULL.",
        "compiler": compiler,
        "flags": ["-O3", "-std=c23"],
        "count": args.count,
        "frames": args.frames,
        "source_sha256": digest(source),
        "header_sha256": digest(neon / "include/physics.h"),
        "driver_sha256": digest(driver),
        "box2d_library_sha256": digest(library),
        "trials": [],
    }
    with tempfile.TemporaryDirectory(prefix="neon-physics-") as directory:
        work = Path(directory)
        (work / "brutal_tpool.h").write_text(
            "#pragma once\n#include <stdlib.h>\n"
            "typedef struct tpool_s tpool_t;\n"
            "static inline void tpool_enqueue(tpool_t *p, int (*f)(void *), void *arg) "
            "{ (void)p; (void)f; (void)arg; abort(); }\n"
            "static inline void tpool_wait(tpool_t *p) { (void)p; abort(); }\n"
        )
        binary = work / "compare"
        subprocess.run(
            [compiler, "-O3", "-std=c23", f"-I{work}", f"-I{neon / 'include'}",
             f"-I{box2d / 'include'}", str(driver), str(source), str(library),
             "-o", str(binary)], check=True
        )
        report["binary_sha256"] = digest(binary)
        for trial in range(args.trials):
            for mode in ([0, 1] if trial % 2 == 0 else [1, 0]):
                result = subprocess.run(
                    [str(binary), str(mode), str(args.count), str(args.frames)],
                    check=True, text=True, capture_output=True
                )
                print(result.stdout.strip(), flush=True)
                report["trials"].append(
                    {"trial": trial, "mode": mode, "output": result.stdout}
                )
    args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
