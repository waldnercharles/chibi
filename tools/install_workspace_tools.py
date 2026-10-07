#!/usr/bin/env python3
"""Install native-session hooks, shared helpers and the macOS retirement job."""

import argparse
import json
import os
from pathlib import Path
import plistlib
import shlex
import shutil
import subprocess
import sys
import tempfile
import time


LABEL = "com.chibi.workspace-retirement"
EVENTS = ("SessionStart", "UserPromptSubmit", "PreToolUse", "Stop", "SessionEnd")


def read_settings(path):
    value = json.loads(path.read_text()) if path.exists() else {}
    if not isinstance(value, dict) or not isinstance(value.get("hooks", {}), dict):
        raise ValueError(f"Invalid hook settings: {path}")
    for event in EVENTS:
        if not isinstance(value.get("hooks", {}).get(event, []), list):
            raise ValueError(f"Invalid {event} hooks: {path}")
    return value


def merge_hooks(settings, command):
    hooks = settings.setdefault("hooks", {})
    for event in EVENTS:
        entries = hooks.setdefault(event, [])
        if any(hook.get("command") == command
               for entry in entries for hook in entry.get("hooks", [])):
            continue
        entries.append({"hooks": [{"type": "command", "command": command,
                                   "timeout": 3 if event == "SessionEnd" else 10}]})
    return settings


def atomic_write(path, content, mode):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(content)
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def prepare(home, state, source, launch_agent):
    launcher = home / ".local/bin/chibi-workspace"
    python = Path(sys.executable).absolute()
    command = (f"#!/bin/sh\nexport CHIBI_WORKSPACE_HOME={shlex.quote(str(state))}\n"
               f"exec {shlex.quote(str(python))} "
               f"{shlex.quote(str(state / 'bin/workspaces.py'))} \"$@\"\n")
    files = {launcher: (command.encode(), 0o755)}
    for name in ("workspaces.py", "workspace_retirement.py", "worktree_reaper.py"):
        files[state / "bin" / name] = ((source / name).read_bytes(), 0o755)
    for app, config in (("codex", home / ".codex/hooks.json"),
                        ("claude", home / ".claude/settings.json")):
        merged = merge_hooks(read_settings(config), f"{shlex.quote(str(launcher))} hook {app}")
        mode = config.stat().st_mode & 0o777 if config.exists() else 0o600
        files[config] = ((json.dumps(merged, indent=2) + "\n").encode(), mode)
    if launch_agent:
        logs = state / "logs"
        tool_dirs = [str(Path(found).parent) for name in ("rg", "ninja", "git", "lsof")
                     if (found := shutil.which(name))]
        environment = {
            "CHIBI_WORKSPACE_HOME": str(state),
            "PATH": os.pathsep.join(dict.fromkeys([
                str(python.parent), *tool_dirs, "/opt/homebrew/bin", "/usr/local/bin",
                "/usr/bin", "/bin", "/usr/sbin", "/sbin"])),
        }
        plist = {
            "Label": LABEL,
            "ProgramArguments": [str(python), str(state / "bin/workspace_retirement.py"),
                                 "sweep", "--apply", "--keep", "3", "--idle-days", "7"],
            "EnvironmentVariables": environment,
            "StartInterval": 3600,
            "RunAtLoad": True,
            "ProcessType": "Background",
            "StandardOutPath": str(logs / "retirement.log"),
            "StandardErrorPath": str(logs / "retirement-error.log"),
        }
        files[home / "Library/LaunchAgents" / f"{LABEL}.plist"] = (plistlib.dumps(plist), 0o600)
    return files


def install(home, state, source, launch_agent=False, apply=False):
    # Validate and render every file before changing any of the user's settings.
    files = prepare(home, state, source, launch_agent)
    changed = {path: value for path, value in files.items()
               if not path.exists() or path.read_bytes() != value[0]}
    if not apply:
        return {"mode": "dry-run", "files": [str(path) for path in changed]}
    backup = state / "backups" / str(time.time_ns())
    for path in changed:
        if path.is_symlink():
            raise ValueError(f"Refusing to replace symlink: {path}")
    originals = {}
    try:
        for path, (content, mode) in changed.items():
            originals[path] = (path.read_bytes(), path.stat().st_mode & 0o777) if path.exists() else None
            if path.exists():
                target = backup / str(path).lstrip("/")
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
            atomic_write(path, content, mode)
    except Exception:
        for path, original in originals.items():
            if original is None:
                path.unlink(missing_ok=True)
            else:
                atomic_write(path, *original)
        raise
    if launch_agent:
        (state / "logs").mkdir(parents=True, exist_ok=True)
        plist = home / "Library/LaunchAgents" / f"{LABEL}.plist"
        target = f"gui/{os.getuid()}/{LABEL}"
        loaded = subprocess.run(["launchctl", "print", target], capture_output=True).returncode == 0
        if loaded and plist in changed:
            subprocess.run(["launchctl", "bootout", target], check=True, capture_output=True)
            loaded = False
        if not loaded:
            subprocess.run(["launchctl", "bootstrap", f"gui/{os.getuid()}", str(plist)],
                           check=True, capture_output=True)
    return {"mode": "installed", "changed": len(changed), "backup": str(backup) if backup.exists() else None,
            "launcher": str(home / ".local/bin/chibi-workspace"),
            "codex_hook_review": "Review the configured commands in /hooks so native Codex sessions run them."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true", help="write files; default is a dry run")
    parser.add_argument("--home", type=Path, default=Path.home())
    parser.add_argument("--state-home", type=Path)
    parser.add_argument("--no-launch-agent", action="store_true")
    args = parser.parse_args()
    home = args.home.expanduser().resolve()
    state = (args.state_home or home / ".local/share/chibi-workspaces").expanduser().resolve()
    launch_agent = sys.platform == "darwin" and not args.no_launch_agent
    try:
        result = install(home, state, Path(__file__).resolve().parent, launch_agent, args.apply)
        print(json.dumps(result, indent=2))
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"Workspace installation failed: {exc}\n")


if __name__ == "__main__":
    main()
