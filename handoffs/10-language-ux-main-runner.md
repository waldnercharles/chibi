# Handoff 10: Chibi language UX - hello-world entry point, ordinary `chibi` CLI, main-runner reload (engine + Studio)

Written 2026-10-28 ~22:50 MDT from the Codex logs and read-only git/gh. Log timestamps are UTC (MDT = UTC-6). Codex session `01a18423-e396-7ca0-b382-2461479fc292` (the ledger calls it "Explain Chibi Hello World"). It ended at 21:19 MDT with `usage_limit_exceeded` (Codex retry date Nov 3 2026 23:35); there is no live Codex process to wait for.

Logs (never cat; parse with `python3 -I`):
- Root, interactive with the owner: `/Users/cwaldner/.codex/sessions/2026/10/28/rollout-2026-10-28T14-11-46-01a18423-e396-7ca0-b382-2461479fc292.jsonl`. The path in HANDOFF-codex-worktrees.md (`...14-12-27-01a18424-7ffb...`) is the fork `/root/studio_starting_flow`, a build/test worker. Its "last message" there is stale (02:09 UTC).
- Workers (same dir): `...14-12-36-01a18424-a441...` language_approachability, `...14-48-12-01a18445...` runner_docs, `...14-56-13-01a1844c...` runner_reload_review, `...16-34-55-01a184a6...` portable_sources, `...16-35-56-01a184a7...` application_io. The three `guardian_review` logs are only auto-approval records.
- Agent-to-agent message bodies are encrypted. The readable record is the assistant text, tool calls and `<codex_delegation>` blocks from the coordinator.

## 1. Goal and scope

Owner requests, in order (all verbatim from the root log):
1. "How do I run a simple program with a single entry point? Like Hello World?" and "it feels like our API is quite bloated, or hard to approach. Especially when I try to develop via Studio... Can you help with this? Step-by-Step?"
2. "how does our chibi compiler compile chibi? It must have an entry point, right?" (answer: the host calls the ordinary function `CompileClosure`; any function can be an entry.)
3. "Let's start with step 1. Can we make `chibi run Hello.chibi` call a `Main` function? Or better yet, just `chibi Hello.chibi` or `chibi .`"
4. "Does this support Hot Reload as well?", then "Can you check existing branches and origin/main? I'm fairly certain that we've been designing hot-reload and want to make sure we're not missing anything." The agent had built on stale checkout 17106d788 and admitted it. Hot reload (#775 retained-state migration; Studio Live / On Save) was already on main.
5. "Let's port the runner onto current origin/main and integrate the existing hot-reload machinery. Preserve the local changes and test both Main execution and state-preserving reload."
6. "Is the work you're doing being done in c or chibi? If it's being done in c, can you explain why?" then "Are we able to do it via chibi instead?" Result: `tools/toolchain/Run.chibi`, since superseded.
7. "I want to understand why this is in `tools`... It feels like it's still separate from chibi for some reason."
8. **"Let's make chibi itself a normal Chibi app with a Main entry point and a shared compiler/runtime API."** This is the current mandate. Later "Continue" messages only resumed after model-capacity errors.

The five-step plan the agent gave the owner (only step 1 is done):
1. A plain-program experience: one file, one entry, one run command. **Done in source, local only.**
2. A Studio "Hello World" starter that opens source and Output with Play preconfigured. **Not started.**
3. Teach functions and values there. **Not started.**
4. Introduce game concepts through visible results. **Not started.**
5. Review API duplication before removing any APIs. **Not started.**

The language_approachability findings are the only API-bloat evidence so far:
- The language guide opens with `library { Run }` but never says how to run it.
- Games use `library/behavior/system` blocks while packages use `package/import/pub`.
- The first example needs a manifest, player, camera, scene and behavior.
- The default catalog has 28 definition packs.
- The published reference covers only `packages/core` (just `strings`).

User-facing result: `chibi Hello.chibi`, `chibi run Hello.chibi`, `chibi .` and `chibi --watch Hello.chibi` call `Main :: () { ... }`. `Main` takes no arguments and returns nothing or an int exit status. A file runs alone; a directory includes sibling sources and local packages; a directory with `project.kdl` still runs as a game. `chibi apps/chibi -- version` runs the CLI through the same program service. `chibi test apps/chibi` runs the CLI's own authored tests. SIGINT gives 130 and SIGTERM 143.

## 2. Ownership

PRIMARY (the only places to work):
- Engine: `/Users/cwaldner/Personal/chibi/.worktrees/main-runner-reload/chibi`, branch `codex/chibi-main-app-20261028` (gitdir `/Users/cwaldner/Personal/chibi/chibi/.git/worktrees/chibi14`).
- Studio: `/Users/cwaldner/Personal/chibi/.worktrees/main-runner-reload/chibi-studio`, branch `codex/chibi-app-resources-20261028`. It holds one commit, 30d76509 on be7709fd: `CMakeLists.txt` `foreach(resource shaders licenses apps core)`, so Studio copies the SDK's `apps/` and `core/` beside its embedded `chibi`.

TOUCHED, DO NOT MODIFY:
- `/Users/cwaldner/Personal/chibi/chibi` (branch `codex/non-code-cleanup-20261027` at 17106d788). It holds the superseded first C runner, uncommitted (`src/authoring/program_run.c`, `chibi_main.c`, `compile_main.c`, README, toolchain.md and others). It is mixed with the owner's own pre-existing edits (probably `docs/design-notes.md`, `PLAN.md`, `docs/ecs-query-language-plan.md`). Codex kept it byte-identical (`original-preservation-final.json`) and keeps a backup at `/Users/cwaldner/Personal/chibi/chibi/.chibi/hello-port-preservation-20261028`. Do not clean, reset or commit it without the owner.
- `/Users/cwaldner/Personal/chibi/chibi-studio` (main df050886, clean). Codex edited its CMakeLists once and restored it byte-exact.
- Read-only research: Studio branches `typed-prototype-editor-20261021`, `studio77-recovery-handoff-20261023`, `studio-native-acceptance-20261024`, `live-outliner-20261018`. These are why the handoff index lists odd branches. They are not this thread's work.

Overlap with other threads:
- **Coordinator** `01a180b0`: owns landing and review. Ledger: `/Users/cwaldner/Personal/chibi/.coordination/chibi-landing-ledger.md` (line "Explain Chibi Hello World"). Root assigned independent read-only reviews, and those findings drive the work below.
- **CMake retirement / runtime-sdk** `01a184bd`: shares `tools/toolchain/SDK.chibi`, `tools/graph/engine/Targets.chibi`, SDK resource lists and `sdk/project_preparation.cmake`.
  - This thread owns program/app/resource/export hunks: `sdk/CMakeLists.txt` program export, `contracts.cmake`, `ChibiConfig.cmake.in`, `source_authoring.cmake`, `tools/package_sdk.py`, and the chibi-related parts of Targets.chibi.
  - CMake owns Graph/SDK build and recovery. Agreed: the eventual public host/capability layer built here serves first-host bootstrap, and the private build-host seed stays temporary with a named deletion gate.
- Merged inputs already integrated: #803 (protocol 14), #815 (`sdk/VERSION`), #817 (reachability; compiler 27d8).
- #818 (graph link response, open) and #801 (array effects, open draft) are separate holds. Their code must not be imported.
- Possible textual overlap, not checked: #821 (strings/integers) touches `test/tools` and `test/language`; #816 touched `tools/test_policy.py`, which this branch also edits.

## 3. Current verified state (read-only, ~22:47 MDT)

Engine worktree:
- Branch `codex/chibi-main-app-20261028` at 1dd12f7c1, 3 ahead and 13 behind local `origin/main` d66cb3beb.
- Own commits: fee955dc2 (Run the public CLI as an ordinary Chibi application), 9acfa17d0 (isolated program SDK consumer), 1dd12f7c1 (merge of 79743c567 = #803, plus fixes for 3 review findings).
- Combined diff vs 79743c567 is 60 files, +8633/-1763.
- **A merge of `origin/main` 907b1fe02 (#814) is in progress, uncommitted** (`MERGE_HEAD` is 907b1fe02, written 21:03 MDT). `git status` says "All conflicts fixed but you are still merging".
  - 14 files are staged: `.github/capture-facade.py`, `.github/workflows/ci.yml`, `Makefile`, `PLAN.md`, `docs/toolchain-inventory.md`, `sdk/README.md`, `test/tools/GraphDiscovery.chibi`, `test/tools/GraphLibclangDiscovery.chibi` (new), `test/tools/build_graph.c`, `test/tools/package.c`, `tools/graph/engine/Engine.chibi`, `Options.chibi` (new), `Profile.chibi`, `tools/toolchain/Graph.chibi`.
  - 1 file is unstaged: `test/language/program_session.c`, a new case `program_tests_reject_nested_runs_and_allow_later_host_runs`.
  - No post-merge build or test was run. Banach was only choosing focused checks when the quota hit.
- **The reentry guard it tests does not exist yet.** `grep -i reentr src/authoring/program_service.c` is empty. The agent was reading `program_service.c` ~730-815 (`program.test` is capability id 6, `native_test`) at the cutoff.

Studio worktree: branch `codex/chibi-app-resources-20261028` at 30d76509, 1 ahead and 0 behind origin/main be7709fd, clean. gh confirms the remote Studio main is be7709fd.

PRs: none. `gh pr list --head` is empty for both branches, and both branches 404 on the remote (never pushed). Remote engine main is d66cb3beb (#816). Related: #814 MERGED (907b1fe02), #803 MERGED (79743c567), #816 MERGED, #818 OPEN and ready, #801 OPEN draft.

Disagreements with the log or the handoff index:
- The index says "dirty 14, ahead 3". Now 15 status entries: 14 staged by the merge plus the 1 test file.
- Banach's final message (protocol14 hosts built, final native build running) is hours old. Later work finished: SDK acceptance at 1dd12f7c passed and a clean tree was recorded at 02:50 UTC.
- At 02:50 UTC the log said the tree was clean. It no longer is. The branch also bases on main 79743c567; main is now d66cb3beb.

## 4. Done so far (as reported in the log, evidence under `.chibi/main-runner-reload/` in the engine worktree)

Design of fee955dc2/1dd12f7c1:
- `apps/chibi/{Main,Arguments,Programs,Projects,Export,Inspect}.chibi` own all command routing, option parsing, watch/retry/exit policy and subprocess construction.
- `src/host/chibi_main.c` shrank to 52 lines. It finds app/core resources, starts the generic application host, and returns Main's status.
- The private `Run` entry, `runner.*` pack and `compiler --run` bridge are removed.
- Exported SDK target `Chibi::program` (`src/authoring/program.{c,h}`, `program_service.{c,h}`): immutable source snapshots including editor buffers, candidate/accepted generations, transactional prepare/commit/discard, structured diagnostics, synchronous zero-arg void/int calls, and retained-state migration via the existing runtime. Authored API: `program.open/update/call/diagnostic/close/test`.
- Generic OS/file/process/network capabilities live in `src/host/application*.c`, with a new `files.readable` (id 21029). `src/host/process.c` got child-group ownership, parent-only signal propagation, terminal ownership, Ctrl-Z resume, and Darwin START_SUSPENDED handoff. Export/web cancellation is plumbed through compile/native/sign/archive.
- Windows source loading is implemented (pinned handles, reparse and limit checks).
- Full design record: the PLAN.md section "Ordinary Chibi CLI application and shared program sessions, 2026-10-28". Review packet: `.chibi/main-runner-reload/REVIEW.md`.

Evidence reported (macOS only, run by the Codex agent):
- fee955dc2: full SDK configure 1.1 s, ALL build 111.0 s, package 20.6 s. Installed CLI 15 cases / 213 assertions. Isolated C consumer 1/13. Export+relocation 2/25. Studio resource copy: 155 files and a copied CLI that prints "Hello, Studio".
- 1dd12f7c1, repairs and installed checks (receipt `ordinary-cli-sdk-acceptance-1dd12f7c.json`):
  - Runtime/session seam 309 cases / 23,211 assertions. Session 11/174.
  - Raw build-tree CLI 4/39. Browser-readability doctor 1/60.
  - Installed CLI 15/213 (29.4 s). Isolated C API 2/17. Export/relocation 2/25.
  - Studio copied CLI plus 155 resources verified.
  - Earlier, full CLI 35/550 (41 s) and POSIX source 41/17,381. Strict MinGW syntax passed on 13 production files plus the Windows fixture.
  - clang-format 22 passed over all C sources.
- Three root review findings closed at 1dd12f7c, each with a RED control that failed before the fix: `program.call` bypassed the caller's instruction budget; standalone raw SDK layout broke the resource lookup; browser doctor treated presence as readability.

## 5. Next actions (ordered)

1. Snapshot first: copy `git diff` of `test/language/program_session.c` and the staged state to `/tmp`. Check that no Codex process holds `.chibi/main-runner-reload/native` or the worktree (`lsof +D`).
2. Resolve the in-progress merge. Either commit it as is (it already resolved clean, gitdir `.../worktrees/chibi14`) or abort and merge `origin/main` d66cb3beb directly. Keep the fee955dc2 and 1dd12f7c1 ancestry; no rebase or amend.
3. Implement the open P2 (root review, adjacent to the original three, found at 1dd12f7c): an active `@test` can call `program.test` recursively and get a fresh runtime plus a fresh 100M-instruction scope. Root's ask: reject reentrant `program.test` while a test is active, using the existing context/lifecycle. Preserve CLI test parallelism. Keep the bounded regression already written in the unstaged test, and show that a later ordinary run still works.
4. After the merge and guard, rebuild matching tools. Run the affected checks listed in section 6: at least `--build-graph-resources`, `--package-ownership`, `--build-sdk` (changed by #814), plus the CLI and session selectors.
5. Refresh the installed SDK acceptance on the final clean tip. The receipts are bound to 1dd12f7c (`public-sdk-final-plan-1dd12f7c.log`), so any product change makes them stale. Update the PLAN.md evidence section.
6. Independent review (the repo rules require one), then follow the repo's pre-commit list in `chibi/CLAUDE.md`.
7. Publish only after owner confirmation: an engine PR, then a Studio companion PR. Studio's AGENTS.md requires the Studio PR description to name the engine PR dependency. Do not open a PR that only bumps a project version (none is needed; `CHIBI_PROJECT_VERSION` was not touched).
8. Product follow-ups the owner originally wanted (not started): the Studio hello-world starter and standalone-Main run/reload flow (the shared `Chibi::program` API accepts editor snapshots for this), tutorial/doc consolidation, and an API-duplication review. Propose these to the owner after the PR; do not remove APIs without evidence.

## 6. Holds, freezes and coordination rules (do not silently violate)

- Compiler artifact `27d8f3d0568e442e1a70b167c04ba97c671ea0d0b1e85e3a7493d88fc59bb912` (20,792,064 B), bootstrap 2204, runtime E1, private build-host protocol **14**, and public/prepared ABI are all unchanged and frozen. No new compiler artifact, seed, E1 serialization or JIT-offset change, or native-catalog ABI bump. `.inherit` was added only on `behavior_execution_scope` in `src/execution/function.h` and `runner.c`, defaulting to false.
- Accepted main as of the log: 907b1fe02 (#814), then d66cb3beb (#816). Per the ledger, accepted combos are the compiler 27d8 and E1 runtime with protocol 14. #801's compiler pair (`209fdf4e`, 6c0cee1c5) is not admitted; check the ledger before assuming a different compiler.
- Do not import #818 or any held source. Do not use a protocol-13 host: native/sdk hosts must be protocol 14 (`--protocol-version` check).
- Heavy native, SDK or asset producers need coordination. The coordinator said the next heavy IO slot went to the pure/790 recovery archive, and that integration should use affected checks only, not repeats of unaffected suites.
- Codex's cross-chat sends were rejected by auto-review, and the coordinator said "No need to retry". It read the chat and relayed. Report to the owner in chat instead.
- Reviewers pin immutable commits. Keep 9acfa17d0 and 1dd12f7c1 as history, and never amend fee955dc2.
- No global/installed SDK change: `~/.local/libexec/chibi/chibi-build-host` and the installed SDK were untouched.
- Authorization: the coordinator turn gave the Codex agent standing implementation, review and merge authorization. The owner has not asked you to push or open PRs, so ask first. This repo also requires: land the exact tested combination, preserve any explicit hold, and do not claim unrun platforms.
- Repo rules that bite here (`/Users/cwaldner/Personal/chibi/chibi/CLAUDE.md`):
  - Prefer new work in Chibi over C (the owner pushed this twice); any new C needs a stated reason.
  - Tests are pico_unit in `test/tools` or `test/language`; add a case for every behavior change.
  - A new native function or event needs a `test/fixtures/feature-coverage` use or a `coverage-gaps.txt` entry. A comment explaining the application/program/SDK catalogs was added there.
  - Probes go in a new `--<case>` selector of the existing dispatcher, not a new executable.
  - Fail fast (`assert`, `xmalloc`), no defensive NULL checks.
  - `make check-format`, clang-format 22 (`/opt/homebrew/opt/llvm@22/bin/clang-format`).
  - Format hand-edited assets with `chibi_ctl --format`.
  - Chibi code must parse with the older build host.
- User memory notes that apply: wasm builds enforce `-Wmissing-field-initializers` (run `test-wasm` before landing; this thread never did); also run `test-stage-one` / `game-equivalence-stage-one` if stage-zero semantics are touched.

## 7. How to validate

Build command used by the thread (Release, native, ccache, release hosts must be protocol 14; adjust the host paths if you rebuild them under your own scratch dir):
```
CCACHE_DIR="$PWD/.chibi/main-runner-reload/ccache" make build-release \
  RELEASE_BUILD_DIR=.chibi/main-runner-reload/native \
  BUILD_HOST="$PWD/.chibi/main-runner-reload/bootstrap-protocol14/bin/chibi-build-host" \
  ASSET_GENERATOR_HOSTS="$PWD/.chibi/main-runner-reload/bootstrap-protocol14/bin" \
  BUILD_TARGET='chibi chibi-compile chibi-build-host chibi-compiler-host test_language test_tools' JOBS=8
```
Selectors (from `.chibi/main-runner-reload/validate_ordinary_cli.py`, which writes `ordinary-cli-validation.json`):
- Via `python3 tools/test_policy.py tool <name>` with `CHIBI_TOOLS_BUILD_DIR=<native>/test/tools`, `CHIBI_CLI=<native>/sdk/chibi`, `CHIBI_LLVM_BIN=/opt/homebrew/opt/llvm/bin`: `chibi-cli` (35 cases), `chibi-cli-process`, `chibi-cli-build`, `chibi-cli-doctor`, `chibi-cli-installed`, `program-sdk`, `native-build`.
- Export: `CHIBI_EXECUTION_PROFILE=gpu-check python3 tools/test_policy.py native <test_tools> --chibi-cli-export <cli>`.
- Direct driver: `test_tools --build-graph-resources <host> <reference>`, `test_tools --build-sdk <host> <native> ''`, `test_tools --package-ownership`.
- Language: `<native>/test/language/test_language --program-session` (also `--execution-scope`, `--program`, `--package-tests`, `--asset-compiler-host`, `--publication`, `--record-publication`, `--root-state`).
- SDK: `python3 tools/package_sdk.py --build <public-sdk-build> --output <dir> --kind sdk`, driven by `.chibi/main-runner-reload/validate_public_sdk.py`, `validate_program_consumer.py` and `validate_studio_resources.py`. Direct standalone SDK ALL build is required; the private SDK installer is not this proof.

NOT run, or unexecuted here:
- Real Windows or Linux runtime. `native-build` ran 0 cases on macOS (10 Linux-only cases).
- Browser/Wasm (`test-wasm`) and the aggregates `make test-native`, `test-materials`, `test-projects`, `test-language-all`, `test-tools`.
- Any Studio build, compile or UI interaction: only the CMake resource-copy fixture (9 files) and the copied-CLI run were done.
- Backend and device names for the export startup test were not retained. Do not infer hardware.
- Everything after the #814 merge.

Slow steps: full SDK ALL ~111 s (raw standalone 86 s); package ~21 s; `--build-sdk` 294 s; `--build-graph-resources` ~55 s; native combined build ~125 s; process/PTY suite ~15 s.

Not a bug: macOS `cat` returns EINTR after Ctrl-Z/fg. It reproduces in bare zsh and bash (`pty-zsh-baseline.txt`). The tests keep real `cat` for basic input and Ctrl-C and use an EINTR-retrying reader for suspend/resume; no production workaround.

Scratch left behind:
- `/Users/cwaldner/Personal/chibi/.worktrees/main-runner-reload/chibi/.chibi` (1.6 GB): native 610 MB, public-sdk-build 464 MB, public-sdk 176 MB, ccache 121 MB, two studio-real-sdk dirs of 69 MB each, reference 53 MB, bootstrap-protocol14 36 MB, program-sdk-configure 28 MB. Keep receipts, `*-preservation*`, `ordinary-cli-pre-*-forward-port`, `review-fixes-pre-protocol14-merge` and the `.json` evidence. Delete the bulky builds only after final proof and the owner's okay. Report whatever you leave.
- `/Users/cwaldner/Personal/chibi/chibi/.chibi/hello-runner` (222 MB, the old debug build; the preservation backup next to it must stay).
- `/private/tmp/chibi-hello-20261028-7uk5f5kd/HelloProgram.chibi` (4 KB).
- `.chibi/main-runner-reload/STATUS.md` is the thread's own notes, last updated 20:24 MDT and partly stale.

## 8. Risks and things that went wrong

- Stale base: the first runner was built on 17106d788, which is 554 commits behind. The agent claimed hot reload needed design; the owner knew better. Rule: check `origin/main` and open branches before proposing designs. Resolved by porting.
- Language drift: the first two runners were C and then a private `Run.chibi` under `tools/` with `runner.*` capabilities. The owner objected twice. Resolved by apps/chibi plus a generic host. Remaining C is the 52-line launcher, program/session, and the application leaf services; each needs a "why C" answer in the PR.
- Scope growth: a hello-world request became a 60-file change including SDK exports, process/terminal control, Windows source loading and export cancellation. The owner approved the direction but not necessarily this scale. Keep the PR description honest about it.
- Review-found defects, all fixed with RED/GREEN controls:
  - empty option values treated as absent;
  - interruption before Main returned 0;
  - export subprocess hang on parent-only SIGINT;
  - Darwin first-read race before the terminal handoff;
  - signalling reused PID/PGID;
  - the three P2s listed in section 4.
- Environment failures: two "Selected model is at capacity" errors, then the Codex usage limit. None corrupted state. The merge and the test file were left mid-way.
- Known limitations carried: Windows ACL/directory access semantics inherited from the retired `access` implementation (root says hardening, not a blocker); first version runs synchronously; Studio standalone Run is outside this increment.
- Merge risk: `test/tools/main.c`, `test/language/main.c` and `tools/test_policy.py` are shared hot spots (#816, #821).
- The CMake owner will build on `Chibi::program`/application capabilities for bootstrap. Changing those APIs needs a message to the owner, not a silent edit.

## 9. First-30-minutes checklist

1. Read this file, then `/Users/cwaldner/Personal/chibi/CLAUDE.md`, `/Users/cwaldner/Personal/chibi/chibi/CLAUDE.md`, `/Users/cwaldner/Personal/chibi/chibi-studio/AGENTS.md`, and the PLAN.md section named above.
2. In the engine worktree: `git status`, `git log --oneline -5`, `ls <gitdir>/MERGE_HEAD`. Confirm MERGE_HEAD is 907b1fe02 and that `test/language/program_session.c` is the only unstaged change. Save `git diff` and `git diff --cached` to `/tmp`.
3. `gh api repos/waldnercharles/chibi/commits/main --jq .sha` and `git fetch` (with the owner's okay). Confirm main is still d66cb3beb or newer, and check whether #818 or #801 landed.
4. Read the ledger and ask the owner whether the coordinator thread is still in charge of landing and whether Codex will resume after Nov 3.
5. Confirm with `ps` that nothing is running from `.chibi/main-runner-reload`. Check free disk space; the scratch above is large.
6. Decide the merge path (commit vs abort-and-remerge d66cb3beb) and tell the owner.
7. Write the `program.test` reentry guard in `src/authoring/program_service.c`. Rebuild with the command above; first confirm the new regression fails without the guard.
8. Run the section 7 selectors that the merge and guard touch. Record results in PLAN.md and `.chibi/main-runner-reload/`.
9. Report to the owner: where the branch stands, the unrun coverage, the scratch left, and the question of whether to push and open the engine and Studio PRs.
