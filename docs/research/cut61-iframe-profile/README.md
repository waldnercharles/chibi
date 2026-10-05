# CUT-61 initial causal profile

The post-context holder traversal repeats, but its cost is small in the existing
`--kit-documents` fixture: **0.285–0.472 ms across 49 frames**, or **0.21–0.34%**
of the measured pre/context/post phases. This does not justify a behavior-changing
skip experiment on this evidence alone. No optimization was applied and no
speedup or safe removal is claimed. Return this checkpoint for parent steering
before choosing a larger representative CPU fixture or native UI acceptance.

## Source and environment

- Main verified against `origin/main`: `7b35b66da8fbaf94012e18ed78cfd26e5ee09f2a`.
- Measured source: `6bedae240471c7436cf5af67874436dd901360b4` on
  `codex/cut61-iframe-profile`, isolated at `/workspace/cut61-iframe-profile`.
  Instrumentation commit: `4427228c`. The original `/workspace/c-blueprints`
  checkout and its build were left untouched.
- RmlUi: `ba95ffe8bfb6370efb2cdcca927eaad4710c5413` (6.3), with the normal
  main-branch patches listed in `cmake/rmlui_dependencies.cmake`. Binary and
  dependency patch-diff SHA-256 values are in `results.json`.
- Linux x86_64, KVM, AMD EPYC 9V74, five visible virtual CPUs. Clang 19.1.7,
  Debug, ASan+UBSan, assertions/leak checks retained. Four build jobs.
  See `build-settings.txt` and `build.log`.
- Existing `/workspace/.cloud-tools/activate.sh` only. No PR170 setup,
  SDK download retry, persistent settings change, or publication. The missing
  commit-hook formatter was installed only in `/tmp/cut61-format` (clang-format
  22.1.0); the existing hook ran normally using `CLANG_FORMAT` for the commit.
- Discovery reported no graphics device nodes and only Vulkan CPU llvmpipe;
  see `capabilities.json`. This was discovery, not a rendered workload.

## Exact commands

From the isolated worktree:

```sh
source /workspace/.cloud-tools/activate.sh
python3 tools/agent.py doctor
make agent-list
make build JOBS=4 BUILD_TARGET=test_suite
python3 docs/research/cut61-iframe-profile/run.py
```

`run.py` executes exactly five sequential copies of:

```sh
CHIBI_EXECUTION_PROFILE=hardware CHIBI_ALLOW_FULL_GPU=1 CHIBI_IFRAME_PROFILE=1 build/test_suite --kit-documents
```

Each subprocess has a **180-second timeout**. The explicit profile is needed
because this selector is absent from `test/execution_selectors.h`; it does not
identify the hardware. No unbounded/full suite is launched. The selector dispatch
in `test/test_suite.c` calls only `test_kit_documents`. Its backend creates RmlUi,
loads fonts/theme, and does CPU update/layout/hit-testing; the fixture never calls
render or creates a Cute app/window. Runtime counters independently report
**49 context updates, zero render captures, zero render passes, zero geometry
compiles** in every run.

A sixth, timeout-bounded run with `CHIBI_IFRAME_PROFILE=0` passed all 10 existing
checks and emitted no profile rows (`profile-disabled.log`). It is a functional
opt-out check, not before/after performance evidence. The first clean build's
link overlapped instrumentation edits and failed with unresolved new symbols;
a normal incremental build resolved it. Final measured builds succeeded. There
was no test timeout, assertion removal, sanitizer suppression, or SDK failure.

## Measured counts and timings

The fixture and all 49 frames are unchanged: initial layout, independent text
changes, same-frame resize, popup/focus/nested dismissal, inspection, and normal
document open/close. All **10 checks passed** in each run. Counts below were
identical across all five final profiles.

| Phase | Calls | Holder updates | Probe OnUpdate | Probe OnLayout | Changed boxes | Changed heights | Settle passes |
|---|---:|---:|---:|---:|---:|---:|---:|
| Pre-context | 49 | 49 | 55 | 20 | 5 | 1 | 52 |
| Context | 49 | 0 | 196 | 18 | 0 | 0 | 0 |
| Post-context | 49 | 49 | 49 | 0 | 0 | 0 | 49 |

`boxes`/`heights` count successful iframe `take_box`/`take_height` changes, not
all element changes inside Context. The layout column counts actual custom
`OnLayout` callbacks, not calls to `UpdateDocument` or global layout passes.
Four probe-bearing documents are observed: index 0 is the holder/page; 1, 2, 3
are A, B, and the fitted bar. Popups/layer and the temporarily opened C document
have no probe. The probe's update hook only counts calls; it does not exercise
mutating custom-hook semantics.

Times are milliseconds, summed across the full 49-frame run:

| Run | Pre-context | Context | Post-context | Post holder subset | Whole process |
|---|---:|---:|---:|---:|---:|
| 1 | 14.509 | 123.770 | 0.783 | 0.472 | 490.454 |
| 2 | 13.403 | 123.098 | 0.669 | 0.359 | 475.579 |
| 3 | 13.871 | 123.787 | 0.576 | 0.293 | 476.396 |
| 4 | 13.457 | 116.360 | 0.581 | 0.296 | 468.099 |
| 5 | 13.735 | 116.049 | 0.563 | 0.285 | 477.371 |

The median post-holder subtotal is 0.296 ms, about 6.0 microseconds per frame.
Pre-holder subtotals range 2.297–2.731 ms. The slowest context sample is frame 7
(first menu opening), 19.410–20.496 ms; it is retained in the raw logs. No samples
or warmup frames were discarded. The preliminary five runs also found a small
post-holder subtotal (0.309–0.457 ms); final evidence above uses the committed,
formatted source and adds renderer counters.

`CLOCK_MONOTONIC` records phase start timestamps and elapsed times. Holder
elapsed time is nested inside settle elapsed time; do not add both. JSON output
happens after all three phase timers stop. Timers/probe overhead is included,
not subtracted or calibrated, and Debug/sanitizer timings are not release-mode
estimates. Whole-process timings include startup, test work outside these phases,
logging, and teardown. `results.json`'s `stable` group means only no iframe box/
height changes and no callbacks in the four **probed** documents; it is not proof
that the unprobed popup/layer did no layout or animation work.

The five `profile-*.log.gz` files retain every timestamp, per-document callback
count, assertion result, and runtime backend counter. `results.json` contains
aggregates, variation, exact source hashes, and process times. The run script
regenerates them from the current committed instrumented source.

## Causal interpretation and remaining work

`src/ui/rml_backend.c:rml_backend_update` executes pre-settle, Context update,
and post-settle. `src/ui/iframe.c:lay_out_holders` calls each distinct holder's
`rml_document_update` once per settle. At the pinned RmlUi source:

- `Source/Core/ElementDocument.cpp:467`: `UpdateDocument` calls `Update`, then
  dirty-guarded layout/position.
- `Source/Core/Element.cpp:120`: `Update` calls `OnUpdate`, animation/scroll/style
  processing, and recursively visits children.
- `Source/Core/Context.cpp:205`: Context already traverses the root, then lays
  out/positions documents; line 217 releases unloaded documents.

The holder probe is called again in all 49 post phases, although its box and
layout do not change. This establishes repeated callback traversal in this
fixture. It does not establish that callbacks or styles are generally redundant,
count every internal style operation, or demonstrate a meaningful iteration
speedup. The dominant measured bucket is Context update; its internal cause
needs a separate profile before proposing another optimization.

Before any skip experiment, retain every `SETTLE_PASSES` iteration and geometry
propagation and add focused regressions for:

1. A later document's `OnUpdate` dirtying an already visited holder, with immediate
   hosted resize and fit-height/following-sibling propagation assertions.
2. Custom-hook side effects and two-stage animation/text measurement. Existing
   `src/ui/text_motion.c:777` and `:910` stage morph measurement across updates;
   `src/ui/drag.c:383` uses an invocation count for starting styles. These are
   semantic risks, not demonstrated failures of this unimplemented optimization.
3. Popup movement/focus and document destruction during callbacks. Context may
   destroy a document before post-settle; preserve observer resolution and drop
   behavior, and never reuse raw pre-settle holder pointers.
4. Repeated/interrupted lifecycle and animation behavior at equal coverage.

No experimental optimization or new semantic regressions were added because the
specified initial fixture did not show material avoidable cost. `workspace_layout`,
`area_documents`, native UI acceptance, web build, GPU/Studio suites, and full
verification remain **not run**. Source review and Fedora render acceptance are
separately coordinated. Nothing is merged, pushed, or published.

## Representative capture preparation

The later, separately authorized native capture preparation is documented in
[`native-preparation.md`](native-preparation.md). It adds an exact lifecycle
selector and opt-in context/outer-frame reporting, without a runtime optimization
or RmlUi internal patch. The initial measurements above remain tied to their
original source and artifacts. Branch publication is now authorized for Fedora
verification; native execution remains separately scheduled.
