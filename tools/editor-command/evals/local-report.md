# Local command model comparison — 2026-09-21

GPT-OSS 20B is the default local model. The installed Ollama model reports GGUF
format and MXFP4 quantization. Inference runs on this Mac's GPU, not Rancher.
PydanticAI owns tool calls, structured output and one validation retry. Native
editor code remains responsible for revisions, transactions, readback and Undo.
Language requests still require review by default. Set `CHIBI_COMMAND_REVIEW=0`
to test automatic execution.

## Matched regression

Apple M4 Pro, 48 GB unified memory; Ollama 0.34.2; 8192 context; one model loaded.
Both runs used the same revised schema, observations and 74 requests. GPT-OSS
used low reasoning effort; Qwen used no reasoning. There are 62 requests with
specified operations and 12 whose old gold result is no operations.

| Model | Correct supported proposals | Median resolution | p95 resolution | Gold mismatches with operations |
|---|---:|---:|---:|---:|
| Qwen3.5 4B MLX | 53/62 (85.5%) | 1.24 s | 8.26 s | 11 |
| GPT-OSS 20B MXFP4 | 61/62 (98.4%) | 2.93 s | 6.81 s | 4 |

These are single sequential runs, not randomized or repeated performance trials.
Timing includes resolution and inference, including one initial request that
loaded each model. Similar consecutive prompts benefit from prefix caching.
The first GPT-OSS request took 15.53 seconds. Timing excludes editor application,
rendering and review. Live requests with richer catalogs took approximately
3–10 seconds in the subsequent checks. These measurements describe this editor
workload, not general model intelligence.

The corpus has already been used for diagnosis and regression; its historical
`heldout` label does not make it an untouched evaluation set. The original
catalog lacked capability metadata; it was added before both runs. All request
gold operations were retained. Scoring now permits floating-point roundoff and
renaming local copy references without changing their relationships. It still
requires exact targets, fields, operation counts, integer colors and booleans.

## Errors and follow-up

The GPT-OSS supported-request error selected BoostEmitter instead of the prior
TrailEmitter for "Set its rate to 23". Adding names to history alone did not fix
it. An explicit history instruction using observed object IDs fixed that targeted
regression. It changes prompts only when history exists; the table reports the
complete run before this follow-up refinement.

Three expected-abstention cases also returned operations:

- "Paint every object to match a sunset" produced only two edits to one emitter.
  This is an incomplete result and remains a real model error.
- The old text-generation handoff case now generated an action name and set the
  requested width. Generation is a new capability, so the old no-operation gold
  result does not describe the desired behavior of this model.
- A false numeric condition returned an assertion of the current value. It made
  no requested property change, but differs from the old empty-operation gold.

Consequently the four raw gold mismatches include two incorrect mutating
proposals, one newly supported generation request, and one read-only assertion.
Keep these distinctions when interpreting the automatic-execution counters in
`local-results.json`; benchmark proposals were not executed in an editor.

## Editor verification

Native and browser builds passed. The focused Python suite passed 16 tests,
web-server tests passed 3, and command bridge tests passed 2 with local socket
access. The sandboxed Node test runner aborts internally when opening its test
sockets; the same tests pass outside that restriction.

A full `make test` run completed the native test suite with two failures in
unrelated binding/type checks: "a type-mismatched active binding reports an error"
and "runtime rejects a whole-component value incompatible with the input".
Later make targets did not run after that failure; the full suite is not green.

Hands-on palette checks applied a two-property edit and restored it with Undo.
The final live model run passed seven requests with explicit prototype naming,
a follow-up, another scene, and a behavior-graph constant. All field edits were
read back and undone. Request times were 2.76–6.42 seconds, excluding human review. The graph check exposed a delayed revision counter that
invalidated guarded Undo one frame after a command. Graph identities now use
persistent asset/graph IDs, and revisions include the serialized graph content,
including connections. The focused live graph regression checks stable IDs,
connection-only changes, stale-request rejection and guarded Undo across frames.

No test changes were saved to authored assets. The model supports existing graph
inputs and connections; creating arbitrary new behavior graphs remains future
work. All measurements and full proposals are in `local-results.json`.

The richer live catalog also exposed inconsistent prototype-versus-instance
selection for an emitter name while another document was active. A later run
proposed the instance where the test expected the prototype; the proposal was
cancelled before applying. Explicit kind qualifiers are worth using in current
testing. The small fixture's 98.4% score is not a measured success rate across
arbitrary live editor states.
