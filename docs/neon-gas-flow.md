# Persistent gas flow

NeonSwarm gas now follows a persistent sparse 2D velocity grid. A moving body
can redirect nearby gas before contact, and its wake keeps moving afterward.
Pressure projection limits compression; vorticity confinement helps preserve
rolling motion lost through interpolation. This is a CPU grid-and-tracer solver,
not MPM. Liquid, powder and elastic matter use MLS-MPM; see
[the material engine](material-engine.md).

## Runtime and authored settings

`src/runtime/material_flow.c` owns the field independently of combat, rendering,
and Box2D. `material_fluid` supplies particles, active indices and collider data.
`MaterialFluidWorld` enables it with three fields:

| Field | NeonSwarm | Meaning |
| --- | --- | --- |
| `gas_cell_size` | 20 | World units per cell; zero disables the field. |
| `gas_vorticity` | 2 | Strength of curl reinforcement. |
| `gas_drag` | 0.1 | Exponential velocity damping per second. |

Each step builds cells around active gas particles with a two-cell halo. Face
velocities live on a staggered grid. Particle velocities seed the field and feed
momentum into it; the previous field is advected with semi-Lagrangian sampling.
Collider velocities constrain solid faces. Curl reinforcement precedes 32
red/black Gauss-Seidel pressure iterations, then particles sample the result.
The ordinary particle collision step still resolves geometry and body impulses.

Gas no longer runs gas-gas density relaxation while the field is enabled.
Liquid cohesion and gas-liquid contacts remain. Engine parcels inject momentum
but keep their authored ballistic velocity, drag and visual lifetime curves.
Gas render kernels have at most a 2.25:1 aspect ratio instead of the earlier 9:1
limit, so shape change comes more from motion of the cloud than stretched splats.

## Contact particles

`MaterialFluidWorld` also controls transient contact particles. Each fluid kind
has a minimum normal closing speed, response strength, maximum particle count
per contact patch, and lifetime. Sandbox uses:

| Response | Speed | Strength | Count | Lifetime |
| --- | --- | --- | --- | --- |
| `gas_wake_*` | 12 | 0.7 | 3 | 0.28 s |
| `liquid_splash_*` | 45 | 1 | 5 | 0.45 s |

Zero count disables a response. Contacts aggregate by body, body-local patch and
fluid kind, with at most 128 events and 256 secondary particles per tick.
Relative surface speed and impulse determine response strength; participating
fluid supplies color and emission. Fast translating colliders use swept contact
checks. These short-lived render particles do not increase the simulated fluid
population. They supplement the existing gas field and liquid solver.

## Deliberate approximations

- The field covers active gas neighborhoods, with open outer boundaries. It does
  not simulate all empty air. Sleeping particles retain state; field cells that
  leave the active region are discarded and reconstructed from particles on return.
- The pressure solve has a fixed iteration count. It reduces divergence without
  guaranteeing an incompressible solution or exact conservation. Momentum blending,
  interpolation and damping are game tuning choices.
- Collider masks expand by half a cell to cover thin geometry. Actual particle
  collisions still use the original geometry. Collider surface velocity includes
  translation and angular motion.
- Grid pressure does not apply an integrated force back to Box2D. Existing
  particle collision impulses provide the current return force. This is not a
  fully coupled pressure/body solve.
- There is no unconditional upward buoyancy in zero gravity. Bodies, exhaust,
  combustion and existing particle motion supply disturbances.
- The field changes motion and therefore reactions, active populations and
  simulation hashes. Performance comparisons must account for that changed work.

## Validation

The material suite checks still gas, collider-driven motion outside contact,
persistent motion after removing the collider, finite bounded velocities,
sleep/wake behavior, ballistic exhaust and serial/parallel determinism including
field state. Existing liquid, combustion and body-response checks also pass.

A temporary scene authored through Chibi MCP uses four entities: a camera, a
material world with HRC, 573 gas particles and a moving kinematic probe. It has
no combat components or behaviors. Two 300-frame snapshots match, and a native
render verifies continuous gas coverage. This fixture does not add another
playable scene to the project.

NeonSwarm acceptance covers movement, aiming, projectile origins, destruction,
finite fuel, debris and deterministic replay. GPU tests cover gas coverage,
liquid lighting, wall shadows and terrain seams at both one and three directly
traced HRC levels. Temporary fixtures, captures and reports are under
`/tmp/neon-flow-implementation`.

## Deferred experiments

MPM remains a candidate for liquids and deformable materials after a separate
quality/cost comparison. Sparse HRC and full-resolution luminance with coarse
color remain experiments pending actual GPU profiling. Neither is implemented
by this change. See [profiling tools](tracy.md#gpu-pass-profiling) and the
[research references](material-simulation-and-hrc-research.md).
