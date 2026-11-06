# Simplified crowd physics: Neon-Swarm comparison

Neon-Swarm's `/path/to/Neon-Swarm/src/physics.c` implements
circle collision detection and soft separation. It stores positions, velocities,
radii and other body properties in separate arrays, rebuilds a spatial grid,
and checks neighboring cells. Penetration applies repulsive spring/damping
forces instead of a rigid-body constraint solver. Contact recording is optional
per body, although its enemy definitions currently enable it.

## Measurement

The checked-in comparison driver runs the actual Neon-Swarm physics source
against Box2D 3.1.1 from this project's Cute Framework library. Both execute
serially, with 20,000 circles, radius 10, unit mass, a 22-unit grid and movement
toward the origin at 55 units/second. Thirty warmup steps precede 240 measured
steps. Both record contacts; Box2D uses four substeps. Three alternating-order
pairs, compiled with `-O3` and no sanitizers:

| Step | Median ms | p95 ms |
| --- | ---: | ---: |
| Neon-Swarm soft circles | 1.118 | 2.196 |
| Box2D circles | 9.971 | 14.239 |

This is approximately 8.9x less step time for the simpler model. It is not an
Arena frame-rate comparison. Body creation and common velocity input are outside
the timer. Box2D here excludes c-blueprints ECS synchronization, current-pose
gameplay contact collection and continuous sensors. Production Arena already
uses four physics workers, so the serial result cannot be substituted for its
9.4 ms physics phase.

The resulting mean distance from the origin differs, 947.78 versus 967.61 units.
That is expected for a softer response. All final positions are checked finite;
this experiment does not establish combat, wall or projectile correctness.
Raw outputs, source/library/executable hashes and methodology are in
[neon-physics-comparison-results.json](neon-physics-comparison-results.json).

## What to take from it

An explicit crowd mode is worth implementing for scenes that accept circle
separation and some overlap. Box2D should remain the rigid-body option. Selecting
the cheaper model automatically from current geometry alone would be unsafe:
identical circles can require very different collision behavior.

A reusable implementation should preserve the existing reflected components,
compiled behaviors and contact-delivery API. Authoring selects the motion model;
the compiler and runtime can then optimize within that declared contract.
Scene and prototype preview must use the same selection. Incompatible shapes or
continuous-motion requirements need an explicit diagnostic or supported hybrid
path, never silent loss of collision detection.

The source is useful inspiration but not a drop-in backend:

- It handles circles, with no wall polygons or swept sensor collision in the
  world step. Fast projectiles need relative-motion casts or a Box2D path.
- Grid memory scales with the entire occupied rectangle, including empty space.
  A sparse cell index or bounded grid with oversized-body handling would avoid
  distant entities or large radius variation creating another performance trap.
- Its contact comparison uses slot indices without generations. Engine event
  identity must retain generations when a body slot is reused.
- Its threaded path allocates per-task force arrays and reduces them across all
  active bodies. The existing engine task pool should be reused, and the cost
  of that reduction measured before adopting the same partitioning.
- Resting contact, zero-delta teleports, filtering, first-hit selection and
  cancellation/destruction must retain documented engine semantics even when
  the separation response intentionally differs.

This change delivers rendering/contact optimizations and a measured physics
comparison. Production still uses Box2D; a simplified engine backend has not
been integrated or claimed to pass Arena acceptance.

## Reproduce

```sh
python3 tools/bench_neon_physics.py \
  --neon /path/to/Neon-Swarm \
  --build build-performance --count 20000 --frames 240 --trials 3 \
  --output docs/research/neon-physics-comparison-results.json
```

The script reads Neon-Swarm without modifying it. Its unpopulated thread-pool
dependency is replaced only for this serial experiment by aborting stubs;
passing a pool would fail immediately. The production engine has no dependency
on Neon-Swarm or these stubs.
