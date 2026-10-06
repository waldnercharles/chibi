# Native development and workspace recovery

`make dev` builds the ordinary native application in RelWithDebInfo and launches
Studio with `.chibi/workspace.json` as its checkpoint. Native C changes take effect
after rebuilding and restarting the application.

## Development loop

Close Studio, edit the native source, and run `make dev` again. The build reuses
unchanged objects and dependency packages. Build failures report diagnostics and
leave the saved workspace available for the next successful launch.

The native application has no code replacement, source watcher, application DLL,
immutable application generations or restart-required notification. `make run`,
`make debug` and `make dev` all use the same linked application lifecycle.

Authored assets still update while Studio runs. Graph assets and systems update
when their files change; prototypes and the open scene update when the runtime
is stopped. Studio's RML and RCSS assets retain their existing live update path.
These asset updates do not require a native rebuild. Browser C/WASM changes
require rebuilding the web application and refreshing the browser.

## Workspace checkpoints

Normal close, Ctrl-C and SIGTERM save the workspace and exit normally. The next
`make dev` launch imports `.chibi/workspace.json`. Window size and position use
its adjacent `.window` file. Other native launches can opt into recovery with
`--checkpoint PATH`.

Unsaved documents, saved baselines, Undo/Redo, graph navigation and selection
survive the checkpoint. Play restarts from authored state with its prior running
or paused intent. Live simulation state and active pointer gestures are recreated.
A checkpoint is separate from saving source documents.

If startup cannot import the saved workspace, the application preserves it as
`.chibi/workspace.json.rejected-XXXXXX`, prints that path, and starts from project
content. The preserved file retains unsaved documents for recovery after a schema
change. Checkpoints contain stable document data and identities, never native
pointers or generated code.

## Checks

`make test`, `make test-short` and `make test-tools` run the minimal C tooling
suite under one shared 60-second deadline. Native host, generation, reflection
and full editor checkpoint checks remain quarantined, disabled and not passed.
See [the test inventory](test-quarantine.md). Compilation does not establish
runtime coverage.

The retained `test_host` binary covers native process and state-file handling.
`check-host` invokes the quarantine guard, which refuses execution. The binary
remains available for compilation. The unadmitted `--editor-checkpoint` assertions in
the native suite cover dirty documents, Undo/Redo, pre-Play restoration and held
input across binding recreation; their existence is not current execution
evidence. CUT65 removed the Python application-recovery driver and its CMake
check target. Do not execute raw native or CMake checks to bypass quarantine.

CUT65 removed the old `CHIBI_DEVELOPMENT_RELOAD` build mode, `chibi_dll`,
`check-reload`, `check-dev`, staging descriptors and native watcher smoke checks.

Existing `build-dev` directories are reconfigured by `make dev` to use the linked
application. Dependency packages use the new build identity; existing caches stay
on disk. Old generation directories and manifests are no longer read. Existing
workspace checkpoints remain available for restart recovery. Remove the retired
reload option from custom configure commands.
