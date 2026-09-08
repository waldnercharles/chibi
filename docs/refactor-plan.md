# Repository refactor and automatic reload

Implementation tracker for the approved September 7 plan. Keep this file current
as milestones are verified; a checked item means implementation and acceptance
checks are complete.

## Contract

Target macOS and Linux for native reload; retain static native and web builds.
Preserve unsaved documents, saved baselines, Undo/Redo, selection, graph cameras,
navigation and docking. Recreate Play after accepted code changes, retaining its
running/paused/stopped intent. Future live simulation migration is separate.
Failed builds, loads or checkpoint imports leave the working generation intact.
Host/framework changes request manual restart with checkpoint recovery.

## Milestones

- [x] 1. Reliable baseline: native interaction failures fixed; explicit formatting;
  atomic changed-only generation; obsolete CI removed; tests organized by concept.
- [x] 2. Remove unused Gameplay documents and relocate fixture-only assets/packs.
- [x] 3. Concept folders and explicit document/runtime ownership; canonical editing
  commands; graph/runtime/reflection monoliths decomposed by responsibility.
- [x] 4. Real Cute/ImGui declarations and Cute bindings for application actions.
- [x] 5. One reloadable application, transactional loader, workspace checkpoints,
  native build controller, manual restart recovery and real successive-library tests.

## Validation

Run focused checks during each change and full native checks at milestones.
Verify input routing, actual rendered UI, Undo/Redo and scene/prototype parity.
Reload validation includes invalid candidates, repeated replacement, resources,
JIT recreation, callbacks, edit/history retention and manual process restart.
Keep static builds and web generation working. Never commit generated build files.

## Implemented structure

The graph editor coordinator is now 352 lines (previously 8,155); scene authoring
is 1,395 (previously 7,170). Canvas, document commands/history, panels, runtime
sessions and viewport code have their own files. Reflection is separated into
collection, identity, emission and publication. Graph serialization separates
values, writing, migration and clipboard data; runtime host services and watches
are separated from execution.

Unused Gameplay documents, their nine content assets and their reflection types
are removed. Reflection examples and example graphs live under test fixtures and
support. Unused root blueprint data, copied framework declarations, legacy reload
helpers and production test scenarios are removed. All authoring uses the editor's
canonical commands. Production headers use the actual Cute and ImGui declarations.

The public include tree contains the application lifecycle ABI. Internal headers
stay beside conceptual modules under src. Compilation ownership is explicit in
cmake/sources.cmake. Build/test commands no longer rewrite source formatting;
generated sources and component identity manifests are published atomically only
when their contents change. CI uses the supported test path and exercises native
reload under Linux/Xvfb.

## Acceptance evidence

- `make test` passes with ASan/UBSan: graph/runtime/ECS, generation,
  checkpoint/input restoration, real loader rejection and replacement, native
  watcher/process tests, and static application startup. `make ui-native` runs
  focused native workspace interactions, while `make render-checks` runs the
  GPU readback checks separately.
- make build, make check-format and git diff --check pass.
- Static Emscripten app.html, JavaScript, WASM and packaged content build. Browser
  rendering has not been tested in this refactor.
- Actual application smoke passes three in-process replacements, checkpointed
  restart. Window geometry is also retained.
- Actual watcher acceptance passes: a compile failure preserves the process;
  application edits replace code in the same process; resident edits request manual restart from the
  checkpoint; restored source reloads and Ctrl-C saves the workspace.
- A 3,840 by 2,160 native capture was inspected with the full editor UI rendered.
- Full editor acceptance ran on macOS. Native watcher, process and controller
  tests also passed on Linux with ASan/UBSan; full Linux editor checks run in CI.
- The native controller fixture passes compile failure, source edits during slow
  builds, same-process replacement, resident restart notices, manual checkpoint
  restoration and graceful shutdown during an active build. The real editor
  smoke passes four successive generations and manual checkpoint restoration.
- CMake generation and reflection tests replace Python drivers, with concurrent
  native subprocesses, unchanged timestamps and failed-publication checks.
  The Python supervisor and its automatic SIGUSR1 restart protocol are removed.

The initial user edit to AGENTS.md is preserved. No generated build files belong
in the source change. Future live simulation migration remains outside this plan:
Play intentionally restarts from the authored scene after code replacement.
