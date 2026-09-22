#!/usr/bin/env python3
"""GPU regression for power through a normal-incidence absorbing glass slab."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from benchmark_optics import read_pfm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT/"build-release/gi_lab")
    parser.add_argument("--output", type=Path, default=ROOT/"build-release/directional-check")
    parser.add_argument("--angular-transfer", type=int, default=1)
    parser.add_argument("--long-rays", type=int, default=0)
    parser.add_argument("--surface-band", type=int, default=0)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    reports = []
    for width, height, directions in ((120, 68, 32), (128, 72, 32), (120, 68, 64)):
        label = f"slab-{width}-{directions}"
        prefix = args.output/label
        command = [str(args.binary), "--method", "multigrid", "--scene", "2",
                   "--width", str(width), "--height", str(height),
                   "--directions", str(directions), "--boundary-filter", "1",
                   "--angular-transfer", str(args.angular_transfer),
                   "--long-rays", str(args.long_rays),
                   "--surface-band", str(args.surface_band),
                   "--cycles", "0", "--iterations", "1024", "--fog", "0",
                   "--warmup", "0", "--frames", "1", "--output", str(prefix)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=120)
        prefix.with_suffix(".log").write_text(result.stdout+result.stderr)
        assert not result.returncode, result.stderr
        w, h, values = read_pfm(Path(f"{prefix}-0000.pfm"))
        ratios = []
        aperture = sum(.3625 <= (y+.5)*1.125/h < .7625 for y in range(h))*1.125/h/.4
        for color, absorption in enumerate((.15, .05, .01)):
            # Fresnel R=.04, T=.96, slab thickness=.25. Sum the infinite
            # geometric series of internal round trips, including Beer loss.
            attenuation = math.exp(-absorption*.25)
            expected = aperture*.96**2*attenuation/(1-.04**2*attenuation**2)
            for fraction in (.55, .7, .9):
                x = int(w*fraction)
                actual = sum(values[(y*w+x)*3+color] for y in range(h))*1.125/h
                ratios.append(actual/expected)
        report = dict(scene=label, ratios=ratios, command=command)
        reports.append(report)
        print(label, f"transmitted power ratios {min(ratios):.6f}..{max(ratios):.6f}", flush=True)
        assert max(abs(r-1) for r in ratios) < .01, report
    (args.output/"summary.json").write_text(json.dumps(reports, indent=2)+"\n")


if __name__ == "__main__":
    main()
