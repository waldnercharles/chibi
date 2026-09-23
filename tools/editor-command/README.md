# Local editor commands

Cmd+K sends your request and live editor metadata to a local generative model.
The default is GPT-OSS 20B through Ollama on Apple Silicon, with low reasoning effort.
PydanticAI handles structured output, read-only inspection tools and one repair
retry. There is no handwritten language grammar or Laya fallback in this path.

## Try it

```sh
tools/editor-command/setup-local.sh
tools/editor-command/run-local.sh --build build-web --project projects/laya_lab --port 8115
```

Open `http://127.0.0.1:8115/chibi.html`, press Cmd+K, and enter a request.
Installation uses ignored `.chibi/` directories for Python, Ollama and model
weights. The first launch downloads the model; later launches reuse it. Inference
runs on the Mac GPU, outside Rancher. The server binds to local loopback and
Ollama cloud routing is disabled.

The launcher preloads the model and defaults to `OLLAMA_KEEP_ALIVE=-1`, keeping
it resident until the server exits or it is explicitly unloaded. This occupies
about 12 GB of unified memory for the tested GPT-OSS configuration. Override the
setting to release it after an idle interval. Restart an existing Ollama server
to change its default; exporting a variable does not reconfigure a running process.

Examples:

- Change the BoostEmitter prototype particle color to green and end color to purple.
- Set its rate to 23.
- Set the selected width to 72.
- Open the LayoutLab scene.
- Set Left Tile width to 80 and height to 50.
- Hide the Inspector.

Up/Down and Enter choose clarification answers; Escape closes the palette and
cancels its pending request. Applied changes show their targets and values with
Undo. External API tasks continue independently of palette visibility.

`CHIBI_MODEL=qwen3.5:4b-mlx` selects the faster but less accurate comparison model.
`CHIBI_MODEL_URL` may select
another loopback OpenAI-compatible endpoint. The launcher manages port 11435;
use `tools/web_serve.py` directly when managing another endpoint yourself.
Language proposals require Apply by default because the measured model still
makes semantic errors. `CHIBI_COMMAND_REVIEW=0` enables automatic execution for
testing. Typed API requests use their caller-supplied operations directly.
Restart the web server or native editor after changing the resolver or its model.
Native editors use the same persistent Python worker and automatically use the
project virtual environment; the local model server must already be running.

## How it works

1. Native code exports object IDs, component/property metadata, current values,
   constraints, choices, selection and a document revision.
2. Generic name search preloads likely objects and reflected property names
   retrieve relevant fields. The model receives the complete object
   directory and can inspect other objects through a read-only tool. Approximate
   names are resolved against that directory. Follow-ups include the previously
   affected identities. Omitted properties require inspection before use;
   matching property names and competing object identities remain available.
3. The model returns a complete typed proposal, a clarification, or an unsupported
   result. It can calculate values and generate requested text directly. Python
   checks that the proposal references observed, editable properties and legal
   operations. No mutation occurs during inference.
4. Native code rejects stale observations, validates the entire batch, applies
   content edits atomically and reads back the result. Content changes form one
   Undo step. A successful readback proves that the proposed edits were applied;
   it does not prove that the model understood the request.

The same field metadata supports particles, geometry, booleans, names, enum
choices and existing graph inputs. The live executor also supports connecting
existing graph nodes and duplicating scene instances. Node creation/deletion,
arbitrary graph synthesis and multi-document edit plans are not yet exposed.
Requests needing those operations should return unsupported without partial edits.
The separate supervised computer-use adapter remains available; Cmd+K does not
fall back to desktop clicks.

The complete object directory currently goes into the model context. Large
projects need bounded catalog retrieval before this design can scale. Descriptions
belong with native operation/property metadata, not in a catalog of English
sentence patterns. Adding ColBERT would only change retrieval; it would not supply
planning, arguments or completion verification.

Property retrieval is a prompt optimization, not an English interpreter. It
does not choose an operation, interpret a color, calculate a number, or authorize
an edit. When there is no property-name match, it supplies all fields; the model
can always inspect the full object through the read-only tool.

## API

Native editors expose newline-delimited JSON when launched with `CHIBI_EDITOR_SOCKET=/tmp/chibi-command.sock`. Browser development servers write a mode-0600 connection file at `.chibi/web-editor-<port>.json`. Its token stays local. Clients must select a registered live browser session; there is no file-edit substitute.

| Operation | Input | Result |
|---|---|---|
| `inspect` | — | Current catalog and revision |
| `resolve_objects` | `query`, optional `kinds` | Task returning ranked live identities |
| `submit` | `command` or `operations` + `revision`; optional `dry_run`, `request_id` | Task ID and status |
| `status` | `task_id` | Current result |
| `respond` | `task_id`, `value` or `approve: true` | Resume a clarification or reviewed proposal |
| `respond` | `task_id`, `undo: true` or `feedback: "incorrect"` | Guarded Undo or local feedback |
| `cancel` | `task_id` | Cancel pending work |

Statuses include `resolving`, `needs_clarification`, `needs_review`, `ready` (validated dry run), `applied`, `resolved` (lookup), `unsupported`, `failed`, `cancelled`, `undone`, and `busy`.

The MCP tool is `editorCommand`. The CLI uses exactly the same adapter:

```sh
printf '%s\n' '{"socket":"/tmp/chibi-command.sock","operation":"inspect"}' \
  | node tools/mcp/editor-command-cli.mjs

printf '%s\n' '{"connection_file":".chibi/web-editor-8112.json","operation":"inspect"}' \
  | node tools/mcp/editor-command-cli.mjs
```

The second command lists sessions until a `session` is supplied. Inspect before submitting already-bound operations:

```json
{
  "operation": "submit",
  "request_id": "client-generated-unique-id",
  "revision": "revision-from-inspect",
  "dry_run": true,
  "operations": [
    {"op": "set", "target": "object-id-from-inspect", "field": "field-id-from-inspect", "value": 30}
  ]
}
```

Bound operations use the native validator directly. Their author already supplies the intended targets and values, so they do not make a redundant model call. Natural-language requests use the local model and the same native validator.

| Bound operation | Required fields |
|---|---|
| `set`, `assert` | `target`, `field`, typed `value`; optional `result_target` for a prior copy |
| `duplicate` | entity `target`, optional unique `result` identity for later operations |
| `connect` | source node `target` and output `field`; destination node `destination` and input `pin` |
| `open`, `select`, `frame` | observed `target` identity |
| `panel` | observed panel `target`, boolean visibility `value` |
| `runtime` | `target: "scene"` or `"preview"`; `value: "play"`, `"pause"`, or `"stop"` |
| `save`, `undo`, `redo` | no extra fields; submit separately |
| `create_project` | unique `name`; creates `projects/<name>/project.json` and an empty Main scene in a native editor |
| `create_asset` | unique `name`, `kind`, `definition`; validates, saves and opens the new asset |
| `edit_graph` | observed open graph asset `target`, `definition` containing semantic graph operations; one Undo step, save separately |
| `set_start_scene` | observed scene `target`; updates the project's startup scene |
| `input` | bound `action`, `pressed` boolean, optional `frames` (1–600, default 1); playing scene only |
| `capture` | local PNG `path`; captures the native editor framebuffer |

Submit creation, graph edits, startup selection, input and capture separately.
Use `dry_run: true` to validate creation and graph edits before publishing.
Creation saves a new asset and has no document Undo. `create_project` leaves the
current project open; launch the created project with `--project-root projects/<name>`.

Prototype definitions contain `components` and `behaviors`. Scene definitions
contain `entities` with `name`, `prototype`, component overrides and `behaviors`.
Graph definitions contain `owner_type` and `operations`. `add_node` uses `type`
for the reflected node name and `ref` for a batch-local identity. `connect` uses
`from`, `from_pin`, `to`, `to_pin` and `kind` (`data` or `exec`). `add_variable`
uses `name`, `type` and `default`. `set_pin` can specialize wildcard pins; fixed
native pin types cannot be changed. Inspect with `kinds: ["nodes"]` or
`kinds: ["components"]` and an optional `query` before building definitions.

Input holds the authored control for the requested number of frame samples,
then releases it. Stop clears held input. Native sessions started with
`CHIBI_EDITOR_SOCKET` render into a capturable framebuffer.

MCP inspections return a successful catalog. Task responses expose `status`,
`task_id` and `message` in both the visible result and `structuredContent`.
`ready` means a dry run passed; `applied` means the command ran. A `failed`
response retains its diagnostic. Poll only pending tasks.

Reset and relative operations resolve to typed `set` values from the captured
catalog. Assertions check preserved values without adding an edit. Property
IDs, legal choices, inherited values, and graph pin direction come from
`inspect`; callers must not derive them from display names.

## Evaluation

```sh
.chibi/commands-venv/bin/python -m unittest discover -s tools/editor-command -p 'test_*.py'
python3 tools/editor-command/evaluate.py --split heldout --include-probes --include-local --output .chibi/commands/local.jsonl
node tools/editor-command/graph-smoke.mjs path-to-live-transport.json
```

The frozen corpus contains 138 requests plus 12 direct probes. Another 16 cases
exercise percentages, text, multiple edits, copy references, graph connections,
conditions and unsupported requests. These are regression sets, not untouched
holdouts. Evaluation checks complete proposals and reports abstentions, incorrect
proposals and latency. Numeric comparisons allow floating-point roundoff; copy
reference names may differ while preserving their relationships. Colors, targets,
fields, operation counts and all other values must match.

See `evals/local-report.md` for measured results and known failures. The older
`direct-report.md` describes the retired Laya implementation, not the current path.
The old text-generation handoff probe expects abstention; a generative model can
now legitimately fulfill it, so that case needs separate interpretation.

## Runtime and retrieval experiments

See [residency and LoRA measurements](evals/residency-report.md) for cold versus
warm timings, the latest prompt comparison, and the separate smaller-model pilot.

```sh
.chibi/commands-venv/bin/python tools/editor-command/benchmark_residency.py \
  --cases tools/editor-command/evals/runtime-screen-cases.json \
  --output .chibi/commands/residency.jsonl --rounds 3
```

This benchmark explicitly unloads GPT-OSS between conditions, so run it while
the command palette is idle. It produces proposals only. It does not flush the
OS filesystem cache. Immediate repeats include prompt-cache reuse; the empty
preload condition measures residency without a previously cached command.

The local QLoRA pilot is reproducible with:

```sh
tools/editor-command/setup-lora.sh
.chibi/commands-venv/bin/python tools/editor-command/training_data.py \
  --output .chibi/commands/lora-data
.chibi/mlx-venv/bin/python -m mlx_lm.lora --config tools/editor-command/lora-pilot.yaml
```

The 128 training, 32 validation, and 48 test examples are synthetic and validated
against the editor contract. Evaluation names and phrasings differ from training;
the operation families overlap. These are a small feasibility experiment, not a
real-user accuracy claim. Existing regression benchmarks are never training data.
The manifest freezes dataset hashes. Training uses only train/valid; keep test
examples out of prompt and adapter tuning. Adapters stay in ignored `.chibi/` and
are not enabled by the command launcher.

See [the tuning report](evals/tuning-report.md) for the matched Ollama cache,
MLX, and ColBERT/Laya measurements. Ollama with its full-precision KV cache
remains the default. These experiments do not enable Laya execution.

`CHIBI_REASONING=low` is the GPT-OSS default; medium/high are available for
accuracy experiments. GPT-OSS does not accept `none` in this adapter.
`OLLAMA_KEEP_ALIVE=-1` keeps the model resident; `OLLAMA_CONTEXT_LENGTH=8192`
is the default context allocation. Server environment changes require restarting
the model server, not just the web server. The launcher now preserves these
environment overrides. More context or parallel requests are not speed knobs
for a single command. Lowering the output cap can truncate a valid transaction.

Reproduce the complete-request benchmark without editing any assets:

```sh
.chibi/commands-venv/bin/python tools/editor-command/benchmark.py \
  --cases tools/editor-command/evals/tuning-cases.json \
  --label ollama --output .chibi/commands/benchmark.jsonl
```

For ColBERT, install the separate environment and run the controlled comparison:

```sh
tools/editor-command/setup-retrieval.sh
.chibi/retrieval-venv/bin/python tools/editor-command/compare_retrieval.py \
  --cases tools/editor-command/evals/shadow-cases.json \
  --output .chibi/commands/retrieval.jsonl
```

It compares lexical and ColBERT retrieval at 1, 2, 3, and 5 candidates, always
with an additional defer option. Descriptions and fully bound actions come from
observed objects and capabilities. Document embeddings are cached; query encoding
is included in the reported request latency. This is an offline shadow harness,
not a production router or English interpreter.

The optional MLX comparison uses the pinned model and `vllm-mlx` Harmony parser:

```sh
tools/editor-command/setup-mlx.sh
.chibi/mlx-venv/bin/vllm-mlx serve "$PWD/.chibi/local-model/mlx-gpt-oss" \
  --host 127.0.0.1 --port 11436 --enable-auto-tool-choice \
  --tool-call-parser harmony --reasoning-parser harmony \
  --max-num-seqs 1 --cache-memory-mb 2048 --max-tokens 800
# In another terminal:
CHIBI_MODEL_BACKEND=mlx CHIBI_MODEL_OUTPUT=prompted \
  .chibi/commands-venv/bin/python tools/editor-command/benchmark.py \
  --cases tools/editor-command/evals/runtime-screen-cases.json --rounds 1 \
  --label mlx --output .chibi/commands/mlx.jsonl
```

`CHIBI_MODEL_OUTPUT=native` (default) requests constrained JSON decoding;
`prompted` tests schema prompting alone. Both use the same typed validation and
repair limits. The basic `mlx_lm.server` did not handle this model's Harmony tool
messages correctly in this experiment. Run GPU comparisons sequentially and
unload the other model to avoid competing memory and compute.

Resolution permits up to three model requests, one repair retry, 800 output
tokens and a 22-second total timeout. Editor mutation happens only after resolution.
Cancellation ignores late results; it does not promise immediate GPU interruption.
The launcher keeps one model loaded for 30 minutes. First requests can include
model loading, Python imports and prompt prefill and are slower than warm requests.

Native traces default to `.chibi/command-traces.jsonl` (`CHIBI_COMMAND_TRACE`
overrides this). Browser traces live under the served project's `.chibi/`.
They contain requests, observations, results and feedback, without API keys.
Use incorrect-result feedback to add regressions before changing the model or
prompt. Unit tests check the adapter and execution boundary; real-model regression
and live editor checks are separate evidence.
