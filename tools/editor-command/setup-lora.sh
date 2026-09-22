#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
export UV_CACHE_DIR="$PWD/.chibi/uv-cache" HF_HOME="$PWD/.chibi/huggingface"
if [ ! -x .chibi/mlx-venv/bin/python ]; then
    uv venv --python 3.13 .chibi/mlx-venv
fi
uv pip install --python .chibi/mlx-venv/bin/python 'mlx-lm[train]==0.31.3' truststore==0.10.4
.chibi/mlx-venv/bin/python - <<'PY'
import truststore
truststore.inject_into_ssl()
from huggingface_hub import snapshot_download
snapshot_download('mlx-community/Qwen3-4B-Instruct-2507-4bit',
                  revision='50d427756c6b1b2fe0c0a10f67fbda1fc8e82c1b',
                  local_dir='.chibi/local-model/qwen3-4b-instruct')
PY
