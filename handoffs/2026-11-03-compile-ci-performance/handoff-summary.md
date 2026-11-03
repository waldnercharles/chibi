# Handoff summary

Checkpoint date: 2026-11-03. Work stopped at the user's instruction. No implementation, benchmark, publication, new dispatch or hosted polling belongs to this checkpoint operation.

## Source and coordination

Owned clone: `/Users/cwaldner/Personal/chibi/.coordination/compile-ci-costs-20261102/chibi`.
Evidence: sibling `evidence/` directory. The user's primary checkout was not edited.

The first parent is `bc6d971d33be36a8d7f8dd32b86253d9fe7f46e5`, tree `1b9aac2eec287c938ff6d1fcf38b8221e9a9448f`. Compiler tree `f840f27b0bc95ded6c92e699bbf9145b10810cd9`, core tree `a48b8dbadc3257a18cc5a3aceb7ca6cfe97c6788`. The source checkout was clean before the checkpoint.

Coordinating chat: `01a199ec-f55f-7cc0-ab24-fe521b2bbe54`, host `local`. Its integration checkout is `/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi`. The coordinator owns final integration and Windows consumer freeze/dispatch. Source evolution there continued after the frozen compiler used by this lane. Reconcile exact heads before resuming; do not edit that checkout or treat this archive as the latest integrated source.

Original handoff named main `b46f391b`, draft PR1330 head `2fb44239`, and draft PR1331 head `13e01f905`. Those are historical handoff values, not verified current heads. Native-default bootstrap selection, content-keyed build-entry reuse and the isolated per-test native runner already existed. They must not be implemented a second time. LLVM22+ is required.

## Completed changes and evidence

| Work | Local commit | Integration or validation |
| --- | --- | --- |
| Mandatory independent Driver and Graph command contracts in native self-host | `4e5539676fb9d085b7bec26237114a45df9f9254` | Coordinator integrated as `d3a829bec70584e0833600d1c45968ea6a113c06`; all five changed file bytes matched. Actual native proof, raw fixed point, Windows cross output, command contract, diagnostics and attributes passed. Forty infrastructure tests passed. Historical VM comparator remains opt-in. |
| Reuse the successful Release native compiler through later Linux core commands | `bc6d971d33be36a8d7f8dd32b86253d9fe7f46e5` | Coordinator integrated as `56b3c0368176da3ccf7d5206fc643aa8f383645a`; all three changed files matched. Eleven focused regressions, all51 infrastructure tests and the real existing Graph selector passed. Cold core old-seed compiler constructions fall from three to one by source count; complete CI saving is unmeasured. |
| Temporary Linux-only Windows artifact producer | `36506138dab6e97ccd9ebc99cd36e8b5eb96dcb3`, tree `e389beee99626474142328773f28364ca5859ca8` | Reviewed, pushed and manually dispatched. Branch `codex/native-windows-producer-20261103`. It is an artifact producer, not a merge-ready second normal workflow. The ordinary one-workflow assertion remains unchanged. |
| Windows compiler/CI corrections | `9eb7c7f013a595ec6fe9eda60dd6ff54d44873e3` | Integrated as `c258d33...`; local/infrastructure review passed. Real Windows acceptance remains separate. |
| Pinned manual Windows dispatch route | `add9df19a1342f2c0347bb38b5ee4f78a0de3e5a` | Integrated as `2ec95de...`; observed queue/dependency latency86m10s to4s is a scheduling observation, not compiler speed. |
| Demand-based pointer construction | `f11df26379b2250ab34d07fda140acfc546de353`, draft PR1337 | Repeated constructor growth was bounded; original N=1/2/4/8 row counts261/810/6588/300s timeout became174 at every size. Separate correctness validation passed. Controlled compiler/edit performance comparison remains held. |
| Read-only type-table access and primitive projection borrowing | Retained tips in `source-refs.json` | Earlier controlled windows found10.1%/11.2% type-table gains and projection compiler37.823s to29.740s,21.37%, with429 tests12.255s to11.260s,8.12%. These are separate frozen epochs, not composed-head CI results. |

Each production change has its own review and evidence boundary. Archive parents also retain earlier experiments, controls and superseded branches. Their presence is not an integration recommendation. See `compiler-performance-status.md`, `compiler-comparison/comparison.md` and `compiler-comparison/follow-through-20261103.md` in the evidence archive.

## Measured costs

| Workload | Wall time | Summed process CPU | Limit |
| --- | ---: | ---: | --- |
| Frozen4e native self-host, Windows cross build and real command contract |73.59s|308.97s|Stages2/3 physically equal; coarse21/22/20s native stages. Mac, shared load. |
| Frozen diagnostic probe compilation/link |51.703s|90.12s|99.434% of this build-plus-execute sample. |
| Same probe's23 scenarios |0.294s|Below reported precision|Compilation dominates this sample. |
| Existing emit-only path |11.100s|10.94s|81,611,164 bytes in55 pieces; it joins a bundle and skips some tool validation, so it is not an exact regular-build prefix. |
| One4.68MB module, O2 optimization |3.544s|3.51s|SROA0.660CPU seconds; one module, not whole-pipeline attribution. |
| Same module, object generation |4.386s|4.35s|Register allocation1.507CPU seconds and coalescing0.913CPU seconds. |
| Existing Graph selector using the successful current compiler |35.46s|130.92s|Includes compiler/entry preparation and actual contract; no C fallback. |

Do not add overlapping selectors or sum these distinct workloads into a CI total. Process CPU is not summed hosted runner work. The original verified PR1330 baseline was30m18s wall, Linux core23m47s, native proof4m13s and macOS core18m19s in parallel. Both C caches hit100%. Largest Linux selectors overlapped. Later historical complete runs42m22s to43m19s wall and64m39s to68m20s runner work did not show a CI improvement. A matched exact-final-head comparison remains outstanding.

The diagnostic closure is98 files/3.42MB despite the assembled39,312-byte probe. Retained output contains341,749 allocas,486,758 loads and422,376 stores. The largest `Lower` function has16,901 allocas. All732 repeated definition groups have identical bodies, adding3.72MB, but most are live internal per-module helpers. Conservatively unused helpers account for only0.295% of generated text. No material saving was established for removing those helpers.

The bounded next emission proposal was to use eligible primitive literals directly in adjacent comparisons. There are9,076 apparent shapes in retained IR. Existing validation already knows unique producers and dominance. No production emitter change was made. Re-establish semantic eligibility and ownership before any patch; active coordinator lanes own overlapping Emit/Snapshot/ValueEquality work.

## Prior art and engineering review

Zig, Odin, Rust and Goose remain the pinned primary-source comparisons. Zig's candidate reuse plus independent bootstrap/fixed-point proof informed the core reuse change. Odin's resolved compiler state and existing LLVM concurrency argue for reusing known facts before replacing execution machinery. Rust's single-definition/dominance analysis informs the small scalar emission proposal. Goose uses generated C and TinyCC, so it is not a matched LLVM benchmark and does not justify another backend. Exact pinned links and qualifications are in the comparison reports.

The repository AGENTS and `.claude/skills/chibi-engineering-review/SKILL.md` were followed for the implementation work. Briefs counted transformations, actual calls and growth before changes. Regression evidence, ordered review passes and independent review receipts are retained. No new cache, backend or incremental framework was justified by the later LLVM investigation.

## In progress at the stop

Hosted producer [41627603178](https://github.com/waldnercharles/chibi/actions/runs/41627603178), job125888813580, `Native self-host (Linux)`, exact head `36506138dab6e97ccd9ebc99cd36e8b5eb96dcb3`. The last saved snapshot, `2026-11-03T12:35:24.273Z`, says `in_progress` in fixed-point/cross-build proof. No fresh poll was made for this checkpoint. Diagnostic, attribute, final-source and artifact steps were still pending in that saved snapshot. No artifact was accepted or PE executed by this lane.

Read-only historical bootstrap bridge planning had been requested. No bridge was selected or executed. The known published-c5 Mac to frozen `72598f131af1e008db34da1270ed1f35c26c0d17` receipt had1194.70s first construction and later29.34/20.11s stages with stage3=4. This does not prove a Linux bridge. Actual14f lineage came from an earlier command-fixture Linux builder, not directly from c5. Exact SHA/source lineage and every final gate remain required.

The coordinator reported14f publication approval, followed by this checkpoint-only instruction. Publication is pending execution; this lane did not publish or advance a pin.

The original local published-c5 Linux admission ended incomplete at its unchanged90-minute bound, exit124. Controller construction passed8.394s; whole compiler construction emitted no module/object/executable. All3485 sources and seed bytes remained unchanged, and only the owned container was removed. The delay is before the external object pipeline, but its earlier compiler subphase is unknown. Rosetta/shared-host results do not predict hosted Linux time or prove unsupported source.

## Holds and resumption constraints

Controlled constructor/compiler/test/edit A/B work remains HOLD pending the coordinator's agreed quiet window. No stop or resource change for another owner's process/service is authorized. Respect the historical prepared-target/full-mode ABI hold and the `managed_values/Audit.ll` invalid-index callback hold. Do not execute those fixtures. Do not copy held legacy DriverTests wholesale. Preserve process isolation, inventory, assertions, diagnostics, sanitizer instrumentation, existing deadlines, raw fixed-point equality and both command obligations.

Heavy work must use `sh /Users/cwaldner/Personal/chibi/handoffs/heavy.sh 'clear label' COMMAND...`. Use explicit LLVM22+ and frozen source/tool hashes. For Git review use `git --no-pager diff --no-ext-diff --no-textconv` because configured external diffs previously returned stale output. No user primary checkout changes, global Docker/resource changes or publication/pin changes belong to this checkpoint.
