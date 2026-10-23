# Authored systems: future acceptance map

Source-only assessment, 2026-10-23. **Every proposed execution case below is unrun. No compiler, test, build, import, probe, generation, publication or default change was performed for this map.** Existing test bodies are reference oracles, not passing results from this assessment. Frozen engine source is `1a59410022d128108e7744cf2b5641047a44d713` (ABI 20, player artifact version 16). The typed owner candidate `6ef` is outside this map and was not modified.

## Exact current rejection and missing contracts

1. The exporter includes systems; it does not silently skip them. `src/authoring/source_catalog.c:2010-2017` scans `systems` as `DECLARATION_SOURCE`. `src/host/compile_main.c:204-216` copies every catalog source identity/text. `src/toolchain/compiler_target.c:519-548` serializes those arrays and the rich catalog inputs. A project containing a system must therefore not be classified as resource-only.
2. The authored compiler rejects that source. `tools/compiler/package_compiler/Behavior.chibi:687-689` admits exactly one `behavior` or `library` root. `Game` at lines 102-109 has Package/Library/Behavior variants only. The existing `package_compiler_rejects_invalid_library_closures` case at `test/language/package_compiler_chibi.c:7263-7294` explicitly expects `Batch :: system() {}` to reject with `one behavior or library declaration`. Passing that test proves the current refusal, not systems support. Its native-oracle expectation is false: do not convert that exact possibly-invalid empty-system fixture into a positive. Establish the current canonical FreeMotion/Drift reference first.
3. Removing the root guard would not implement systems. `tools/compiler/prepared_writer/Writer.chibi:647-648` writes zero queries, query=-1, zero query variables/filter/bindings/actions. `Compile.chibi:2117-2149` emits event handlers or ordinary procedures; it has no system entry construction. Native lowering supplies system step reads through BF_DELTA (`src/language/lower.c:3671-3684`) and builds query tables/predicates (`lower.c:7535-7553,7907-7910`). The rich input currently carries events/types/entity/operators/globals/makes/effects, with no relationship declaration array (`compiler_target.c:539-548`). Relationship queries need an explicit producer/consumer contract; do not invent IDs or infer a new query format from source names.
4. `StageOne.chibi:109-139` exports, invokes the real rich compiler entries, then assembles supplied modules. Rejection returns failure before assembly. The accepted route must retain that behavior and never recover by native-compiling an unsupported system.
5. Native source compilation is a separate reference route: analysis and `script_lower`/module finalization in `src/authoring/source_prepare.c:79-81,99-128`. The runtime selects source systems from EngineSystem scene registrations, not behavior attachments (`src/runtime/engine.c:374-435`; `src/runtime/behaviors/source.c:588-624`). Phase/order and inferred effects remain meaningful when the source compiler changes.

Starter/Terrain having no authored system files cannot establish any positive systems coverage. The five current game-equivalence stories (`test/tools/game_equivalence.c:37-43`) are useful harness infrastructure; they do not replace a system-bearing fixture.

## Existing native/reference material

| Source or selector | Observable oracle available in current ABI 20 source |
| --- | --- |
| `test/fixtures/language/content/systems/FreeMotion.chibi:1-3` and `test/runtime/system_schedule.c:171-239` | Required Transform/Velocity/Appearance, `without Collider, Body`, explicit Tick dt, scene EngineSystem activation; x reaches 12 after 60 steps, replacement changes membership, removing registration removes schedule entry. |
| `test/fixtures/declared-components/content/systems/Recharge.chibi:1-9` | Authored component numeric/bool fields, conditional writes and cycles counter. Project preparation must preserve its declared component IDs/layouts. |
| `test/fixtures/feature-coverage/content/systems/Drift.chibi:1-3`, `Cool.chibi:1-3` | Small component-update systems using Tick dt, numeric aggregate or scalar fields. |
| `test/language/program.c:833-915` | `program_systems_name_their_step`: on VM and compiled backend, position 1 + velocity 4 * dt .5 = 3; rejects wrong event, empty/wrong payload, hidden step capture in helper, and query predicates reading invocation values. |
| `test/code_system_runtime.c:195-239,340-428,435-455` | Persistent wait/membership cancellation, live component access after resume, optional component/exclusion roundtrip, any/empty-any membership, synchronous component update yielding .6. Native route only. The persistent-wait case still reads Health after waiting, so it does not prove dependency retention after the last access. |
| `test/runtime/system_schedule.c:300-441,596-625` | Multiple source systems, read/write conflicts, serial/parallel outcomes, registration replacement/removal and scheduling barriers; native scheduler checks explicit dependencies/order/cycles. |
| `test/language/program.c:949-1765,1859-1928` | Existing ABI20 query path/source/optional/join/value/change-filter/generator semantics; admission frozen before writes, short-circuit failure order, busy-write observations and registration-owned shared state. These are not ABI22 query-value tests. |
| `test/language/query_boundary.c:306-331,461-490,736-1089` | Direct query instructions, prepared roundtrip, required bound-variable masks, transitive predicate purity and relinking/forged-metadata rejection. |
| `test/language/program.c:1766-1858` | Imported callable generation, owner namespace/reader retention and replacement across waits. This fixture uses a behavior caller, so it is an import-lifetime reference; a system caller case remains to be authored. |
| `test/tools/main.c:1090-1093,1352-1365` | Existing C dispatchers: `--system-schedule`, `--source-systems`, `--source-query-rows`. Extend the owning C pico_unit dispatchers for future parity assertions; do not add standalone probe executables. |

`test/fixtures/systems/MovementSystem.chibi` uses an older nested `on Tick` spelling; do not choose it as the canonical positive fixture. Use current header/body syntax above and verify the native reference before admitting a new case.

## Proposed acceptance cases before a shipped systems-default claim

All rows are proposed and unrun. Cases 1-5 form a useful first synchronous implementation slice, but do not by themselves establish parity with the already-supported native systems contract in rows 6-10.

| Case | Concrete acceptance and useful failure control |
| --- | --- |
| 1. One scheduled system and step | Package the current Drift/Motion header syntax with one EngineSystem scene registration, one matching entity and one missing a required component. Fixed dt updates only the match once per step. In this scene-scheduling case, no registration runs nothing; a world query with zero matching rows runs nothing. This does not describe an empty `system()` header: the separate low-level manual-attachment contract creates no world query and runs only attached instances. Assert emitted entry is a system, its query/step shape is correct, and no source is lost or classified resource-only. Use `program_systems_name_their_step` plus the actual scheduled engine fixture as oracles. |
| 2. Required/optional/excluded membership | Adapt the native Optional system: matching Health+Aim adds 7; missing Aim adds 2; Collider excludes the row. Remove optional/required components, reapply commands, and verify membership/presence behavior without stale reads. Add all/any/empty-any and short-circuit conditions already supported by the native route. |
| 3. Real native catalog calls and components | System body uses a supported synchronous real native function/action and scalar/aggregate component reads/writes, preserving actual binding ID/ABI/defaults/effects and source evaluation order. Use a side-effect result that cannot pass from metadata alone. Reuse Recharge for declared-component bool/numeric fields. Wrong nominal component/catalog identity and a genuinely unsupported call must reject before artifact replacement. |
| 4. Imported callable body | System calls an actual separately authored library callable using explicit arguments for step/query values (for example a numeric Health update helper). The native source route must accept the exact fixture first. Check computed result, cross-module ID/fingerprint, transitive read/write effect and source-order independence. The existing imported behavior fixture is only an oracle seam, not a pre-existing system pass. Reject implicit capture of system invocation locals; do not equate an import declaration or catalog row with executed helper code. |
| 5. Scheduling/effects interaction | Compose Motion (`x += velocity * dt`), Scale (`x *= 2`) and independent Heal. Check EngineSystem phase/order and dependency-derived serial/parallel results, including a deliberately order-sensitive value. Invalid/duplicate/unknown registrations, dependency cycles and native-name conflicts match native errors. Queries refresh after structural command boundaries. |
| 6. Existing sourced-query semantics | On ABI20's existing module query representation, compare exclusive/optional/ancestor sources and explicit multi-target generators, value/change filters and short-circuit failure order. Include no matches, two roots sharing a target, target replacement and a transitive pure helper predicate. Assert query shape/bound masks and effects, not just final position. Exporter relationships must be explicit and reviewed before these cases. |
| 7. Existing waits/state/lifecycle | Native ABI20 provides historical suspension, per-row retention, registration state and membership-cancellation oracles. Distinguish structural tuple loss from a false value guard: a false guard suppresses new admission and does not by itself cancel an already suspended activation. Old live-component oracles do not establish flat readonly-snapshot or `^` live-reference behavior. Preserve the latest required whole-task dependency lifetime: after the final component access, while the task still waits, removing that dependency must cancel every remaining continuation/completion effect while retaining effects already completed. Run that exact case as well as removal before access/resume; compare state, pending count and cleanup. An older native ABI20 outcome cannot override this decision. Reconcile the common/query lifetime owners and their matching evidence before implementation/adoption; this note grants no ABI22 import or pass. Async native actions remain unsupported by today's rich game compiler; a complete default cannot silently substitute native compilation. This row is a separate implementation dependency, not coverage granted by synchronous cases. |
| 8. Prepared ownership and publication | Roundtrip system-bearing modules through the real supplied-module artifact assembly and prepared reader, then run with source/compiler directories unavailable. Missing/forged module, query bound masks, type identity or service bindings reject; the prior artifact remains intact on failure. Include linked predicate purity revalidation. |
| 9. Project/player/Studio behavior | Use a real small project with scene EngineSystem registration and declared components. Compare native vs authored builds under identical ticks/inputs using existing game-equivalence ECS/save/UI observations, then save/reload and rerun. Confirm editor open/run rebuild consumes the selected installed compiler route. Compiler self-reproduction and an SDK doctor pass do not prove this row. |
| 10. Diagnostics and policy | Wrong Tick event/payload, hidden step captures, unsupported system construct, source-private access, missing imported callable and invalid query predicate produce source-backed errors. Exercise actual exporter → authored compiler → assembly route, checking failure status and no output publication/native fallback. Keep negative unsupported cases until each becomes a genuine positive oracle; do not merely delete the current system rejection assertion. |

For every positive case use the same exact source/catalog/target inputs on native and authored routes; compare the prepared contract (system entry, query membership/filter, imports and transitive effects), meaningful runtime observations and failure behavior on bytecode and the supported compiled backend. Native C remains the assembly/runtime boundary; all executable system and helper bodies in the authored artifact must come from the selected authored compiler. Pin source/compiler/host/SDK identities in the receipt. Broader/native tests may remain green while the authored path rejects systems, so their results must stay separate.

## Query-owner boundary: read, do not import

Read-only consultation used `/Users/cwaldner/Personal/chibi/.worktrees/query-lifetime/chibi`. Its live HEAD is `cbed73d239f7b50d0a2a6c5644afa4cc8cdf4e91`; it has a broad dirty source candidate. `PLAN.md:72-80` explicitly calls prepared ABI22 local and incompatible with the collection candidate's independently numbered ABI22. `PLAN.md:161-182` distinguishes the frozen query review tree `4c3d1d844eec07fa02791d7b1b1f0b65d834a5a3`, common pointer ABI21 and main ABI20, and requires a fresh reconciled wire/registry/bootstrap. The separate `lifetime-tree.txt` records `b2c82d244774c095feb9d4c223740a1a85265099`; do not conflate these two snapshots or treat either as current combined evidence.

The pending `src/types/query.h:7-23` introduces managed query-description/row values retaining module generations. `src/execution/query.h` and `tools/compiler/package_compiler/{Queries,QueryLoops}.chibi` belong to that query/value/lifetime lane. These files, operations 80-88 and a **future matching ABI22 bootstrap** are not inputs to source32 ABI20; no such combined bootstrap proof has run. Existing native ABI20 system-header query semantics in the acceptance rows above do not require importing the pending first-class query-value representation. Any later choice to use that representation needs the query/common/collection owners' reconciled contract and fresh matching evidence first.

The query owner's current `test/language/query_values.c:204-233` and `docs/code-language.md:759-763` distinguish manually attached, unqueried `system()` instances from zero matching query rows. Its `src/execution/program.c` refresh_rows logic distinguishes structural tuple loss from guard-based admission. These are read-only source references in a separate pending candidate, not source32 runtime results.

**Separate proposed authored/query lifetime gate, unrun:** the authored compiler infers a dependency from actual component access. That access is the last component read: the task then increments an independent durable counter and waits while still incomplete. It later attempts a completion effect, with NO component access after that initial read. No host-installed binding or authored lifetime marker may supply the dependency. Remove the target/dependency during the wait, including a remove-and-readd variant: the earlier counter remains 1, completion remains 0, subsequent sound/effects are suppressed, an independent surviving waiter receives `Cancelled`, pending requests cannot publish into that task, and resources detach. Re-adding an entity/component must never revive the old activation identity. Include a surviving-target control that completes normally, completed-success retention after later removal, child cancellation and independent value-only work. Exercise false value guards separately so they do not masquerade as structural cancellation. This is the approved lifetime contract to implement, not a parity claim about an older ABI20 result; it needs reconciled common/query evidence and meaningful native/authored checks.

If the flat query representation is selected later, additional distinct unrun native/authored parity gates cover readonly body-local `dt`, readonly snapshots versus `^` live references across waits, query-description/cursor/module-generation retention, and migration/legacy rejection. These conditional gates make no ABI selection and grant no implementation, import, bootstrap or execution coverage.

The task owner's read-only fixture review is recorded at `/Users/cwaldner/Personal/chibi/.worktrees/value-semantics/chibi/.chibi/task-prerequisite-review/systems-lifetime-review.md`. The query-lane `test/language/program.c` source matches frozen tree `b2c82d244774c095feb9d4c223740a1a85265099`, blob `d16c9a5e0d64a37786e2f181d617d644b5ba6b2b`. Its cases at 4199 (pending children/cancellation/replacement/passive users), 4373 (success publication), 4459 (completed result and child dependencies) and 4496 (teardown) are useful source-level runtime assertions. Their `lifetime_start`/`program_task_start_bound` and `lifetime_bind` harness explicitly installs dependencies: none proves authored inference, and no case was executed by this review. Keep these private-runtime prerequisites separate from the proposed inferred-dependency acceptance above. The task candidate `6353f5a382785977b0b9948cfcbcde37facd0d0c` and lifetime documentation overlay `f0fe82ef0e09532f74c356fc194edf556f78d13d` remain separate frozen owner inputs, not imported coverage.


## Verified identities

Engine paths below are relative to `/Users/cwaldner/Personal/chibi/.worktrees/selfhost-behavior-state/chibi`; each byte sequence was checked against immutable tree `1a59410022d128108e7744cf2b5641047a44d713`. SHA256 values identify the source read here, not executed results.

| Engine source | SHA256 |
| --- | --- |
| `src/host/compile_main.c` | `c472912fa136f3d42a69b0e907c49a7140801e0080ac8582b759520249fd3aee` |
| `src/authoring/source_catalog.c` | `2c65a4cedc5be72f9bc0396e801a86218d5a21136f6ae0fcae9efade262e12fe` |
| `src/toolchain/compiler_target.c` | `8b2d14611567f15b42c0cce47891f0469667b8fdf177a02a5a982255e8320430` |
| `tools/toolchain/StageOne.chibi` | `1a2e43dc6cede3b9eb9140f35e8f4b305f42e347d59a9cea51dcff39e1d2dd29` |
| `tools/compiler/package_compiler/Behavior.chibi` | `bf1fc927fc873a13d9880046a002f646b2af2e4494c65cc8778e5b1ba87f592d` |
| `tools/compiler/package_compiler/Compile.chibi` | `d8f2e0e8698de16da6369cc78ae05e1a6e29aea14287dd65474f7f086cacbaae` |
| `tools/compiler/prepared_writer/Writer.chibi` | `f10c7e7fce12aa046f59f4196b6ce6364dba217f3331dec196d463e347936281` |
| `src/execution/prepared.h` | `50468500004529a465d64d6236f6d57cc7ce4dcbb6cfe0be0de948466b4186ce` |
| `src/runtime/player_prepared.h` | `faa73e15b31af130ee96f81a47ec6c3b7c0ab05e3fe928a90198a422a295accd` |
| `src/authoring/source_prepare.c` | `991fad76146ec42ed3bd814124faf120f1d9ef09eac98d4f6ba3276f7ec171e9` |
| `src/language/semantic.c` | `688afc7375657448b18f3f08fc4eb05097d74ff34c24b466f5c0766227b0acbb` |
| `src/language/lower.c` | `d7d2ffb68b2c53cfc99d8d4c525347152b719071ff05ab253b90156966999537` |
| `src/runtime/engine.c` | `2b91ce3a2835e915f92c295a300467dc093ebb9cfd6fae07da1fd78032e27d26` |
| `src/runtime/behaviors/source.c` | `8fa8ac4c99d7c85da7647a4557fcdc7fbaaad8ae4ca404bbb70ced64d11fc178` |
| `test/language/package_compiler_chibi.c` | `52109159cabee65a5f7eeb702481ec07bac596374359399f4719143d75eadff1` |
| `test/language/program.c` | `2045da9bb804d3b7a181e6f4ca178f9e0bed6421582a48730c55725751d44a19` |
| `test/language/query_boundary.c` | `13b0c7e419bd605fdc30c42dc37dbf5a27a434d26515e9671428a28eac5fa291` |
| `test/runtime/system_schedule.c` | `a33badbd6dc38b49caeea4f94ffc2540793777d5b3340b42f2a88e0896e593fa` |
| `test/code_system_runtime.c` | `c60c83f29e5cee32ee5ad3f08a015b34847cf149d14e085db5199689f6567a70` |
| `test/tools/game_equivalence.c` | `e1041c556a2cebf215228320ee4bb97a2bedda61bdb44dfbf71c665b2d110e5b` |
| `test/tools/main.c` | `66f0514d4a6613fa90424d09142df08f5e793e7508bbb21840aded60c71c9fe9` |
| `test/fixtures/language/content/systems/FreeMotion.chibi` | `c1262e1507842cb0c209a58b1b5229af6e328ff0a3c3f1f3fb68e5f9b151732a` |
| `test/fixtures/declared-components/content/systems/Recharge.chibi` | `cbb5ee003ddd2d02a27bbccf3a6d9bf82af2925841e4540636d6e44da58d1cd7` |
| `test/fixtures/feature-coverage/content/systems/Drift.chibi` | `77febec59f3cd4c8e8769455e2f70f61af5788799a5e0ebdc5e632f438dd3bc1` |
| `test/fixtures/feature-coverage/content/systems/Cool.chibi` | `ae96750c029d996708fa918399f62f9a4aab24cf3d72b68cf6f5f27deaced849` |
| `test/fixtures/systems/MovementSystem.chibi` | `b0833051489f2884ebac6611403b27bba82353a5358b7858ae32b5e2ef2e678a` |

Query-owner evidence is read-only and outside the engine candidate. These hashes identify the consulted bytes; owner PLAN status is not execution evidence produced here.

| Query-owner path | SHA256 |
| --- | --- |
| `PLAN.md` | `9e0b3a806b403b71fcb74cc8bc02cac8f8f84dc9b0dfc3206dcefd88994db003` |
| `.chibi/query-validation/lifetime-tree.txt` | `0028fe271f6864dfb6313c1a465c0174b5659872479b36209147157c6878d950` |
| `.chibi/query-validation/lifetime-source.sha256` | `5a9e6f629ab684e77b70e7d7b54a6ca4b240ad0acaadbaf3dc8c319034d34761` |
| `src/execution/prepared.h` | `6597cf7904eefe1cc14a5c0b8499cb66a1909000b01f1813815bb292975fb389` |
| `src/types/query.h` | `40b4c8269db5f5038a201ef913193446e92246aa6af659a6298597eaa4f86776` |
| `src/execution/query.h` | `90cbceb0153ed5c4c2751c747e09fd0f6bc73f3aa862bd7d790d4730989cecb6` |
| `tools/compiler/package_compiler/Queries.chibi` | `706915b2d284cbcc81d53d92285f04a0ae2f1f6bceeb20ff68215863c2f71f41` |
| `tools/compiler/package_compiler/QueryLoops.chibi` | `761c42acf532f4eee1067682047124473c0431b2b58ab5ee828a54dce417c406` |

Only this ignored map was written. Frozen source32/index and the query/typed candidates were not changed. All proposed coverage remains unrun.
