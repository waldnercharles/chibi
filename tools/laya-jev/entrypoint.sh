#!/bin/sh
set -eu

MODEL_PATH=${LAYA_MODEL_PATH:-/models/laya}

model_ready() {
    [ -f "$MODEL_PATH/model.safetensors" ] \
        && [ -f "$MODEL_PATH/rl_agent_config.json" ] \
        && [ -f "$MODEL_PATH/tokenizer/tokenizer.json" ] \
        && [ -f "$MODEL_PATH/tokenizer/tokenizer_config.json" ] \
        && [ -f "$MODEL_PATH/encoder/config.json" ]
}

if ! model_ready; then
    if [ "${HF_HUB_OFFLINE:-0}" = "1" ]; then
        echo "Laya model is missing from $MODEL_PATH and HF_HUB_OFFLINE=1." >&2
        exit 1
    fi
    echo "Laya model is missing; downloading the pinned checkpoint..."
    mkdir -p "$MODEL_PATH"
    python /app/download_model.py --output "$MODEL_PATH"
fi

if [ "$#" -eq 0 ]; then
    set -- uvicorn server:app --host 0.0.0.0 --port 8000 --workers 1
fi

exec "$@"
