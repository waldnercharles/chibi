# Field reuse and SIMD

Continue from `5a74676f`, the frame-time batch merged to remote main on September
30, 2026. Keep the existing multiplayer `PLAN.md` intact.

The next batch removes repeated parameter parsing, binding and scratch allocation
from streamed field slices, and measures SIMD for contiguous row evaluation.
Native and WASM must retain identical field values and scenario replay hashes.

## Ownership and validation

- A compiled field remains immutable. A caller owns reusable evaluation state,
  including parameter values and scratch; concurrent bakes use separate state.
- A material stream owns its evaluation state. Field recompilation invalidates
  the retained binding before another slice, including a pending tile.
- SIMD preserves separate multiply/add rounding, bounds, scalar tails and
  invalid-value reporting. Keep an experiment only when matched runs support it.
- Compare the merged baseline binaries with optimized candidates in alternating
  Release runs. Record field hashes, allocation behavior and full scenario
  responses, with native, serial WASM and threaded WASM correctness checks.
- Run the native sanitizer suite, native build, serial and threaded web builds,
  formatting and assets checks after the final source change.

## Physics assessment

Read official Box2D v3 documentation and the supplied Neon-Swarm grid Verlet
implementation. Assess body reuse, sleeping, event collection and a separate
solver against current phase measurements. Firefight's Box2D solve is small;
keep that evidence separate from a contact-heavy Arena workload. Record the
recommendation with primary-source links in `box2d-performance-and-simple-solver.md`.

If an experiment regresses or changes simulation, remove it and retain the
measurement and reason. If a check is blocked, report the command and boundary
without claiming validation.

## Completion

Retained bindings and worker scratch are implemented. Scoped compiler SIMD is
enabled for WASM fields; the handwritten SIMD experiment was removed because it
added less than 1% over compiler vectorization. Native sanitizer tests, native
Release builds, serial/threaded web builds, browser smokes, formatting, and
complete native/WASM replays pass. Matched measurements and their limits are in
[the implementation report](field-optimization-implementation.md).

The Box2D assessment is complete. It recommends measuring pair collection and
an optional uniform-circle crowd solver, with shared query/contact broadphase.
Physics behavior and body lifetime policies are unchanged in this batch.
