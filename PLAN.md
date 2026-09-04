# Remaining Issues Implementation Plan

## Summary

Implement the open issues in dependency order: finish verification for #2/#25, complete runtime controls (#4), harden authoring/live-edit semantics (#9), build the editor-authored movement/weapon/projectile/enemy loop (#5–#7), prove native/bytecode parity (#8), then run the end-to-end acceptance workflow (#10).

The repository already has substantial foundations: Behavior Asset graph persistence and typed commands, compile diagnostics, Prototype serialization/spawning, a separate Prototype Preview world, reflected `GameTransform`, `GameHealth`, `GameMovement`, `GameInput`, `GameWeapon`, and `GameProjectile`, plus `InputBehavior.json` and `MovementSystem.json`. However, the gameplay JSON files are currently generic data assets, not a complete Prototype/Behavior composition. Weapon firing, projectile lifetime, targeting, collision/damage, and the complete editor path are absent.

## Acceptance comparison

- **#2:** Largely implemented in `core/behavior_asset`, `editor/editor.[ch]`, serialization, and `test/test_suite.c`; verify remaining UI graph selection/search, literal editing, diagnostic navigation, and full save/reload/screenshot acceptance.
- **#25:** `test/ui_integration.c`, `make ui-integration`, deterministic injected input, semantic state checks, trace, command output, and screenshots exist. Strengthen negative/repeatability checks and CI integration; the current scenario still reaches the asset with fixed coordinates rather than a fully semantic interaction seam.
- **#3:** Model-level prototype composition, persistence, attachment, and isolated preview are implemented and tested. The editor/API needs the complete user workflow needed by later issues.
- **#4:** Engine play/pause/step and app target APIs/UI exist, but unified target-specific inspection, deterministic restart/stop, and complete Entity/Component/Behavior Instance/log queries are incomplete.
- **#9:** Undo/redo, sparse overrides, working/applied snapshots, atomic save helpers, and compile diagnostics exist. Missing/probably insufficient are visual inherited-vs-override state, propagation preserving explicit overrides, transactional failure guarantees, and an explicit live-edit policy.
- **#5:** Reflected movement/input and movement behavior examples exist, but no complete Player Prototype + attached behavior + editor Preview acceptance scenario.
- **#6/#7:** Component descriptors exist, but no usable projectile spawning/lifetime, weapon behavior, enemy targeting, collision, or damage graph/runtime implementation exists.
- **#8:** Generic/typed bytecode and native SLJIT facilities exist for systems and benchmarks, but no gameplay parity fixture, explicit portable-WASM execution proof, or backend-specific diagnostics exists.
- **#10:** Blocked by #7/#8/#9.

## TDD implementation sequence

### 1. Lock #2/#25 baseline

- Add failing pico-unit coverage for graph create/rename/select, reflected node search, literal edits, invalid connection diagnostics and navigation, save/reload dirty state, and compile results.
- Refine `test/ui_integration.c` to assert Content Drawer, active Behavior Asset/document, visible Node Canvas, graph identity, expected node, and diagnostics through observable semantic state while retaining real native mouse/double-click input.
- Add a deliberate failure-path fixture and repeated-run assertions. Preserve command output, `interaction-trace.txt`, screenshot, and optional frame artifacts.
- Integrate `make ui-integration` into native CI where available and update `PRODUCT.md`, `docs/manual-verification.md`, and repository guidance only where invocation/evidence wording is incomplete.

### 2. Complete #4 runtime controls and inspection

- First add API tests for Preview and Scene targets covering Play, Pause, Step, Restart, Stop, deterministic state transitions, separate worlds, and no authored history changes.
- Extend the app query state to expose selected Entity, reflected Component descriptors/values, attached Behavior Asset/Instance data, logs, diagnostics, and Graph/node origins. Reuse `game_behavior_attachment_info`, compile diagnostics, and engine state; do not create a parallel runtime model.
- Route UI buttons and typed C commands through the same target-neutral operations. Restart only the selected world; Stop clears borrowed inspection references safely.
- Add UI integration checkpoints for labels/state, Entity selection, visible Components/Behavior Instances, and origin-aware logs/diagnostics.

### 3. Harden #9 history, overrides, save, and live edits

- Add red tests for one history entry per user action, redo invalidation, rejected edits producing no entry, and runtime transitions staying out of authoring history.
- Track/query field origin (`inherited` versus `override`, with Prototype default as applicable) and render distinct Inspector labels/style/tooltips.
- Propagate changed Prototype defaults only to matching live fields that still inherit; preserve sparse Scene overrides. Test multiple entities with mixed origins.
- Make save/apply transactional: atomically persist working data, compile a clone, and activate only after both succeed. On write/compile failure retain the last valid runtime snapshot, working dirty state, and actionable diagnostics.
- Adopt and document the safe default policy: **stop, save/apply, then explicit restart**. While running, edits remain working-only and set apply-required state.

### 4. Implement #5 player movement

- Add an integration test loading a real Player Prototype, attaching movement Behavior Asset, spawning Preview, dispatching deterministic input, advancing fixed time, and asserting Transform movement plus Component/Behavior Instance inspection.
- Add canonical Prototype/Behavior content using reflected `GameTransform`, `GameMovement`, and `GameInput`; migrate or supplement generic `content/gameplay/Player.json` rather than treating it as completed runtime composition.
- Expose component editing, behavior attachment, save/reload, Preview controls, and inspection through editor commands/queries and the real Prototype Editor. Verify all nodes/components in registry packs and UI allowlists.

### 5. Implement #6 weapon/projectile

- Define minimal reflected durable state and operations for projectile Transform/speed/damage/lifetime, weapon cooldown/damage/projectile reference, and spawning a Prototype with authored defaults.
- Add registry-backed graph nodes for cooldown/fire, spawn Prototype, component access, and destroy/expire. Implement ECS effects in the game layer, not `core`, and attach stable node origins to logs/diagnostics.
- Add real projectile/weapon assets and TDD scenarios asserting fire → Entity/Behavior Instance spawn → movement → lifetime expiry → save/reload/restart. Add UI inspection/editing coverage.

### 6. Implement #7 enemy/damage

- Add red tests for reflected enemy Transform/Health, attached behavior, deterministic target selection, damage application, runtime health inspection, and diagnostics.
- Implement the smallest deterministic target/collision operation needed (distance/radius or explicit target query) as game-backed graph nodes; no handwritten gameplay controller.
- Add enemy and damage assets and assert the complete movement → fire → projectile → target → health reduction sequence with fixed time/entity ordering. Ensure logs identify Entity, Behavior Asset, Graph, and node.

### 7. Prove #8 backend parity

- Build one backend-neutral gameplay fixture from shared Graph IR and compare observable hashes/traces for movement, spawning, projectile expiry, targeting, and health.
- Assert native `AUTO` chooses the compiled fast path and the portable mode uses bytecode without SLJIT/native executable memory. Add a WASM-configured build/runtime assertion.
- Standardize backend/Graph/node/pin lowering diagnostics and test malformed/unsupported graphs preserve the previous valid snapshot.
- Keep correctness separate from performance; extend/add a gameplay benchmark reporting native and bytecode measurements independently. Re-run native regression, UI, screenshot, and web workflows.

### 8. Complete #10

- Add one deterministic real-editor interaction trace covering Prototype/Behavior Asset creation/opening, reflected field and graph literal edits, save, compile, diagnostic repair, runtime controls, Entity/Component/Behavior Instance inspection, stop, reload, and the complete gameplay slice.
- Assert canonical terminology in visible labels/messages and remove or update duplicate legacy movement-only surfaces.
- Run `make ui-integration` and inspect `build/ui-integration-artifacts/`; validate 1920×1080, non-black, no magenta sentinel, command output, trace, and any decoded recording evidence.
- Close each issue only with direct test/trace evidence; close #10 last.

## Verification commands

- `make test`
- `make ui-integration`
- `make check-format`
- `make benchmark` (separate performance evidence)
- `make web` and local HTTP/browser verification of the bytecode path
- Inspect `build/ui-integration-artifacts/` and native screenshot dimensions/pixels.

## Assumptions/defaults

- GitHub blocker order is authoritative; existing lower-level APIs do not count as feature completion without acceptance evidence.
- Scope is only the first gameplay loop; waves, progression, shops, bosses, polish, and a JSON/RPC adapter remain out of scope.
- Prototype/Behavior Asset content is the canonical runtime authoring source; generic gameplay JSON is migrated or supplemented.
- Live edits use stop/apply/restart and never implicitly mutate a running Runtime World.
- Fixed timestep/input sequences and observable state hashes are preferred over wall-clock assertions.
- Existing atomic save helpers may be reused, but filesystem and compile/apply failure behavior must be tested.
- Every new node must be added to the correct generated registry pack and all UI context filters, then verified by name in the spawn menu.
