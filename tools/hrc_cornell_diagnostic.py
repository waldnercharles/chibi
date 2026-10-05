#!/usr/bin/env python3
"""Capture bounded HRC Cornell intermediate diagnostics from the native renderer.

The script changes only debug_view in disposable copies of the authored
projects. It writes one final 320x180 still per view and leaves the source
projects untouched. The renderer must include the opt-in diagnostic debug
views Surface quadrants and Gather probes.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
CASES = [
    ("flat-blur-transport", "projects/hrc_lab", "HRCLabCornell2D", "HRC transport quadrants"),
    ("flat-blur-surface", "projects/hrc_lab", "HRCLabCornell2D", "Surface quadrants"),
    ("camera-blur-transport", "projects/sdf_lab", "HrcCornellModesBlur", "HRC transport quadrants"),
    ("camera-blur-surface", "projects/sdf_lab", "HrcCornellModesBlur", "Surface quadrants"),
    ("camera-gather-transport", "projects/sdf_lab", "HrcCornellModesGather", "HRC transport quadrants"),
    ("camera-gather-surface", "projects/sdf_lab", "HrcCornellModesGather", "Surface quadrants"),
    ("camera-gather-probes", "projects/sdf_lab", "HrcCornellModesGather", "Gather probes"),
    ("flat-fluence-bounce-off", "projects/hrc_lab", "HRCLabCornell2D", "Fluence", 0.0),
    ("flat-fluence-bounce-on", "projects/hrc_lab", "HRCLabCornell2D", "Fluence", 0.35),
]


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def scene_with_view(source, destination, debug_view, bounce_intensity=None):
    text = source.read_text()
    lines = text.splitlines(keepends=True)
    changed = 0
    for index, line in enumerate(lines):
        if "GameHRC2D" not in line:
            continue
        if "debug_view=" in line:
            raise RuntimeError(f"{source} already has an authored debug_view")
        line = line.replace("GameHRC2D", f'GameHRC2D debug_view="{debug_view}"', 1)
        if bounce_intensity is not None:
            line, replaced = re.subn(
                r"\bbounce_intensity=[^\s]+",
                f"bounce_intensity={bounce_intensity:g}",
                line,
                count=1,
            )
            if replaced != 1:
                raise RuntimeError(f"{source} has no authored bounce_intensity")
        lines[index] = line
        changed += 1
    if changed != 1:
        raise RuntimeError(f"Expected one GameHRC2D line in {source}, found {changed}")
    destination.write_text("".join(lines))


def response_summary(response):
    result = response["result"]
    preview = result.get("preview", {})
    return {
        "ok": response.get("ok"),
        "operation": response.get("operation"),
        "initial_state": result.get("initial_state"),
        "final_state": result.get("final_state"),
        "backend": result.get("backend"),
        "performance": result.get("performance"),
        "preview": {
            "mime_type": preview.get("mime_type"),
            "width": preview.get("width"),
            "height": preview.get("height"),
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build-release")
    parser.add_argument(
        "--output", type=Path,
        default=ROOT / "projects/sdf_lab/evidence/cornell-mode-diagnostics",
    )
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--height", type=int, default=180)
    parser.add_argument("--render-frames", type=int, default=256)
    args = parser.parse_args()

    build = args.build.resolve()
    ctl = build / "chibi_ctl"
    if not ctl.is_file():
        raise SystemExit(f"Missing controller: {ctl}")
    if not (2 <= args.render_frames <= 3600):
        raise SystemExit("--render-frames must be in [2, 3600]")
    if not (64 <= args.width <= 4096 and 64 <= args.height <= 4096):
        raise SystemExit("width and height must be in [64, 4096]")

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True
    ).strip()
    manifest = {
        "source_checkpoint": source,
        "controller": str(ctl),
        "controller_sha256": sha256(ctl),
        "render_frames": args.render_frames,
        "freeze_simulation": False,
        "width": args.width,
        "height": args.height,
        "output_count": len(CASES),
        "display_transfer": "RGB log1p(energy * 65535) / log1p(65535); alpha is shown in a separate strip",
        "cases": [],
    }

    with tempfile.TemporaryDirectory(prefix="hrc-cornell-diagnostic-") as temp:
        scratch = Path(temp)
        for case in CASES:
            name, project_rel, scene_name, debug_view = case[:4]
            bounce_intensity = case[4] if len(case) > 4 else None
            project_source = ROOT / project_rel
            project_copy = scratch / f"{name}-project"
            shutil.copytree(
                project_source,
                project_copy,
                ignore=shutil.ignore_patterns(
                    ".chibi", "build", ".git", "evidence"
                ),
            )
            scene_rel = Path("content/scenes") / f"{scene_name}.kdl"
            original_scene = project_source / scene_rel
            copied_scene = project_copy / scene_rel
            scene_with_view(original_scene, copied_scene, debug_view, bounce_intensity)

            request = {
                "contract": 1,
                "operation": "preview_scene",
                "kind": "scene",
                "name": scene_name,
                "frames": 1,
                "render_frames": args.render_frames,
                "freeze_simulation": False,
                "width": args.width,
                "height": args.height,
            }
            request_path = output / f"{name}.request.json"
            request_path.write_text(json.dumps(request, indent=2) + "\n")
            command = [
                str(ctl), "--project-root", str(project_copy),
                "--render-request", str(request_path),
            ]
            run = subprocess.run(
                command, cwd=ROOT, text=True, capture_output=True, check=False,
            )
            (output / f"{name}.log").write_text(run.stderr)
            if run.returncode:
                (output / f"{name}.stdout.txt").write_text(run.stdout)
                raise SystemExit(f"Render failed for {name}; see {output / f'{name}.log'}")
            response = json.loads(run.stdout)
            if not response.get("ok"):
                (output / f"{name}.response.json").write_text(
                    json.dumps(response, indent=2) + "\n"
                )
                raise SystemExit(f"preview_scene failed for {name}")
            summary = response_summary(response)
            if summary["preview"].get("width") != args.width or \
                    summary["preview"].get("height") != args.height:
                raise SystemExit(f"Unexpected preview dimensions for {name}: {summary['preview']}")
            png = base64.b64decode(response["result"]["preview"]["data"])
            image_path = output / f"{name}.png"
            image_path.write_bytes(png)
            run_record = {
                "name": name,
                "scene": scene_name,
                "debug_view": debug_view,
                "bounce_intensity_override": bounce_intensity,
                "source_scene_sha256": sha256(original_scene),
                "debug_scene_sha256": sha256(copied_scene),
                "project_root": str(project_copy),
                "argv": command,
                "request": request,
                "response": summary,
                "image": image_path.name,
                "image_sha256": sha256(image_path),
            }
            (output / f"{name}.run.json").write_text(
                json.dumps(run_record, indent=2) + "\n"
            )
            (output / f"{name}.response.json").write_text(
                json.dumps(summary, indent=2) + "\n"
            )
            manifest["cases"].append(run_record)
            print(f"{name}: {args.width}x{args.height}, {summary['backend']}")

    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Wrote {len(CASES)} single-frame diagnostics to {output}")


if __name__ == "__main__":
    main()
