#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
export UV_CACHE_DIR="$PWD/.chibi/uv-cache" HF_HOME="$PWD/.chibi/huggingface"
if [ ! -x .chibi/commands-venv/bin/python ]; then
    tools/editor-command/setup-local.sh
fi
if [ ! -x .chibi/mlx-venv/bin/python ]; then
    uv venv --python 3.13 .chibi/mlx-venv
fi
uv pip install --python .chibi/mlx-venv/bin/python 'vllm-mlx[harmony]==0.4.1' mlx-lm==0.31.3 truststore==0.10.4
.chibi/mlx-venv/bin/python - <<'PY'
import truststore
truststore.inject_into_ssl()
from huggingface_hub import snapshot_download
snapshot_download('mlx-community/gpt-oss-20b-MXFP4-Q8',
                  revision='773a7da77e569019bb0fd17a554b263738d669a3',
                  local_dir='.chibi/local-model/mlx-gpt-oss')
PY
