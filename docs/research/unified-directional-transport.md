# Unified directional transport and Grant Kot's demonstration

September 21, 2026. Follow-up to `gi-experiments.md`.

Status: this remains an experimental candidate. Current implementation work
focuses on the [integrated HRC scene renderer](scene-renderer-optimization.md).
The experiments below have not earned replacing it.

Later follow-up: [conservative angular transfer and surface-band transport](directional-flux-transport.md).
It identifies a discrete flux error in the initial directional operator below.

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
not reject this unified approach.

## Residual-correction prototype

`tools/gi_lab/multigrid.c` now implements a spatial V-cycle over the same angular
transport operator as `iterate`. The fixed-scene equation is `L = E + K L`.
After relaxation, it computes `r = E + K L - L`, restricts the residual to a
coarser grid, solves a signed correction with homogeneous emitter boundaries,
prolongs and adds the correction, then relaxes again. Coarse residuals scale by
the ratio of transport step lengths. Geometry is rediscretized on each grid;
this is not a Galerkin coarse operator. Angular resolution stays constant.
Only the physical fine-grid correction is limited to nonnegative radiance;
error fields remain signed. All fields use 32-bit floats for this experiment.

Every frame starts from zero. This measures same-frame propagation and avoids
hidden dependence on settled history. `--cycles 0 --iterations N` runs just
fine-grid relaxation in the same precision. The 1024/2048-sweep controls agree
within 0.00004% on the tested fixtures, establishing their settled state.

Reproduction:

```sh
python3 tools/benchmark_optics.py --multigrid --repeats 2 \
  --output build-release/multigrid-evidence/benchmark
```

The initial lens results at 120x68, 32 directions and 1440p reconstruction are:

| V-cycles | Median ms | Worst repeat p95 ms | Error vs settled grid | Error vs ray reference |
| --- | ---: | ---: | ---: | ---: |
| 1 | 3.26 | 3.44 | 32.87% | 41.19% |
| 2 | 6.25 | 6.58 | 11.79% | 25.35% |
| 4 | 12.19 | 12.58 | 4.30% | 22.28% |
| 8 | 23.80 | 24.20 | 1.54% | 21.46% |

These are hidden GPU-completion timings for transport and reconstruction only,
with four pre/post sweeps, 16 warmup frames and 32 measured frames per repeat.
Cold convergence-control timings are not steady-frame benchmarks. At 240x135,
one cycle took 7.80 ms and four took 29.45 ms. No complete-game 4K60 claim follows.

The settled lens field still has 20.95% error against the continuous-path
reference and 47.53% focal-region error. Slab and prism settled errors are about
40.8% and 41.2%; the mirror is about 3.05%. More cycles cannot repair the fine
operator's spatial/angular discretization bias. Slab convergence is also not
monotone, exposing the limitations of this coarse correction. All primary light
disappears on the first light-off frame. Moving-glass results are captured too.

This is a useful convergence control, not a viable renderer replacement yet.
The next work must improve the transport discretization and boundary handling,
and avoid recomputing geometry/filter decisions on every relaxation sweep.
HRC/SDF acceleration for straight spans remains relevant. No unified replacement
has passed the quality and frame-time requirements. The active goal remains open.

### Rejected clearance shortcut

A follow-up computed exact analytic distance to the nearest optical boundary
inside each directional texture sample. When a full interpolation footprint was
clear, it used hardware bilinear sampling instead of four material checks.
The lens four-cycle test took 13.40 ms median versus the earlier 12.19 ms, and
its field changed by 0.245%. This failed the 0.01% equivalence check and offered
no measured speedup. The change was removed. Its patch, provenance, captures and
timing report remain under `multigrid-evidence`. A precomputed transport stencil
or clearance field is a separate experiment; these results do not measure it.

The shared-operator refactor passed `make test-optics-gpu`, including all four
forward-reference fixtures, light-off checks and the grid-aligned mirror leak
check. The residual-correction benchmark passed its settled-control check for
all four fixtures and immediate light removal. These checks establish the
experiment's behavior, not that its image quality or speed is acceptable.
