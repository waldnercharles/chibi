# Native toolchain continuation

The November 2 handoff is being integrated on `codex/native-default-20261102`,
based on `b46f391b7`. The review branch is PR #1331. The owner's checkout and
unrelated work remain untouched; existing compiler worktrees supplied the
independent driver, CLI, producer, maps, tasks, Windows and reload lanes.

## Order of work

1. Make the SHA-256-verified native seed the normal build entry and make native
   compilation failures stop the build.
2. Integrate native driver caching, CLI commands, OS operations and LLVM setup.
3. Replace VM-dependent behavior with tested native maps, tasks and retained
   program generations. Keep unsupported forms explicit.
4. Move the installed product, SDK asset compiler and remaining test consumers
   to the native path, then remove their hosts and bytecode seeds.
5. Delete the VM only after the replacement passes the remaining behavior and
   platform checks. Remove obsolete aliases, flags, tests and workflows with it.

The user approved program-owned tasks with parent-child cancellation, suspended
tasks finishing in their original code generation after reload, and LLVM 22 as
the minimum. Task deadlines and lifetime after the last handle drops remain
part of the parity requirement.

## Implemented in the integration branch

- The native seed and native authored build programs are the default. Their
  caches preserve previous outputs on rejection; the automatic VM fallback is
  removed. Native recovery and the vendor sample need no C compiler or VM host.
- The native CLI supports build, run, test, version and doctor, plus Linux
  `run --watch` with retained-state migration and rejected-edit recovery. Source discovery,
  cache receipts, content invalidation, thin-archive rejection and interrupted
  publication have real compiler regressions.
- LLVM/LLD 22 or newer is required before compilation writes outputs. Verified
  cache hits avoid repeating version subprocesses.
- Native maps support scalar/string values, owned copies, updates, deletion,
  parameters/results and references. Native tasks use reusable generational
  handles, ordered scheduling and explicit cancellation/shutdown cleanup.
- Native Linux shared generations support numeric/bool retained records and
  standalone strings, schema checks, runtime failures and process context.
  The host advances active tasks and keeps retired tasks in their original code.
- Windows has filesystem/cache operations, a cross-build route and verified
  LLVM installation that extracts only the tools and DLLs it uses.
- Legacy asset generation, host test selectors and explicit bytecode recovery
  prepare their seed at their own boundary. Native-only routes skip that seed.

## Verification recorded so far

All local final checks below used LLVM/LLD 22.1.8 from `/tmp/chibi-llvm22`.
Earlier local lane checks used a mixed LLVM 23 / LLD 22 directory; they are not
minimum-version evidence.

- The integrated compiler's stages three and four are byte-identical:
  `702dff527162e6c7397a59af9675a64da287a7f647bca2552afb91703a394f5f`.
  Stage two took 40 s, three 33 s, four 31 s. The full proof, including a
  Windows cross-build (29 s) and native driver/graph checks, took 138.76 s.
- The integrated macOS cache, LLVM minimum, CLI, maps, tasks, seed-boundary and
  bootstrap-entry checks passed in 36.75 s. Map and task tests execute at O0/O2
  and check allocation balance; map tests also poison released allocations.
- Linux CI passed the no-C-compiler recovery/vendor proof at `5a789a2a3` in
  1m50s. Earlier native self-host and compiler self-build CI passed before the
  maps/tasks/reload integrations; final branch CI must be assessed separately.
- Broader CI exposed the removed implicit bytecode-seed dependency and then a
  missing `ScalarMaps.chibi` source-list entry. Both are fixed in this branch.
- The first Windows job downloaded LLVM in about five seconds but timed out
  during checksum/extraction. Selective extraction has PowerShell fixtures;
  native Windows execution still needs successful CI evidence.

The native Linux generation suite passed in 37.184 s, including real reloads,
active task progress, active and retired task failures, and rejection of waiting
Main/Frame entries. The full native watch regression passed in 11.49 s,
including old-generation task completion, empty PATH, deleted-import restoration
and creation of a missing import. The warm native build-entry census took
0.56 s. Final CI results are recorded in the PR description. Cross-linking alone
is not runtime validation.

## Gates before deleting the hosts or VM

- Install the native CLI/compiler as the product and replace the packaged SDK
  asset compiler. The engine graph and installed SDK still build/use C hosts.
- Complete native typed failure reporting and `core:testing.ExpectFailure`;
  preserve the existing failure categories and imported-test behavior.
- Complete maps: authored iteration, equality, compound writes, inferred
  literals, and nested/record values. Unsupported forms must reject.
- Complete tasks: handle result unions, deadlines/delays, managed suspended
  values, component dependencies and complete failure behavior.
- Complete retained-state parity for managed records, arrays, unions and task
  handles, and validate reload/watch beyond Linux x64.
- Complete Windows asynchronous process operations, a verified native Windows
  seed, native Windows execution, and remaining SDK/Studio platform coverage.

Do not remove the VM, host sources or recovery assets while those consumers
still depend on them. This branch removes the build-entry fallback; it does
not claim that the full retirement is complete.
