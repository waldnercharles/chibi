"""Command-line entry points: `clicker` and `clicker-inspect`."""

from __future__ import annotations

import argparse
import json
import signal
import subprocess
import sys
import time
import uuid
from pathlib import Path

from . import config, macos, session
from .perception import capture, perceive
from .report import annotate, ax_count, render_payload
from .runner import RunConfig
from .timing import format_timing

DOTENV = Path.cwd() / ".env"


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(prog="clicker", description="Local Laya computer use with Codex supervision")
    commands = parser.add_subparsers(dest="command", required=True)
    start = commands.add_parser("start")
    start.add_argument("goal")
    start.add_argument("--act", action="store_true", help="execute native clicks and typing")
    start.add_argument("--target-app", help="exact foreground process name; required for live input")
    start.add_argument("--editor-socket", help="opt-in Chibi local socket; structured editor input")
    start.add_argument("--workflow", choices=["create_asset"], help="bounded workflow; Laya chooses the asset type, Codex supplies its name")
    start.add_argument("--steps", type=int, default=12)
    start.add_argument("--burst", type=int, default=3)
    start.add_argument("--min-confidence", type=float, default=config.DEFAULT_MIN_CONFIDENCE)
    start.add_argument("--delay", type=float, default=config.DEFAULT_DELAY)
    start.add_argument("--out", type=Path, default=session.ROOT / uuid.uuid4().hex)
    start.add_argument("--image", type=Path, help="saved image replay; never executes input")
    start.add_argument("--app", help="application name for saved image replay")
    start.add_argument("--url", help="URL for saved image replay")
    resume = commands.add_parser("resume")
    resume.add_argument("out", type=Path)
    resume.add_argument("request_id")
    resume.add_argument("--reply", type=Path, help="JSON file; omit to read stdin")
    status = commands.add_parser("status")
    status.add_argument("out", type=Path)
    commands.add_parser("permissions")
    args = parser.parse_args(argv)
    config.load_dotenv(DOTENV)

    def interrupted(*_):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, interrupted)
    try:
        if args.command == "permissions":
            result = macos.permissions()
        elif args.command == "start":
            values = vars(args).copy()
            values.pop("command")
            result = session.start(RunConfig(**values))
        elif args.command == "resume":
            reply = json.loads(args.reply.read_text() if args.reply else sys.stdin.read())
            result = session.resume(args.out, args.request_id, reply)
        else:
            result = session.status(args.out)
        print(json.dumps(result, indent=2))
    except Exception as error:
        print(json.dumps({"outcome": "error", "error": str(error)}))
        sys.exit(1)


def inspect(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        prog="clicker-inspect",
        description="Count down, capture the screen, and show the OCR/accessibility observation before Laya compaction.",
    )
    parser.add_argument("goal", nargs="?", default="(no goal given)")
    parser.add_argument("--countdown", type=int, default=3)
    parser.add_argument("--no-open", action="store_true", help="write files without opening them")
    parser.add_argument("--out", type=Path, default=Path("inspections") / time.strftime("%Y%m%d-%H%M%S"))
    args = parser.parse_args(argv)
    config.load_dotenv(DOTENV)
    args.out.mkdir(parents=True, exist_ok=True)

    for n in range(args.countdown, 0, -1):
        print(f"{n}...", end=" ", flush=True)
        time.sleep(1)
    print("capture")

    browser = config.browser()
    timing: dict[str, float] = {}
    screen = capture(browser=browser, timing=timing)
    items = perceive(screen, config.MAX_OPTIONS, args.goal, timing)
    annotated = args.out / "annotated.png"
    text = args.out / "state.txt"
    screen.image.save(args.out / "raw.png")
    annotate(screen, items, chosen="", out=annotated)
    text.write_text(render_payload(args.goal, screen, items, [], browser, config.email()))

    print(
        f"app={screen.app!r} url={screen.url!r} items={len(items)} ax={ax_count(items)} "
        f"offscreen={len(screen.offscreen)} field={screen.field.role if screen.field else None}"
    )
    print(format_timing(timing))
    print(f"  {annotated}\n  {text}")
    if not args.no_open:
        subprocess.run(["open", str(annotated)], check=False)
        subprocess.run(["open", "-t", str(text)], check=False)


if __name__ == "__main__":
    main()
