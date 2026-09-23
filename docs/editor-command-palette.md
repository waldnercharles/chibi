# Editor command palette

The palette turns a request into a proposal against the editor's current catalog. It runs in the native C editor and uses two local decision paths plus a ChatGPT subscription provider:

1. A narrow C quick edit handles an unambiguous, named-target, single-property change. It copies target IDs, field IDs, choice values, and current values from the catalog. Other requests continue to the model.
2. GPT-OSS receives a focused catalog and returns a constrained JSON proposal. The C adapter normalizes literal colors and listed choices, checks target and field IDs, and rejects unavailable operations.
3. ChatGPT sends the full observed catalog to a Codex model. The editor signs in through a browser, sends the request over HTTPS, and converts the streamed `submit_plan` result to the same proposal contract.

All providers use the same review and execution contract. A dry run checks the proposal before showing it. Approval applies it against the original catalog revision. A fresh catalog readback verifies set values. A failed readback is reported as unverified; the editor does not silently retry a mutation.

The palette settings popup accepts a provider and model name. Choose **ChatGPT** and **Sign in** to use a Codex model through the account's subscription. The default model in that preset is `gpt-6-luna`; the model field can be changed if the account offers a different model. No separate Codex process runs. The browser sign-in opens a temporary callback on localhost port 1455. Sign out removes the editor's stored credential.

For local providers, the popup accepts an HTTP model host. `JSON schema` enables the OpenAI-compatible JSON-schema response path used with llama.cpp. The default host is the local Ollama API at `http://127.0.0.1:11435/api`, with model `gpt-oss:20b`. Ollama requests keep the model resident for 30 minutes. Provider and model settings last for the editor session.

The direct ChatGPT connection follows Pi's Codex subscription protocol. OpenAI documents the separate Codex App Server integration, but does not publish this direct endpoint as a stable public API. If the direct protocol changes, this provider may need an update.

For a local llama.cpp host, use its `llama-server` with the GPT-OSS [chat template](../tools/mcp/evaluation/gpt-oss-chat-template.jinja), `--jinja`, and `--reasoning-effort low`; set the palette host to its `/v1` URL and enable `JSON schema`. The model file and server lifecycle are managed outside the editor.

Headless checks use the same command API and execution path as the palette. They create an isolated scene, submit each request, inspect the exact proposal, approve supported edits, and read back affected and preserved fields:

```sh
make build
build/test_suite --command-model
build/test_suite --command-quick-edit
CHIBI_LIVE_HOST=http://127.0.0.1:59624/v1 CHIBI_LIVE_JSON_SCHEMA=1 \
  build/test_suite --command-live gpt-oss:20b tools/mcp/evaluation/command-smoke.json
CHIBI_LIVE_HOST=http://127.0.0.1:59624/v1 CHIBI_LIVE_JSON_SCHEMA=1 \
  build/test_suite --command-live gpt-oss:20b tools/mcp/evaluation/command-dialogues.json
CHIBI_LIVE_HOST=http://127.0.0.1:59624/v1 CHIBI_LIVE_JSON_SCHEMA=1 \
  build/test_suite --command-project-live gpt-oss:20b projects/arena2d
# After signing in to ChatGPT in the native editor:
CHIBI_LIVE_HOST=codex build/test_suite --command-live gpt-6-luna \
  tools/mcp/evaluation/command-smoke.json
```

The project smoke loads the authored start scene and Player prototype without creating a window. It asks GPT-OSS for one exact width change, approves it as a dry run, reads the original value back, and compares the Player asset bytes before and after.

The numbered `command-validation-v*.json` files are broader exact-operation corpora. Use a new unseen fixture to measure generalization after changing decision rules. A fast quick-edit result has zero model calls; it must not be counted as model latency. Requests outside the observed editor catalog, and batches spanning documents, are unsupported. Unspecified targets, fields, or amounts require clarification.

The first run of the independently written v7 fixture matched 44/49 expected outcomes. After semantic fixes, the same fixture matched 48/49 with low reasoning. Its 43 model calls had a 2.9 s median and 4.8 s p95; six narrow quick edits had a 7 ms median. Minimal reasoning matched 47/49 with a 2.7 s model median. These are synthetic scene tests, not evidence of general autonomous app use. The runner approves only proposals that match the expected operations exactly; it does not silently apply model mistakes.
