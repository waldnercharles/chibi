#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
export OLLAMA_HOST=127.0.0.1:11435
export OLLAMA_MODELS="$PWD/.chibi/local-model/models"
export OLLAMA_NO_CLOUD=1 OLLAMA_NUM_PARALLEL=1 OLLAMA_MAX_LOADED_MODELS=1
export OLLAMA_KEEP_ALIVE="${OLLAMA_KEEP_ALIVE:--1}" OLLAMA_CONTEXT_LENGTH="${OLLAMA_CONTEXT_LENGTH:-8192}"
runtime="$PWD/.chibi/local-model/runtime/ollama"
python="$PWD/.chibi/commands-venv/bin/python"
if [ ! -x "$runtime" ] || [ ! -x "$python" ]; then
    echo 'Run tools/editor-command/setup-local.sh first.' >&2
    exit 1
fi
if ! curl -fsS http://127.0.0.1:11435/api/version >/dev/null; then
    nohup "$runtime" serve > .chibi/local-model/server.log 2>&1 &
    echo $! > .chibi/local-model/server.pid
    "$python" - <<'PY'
import time, urllib.request
for _ in range(60):
    try:
        urllib.request.urlopen('http://127.0.0.1:11435/api/version', timeout=1).close()
        break
    except OSError:
        time.sleep(1)
else:
    raise SystemExit('Local model server did not start. See .chibi/local-model/server.log')
PY
fi
export CHIBI_MODEL="${CHIBI_MODEL:-gpt-oss:20b}"
if ! "$runtime" show "$CHIBI_MODEL" >/dev/null 2>&1; then
    "$runtime" pull "$CHIBI_MODEL"
fi
"$python" - <<'PY'
import json, os, urllib.request
packet = {'model': os.environ['CHIBI_MODEL'], 'stream': False, 'keep_alive': os.environ['OLLAMA_KEEP_ALIVE']}
request = urllib.request.Request('http://127.0.0.1:11435/api/generate', json.dumps(packet).encode(), {'Content-Type': 'application/json'})
with urllib.request.urlopen(request, timeout=120) as response:
    print('Local model ready:', json.load(response)['model'])
PY
exec "$python" tools/web_serve.py "$@"
