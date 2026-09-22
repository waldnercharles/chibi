# Unified directional transport and Grant Kot's demonstration

September 21, 2026. Follow-up to `gi-experiments.md`.

## Source correction

The earlier research read the written post and video description, **not the
closed captions**. The auto-generated English transcript of
[Refracting Radiance Cascades](https://www.youtube.com/watch?v=N6hcS7Ub2pU)
was inspected in this follow-up. Browser transcript export initially failed
while an advertisement was loading; it succeeded after the main video loaded.
No inference should be presented as source-code inspection.

The spoken explanation adds these details:

- [0:01–0:17](https://www.youtube.com/watch?v=N6hcS7Ub2pU&t=1s): the experiment
  combines radiance cascades with his multigrid light solver; optimization was
  still in progress.
- [0:21–0:28](https://www.youtube.com/watch?v=N6hcS7Ub2pU&t=21s): particles were
  sampling the wrong light field, affecting the water's appearance.
- [0:36–1:05](https://www.youtube.com/watch?v=N6hcS7Ub2pU&t=36s): the older method
  repurposed his pressure-solver approach to propagate light. The captions
  garble an equation name; they do not establish the actual discretization.
- [1:07–1:22](https://www.youtube.com/watch?v=N6hcS7Ub2pU&t=67s): RC tiling
  supplies sharper directional lighting than his earlier experiment.
- [1:26–1:59](https://www.youtube.com/watch?v=N6hcS7Ub2pU&t=86s): refracted rays
  select a tile for their changed direction instead of continuing to sample
  the original tile. He also reports reflection and total internal reflection.

His [written post](https://ko-fi.com/post/Refracting-Radiance-Cascades-J3J01V1XZ9)
and video description identify the same combination. Neither the captions nor
those descriptions specify a complete V-cycle, residual operator, restriction,
prolongation, angular interpolation, interface weights, iteration count or
performance budget. The demonstration does not establish that he uses our HRC
variant or its particular trace/extend/merge implementation.

## Consequences for this renderer

A unified directional solver is a valid candidate. Directional light states can
couple to other directions at a boundary; scattering can redistribute their
energy in the same field. Repeated solves can therefore include diffuse-to-glass
paths that the current primary-path tracer plus diffuse HRC approximation omits.
This is our engineering interpretation of the described mechanism, not a recovered
implementation of Grant's solver.

Our HRC interval represents emitted radiance and transmittance along a fixed
straight direction. Refraction changes both the continuation position and its
direction. Replacing `direction` in one raymarch loop does not update the rest of
the hierarchy's interval-reuse assumptions. The final RGB gather also discards
angular information needed for later specular interactions. Retain directional
state through transport and gather only for shading/scattering.

A pressure-style scalar diffusion solve alone cannot retain sharp directional
beams. Multigrid accelerates a chosen transport operator; it does not supply the
optical boundary physics. An independently documented
[GPU multigrid radiative-transfer solver](https://pmc.ncbi.nlm.nih.gov/articles/PMC3497889/)
uses both angular and spatial variables, residual restriction, coarse correction
and post-relaxation. It also discusses coupling between angles at refractive
boundaries and the convergence cost of ignoring transport ordering. Its medical
photon-transport timings are not realtime game-renderer performance evidence,
and its exact algorithm must not be attributed to Grant.

## Next experiment

Keep the CPU reference and native forward tracer as correctness/performance
controls. Evaluate a unified directional transport candidate with:

1. One local transport operator for extinction, emission, diffuse scattering,
   reflection and Snell/Fresnel transmission. It must be linear for a fixed scene
   so that residuals and correction solves have a consistent meaning.
2. Material-aware spatial interpolation and conservative angular transfer.
   A nearest-direction tile is a useful baseline; blending must be checked for
   broadening, energy errors and leakage around interfaces.
3. Actual residual correction across grids, with measured convergence per unit
   time. Compare against the same discretization run to convergence, separately
   from comparison against the independent continuous-path reference.
4. Same-frame update schedules and dynamic tests: light removal, moving glass,
   moving fog, total internal reflection and repeated bounces.
5. Optional HRC or SDF acceleration for straight spans. MPM supplies density,
   material/IOR and surface geometry; it does not need a separate light solver.

The existing `coarse` candidate only initializes coarse-to-fine and relaxes. It
is **not** a multigrid residual-correction implementation, and its failure does
not reject this unified approach. No unified replacement has passed the quality
and frame-time requirements yet. The active goal remains open.
