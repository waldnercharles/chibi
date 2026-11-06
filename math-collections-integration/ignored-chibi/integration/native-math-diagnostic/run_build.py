import hashlib
import json
import os
import pathlib
import signal
import subprocess
import time

task = pathlib.Path(__file__).resolve().parent
root = task.parents[2]
snapshot = root / ".chibi/integration/native-math-artifact-snapshot"
build = snapshot / ".chibi/integration/language"
manifest = json.loads((snapshot / "tools/compiler/bootstrap/bootstrap.json").read_bytes())
expected_paths = [row["path"] for row in manifest["sources"]]
environment = {key: value for key, value in os.environ.items() if not key.startswith("CHIBI_")}
argv = ["cmake", "--build", ".chibi/integration/language", "--parallel", "4"]
log = task / "build-native-math.log"
assert not log.exists()
start = time.monotonic()
diagnostic = None
print("Running " + json.dumps(argv), flush=True)
with log.open("wb") as output:
    process = subprocess.Popen(argv, cwd=snapshot, env=environment, stdout=output,
                               stderr=subprocess.STDOUT, start_new_session=True)
    while process.poll() is None:
        if diagnostic is not None:
            time.sleep(0.25)
            continue
        captured = build / "sdk/generated/assets/input.json"
        try:
            raw = captured.read_bytes()
            request = json.loads(raw)
        except (OSError, ValueError):
            time.sleep(0.25)
            continue
        if (len(request) != 12 or not request[3] or
                request[3][0] != "package_compiler/Compile.chibi" or
                request[3] == expected_paths):
            time.sleep(0.25)
            continue
        saved = task / "captured-bootstrap-request.json"
        saved.write_bytes(raw)
        host = build / "sdk/chibi-compiler-host"
        target_file = task / "fresh-native-target.json"
        target_argv = [str(host), "--target-json", str(target_file)]
        target_start = time.monotonic()
        with (task / "fresh-native-target.log").open("wb") as target_log:
            target_code = subprocess.call(target_argv, cwd=snapshot, env=environment,
                                          stdout=target_log, stderr=subprocess.STDOUT)
        target_seconds = time.monotonic() - target_start
        target_matches = target_code == 0 and json.loads(target_file.read_bytes()) == manifest["target"]
        reuse_argv = [str(host), "--reuse-bootstrap", "tools/compiler/bootstrap",
                      "--compiler", "tools/compiler", "--core", "packages/core",
                      "--input", str(saved), "--output-artifact", str(task / "reuse-output.chibip")]
        reuse_start = time.monotonic()
        with (task / "reuse-captured-request.log").open("wb") as reuse_log:
            reuse_code = subprocess.call(reuse_argv, cwd=snapshot, env=environment,
                                         stdout=reuse_log, stderr=subprocess.STDOUT)
        diagnostic = {
            "captured_request_sha256": hashlib.sha256(raw).hexdigest(),
            "captured_source_count": len(request[3]), "recorded_source_count": len(expected_paths),
            "extra_paths": [path for path in request[3] if path not in expected_paths],
            "missing_paths": [path for path in expected_paths if path not in request[3]],
            "target_argv": target_argv, "target_exit_code": target_code,
            "target_seconds": target_seconds, "fresh_target_matches_artifact": target_matches,
            "reuse_argv": reuse_argv, "reuse_exit_code": reuse_code,
            "reuse_seconds": time.monotonic() - reuse_start,
            "reuse_wrote_output": (task / "reuse-output.chibip").exists(),
        }
        if reuse_code != 0:
            diagnostic["cancellation_reason"] = "Task-directed stop after actual captured request could not reuse the recorded compiler; production guards unchanged"
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
            break
        time.sleep(0.25)
    code = process.wait()
receipt = {
    "purpose": "NATIVE MATH LOCAL DIRTY DIAGNOSTIC ONLY; not publication",
    "argv": argv, "cwd": str(snapshot), "exit_code": code,
    "wall_seconds": time.monotonic() - start, "diagnostic": diagnostic,
    "log": str(log), "log_sha256": hashlib.sha256(log.read_bytes()).hexdigest(),
    "head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=snapshot, text=True).strip(),
    "snapshot_revision": subprocess.check_output(["git", "describe", "--always", "--dirty"], cwd=snapshot, text=True).strip(),
    "tracked_status": subprocess.check_output(["git", "status", "--porcelain=v1", "--untracked-files=no"], cwd=snapshot, text=True),
}
(task / "build-native-math.json").write_text(json.dumps(receipt, indent=2) + "\n")
print(json.dumps(receipt), flush=True)
print(log.read_text(errors="replace")[-4000:], flush=True)
raise SystemExit(3 if diagnostic and diagnostic.get("cancellation_reason") else code)
