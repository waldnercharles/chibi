# Handoff 09: Core math, collections and reachability integration

Codex session `01a180c6-be08-7341-b87a-7ef9b8ce2c90` (agent-created thread, parent/coordinator `01a180b0-3773-78a1-beeb-03420dfa7a21`). Log: `/Users/cwaldner/.codex/sessions/2026/10/27/rollout-2026-10-27T22-31-10-01a180c6-be08-7341-b87a-7ef9b8ce2c90.jsonl` (33 MB, idle since 10-28 21:18 MDT; no process used any of its worktrees at 22:50). Written 2026-10-28 ~22:55 MDT from the log plus read-only git/gh. Times are MDT; log timestamps are UTC (MDT = UTC-6).

Three things to know first:
1. `core:math`, `core:math/ease`, the analytic spring and the C deletions are written, reviewed and pushed but NOT landed and NOT validated on current main. Math SDK publication is under an explicit owner hold. Do not merge or publish it.
2. The compiler-reachability half of this thread is done: waldnercharles/chibi#817 merged as `a253ef4b5`. The Math forward port sits on `a253ef4b5`, but main is now 25 commits further (`d66cb3beb`, build protocol 14, CMake retirement).
3. A reachability archive/cleanup task was stopped mid-way (restore verification failed). Originals are intact. Delete nothing in the reachability worktrees or `/tmp/p790-hqmks0bc*`.

## 1. Goal and scope

Brief from the coordinator (verbatim, trimmed): "Own preserved core math recovery, primary-source spring/interpolation research, and a proposed production math C-to-Chibi conversion. The user agreed to recover existing math before another implementation and specifically asked whether Odin or Rust offer implementations to learn from." Recover the frozen candidates `origin/codex/core-math-main-20261024` (`90e0936e9`) and `origin/codex/core-math-inverse-lerp-20261024` (`d039de3ee`); "do not silently clear a hold"; move suitable higher-level policy from `src/runtime/math.c` into Chibi, keep libm primitives narrow, migrate callers so the old C bodies leave. Rhythm is not ported; visual motion and UI spring behavior are separate threads.

Owner decisions, all given in chat on 10-27 around 22:40 on the plan (the first turn only proposed and stopped):
- On the reference table (Odin `core:math`, Odin easing, Bevy `smooth_nudge`, Rust analytic springs / Juckett): "I would love to implement all of these in Chibi".
- On "replace the existing implicit spring response with the analytic response" and "replace high-level behavior math nodes/native spellings with imported `core:math` calls": "Agreed." Final: "Looks good to me."
- On the compiler prerequisite (normal asset builds compiled every unused core package against a catalog with no math capabilities): "Include the reachability fix here".
- Relayed by the coordinator: the human said "Yes, coordinate all ten chats" and "Work as autonomously as possible please. You may commit, open PRs, review, and merge on my behalf." That was said to the Codex threads. Confirm it carries over to you before you push or merge. Coordinator also relayed that the user's latest assessment "emphasizes actual C removal" and that the user authorized cleaning finished worktrees.
- Coordinator rule: "do not label first-order smooth_nudge a second-order spring."

What was built (`/Users/cwaldner/Personal/chibi/.worktrees/math-collections-integration/chibi`, docs in `docs/core-math.md`):
- `packages/core/math/{Math,Spring}.chibi` plus tests: 42 exports total. Interpolation, InverseLerp (unclamped, raw f32 rounding from the frozen candidate), Remap, SmoothStep, Step, angle helpers, first-order exponential damping, analytic spring (underdamped/critical/overdamped) that keeps position and velocity across retargeting and does not snap-settle.
- `packages/core/math/ease/Ease.chibi` plus tests: 31 progress curves.
- Public numeric type is `number` (= f32). No f64 specializations; explicit narrowing only. Precision-polymorphic exports wait on generics (waldnercharles/chibi#810, draft).
- `math.Step(value, edge)` differs from the builtin lowercase `step(edge, value)` (builtin-math thread owns that builtin). Omitted `fmod` divisor defaults to 1 for both widths.
- Deleted: the C spring/lerp policy in `src/runtime/math.c` (-142), behavior-graph math nodes (`math_nodes.c` -101), registrations. Kept: thin libm sin/cos/fmod host calls; the engine's deterministic `sin/cos` approximation is unchanged.
- Reachability: compiler prepares only project roots plus imported collection packages (`source_package_prepare.c`, `project_compile.c`, Generate bootstrap bridge). Already on main via #817.
- Studio consumer fix (Outliner `step`): waldnercharles/chibi-studio#104, merged `be7709fd`.

## 2. Ownership

| Path (under `/Users/cwaldner/Personal/chibi/.worktrees/`) | Branch | Role |
|---|---|---|
| `math-collections-integration/chibi` | `codex/math-collections-integration-20261028` | PRIMARY. The live Math candidate. |
| `core-math-recovery/chibi` | `codex/core-math-recovery-20261027` | PRIMARY, frozen. Head of draft #791. Historical evidence and scratch `.chibi/math` (959 MB). |
| `reachability-collections-integration/chibi` | `codex/reachability-collections-integration-20261028` | PRIMARY, finished (#817 merged). In archive scope. Scratch `.chibi/integration` 2.0 GB. |
| `collection-reachability/chibi` | `codex/collection-reachability-20261028` | PRIMARY, superseded (#790 closed). In archive scope. Also #791's base branch. |
| `/tmp/p790-hqmks0bc`, `-cmake`, `-cmake-canonical` | n/a | External build outputs (598 MB, 47 MB, 48 MB), in archive scope. |
| `.worktrees/collection-fixtures` | n/a | Merged #800 helper; no longer a git worktree. Archived under `.coordination/archives/2026-10-28/collection-fixtures`. |
| Studio branch `codex/core-math-consumer-20261027` (`/Users/cwaldner/Personal/chibi/chibi-studio`) | merged as #104 | Worktree removed 10-28 20:06; bundle and manifest in `.coordination/archives/2026-10-28/studio-outliner-policy/`. Studio local main predates #104. |

Nested detached worktrees (registered in git, do not prune): `math-collections-integration/chibi/.chibi/integration/native-math-{308,artifact,focused}-snapshot` and `reachability-collections-integration/chibi/.chibi/integration/local-artifact-snapshot`.

Touched or read only: `portable-byte-libraries` (#795 owner), `builtin-math*`, `authored-strings-regressions` (#821), `generics-callables-integration`, `recoverable-errors-forward`, `cmake-retirement`, `host-clock*`. Never edit them. Shared checkout `/Users/cwaldner/Personal/chibi/chibi` (`codex/non-code-cleanup-20261027`, dirty) is not ours.

Overlap with other threads:
- Coordinator `01a180b0` (root): assigns producer slots, final-merges, retires superseded PRs, sequences compiler/bootstrap publication. You cannot self-assign a heavy build slot or merge.
- Portable libraries `01a180c5-6082` (waldnercharles/chibi#795, draft): owns Generate/Driver split between private compiler inputs and public SDK inventory, recursive core installation for every SDK profile, and `core:math/bits` (unsigned counts, reversal, byte swaps, rotations). Math owns everything else in `core:math`. Do not overlay its source roles.
- Builtin math `01a180c5-6950` (`builtin-math*` worktrees): vectors/matrices/quaternions and synthetic math builtins; owns builtin `step(edge,value)`.
- Strings/integers `01a180c6-c4ac` (#821): test dispatcher registrations overlapped once (resolved at #789). Allocation reporter `01a180c6-b988` (#788 merged): owns the `cost_explanations/Explain.chibi` facade input in Generate.chibi.
- Generics #810 and recoverable errors #813 (drafts): both touch `source_package_prepare.c` indexing, which #817 had to preserve (sparse graph-indexed package storage plus `needed[]`, compact only at the public enumeration boundary).
- CMake retirement (#814 merged, #818 open): Math still edits `test/language/CMakeLists.txt` and `tools/graph/engine/Targets.chibi`.

## 3. Current verified state (read-only, 22:50 MDT)

`git status --short` is empty in all four primary worktrees.

| Worktree | HEAD | ahead/behind local `origin/main` | Remote |
|---|---|---|---|
| math-collections-integration | `8d1b06209af599851928e55835a0df4a72e18da3` | 15 / 25 | pushed, same SHA, NO PR |
| core-math-recovery | `35208481b36c7f7224376b218149ec1e17c32195` | 10 / 91 | pushed, = #791 head |
| collection-reachability | `8f5685e021fca09b9a0edd827b10261e2b0ff1ca` | 5 / 91 | pushed, = #790 head |
| reachability-collections-integration | `0c22447fbe2a5c329271df7bc0d98a6dd0a52f10` | 0 / 26 | pushed, = #817 head |

Live main (`gh api`, equals local ref): `d66cb3bebf33ff3c4e21ef53e3f3990a67ecc60c` (#816, 10-28 21:42). Math merge-base with main is `a253ef4b5`. Files changed on both sides since then: only `tools/test_policy.py`. Checked compiler on main and on the Math branch: both sha256 `27d8f3d0568e442e1a70b167c04ba97c671ea0d0b1e85e3a7493d88fc59bb912` (20,792,064 B).

PRs (`gh pr view`):
- waldnercharles/chibi#791: OPEN, draft, MERGEABLE. Head is the frozen pre-collection `35208481b`; base is `codex/collection-reachability-20261028` (the closed #790 branch, not main). All 7 checks SKIPPED, so no CI has ever run on either Math head. Body already documents the forward port `98c3b22a8` / `8d1b06209`.
- waldnercharles/chibi#790: CLOSED (superseded by #817), CONFLICTING, 6 failures retained as history.
- waldnercharles/chibi#817: MERGED `a253ef4b5` 10-28 18:57. 11 success, 4 skipped.
- waldnercharles/chibi#800 (fixture prerequisite authored here): MERGED `afcd777ba`.
- waldnercharles/chibi#795: OPEN draft; "Graph SDK packaging and relocation (Linux)" currently FAILS. Math depends on it.
- waldnercharles/chibi#818 (authored response files for long graph links): OPEN, ready, 11 success.
- waldnercharles/chibi-studio#104: MERGED `be7709fd`.

Disagreements with the log or the coordinator list:
- The worktree list marks `reachability-collections-integration` "merged: clean up" and `collection-reachability` "LIVE". Both are in a stopped archive plan. Cleanup has not happened and must not yet.
- The log's last Math base (`a253ef4b5`) and "accepted main" mentions (`79743c567`, protocol 14) are stale. Main has since gained #814 (Linux `make build` defaults to the Chibi Graph) and #816.
- The log says #791 was "updated". Only its body was; its head is still the old frozen checkpoint. The real candidate has no PR.

## 4. Done so far, and what is left

Done (evidence in the repo, not just the log):
- 10-27 22:36 plan approved. 10-28 00:17 implementation complete in `core-math-recovery`. Native, installed-SDK, Wasm and ASan/UBSan math checks passed on that candidate: 29 authored tests per backend, 6 cases / 4,132 assertions, package-imports 1,484, full language group. Receipt: `core-math-recovery/chibi/docs/evidence/core-math-20261027/RESULTS.md`.
- Generated-C alias failure (`--c-export`, unresolved `behavior_function_host_arguments`) was reproduced on the clean base `4981e9733`: pre-existing, not ours. Evidence `core-math-recovery/chibi/.chibi/math/baseline-classification/`. Two tools-group subprocess timeouts passed on focused retry.
- Split into #790 (reachability) and #791 (math). #790 CI failed: reviewed-artifact parity, plus compiler startup 24.19 s versus 0.42 s baseline. Diagnosis: the host re-recovers the changed compiler against the older checked artifact. A matching scratch artifact restored 0.42 s. Remedy was a reviewed artifact refresh, not a limit change.
- Authored and landed the 18-file fixture prerequisite #800 (pin_string designated initializers, strict-Wasm fixes, import-fixture managed heap, snapshot canonical-empty oracle), with native controls 14/1292, 16/286093, 7/230.
- Reachability regenerated on accepted Body #807 / main `51650ccff`: source `d1315047c`, recovery 216.35 s, bootstrap fixture 1 case / 31,262 assertions (398 s), unmodified production writer, pair installed at `bad6bd81d`. Unmodified normal SDK build 116.31 s; 91 cases / 305,391 assertions; Graph build 192.3 s at a short output path (194.5 s FAILED at the original long path: 502 args / 71,540 bytes over the 64 KiB link limit, handed to the CMake owner, now #818). Replacement PR #817 merged after two independent reviews and three-platform self-build.
- Math forward port onto `a253ef4b5`: `98c3b22a8` (tree `e0364b21d319509699c73eec375c9b24b10516f7`), evidence carrier `8d1b06209`. Independent review: all 33 Math paths preserved, all 27 compiler inputs and the artifact pair unchanged. Source-only; no build run on it.
- Best execution evidence is older: clean `b87afaea2106dd8f56ff0ededc6d4604151096e5` on runtime #797 (`30837bd5`). Native 8 selectors: 59 cases / 329,241 assertions. Clean Wasm: 42 cases / 308,078. Spring benchmark: 20,624 to 128 wrapped allocations per 2,048-step SLJIT trial, lerp loops 0. Receipt: `RUNTIME-308.md` in `math-collections-integration/chibi/docs/evidence/core-math-20261028-integration/`.
- Hindsight: the thread ingested several documents (core math candidate, collection fixture ownership, native format-26 checkpoint, runtime-308, E1 reachability regeneration). Search curated pages for those terms before redoing analysis.

Next actions, in order:
1. Re-sync: merge `origin/main` (`d66cb3beb` or newer) into `codex/math-collections-integration-20261028`. Expected conflict surface is `tools/test_policy.py` only, but protocol 14 changed build-host code, so re-check `Targets.chibi` and the `test/language/CMakeLists.txt` hunks still build under the Graph. Prove the 27 compiler inputs and `compiler.chibip` hash are unchanged; do NOT regenerate the compiler. Record the new boundary like `ACCEPTED-817.md` and keep older evidence attributed to its own source.
2. Wait for waldnercharles/chibi#795 (and whatever fixes its Linux Graph SDK failure) to reach main, then merge it. Until then ordinary Generate would send 33 sources (27 + the 6 Math/ease files) to a 27-input compiler, so exact reuse misses (status 3) and the normal build cannot pass.
3. Ask root for a heavy producer slot, then validate the exact combined tree (see section 6): focused native and Wasm, normal Graph/asset build, installed and relocated SDK importing `core:math`.
4. Replace #791: the real candidate has no PR. For #790 the coordinator said to open a clearly linked replacement and let root retire the old one; presumably the same here. Ask root before retargeting or force-pushing. Do not delete the remote branch `codex/collection-reachability-20261028` (it is #791's base).
5. Get the explicit owner release decision before any SDK publication.
6. Finish the reachability archive (section 7) only when root says the validation quiet boundary is open.
7. After Math lands: remove Math scratch, nested snapshot worktrees and the fixture/benchmark leftovers; report what was left.

## 5. Holds, freezes and coordination rules (exact)

- Math SDK publication hold: `docs/core-math.md` says "SDK publication requires an explicit release decision". It stands unless the owner supersedes it in this thread. Passing #817 does not clear it; coordinator wording: "do not merge or publish Math merely because pure reachability is accepted."
- Historical hold in the frozen candidates (docs/core-math.md in `90e0936e9`, lines ~119-138). Today's main implements explicit f64 to number narrowing, but fresh execution is what clears it, not source inspection.
- Final merge and producer-slot assignment belong to root `01a180b0`. Last words to this thread: "Math stays frozen", "Producer, publication and cleanup holds remain in place", "no competing heavy SDK/compiler producer". Before starting a full compiler self-build, asset generation or sanitizer compiler matrix, request a slot. Resource scheduling only: no test-duration ceiling, no cancelling healthy runs.
- Frozen identities, never rewrite or force-push: #791 head `35208481b`, #790 head `8f5685e02`, #817 head `0c22447fb`, Math source `98c3b22a8`, carrier `8d1b06209`, clean runtime checkpoint `b87afaea2`, preserved refs `90e0936e9` and `d039de3ee`. Do new work as new commits.
- Admitted compiler/runtime combination: checked compiler sha256 `27d8f3d0...59bb912`, E1 runtime pin sha256 `e1adf32b577f9a20993e9db40d4c3777d2b3a8325d1bb8a19a57bf2256f23348`, prepared format 26, bootstrap version 7 (minimum 6), 27 ordered compiler inputs (26 compiler files plus core strings), six target-fact rows. Main is at build protocol 14 (#803) with these unchanged and no new seed. Do not add runtime seeds, regenerate the checked pair, or widen limits (asset generation 240 s, Linux SDK CompilerTools 120 s, Graph 64 KiB link command).
- Attribution rules from the coordinator: never transfer old validation to a changed tree; keep failed runs as failed; keep unit-fixture proof separate from current-source compiler provenance; report proposed / implemented / merged / verified separately; a source-only merge claims no runtime result.
- No unpublished feature overlay: no #795, #810, #813, records, results, clock or capability code enters a Math/reachability candidate except through accepted main. Do not copy #795's Generate/Driver changes early.
- Isolation of the focused-driver proof: the only allowed tweak was `if(FALSE AND TARGET Chibi::asset_compiler_host)` in `test/language/CMakeLists.txt` (patch sha256 `69062a48...e108e163`), on a private snapshot, with all 3,473 tracked files otherwise compared. Never present it as an ordinary build.
- Cleanup rules: no global prune, `git clean`, ref deletion, or shared vendor-cache cleanup (`/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party`). Keep everything leased intact until root releases it. The reachability worktrees and `/tmp/p790-hqmks0bc*` outputs (section 2) are under "no removal" until a restore passes independent verification.

## 6. How to validate

Repo rules that bite (`/Users/cwaldner/Personal/chibi/chibi/AGENTS.md`; `CLAUDE.md` is a symlink): new behavior in Chibi, not C; delete obsolete C instead of keeping aliases; pico_unit case in the owning dispatcher (`test/language/main.c`) for each behavior change; new native function/component either in a `test/fixtures/feature-coverage` scene or in `coverage-gaps.txt` (Math added 4 lines there); no `CHIBI_PROJECT_VERSION` bump (none needed: "No prepared-format, ABI or project version changed"); clang-format 22 (`/opt/homebrew/opt/llvm@22/bin/clang-format`) and `chibi_ctl --format` / `chibi-compile --project-root packages/core/math --format --check`; review the diff before each commit; run test-wasm before landing (strict `-Wmissing-field-initializers`); do not use `git stash` for before/after comparisons (shared stash list); delete scratch binaries and report leftovers; Build.chibi must parse on older build hosts.

Focused selectors (all via `python3 tools/test_policy.py native <test_language> --<sel>`): `--package-math` (6 cases, 29 authored tests per backend), `--package-compiler-chibi-imports`, `--package-compiler-chibi-imports-compiled`, `--package-imports`, `--package-snapshot-chibi`, `--record-publication`, `--native-calls`, `--package-math-benchmark` (opt-in). Tool aliases: `language-package-math`, `language-package-math-benchmark`, `wasm-package-math`. Wasm set: package-math, `--row-array`, `--package-snapshot-chibi`, both compiler-import modes (use `node <wasm>/test_language.js --<sel>`; the manual `--package-imports` is SLJIT-only and not Wasm-admitted; `--packed` is not a flag).

Build used by the thread (task-private dir, Clang 22.1.8, Release, 4 jobs): `cmake -S test/language -B <dir> -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm@22/bin/clang -DCHIBI_SDK_BUILD_COMPILER=ON -DCHIBI_SDK_BUILD_DESKTOP=OFF -DCHIBI_SDK_BUILD_NATIVE=OFF`, then `cmake --build <dir> --target test_language --parallel 4`. Ordinary build was `make build BUILD_DIR=... BUILD_PROFILE=release BUILD_HOST=... ASSET_GENERATOR_HOSTS=... JOBS=4 BUILD_TARGET='test_tools test_language chibi_ctl chibi'` (pre-protocol-14 invocation; re-read `Makefile` and `docs/testing.md` after merging main, and prefer `make build` / `make agent-test AGENT_SELECTOR=--package-math` per AGENTS.md). Installed-SDK check: `cmake --install`, then configure `test/language` with `-DCHIBI_INSTALLED_SDK=ON -DCMAKE_PREFIX_PATH=<prefix>`; `CHIBI_CORE_MATH_ROOT` selects the math sources and alone proves nothing.

Run on which tree:
- Ran and passed on early frozen sources only (`35208481b` lineage): native/materials/projects/language groups, installed SDK, Wasm math, ASan/UBSan math + import retention.
- Ran and passed on `b87afaea2` (focused only): the 8 native selectors, Wasm 3 selections.
- NOT run on `98c3b22a8`/`8d1b06209` or anything newer: everything. Never run anywhere: Windows, full Wasm suite, GPU/Studio interaction, complete installed/relocated SDK on the 33-source tree.
- Required before landing (AGENTS.md): `make test-native`, `make test-materials`, `make test-projects`, `make test-language-all`, `make test-tools`; CI also runs three-platform compiler self-build.

Slow or flaky: bootstrap fixture 400-580 s; compiler recovery ~216-230 s; the first "fixed-point" attempt failed only because the optional `CHIBI_BOOTSTRAP_SEED` export directory did not exist; Wasm compiled-compiler lane previously hit "Wasm function exceeds compiler limits" (fixed by #807); `/tmp` versus `/private/tmp` canonicalization breaks Graph reference comparisons on macOS; three heavy producers on a 14-core host collide, hence the slot rule.

Scratch left behind (report, do not silently delete): `core-math-recovery/chibi/.chibi/math` (959 MB), `collection-reachability/chibi/.chibi/reach` (513 MB), `math-collections-integration/chibi/.chibi/integration` (796 MB, incl. three nested snapshot worktrees, Wasm builds, receipts), `reachability-collections-integration/chibi/.chibi/integration` (2.0 GB), `/tmp/p790-hqmks0bc*` (693 MB), archive tree under `/Users/cwaldner/Personal/chibi/.coordination/archives/2026-10-28/reachability-817-790/` (bundle 191,856,970 B, sha256 `5a15b20f...ce55d`).

## 7. Open questions, risks, incidents

- Who is coordinator now? Every merge/slot decision went through Codex root `01a180b0`. Ask the human whether you report to it, to them, or to a Claude replacement.
- #791 disposition: retarget, or replacement PR from `codex/math-collections-integration-20261028`? #791 has never had CI (stacked on a non-main base).
- Math cannot pass the ordinary builder until #795 lands (33 vs 27 sources). Its own Linux Graph SDK check is red. Decide whether to wait or to ask root for an isolated "private compiler vs public SDK" source-role packet (one exists, per root; never imported).
- Windows and PowerShell paths for Math are unexecuted.
- Archive incident (10-29 03:18Z / 21:18 MDT): payload tars and the ten-root self-contained bundle were captured; the independent restore failed strict xattr comparison because 7,741 restored entries gained `com.apple.provenance` (13 are ignored data, so avoiding Git checkout is not a complete fix) and 1,184 directory mtimes differed. `removexattr(NOFOLLOW)` returned success but the attribute stayed in readback. The coordinator forbade waiving the comparator, stripping source metadata, or repacking. Status: removal STOPPED, nothing removed. Needs a recovery path that restores exact attributes or an explicit root/human decision; read `.coordination/archives/2026-10-28/reachability-817-790/STATUS.md`, `execution-stopped.json`, `metadata-probes/`. Plan: `reachability-817-790-archive-plan.md` (reviewed hashes `9c7fc188`, `b2a6d9d7`, review `f8492438`).
- Earlier incidents, resolved: reserved word `state` as a parameter name broke asset generation; build host lacked libm/assert capability packs (reachability removed the need); test helper missing activation storage crashed both compiler import lanes; `fmod` default divisor mismatch between widths; ordinary build timing out because of artifact mismatch (not a reachability cost); #790 CI artifact parity; Graph link-command size.
- Numerics: engine VM `sin/cos` is a deterministic float rational approximation (`support/cos_sin.h`), not libm; native/shader parity for Math is still pending.
- Memory-rule tension: your auto-memory says "merge own PRs when green"; this thread's rules reserve final merge to root and add publication holds. The holds win.

## 8. First 30 minutes

1. Read this file, then `AGENTS.md`, `docs/core-math.md`, `docs/evidence/core-math-20261028-integration/{ACCEPTED-817.md,RUNTIME-308.md}`, and `.coordination/archives/2026-10-28/reachability-817-790/STATUS.md`.
2. Confirm the Codex thread is stopped (`ps -A | grep -E 'math-collections|core-math|p790'` empty at 22:50). Ask the human who coordinates and whether the "commit, PR, merge on my behalf" grant covers you.
3. Re-verify with `GIT_OPTIONAL_LOCKS=0 git -C <wt> status --short; git -C <wt> rev-parse HEAD; git rev-list --count origin/main..HEAD`, then `gh pr view` for #791, #795, #817, #818 and `gh api repos/waldnercharles/chibi/commits/main`. Expected: clean trees at the HEADs in section 3; main may have moved.
4. In a new session, search Hindsight curated pages (core math, native format-26, e1 reachability).
5. Do not run builds or tests yet. Decide with the human: wait for #795 or request a source-role packet; replacement PR vs retarget #791.
6. When cleared, start from `math-collections-integration` (not `core-math-recovery`): merge main, rerun the compiler-input and artifact-hash check, then request a producer slot.
7. Leave the archive task and every `/tmp/p790-hqmks0bc*` path untouched until root confirms a safe recovery method.
