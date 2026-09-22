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
2. Generic name search preloads likely objects. The model receives the object
   directory and can inspect other objects through a read-only tool. Approximate
   names are resolved against that directory. Follow-ups include the previously
   affected identities.
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
