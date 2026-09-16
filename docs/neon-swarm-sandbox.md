# Neon-Swarm visual sandbox

`projects/arena2d/content/scenes/NeonSwarmSandbox.json` is the playable sandbox.
Run it from this worktree:

```sh
make build-release
./build-release/app --project-root projects/arena2d \
  --scene content/scenes/NeonSwarmSandbox.json --play
```

Move with WASD or the left stick, boost with Space, and aim/fire with the mouse
or right stick. Inactive manual aim selects the nearest swarmer automatically.
The scene also runs in the editor's Scene preview. Gameplay captures exclude
editor controls and diagnostics.

## Authored content

Chibi MCP authored the scene, prototypes, and behavior graphs. Movement retains
analog magnitude after a 0.1 deadzone, uses speed 120, and boosts along the current
facing at 240 even after releasing the movement stick. Bounds are ±640 by ±360.
The camera follows with the source's 960 by 540 view and margin. Projectiles fire
at 8 Hz, travel at 400, and expire after 1.5 seconds. Swarmers accelerate toward
the player at 120, capped at 75. The deterministic director starts after four
seconds and then spawns every 0.12 seconds around the arena.

The player uses the four source chevron points and a purple emission tint.
Projectiles are cyan capsules. Swarmers have dark bodies and red emissive borders.
The walls use source absorption 0.01. Player movement, boost, projectile trails,
hits, deaths, and spawns each have separate authored emitters. Death emission is
reduced for the white/blue palette so repeated kills do not wash out the arena.
The sandbox intentionally omits progression, shops, HUD, audio, and player death.

Shared native code supplies reusable components and graph functions. The optional
emitter system owns deterministic particles and attachment lifetimes. It supports
random ranges, age curves, analytic drag, inherited velocity, velocity alignment,
capsule stretch, material absorption/emission, and metaball groups. The source
movement and trail presets inherit zero velocity so particles remain behind.
Runtime and preview use the same simulation and material renderer.

## HRC and performance

The renderer uses the source-style four-quadrant HRC pipeline with packed
half-precision storage, trace/extend/merge/gather passes, filtering, and feedback.
Material emission enters HRC directly. The 2D scene renderer has no point-light
collection or light-field allocation/draw/composite path. The separate light-field
module retains its independent tests. Bloom and direct glow are disabled here.
The three-layer procedural star texture composites behind scene alpha in linear
color space, with ambient zero.

`GameHRC2D` owns the requested source defaults and trace/feedback settings. Scale
is fractional; only texture allocations round to pixels. Adaptive resolution
samples whole-frame time and preserves the authored value independently of its
current allocation. Material edges and the final image retain viewport resolution.
The target is 60 Hz with tolerance 0.1. Future particle physics is not part of
this benchmark and will need its own CPU/GPU budget.

The sandbox authors HRC scale 0.5 for performance. The reusable component's
source default remains 0.976883, and all other requested source settings remain
in the sandbox. Only transported lighting uses the smaller grid; entities,
particles, stars, and the final image remain at viewport resolution.

On an Apple M4 Pro with 48 GB RAM at 960 by 540, three consecutive 344-sample
runs measured median frame work at 4.92–5.08 ms, p95 at 5.90–6.10 ms, and p99 at
6.35–6.53 ms. Each ended with 187 live entities and 373 particles. Adaptive HRC
ended between scale 0.425 and the authored ceiling 0.5. These are GPU-complete work times with 60 Hz pacing. They exclude renderer
warm-up and the pacing wait, and leave about 10.5 ms of the 60 Hz work budget at
p95. Future particle physics is not included.

The initial pipeline measured about 41 ms even in a sparse scene. The first
source-style implementation had fast medians but intermittent long stalls;
near-full-resolution runs did not reliably meet the p95 budget. The final path
uses geometric entity-buffer growth, retains cascade capacity and feedback
images across adaptive grid changes, avoids redundant zero uploads, and bounds
full-resolution gather refinement. A 16-pixel viewport margin avoids processing
an unnecessary outer cascade. The benchmark now explicitly warms the renderer,
waits for GPU completion, and paces at the intended 60 Hz.

The three acceptance trials are in
`/private/tmp/neon-acceptance-trial-{1,2,3}/performance.json`.
Final captures and the single-command rerun are in
`/private/tmp/neon-swarm-validation`.

The implementation choice followed a review of the original
[HRC description](https://adrianmargel.ca/projects/holographicRadianceCascades/),
[the RC paper sources](https://github.com/Raikiri/RadianceCascadesPaper), and
[the Amitabha implementation](https://github.com/entropylost/amitabha).
Local measurements and the source game's existing implementation justified
adapting the existing HRC code rather than adding another renderer dependency.

## Reproducing validation

```sh
make build
make test
build/test_suite --render-checks
make build-release
python3 tools/check_neon_swarm.py --captures --performance
git diff --check
```

Run the performance command after builds and test processes finish. It measures
360 native presentation frames after simulating 1,200 fixed steps. It first warms
the renderer for 240 presentation frames with simulation frozen, discards the
first 16 measured presentation samples, and fails when p95 exceeds 16.667 ms.
Timing includes simulation, GPU completion, and presentation submission, with
immediate present mode. The remaining 60 Hz pacing wait is excluded. This is a
steady-state frame-work measurement, not a cold-start or isolated GPU benchmark.

The acceptance script checks movement, deadzone, analog magnitude, facing, boost
hold/release, bounds, manual and automatic aim, physical mouse binding, firing
cadence, projectile motion/lifetime, collision damage and death, deterministic
world/particle state, and HRC-only scene composition. Native tests cover particle
ranges, curves, drag, attachments, activation, grouping, alignment and seeds;
material geometry/channels; HRC transport, absorption, bounce, resize, and adaptive
scale; and prototype registry ownership after the loader's stack frame ends.

MCP `recordScenario` and `replayScenario` matched all 1,608 frames at 60 Hz without
divergence. `validateAsset` now links library and event definitions before
compilation, as execution does. MCP checks its resolved project root before
native operations. For authoring this worktree, set `CHIBI_ROOT` to its
`projects/arena2d`, and `CHIBI_CTL` / `CHIBI_RENDER_CTL` to its own binaries.
The desktop connector may still address a different checkout.

The alternate Arena2D scene rendered successfully without Neon-Swarm behavior.
The MCP design smoke also exercises an independent visibility/fog scene.
Keyboard movement, facing boost, mouse fire, standalone output, and editor
preview were exercised in the native UI. A physical controller was not attached;
analog and held-button semantics were verified through deterministic inputs.

## Visual evidence

`/private/tmp/neon-swarm-validation` contains gameplay PNGs at 0, 2, 4, 6, 8, 12,
18 and 24 seconds, pose/population counts, a source comparison sheet, image
metrics, the MCP recording/replay results, and the alternate scene capture.
The input timeline is authored for repeatability; the recording contains no
recoverable original input stream, so player trajectories and kill timing differ.
Image differences quantify that mismatch and are not a pixel-parity claim.

To regenerate the comparison, extract the source timestamps into a directory
as `00.png`, `02.png`, etc., then use Python with Pillow and NumPy:

```sh
python3 tools/compare_neon_swarm.py --source /path/to/source/frames \
  --captures /private/tmp/neon-swarm-validation
```

The comparison crops the recording's window chrome, resizes to 960 by 540, and
excludes the debug strip from MAE/RMSE. Visual review checks the small chevron,
sparse cool stars, warm/cyan exhaust, dotted cyan trails, hollow red swarmers,
and arena-wide transported red light. The late swarm's arrangement differs
because its director and input timeline are deterministic approximations.
