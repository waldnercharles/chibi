# Studio UI and interaction QA

This pass preserves Studio's C/RmlUi implementation, shared kit controls, and
compact authoring layout. The requested design skills informed the audit. The
editor's own UI and graph contracts governed the implementation.

The requested [Laws of UX guide](https://raw.githubusercontent.com/keysjoao/laws-of-ux-skills/refs/heads/main/laws-of-ux/references/ux-laws-complete.md)
informed this audit. All 30 laws are included directly in the generic
[UX laws skill](https://github.com/waldnercharles/chibi-studio/blob/main/.claude/skills/ux-laws/SKILL.md), now in chibi-studio.
The table below records how those principles affected concrete decisions.

## Changes

| Observed problem | Result | UX guidance |
| --- | --- | --- |
| Picking up a connected graph input deleted its edge before release. Escape lost the connection, and reconnecting needed two Undo actions. | Pickup is a preview. Escape, incompatible drops, and the original socket preserve the graph. Reconnect or empty-canvas disconnect commits once and undoes once. A source changed by another editor is rejected. | Mental Model, Jakob's Law, Working Memory |
| Thin wires were harder to select than the documented interaction target. | Wire picking uses a 20 px target; drag and double-click thresholds match the graph contract. | Fitts's Law |
| Save availability could describe the scene while Save acted on the focused behavior. | Command availability follows the same source, UI, graph, and scene ownership as Save. | Mental Model, Jakob's Law |
| Reload discarded unsaved edits immediately, could steal tab focus, and could reuse stale source overlays. | Unsaved reload requires the existing toast action. The request retains its original path across tab changes. Failure preserves the live code editor, exact applied preview, deferred draft, history, cursor, and checkpoint. Success removes discarded source state. Background scene state is cleared. | Jakob's Law, Working Memory |
| Opening another document from an incompatible maximized pane left the old content covering it. | Document transitions reveal the relevant Canvas, graph, or Text Editor. Initial layouts, a maximized prototype inspector, normal splits, deliberate maximize, and followed collaborator layouts are preserved. | Mental Model, Selective Attention |
| UI preview content stretched after a document or device-size change until later interaction. | A post-layout size mismatch requests a follow-up emission so preview texture and display dimensions converge without input. Settled idle scheduling remains bounded. | Doherty Threshold, Aesthetic-Usability Effect |
| Canvas/source selection could be hidden by a collapsed Hierarchy or excluding filter. | Selection opens its ancestors and reveals its row. Only an excluding filter is cleared. | Jakob's Law, Working Memory |
| Reusable UI widgets could show a zero-width selection outline around a visible button. | Selection outlines, dimension labels, and drop placement include the rendered widget roots. | Mental Model, Aesthetic-Usability Effect |
| Canvas selection guides covered controls in Settings. | Authoring guides use a shared layer below dialogs and popups. The UI Inspector retains its ability to highlight dialog controls. | Selective Attention, Common Region |
| Empty scenes gave little guidance, and Frame looked available without a selection. | The Outliner explains how to add a prototype. Frame shows its unavailable state. | Proximity, Mental Model |
| Numeric Page keys and keypad Enter were inconsistent across controls. | PageUp/PageDown move ten steps; keypad Enter activates buttons, checks, and menu items. Locking a number cancels its draft. | Jakob's Law, Postel's Law |
| Compact graph source fields clipped text vertically. | Graph inputs use padding that fits their smaller inner box. Checked in dark/light themes and both densities. | Similarity, Aesthetic-Usability Effect |
| Maximized Properties spread the label column across 38% of the window, separating fields from component headings. | The shared property split caps labels at 160 dp, scaled with the standard font. Narrow panes retain their proportional split. | Proximity, Common Region |
| Larger fonts made graph breadcrumbs and source toolbar controls overlap. | Header actions retain their width; document names shrink with ellipsis. | Fitts's Law, Similarity |
| Function parameter names disappeared in narrow Properties panes. | Signature fields wrap while retaining an editable name field. | Proximity, Working Memory |
| KDL source exposed floating-point representation noise such as `0.20000000000000001`. | Serialization uses concise decimals that preserve the original scalar on reload. Native float values no longer gain digits when promoted to doubles. | Mental Model, Aesthetic-Usability Effect |
| Every Assets emission rescanned references, filtered and sorted all assets, and allocated temporary lists. | Usage counts and sorted IDs follow the actual index revision. Filter changes rebuild rows only; unchanged emissions reuse the model. Explicit index refresh wakes the view. | Doherty Threshold |
| Closed graph asset pickers still enumerated the entire catalog. | Catalog work occurs when the picker opens. | Doherty Threshold |
| Minified web HTML silently skipped local workspace injection and opened bundled content. | The server recognizes executable quoted or unquoted serial, threaded, and plain launch scripts, skips inert templates, and fails clearly when no launch script exists. | Mental Model, Postel's Law |

## Interaction coverage

Browser checks used real keyboard and pointer input in Chromium with the Metal
WebGL backend on an Apple M4 Pro. The workspace was copied to an isolated
temporary directory before authoring tests. SDL regressions exercise native
rendered controls and inspect document/model results.

| Area | Exercised workflows |
| --- | --- |
| Shell and commands | Menus, palette search and prefixes, keyboard activation and cancellation, settings search, command context, Save ownership, tab switching, close/reload handling, global shortcuts while fields retain editing keys |
| Layout and preferences | Horizontal/vertical splits, maximize/restore, drawer switching, document reveal, side-pane state, dark/light themes, compact/comfortable density, narrow 1024×768 and wide 1440×1000 windows, UI scaling through 200%, font changes through 18 dp, shortcut recording and cancellation |
| Assets and scenes | Search and no results, kind/group filtering, virtual rows, explicit index refresh, new-asset menus, invalid-name feedback, scene creation, add prototype, object rename, Frame, delete and Undo, save to the mirrored workspace |
| Graph and Properties | Node drag and cached geometry, wire pickup/cancel/reconnect/disconnect/stale source/Undo, numeric and source autocomplete edits, expression drafts, folded sources, typed vectors, enum search, colors and alpha, nested collections, function parameter name typing/cancellation and container menus, Undo/Redo and serialized replay |
| Canvas and source | Fit/Phone/Desktop preview sizing, selection through collapsed and filtered Hierarchy, source/canvas selection synchronization, RML/RCSS and KDL editors, live/deferred edits, failed and successful reload, checkpoint-only drafts, source navigation and code intelligence |
| Playback and diagnostics | Play, Pause, Step, Stop, focused game input, Output tabs, Performance history, bounded idle/playing inspection and retained intermediate telemetry |
| Collaboration | Dialog and cancellation in browser; follow/stop/leave/request failure states in rendered fixtures; action serialization, stale-action rejection, atomic reconnect replay, and Undo in native tests |

The source audit also followed the graph gesture, breakpoint/watch, navigation,
asset refactor, designer drag/drop, and collaboration command paths. Source
inspection is not counted as an independently exercised browser interaction.
No claim is made that every possible state combination or input device has been
tested. Real multi-user sessions, Safari/Firefox, touch/pen input, and platform
accessibility APIs were outside the executed matrix. This pass does not establish
feature parity with Unreal.

## Performance

The [portable Assets benchmark](performance/README.md) compares baseline commit
`29a0e24f5e057223421018f82042f568ebfec057` with this implementation. Three recorded
runs measure 10,000-asset filter preparation at **1.754 ms → 0.178 ms**, about
**9.9× faster**. Reference-index reads fall from 409,600 to 10,000. Unchanged
emissions fall from 409,513 reads and 23 allocations to zero of each.

This is a model-preparation benchmark, not a whole-editor speedup. Full index
rebuild work remains necessary. The benchmark includes correctness assertions,
raw results, compiler details, and its source hash.

`browser-sample.js` (removed on 2026-10-20; Git keeps it) recorded settled editor CPU
zones, frame intervals, and retained-render counters from a profiling build.
It cleared trace entries before and after each bounded sample. CPU submission
measurements must not be interpreted as GPU execution time.

The [rebuilt-editor samples](performance/browser-results.md) recorded 596 frames
during ten seconds of continuous graph panning, with **1.6 ms p95 editor work**
and **18 ms p95 frame intervals**. UI preview also measured 1.6 ms p95 editor
work. Sandbox playback measured 9.5 ms, including viewport work, and had a
68 ms maximum frame interval. Raw results retain that outlier and distinguish
idle scheduling from active frame pacing.

These frame samples preceded the final alignment and float-serialization
follow-up; their source fingerprint is retained with the environment record.
The [serialization checks and timings](performance/float-serialization.md)
record an added median 0.013 ms per Sandbox write and 0.001 ms per Movement
write. These are formatting measurements, excluding disk I/O.

## Screenshots

- [Canvas, source, selection bounds, and Hierarchy](screenshots/canvas-source.png)
- [Dark compact graph](screenshots/graph-dark.png) and [light comfortable graph](screenshots/graph-light.png)
- [Settings with selection guides correctly behind the dialog](screenshots/settings.png)
- [Reload confirmation after switching documents](screenshots/reload-confirmation.png)
- [Prototype opened in maximized Properties](screenshots/prototype-properties.png)
- Properties label alignment: [before](screenshots/properties-alignment-before.png), [after](screenshots/properties-alignment-after.png), and [18 dp text](screenshots/properties-large-font.png)
- Large-font headers and function fields: [before](screenshots/function-fields-before.png), [after](screenshots/function-fields-after.png), [260 px pane](screenshots/function-fields-narrow.png), and [light compact](screenshots/function-fields-light.png)
- [Concise float values in the source editor](screenshots/concise-float-source.png)

The alignment follow-up checked normal and maximized Properties, shared field
edges and baselines, icon alignment, graph breadcrumbs, source actions, and
function signatures. Browser checks covered 13 and 18 dp standard fonts,
compact and comfortable density, dark and light themes, and a side pane narrowed
to approximately 260 px. Function names remained editable; Escape restored the
draft, and the container menu stayed anchored and usable.

## Validation

| Check | Result |
| --- | --- |
| `make build` (`test_suite`, `chibi`, `chibi_ctl`) | Passed |
| `make test` on the final source | Passed, including canonical assets, generation/reflection, reload, MCP, web packaging/storage/isolation, collaboration client, engine tests, and whole-function audit |
| `test_suite --studio-documents` | 228 rendered checks passed, including the graph input suite |
| `test_suite --studio-properties` | All five isolated Properties checks passed |
| `test_suite --editor-graph` | 139 checks passed, including data/exec reconnect replay and atomic Undo |
| `test_suite --ui-designer` | 39 checks passed, including expanded-widget bounds |
| `test_suite --rmlui-render` | GPU pixel checks passed, including guide/dialog/Inspector ordering |
| `make web` with profiling ON and OFF | Serial and worker builds, backend checks, and paired packaging passed; final build has profiling OFF |
| `make check-format` and `git diff --check` | Passed |
| Numeric serialization | Adjacent values, subnormals, extremes, signed zero, concise decimals, cloning, and 19,951 sampled native numeric round trips passed |
| Canonical numeric migration | 102 KDL files reformatted; all 353 changed native float values preserve their original bits; no unexpected value changes |
| Chromium browser QA | Worker and explicit serial startup, playback controls, and the recorded interaction matrix passed; no captured page exceptions |

Native validation used Debug builds with AddressSanitizer, UndefinedBehaviorSanitizer,
warnings, and ccache enabled; no sanitizer failures were reported. This Mac used
Clang 23.1.1 and the macOS 26.5 SDK. The local CMake override was
`-DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk`.

The browser [startup and error record](performance/browser-smoke.json),
[timings](performance/browser-results.md), and screenshots retain the evidence
separately from the source audit. Authoring checks used the temporary workspace;
authoring tests did not change the repository's game content. Checked-in KDL
assets were subsequently reformatted for concise numeric spelling, with value
preservation checked separately.
