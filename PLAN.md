# Editor-first gameplay: cleanup and next slice

## Goal and architectural rule

Build an Unreal-style visual event graph capable of authoring a small Neon-Swarm-like game. All game rules live in Behavior Assets, composed onto Prototypes through the editor. C provides generic mechanisms (input events, reflected component access, entity creation/destruction, queries, rendering), not a second gameplay controller. A `FireWeapon` node hiding an entire weapon implementation would not satisfy this rule.

## Inspection findings

- Open issues #2, #4–#9, #10 and #25 describe the right broad workflow, but older screenshot/computer-use acceptance language is not the correctness gate.
- The initial `make test` failed: the UI harness omitted components needed by Player and depended on fixed coordinates and persisted window settings. A fresh Content Drawer also clipped asset cards because its default width was 280 pixels.
- The suite included unconditional success (`|| 1`, `check(1)`) and registry-count checks that did not establish the behavior described by their labels. Delete those claims rather than replace them with more implementation trivia.
- The checkpoint's `game/gameplay.c` implements firing, hardcoded projectile defaults, movement, collision and damage in C. `app/main.c` also encodes movement speed. These are temporary scaffolding, NOT completion of #5–#7. Remove them as graph-authored replacements land; do not build further features on them.
- `../repos/Neon-Swarm/src` includes movement, projectile/weapon rules, enemy behaviors, waves, upgrades, shops and effects. Port only the first observable loop, not its entire architecture.

## Verification contract

- `make test` is the required gate: sanitizer-enabled regression checks, real native ImGui interaction assertions, and the resize interaction assertion. `make ui-integration` is an alias, not another evidence pipeline.
- Headless means a hidden native window; the UI still requires a working graphics backend. Pure model checks are not a substitute for native interaction coverage.
- Default evidence is exit status and assertion output. Optional app screenshots are diagnostic only. No video encoder, cursor overlay, frame dumps, image golden files, or LLM walkthrough requirement.
- Tests should fail for a plausible product regression: wrong graph opened, edit lost after reload, undo changes the wrong object, rejected save replaces the running snapshot, input produces wrong displacement. Constants, nonnegative catalog counts and self-comparisons are not acceptance tests.
- Keep behavioral coverage at existing authoring/runtime command interfaces and a small real-UI smoke path. Do not create a parallel automation editor, RPC protocol or generic test framework.

## Small, ordered changes

### 1. Recover the safety net (#2/#25)

First cleanup removes video machinery and false assertions, fixes the Content Drawer default size, and locates rendered asset cards by path. The harness ignores personal ImGui settings and drives real clicks to open the behaviors folder and InputBehavior.

The next cleanup replaces the thumbnail/list drawer, breadcrumbs, folder history and duplicate source browser with one docked Content file tree. The native test now covers folder collapse/expand, opening Behavior Assets and Prototypes, creating both asset kinds, Prototype Save As/reload through the Document menu, discovery of the saved file, unmatched search, and closing/reopening Content. Prototype edits in this scenario use the shared field-edit command; saves use an isolated temporary directory. Creating a Behavior Asset after navigating through another asset exposed a stale graph-preview cache and a borrowed temporary document name; both ownership errors are fixed.

Remaining: share native/test startup to prevent component-registration drift; drive reflected field edits and graph edit/save/reload through widgets as well as commands; cover entity creation, selection, editing and deletion; assert rendered graph/node visibility, not just model presence. Move the surviving app-side authoring/resize scenarios into the same harness when touching their startup. Do not close #25 on the current scenarios alone.

Component Get/Has nodes now have data outputs only. Contextual reads use the activation's service snapshot, copy component values into activation storage, and re-evaluate for each consuming execution step. Regression tests cover a read before/after a write, Has with/without the component, missing-Get diagnostics, and migration of legacy execution chains. The loader preserves data pin IDs and bypasses the old execution pins; invalid cyclic reader wiring is rejected rather than silently erased. Bundled InputBehavior uses the new layout. The native harness opens it, places Has through the shared authoring command, reveals the reader and asserts the pins actually drawn in the canvas. Reveal now focuses the canvas tab.

Next: replace C movement/firing policy with the authored gameplay slices below; particles follow movement and shooting. Expand actual spawn-menu and widget-edit interaction coverage alongside those assets.

### 2. Finish one editing transaction (#2/#9)

Use the same operations from UI and tests: add a reflected node, connect typed pins, edit a literal, undo/redo, save, reload, compile. Check exact persisted values and rejected-operation behavior. Keep working data distinct from the last valid compiled runtime snapshot. Adopt stop/apply/explicit-restart; no silent live mutation.

### 3. Author movement end to end (#4/#5)

Compose Player with reflected input/movement/transform state. Author input and Tick graph logic, including speed and direction. Fixed input and delta time must produce independently calculated displacement after save/reload/restart. Assert Play/Pause/Step/Stop, isolated preview and scene worlds, and observable component values. Remove C movement policy once this works. This is the first usable milestone.

### 4. Author one weapon, projectile and enemy (#6/#7)

Add only missing generic graph operations required by the fixture. Prototype spawning must use authored defaults. Put cooldown, direction, lifetime, target selection, hit response and health subtraction in graphs. Verify a fixed input sequence creates the expected projectile, moves it, changes enemy health, and destroys it on expiry/hit. Remove `game/gameplay.[ch]` and its runtime hooks with these replacements. Every operation must be registered and discoverable by name in the relevant spawn menu.

### 5. Broaden only after the loop works (#8/#10)

Replay the same authored fixture through compiled native and portable bytecode paths and compare observable outcomes. Actual WASM execution is separate evidence, not inferred from native bytecode tests. Extend the real-editor interaction path across the completed slice. Keep performance measurements separate from correctness.

## Explicitly deferred

Full Neon-Swarm port, waves, shops, progression, bosses, visual polish, general-purpose automation infrastructure, video/screenshot acceptance systems, and further compiler/backend architecture work not required by the slice. Existing meaningful compiler/ECS regression coverage stays.
