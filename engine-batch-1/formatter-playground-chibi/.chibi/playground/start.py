from pathlib import Path
import subprocess, sys
repo = Path(__file__).resolve().parents[2]
state = repo / ".chibi/playground"
with (state / "server.log").open("ab", buffering=0) as log:
    server = subprocess.Popen([sys.executable, "-u", str(repo / "tools/docs/playground/serve.py"), str(repo), str(state / "runtime/sdk/chibi"), "8765"], cwd=repo, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
(state / "server.pid").write_text(str(server.pid) + "\n")
print(f"Playground PID {server.pid}: http://127.0.0.1:8765/")
