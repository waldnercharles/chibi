# Laya Jev compatibility service

This bundle runs the current Laya REST PR behind a small compatibility service. It exposes the typed decision contract used by Jev projects:

```text
POST /v1/systemone
POST /v1/decide
POST /v1/decide/batch
GET  /health
GET  /v1/capabilities
```

The image pins the PR source to commit `e683ef8d37b0828758a4e9e2004d1ab7fc3505bc`. The model is loaded once per container. The container entrypoint checks the persistent model directory on every start and downloads the pinned checkpoint only when it is missing.

Authenticated capabilities and decision usage report the loaded tokenizer and
configuration identities. `token_budget.py` measures the actual serialized
input before inference and rejects truncation of instructions, options, or
state. The editor caps requests at six heads and two decision rounds. See
[Editor commands](../editor-command/README.md) for the resolver and evaluation
workflow. Rebuild the image when updating these service files so container
recreation retains the changes.

## Run locally

```sh
cd tools/laya-jev
cp .env.example .env
# Replace the example API key in .env.
docker compose up -d --build
```

The default model directory is `$HOME/.cache/laya-jev/laya`, so the downloaded
checkpoint already present there is used immediately. If it is absent, the
container downloads it during its first startup and leaves it in that
persistent directory. `restart: unless-stopped` brings the container back after
Docker restarts, and the entrypoint skips the download when the files are
already complete.

Compose binds the port to localhost by default. Set `LAYA_BIND=0.0.0.0`
only when another machine needs to reach it, and keep the API key plus a
firewall or reverse proxy in front of the service.

## Download the model yourself

The model can also be downloaded explicitly before starting the service:

```sh
python3 -m pip install huggingface_hub
python3 download_model.py --output "$HOME/.cache/laya-jev/laya"
docker compose up -d
```

The downloader pins the checkpoint revision, copies only the English model
files, fixes the tokenizer configuration, verifies required files, and writes
`manifest.json` with the model size and SHA-256. If the checkpoint is already
in your Hugging Face cache, use `--offline --cache-dir "$HOME/.cache/huggingface/hub"`
to create the standalone directory without another network request.

The offline override mounts the model read-only and sets `LAYA_MODEL_PATH`, so
the server does not query Hugging Face at startup. The same directory can be
copied into a registry image or a Kubernetes PVC for an air-gapped Rancher
deployment. For a registry image, see `Dockerfile.preloaded` and
`k8s/deployment-preloaded-patch.yaml`.

Smoke test it with:

```sh
curl -H "Authorization: Bearer replace-with-a-long-local-secret" \
  -H 'content-type: application/json' \
  -d '{
    "state": {"message": "I was charged twice and want a refund."},
    "questions": {
      "department": {
        "type": "choice",
        "instructions": "Which team should handle this?",
        "criteria": {"billing": "invoices and refunds", "technical": "bugs"}
      },
      "refund": {"type": "noul", "instructions": "Does the customer ask for money back?"}
    }
  }' \
  http://127.0.0.1:8768/v1/systemone
```

Use the same URL from `fast-jev-compaction`:

```ts
new JevClient({
  baseUrl: 'http://127.0.0.1:8768/v1/systemone',
  apiKey: process.env.TYPESAFE_API_KEY,
});
```

The current `jev-ultrafast` checkout hardcodes the TypeSafe URL. Apply [patches/jev-ultrafast-base-url.patch](patches/jev-ultrafast-base-url.patch), then run it with:

```sh
export TYPESAFE_BASE_URL=http://127.0.0.1:8768/v1/systemone
export TYPESAFE_API_KEY=replace-with-a-long-local-secret
```

## Rancher and performance

Rancher Desktop can run this Compose file directly. For Rancher Manager, push
the image to a registry and follow [k8s/README.md](k8s/README.md); it provides
the Kubernetes Deployment, Service, persistent cache, probes, and optional GPU
patch. Compose remains the local reproducible source of truth.

The container adds one local HTTP hop, which is small compared with CPU model inference. Keep one container worker and the model warm. On a Linux Rancher node with an NVIDIA GPU, use:

```sh
docker compose -f compose.yaml -f compose.gpu.yaml up --build
```

That requires an NVIDIA runtime and a Torch build with CUDA support. A Rancher Desktop VM on macOS normally has no MPS passthrough, so native macOS Python can be faster than a CPU-only Linux container. A Linux GPU node is the preferred hosting target when latency matters.

Do not expose the service without `LAYA_API_KEY`; the Compose file requires the secret in `.env`.

## Validate the adapter

With the Laya Python environment available:

```sh
PYTHONPATH=. /path/to/python -m pytest test_server.py
```
