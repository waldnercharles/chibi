# Native development and workspace recovery

`make studio-dev STUDIO_SOURCE=/studio STUDIO_SDK_PREFIX=/sdk PROJECT_ROOT=/game` builds the separate Studio consumer in the selected native profile and launches
Studio with `build-studio/workspace.json` as its checkpoint. Native C changes take effect
after rebuilding the owning SDK or Studio product and restarting the application.
`STUDIO_CMAKE_FLAGS` chooses the profile; `STUDIO_CHECKPOINT` selects the recovery file.

## Development loop

Close Studio, edit the native source, and run `make studio-dev STUDIO_SOURCE=/studio STUDIO_SDK_PREFIX=/sdk PROJECT_ROOT=/game` again. The build reuses
unchanged objects. Build failures report diagnostics and
leave the saved workspace available for the next successful launch.

The native application has no code replacement, source watcher, application DLL,
immutable application generations or restart-required notification. `make studio-run` and `make studio-dev` use the same linked application lifecycle.

Authored assets still update while Studio runs. Graph assets and systems update
when their files change; prototypes and the open scene update when the runtime
is stopped. Studio's RML and RCSS assets retain their existing live update path.
These asset updates do not require a native rebuild. Browser C/WASM changes
require rebuilding the web application and refreshing the browser.

Authored executable publication still owns immutable modules, state matching,
task cancellation and late-request safety. The preview and runtime callers
preserve these obligations when authored sources change.

## Workspace checkpoints

Normal close, Ctrl-C and SIGTERM save the workspace and exit normally. The next
`make studio-dev STUDIO_SOURCE=/studio STUDIO_SDK_PREFIX=/sdk PROJECT_ROOT=/game` launch imports `build-studio/workspace.json`. Window size and position use
its adjacent `.window` file. Other native launches can opt into recovery with
`--checkpoint PATH`.

Unsaved documents, saved baselines, Undo/Redo, graph navigation and selection
survive the checkpoint. Play restarts from authored state with its prior running
or paused intent. Live simulation state and active pointer gestures are recreated.
A checkpoint is separate from saving source documents.

If startup cannot import the saved workspace, the application preserves it as
`build-studio/workspace.json.rejected-XXXXXX`, prints that path, and starts from project
content. The preserved file retains unsaved documents for recovery after a schema
change. Checkpoints contain stable document data and identities, never native
pointers or generated code.

## Checks

`make test` and `make test-short` run six pure C execution-policy cases.
`make test-tools` runs the complete C tooling/process integration group.
Individual reflection/source/publication routes have their own scoped coverage;
they do not establish workspace recovery. See the current
[test instructions](testing.md) and [test inventory](test-inventory.md).
Compilation does not establish runtime coverage.

The retained `test_host` binary covers native process and state-file handling.
The `--editor-checkpoint` assertions in the native suite cover dirty documents,
Undo/Redo, pre-Play restoration and held input across binding recreation; their
existence is not current execution evidence. CUT65 removed the Python application-recovery driver and its CMake
check target.

CUT65 removed the old `CHIBI_DEVELOPMENT_RELOAD` build mode, `chibi_dll`,
`check-reload`, `check-dev`, staging descriptors and native watcher smoke checks.

Existing `build-dev` directories are reconfigured by `make studio-dev STUDIO_SOURCE=/studio STUDIO_SDK_PREFIX=/sdk PROJECT_ROOT=/game` to use the linked
application. Old generation directories and manifests are no longer read. Existing
workspace checkpoints remain available for restart recovery. Remove the retired
reload option from custom configure commands.
