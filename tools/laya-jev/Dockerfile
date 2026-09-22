FROM python:3.12-slim

ARG LAYA_REPOSITORY=https://github.com/sirgio03/laya.git
ARG LAYA_REF=e683ef8d37b0828758a4e9e2004d1ab7fc3505bc

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    HF_HOME=/cache/huggingface \
    TOKENIZERS_PARALLELISM=false \
    USE_TF=0 \
    USE_TORCH=1

RUN apt-get update \
    && apt-get install --no-install-recommends -y ca-certificates git \
    && python -m pip install --no-cache-dir \
        "laya @ git+${LAYA_REPOSITORY}@${LAYA_REF}" \
        "fastapi>=0.111.0" \
        "uvicorn[standard]>=0.29.0" \
        "httpx>=0.27.0" \
    && apt-get purge -y --auto-remove git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY server.py token_budget.py /app/
COPY download_model.py /app/download_model.py
COPY entrypoint.sh /app/entrypoint.sh
RUN chmod 755 /app/entrypoint.sh

EXPOSE 8000

ENTRYPOINT ["/app/entrypoint.sh"]
