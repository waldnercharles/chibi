# Editor command evaluation — 2026-09-21

Historical interpreter results. The handwritten interpreter and its automatic
confidence policy have since been removed. The current direct resolver and
its measurements are documented in [direct-report.md](direct-report.md).

The shared command workflow is implemented and running in native and browser
editors. The quality gate remains conservative: the final held-out regression
run resolved 40/40 supported requests correctly, automatically accepted 30/40,
and made no incorrect automatic proposals. Automatic coverage is **75%**, below
the planned 80% target. The remaining proposals require review.

## Frozen resolver benchmark

`requests.jsonl` contains 138 requests: 120 supported and 18 expected abstentions.
Each partition has 40 supported and six abstention cases. `frozen.json` records
the corpus hash. Requests cover properties, arithmetic, multiple clauses,
follow-ups, names, selection, copies, graph pins, panels, history, playback,
and unsupported instructions. Cases share families across partitions; they do
not establish accuracy for unrestricted language or unseen projects.

The catalog combines captured native reflected fields with explicit instance,
graph, and ambiguity fixtures. This benchmark measures proposal resolution;
it does not execute mutations. Full live catalogs can contain more competing
properties and therefore produce more reviews than this benchmark.

| Run | Correct supported proposals | Automatic coverage | Wrong automatic | Unsupported with operations | p50 / p95 |
|---|---:|---:|---:|---:|---:|
| Original resolver, all 138 | 64/120 | 42.5% | 1 | 2 | 351 / 661 ms |
| Original resolver, held-out partition | 20/40 | 45% | 0 | 0 | 348 / 657 ms |
| First held-out evaluation | 40/40 | 72.5% | 0 | 0 | 382 / 585 ms |
| Final regression run | 40/40 | 75% | 0 | 0 | 224 / 283 ms |

Timings include Python resolution and the local HTTP Laya call, excluding editor
transport, application, rendering, and human review. They include all cases,
including quick abstentions and deterministic navigation. The same corpus and
CPU Laya checkpoint were used. The original resolver ran before tokenizer
validation was added, and service warm-up/load varied, so these numbers are not
a controlled hardware speedup claim. The native live command completed in
691 ms in the final focused test. Two browser requests took about 490 ms and
300 ms of processing, excluding time waiting for Apply; these are observations,
not end-to-end percentile estimates.

`results.json` stores the baseline, first held-out, final calibration, and final
held-out summaries, plus the policy and baseline source hash. Detailed local
records are under `.chibi/commands/`. The original resolver snapshot is retained
locally at `.chibi/commands/baseline-resolver.py`; it is not a production fallback.

## Decisions and calibration

Literal values and arithmetic are computed from observed types. Laya selects
targets or property labels from compact menus. Its disagreement with an explicit
property match requires review; the chosen property is never silently changed.
Both correct reviewed proposals and correct automatic proposals count toward
resolution. Thus 40/40 is pipeline proposal accuracy, **not raw Laya accuracy**.

Development experiments compared exact-value acceptance with property-choice
heads. Full observed property labels performed better. The prompt was frozen
before the first held-out run. Subsequent changes hardened literal handling,
context validation, read-only pins, idle-connection recovery, and clause target
inheritance and asset filename matching. Recalibration
used only the calibration partition; the held-out set was rerun as regression
evidence. It is no longer an untouched holdout for future tuning.

The policy requires the resolver source hash, model configuration hash,
tokenizer hash, operation family, and candidate-count gate to match. Sparse
family samples are an initial empirical gate, not a statistical reliability
guarantee. Model configuration/tokenizer hashes do not fingerprint model weights.
The service measures serialized tokens and rejects truncation before inference.
The local `laya-jev:pr3` image was rebuilt with that validation.

## Implementation validation

- Native build and WebAssembly build passed.
- 19 Python resolver/session/idle-connection tests passed.
- Two MCP native-socket/browser-bridge transport tests passed; tool name audit
  and updated MCP protocol/authoring smoke tests passed.
- Five Laya service/token-budget tests passed.
- Native live UI checks passed: atomic prototype and scene edits, staged graph
  pin edits, grouped Undo/Redo, copy references, stale-state rejection, invalid
  batches, dry runs, idempotency, cancellation, Cmd+K, Escape, and real Laya
  application with field readback.
- Playback requests captured from another scene tab are rejected even when
  the tabs share a display label.
- Browser interaction verified the two-color review, Apply, follow-up context,
  idle reconnection, guarded Undo, keyboard clarification, and Escape.
  Processing time excludes human review.
- The final live API smoke test passed 12 command workflows across LayaLab and
  LayoutLab, with exact operation checks and readbacks for batches, follow-ups,
  selected groups, duplicates, and navigation. It also checked cancellation,
  unsupported-request abstention, and playback. Changes were undone and never
  saved. `live-results.json` records its processing times and review decisions.
- Scene-document tests, whole-function audit, and a three-frame headless run
  passed separately after the full suite stopped.

The independent LayoutLab browser smoke test found a wrong automatic edit that
the frozen catalog did not catch: the omitted subject in “width to 80 and height
to 50” could resolve against the current selection. The second write reached
the camera height. It was unsaved test state. Clause inheritance now explicitly
links the second operation to the first target, including after fuzzy target
resolution. A regression with a competing selection and exact live-operation
assertions was added. Zero wrong automatic proposals in the frozen benchmark
must not be read as zero defects found during implementation.

Final navigation checking also found that the initial scene's temporary tab
label could obscure its asset name. Object lookup now considers observed asset
filenames as well as display names, while keeping entity names independent of
their prototype filenames. The live test verifies the final scene identity and
its entity contents, not just an `applied` status.

The full `make test` run is **not passing**. Its main runtime suite reports:

- `test/graph/input_bindings.c:191`: a type-mismatched active binding reports an error.
- `test/graph/input_bindings.c:805`: runtime rejects an incompatible whole-component input.

Those assertions interact with conversion changes already present in the dirty
checkout's `src/graph/runtime/eval.c`. That file was not changed for this feature;
a clean baseline run has not established whether those failures predate it.
The unrelated conversion work was preserved. Do not describe the entire test
suite as green.

## Next evaluation work

Collect corrected requests through **Details → Report incorrect result** and
add a fresh evaluation partition before tuning further. Expand the catalog
fixtures to match full live object/property competition. Prioritize pronoun
follow-ups, competing color fields, typo names, and unseen reflected fields.
Keep the 80% automatic-coverage target and zero observed wrong-automatic target
visible together; increasing coverage by accepting known disagreements would
hide the remaining accuracy problem.

General text generation, new graph design, and automatic desktop fallback are
outside this resolver. Existing computer-use tools remain available to a
supervisor. The live API exposes the same observations, replies, and typed
execution path for a future planner without putting that planner in every fast
request.
