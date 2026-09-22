# Laya and Jev project integration

Checked 2026-09-20 against the current public repositories.

## Recommendation

Start with a local Laya adapter for [`jev-ultrafast`](https://github.com/browser-use/jev-ultrafast). Its `choose()` function already turns the browser observation into `choice` questions, and its executor already validates the returned choice before acting. Laya returns the same fields that this code consumes: `choice`, `probabilities`, and `confidence`.

Keep the separate text model for `TYPE_TEXT`. Laya chooses an operation and target; it does not generate field values.

## Laya facts that affect the design

- The official Python package is `laya`; the runtime loads `convaiinnovations/laya` with `laya.load()` or `Router`.
- The model answers `choice`, `score`, and `noul` questions in one forward pass and emits no generated text.
- The English checkpoint has a 512-token context. The multilingual checkpoint has a 1,024-token default context and is the better candidate when state or language coverage needs more room.
- The published latency numbers are T4 measurements. The package documentation reports roughly 193–464 ms on CPU for a preloaded request, so local warm latency must be measured separately.
- Laya's own comparison says it falls behind Jev on large choice sets. Browser target lists with more than roughly 20 candidates need a fixture benchmark, larger head budgets, candidate grouping, or a fallback.
- The 0.766 typed-decisions result comes from the fine-tuned typed-decisions checkpoint. The base checkpoints perform near the random baseline on that workflow, so a browser adapter needs task-specific evaluation before it drives real actions.

## Minimal Python port

Add `laya` to `jev-ultrafast` and replace the HTTP request in `jev_ultrafast/model.py` with a cached local agent:

```python
import laya

_LAYA_AGENT = None


def laya_agent():
    global _LAYA_AGENT
    if _LAYA_AGENT is None:
        _LAYA_AGENT = laya.load(
            os.environ.get("LAYA_MODEL", "convaiinnovations/laya"),
            device=os.environ.get("LAYA_DEVICE") or None,
        )
    return _LAYA_AGENT
```

Then call `laya_agent().predict(body["state"], body["questions"])` where the current code posts `body` to TypeSafe. The existing `validate_choice()` and the remainder of the executor can stay in place. Use a direct English checkpoint first. Use the router or multilingual checkpoint only after measuring the actual pages and languages.

## fast-jev-compaction

[`fast-jev-compaction`](https://github.com/tamaratran/fast-jev-compaction) exposes a `JevAsker` interface with one `ask(state, questions)` method. That is a good abstraction for a Laya provider. The practical bridge is a small long-lived Python process that loads Laya once and exposes a local HTTP endpoint; the TypeScript package calls it through a custom `JevAsker`.

This port needs extra care. The compaction package fits state for Jev's much larger request budget, while Laya's normal checkpoints only see 512 or 1,024 tokens. The adapter must explicitly fit or reorder the state for Laya, and its keep/drop questions need a small labeled evaluation set.

## Proxy / compatibility layer

There is an upstream Laya pull request that is almost exactly this bridge. It adds a FastAPI server with `/v1/decide`, batch inference, and a `/v1/systemone` alias intended to be wire-compatible with TypeSafe Jev, plus a TypeScript SDK. It is still an open pull request, so it should be treated as experimental until merged and released.

If it is not ready, build the same shape locally: a long-lived Python process loads Laya once, accepts the existing Jev request JSON, calls `Router.predict()`, and returns the existing `answers` envelope. `jev-ultrafast` can point its current `https://api.typesafe.ai/v1/systemone` URL at this service, and `fast-jev-compaction` can implement its `JevAsker` with a small HTTP client. That gives the projects one provider boundary without maintaining separate Laya forks.

Keep the proxy responsible for transport, schema validation, authentication, metrics, and provider selection. Keep candidate trimming, state fitting, fallback behavior, and browser-specific evaluation in the caller or a shared adapter. Do not expose Laya as a chat-completions proxy: its useful contract is typed decisions with probabilities and confidence, not generated text.

## Reading the larger project list

The Reddit review is useful as a map of patterns, not as compatibility evidence. The strongest Laya candidates are the projects whose source keeps a provider boundary around typed decisions:

- Browser and desktop loops: bounded `choice` questions over deterministic observations, followed by local execution.
- Model routers and CLI tools: small `choice` or `score` questions with a short state. These are likely to be the easiest Laya wins.
- Context garbage collectors: many `noul` relevance decisions over long histories. These need a context-aware adapter and stronger task evaluation.
- MCP and SDK wrappers: transport work. A local Python service or validated ONNX implementation can fill the provider slot.

The [`awesome-jev-projects`](https://github.com/logicrw/awesome-jev-projects) directory is more useful for this purpose than copying individual Jev prompts because it records the decision boundary and groups projects by domain. The Reddit post and directory both point to `jev-ultrafast` and `fast-jev-compaction` as representative examples, but each project's model assumptions still need to be checked before swapping in Laya.

## Suggested validation order

1. Run a warm local Laya decision on a static page with fewer than ten controls.
2. Compare Jev and Laya on identical recorded observations and goals. Record success, wrong-target choices, number of steps, cold load time, and warm p50/p95 decision latency.
3. Increase the number of controls until accuracy or latency breaks. This finds the target-cardinality limit before browser execution makes failures harder to diagnose.
4. Only then port the TypeScript context-compaction projects through a sidecar or a validated ONNX runtime.

The community [`laya-onnx`](https://huggingface.co/Mattepiu/laya-onnx) export can support a Node path, but the official Laya runtime and `jev-ultrafast` path are Python, so the sidecar is the lower-risk first integration.

## Live lab results

On 2026-09-20, an isolated lab under `/private/tmp/laya-jev-lab` cloned the PR source branch `feat/rest-api-server-and-pydantic-schemas`, installed it in Python 3.12, and served the English checkpoint on CPU at `http://127.0.0.1:8768`.

- Laya's server tests passed: `10 passed`; routing and criteria standalone checks passed: `98` and `34` checks.
- The PR's TypeScript SDK passed typecheck, build, and `11` tests. A live SDK request returned billing and refund answers through `/v1/decide`.
- `jev-ultrafast` passed its offline suite (`31 passed`), its `choose()` function returned a live Laya decision, and a full local HTML browser run completed `CLICK` followed by `DONE`.
- `fast-jev-compaction` passed typecheck, build, and `29` tests. Its `JevClient` and `compact()` flow completed a live request through the same server.

The PR needed two local compatibility fixes before `jev-ultrafast` worked. The server had to remove Router-only `routing` metadata before validating the public response, and its Pydantic request schema had to accept the structured `instructions` and choice criteria that `jev-ultrafast` actually sends. The server also needs the model alias `LAYA_MODEL=laya`; its default full Hugging Face ID is not currently accepted by `Router.preload()`.

The base English checkpoint made the correct click-only browser decision, but on a synthetic form with empty fields it sometimes chose `DONE` or the wrong operation. The transport path is working; browser policy quality still needs a recorded task benchmark and likely a browser-specific fine-tune or fallback.

## Container deployment results

The reproducible bundle is in [`tools/laya-jev`](../../tools/laya-jev/). It
pins the PR to commit `e683ef8d37b0828758a4e9e2004d1ab7fc3505bc`, keeps one
preloaded worker, persists the Hugging Face cache, and exposes the
`/v1/systemone` compatibility endpoint. The Rancher Manager manifests use one
Deployment replica, a 5 GiB cache PVC, a ClusterIP Service, and startup probes
that allow model loading to finish.

The image built successfully at 3.33 GB on the local arm64 Docker runtime. A
warm CPU container returned the billing/refund smoke request in 0.388 seconds;
five subsequent requests took 0.358, 0.377, 0.379, 0.392, and 0.485 seconds.
The local HTTP hop was small relative to inference for this workload. The
image is large because the upstream Torch dependency pulled CUDA libraries
even for `LAYA_DEVICE=cpu`; those libraries add image transfer and disk cost,
not CPU inference work.

The live container path was exercised by `fast-jev-compaction`'s
`JevClient` and `compact()` flow, `jev-ultrafast`'s `choose()` call, and a full
local browser loop that executed `CLICK` followed by `DONE`. The bundle now
also includes `download_model.py`, which copies only the English checkpoint to
a standalone directory, fixes the tokenizer configuration, and writes a
revision/SHA-256 manifest. The resulting 807 MiB directory loaded correctly
from the host and from a read-only Docker mount with `HF_HUB_OFFLINE=1`.

The image entrypoint now makes that path automatic: Compose mounts the
persistent model directory, the container downloads the pinned checkpoint only
when its required files are absent, and Uvicorn starts afterward. A preloaded
image override is also available for deployments that need the model inside the
image rather than in a host mount.

An air-gapped image built from that directory was 4.11 GB and passed both
health and authenticated inference requests without a model volume or Hub
access. The regular image was 3.33 GB. The first fresh container startup
could not download Hugging Face files through the local corporate TLS proxy
because its CA is not present in the slim image; the preloaded image or a
Rancher node with its corporate CA installed avoids that dependency.
