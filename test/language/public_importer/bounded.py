"""macOS-only wrapper: bound the aggregate RSS of the command's descendants."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def snapshot():
    output = subprocess.check_output(
        ["ps", "-axo", "pid=,ppid=,pgid=,rss=,lstart="], text=True, timeout=5
    )
    rows = {}
    for line in output.splitlines():
        fields = line.split(maxsplit=4)
        if len(fields) == 5:
            pid, parent, group, rss = map(int, fields[:4])
            rows[pid] = (parent, group, rss, fields[4])
    return rows


def send(pid, sig):
    try:
        os.kill(pid, sig)
    except ProcessLookupError:
        pass


def cleanup(process, known):
    def survivors():
        try:
            current = snapshot()
            return {pid for pid, started in known.items()
                    if pid in current and current[pid][3] == started}
        except (OSError, ValueError, subprocess.SubprocessError):
            # Retain the last observed descendants if monitoring itself fails.
            return set(known)

    def stop(sig):
        try:
            os.killpg(process.pid, sig)
        except ProcessLookupError:
            pass
        for pid in survivors():
            send(pid, sig)

    stop(signal.SIGTERM)
    deadline = time.monotonic() + 1
    while time.monotonic() < deadline:
        process.poll()
        if not survivors() and process.returncode is not None:
            break
        time.sleep(0.05)
    # Descendants may survive after the root exits, including in other groups.
    stop(signal.SIGKILL)
    process.wait(timeout=5)


def main():
    name, seconds, mib, *command = sys.argv[1:]
    output = Path("evidence") / name
    output.parent.mkdir(parents=True, exist_ok=True)
    limit = int(mib) * 1024
    started = time.monotonic()
    peak, stopped, monitor_error = 0, None, None
    known = {}
    with output.with_suffix(".log").open("w") as log:
        process = subprocess.Popen(
            ["/usr/bin/time", "-l", *command], stdout=log,
            stderr=subprocess.STDOUT, start_new_session=True
        )
        try:
            while process.poll() is None:
                rows = snapshot()
                ids = {process.pid}
                while True:
                    more = {pid for pid, (parent, group, _, _) in rows.items()
                            if parent in ids or group == process.pid}
                    if more <= ids:
                        break
                    ids |= more
                for pid in ids & rows.keys():
                    known[pid] = rows[pid][3]
                rss = sum(rows[pid][2] for pid in ids & rows.keys())
                peak = max(peak, rss)
                if rss > limit or time.monotonic() - started > int(seconds):
                    stopped = "memory" if rss > limit else "timeout"
                    break
                time.sleep(0.2)
        except (OSError, ValueError, subprocess.SubprocessError, KeyboardInterrupt) as error:
            stopped = "monitor_error"
            monitor_error = str(error)
        finally:
            if stopped is not None:
                cleanup(process, known)
        code = process.wait()
    result = dict(command=command, exit=code,
                  elapsed_seconds=round(time.monotonic() - started, 3),
                  peak_group_rss_kib=peak, limit_mib=int(mib),
                  timeout_seconds=int(seconds), stopped=stopped,
                  monitor_error=monitor_error)
    output.with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))
    # Resource or monitoring failure remains a failure even if SIGTERM exits 0.
    return 1 if stopped is not None or code < 0 else code


if __name__ == "__main__":
    sys.exit(main())
