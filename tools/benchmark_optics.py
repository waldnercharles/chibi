#!/usr/bin/env python3
"""Hidden GPU optics experiments, matched CPU references, and temporal diagnostics.

Only the forward tracer is checked for reference agreement. Directional methods
are candidates whose bias and latency are measured, not correctness baselines.
"""
import argparse
from array import array
from dataclasses import replace
import hashlib
import html
import json
import math
from pathlib import Path
import platform
import statistics
import subprocess
import sys

from optics_reference import Reference, fixture, write_images

ROOT = Path(__file__).resolve().parents[1]
SCENES = {"lens": 1, "slab": 2, "prism": 3, "mirror": 4}


def read_pfm(path):
    with path.open("rb") as f:
        assert f.readline().strip() == b"PF"
        width, height = map(int, f.readline().split())
        assert f.readline().strip() == b"-1.0"
        values = array("f")
        values.frombytes(f.read())
    if sys.byteorder != "little":
        values.byteswap()
    assert len(values) == width * height * 3
    assert all(math.isfinite(v) and v >= 0 for v in values)
    return width, height, values


def difference(reference, candidate, width, height):
    total = sum(reference)
    roi = [i for y in range(int(height*.39), int(height*.61))
           for x in range(int(width*.52), int(width*.68))
           for i in range((y*width+x)*3, (y*width+x)*3+3)]
    return dict(relative_l1=sum(abs(a-b) for a, b in zip(reference, candidate))/total,
                integrated_fluence_ratio=sum(candidate)/total,
                focal_relative_l1=(sum(abs(reference[i]-candidate[i]) for i in roi) /
                sum(reference[i] for i in roi) if sum(reference[i] for i in roi) > total*1e-8 else None))


def reference(output, name, width, height, rays, shift=0, fog=.2):
    directory = output / f"reference-{name}-{width}-{height}-{rays}-{shift:g}-{fog:g}"
    directory.mkdir(exist_ok=True)
    path = directory / "fluence.pfm"
    if path.exists():
        return read_pfm(path)[2], path
    scene = fixture(name, fog)
    if shift:
        scene = replace(scene, shapes=tuple(replace(shape,
            center=(shape.center[0], shape.center[1]+shift),
            points=tuple((x,y+shift) for x,y in shape.points)) for shape in scene.shapes))
    ref = Reference(scene, width, height, 4)
    for ray in range(rays):
        ref.trace((.06, .3625+.4*(ray+.5)/rays), (1,0), (1/rays,)*3)
    write_images(directory, "fluence", ref.fluence, width, height, .25)
    (directory / "ledger.json").write_text(json.dumps(ref.ledger, indent=2)+"\n")
    return ref.fluence, path


def run(binary, output, label, method, scene="lens", width=240, height=135,
        rays=1024, directions=32, display=(2560,1440), change=None, repeats=3,
        warmup=128, frames=64, fog=.2, boundary_filter=0):
    directory = output / label
    directory.mkdir(exist_ok=True)
    reports = []
    for repeat in range(repeats):
        command = [str(binary), "--method", method, "--scene", str(SCENES[scene]),
            "--width", str(width), "--height", str(height), "--rays", str(rays),
            "--directions", str(directions), "--display-width", str(display[0]),
            "--display-height", str(display[1]), "--frames", str(frames),
            "--warmup", str(warmup), "--fog", str(fog),
            "--boundary-filter", str(boundary_filter),
            "--output", str(directory/f"run{repeat}")]
        if change:
            command += ["--off-frame" if change == "off" else "--move-frame", "1"]
        process = subprocess.run(command, capture_output=True, text=True, timeout=120)
        (directory / f"run{repeat}.log").write_text(process.stderr+process.stdout)
        (directory / f"run{repeat}-command.json").write_text(json.dumps(command,indent=2)+"\n")
        if process.returncode:
            raise RuntimeError(f"GPU run failed: {directory}/run{repeat}.log")
        report = json.loads(process.stdout.strip().splitlines()[-1])
        assert report["hidden_window"]
        reports.append(report)
    report = dict(label=label, method=method, scene=scene, fog=fog, change=change,
                  boundary_filter=boundary_filter, runs=reports, median_ms=statistics.median(r["p50_ms"] for r in reports),
                  worst_p95_ms=max(r["p95_ms"] for r in reports))
    captures = []
    for path in sorted(directory.glob("run0-*.pfm")):
        w, h, values = read_pfm(path)
        frame = int(path.stem.split("-")[-1])
        write_images(directory, path.stem, values, w, h, .25)
        capture = dict(frame=frame, image=str(path.with_suffix(".png").relative_to(output)),
                       integrated_fluence=sum(values)*(2*1.125)/(w*h*3))
        if change != "off" or frame < 1:
            ref, ref_path = reference(output, scene, w, h, rays,
                                      .1 if change == "move" and frame >= 1 else 0, fog)
            capture.update(difference(ref, values, w, h))
            capture["reference"] = str(ref_path.with_suffix(".png").relative_to(output))
            if method == "trace" and capture["relative_l1"] > .003:
                raise AssertionError(f"Tracer/reference disagreement: {label} {capture}")
        captures.append(capture)
    if change == "off":
        initial = captures[0]["integrated_fluence"]
        for capture in captures:
            capture["remaining_fluence_fraction"] = capture["integrated_fluence"]/initial
        if method in ("trace", "coarse"):
            assert all(c["integrated_fluence"] == 0 for c in captures if c["frame"] >= 1)
    report["captures"] = captures
    (directory / "report.json").write_text(json.dumps(report, indent=2)+"\n")
    print(f"{label}: {report['median_ms']:.3f} ms, worst repeat p95 {report['worst_p95_ms']:.3f} ms", flush=True)
    return report


def gallery(output, reports):
    rows = []
    for report in reports:
        images = []
        for capture in report["captures"]:
            metrics = (f"L1 {capture['relative_l1']*100:.2f}%" if "relative_l1" in capture else
                       f"remaining light {capture['remaining_fluence_fraction']*100:.1f}%")
            images.append(f"<figure><img src='{capture['image']}'><figcaption>Frame {capture['frame']} · {metrics}</figcaption></figure>")
        first = report["captures"][0]
        if "reference" in first:
            images.insert(0, f"<figure><img src='{first['reference']}'><figcaption>CPU reference · same samples</figcaption></figure>")
        rows.append(f"<section><h2>{html.escape(report['label'])}</h2><p>{report['median_ms']:.3f} ms median · {report['worst_p95_ms']:.3f} ms worst repeat p95</p><div>{''.join(images)}</div></section>")
    page = """<!doctype html><meta charset='utf-8'><title>GPU optics experiments</title>
<style>body{background:#14171b;color:#e4e7ec;font:15px system-ui;margin:32px;max-width:1500px}h1{font-size:28px}h2{font-size:19px}section{border-top:1px solid #454950;margin-top:28px;padding-top:8px}section div{display:flex;flex-wrap:wrap;gap:16px}figure{margin:0}img{width:384px;max-width:100%;image-rendering:auto;background:black}figcaption{padding:8px 0;color:#c7ccd4}p{max-width:1000px;line-height:1.5}</style>
<h1>GPU optics experiments</h1><p>Hidden native GPU runs. Timings cover optical transport and one display reconstruction, including synchronized GPU completion. They exclude diffuse GI, simulation, scene rasterization and final game composition. These images show planar ballistic fluence in persistent fog, not a camera view or multiple scattering. All previews use exposure 0.25.</p>
<p>Trace splits reflection and transmission through at most four interfaces. Iterate keeps a directional field across frames. Coarse rebuilds a hierarchy each frame, prolonging the coarse solution before fine iterations; it is not a reconstruction of Grant Kot's private solver or a residual-correction multigrid method. Temporal frame zero is the settled image; the change occurs at frame one.</p>""" + "".join(rows)
    (output / "comparison.html").write_text(page)
    (output / "summary.json").write_text(json.dumps(reports,indent=2)+"\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT/"build-release/gi_lab")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true", help="Short physical GPU regression checks")
    parser.add_argument("--filtered", action="store_true", help="Only test interpolation that respects optical boundaries")
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    assert args.repeats > 0
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binary = args.binary.resolve()
    files = [binary, Path(__file__), ROOT/"tools/optics_reference.py", *sorted((ROOT/"tools/gi_lab").rglob("*"))]
    provenance = dict(host=platform.platform(), files={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files if p.is_file()},
                      git_head=subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),
                      git_status=subprocess.check_output(["git","status","--short"],cwd=ROOT,text=True),
                      presentation="Hidden swapchain acquisition bypassed; CPU dispatch plus GPU completion")
    (output/"provenance.json").write_text(json.dumps(provenance,indent=2)+"\n")
    reports=[]
    if args.filtered:
        for name in ("lens", "mirror"):
            for method in ("iterate", "coarse"):
                reports.append(run(binary,output,f"{name}-{method}-filtered",method,scene=name,boundary_filter=1,repeats=args.repeats))
        reports.append(run(binary,output,"lens-iterate-filtered-off","iterate",boundary_filter=1,change="off",repeats=args.repeats))
    elif args.check:
        for name in SCENES:
            reports.append(run(binary,output,f"check-{name}","trace",scene=name,width=120,height=68,rays=256,warmup=2,frames=2,repeats=1))
        for method in ("trace","iterate","coarse"):
            reports.append(run(binary,output,f"check-off-{method}",method,width=120,height=68,rays=256,change="off",warmup=128,frames=64,repeats=1))
        mirror = run(binary,output,"check-grid-aligned-mirror","iterate",scene="mirror",boundary_filter=1,repeats=1)
        reports.append(mirror)
        path = (output/mirror["captures"][-1]["image"]).with_suffix(".pfm")
        w,h,values = read_pfm(path)
        # The finite mirror covers the emitter completely. Its downstream beam
        # region must stay dark even when mirror positions coincide with cells.
        assert max(values[(y*w+x)*3+c] for y in range(int(.36*h),int(.63*h))
                   for x in range(int(.7*w),w) for c in range(3)) < .001
    else:
        for name in SCENES:
            for method in ("trace","iterate","coarse"):
                reports.append(run(binary,output,f"{name}-{method}-240",method,scene=name,repeats=args.repeats))
                gallery(output,reports)
        for method in ("trace","iterate","coarse"):
            for change in ("off","move"):
                reports.append(run(binary,output,f"lens-{method}-{change}",method,change=change,repeats=args.repeats))
        for method in ("trace","iterate","coarse"):
            reports.append(run(binary,output,f"lens-{method}-480-4k",method,width=480,height=270,rays=2048,directions=64,display=(3840,2160),warmup=256,repeats=args.repeats))
        reports.append(run(binary,output,"dense-fog-trace","trace",fog=2,repeats=args.repeats))
    gallery(output,reports)
    print(output/"comparison.html")


if __name__ == "__main__":
    main()
