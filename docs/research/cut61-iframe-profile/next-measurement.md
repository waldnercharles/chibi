# CUT-61 representative panel measurement proposal

Historical proposal saved at `6738e28b130b645a41023575705050a57d2eb9a8`.
Its present-tense scope statements describe that checkpoint, not the later
implementation. See [integration status](integration.md) for the current status.

The initial result is unchanged: the 49-frame `--kit-documents` fixture spends
0.21–0.34% of its measured pre/context/post time in post-context holder updates.
That fixture is not representative evidence for the reported approximately
305-second real Studio panels workload. No traversal removal or production
speedup follows from it. This follow-up is source inspection and a proposal;
no further native, GPU, or optimization experiment was run.

## Current instrumentation scope

Relative to main `7b35b66d`, the code diff is four files, 187 insertions and four
removed/replaced lines:

| File | Scope |
|---|---|
| `src/ui/iframe.c` (+62) | Disabled-by-default single-threaded profile state and monotonic clock; counts/times distinct holder updates; counts successful box/height changes and actual settle-pass executions. |
| `src/ui/iframe_profile.h` (+30) | Diagnostic enable/reset, phase begin/end, current phase, and sample accessors. |
| `src/ui/rml_backend.c` (+7) | Header plus three begin/end pairs around existing pre-settle, Context update, and post-settle calls. |
| `test/editor/kit_documents.c` (+88/-4) | Adds counting-only custom `OnUpdate` hook; shares the existing layout probe's document lookup; enables/reset/reports only when `CHIBI_IFRAME_PROFILE=1`; records backend renderer counters. |

No RmlUi dependency, test selector, assertion, frame count/order, settle limit,
style guard, geometry algorithm, or update call was changed. Timing helper calls
and enabled branches add overhead; the diagnostic branch is not a zero-overhead
production patch. Twelve initial evidence/report files accompany the four code files.
The measured source remains `6bedae240471c7436cf5af67874436dd901360b4`; the initial
profile and evidence were preserved at `0dfab2db`.

The same phase/holder/box/height/pass counters can measure an existing native
case without changing its sequence or semantics. They are **not yet wired** to
Studio test activation or output: setting `CHIBI_IFRAME_PROFILE=1` on a Studio
selector alone currently does nothing. A test-side driver must enable/reset and
read them around the existing application frames. It should add backend/context
identity because the current singleton aggregates every backend: the main Studio,
Behavior canvas, and any guest can update within one application frame. It must
also report retained frames that perform no backend update.

Current limits:

- `Context` is an inclusive bucket: model views, custom hooks/animations, style
  traversal, dirty layout/position, and document release. Existing counters cannot
  distinguish their time.
- Layout/update callback counts exist only on the kit fixture's four probes, not
  real Studio documents. Do not report missing probes as zero global layout.
- `kit_begin` may directly call `rml_context_update` on restyle
  (`src/editor/rmlui/kit.c:1113`), outside the three backend timers.
- Presence/popover/autofocus settling after post-settle, host model sync/emit,
  viewport work, Rml render/replay, and presentation are outside these timers.
  `rml_backend_frame_retained` also calls `ensure_gpu` before deciding to update.

## Recommended case and why native

Measure **one full `area_panels_take_turns(false)` invocation**, currently the
first check in `studio_documents_panel_checks` at
`test/editor/studio_documents_panels.c:1096` and `:1172`.

This existing case opens `projects/arena2d/content/scenes/Sandbox.chibi` with
`Movement`, mounts actual Studio panels, switches every directed pair of the
nine editors, maximizes/restores areas, changes drawer and side-pane contents,
opens/closes the HUD preview and palette/picker/refactor UI, and compares bound
view counts and warnings. Preserve all three rounds and all original assertions.

Source arithmetic gives **2,016 explicit application frames** (672 per round):
432 pair-switch frames, 81 maximize/restore frames, 126 workspace frames, three
Scene frames, 12 HUD preview frames, 14 palette/picker/refactor frames, three
Assets frames, and one data-view assertion frame. Initialization/teardown and
any internal Rml calls also stay in the recording. This is a case selection,
not a shorter pair sequence; no 80-versus-144 rewrite is involved.

On the inspected baseline there is no single-case selector: `--studio-panels`
runs both lifecycle variants and ten additional checks. Therefore **do not run
that broad selector as if it selected this one case**. Use the separately
coordinated focused selector if its exact source/ref lists this case; otherwise
add a test-only exact case entry pointing to the unchanged function. Keep the
existing suite entry and call order untouched, and reject unknown case names.
Do not invent a command spelling before the selector is verified.

Why not substitute an existing headless run: `--ui-bench headless` is indeed
headless (`test/test_suite.c:1440`), but its Properties fixture mounts
`spike/PropertiesSpike` (`test/editor/ui_bench.c:362`) in a separate single
document, with synthetic list/output workloads around it. It does not retain
Studio's actual panel models, iframe hierarchy, mount/unmount history, retained
scheduler, or canvas/preview work. A captured real DOM/model replay could later
isolate CPU costs, but constructing and proving its equivalence is separate
work; it would not certify this existing lifecycle case. The native fixture's
`frame_delta` calls `cf_app_update`, `studio_frame`, and presentation, and its
runner creates a hidden 1280x800 app (`studio_documents.c:77`, `:308`). Removing
those calls would create a different workload.

## Bounded capture specification, after the hardware lane is free

1. One isolated native process, one complete ordinary lifecycle case, existing
   1280x800 fixture, **180-second hard deadline**, four build jobs. Keep current
   sanitizer/assertion settings, original frame delta and all 2,016 frames.
   Preserve test preference isolation and all existing rendering. Record actual
   backend/device; an execution-profile name is authorization, not hardware proof.
   No timeout increase, automatic retry, full group, or companion Designer run.
   A timeout remains a failed/partial capture, never a passing shorter case.
2. Add only opt-in diagnostics. Record frame/case/round, editor transition, context
   identity, current document/element counts, and backend stats deltas. Time the
   existing outer `frame_delta` and unchanged host sync/emit/update/render/present
   boundaries, then report residual time rather than assuming all ~305 seconds
   is RmlUi. Include retained-frame counts and direct restyle Context updates.
   Existing Studio Tracy zone locations identify useful boundaries in
   `studio_host.c:1270–1319` and `studio_graph.c:592–609`; no Tracy availability or
   profiling build is assumed.
3. Reuse pre/context/post and holder counters. Add opt-in timer/counter hooks in
   the **pinned RmlUi source**, separately from optimization, to partition Context
   into model update (`Context.cpp:196`), root traversal (`:205`), document layout/
   position (`:207–214`), and unloaded-document release (`:217`). Count/time actual
   dirty layouts inside `ElementDocument::UpdateLayout`'s guard (`:479–490`), and
   count dirty position updates separately. For style work, time the nonrecursive
   `Element::UpdateProperties` call itself (`Element.cpp:158`) and count OnUpdate
   invocations; do not sum recursively inclusive `Element::Update` times. Style
   time is a subset of traversal, and layout/holder times are nested subsets, so
   present them without double-counting. Tag these hooks with the current context
   and pre/context/post/direct-update phase. This instrumentation is proposed,
   not present or validated on the branch. Existing upstream Tracy has dirty-layout
   zones but not a complete isolated style timer, so it alone does not supply all
   of these measurements.
4. Capture all samples, slow frames, warnings, exit status, source/dependency/binary
   hashes, build configuration, and actual test/frame counts. Emit buffered records
   after timed regions with a fixed-size per-frame aggregate (no per-element log);
   retain a bounded raw log, e.g. 10 MiB, treating overflow as a failed capture.
   Do not add synthetic warmups or discard existing lifecycle frames. Report one
   sample as one sample; repeated comparative runs require a separately admitted
   hardware slot and equal coverage.

This measurement would establish whether the real case's CPU cost lies in
settling, Context style/layout/model work, host emit/sync, or rendering/presentation.
It would still not prove a safe optimization. No runtime change or GPU execution
has been made for this proposal; native execution waits for parent coordination.
