# Handoff 11: CMake retirement / runtime SDK construction

Prepared 2026-10-28 (22:52 local) from the Codex logs, git, `gh`, and the landing ledger. Engine repo = waldnercharles/chibi.

**Thread status: dead until Nov 3.** The Codex root thread ended 2026-10-28 21:19 MDT (2026-10-29T03:19:48Z) with `usage_limit_exceeded` ("try again at Nov 3rd, 2026 11:35 PM"). It was mid-edit. Nothing in the runtime-SDK worktree is committed or pushed.

Logs (use the first; the coordinator's index points at the second, which is only a forked sub-agent):
- Root thread: `/Users/cwaldner/.codex/sessions/2026/10/28/rollout-2026-10-28T16-59-36-01a184bd-8a9f-7640-ae68-ec4a81af1254.jsonl` (14 MB, session `01a184bd-8a9f-7640-ae68-ec4a81af1254`).
- Sub-agent fork "reflection_audit": `.../rollout-2026-10-28T17-01-32-01a184bf-4ce8-7661-99f8-21832b1b24c7.jsonl`. Its last message ("runtime entry now rejects universal and cross-architecture requests...") is the one quoted in `/Users/cwaldner/Personal/chibi/HANDOFF-codex-worktrees.md`. The root's real last message (03:19:05Z) is that #818 is all green and runtime-SDK construction is "the next unvalidated piece".

Coordination archives for this thread: `/Users/cwaldner/Personal/chibi/.coordination/archives/2026-10-28/{cmake-reflection,sdk-version,pr814-ci-3cef,pr814-merge,pr815-merge,graph-link-response,pr818-ci-181,pr818-ci-24b210a4,pr818-ci-f5b0b500}`. Ledger: `/Users/cwaldner/Personal/chibi/.coordination/chibi-landing-ledger.md`.

## 1. Goal and scope

There was no human message in this thread. The coordinator (Codex chat `01a180b0-3773-78a1-beeb-03420dfa7a21`) spawned it with `create_thread` (log record 13). Key brief text:
- "The user requested a dedicated chat to remove CMake from Chibi and coordinate with 'Explain Chibi Hello World.'"
- "prioritizing moving real functionality into Chibi, switching its callers, and deleting obsolete implementations."
- "Merely invoking CMake from Chibi or adding parallel unused implementations does not meet this task."
- "Do not delete recovery or consumer interfaces ahead of their replacement."
- "Keep any necessary compatibility output only for actual live consumers, with a named removal condition."
- "Do not add a blanket global test-duration ceiling." Choose checks by affected behavior; report exact source/tool/artifact identities and unrun coverage.
- Authorized by the user at that time: autonomous work, commits, PRs, review and merge. In practice the coordinator ("root") did the final verification and merge of #814, #815, #803 and #817. This thread said "No merge from here" for #818. Confirm merge authority with the user before merging anything.

Planned order: (1) concrete responsibility/deletion map plus a small complete slice, which was SDK reflection; (2) cold bootstrap and full SDK construction/install; (3) remaining platform and consumer routes (Windows, browser, Studio, vendor/test builders, graph parity builds).

CMake still owns all of the following (from `docs/toolchain-inventory.md`, section "CMake retirement boundaries, 2026-10-28"):
- cold first-host bootstrap and recovery
- full public SDK build/install/export
- the native runtime SDK (now being replaced, uncommitted)
- Windows and browser construction
- Studio and installed-consumer metadata (`ChibiConfig.cmake` and friends)
- standalone test, vendor and reference builds

## 2. Ownership

| Item | Role | Notes |
|---|---|---|
| `/Users/cwaldner/Personal/chibi/.worktrees/runtime-sdk-construction/chibi`, branch `codex/runtime-sdk-construction-20261028` | **PRIMARY.** Holds all uncommitted runtime-SDK work. | Local-only branch with no upstream and 0 commits ahead of main. The coordinator's table calls it "merged: clean up". **Do not clean it up.** |
| `/Users/cwaldner/Personal/chibi/.worktrees/graph-link-response/chibi`, branch `codex/graph-link-response-20261028` | **PRIMARY.** Owns PR waldnercharles/chibi#818 (open). | Landing is nearly done; see section 3. |
| `/Users/cwaldner/Personal/chibi/.worktrees/cmake-retirement/chibi`, branch `codex/cmake-retirement-20261028` | Merged (#814), clean tree. **Still a dependency.** | Holds the protocol-14 build host, the borrowed formatter, the test dispatcher, the CMake reference and all receipts (section 6). Copy what you need before any cleanup. |
| Merged PRs: waldnercharles/chibi#814 (reflection), #815 (`sdk/VERSION`) | Done. | |

Overlap with other threads:
- **Coordinator** `01a180b0-3773-78a1-beeb-03420dfa7a21`. It assigns heavy-producer slots, reviews, and does final merges. All "producer slot" requests go through it.
- **Language-UX / main-runner-reload ("Explain Chibi Hello World")**, chat `01a18423-e396-7ca0-b382-2461479fc292`, worktree `/Users/cwaldner/Personal/chibi/.worktrees/main-runner-reload/chibi`. It owns `apps/chibi` Main, command dispatch and `Chibi::program` sessions.
  - Shared files that need agreement before edit: `tools/graph/engine/Targets.chibi`, `tools/toolchain/SDK.chibi`, SDK CMake/resource lists/installs, `tools/package_sdk.py`, project preparation.
  - Its exact ownership entries must survive in the runtime-SDK replacement. They are the exported `Chibi::program` target, the `share/chibi/apps/chibi` and core resource installs, and the program/application/session-test entries in `Targets.chibi`.
  - Its `apps/chibi/Projects.chibi` is the future consumer of your reusable build/SDK modules. Do not build a second CLI.
- **Pure reachability / core-math owner** `01a180c6-be08-7341-b87a-7ef9b8ce2c90`. It released its deep-path Graph roots and `/tmp/p790-*`. Do not touch them.
- **Strings-integers (#821)**, `01a180c6-c4ac-7941-84c4-24e8cca4bb62`. It edits only the short yield paragraph in `docs/self-hosting-build.md`. #818 also edits that file elsewhere. Do not touch that paragraph.
- **#801 (array/view effects; its owning chat is not named in this thread's log, so ask the coordinator)** owns the next compiler/ABI producer. Records, generics #810, math, recoverable-errors #813 and host-clock keep their own ABI/compiler/bootstrap/SDK publication holds. Do not pull held features in.

## 3. Current verified state (read-only checks at about 22:30 local)

**Engine main**: `d66cb3be` (d66cb3bebf33ff3c4e21ef53e3f3990a67ecc60c). This is PR #816, merged 03:42Z, **after the thread died**. #816 touched `docs/test-parallel-resources.md`, `test/language/generic.c`, `test/tools/test_policy.c` and `tools/test_policy.py`.

**waldnercharles/chibi#814** is MERGED as `907b1fe0` (02:47Z Oct 29). Head `3cef6c2a`, tree `c4b2793b`. All CI is green; Windows jobs are skipped and paused.
- Title: "Use the Chibi graph for Linux and macOS SDK reflection".
- It did **not** delete `tools/reflect/CMakeLists.txt`, which was the original plan. Windows keeps its exact original C-only standalone route (restored at `dea8b585`). Deletion waits for Windows execution of the authored graph.

**waldnercharles/chibi#815** is MERGED as `498f1b9a` (head `5a82f7e7`). It added `sdk/VERSION`. CMake and Chibi both read it, and the identity files are byte-identical to before.

**waldnercharles/chibi#818** ("Use authored response files for long graph links") is **OPEN, not draft, mergeStateStatus CLEAN, MERGEABLE**.
- Head `f5b0b500` (f5b0b50034eb87cad2af6cd8e5499a2dcea6d7b7), tree `e13c4e1d`. It is 8 commits ahead of and **2 behind** main (`behind_by` 2 per `gh api compare`).
- All CI is green: normal 40809070712, self-build 40809070679 (3 platforms plus comparison, 7 eligible jobs, 3 configured skips). Tested merge `ebd33956` (parents `907b1fe0` and `f5b0b500`).
- Landing evidence: `/Users/cwaldner/Personal/chibi/.coordination/archives/2026-10-28/pr818-ci-f5b0b500/continuation-receipt.json` (SHA-256 `d7c4cbe3`).
- The coordinator had reviewed it (reviews 5692065633, 5692240330, 5692414521) and was to do the "final current main/head/tree/rules/review merge audit". **Not merged.**
- It conflicts in file set with #816: both change `test/tools/test_policy.c` and `tools/test_policy.py`. GitHub shows no textual conflict, but the combination has not been CI-tested. It also touches `PLAN.md`, which will conflict with the runtime-SDK PLAN edit.

**`runtime-sdk-construction`** (`git status`; HEAD `907b1fe0`, nothing else committed). **11 status entries, 14 files, all uncommitted.** The coordinator's "7 dirty" is stale.
- Modified:
  - `PLAN.md` (+48: section "Runtime SDK construction and installation"; it was truncated by a script and restored from `.chibi/runtime-sdk/prior-PLAN.md`)
  - `test/tools/build_sdk.c` (+2: passes `--collection graph=<root>/tools/graph` to the private SDK installer)
  - `tools/graph/engine/Engine.chibi` (`WriteIdentity` made `pub`)
  - `tools/toolchain/Graph.chibi` (+207)
  - `tools/toolchain/SDK.chibi` (+11/-149: filesystem helpers moved out)
- New:
  - `tools/graph/sdk/Contracts.chibi` (784 lines). The runtime installed surface: 71 public exports (63 static libs plus 8 interfaces), 10 vendor exports, 293 owned headers, 632 resource/vendor files. Darwin only; Linux vendor generation is rejected.
  - `tools/graph/sdk/CMake.chibi` (255). Generators for the retained installed `lib/cmake/Chibi/*.cmake`: `Targets`, `Vendor`, `Config`, `ConfigVersion`.
  - `tools/graph/sdk/JSON.chibi` (459). Canonical JSON matching `tools/package_sdk.py`'s identity algorithm.
  - `tools/graph/sdk/Evidence.chibi` (647 and still growing at cutoff). `PrepareBuildConfiguration`, `WriteBuildConfiguration`, `RecordEvidence`, `GraphEvidence`: graph compile/link provenance to replace the CMake/Ninja adapter in `package_sdk.py`.
  - `tools/graph/install/Files.chibi` (146). Filesystem policy extracted from `SDK.chibi`, because build-host programs see only an explicit file plus imports, not sibling files.
  - `tools/toolchain/RuntimeSDK.chibi` (541). The installer: `RuntimePackage` entry, stage, verify, publish with rollback, manifest, exports.
  - `test/tools/RuntimeSDKJSON.chibi` (134), `RuntimeSDKInstall.chibi` (110), `RuntimeSDKEvidence.chibi` (190). **None is wired into any `test/tools/main.c` selector.** Scratch verifiers sit in `.chibi/runtime-sdk/` (`Case.chibi`, `Verify*.chibi`).
- `Graph.chibi` now has a `RuntimeSDK` entry with a strict option parser, which routes through `BuildLibrariesWithArguments(options, runtime_sdk)`.
  - Defaults to Debug and no sanitizers.
  - Rejects universal and cross-architecture builds before writing.
  - Maps `-D` toolchain definitions.
  - The four vendor-only interface exports (`box2d`, `box3d`, `execution_sljit`, `task_scheduler`) stay metadata, not build targets.
- `Makefile` is unchanged. `runtime-sdk-build` (line 208) is still `configure.sh sdk-native` plus a CMake build, and `package-runtime-sdk` is still `package_sdk.py --kind runtime`. The caller cutover has not started.
- **State at the moment of death: broken.**
  - `chibi-compile --format` on `tools/graph/sdk` reported `Evidence.chibi: Cannot format source with syntax errors` (record 5142).
  - The install-fixture run printed `Unexpected syntax` (exit 1).
  - A sub-agent was still rewriting `Evidence.chibi`. It grew from 208 to 647 lines between 03:14Z and the end of the log.
  - A reserved-identifier sweep was in progress. These words cannot be identifiers: `entry library system source state event after as input object and or from on action`. The thread renamed `entry` to `item`. A literal `"\0"` had to become `"\u0000"`.
  - Treat everything uncommitted as a draft that has never fully parsed together.

**`cmake-retirement`**: clean at `3cef6c2a`, 0 ahead. Ignored scratch is 369 MB in `.chibi/` plus 35 MB in `build-sdk-reflect/`.

**Disagreements with the log or coordinator doc**
- The doc's log path is the sub-agent fork, not the root thread.
- "runtime-sdk-construction merged: clean up" is wrong. The directory is where the work lives.
- "Dirty 7" is now 11 entries / 14 files.
- The ledger says #818 CI is "active". Fresh `gh` shows green.
- The initial plan said to "retire `tools/reflect/CMakeLists.txt`". Windows still needs it.

## 4. Done and left

**Done (evidence as reported in the log; see the PR bodies and receipts for details)**
- **#815.** `sdk/VERSION` single authority. Regression: 3 cases / 20 assertions. Studio CI initially failed ("SDK does not identify the clean engine candidate") because the reflection recovery build dirtied git. Fixed in #814 by nesting `host/` and `native/` under the reflection build root (`e229c781`).
- **#814.** SDK reflection through the graph on Linux and macOS. Merged after many review rounds. Reported results:
  - Package 7 cases / 125 assertions.
  - Toolchain recovery 11/165.
  - Generator/reference parity 10/212; the graph's link command and both generated C files are byte-identical to the CMake reference.
  - Real cold build about 17 s; warm Make recipe about 12.5 s.
  - Authored libclang discovery in `tools/graph/engine/Profile.chibi` and `Engine.chibi`: `CMAKE_PREFIX_PATH`, `LIBCLANG_ROOT` precedence, bare `include`/`lib`/`lib64`, multiarch, trailing slashes, driver-owned resource dirs.
  - Protocol-14 admissions in both Make reflection recipes and the fixtures.
- **#818.** Graph links over the build host's 64 KiB argv cap (the pure owner's 502 args / 71,540 B failure) now use authored GNU response files in `tools/graph/c/C.chibi`.
  - Empty arguments stay in direct argv because Clang 18 drops empty quoted response-file tokens.
  - Fixture `test/tools/GraphLinkResponse.chibi` passes on Clang 21/22/23 (1 case / 12 assertions, about 8 s, 405 args / about 106 KB).
  - Historical 502-arg replay linked and cache-hit (receipt 5b521e8d).
  - Linux x64 CI now shows the empty `-B` case passing.
- **Runtime SDK, preparation only.**
  - Metadata revalidated on `79743c56` with protocol 14: authored generation 8.37 s; 12 metadata-only CMake configure controls, 291 imported properties, 11.8 s. Receipt `0c269784`, at `/Users/cwaldner/Personal/chibi/.worktrees/cmake-retirement/chibi/.chibi/cmake-retirement/runtime-sdk-wip/79743c5-protocol14/validation.json`. The archives in that test were non-linkable placeholders.
  - `RuntimeSDK` commands-only: 338 compile entries, 63 archives plus the player, 1 link, no objects. Option, architecture and precedence rejection batch passed.
  - Canonical JSON: 294 differential vectors plus a 1,000-entry inventory (4.268 s).
  - **No runtime SDK has ever been built, installed, packaged or consumed.**

**Left, in order**
1. **Land #818.**
   - Ask the user or coordinator who merges.
   - Integrate main `d66cb3be` (conflict risk in the two `test_policy` files and `PLAN.md`).
   - Re-run the response-file selector and the policy selector on the combined tree.
   - Get a fresh normal and self-build CI pair on the exact head.
   - Merge only when head/tree/reviews/CI agree.
2. **Secure the runtime-SDK draft.**
   - Snapshot it first. Ask the user whether to make a local WIP commit.
   - Then fix parse errors in `Evidence.chibi` and `RuntimeSDK.chibi`.
   - Run the Chibi formatter (section 6) and canonical checks.
   - Reconcile `PLAN.md` with #818's edit.
3. **Wire tests into the dispatcher.** Add `--runtime-sdk-*` selectors in `test/tools/main.c` (repo rule: one dispatching tool, no per-probe executables). Promote or delete the scratch `Verify*.chibi`. Add a regression for the `SDK.chibi` split (`test/tools/build_sdk.c` plus a private-compiler-tools relocation check).
4. **Source review.** Run `/code-review` or an independent reviewer on the diff before any commit. The thread reviewed everything with independent sub-agents.
5. **First real runtime SDK build.**
   - Needs a heavy-producer slot from the coordinator. At 02:54Z the next heavy IO slot belonged to the pure/790 archive cleanup, and "hold actual new native/SDK/asset producer until archive slot closes" was in force.
   - Sequence: `RuntimeSDK` graph build, then `RuntimePackage`, then compare to `package_sdk.py --kind runtime` output (manifest fields, canonical identity, ownership).
6. **Acceptance checks** (from the plan):
   - Relocated, read-only prefix; configure/compile/link C and C++ consumers outside the source tree.
   - Source-free player run.
   - Feed the result to unchanged `tools/package_player.py` (`package-player` consumes the runtime package).
   - Cache reuse and invalidation; transactional replace, failure and rollback.
   - Unsupported flags or platforms fail before staging.
7. **Cut over the Make caller on macOS only.** Replace `runtime-sdk-build` / `package-runtime-sdk` (`Makefile` lines 208-214) and drop their CMake invocations. Keep `sdk/CMakeLists.txt`, `configure.sh`, `package_sdk.py`, the installed CMake exports, the full SDK, Studio (needs package kind `sdk`), and the Windows and web routes. `package_player.py` and `package_studio.py` import helpers from `package_sdk.py`.
8. **Later slices.**
   - Full authoring SDK.
   - Cold bootstrap. E1 (`tools/compiler/runtime.json`) pins only `chibi-compiler-host`, which is not a usable graph host. The plan is the CLI/apphost as the eventual public execution host, with the private recovery path kept until that is proven.
   - Windows and browser construction, and deletion of `tools/reflect/CMakeLists.txt`.
   - Standalone test/vendor/reference builders.
   - Studio and installed-consumer metadata.

## 5. Holds, freezes, coordination rules (exact)

- **Compiler and runtime identities are frozen.**
  - Compiler `27d8f3d0568e442e1a70b167c04ba97c671ea0d0b1e85e3a7493d88fc59bb912` (20,792,064 B).
  - Runtime pin **E1** (#802) unchanged. "Keep E1 unchanged."
  - Bootstrap "2204" per the ledger. Public/prepared ABI unchanged.
  - Private build protocol is **14** (accepted with #803 at `79743c56`). Do not mix protocol-13 guards with protocol-14 hosts. The only valid local host is `de63381c...` (section 6).
- **No seed publication, new compiler artifact, ABI change, or native admission-limit increase** is authorized. Quote: "no seed publication is granted by this message."
  - A future private build-host seed must include the #803 lifetime/protocol-14 repair and explicit source/compiler/platform/vendor identities.
  - Any native change needs a concrete required capability. Prefer authored solutions (as with #818 response files).
- **Heavy producers** (compiler, SDK, asset, full Graph builds) are scheduled by the coordinator. Do not start a competing one. Source work and small fixtures are fine. Current state: #801's isolated writer/recovery phase is green; the pure/790 archive cleanup holds the next IO slot.
- **Held work.** Do not take it in implicitly. This covers #801 full ABI, #808 records, #810 generics, #813 errors, #783 math, #784 clock, #795 libraries. Their SDK publication gates stay.
- **CLI owner agreement.** One public `chibi` interface. Private `build.*` supervision/environment/output/concurrency must migrate to the public host contract before first-host recovery is deleted. Keep the recovery path with a named deletion gate until then.
- **Windows.** General Windows engine/Studio CI is paused. Do not count paused jobs or declaration-only mocks as execution. Say "Windows unrun". The repo rule is that CUT62 (`docs/testing.md#batch-validation-cut62`) requires a reviewed replacement when coverage is unavailable.
- **Evidence discipline** (the coordinator enforced this repeatedly):
  - Keep original failed receipts and logs; attribute each result to its exact source, tool and time.
  - Never relabel an older proof as current.
  - Never claim "full product" from a transport-only or metadata-only check.
  - An empty log file is not evidence. The excerpt from the failed Studio job was 4,514 B, SHA `361a9c98`.
- **Compat output** (the installed CMake files) only for named live consumers (Studio, C consumers), generated from Chibi metadata, with a removal condition.
- **Repo rules that bite** (`/Users/cwaldner/Personal/chibi/chibi/CLAUDE.md`):
  - New work goes in Chibi (`.chibi`), not C or CMake. Keep it small; delete replaced code in the same change.
  - Before a commit: whole-diff review, a pico_unit case per behavior change in `test/tools`, `make test-native`, `test-materials`, `test-projects`, `test-language-all` and `test-tools`. Report what was not run.
  - Format authored `.chibi` with `chibi_ctl --format` (or the formatter in section 6) and run the canonical check; `make test` alone does not cover canonicality.
  - clang-format 22 for C (`make check-format`).
  - No `CHIBI_PROJECT_VERSION` bump unless a project would break.
  - Owner memory says merge your own PRs once green, but the coordinator merged this thread's PRs, so confirm first. Do not use `git stash` for before/after.
  - Probes go in one dispatcher. Delete scratch builds and clones and report what is left.

## 6. Validation: commands, tools, artifacts

Used by the thread (all macOS arm64; sandbox ccache temp files were blocked at first, hence `CCACHE_DISABLE=1`):
- Dispatcher build, from the worktree root: `cmake -S test/tools -B <dir> -G Ninja -DCMAKE_BUILD_TYPE=Release -DCHIBI_SDK_BUILD_COMPILER=ON -DCHIBI_REFLECT_EXECUTABLE=<reflect>`, then `cmake --build <dir> --target test_tools --parallel 8`.
- Selectors: `<dir>/test_tools --package`, `--build-graph-reflect`, `--build-graph-resources`, `--build-graph-generated <host> ...`, `--build-graph-link-response <host>`, `--policy`, `--test-runner`. Check the list with `make test-list`.
- Make recipes: `make sdk-reflect-build BUILD_HOST=<host> SDK_REFLECT_BUILD_DIR=<dir>`, `make tools-build TOOLS_BUILD_DIR=... TOOLS_REFLECT=...`, `make test-tools ...`, `make graph-reference CMAKE_REFERENCE_DIR=... BUILD_DIR=...`.
- The Make fixture needs `--no-print-directory` (GNU Make 4.3 prints directory banners when `MAKELEVEL>0`).
- Formatting: `sh tools/clang-format.sh --check`; `/opt/homebrew/opt/llvm@22/bin/clang-format`.
  - Chibi sources: `chibi-compile --project-root <dir> --format`, run on a copy of the file in a temp directory.
  - The thread's formatter: `/Users/cwaldner/Personal/chibi/.worktrees/cmake-retirement/chibi/.chibi/borrowed-formatter/chibi-compile` (SHA `74ecdb33...`, system dylibs only).

Not run: any Windows, Linux-local, browser or Wasm execution of the new routes (Linux and Windows were validated by CI or not at all). Not run for the runtime SDK: any compile, link, install, package, consumer or player check, and none of the full make groups.

Slow or flaky:
- Cold reflect about 17 s; parity suite about 2 min; the full tools suite was 232 cases in about 228 s.
- A macOS CI job failed once on DNS while downloading a pinned tool. Retry only that job on the same source.
- The raw SDK job log is truncated: the build-host result aggregate is 4 KiB and per-child output is 1 MiB. The thread proposed (not implemented) capturing `build.last_stdout`/`last_stderr` after a failed final build.
- The scalar-fixture PowerShell transport had an unexplained 10 s timeout once (#818 run). It was recorded and not changed.

**Artifacts other worktrees depend on** (do not delete `cmake-retirement` before relocating these):
- `/Users/cwaldner/Personal/chibi/.worktrees/cmake-retirement/chibi/build-sdk-reflect/host/chibi-build-host` (protocol 14, SHA256 `de63381c9f0cbca82c932db0d9117aa48448d0ff50cd600c3b08efea64f20335`). Every runtime-SDK and #818 command in the log uses this absolute path.
- The borrowed formatter above. `.chibi/cmake-retirement/graph-tests/test_tools` (dispatcher). `.chibi/cmake-retirement/reference` (CMake reference for parity). `.chibi/cmake-retirement/runtime-contract.json` plus `extract-runtime-contract.py`, `generate-sdk-cmake.py`, `generate-sdk-contracts.py` (the oracles for the runtime metadata).

**Scratch to clean when done:**
- `cmake-retirement/chibi/.chibi` (369 MB), `build-sdk-reflect`, `compile_commands.json`, `src/runtime/component_ids.tsv.lock`.
- `graph-link-response/chibi/.chibi` (12 MB; includes a scratch `test_tools` and objects).
- `runtime-sdk-construction/chibi/.chibi` (14 MB).
- Receipts under `.coordination/archives` are kept evidence.

## 7. Risks and what went wrong

- **Uncommitted, unparseable, unpushed runtime-SDK draft.** Biggest risk is loss or silent edits. It also has no wired tests, and a sub-agent may have been mid-write when the thread died. Re-read each file; do not trust the line counts here.
- **#818 vs #816 combination is untested.** Main moved after the green CI. Re-run on the combined tree.
- **Windows regression found in review of #814.** Switching Windows to the root CMake project enabled C++, compiler, native and desktop defaults. That broke the `CHIBI_SDK_COMPILER_HOST_ONLY=ON` override. Fix: restore the exact old route (`dea8b585`, receipt under `.chibi/cmake-retirement/windows-profile`). Windows execution is still unclaimed.
- **Studio CI dirtiness.** See section 4: nested build outputs dirtied the clean-source check.
- **libclang discovery P2.** Original discovery ignored `CMAKE_PREFIX_PATH` and system prefixes. Fixed in authored Chibi with no protocol change.
- **Link transport.** 502 args / 71,540 B exceeded the 64 KiB cap. Response files were chosen over raising native limits. Review then found the empty-operand Clang 18 bug (`-L ""` shifted operands).
- **CI fixture failures on Linux.** `-L ""` direct links fail on Clang 18 (the fixture was redone with `-B ""`). MAKELEVEL banners broke the Make fixture (fixed with `--no-print-directory`).
- **Stale reference headers.** The CMake reference went stale after compiler-source changes. Refresh the reference before comparing.
- **Provenance gap.** Graph records do not bind compiler bytes. The runtime path must invalidate object records when tool identity changes (the design is in `Evidence.chibi`; nothing has run).
- **Metadata-only evidence.** The validated 71/10/293/632 contract is for macOS Release. Linux, Windows and web are not covered.
- **Standalone-program boundary.** Build-host programs do not import sibling files, which is why `graph:install` exists and why `SDK.chibi`'s entry now needs the `graph` collection. Check every invocation of `SDK.chibi`, including CI workflows, docs and Makefile callers. The log shows only a grep of `build_sdk.c`, `tools/` and the Makefile (record 5047), not a full audit.
- **Open design questions** (decide with the user or coordinator):
  - The first Make cutover is macOS-only; do you accept Linux staying on CMake for the runtime route?
  - The packaged runtime package contract: keep the current manifest fields and "rich requirements" (the thread's plan), with graph provenance replacing the Ninja-specific `build.inputs`.
  - Whether `package_sdk.py --kind runtime` should be deleted or kept as the independent oracle after cutover.
  - How cold bootstrap proceeds (public apphost versus private seed) and who produces any seed.

## 8. First 30 minutes

1. Confirm with the user that no Codex thread is writing in these worktrees (the coordinator `01a180b0...` may still be active). Do not touch `main-runner-reload` or the pure worktrees.
2. `git -C /Users/cwaldner/Personal/chibi/.worktrees/runtime-sdk-construction/chibi status --short` and snapshot the 14 files (copy to a dated dir under `.chibi/`, or ask the user for a local WIP commit). Do not run `git stash`, `checkout` or `clean`.
3. Refresh: `gh pr view 818 --repo waldnercharles/chibi --json state,headRefOid,mergeStateStatus,statusCheckRollup`, `gh api repos/waldnercharles/chibi/commits/main --jq .sha`, and the open PRs (#821, #822 may overlap `test/tools` or `docs/self-hosting-build.md`). Re-read the ledger and the last coordinator messages in the root log (records 4596 onward are the latest).
4. Ask the user two things: who merges #818 and whether you may take over Codex's final-merge role; and whether the runtime-SDK work should resume now, with the first heavy producer slot coming from the coordinator.
5. Check the host and dispatcher still exist and match: the `chibi-build-host` SHA above and `.chibi/cmake-retirement/graph-tests/test_tools`. If rebuilding is needed, use `make tools-build` per section 6, not an ad hoc build.
6. Start on #818 (integrate main, re-run the response-file and policy selectors, request fresh CI). Then fix `Evidence.chibi` and `RuntimeSDK.chibi` syntax and run the formatter on copies.
7. Record decisions, resolved failure causes and lessons in Hindsight with `hindsight_ingest_document` (repo, date, commits, evidence, status). Keep credentials out.
