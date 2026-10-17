# Private package membership and import-graph foundation

## Scope and status

Publication status, 2026-10-17: the bounded private implementation has been built and its finite product fixture passed, with independent terminal qualification described below. The initial source-only review statements are historical; frontend and runtime package activation remain later work.

This foundation was initially reviewed as a source-only draft against Chibi working tree `664c578cb79984fb049241727696bbf3d1ea8bd1` (numeric PR 430 head `b3bfbb828deae8737e1b122b4b3926bfd8eeeedb`, dependent on CUT `440b6445bb685fb7961167a6b1f4f8983e53ffc8`). The local donor HEAD/index remain `965a1c5e7f5ed4f908dbbf507323e524d6818fc6`; tree equivalence does not change that commit identity.

The new `src/authoring/source_packages.c/.h` is private, linked only into the existing `test_language` dispatcher for this initial slice. No normal parser, SDK export, catalog, runtime, asset, project version, or language behavior changes. At the initial source-review snapshot on 2026-10-17, no source application, configure, compile, link, product test, authored execution, commit, or publication had occurred. The later bounded validation below supersedes that historical execution status.

Implemented facts:

- Canonical resolved directory determines package identity, independently of aliases or declared spelling. Snapshot-local integer indices are never durable declaration identities.
- Complete member descriptors group sibling files; member filenames are ordered alphabetically. Nested directories remain distinct.
- Conflicting headers within a directory, duplicate files, and the same declared name in distinct directories fail with both input locations.
- Named aliases are file-local; default names use resolved directory basenames, following the existing ASCII Chibi identifier shape. Invalid basenames need an explicit alias. The literal blank alias creates no lookup binding.
- Every resolved import contributes an edge regardless of named lookup, including blank/unused imports. Repeated edges retain their first source import as diagnostic provenance.
- Self-imports preserve the same identity, edge and ordinary file-local binding. They neither create another package nor impose an inter-package ordering constraint.
- Dependency-correct deterministic ordering applies to inter-package edges, with canonical-directory tie breaking. Genuine multi-package cycles fail with a closed sequence of original import indices, including graphs that also contain self edges.
- Constructors publish only a complete graph or return a structured failure. A failed new graph cannot mutate a separately retained graph.

This follows the approved dependency-correct initialization contract. It deliberately does not promise literal reproduction of the pinned Odin priority queue's incidental mixed-self ordering. The separate source proof documents that qualification. No temporary mixed-self restriction or pending-order flag is exposed.

The companion syntax fixtures and self-import source proof retain the task-handle uncertainty from their earlier review snapshot. That uncertainty is superseded by preserving caller ownership when handles are stored in traced package state; ownerless init/fini task creation remains the later lifecycle choice.

## Existing integration seam

`source_catalog.c` owns a complete source snapshot and its arena. In `build`, after filesystem/overlay membership has been selected and sorted, before `declared_types` and `script_declarations_document`, future package-aware parsing supplies one `source_package_file` per member. Its canonical directory, declared header and real header range come from that member, never a synthetic concatenated source.

A small collection resolver must first resolve every import relative to its file or an explicit collection root, canonicalize paths, check allowed-root boundaries and retain the resolved directory in the same snapshot. Those results become `source_package_import` descriptors. This slice does not do that filesystem work or infer missing members. A missing target is an incomplete snapshot error, with no fallback into an older catalog.

The catalog owns descriptor arrays and all referenced strings while the private graph exists. The graph owns its package/member/index/edge/binding/order arrays; callers may inspect const views but must not mutate them. Destruction touches only owned arrays. Source diagnostics recover exact header/import ranges from original descriptor indices, including related declarations and every cycle edge.

After the parser and resolver exist, package scope collection must replace the current project-wide library exports. The graph's file-local binding lookup is only the import-name part of full lexical/file/package/builtin lookup. It does not check collisions with declarations or implement public/package/file privacy. `source_collection.c` currently derives dependencies from resolved expressions and links library files; its future package publication loop must consume these explicit package edges instead, retaining exact imported generations and invalidating affected packages.

Do not wire this into `source_catalog` or expose package syntax until those changes, complete snapshot preparation, nominal identities, and runtime ownership are ready. The current private dispatcher registration is an additive internal seam, not a usable half-migrated package feature.

## Finite owning-dispatcher workload

Selected finite route: `test_language --package-graph`, supervised through `tools/test_policy.py language-package-graph` after separate independent source/workload review and exact-candidate build admission. The patch registers the selector and existing 60-second production route. The admitted run below executed that route once. No separate linked probe executable, sanitizer expansion, aggregate expansion, parser, filesystem, process child, backend, world, frame, GPU, network or authored code is part of this fixture.

There are 23 pico_unit cases. If every case completes, there are exactly 28 graph constructor attempts: 16 successful expectations and 12 intentional refusals. At most two owners coexist, only for tiny retained-snapshot cases. The largest case has the existing 256-file/package ceiling and 255 imports, forming a fixed chain. Other cases use at most four files and four imports.

The largest graph requests under 280 KiB of its own heap arrays on the current 64-bit ABI; 320 KiB is the conservative own-storage review envelope. Its borrowed stack descriptors/strings and bounded scratch fit within 64 KiB. No recursive walk exists. Membership and topological scans are bounded by the square of 256; named bindings sort once and use binary search, avoiding quadratic duplicate-alias scanning. The source/catalog byte limits remain the upstream owner of import/text bounds; no new source-language import limit is introduced here.

Cases cover empty membership, cross-file membership/filename order, nested packages, header/name/file conflicts, already-resolved canonical identity, file-local aliases, resolved-basename aliases, invalid/underscore basenames, blank imports, duplicate aliases with both ranges, missing snapshot members, direct self-import, unused direct/indirect cycles, mixed self-plus-larger cycles, cycle witnesses without an acyclic tail, dependency-correct mixed-self order, discovery-order independence, rejected-candidate retention, snapshot isolation, and the maximum-file chain.

The companion `package-syntax-fixtures.md` contains approved-syntax frontend fixtures for later integration. These direct descriptor tests do not parse those examples, test real path canonicalization, or establish any runtime behavior.

## Remaining acceptance gates

- Required server checks, independent published-head review, and existing integration/retention holds remain in force. The bounded local source/application/build/runtime review is recorded below; configure/build timing is separate from the 60-second supervised test.
- Parser, generated artifacts, formatter, package attributes, source origins, complete-file discovery and import resolver/root containment.
- Package and file visibility, cross-file declaration collection/recursion, alias-independent nominal identities, full declaration/import collision rules, and ordinary source diagnostics.
- Package incremental invalidation, strict prepared-generation publication, navigation/refactoring and SDK complete-package preparation.
- One package-state owner per running world, preview isolation, startup init/shutdown fini, dependency then alphabetical filename then source ordering, reverse fini, compatible edit state preservation, and restart for membership/incompatible global changes.
- Future package storage must participate in task-GC tracing while preserving caller-owned task execution, await/cancel access and native owner context; storing a handle does not transfer those rights. Ownerless task creation during init/fini remains a separate future lifecycle choice. This graph slice reads or stores no task handles.
- Existing projects must migrate atomically when the contract activates; no version bump or asset migration is justified by this inactive internal slice alone.
- Authored enums and their backing ABI remain separate later work. Numeric frontend semantics stay held.

At the initial source-review snapshot on 2026-10-17, the evidence was limited to source inspection, clang-format 22 formatting/checks and read-only patch applicability. No product pass was claimed at that stage.


## Bounded validation, 2026-10-17

- Independent review reconstructed all nine initial postimages from the exact published numeric tree. Source, ownership, cycle witnesses, file-local bindings, integration scope and the finite 23-case workload passed review.
- The first strict build found missing-field initializer warnings in the fixture. The reviewed correction inserted only 39 explicit zero-valued initializer completions, including four default-alias `NULL` values. All assertion expressions and the 23-case list stayed byte-identical; warnings were not weakened. clang-format 22.1.1 verification passed.
- The corrected tested tree is `3ab636ece1854115a06bb4edc87234ccfa1c5985`. Its standalone `test_language` graph contains 367 C compilation units, 55 archives and one executable. Independent inspection qualified every actual dependency and the real allocator linkage, with no unknown inputs.
- The initial failed build log and all partial products were retained. The admitted continuation built only 13 remaining products, preserving 410 completed products, 773 completed product/depfile records and the original Ninja-log prefix. The continuation passed in 11.108 seconds; configure/build time is separate from runtime.
- The production-supervised `language-package-graph` run passed all 23 cases, one suite and 457 assertions in 0.076 seconds, below its unchanged 60-second ceiling. Before/test/after phases all exited zero; complete input checks and descendant cleanup passed.
- The outer evidence wrapper remains recorded as `STOPPED`: it copied a numeric-fixture summary regex expecting `Suites: 0`, while this registered `TEST_SUITE` correctly reports `Suites: 1`. Independent read-only terminal qualification checked the complete preserved log, exact ordered source registrations, corrected one-suite summary, all 23 metric rows and unchanged inventories. It qualified the product run as passed without rerunning it or rewriting the stopped wrapper result.
- The tested executable SHA-256 is `54d7e833205e6b540f8bb00238b26baff55c29b7d728623e20f297a73ed572d1`. The complete retained runtime log SHA-256 is `fcd8e5c769ec1fb82dd513c81ef7b41c7042149b6fcfc6e2567648360d53ff22`; the original stopped-wrapper result SHA-256 is `dfaaee48466bba9ca3a266223b4fac38511e620d8b0a69d13570b8378314bfa3`.

This publication changes only this status document relative to the tested tree. All implementation, fixture, dispatcher, build and production-policy bytes remain exactly those reviewed, built and executed. The tested source/build and original evidence remain frozen. No broader or quarantined suite, parser/backend program, authored lifecycle, Odin runtime witness or hosted CI pass is claimed.
