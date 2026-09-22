#!/bin/sh
# Install into ignored project storage; no global Python or application changes.
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
if [ "$(uname -s)" != Darwin ] || [ "$(uname -m)" != arm64 ]; then
    echo 'This installer targets Apple Silicon. Install Ollama and the Python requirements for other hosts.' >&2
    exit 1
fi
command -v uv >/dev/null
version=v0.34.2
dir=.chibi/local-model
mkdir -p "$dir/runtime"
if [ ! -x "$dir/runtime/ollama" ]; then
    curl -fL --retry 2 "https://github.com/ollama/ollama/releases/download/$version/ollama-darwin.tgz" -o "$dir/ollama-darwin.tgz"
    curl -fL --retry 2 "https://github.com/ollama/ollama/releases/download/$version/sha256sum.txt" -o "$dir/sha256sum.txt"
    python3 - <<'PY'
import hashlib, pathlib, tarfile
p = pathlib.Path('.chibi/local-model')
expected = next(line.split()[0] for line in (p / 'sha256sum.txt').read_text().splitlines() if line.endswith('ollama-darwin.tgz'))
assert hashlib.file_digest((p / 'ollama-darwin.tgz').open('rb'), 'sha256').hexdigest() == expected
with tarfile.open(p / 'ollama-darwin.tgz') as archive:
    archive.extractall(p / 'runtime', filter='data')
PY
fi
export UV_CACHE_DIR="$PWD/.chibi/uv-cache"
if [ ! -x .chibi/commands-venv/bin/python ]; then
    uv venv --python 3.13 .chibi/commands-venv
fi
uv pip install --python .chibi/commands-venv/bin/python -r tools/editor-command/requirements.txt
printf '%s\n' 'Installed. Run tools/editor-command/run-local.sh --project projects/laya_lab --port 8115'
