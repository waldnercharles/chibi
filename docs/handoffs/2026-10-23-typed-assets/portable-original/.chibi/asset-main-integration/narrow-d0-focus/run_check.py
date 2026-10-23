from pathlib import Path
import datetime
import hashlib
import json
import os
import subprocess
import sys
import time

folder = Path(__file__).resolve().parent
root = folder.parents[2]
receipt = folder / "receipt.json"
data = json.loads(receipt.read_text())
for key, checkout in (("engine", root), ("studio", root.parent / "chibi-studio")):
    for field, arguments in (
        ("head", ["rev-parse", "HEAD"]),
        ("merge_head", ["rev-parse", "MERGE_HEAD"]),
        ("tree", ["write-tree"]),
    ):
        actual = subprocess.check_output(["git", *arguments], cwd=checkout, text=True).strip()
        expected = data[key][field]
        if key == "engine" and field == "tree":
            expected = data.get("execution_engine_tree", expected)
        assert actual == expected, (key, field, actual)
    assert not subprocess.check_output(["git", "diff", "--name-only"], cwd=checkout).strip()
label, *argv = sys.argv[1:]
assert label and argv
assert not any(item["label"] == label for item in data["checks"])
log = folder / (label + ".log")
assert not log.exists()
row = {
    "label": label,
    "argv": argv,
    "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "state": "running",
    "log": log.name,
    "engine_tree": data.get("execution_engine_tree", data["engine"]["tree"]),
    "studio_tree": data["studio"]["tree"],
}
data["checks"].append(row)
receipt.write_text(json.dumps(data, indent=2) + "\n")
environment = os.environ.copy()
environment["PATH"] = "/opt/homebrew/opt/python@3.13/libexec/bin:" + environment.get("PATH", "")
started = time.monotonic()
print("START", label, flush=True)
with log.open("wb") as output:
    process = subprocess.run(argv, cwd=root, env=environment, stdout=output, stderr=subprocess.STDOUT)
row.update(
    state="passed" if process.returncode == 0 else "failed",
    exit_code=process.returncode,
    elapsed_seconds=round(time.monotonic() - started, 3),
    completed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
    log_sha256=hashlib.sha256(log.read_bytes()).hexdigest(),
)
receipt.write_text(json.dumps(data, indent=2) + "\n")
print(json.dumps(row), flush=True)
print("\n".join(log.read_text(errors="replace").splitlines()[-24:]), flush=True)
raise SystemExit(process.returncode)
