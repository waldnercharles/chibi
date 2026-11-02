# Native toolchain continuation

Resume the November 2 handoff against `b46f391b7`, with the verified native seed
as the normal build entry. Preserve runtime behavior before removing the VM.

The seed pin is merged. Native self-reproduction has previous CI evidence, but
the build entry still defaults to a C host and its runner silently falls back
when native compilation fails. The remaining draft branches cover driver
parity, CLI execution, Windows, maps, tasks, producer gaps and Edit and Continue.

## Changes

- `tools/build.sh`, `tools/bootstrap/native-seed.sh`: select and verify the native
  seed by default, reuse verified cached downloads, resolve LLVM explicitly, and
  remove the VM bootstrap fallback from the entry.
- `tools/toolchain/Driver.chibi`: run authored programs natively, propagate failed
  compilation, avoid bootstrapping hosts that these programs no longer need, and
  let native self-build verification run without VM comparisons.
- `test/bootstrap/native-entry.sh`, `docs/native-build-runner.md`: exercise and
  document cold, warm, failed and invalidated native entry behavior.
- Driver lane, existing `claude/native-driver-options`: root discovery, content
  cache, receipts, preservation of previous outputs, and native CLI integration.
- CLI lane, existing `claude/native-chibi`: native build/run/test routes and
  diagnostics. Connect watch only after the retained-state host is proven.
- Producer, maps, tasks and EnC lanes: complete behavior in their existing
  worktrees and integrate reviewed commits serially.
- Windows lane: repair LLVM setup, run self-reproduction and supply OS operations
  needed by the native build runner.

The user approved program-owned tasks with parent-child cancellation, keeping
suspended tasks in their original code generation across reload, and LLVM 22 as
the minimum. Preserve deadlines and task lifetime after the last handle drops.

## Verification

Use a SHA-256-verified release seed and cached LLVM tools. Run real native
compile/run checks for each change, including error and interrupted-build cases.
Run the bootstrap-entry fixture and real cold/warm entry. Compare self-build
stages at the integrated revision, then exercise native CLI, retained-state reload,
and the affected compiler corpus. Record elapsed times. Require Linux, macOS and
Windows evidence before claiming the platform migration complete.

The build graph still contains C products and a packaged compiler generator.
Delete those callers before removing their hosts. Delete VM code, seeds, aliases,
tests and workflow routes only after the replacement passes its behavior checks.
