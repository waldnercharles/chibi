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
- Legacy compiler-resource staging, host test selectors and explicit bytecode
  recovery prepare their seed at their own boundary. Native-only routes skip
  that seed; host-only SDK builds prepare it before copying compiler resources.

## Verification recorded so far

Final local checks used LLVM/LLD 22.1.8 from `/tmp/chibi-llvm22`. Earlier
checks using a mixed LLVM 23 / LLD 22 directory are not minimum-version evidence.

- At `9f9e66c7a`, macOS native stages two and three are byte-identical:
  `789bdf44015f6c8e80fe854383e3334112b9f53798543f79e78998de6772ce19`.
  They took 40 s and 36 s; the full proof, including native driver/graph checks,
  took 80.50 s.
- Linux CI at `9dae90af7` passed the complete native suite, no-C-compiler proof,
  default full build, SDK relocation, and legacy compiler self-build with its
  strict scalar regression. Its native stage-three/four fixed point was
  `fddf00d2ebdfcb62e6cfb3cd058e44e84dd6820cffddbf71b65628bccf76633a`.
- Windows CI at that revision passed filesystem/object checks and native
  self-reproduction. Stages three and four took 325 s and 321 s and both produced
  `b1bc3c1e4dac87472fdb624e471f21b368ff9d76f97facc54aa4d722db8a5490`.
  Compilation remains serial until asynchronous processes land. Verified
  selective LLVM extraction also passed on the real Windows runner.
- Final native generation and watch suites passed in 37.46 s and 11.92 s.
  They cover rejected edits, retained records/strings, active and retired tasks,
  task failure, Main once, empty PATH and missing-import recovery. The legacy
  bulk-state regression preserves `1,2,3,113,123`, including a new binding's
  initializer; bool and float defaults retain their encoded values.
- All 408 authored compiler tests passed in 84.62 s after moving four scalar
  facts tests into their proper test layer. The moved tests also passed through
  the strict bytecode/SLJIT entry with identical artifacts and zero fallback.
  All 160 original emitter tests separately passed natively in 2.85 s.
- The final C scalar selector passed 10/10 tests and 2,456 assertions in 13.26 s.
  Target metadata checks accept LLVM 22's canonical macOS layout and still
  reject incompatible, missing and duplicate metadata.
- Real SDK installation/relocation passed locally in 26.83 s after a 5.11 s
  incremental host/resource build. Graph planner tests passed through their
  native fixture in 14.10 s and exact legacy-host CI route in 20.16 s.
- Map/task O0/O2 tests check allocation balance; map tests poison freed memory.
  The warm native build-entry census took 0.56 s.

The broader engine CI at `9dae90af7` exposed the test-layer dependency, LLVM
layout fixture and legacy retained-default defect described above. All three
have focused passing checks in the final source tree. The next CI run verifies
them together; its status is recorded in the PR. Cross-linking alone is not
runtime validation.

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
  seed, content-safe invalidation for its build-runner/graph consumers, and
  remaining SDK/Studio platform coverage. Compiler execution and self-reproduction
  on Windows now have CI proof.

Do not remove the VM, host sources or recovery assets while those consumers
still depend on them. This branch removes the build-entry fallback; it does
not claim that the full retirement is complete.
