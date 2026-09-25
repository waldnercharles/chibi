# Editor command palette

The Studio command palette (Cmd+K / Ctrl+K) lists commands, objects and assets.
When a query is typed, a trailing **Ask** row sends it to the editor command
runtime, which turns the request into a proposal against the editor's current
catalog. The same runtime serves the `editorCommand` MCP tool and the local
control socket (see [mcp.md](mcp.md#drive-a-live-editor)).

A request takes one of two local decision paths:

1. A narrow C quick edit handles an unambiguous, named-target, single-property
   change. It copies target IDs, field IDs, choice values and current values
   from the catalog, and makes no model call. Other requests continue to the
   model.
2. GPT-OSS receives a focused catalog and returns a JSON proposal constrained
   by a per-request schema. The C adapter normalizes literal colors and listed
   choices, checks target and field IDs, and rejects unavailable operations.

Both paths use the same execution contract. A dry run checks every proposal
against the original catalog revision. A loading toast shows the request while
the model works; its Cancel stops it. Changes that Undo can revert (`set`,
`assert`, `duplicate`, `connect`, `edit_graph`) and navigation apply at once,
and a success toast lists their targets and values with Undo. The toast
dismisses itself once the document has a later edit, so it never offers a
stale Undo. `create_project`, `create_asset`, `set_start_scene`, `save` and
`capture` reopen the palette with one Confirm row under what will happen:
Enter confirms and Escape cancels. A question reopens the palette with its
choices, or takes a typed answer. A fresh catalog readback verifies set
values; a failed readback shows an error toast, and the editor never silently
retries a mutation. Failures show a toast and appear in Output. Captures are
saved in the project's `.chibi/` directory.

The palette ranks its commands, objects and assets itself and always lists the
Ask row last.

The catalog spells enum values and choices by label, as asset documents do.
Graph pins are fields keyed by side and name (`in.<name>`, `out.<name>`).
Studio registers its drawers (Assets, Output, Performance) as panels and
provides framing and document Undo/Redo; without Studio, as in `--imgui` mode
or headless checks, those operations are unavailable.

## Model host

The default host is the local Ollama API at `http://127.0.0.1:11435/api`, with
model `gpt-oss:20b`; `CHIBI_MODEL_URL` and `CHIBI_MODEL` override them. Hosts
ending in `/api` use Ollama's native chat with schema output and keep the model
resident for 30 minutes. Other hosts use the OpenAI-compatible
chat-completions interface with a `submit_plan` function call, or a JSON-schema
response when enabled. Only plain HTTP is accepted.

The native editor posts to the host on a background thread. The browser editor
cannot reach the host across origins, so `tools/web_serve.py` forwards the
request from its token-guarded `/__chibi/model` route, and only to a loopback
HTTP address.

For a local llama.cpp host, use its `llama-server` with the GPT-OSS
[chat template](../tools/mcp/evaluation/gpt-oss-chat-template.jinja),
`--jinja`, and `--reasoning-effort low`; set the host to its `/v1` URL and
enable the JSON schema (`CHIBI_LIVE_JSON_SCHEMA=1` in the checks below). The
model file and server lifecycle are managed outside the editor.

Each finished request is appended to `.chibi/command-traces.jsonl`
(`CHIBI_COMMAND_TRACE` overrides the path), or to the served project's
`.chibi/` directory in the browser.

## Checks

Headless checks use the same command API and execution path as the palette.
They create an isolated scene, submit each request, compare the applied
operations with the expected ones, and read back affected and preserved
fields. The first two run in `make test` without a model host:

```sh
make build
build/test_suite --command-model
build/test_suite --command-quick-edit
CHIBI_LIVE_HOST=http://127.0.0.1:11435/api \
  build/test_suite --command-live gpt-oss:20b tools/mcp/evaluation/command-smoke.json
CHIBI_LIVE_HOST=http://127.0.0.1:11435/api \
  build/test_suite --command-live gpt-oss:20b tools/mcp/evaluation/command-dialogues.json
CHIBI_LIVE_HOST=http://127.0.0.1:11435/api \
  build/test_suite --command-project-live gpt-oss:20b projects/arena2d
```

`COMMAND_LIVE_CASE` runs one case, `COMMAND_LIVE_LIMIT` the first cases, and
`COMMAND_LIVE_VERBOSE` prints failing replies. The project check loads the
authored start scene and Player prototype without a window, asks for one
exact width change as a dry run, reads the original value back, and
compares the Player asset bytes before and after.

The numbered `command-validation-v*.json` files are broader exact-operation
corpora. Use a new unseen fixture to measure generalization after changing
decision rules. A quick edit has zero model calls and must not be counted as
model latency. Requests outside the observed editor catalog, and batches
spanning documents, are unsupported. Unspecified targets, fields or amounts
require clarification. These are synthetic scene tests, not evidence of
general autonomous app use: a case passes only when the applied operations
match the expected ones exactly. The live checks register Studio's drawers as
panels, so panel cases name only Assets, Output and Performance. With
`gpt-oss:20b` on Ollama, v7 matched 45/49 expected outcomes; five were quick
edits with no model call.
