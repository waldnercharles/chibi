"""Durable, single-use checkpoints shared by the CLI and Chibi MCP."""

from __future__ import annotations

import fcntl
import json
from contextlib import contextmanager
from dataclasses import asdict
from pathlib import Path

from . import config, macos
from .actions import Context
from .runner import RunConfig, RunState, run
from .supervisor import Supervisor

ROOT = Path(__file__).resolve().parents[3] / ".chibi" / "computer-use"


@contextmanager
def locked(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise ValueError("Another computer-use request is running") from error
        yield


def validate(cfg):
    if not cfg.goal.strip() or len(cfg.goal) > 2000:
        raise ValueError("Goal must contain 1-2000 characters")
    if not 1 <= cfg.steps <= 100 or not 1 <= cfg.burst <= 5:
        raise ValueError("steps must be 1-100 and burst 1-5")
    if not 0 <= cfg.min_confidence <= 1 or not 0 <= cfg.delay <= 10:
        raise ValueError("Invalid confidence or delay")
    if cfg.editor_socket:
        if cfg.workflow != "create_asset" or cfg.replay or len(cfg.goal) > 200:
            raise ValueError("The editor connection requires workflow=create_asset, no replay and a goal of at most 200 characters")
        return
    if cfg.workflow:
        raise ValueError("A structured workflow requires --editor-socket")
    if cfg.act and not cfg.replay and not cfg.target_app:
        raise ValueError("Live execution requires --target-app (the exact frontmost process name)")
    if cfg.act and not cfg.replay and not macos.accessibility_trusted():
        raise ValueError(
            "Accessibility permission is required for live input. Enable the hosting app in macOS Privacy & Security."
        )


def save(cfg, state):
    data = {
        "config": {k: str(v) if isinstance(v, Path) else v for k, v in asdict(cfg).items()},
        "state": {
            k: getattr(state, k) for k in ("history", "timings", "consecutive_noops", "last_url", "outcome", "step", "pending", "editor")
        },
    }
    temporary = cfg.out / "session.tmp"
    temporary.write_text(json.dumps(data, indent=2))
    temporary.replace(cfg.out / "session.json")


def execute(cfg, state, pending=None, reply=None):
    if cfg.editor_socket:
        from .editor import run_editor

        run_editor(cfg, state, lambda current: save(cfg, current), pending, reply)
        return status(cfg.out)
    supervisor = Supervisor(pending, reply, cfg.target_app)
    # App-scoped editor tasks have no reason to switch to an unrelated browser.
    browser = "" if cfg.target_app else config.browser()

    def context(client, history):
        return Context(cfg.goal, browser, None, client, supervisor, history)

    run(cfg, context, state, checkpoint=lambda current: save(cfg, current))
    return status(cfg.out)


def start(cfg):
    validate(cfg)
    cfg.out = cfg.out.resolve()
    with locked(ROOT / ".desktop.lock"), locked(cfg.out / ".lock"):
        if (cfg.out / "session.json").exists():
            raise ValueError("Run already exists; resume it or choose another output folder")
        state = RunState()
        save(cfg, state)
        return execute(cfg, state)


def resume(out, request_id, reply):
    out = Path(out).resolve()
    if not isinstance(reply, dict) or not reply:
        raise ValueError("Reply must be a nonempty JSON object")
    with locked(ROOT / ".desktop.lock"), locked(out / ".lock"):
        data = json.loads((out / "session.json").read_text())
        pending = data["state"].get("pending")
        if data["state"]["outcome"] != "paused" or not pending or pending["request_id"] != request_id:
            raise ValueError("Unknown or already consumed request_id")
        cfg = RunConfig(**data["config"])
        cfg.out = out
        cfg.image = Path(cfg.image) if cfg.image else None
        validate(cfg)
        state = RunState(**data["state"])
        state.pending = None
        state.outcome = "running"
        # Persist consumption before any desktop input. A crash never replays a reply.
        save(cfg, state)
        return execute(cfg, state, pending, reply)


def status(out):
    out = Path(out).resolve()
    data = json.loads((out / "session.json").read_text())
    current = data["state"]
    result = json.loads((out / "run.json").read_text()) if (out / "run.json").exists() else {"goal": data["config"]["goal"]}
    outcome = current["outcome"]
    if outcome in {"running", "crashed"}:
        try:
            with locked(out / ".lock"):
                outcome = "interrupted"
        except ValueError:
            pass
    result.update(outcome=outcome, pending=current["pending"], history=current["history"], run_dir=str(out))
    return result
