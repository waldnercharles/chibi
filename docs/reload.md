# Native development and workspace recovery

Studio builds and launches from `chibi-studio`. The engine's `studio-dev`,
`studio-run` and `studio-build` Make targets were deleted with Studio packaging
in 7b97ecf7c, and the Makefile itself in e4aa7b4e2. Native C changes take effect
after rebuilding the owning SDK or Studio product and restarting the
application.

## Development loop

Close Studio, edit the native source, rebuild and relaunch it. Build failures
report diagnostics and leave the saved workspace available for the next
successful launch.

The native application has no code replacement, source watcher, application DLL,
immutable application generations or restart-required notification.

Authored assets still update while Studio runs. Graph assets and systems update
when their files change; prototypes and the open scene update when the runtime
is stopped. Studio's RML and RCSS assets retain their existing live update path.
These asset updates do not require a native rebuild.

Authored executable publication still owns immutable modules, state matching,
task cancellation and late-request safety. The preview and runtime callers
preserve these obligations when authored sources change.

## Workspace checkpoints

Normal close, Ctrl-C and SIGTERM save the workspace and exit normally. The next
launch with `--checkpoint PATH` imports it. Window size and position use its
adjacent `.window` file.

Unsaved documents, saved baselines, Undo/Redo, graph navigation and selection
survive the checkpoint. Play restarts from authored state with its prior running
or paused intent. Live simulation state and active pointer gestures are recreated.
A checkpoint is separate from saving source documents.

If startup cannot import the saved workspace, the application preserves it as
`PATH.rejected-XXXXXX`, prints that path, and starts from project content. The
preserved file retains unsaved documents for recovery after a schema change.
Checkpoints contain stable document data and identities, never native pointers
or generated code.

## Checks

`chibi build test unit` runs six pure C execution-policy cases.
`chibi build test short` runs the complete C tooling/process integration group.
They do not establish workspace recovery. See the current
[test instructions](testing.md) and [test inventory](test-inventory.md).
Compilation does not establish runtime coverage.

The retained `test_host` binary (`test/host`) covers native process and
state-file handling. Studio's checkpoint assertions belong to `chibi-studio`.
CUT65 removed the Python application-recovery driver, the old
`CHIBI_DEVELOPMENT_RELOAD` build mode, `chibi_dll`, `check-reload`, `check-dev`,
staging descriptors and native watcher smoke checks.
