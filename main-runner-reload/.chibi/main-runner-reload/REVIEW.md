# Ordinary Chibi application review packet

Owner: Explain Chibi Hello World, worktree main-runner-reload/chibi, branch codex/chibi-main-app-20261028.

## Changed contracts and selected checks

- Public command policy moves from chibi_main.c into the ordinary apps/chibi package. Authored parser/status tests and public CLI command/process/export/installed cases cover routing, errors, argv, native-driver forwarding, cache behavior, interruption and cleanup. Self-invocation exercises the same program path as user Main.
- Chibi::program owns immutable source snapshots, candidate/accepted generations, transactional replacement and diagnostics. Native program-session cases cover ownership, handles, source buffers and rejected drafts. Existing program/publication/record/root-state/module cases cover bytecode and platform compiled retained-state behavior.
- Application services expose generic OS leaves and typed SDK/project facts. Parent-only signals, actual terminal input, suspension/resume, redirected stdin, child descendants and export cleanup require C process-boundary cases; authored tests cover expressible policy.
- SDK/CMake/Graph export the program target and install ordinary app/core resources. Public header consumers, graph/reference parity, actual full SDK assembly and source-free relocated consumers establish packaging. Studio's separate change copies these resources beside its embedded CLI; the resource fixture covers this packaging-only change.
- Windows source input is implemented because normal app startup now needs package loading even for version/doctor. Native POSIX path cases and strict MinGW production/fixture syntax provide local coverage. Real Windows and Linux execution remain explicit external gates; Studio UI behavior is outside this increment.

## Review

Independent ownership/session, CLI/native-driver and whole-change interaction reviews found and corrected empty explicit option values, interruption before Main, worker signal polling, terminal job-control behavior and export subprocess cancellation. Final correction review and validation receipts follow in the task logs. No held protocol14/private-host result overlay or compiler seed publication is included.

## Preservation

original-preservation-final.json verifies original checkout HEAD, tracked binary diff and three untracked files byte-identical. Earlier implementation checkpoints remain under the task directory. Global installed SDK is unchanged.
