# Direct Laya editor commands — 2026-09-21

Historical report for the retired Laya resolver. See `local-report.md` for the current local model.

The handwritten interpreter, phrase grammar, legacy automatic confidence policy,
and alternate resolver mode have been removed. Cmd+K and the native worker now
use one direct Laya resolver. Historical interpreter measurements remain as
benchmark evidence, not as a production fallback.

## Current design

The editor supplies stable object IDs, component/property metadata, current
values and legal choices. Generic name retrieval narrows the observed controls;
Laya receives the original request and chooses a complete action set and field
values in one batched call. No clause parser decides which value belongs to which
field. Existing native revision checks, typed validation, atomic application,
readback and Undo remain authoritative. All language proposals require review.

The early direct loop asked Laya which action to do next and whether to finish.
It often stopped too soon or added unrelated edits. The current implementation
presents complete combinations of up to three relevant actions. Other improvements:

- Verified follow-up targets take precedence over current selection.
- Longer metadata names win over shorter names at the same text span.
- Component labels disambiguate duplicate names such as appearance color versus
  particle color, without particle-specific command rules.
- Invalid requested values remain recognizable choices and are rejected if
  selected, instead of disappearing and redirecting the model to another value.
- Native constraints remove impossible combinations of navigation/editor controls.
- Literal text can request a typed generative handoff; no generative provider is
  currently configured or called.

## Before/after direct comparison

Both versions were tested on the same 46 regression requests plus 12 additional
requests, using the same frozen catalog and local CPU Laya checkpoint. There are
50 supported requests and eight expected abstentions. The older run alternated
with the now-retired interpreter; the final direct run was sequential. These
are before/after observations, not interleaved or repeated performance trials.

| Measure | Earlier direct loop | Current direct resolver |
|---|---:|---:|
| Correct supported proposals | 15/50 | 36/50 |
| Correct existing regression proposals | 12/40 | 29/40 |
| Correct additional proposals | 3/10 | 7/10 |
| Wrong nonempty proposals, all 58 cases | 13 | 9 |
| Expected-abstention cases with operations | 1/8 | 2/8 |
| Median resolution time | 2,182 ms | 733 ms |
| p95 resolution time | 3,606 ms | 1,965 ms |
| Automatic execution | Disabled | Disabled |

Timing includes Python resolution and local HTTP/model inference. It excludes
editor application, rendering and review time. The model reports CPU execution
with ModernBERT-large. Raw entropy confidence is not a calibrated probability
of completing an editor request correctly.

The final component-disambiguation fix was verified to leave all 150 frozen
fixture requests' model packets and statuses byte-for-byte unchanged, and was
then tested against the richer live catalog. It therefore does not require a
new model benchmark on the smaller fixture catalog. Source hashes are recorded
in `direct-results.json` alongside expected/actual operations and timings.

These datasets have now been used for regression and investigation; neither
partition is an untouched holdout. Results support improvement on this workload,
not unrestricted editor autonomy. The retired interpreter resolved 44/50 in the
original paired run; it was removed because maintaining a custom interpreter was
not the requested architecture.

## Remaining errors

Some requests still produce incorrect arithmetic, miss a condition, or propose
only the supported portion of a larger unsupported request. For example, the
model proposed a rate edit from “set the rate and erase the project” and ignored
a visibility condition in another case. Such proposals are not applied without
review. Plain-English paraphrases whose field is absent from the retrieved
choices can also select an irrelevant action. These are measured limitations,
not reasons to reintroduce sentence-specific rules.

The next accuracy work should evaluate metadata/candidate recall separately from
model selection, expand full live-catalog regressions, and compare checkpoint
training or a small generative model on the same typed contract. Generated text
can use the existing handoff, but text generation alone will not fix target,
arithmetic or completeness errors. A future provider must be evaluated on total
request latency and complete-operation accuracy before automatic execution.

## Validation

- 14 focused Python resolver, identity, value and transport/session tests pass.
- Browser workspace/proxy/cancellation tests pass.
- Model inputs and abstention/clarification statuses were unchanged for all 150
  fixture cases after the final component-name fix.
- Live editor verification is recorded below. Native/WASM code was unchanged;
  the full native test suite was not rerun for this Python resolver change.

Full local inference traces: `.chibi/commands/direct-release.jsonl`.
Reproduce with `evaluate.py --split heldout --include-probes --output PATH`.

### Live editor result

On the full LayaLab catalog, the original request “Change the boost emitter
particle color to green, and the end color to purple” produced both correct
particle-field operations, excluding appearance color. Cmd+K resolution took
1,270 ms; processing including Apply was 1,290 ms, excluding human review.
Readback confirmed both requested values and unchanged appearance/other fields.
End color was already purple in this fixture, so one value actually changed.
The palette's Undo restored every original BoostEmitter field value. No asset
was saved. The updated direct-only editor is running on port 8114; earlier
editor tabs were left intact.
