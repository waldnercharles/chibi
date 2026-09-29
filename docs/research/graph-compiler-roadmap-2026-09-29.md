# Graph compilers: optimization and extension roadmap

This is an audit of the behavior compiler, the shader and field compilers, the
ECS they run on, and how authored graphs read. It was done on 2026-09-29 and
builds on [the graph language research](graph-language-2026-09-23.md); proposals
already made there are only referenced here. Flecs mechanisms come from
[Sander Mertens' documentation](https://github.com/SanderMertens/flecs/tree/master/docs).

## Changes made in this pass

| Change | Where | Evidence |
| --- | --- | --- |
| **Shader IR optimizer.**<br>- Folds constants and removes exact identities (`x*1`, `x+0`, `min(x,x)`, Select on a constant, a component of a vector just built).<br>- Merges duplicates across commutative operand orders and drops dead nodes.<br>- Positive divisors skip the zero guard in GLSL. | `shader_graph_optimize`, `graph/shader/shader_graph.c` | The CPU evaluator (picking, previews) visits 550 nodes instead of 1,321 across authored materials.<br>Emitted GLSL has about 5% fewer statements than the previous emit-time deduplication.<br>A randomized test checks 200 programs evaluate identically. |
| **Parameter fetches bounded by use.**<br>`sg_parameter_count(id)` limits the fill loops in the 3D, native 2D and web paths. | `render/shader_catalog.c`, `arena3d.frag`, `entity_raster.frag`, `shader_fragment.c` | A 3D graph fragment did 38 `texelFetch` calls. It now does 6 plus the material's slots; authored materials use 0 to 7.<br>Editor screenshots of Arena3D and Arena2D are pixel-identical before and after. |
| **Derived expression functions.**<br>- `radians`, `degrees`, `saturate`, `smoothstep`, `remap`, `cross`, `angle` and `rotate`, spelled with existing functions.<br>- Variadic `min` and `max`. | `graph/nodes/math_expression.c` | No new node kinds, so every backend already lowers them.<br>Tests cover the behavior, shader and field dialects. |
| **Word-level ECS query scan.**<br>- Queries without a dense term AND the terms' occupancy words and jump with `ctz`.<br>- System batches keep their slot buffer across ticks. | `ecs/ecs.c`, `runtime/systems/system.c` | A word-boundary test compares the scan with per-entity checks. Not benchmarked. |
| **Legibility.**<br>- Expressions are titled by role: If (decides), Set (assigns) or Expression.<br>- A local's hidden wire draws faintly while either end is hovered or selected, and takes no pointer input. | `editor/studio/studio_graph_layout.c`, `studio_graph_draw.c` | Native Xvfb screenshots. The web build was not available in the audit environment. |

## Where the time goes

Per-row overhead dominates behavior Tick, not body code quality.

**At 20,000 enemies:**
- Validating rows took 43.7% of behavior update and bodies 17.9% ([shared clock](shared-clock.md)).
- After parallel validation, native Tick runs at 1.56–1.92× an independent C loop, using 8 workers against its 1 ([branch reads](branch-reads.md)).

**Entities are validated three times per row:**
1. The parallel `ready()` pass.
2. The row guard, about 13 checks in `update_jit.c`.
3. Every entity and component access in the body, each checking generation, occupancy, layout and stride (`behavior_function_jit_component.c`).

**Body code:**
- Where it matters, SLJIT runs 2.1–2.5× behind generated C on arithmetic and loop kernels ([loops](whole-loops.md), [motion](whole-motion.md)). Clang applies mem2reg, GVN and LICM there; our IR has none of them.
- Portable bytecode, the only web backend, runs 25–38× behind SLJIT.

**Shaders:**
- The per-fragment cost was the 32-slot parameter fill, now bounded.
- One program still contains every material and shape, dispatched by `switch(id)`. Register pressure is therefore the worst case across the whole catalog.
- Editing any asset recompiles every program (`shader_program.c`).

## Being more opinionated

These rules trade expressiveness nobody uses for guarantees the compiler can exploit. Each one needs the oracle (`graph/runtime/flow.c`) changed together with the compiled backends to keep parity.

1. **Pure means once per activation.**
   - *Today:* a pure subgraph read by K exec nodes is lowered and run K times, because the memo resets per exec node (`behavior_function.c`, `aclear(b->outputs)`).
   - *Rule:* component and variable reads observe the activation's start, plus its own writes forwarded to them.
   - *Gain:* one value number per read for the whole activation, hoisting reads to entry, and LICM of pure work out of loops. This relaxes the "fresh memo per EXEC activation" promise in [the optimizer](../graph-optimizer.md).
2. **Tick has a contract.**
   - *Rule:*
     - Tick declares the components it requires, checked at attach time.
     - It writes only Self components and its own variables. Other effects go through commands.
     - Its errors abort and log the row instead of replaying precise partial effects.
   - *Gain:*
     - Validation collapses to one generation check per row, and column bases bind outside the row loop.
     - The parallel proof becomes a language guarantee.
     - Bodies with no other effects become column loops that reuse the System batch design (`graph_jit_sljit.c` batch entry).
   - *Scope:* this is the largest measured win available.
3. **Randomness is keyed, not global.**
   - *Rule:* a counter-based random value keyed by entity, frame and call site.
   - *Gain:* removes the RNG veto on parallel Tick (`behavior_function_parallel.c`), and gives the "Random by ID" stability proposed on 2026-09-23.
4. **Shader values have a rate.**
   - *Rule:* classify every IR node as constant, frame, instance or fragment. Field graphs already do this with their `spatial` bits and `bake_row`.
   - *Gain:*
     - Evaluate instance- and frame-rate subgraphs on the CPU with the existing evaluator, writing the results into spare parameter slots.
     - Static switches come almost free.
     - `render_shader_may_be_transparent` becomes decidable per instance instead of "not a constant below 1".
5. **Handles have owners.**
   - *Rule:* a `NODE_HANDLE` field declares a cleanup policy (`clear` or `delete_source`), backed by a reverse index kept up by command application.
   - *Gain:* the useful part of flecs relationships, `(OnDeleteTarget, Delete)`, without pair ids. Every component id here allocates a full-world occupancy bitmap.
   - *Cleans up:* today `GameParticleEmitter.parent`, `GamePickup.target` and `GameCollider.ignore_entity` are resolved by lookup, and nothing clears them when their target dies.

## Flecs ideas, sized for this engine

**Adopt** (with triggers and sketches):
- **Cached queries.**
  - *Flecs:* stores matched tables and updates them only when tables are created.
  - *Here:*
    - Add a world `structure_revision`, bumped in `apply_add`, `apply_remove`, `apply_create` and `apply_destroy`.
    - Systems rebuild their snapshot only when it changes.
    - The word-level scan above covers bitset terms.
- **Change detection.**
  - *Flecs:* per-column version counters, bumped by writes.
  - *Here:*
    - Add a version per `ecs_execution_column`. It bumps on add and remove, and on the declared writes of each system that ran; the scheduler already knows those.
    - A system may skip when nothing it reads changed.
  - *Good fits:* render-state rebuilds, hierarchy propagation and the nearest-query candidate cache.
- **Automatic sync points.**
  - *Flecs:* merges commands only before a system that reads a component with pending commands.
  - *Here:* structural systems are full barriers (`schedule.c`), with one merge per frame at `engine.c`. Recording `structural_writes` per system lets unrelated systems keep running in parallel.
- **Intra-system parallel ranges.**
  - *Flecs:* slices each table across workers.
  - *Here:* when `compile_effects` proves owner-only writes, split the batch `slots[]` across the task pool. The batch JIT already takes slots and a count.
- **Prefab spawn plans.**
  - *Idea:* flecs' tree spawner caches a prefab's structure.
  - *Today:* spawning here takes two sync points and then writes every field through reflection (`prototype.c`).
  - *Change:* cache a per-prototype plan of component ids and prebuilt blobs, and spawn with one `record_create` plus `record_add(..., initial_value)` per component.
- **Data-oriented hierarchy, when content needs one.**
  - *Flecs 4.1:* a non-fragmenting `Parent` with breadth-first order by depth.
  - *Here:*
    - A dense `GameParent { entity, offset }` component.
    - A native module that keeps depth and a breadth-first slot order, rebuilt when a hierarchy revision changes.
    - One propagation pass in which parents precede children.
  - *Trigger:* sockets, turrets or emitters that follow an owner. Validate it with a second scene.

**Do not adopt:**
- Archetype tables. [The frame-time roadmap](frame-time-roadmap.md) already rejected them, and slot-indexed columns with sorted dense lists already act as the table.
- IsA shared components. They would add indirection to JIT column addressing.
- General observers. Flecs itself recommends systems first.

## Compiler work, ranked

| # | Work | Impact | Effort |
| ---: | --- | --- | --- |
| 1 | **Tick contract (rule 2):**<br>- validate once per span<br>- bind columns outside the row loop<br>- drop the `ready()` pass | High: 44% of update is validation | Medium |
| 2 | **Sparse Tick:** hoist a leading Branch on the body's own state into the row loop, so rows that do nothing skip bind and call. The C reference skips the inactive CombatFeedback half. | Medium–high | Small–medium |
| 3 | **Activation memo plus cross-block value numbering (rule 1)** | Medium–high | Medium |
| 4 | **Shader rate analysis and CPU hoisting (rule 4)** | High for GPU occupancy | Medium |
| 5 | **Shader program sets built from what is drawn, and per-asset revisions:**<br>- specialize the 3D path per material<br>- stop an edit recompiling every program<br>- stop default-only edits recompiling | High for iteration | Medium |
| 6 | **Keyed randomness (rule 3)** | Medium | Small–medium |
| 7 | **SSA whole-function IR with dominators:**<br>- SCCP, GVN, LICM<br>- induction variables and bounds-check elimination<br>- static budget coalescing<br>- more than two integer scratch registers | Medium, content-dependent | Large |
| 8 | **Typed bytecode VM with superinstructions** | High on web | Medium–large |
| 9 | **Field kernels by instruction rather than by cell**, so the column loop vectorizes (`field_program.c`) | Medium | Small |
| 10 | **2D noise specialization:** `sgx_noise` hashes 8 corners for 2D input. Unroll `fbm` for a constant Detail. | Medium | Small |
| 11 | **Per-entity parameter override index:** the lookup is O(entities × overrides) with `strcmp` (`shader_catalog.c`) | Medium once overrides spread | Small |

**Missing shader features, by value for an arena game:**
1. `fwidth`/`dFdx` and an antialiased step. ArenaFloor's fixed 0.006 edge shimmers at distance.
2. Static switches.
3. Per-instance random.
4. Mipmaps or `textureGrad` for Image Texture.
5. `pow`, `exp`, `atan2`, `mod` and `cross` in the shader dialect.
6. A vertex stage with world-position offset.
7. Depth fade.

## Legibility

The remaining findings, ranked:

1. **Enums instead of magic numbers.**
   - *Problem:* `GameEncounter.status` and `GamePickup.state` are plain `int` fields. Expressions therefore read `status != 2` and `Switch on Int` picks on numbers.
   - *Fix:* reflect named enums on those fields and add enum literals to expressions (`status != Encounter.Cleared`).
2. **One contract per name.**
   - *Problem:* Select has three pin orders and meanings across behavior, shader and field graphs. Behavior `Smooth Step` is a one-argument ease, while the expression `smoothstep` is GLSL's three-argument form.
   - *Fix:* rename the behavior nodes ("Select by Index", "Ease In Out") and migrate assets.
3. **Conditions drop their unused Result row.**
   - *Problem:* every If expression shows a Result output that nothing reads.
   - *Related:* an If expression and a Branch node now sit side by side in the same graphs (Refresh Encounter).
4. **Return values without scratch variables.**
   - *Problem:* Formation Position writes a persistent State variable in four branches just to return it.
   - *Fix:* a value-returning Switch or multiple Returns. Call outputs should bind straight to variables, which removes the Call → Set → Set chains in ArenaShipMotion.
5. **Content migrations the new functions enable**, each verified against a `runScenario` baseline:

   | Asset | Replace | With |
   | --- | --- | --- |
   | ArenaPickup | the hand-expanded "Spiral X"/"Spiral Y" pair | `rotate` |
   | Spawn Telegraph | `atan2(center.y - position.y, center.x - position.x)` | `angle(center - position)` |
   | ArenaShipMotion | `move.y*aim.x - move.x*aim.y` | `cross(move, aim)` |
   | BasaltCave | the 153-character `min(min(min(…)))` nest | variadic `min` |
   | several | `* (pi / 180)` | `radians` |

6. **Compact min, max, abs and clamp** as operator pills, as Unreal does (`editor/graph/presentation.c`).
7. **Shared math across dialects.**
   - *Problem:* Basalt (shader) and BasaltCave (field) duplicate their wobble and joint formulas.
   - *Fix:* a library limited to the common function subset could compile for both.

## Building on Linux

These are environment notes from this audit; none of them is a repository change.

- **Compiler:** the project expects Clang (`-Wmultichar` fails on GCC).
- **Linker:** with GNU ld, `chibi` fails to link. `editor` calls `ui_check_*` from `chibi_render` without linking it, and static archive order matters to GNU ld. Linking with `-fuse-ld=lld` avoids it; declaring the dependency would fix it.
- **Downloads:** where GitHub archive downloads are blocked but git is not, clone s2n-tls and tree-sitter at their tags and pass `FETCHCONTENT_SOURCE_DIR_<NAME>`.
