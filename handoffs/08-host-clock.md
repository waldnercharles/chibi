# Handoff 08: Host clock (clock-first, host-controlled grants)

Written 2026-10-28 ~22:45 MDT from the Codex session `01a180c6-bb3b-7f71-9e1c-e7b430e4a12a` (log ends 19:45 MDT; no live process uses its worktrees). Log: `/Users/cwaldner/.codex/sessions/2026/10/27/rollout-2026-10-27T22-31-10-01a180c6-bb3b-7f71-9e1c-e7b430e4a12a.jsonl` (only copy). Log timestamps are UTC; add -6h for MDT. "Claude" in that log means Charles's Claude sessions (owners of #694, #775, #797), not you. "Root"/"coordinator" is the Codex thread `01a180b0-3773-78a1-beeb-03420dfa7a21`.

## 1. Goal and scope

The coordinator delegated: "Own the standard-library prerequisite for imported host capabilities, resource cleanup and target availability", with a small first vertical slice and a plan-only first turn. The owner approved the plan in this thread (2026-10-27 22:49 MDT), verbatim:

> "Approved. Proceed with the clock-first plan, host-controlled grants, and macOS/Linux first. Use an isolated worktree and coordinate shared compiler changes with Claude and the result contract with the results chat."

The only other owner turn here was a question answer: result parity (optional types and named results in the compiler) stays with the results chat ("Keep result parity with the results chat (Recommended)"). Second-hand, via the coordinator (2026-10-28 00:23 MDT): the owner said "Yes, coordinate all ten chats" and "Work as autonomously as possible please. You may commit, open PRs, review, and merge on my behalf." That authority was then narrowed by the sequencing holds in section 5. Re-confirm it with the owner before merging anything.

The approved design (`docs/host-packages.md` in the forward worktree is the contract):
1. Host capabilities become importable source packages: `import watch "host:clock"`. A native kernel keeps its canonical name (`host:clock.Sample`) and is callable only from its owning package. Both compiler stages (stage zero C, stage one authored) resolve it; no prepared-format change.
2. Registration permits compilation. Only the embedder's `HOST_CLOCK_SERVICE` binding holding an initialized `host_clock_grant` permits execution. No binding gives a recoverable "denied" status. Not an OS sandbox.
3. A narrow C sampler (`CLOCK_UPTIME_RAW` on macOS, `CLOCK_MONOTONIC` on Linux). Windows and browser report "unavailable"; hosts omit the package there.
4. Portable `core:time` (`packages/core/time/Time.chibi`): `Instant`, `Duration`, exact/float conversions. No engine globals.
5. Public `Now() -> (instant: time.Instant, error: ClockError?)` and a checked `Between`. NOT implemented; blocked on the results contract.

Not started: read-only filesystem (size-limited regular files, owned bytes), processes and cancellation, networking, Windows/browser clocks. Hindsight initiative page "Imported host packages and monotonic clocks" was captured; search it first.

## 2. Ownership

| Worktree | Branch | Role |
|---|---|---|
| `/Users/cwaldner/Personal/chibi/.worktrees/host-clock-forward/chibi` | `codex/host-clock-forward-20261028` | PRIMARY. Current source, all evidence in ignored `.chibi/clock/`. Local only, never pushed. |
| `/Users/cwaldner/Personal/chibi/.worktrees/host-clock/chibi` | `codex/host-clock-20261027` | PRIMARY for PR #784 only. Frozen at `76615d5fc`; pushed. Old base (`7f35662ac`). |
| `.worktrees/graph-discovery-fixture` | `codex/graph-discovery-fixture-20261028` | Was primary for #804 (merged). Worktree removed; branch kept; 124 evidence files archived at `/Users/cwaldner/Personal/chibi/.coordination/archives/2026-10-28/graph-discovery-fixture/` (README.md maps old paths). |
| `retire-rhythm-motion`, `odin-package-foundations`, `main-runner-reload`, `recoverable-errors-forward` and others | | Only appear in process listings or were read. Do not touch. |

Overlap with other threads (shared files and seams):
- Coordinator (root): owns checked bootstrap, runtime pin, default SDK publication, final merge, heavy-producer slots. You own none of those.
- Recoverable errors (#813): owns optional/named-result parity and public three-result compiler helpers. Both of you touch `tools/compiler/package_compiler/{Body,Steps}.chibi`. Their `current-*` packets in `/Users/cwaldner/Personal/chibi/.worktrees/recoverable-errors-forward/chibi/.chibi/errors/` are source context only.
- Generics (#810) and records (#808): also edit `Compile/Body/Steps/References`. Records move to Prepared 27 (relink needed).
- Portable libraries (#795): owns recursive all-core source install in the SDK and `packages/core/`; clock adds `packages/core/time` and a 74-export SDK contract.
- Math (#783/#791): authored the common fixture fix #800. Phases 72-77 are theirs.
- cmake-retirement/runtime-sdk: Graph is now the default Linux build (#793, #814); `tools/graph/engine/{Targets,LinuxLinks}.chibi` and `sdk/CMakeLists.txt` are shared.
- Strings/integers (#821), core-math/collections (#817 merged), language-UX/main-runner-reload: no file overlap seen. The main-runner-reload lane ran an external Graph build during C1; never kill other lanes' builds.

## 3. Current verified state (read-only, observed now)

`origin/main` = `d66cb3bebf33ff3c4e21ef53e3f3990a67ecc60c` (#816, 21:42 MDT; matches `gh api`). Local refs were not fetched by me beyond a dry run.

| Item | Observed | Log/ledger claim | Agrees? |
|---|---|---|---|
| forward worktree | branch `codex/host-clock-forward-20261028`, HEAD `102c148f9fe264fddd79c18b5dd71c81de1aed14`, 8 ahead / 60 behind `origin/main`, 2 tracked files modified | same | yes |
| uncommitted diff | `test/language/CMakeLists.txt`, `test/language/host_packages_compiler.c`; `git diff --binary` sha256 = `5ee949b366480ef1f732d200e573896e8001561c7a46ba973132349efd29db51` | patch `5ee949b3` uncommitted | yes. Backups: `.chibi/clock/installed-c1/fixture-path-routing.patch` (same hash) |
| forward remote branch | does not exist on GitHub (404) | "has not been published" | yes |
| host-clock worktree | clean, `76615d5fc`, 4 ahead / 98 behind | frozen at `76615d5f` | yes |
| PR waldnercharles/chibi#784 | OPEN draft, head `76615d5fc`, **CONFLICTING** with main, 11 checks SUCCESS / 8 SKIPPED (historical, old head), no reviews or comments | "frozen, CI green" | green yes; conflict is new |
| #804 (Graph fixture, clock-authored) | MERGED as `dafe746411182c3320281c0b1fe4e75e7b9b6456` | merged | yes |
| #813 (results, now `@error` on types) | OPEN draft, head `e902fcf3c`, CONFLICTING, 8 failing checks | "revised, not admitted" | yes |
| #795, #801 | OPEN drafts | held | yes |
| #694, #797, #802, #803, #807, #817, #814 | MERGED. #790 CLOSED (superseded by #817) | | yes |

Forward branch composition: tested clock commit `22732d1341dc406046976046eecd346821a3a6ad` ("Add clock bindings with host grants and portable time values", 26 files, +1617/-33, on accepted #694 `7c6ca2f5d`), `1df1789ff` (Graph link order, one line in `Targets.chibi`), plus merges of #797 runtime, #799, #800, #804 (`dafe74641`), tip merge `102c148f9`. Patch identities: clock-only diff `768c5dc02c0a85cf7aa110c4057aa209e3d1397b10025e7281aafe749d1211d6`; original freeze patch `073585b62536d3d7f0491df2af29131d280da2b184c0928986db062faa387ec4`.

Drift since the forward base (`dafe746`): main took nine more PRs (#787, #802, #807, #811, #815, #817, #803, #814, #816). Files changed on both sides: `sdk/CMakeLists.txt`, `src/authoring/source_package_prepare.c`, `tools/compiler/package_compiler/Body.chibi`, `tools/compiler/package_compiler/Compile.chibi`, `tools/test_policy.py`. Main's `tools/compiler/runtime.json` is now `compiler-runtime-prepared26-43eec64f98e6fb288935dacfcbf86296dbc43b02` (E1, from #802). The forward branch still has `9e9c374a` (sha256 of the file = `75fc6e2b260600b3877b0bd3a2068902bb050c23210da2d1061c43fcc0f63289`, the "75fc" in the log). So every clock proof predates E1, compiler `27d8f3d0…` (#817), and build protocol 14 (#803).

## 4. Done and left

Done (evidence in `.chibi/clock/HANDOFF.md`, index `installed-c1/README.md`; all local macOS unless noted):
- Design, prior-art review, native sampler, grants, host-package import machinery, `core:time`, SDK export `host_clock`, Graph registration for macOS and Linux (draft #784 opened 2026-10-28 00:08 MDT).
- Old head `76615d5fc` hosted CI green: Linux/macOS native incl. real clock (9 cases/2466 assertions), stage-one clock 1/115, three-platform compiler self-build with refreshed bootstrap artifact (version 8, min 2), SDK packaging, Studio startup. Windows and browser clock not run (explicitly unavailable).
- Tested forward source `073585b6` on #694 main: native 10 cases/2509 assertions; compiled stage-one clock 1/153 on Bytecode and SLJIT; compiler fixed point 1/30353; ASan+UBSan native 10/2509 and stage-one 1/153; installed SDK consumer; format.
- Found and fixed a stage-one vs stage-zero lookup mismatch (ambient `Sample` beat the private kernel). Fix is a single-pass private-name precedence in `ExactNativeKind/ExactNativeName/NativeCandidate/Steps`, with a mixed-target regression on both VMs.
- Authored and landed #804 (Graph test hard-coded `/bin/false`); consumed #797, #799, #800.
- Graph (protocol-13 private host): compile parity 1007/1007 commands, link parity 13/13 executables and `build-graph-programs` 8 cases, run at the `1df1789ff` link-order fix; `build-graph-generated` 9 cases/211 assertions (112.4 s) on `102c148f`.
- Fresh SDK from `102c148f` + patch `5ee949b3`: configure 2.3 s, build 370 s, strict package 22 s; 74 exports/459 files; installed C1 `language-host-packages-compiler` 1 case/153 assertions (143.9 s) with checkout compiler/core hidden, plus two negative controls. Receipt `installed-c1/result.json`. Build ID `ccd712ca…3134f3`.
- PR #784 description rewritten to point to the forward checkpoint and holds.

Left, in order:
1. Commit patch `5ee949b3` locally as its own commit (it is the only copy besides the backup). Record "102c148f + 5ee949b3" attribution; do not call it clean 102.
2. Ask the owner/coordinator for admission: which agent is coordinator now, and whether the clock forward port may start against `d66cb3be` (needs a producer slot, section 5).
3. Port forward onto current main: resolve the 5 overlapping files; keep the private-lookup seam and `OptionalWrapPhase78`/other reservations; add Linux/macOS Graph registration against the new Graph profile; rebuild all C objects, hosts, headers against E1/protocol 14 (do not mix ABI-26 binaries).
4. Wait for an admitted results contract (#813 is conflicting and red). Then implement public `Now`/`Between` using the final spelling and migrate the `rejects()` helper in `test/language/host_packages_compiler.c` to read `CompileWithEffects`' final optional diagnostic from the physical three-result headers (leading `compiler_backend` parameter; keep `CompileClosure` byte entries). Expected shape (inference from #813, verify): `ClockError` marked `@error`, no function `@fallible`. The older `@fallible pub Now :: …` spelling is obsolete.
5. Re-prove on the combined tree: native clock/import, stage-one compiled clock, compiler fixed point, sanitizer, Graph parity, fresh SDK + installed C1, and Linux via hosted CI of the published candidate (no local Linux/Docker).
6. Coordinator regenerates the checked bootstrap once on then-current main; then publish (decide: force-update `codex/host-clock-20261027`/#784 or open a new PR) and land atomically.
7. Later slices: read-only files, process, networking, Windows/browser adapters.

## 5. Holds, freezes, coordination rules (do not violate)

- Remote #784 source stays frozen at `76615d5f` until the coordinator's integration plan admits the forward source ("Keep the remote feature source frozen until its normal integration plan admits it"). Editing the PR description is allowed. Do not push the forward branch before admission.
- No API workaround: "Public Now/Between still waits for the approved results contract; no wrapper/API workaround." Do not add a scalar `ClockError`-with-`None`-code fallback or any generic `Result`, exceptions or propagation syntax. Do not use the obsolete function `@fallible` marker ("Do not overlay that obsolete planned annotation").
- Proof attribution: the C1 proof is valid "only for its original frozen source and pin" (runtime pin sha256 `75fc…`). Never present it as proof for E1 (`43eec64f`, manifest `e1adf32b…`), compiler `27d8f3d0568e442e1a70b167c04ba97c671ea0d0b1e85e3a7493d88fc59bb912`, protocol 14, or accepted main `79743c56`/later. A passing check on one tree is not transferred to another.
- Bootstrap, prepared format, runtime pin, default SDK publication, final merge: coordinator-owned and serialized. "One final bootstrap regeneration on then-current main." Do not regenerate `tools/compiler/bootstrap/*`, edit `tools/compiler/runtime.json`, or bump Prepared. The old branch's bootstrap artifact (version 8/min 2, `f5acd4fb6`) is historical; the forward branch deliberately excludes bootstrap files. Forward branch targets Prepared 26 and bootstrap minimum 6.
- Heavy local producers (compiler self-build, asset generation, sanitizer compiler matrix, SDK build): request a slot from root first. The host has 14 cores and overlapping runs caused timeouts. Do not cancel other lanes' healthy runs. Focused C checks, edits and review are unrestricted. No global timeout ceiling, timing quarantine or guard increase was added; do not raise the 256 Mi authored-layout work limit or other guards (SDK asset outer limit 600000 ms; bootstrap 2e9, clock compile 2e8, call 2000).
- Unadmitted work stays out: #801 (capability-effect/ABI/seed hold, argument-effects metadata), #795, #808, #810, #813, #783/#791. No merges, cherry-picks or dependencies on them ("Do not bring held code into an integration PR").
- Reserved constants: `OptionalWrapPhase78` (Work.phase 78) is reserved for the optional-wrap integration; math owns phases 72-77; generics 80-82; TypeDecl `Optional12/Collection13/Char14/Math15`. The clock claims no new ordinal. After any merge into `tools/compiler/package_compiler`, audit that no two `*Place`/`*Decl`/storage/intrinsic constants share a value (#694 comment `6461212824`).
- Landing order from #694's sequencing comment: shared compiler hooks one workstream at a time, each regenerating the bootstrap once. #694 is merged and keeps no files reserved. #795 had the first feature lane.
- Merge authority: your memory note says merge own PRs when green, but #784 is under the coordinator's sequencing and every explicit hold. Ask the owner before any merge.
- Do not touch `chibi-examples`, `chibi-voidweaver`, or Studio.

## 6. How to validate

Repo rules that bite here (`/Users/cwaldner/Personal/chibi/chibi/CLAUDE.md`): add a pico_unit case for every behavior change (a fix needs a case that failed first); new native functions must be used in a `test/fixtures/feature-coverage` scene or listed in `coverage-gaps.txt` (the clock already added 5 lines); before publishing run `make test-native`, `make test-materials`, `make test-projects`, `make test-language-all`, `make test-tools`, `make check-format`, and say which ran; clang-format 22 (`/opt/homebrew/opt/llvm@22/bin/clang-format`); probes go in one dispatcher, never extra linked executables, and delete scratch afterwards; fail fast with `assert`, no defensive NULL checks; no C++. From your own memory: run `test-wasm` before landing (wasm enforces `-Wmissing-field-initializers`; this thread hit exactly that with Clang 22 and aligned its standalone build to main's policy), and never use `git stash` (the stash list is shared across worktrees and holds other lanes' stashes).

Commands this thread used (macOS arm64, Clang 22.1.8, Debug `-O1`, assertions on). The thread ran narrow selectors by the owner's "checks appropriate to the change" preference; the full `make test-*` set was NOT run locally (only hosted CI on the old head).
- Release generator hosts: `cmake -S sdk -B .chibi/clock/release-hosts -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm@22/bin/clang -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm@22/bin/clang++ -DCHIBI_SDK_BUILD_NATIVE=OFF -DCHIBI_SDK_BUILD_DESKTOP=OFF`
- Language driver: `cmake -S test/language -B build-language -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm@22/bin/clang -DCMAKE_C_FLAGS= -DCMAKE_BUILD_TYPE=Debug -DCHIBI_ASSET_GENERATOR_HOSTS=<release-hosts dir>`, then `cmake --build`. Keep sources frozen during asset generation (the source-snapshot guard rejects mid-build edits).
- Selectors: `CHIBI_LANGUAGE_BUILD_DIR=build-language python3 tools/test_policy.py tool <selector>` for `language-host-packages` (10/2509, seconds), `language-host-packages-compiler` (1/153; 178-303 s local, 144 s installed, ~740-1222 s sanitized), `language-package-imports` (14/1292), plus neighbours `language-package-core`, `language-package-tests`, `language-docs-markdown`, `language-native-calls`, `language-native-char-results`, `language-package-compiler-chibi-native-arguments`.
- Fixed point: `python3 tools/quiet.py --log-dir <dir> <name> -- env CHIBI_BOOTSTRAP_SEED=<seed dir> CHIBI_RELEASE_LANGUAGE_BUILD_DIR=<release build> python3 tools/test_policy.py tool language-package-compiler-chibi-bootstrap` (628 s, 1/30353).
- Graph: selectors `build-graph`, `build-graph-programs`, `build-graph-generated` with `CHIBI_TOOLS_BUILD_DIR`, `CHIBI_BUILD_HOST`, `CHIBI_GRAPH_REFERENCE` set; reference must be a Ninja build with the same SDK path as Graph (Makefile reference and implicit SDK path both produced false mismatches). Linux Graph route needs `chibi_host_clock` in `LinuxLinks.chibi`.
- SDK: `tools/package_sdk.py` strict package; audit export count (74 at `102c148f`) and installed consumer; `installed-c1/admission.json` (sha `316b28e6…`) and `controls.py` hold the full control harness.

Unexecuted on the forward candidate: Linux execution, current sanitizer run (last: `22732d134`, pre-#797 arena layout), anything on E1/compiler 27d8/protocol 14, public `Now`/`Between`, combined results proof, Windows/browser. Known slow or flaky: asset generation at the 256 Mi layout limit and facade 180/240 s timeouts hit other lanes' larger compilers (not the clock); #804's SDK install hit the 120 s limit on the old runtime and passed on the accepted one (SDK verification total 415 s); sanitizer stage-one is ~12-20 min; there is no local Docker.

Scratch to clean when integration finishes (all gitignored, 95% disk full, 24 GiB free): `host-clock-forward/chibi/.chibi/clock` 2.9 GB (reference 624M, build-language-asan 558M, installed-c1 433M, build-language 430M, sdk-validation-build 353M, runtime-308 155M, sdk-validation 83M, installed-language 74M, graph-reference 56M, release-hosts 43M, runtime-804 35M, lookup-probe 32M); `host-clock/chibi` 953M (`.chibi/`, `build-language/`). Keep receipts (`*.json`, `HANDOFF.md`, `installed-c1/proof`) and, until the coordinator says otherwise, the protected private Graph closure (`private-graph-resources.json`, `private-graph-validated.chibip`, `local.seed`, `local-bootstrap`, `release-hosts`): the coordinator said "Do not remove host-clock-forward, its private aa0 closure/resources, or any current installed-stage-one inputs." No `/tmp` scratch from this thread remains (its C1 temp dir was removed).

## 7. Open questions, risks, and what went wrong

- The forward branch is unpublished and its fixture patch is uncommitted: one disk, one copy. Commit first (checklist step 3).
- Who is coordinator now? The ledger (`/Users/cwaldner/Personal/chibi/.coordination/chibi-landing-ledger.md`) was written by a Codex thread and is partly stale (it names `907b1fe0` as main). Treat it as evidence, not truth.
- Results contract is moving: `@fallible` (function marker, 8dd33d14) was replaced by `@error` on types in #813 `e902fcf3`; parameter-name `?` syntax is "discussion only". #813 also needs a grammar-bearing runtime beyond E1 per its own body. Do not freeze the public clock signature until it is admitted.
- ABI churn: #797 changed arena layout, #802 changed the runtime pin, #803 moved build protocol 13 to 14, records will move Prepared to 27. Each invalidates old objects and relink proofs.
- Platform semantics: `CLOCK_UPTIME_RAW` (macOS) and `CLOCK_MONOTONIC` (Linux) both exclude suspend; the docs promise only "platform-dependent". Instants carry a per-grant process-local domain and must not be persisted or compared across processes.
- Existing build reader (`src/host/build_io.c:145-147` at `18dc0166`) treats a missing file as empty success and its root is a resolution base, not containment; do not reuse it for the future public filesystem API.
- Resolved incidents, for pattern recognition: (a) `Instant + ClockError?` rejected by stage one ("named result type is not supported yet") -> parity delegated to the results chat; (b) editing compiler source during asset generation tripped the snapshot guard -> freeze sources; (c) CI strict reviewed-bootstrap-artifact gate failed -> artifact refresh (old branch only); (d) SDK packaging contract missed `host_clock` -> `tools/package_sdk.py` fix in `76615d5fc`; (e) imports suite failed from a fixture missing a caller-owned heap -> fixed by math's #800; (f) Graph reference mismatches (Make vs Ninja, explicit macOS SDK path) and test object order -> `1df1789ff`; (g) installed fixture loaded stage one from checkout paths while linking installed libs -> patch `5ee949b3`; (h) C1 harness hook loaded the package twice -> scratch hook corrected; (i) the audit mis-read `CLAUDE.md` (a symlink to `AGENTS.md`) -> audit now compares the symlink identity.
- #784 now conflicts with main; resolving it by merging main into the old branch would be wrong (old artifact, old runtime). Re-publish from the forward port.

## 8. First 30 minutes

1. Read this file, both CLAUDE.md files, `docs/host-packages.md`, `.chibi/clock/HANDOFF.md` and `installed-c1/README.md` (all in the forward worktree).
2. Confirm with the owner that Codex thread `01a180c6` is stopped and who coordinates; ask whether to proceed with the forward port now and who grants a producer slot. Search Hindsight curated pages for "Imported host packages and monotonic clocks".
3. In the forward worktree: `git status`, verify `git diff --binary | shasum -a 256` is `5ee949b3…`, then commit it as a separate commit with a message saying it routes the installed-mode test fixture to the SDK's exported resource paths. Do not stash, reset or checkout over it.
4. `git fetch`, then re-read state: `gh pr view 784|813|801|795 --repo waldnercharles/chibi --json state,isDraft,mergeable,headRefOid`, main head, `tools/compiler/runtime.json` on main, any new coordinator ledger entry.
5. Conflict preview without touching the tree (`git merge-tree` against `origin/main`, or diff the five overlap files: `sdk/CMakeLists.txt`, `source_package_prepare.c`, `Body.chibi`, `Compile.chibi`, `tools/test_policy.py`). Plan the port in a fresh worktree/branch off main; leave `host-clock-forward` and the protected `.chibi/clock` inputs as the frozen reference.
6. Do cheap work while waiting for a slot: re-read the private-lookup seam and its mixed-target regression, draft the `Now`/`Between` and `rejects()` migration against #813's docs, and decide the publication route with the coordinator.
7. Start no compiler self-build, asset generation, sanitizer or SDK build until a slot is granted and the target (main SHA, runtime E1, protocol 14) is written down.
