# Handoff 06: Builtin math (vec/quat/mat, writable swizzles, recoverable math failures)

Codex session `01a180c5-6950-7cb2-879d-d6f854658c04`, 2026-10-27 22:29 to 2026-10-28 20:25 MDT (log mtime; last turn ended 20:25).
Log: `/Users/cwaldner/.codex/sessions/2026/10/27/rollout-2026-10-27T22-29-43-01a180c5-6950-7cb2-879d-d6f854658c04.jsonl` (32 MB; only copy). Written 2026-10-28 ~22:55 MDT from the log plus read-only git/gh. Coordinator-side name for this chat: "Plan Chibi vectors matrices and quaternions".

## 0. Read this first

1. The only working copy of the math compiler integration is **staged but uncommitted and unpushed** in `/Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi`. `/Users/cwaldner/Personal/chibi/HANDOFF-codex-worktrees.md` lists that worktree as "merged: clean up". Do not clean it up. It is also protected by tree refs `refs/codex/builtin-math-*` (section 3).
2. The thread ended frozen at staged tree `e0731ca6` on accepted main `79743c567`. Nothing is waiting on this thread. Everything left needs a coordinator-allocated heavy-producer slot (compiler artifact, asset build, self-build, ci-seed) or an upstream owner (#801 WriteStep, #813 results, #808 records).
3. Private seed "f1a" and every earlier math proof are attributed to older sources. Compile, Body and Steps changed since, so none of those proofs transfers to the current tree. A fresh combined regeneration on then-current main is still owed.

## 1. Goal and scope

Root prompt (coordinator thread `01a180b0-3773-78a1-beeb-03420dfa7a21`, 10-27 22:29): "Own the built-in f32 vector/quaternion/matrix work. The direction is already selected: vec2/vec3/vec4, quat, mat2/mat3/mat4 with CPU/shader consistency; do not reopen built-in-vs-library as if undecided." Also: "Preserve Chibi value semantics instead of cloning Odin's allocator/pointer APIs. Prefer inference and existing forms before new syntax." and "Report proposed, implemented, merged and verified states separately." and "Use meaningful behavior/oracle checks rather than tests mirroring implementation; no global test-duration ceiling."

Owner decisions in this chat (UTC-6):
- 10-27 22:52: "Approved. Proceed with writable swizzles, recoverable math failures, and staged backend delivery. Own the math modules and tests, and coordinate shared compiler integration with Claude to avoid overlap."
- 10-28 00:08: "Is there any way we can rebase off of 694 early or something? It's still failing CI." Then 00:13: "Go ahead and build and test that branch, then continue the math compiler integration. Keep coordinating with Claude and track inherited failures separately."
- Relayed by the parent chat: "Yes, coordinate all ten chats" and "Work as autonomously as possible please. You may commit, open PRs, review, and merge on my behalf." That grant was to the Codex threads. Re-confirm with the owner before you commit, push or merge.
- "Claude" in the log means the owner's Claude Code sessions that owned #694 (collections), #775 and #797. Their #694 file reservation ended when #694 merged. Direct messaging between those sessions and Codex was not authorized; the coordinator relayed.

Approved contract (full text: `docs/chibi-shaders/builtin-math-stage.md` in the primary worktree):
- Types `vec2 vec3 vec4 quat mat2 mat3 mat4`, all f32. Constructors take exact component counts, scalar splat, `mat3(1)` is identity. Contextual literals follow existing rules; f64 needs explicit `f32()` or `number()`.
- Memberwise `+ - * /` with scalar broadcast in either order. `==` is exact and returns one bool. Ordering comparisons are rejected.
- Swizzles use `xyzw` or `rgba`, one alphabet per access. Reads may repeat components. Writes need distinct destinations, evaluate the receiver once, and snapshot the RHS (`v.xy = v.yx` swaps).
- Column vectors. `M * v`; `A * B` applies B first; `v * M` is rejected with a convention diagnostic. Explicit `matrix_comp_mult`.
- `quat(x,y,z,w)`, w real, Hamilton product, `q2 * q1` applies q1 first, radians. Raw construction never normalizes.
- Recoverable: `normalize`, `normalize_or`, `inverse`, `quat_normalize`, quaternion-vector rotation. `normalize_or_zero` supplies zero itself.
- Lowercase builtin `step(edge, value)` uses GLSL order. The scalar package keeps `math.Step(value, edge)`. Do not rewrite either.
- Layout: compact CPU storage (vec3 12 B, mat3 36 B). std140/std430 stride and alignment are explicit buffer contracts. Do not memcpy compact mat3 into a shader buffer. No CPU/GPU bit-identity promise.
- Delivery lanes are separate and must be reported separately: module, stage zero (C, `src/language`), stage one (Chibi, `tools/compiler`), VMs (BYTECODE, SLJIT), native LLVM (not started), shader S1/S3/S7 (not started).

## 2. Ownership

PRIMARY (this thread owns):
- `/Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi`, branch `codex/builtin-math-main-20261028`. This is the live forward port, staged only.
- `/Users/cwaldner/Personal/chibi/.worktrees/builtin-math/chibi`, branch `codex/builtin-math-on-694-20261028`, pinned to #694 head `19ad570dd`. Older frozen candidate (index tree `170b9a09`). Superseded by the main port; keep as history until the owner says otherwise.
- Branch `codex/builtin-math-20261027` is draft PR waldnercharles/chibi#783 (module stage only). It has no local worktree.
- `cwaldner/first-class-vectors` at `be5ab05b968624155445bf803678778886420eb9` (2026-10-17 vector work). Source of ideas only. Do not merge, delete or move it. Do not import its project-version migration.
- Hindsight initiative page `kp-05c57bb27d324ace8275d84f395418c0` ("Built-in f32 math recovery and staged backend integration"), with about 20 ingested notes.

TOUCHED, not owned:
- #802 prepared-record writer fix: authored by this thread, merged. Worktree `.worktrees/prepared-record-write-cache` is removed; branch `codex/prepared-record-write-cache-20261028` still exists locally and on origin. Archive: `/Users/cwaldner/Personal/chibi/.coordination/archives/2026-10-28/prepared-record-write-cache/`.
- `.worktrees/array-argument-effects-a253/chibi` and `.worktrees/array-argument-effects/chibi` (#801): read only. Source of the WRITE-only patch.
- `.worktrees/compositional-records/chibi` (#808): read only.

Overlap with other threads:
- Coordinator: serializes producer slots, landing slots, ci-seed and pin mutation, and final merges. Math has no landing slot yet.
- Strings/integers #821: none, except shared test fixtures (pin_string initializers fixed by #800).
- Portable libraries #795: first feature landing. Its owner handles the Wasm pin_string warnings (assigned to #795's portability lane). Do not duplicate.
- Compositional records #808 (Prepared27, frozen at `696719e1f9aae8346a7c29af31b990685ec81490`): seams are `MathDecl15` admission, requirement indices and nested math fields (section 5).
- Generics/callables #810: numeric binding, Work.phase 80 to 82.
- Recoverable errors/results #813: decides the public recoverable result shape. Math waits on it.
- Host clock #784: none, except installed-seed timing.
- Core math #791: `math.Step` vs builtin `step`; retires the legacy C Step; adds `sin_precise`, `cos_precise`, `fmod` natives that do not redefine builtin `sin`/`cos`.
- Language-UX/main-runner-reload and cmake-retirement/runtime-sdk: none directly. #814 (CMake retirement) landed on main after this tree's base.
- #801 effects owner: owns shared `WriteStep`/`ReferencePlace` metadata. Math includes only the reviewed WRITE-only hunk.

## 3. Current verified state (read-only, 2026-10-28 ~22:50 MDT)

Primary worktree `.../builtin-math-main/chibi`:
- Branch `codex/builtin-math-main-20261028`, HEAD `79743c567` (#803 merge), 0 commits ahead of origin/main, 13 behind. Local origin/main = `d66cb3bebf33ff3c4e21ef53e3f3990a67ecc60c` (#816); `gh api` agrees.
- `git status`: 44 entries, all staged (`A`/`M`), 0 unstaged, 0 untracked (`.chibi/` is ignored, 2.5 GB). Diff vs HEAD: 44 files, +6476/-67.
- `git diff-index --cached e0731ca6` is empty, so the index equals tree `e0731ca621abf03d0632bac9ec2aa4485e88115f`. This matches the log's claim.
- The 13 newer main commits touch 18 files (CI, Makefile, `tools/graph/engine/{Engine,Options,Profile}.chibi`, `tools/toolchain/Graph.chibi`, `test/tools/*`, `test/language/generic.c`, docs). **None overlap the 44 math paths.**
- Preservation refs (shared `.git`): `refs/codex/builtin-math-accepted797-scoped-write-sourceprep-20261028` (tree `e0731ca6`, current), `...-scoped-write-code-20261028` (`43f0d024`), `...-accepteda253-documented-20261028` (`0abd96c6`), `...-accepteda253-sourceprep-20261028` (`b4dbc77d`), `...-7c6-source-20261028` (commit `5c1cf7b38`), plus `before-*` and `*-evidence-*` refs. They are stash-style commits or bare trees, not on the stash list.

Older worktree `.../builtin-math/chibi`: branch `codex/builtin-math-on-694-20261028`, HEAD `19ad570dd`, 222 ahead of the local origin/main only because #694 was squash-merged, 41 staged files, 805 MB. The log agrees.

Math PR (only one open):
- waldnercharles/chibi#783, draft, "Add authored math contracts and constant-evaluation helpers". Head `22c999a9acc83511be32ec488a3a9cfcdc4975df`, mergeable CLEAN, all executed checks SUCCESS (Linux and macOS native, three self-builds, Studio, clang-format, Linux SDK relocation; Windows and paired lanes SKIPPED). **Disagrees with the log:** the log knows only head `94d09d8cb650417197f6c5202e35497d76943826`. `22c999a9a` (10-28 12:04 MDT) was pushed by someone else after an independent review comment (`pull/783#issuecomment-6461900701`). It resolves fixtures via `CHIBI_SOURCE_ROOT` and spells the exact f32 max in `tools/compiler/math_constants/Values.chibi`.
- **Gap:** the forward port already uses `CHIBI_SOURCE_ROOT`, but its `Values.chibi` still has `3.4028234663852886e38`, and it lacks #783's 12-line boundary test. That is the Linux parse bug (rounded decimal slightly above f32 max). Carry the fix and test over.
- #783 keeps the module tests under `tools/compiler/math_*/Tests.chibi`; the forward port moved them to `test/compiler_math/*/Tests.chibi` (to keep the 30-source bootstrap inventory clean). Expect a rename conflict if both land.

Related PRs (gh): #694 MERGED `7c6ca2f5d2c0` 10-28 14:00Z; #802 MERGED `74a9ac60a44a` (source `43eec64f`, pin `d6ee0ce259`); #803 MERGED `79743c5675e5`; #807 MERGED `51650ccff2c8` (Body); #817 MERGED `a253ef4b5b91`; #797 MERGED `30837bd5dd05`; #801 OPEN draft head `563ba6c644` (all CI red; its remote head was not updated to the reviewed local `6c0cee1c`); #813 draft `e902fcf3c0`; #808 draft `696719e1f9`; #795 draft `dea59917c7`; #791 draft `35208481b3`.

## 4. Done and left

Done, with evidence (all as reported in the log; I verified the git and PR facts above, not the test results):
- Plan approved (see section 1). Draft #783: 44 authored module cases pass on BYTECODE and SLJIT, 17 policy cases via stage one.
- Provisional integration on the #694 pin: 42 source cases (34 accepted programs plus 8 required diagnostics) pass on both compiler VMs. Semantic 78, program 205, native-calls 7, component-write 15, borrow 31, navigation 36, source-edits 8.
- Forward port to accepted main in steps (`7c6ca2f5` -> `3e2b28dc` -> `afcd777b` -> `30837bd5` -> `74a9ac60` -> `51650ccf` -> `a253ef4b` -> `79743c56` + WRITE-only patch -> `e0731ca6`). Fixed along the way: native-return type dispatch, native descriptor preservation, pointer-swizzle effects, debugger watch for implicit deref, stage-zero copy optimization missing swizzle writes, `^vec3` scoped parameter lookup, inventory order vs the bootstrap producer, 34 math sample initializers (strict `-Werror`).
- 30-source compiler inventory (27 plus `package_compiler/Math.chibi`, `math_types/Types.chibi`, `math_types/Functions.chibi`; listed in `tools/compiler/asset_compiler/CompilerSources.txt`). Self-bootstrap at the `7c6ca2f5` base: 30,371 assertions, 691 s, reproduces the identical 22,469,206-byte compiler (sha `f1a6bdff...c9c`); supported artifact writer and recovery passed (experimental only).
- Asset generation with that experimental artifact failed with `prepared authored layout work limit exceeded`. The accepted baseline passed, so this is a **math-caused regression**. Fixed as separate prerequisite #802 (writer descriptor cache). Counter proof: 2,105,152 -> 16,256 duplicate-name comparisons for 128 fields and 256 headers with identical output bytes. The paired replay on runtime `30837bd5` failed without it and passed with it (94.7 s vs 99.0 s). Installed published-seed oracle: old seed rejects the retained import, new seed returns 37 on both VMs. Merged as `74a9ac60`.
- Shared WRITE gap found: C marks the mutating helper WRITE=2, the authored compiler says 0, for ordinary scoped `^int`/`^Box` stores. Inherited, independent of math. Reviewed fix patch (sha256 `e497dda37cd4cd57222040b82fbabb6df8f7439508ba7258164166bf4e93298c`) from #801 owner checkpoint `6c0cee1c5604dfc8fc4f49c518235626e0a66ea4` is now in the staged tree. A private replay (not production) passed 741 assertions, 12 target runs, GC pressure 850 marks (C) and 431 (authored), mask 63, 3 getter calls.
- Last strict native check (frozen tree `b4dbc77d` on `a253ef4b`): `--compiler-fixture`, `--package-imports`, `--package-tests` = 33 cases, 2,282 assertions, plus 44 math module cases per VM.
- Reservations agreed: `MathDecl=15` (Optional 12, Collection 13, Char 14), Work.phase math 72 to 77, optional-wrap 78, generics 80 to 82, completion 8/9. Query intrinsics 80 to 88 are a different domain.

Left, in order:
1. Ask the owner whether to commit tree `e0731ca6`. Verify the index equals it (`git diff-index --cached --quiet e0731ca6`), then commit onto a new local branch so the work is not index-only. Do not push without confirmation.
2. Rebase the port onto current main (`d66cb3beb` or newer). No path overlap today, so expected low risk. Re-audit reserved ordinals and the 30-source order against the new tree. Record the rebased tree as a new checkpoint; do not overwrite old receipts.
3. Port #783's `22c999a9a` f32-max fix and test into the forward port. Decide with the owner how #783 relates to the forward port (update #783, or a new PR plus closing #783).
4. Lightweight strict native replay on the rebased tree (section 6). No producer needed.
5. Ask the coordinator/owner for a heavy-producer slot, then in order: scoped-WRITE and purity controls (rows in `.chibi/math-main/runtime797-write-prep/scoped-write-controls-supplement.json`: StoreInt, StoreRecord, CallWriteInt, CallWriteRecord, OwnWriteInt, OwnWriteRecord must be WRITE=2/parallel=false; LocalBox, ScopedRead, CallRead, Identity, StoreView stay effects 0); `--package-compiler-chibi-types`, `-types-compiled` (42 math cases, native math, GC pressure); collections, imports, sharing, debug-free; fresh 30-source compiler artifact with manifest, recovery, two-stage self-build and fixed point; normal default asset build and SDK checks. The authoritative bootstrap must be regenerated once, on then-current main.
6. Remaining feature work (from the stage doc): public recoverable result shape and call-site tests (after #813); source constant folding parity; user records containing math values (after #808/Prepared27 and `MathDecl15` admission); quaternion axis-angle, slerp, matrix conversion; native LLVM O0/O2 oracle execution (needs a named native owner; do not edit the shared LLVM producer); shader CPU/GPU agreement (S1/S3, matrix IR S7). Full `make test-*` aggregates and `test-wasm` are unrun on this tree.

## 5. Holds, freezes, coordination rules (exact)

- Nothing in this thread is published or merged except #783 draft and #802. Math SDK publication, ABI, artifact and seed holds all remain. Do not mark #783 ready or merge it on your own.
- Do not reuse private seed f1a (`.chibi/math-main/seed/compiler.chibip`, sha `f1a6bdffc9d8bec2de2b55f61bad48a2d8593c2a8ab1fc21cca42f46a6497c9c`) for the current tree. Compile, Body, Steps differ (`Compile` sha256 `a0a98b21...`, `Body` `7d8ea6d4...`, `Steps` `a0a09a15...`). Any experimental artifact must be labelled experimental and kept out of the checked files.
- Accepted base pair is exact: checked compiler `27d8f3d0568e442e1a70b167c04ba97c671ea0d0b1e85e3a7493d88fc59bb912` (20,792,064 B), E1 native runtime pin (#802 manifest `e1adf32b...23348`), prepared format 26, private build protocol 14, public ABI unchanged. Math changes none of these. Do not touch prepared format, runtime, GC, LLVM producer, checked bootstrap files or SDK registration. If an ABI change seems needed, stop and report.
- Heavy producers (full compiler fixture or self-build, artifact writer, asset generation, sanitizer matrix, ci-seed) require a slot from the coordinator. The host has 14 cores; overlapping runs caused the asset timeouts. Keep existing limits: no quota or budget increases, no warning exemptions in production targets.
- Landing: one shared-compiler workstream at a time, with one final bootstrap regeneration on then-current main (Claude's rule on #694: `pull/694#issuecomment-6461212824`). #795 libraries are the first feature landing lane. Records, generics, vectors and clock keep their own gates.
- #801: import only the WRITE-only hunk already staged. Do not import its 22 array metadata changes, its read helpers, or run its full argument-effects selector as proof (expected masks assume the array changes). Excluded rows: Owned, Imported, View, ViewMethod, ViewSlice, ViewIndex, Deep, OptionalView, MapView, Dereference. #801 source ownership is the coordinator's `stdlib_parallel_audit` helper (Gibbs).
- Records (#808, Prepared27): do not import. Seams to honor later: records' `RecordFieldType` and `requirement_indices` omit `MathDecl15`; math must keep its strict `script_math_layout_valid` exception inside `managed_address_layout` eligibility while records removes the inline-field exclusion; names, storage and hash stay nominal; no math-specific wire form. Nested or generic math fields stay pending.
- Results (#813): ignore the old `@fallible` marker; the live convention is a typed final optional error inferred from `@error` result types (`docs/recoverable-errors.md` at `e902fcf3`). Current adapters reject recoverable operations explicitly; do not infer fallibility from names, and do not add a math-only workaround.
- Core math (#791): never reintroduce the retired native/graph Step. The builtin must work without the retired capability.
- Reserved ordinals above are binding. If #813, #810 or #808 land first, re-audit before use; report any collision before choosing a replacement. The optional-wrap remap to 78 belongs to the combined integration, not to frozen standalone proofs.
- Evidence preservation: the 554 protected files under `.chibi/math-main` and the `refs/codex/builtin-math-*` refs stay byte-identical. Raw receipts are never rewritten; new facts go in new files. Keep old and new proof attribution separate.
- Project version: no `CHIBI_PROJECT_VERSION` bump. Math adds no project-loading contract change; the old `be5ab05b9` migration is obsolete.

## 6. How to validate

Repo rules that bite (`/Users/cwaldner/Personal/chibi/chibi/CLAUDE.md`): pico_unit tests in the owning dispatcher (`test/language/main.c`), a failing-first case for every fix; no per-probe executables; format C with clang-format 22 and `.chibi` with `chibi_ctl --format`; review the diff before commit; run what CI runs (`make test-native`, `test-materials`, `test-projects`, `test-language-all`, `test-tools`; owner memory also requires `make test-wasm`, `test-stage-one` and `game-equivalence-stage-one`) and say what was not run; leave no scratch; stage-zero C is deliberate here because both compilers must match, but do not optimize it; `Build.chibi` must parse with an older build host (owner memory note).

Lightweight replay used by this thread (no producer; exact arrays in `.chibi/math-main/runtimea253-validation/build/{configure,build,selectors}-result.json`, recipe in `.../REPLAY.md`):
- `git archive` the candidate into a new empty ignored dir, then `cmake -S <snap>/test/language -B <b> -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm@22/bin/clang -DCMAKE_C_COMPILER_LAUNCHER= -DCMAKE_C_FLAGS= -DCHIBI_SDK_BUILD_COMPILER=ON -DCHIBI_SDK_BUILD_NATIVE=OFF -DCHIBI_SDK_BUILD_DESKTOP=OFF -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_LANGUAGE_TEST_ASSET_COMPILER_HOST=OFF -DCHIBI_THIRD_PARTY_CACHE=/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party`. Build `test_language` (`--parallel 4`; about 3.5 s configure, 24 to 33 s build).
- Selectors, serially: `python3 <snap>/tools/test_policy.py --jobs 1 native <b>/test_language --<selector>`. Run at `74a9ac60`: 9 groups, 235 cases, 21,636 assertions.
- Math selectors: `--package-tests` (44 module cases per VM), `--package-compiler-chibi-types` and `-types-compiled` (42 source cases; math case 71 s, whole selector about 101 s), `--package-compiler-chibi-math-native`, `--semantic`, `--program`, `--native-calls`, `--component-write`, `--borrow`, `--navigation`, `--source-edits`, `--asset-packaging-chibi` (4 cases at 30 sources), `--native-char-results`, `--record-*`, `--package-compiler-chibi-{collections,sharing,imports,debug-free}`.
- Not run for the current tree: everything in the plan's `pending_execution_order` (`.chibi/math-main/runtime797-write-prep/validation-plan.json`), all producers, the default-ON asset-host build, SDK/Studio, ASan/UBSan, Wasm, Linux/Windows native, LLVM, shader.
- Slow or flaky: self-bootstrap about 11.5 min; asset generation 95 to 395 s; Debug fixed point about 33 min (Body's, not math's). The sandbox once denied the default ccache dir, so use a writable `CCACHE_DIR`. The bootstrap selector needs `CHIBI_LANGUAGE_BUILD_DIR`/`CHIBI_COMPILER_HOST` pointed at your build or it fails on default `build/` paths. Seed command used by the generics thread: `CHIBI_BOOTSTRAP_SEED=<abs dir> CHIBI_RELEASE_LANGUAGE_BUILD_DIR=<abs build> python3 tools/test_policy.py tool language-package-compiler-chibi-bootstrap`. Native-seed GitHub release assets must be fetched with `gh`, not the connector.

Scratch to clean (ask first; keep receipts and refs):
- `/Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi/.chibi/math-main` (2.5 GB; biggest: `paired-cache-308` 611 MB, `runtime74a-validation` 230 MB, `baseline-assets-3e2` 158 MB, plus several 80 to 135 MB `*-build`/`*-validation` dirs).
- `/Users/cwaldner/Personal/chibi/.worktrees/builtin-math/chibi/.chibi` (129 MB, mostly `ccache`; `math/` holds `builtin-math-integration.patch` and the stage-zero and stage-one recovery zips).
- Empty dirs `.worktrees/builtin-math-stageone` and `.worktrees/builtin-math-stagezero`.
- No build dirs remain in either worktree (`build-math*` already removed).

## 7. Risks and what went wrong (and how it resolved)

- Math-caused asset regression (layout-work limit). First #802 cache was insufficient: review showed the value encoder still validated fully before the cache. Repaired with a preflight; also caught a retained-schema regression. Resolved in the merged 4-file #802.
- Phase collision: results' `OptionalWrapPhase=72` equals math's `MathAssignBegin=72`. Resolved by reserving 78; remap still not applied anywhere.
- Inherited failures on the #694 pin (not math): asset packaging expected 25 sources vs 27 (fixed upstream in `a16788d1c`); `--record-compatibility` line 97/213; `--record-values` exit 139. Both record cases pass on accepted main.
- Bootstrap cannot reuse the checked artifact once `Math.chibi` is in the compiler package. Used snapshot-only construction (asset-host OFF) and an experimental artifact. The checked files were never changed.
- GC-pressure test: a first run double-initialized the fixture (lifecycle fix); then the WRITE-summary mismatch above.
- Strict build exposed 34 math sample rows missing `diagnostic`; fixed without changing oracles.
- `sin` test assumed libm; the VM deliberately uses a deterministic approximation. Test now uses quarter-turn inputs. Do not change the VM's precision contract here.
- Process hazards for you: stale `HANDOFF-codex-worktrees.md` classification (section 0); #783 head moved without this log knowing; local `origin/main` is an unfetched ref; the Codex app server is still running, so stop or confirm every Codex thread (esp. coordinator `01a180b0`) before editing shared worktrees. `CLAUDE.md` and your own norms say commit or push only when the owner asks.
- Open unknowns: whether #783 should be updated or replaced; who owns native LLVM lowering for math; whether the coordinator will keep sequencing now that its Codex session may be gone.

## 8. First 30 minutes

1. `git -C /Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi status --short | wc -l` (expect 44) and `git diff-index --cached --quiet e0731ca6 && echo ok`. Confirm no Codex thread is still writing there; ask the owner.
2. Read, in order: `docs/chibi-shaders/builtin-math-stage.md` (primary worktree), `.chibi/math-main/runtime797-write-prep/validation-plan.json`, `/Users/cwaldner/Personal/chibi/.coordination/chibi-landing-ledger.md` (math rows near lines 66 and 85 to 92), #783 body and `pull/783#issuecomment-6461900701`.
3. Ask the owner three things: commit the staged tree to a local branch; #783 update vs replace; who now plays coordinator (producer slots, landing order).
4. `gh pr list --repo waldnercharles/chibi --state open` and `gh api repos/waldnercharles/chibi/commits?sha=main` for fresh main. Re-run the path-overlap check (`git diff --name-only 79743c567 <main>` vs `git diff --cached --name-only`).
5. Re-audit reserved ordinals (TypeDecl 12 to 15, Work.phase 72 to 82, completion 8/9) against current `tools/compiler/package_compiler/*.chibi`.
6. Create a rebased candidate, port the #783 f32-max fix, then run the lightweight replay in section 6 in a fresh snapshot dir. Do not start any producer.
7. Record results in a new `.chibi/math-main/<name>/` dir. Never overwrite the 554 protected files.
