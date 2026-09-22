#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
export UV_CACHE_DIR="$PWD/.chibi/uv-cache" HF_HOME="$PWD/.chibi/huggingface"
if [ ! -x .chibi/retrieval-venv/bin/python ]; then
    uv venv --python 3.13 .chibi/retrieval-venv
fi
uv pip install --python .chibi/retrieval-venv/bin/python sentence-transformers==6.1.0 pydantic==2.13.5 truststore==0.10.4
.chibi/retrieval-venv/bin/python - <<'PY'
import truststore
truststore.inject_into_ssl()
from huggingface_hub import snapshot_download
snapshot_download('answerdotai/answerai-colbert-small-v1',
                  revision='934fa8bb4ce2284f4c2baa232d81aca4d076fa5e',
                  local_dir='.chibi/local-model/colbert-small',
                  ignore_patterns=['*.onnx', '*.bin', 'onnx/*'])
PY
