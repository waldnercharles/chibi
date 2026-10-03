# SDF terrain direction

The reference is Mike Turitzin's [dynamic SDF engine video](https://www.youtube.com/watch?v=il-TXbn5iMA). Its description, chapters and auto-generated transcript explain distance caching, interpolation, sparse bricks, geometry clipmaps and incremental updates. The following choices are our application of those ideas to a 2D game. See [the performance follow-up](neon-material-performance.md) for measured priorities.

| Technique | Decision for this engine |
| --- | --- |
| Interpolate signed samples, rather than occupied pixels | Implemented: retain cave field values, reconstruct subcell contours, then evaluate polygon SDFs with antialiasing at the material raster's resolution. |
| SDF subtraction for destruction | Implemented: circular cuts update nearby signed samples after the material's damage threshold is reached. Cells still own connectivity, resistance and Box2D collision. |
| Cache work between edits | Implemented: rebuild the mask's contour only when it changes. Merge full interior tiles into rectangles. Cull outside the padded HRC view before preparing terrain draw records. |
| Sparse brick atlas on the GPU | Potential next optimization if terrain submission still dominates at very wide views. It could replace many contour primitives with a field lookup. It needs revision tracking, border samples and measured upload costs. |
| Levels of detail (clipmaps, mipmaps) | Implemented as mipmapped masks: a distant mask draws a coarser level of its own cells, so pieces follow screen pixels. See [Distant masks](#distant-masks-levels-of-detail). Nested clipmap caches around the viewer remain unneeded: streaming already bounds the resident tiles. |
| Temporary/non-destructive CSG tools | Possible later gameplay feature. It needs edit ownership and collision updates; it is not part of this visual pass. |

[Chris Green's Valve paper](https://steamcdn-a.akamaihd.net/apps/valve/2007/SIGGRAPH2007_AlphaTestedMagnification.pdf) explains why interpolating distance samples preserves edges better than filtering binary coverage. Its screen-space antialiasing approach is relevant to keeping our boundaries crisp. It also describes the limitation of a single distance channel at sharp corners; increasing display resolution cannot recover missing topology.

[Söderlund, Evans and Akenine-Möller's paper](https://jcgt.org/published/0011/03/06/paper-lowres.pdf) compares SDF-grid traversal and intersection methods, including sparse brick storage and continuous normals. Their fastest reported path uses RTX hardware for 3D ray traversal. Our top-down 2D surface pass does not need that traversal. Sparse storage and coherent boundary samples transfer more directly; replacing HRC or requiring hardware ray tracing is unnecessary here.

[Losasso and Hoppe's geometry clipmaps](https://hhoppe.com/geomclipmap.pdf) keep nested grids around the viewer and update them incrementally as the view moves. That is relevant to future large-world streaming, but their terrain is a height field. Our caves need a 2D signed field with holes, so we would borrow the cache/update strategy rather than their terrain representation.

## Current approximations

The procedural cave field preserves its zero crossing but is not a globally exact Euclidean distance field. It is used for contour extraction, not safe sphere-tracing steps. Circular subtraction is restricted to damaged regions. Physics and fragment connectivity retain their two-unit terrain cells; tiny holes below that scale and perfectly matching smooth collision are not provided. Chunk boundary samples currently clamp at the edge, so a small contour kink can remain at a storage boundary. Field halos and synchronized boundary edits would address that if visible in motion.

Liquids use an isodensity boundary with an analytic gradient to limit antialiasing to roughly a pixel. Exhaust uses smooth unions of circles; burning fuel uses smaller separate hot cores. Cold gas uses a translucent density field stretched along flow. These presentations share particle physics and HRC material textures.

## Distant masks: levels of detail

A mask draws one piece per silhouette cell plus its merged interior rectangles. Pieces are the material raster's unit of work: the CPU builds, sorts, bins and uploads each one, and each pixel loops over the pieces binned to its 16-pixel tile, evaluating the material graph for every piece its footprint touches. Zoomed out, a pixel covers many of Sandbox's 2-unit cells, so the piece count followed the visible world instead of the screen.

Profile before the change: release `chibi_ctl` `preview_scene` of Sandbox, frozen, 1600×1000, with `pass_timing`. Each zoom is a scene copy whose camera spans 1600/zoom by 1000/zoom units (zoom = pixels per world unit, as the Scene view's readout). The CPU columns come from a temporary timer around the renderer's stages, not kept in the repository. Pieces, tile references and upload bytes do not depend on machine load; the milliseconds were measured while the machine ran other agents' builds (load averages 50-200) and are provisional.

| Zoom | Pieces | Tile references | Upload per frame | Material raster GPU | Collect + raster CPU | HRC passes GPU |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 100% | 8,805 | 23,785 | 12.1 MB | 8.1 ms | 2.4 ms | 10.8 ms |
| 40% | 43,482 | 82,454 | 59.5 MB | 7.9 ms | 11.6 ms | 7.2 ms |
| 20% | 147,628 | 246,236 | 201.8 MB | 27.5 ms | 49.1 ms | 9.8 ms |
| 10% | 530,394 | 817,674 | 724.6 MB | 90.3 ms | 204.6 ms | 15.9 ms |

The material raster and its CPU preparation grew with the pieces; fluid stayed near 1-3 ms. The HRC passes did not grow: their grid follows the raster's pixels, not the world. In the Studio web build at the default 40% (a 1210×520 Scene view), Sandbox drew 25,723 pieces, the scene renderer took 24-35 ms of CPU a frame depending on load, and the editor ran at 27-37 fps once the Generated World had finished streaming. Uploads dominate there: WebGL emulates storage buffers as textures, and in an instrumented run with 12,683 pieces the upload took 12.6 of the raster's 15 ms.

### Design

- Baked chunk impostors, as Noita's chunk textures or Unreal's HLOD proxies, would draw a quad per tile, but each tile would need all six material planes per texel, animated materials would freeze, and every cut would rebake a tile. A [static material tile cache](neon-hrc-optimization.md#static-material-tile-cache-rejected) was already measured and rejected.
- Mipmapped masks keep the representation: a coarser level is another mask of SDF pieces, so materials, lighting inputs, destruction and coverage antialiasing work unchanged. This is the chosen design.
- `material_mask_reduce` halves a mask's resolution: each cell merges a 2×2 block and is solid where the block's mean distance, the field at its center, is inside, and marching squares rebuilds its silhouette. A mask has one material, so every level keeps it.
- `render/material_lod` keeps the levels of the masks a renderer drew last frame, builds them lazily from the next finer level, rebuilds them when a cut changes the mask's revision, and releases those the view no longer draws. Each level holds a quarter of the cells of the one below, so all of them add at most a third to the visible masks.
- A mask draws the finest level whose cells span at least a pixel (`RENDER_MATERIAL_LOD_PIXELS`), as a mipmap is picked so its texels cover pixels. Cells of a pixel or more draw exactly as authored. A level is held until its cells leave their range by a quarter octave, per cell size, so all masks of one cell size switch together and a view that hovers at a boundary does not alternate. A threshold of two pixels would halve the pieces again, but Swarmers' 1-unit cells span 1.3 pixels at Sandbox's game zoom in a 720p window and would draw coarser in the game.
- Masks whose dimensions stop halving evenly keep the coarsest even level, so a level never extends past its mask. A mask with a border keeps it as thick as authored: it coarsens only while whole cells still make up the border.
- Pieces of one mask carry the same 1 KB of material and shape parameters. The raster now uploads each run of equal parameters once, and a piece's `time[1]` names its block, so a piece uploads 336 bytes instead of 1,360. Output is pixel-identical.

### Results

Same scenes after the change, interleaved with the old binary for five rounds; medians, with the range of the per-round ratios. `preview_scene` now reports `material_pieces`.

| Zoom | Pieces | Upload per frame | Raster GPU ratio | Collect ratio | Raster CPU ratio | Frame ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 100% | 8,805 | 3.1 MB | 1.07 (0.54-1.31) | 1.11 (1.00-1.27) | 0.90 (0.76-1.13) | 1.13 (0.97-2.20) |
| 40% | 21,422 | 7.4 MB | 0.65 (0.65-0.73) | 0.53 (0.51-0.57) | 0.38 (0.33-0.45) | 0.79 (0.65-1.00) |
| 20% | 36,061 | 12.4 MB | 0.32 (0.30-0.38) | 0.27 (0.26-0.30) | 0.19 (0.18-0.22) | 0.37 (0.36-0.41) |
| 10% | 63,484 | 21.8 MB | 0.14 (0.12-0.21) | 0.12 (0.11-0.15) | 0.08 (0.08-0.11) | 0.16 (0.11-0.21) |

At 100% nothing changes but the upload; its frame ratio's spread comes from adaptive HRC choosing different resolutions under load.

Frozen frames compared with the old binary:

| Scene | Pixels that differ | More than 2/255 | Largest |
| --- | ---: | ---: | ---: |
| Sandbox at its camera and at 100%, MaterialStudy, MaterialShowcaseDestruction, BloomCircuit (per-entity shader parameters), Arena3D | 0 | 0 | 0 |
| Sandbox at 40% | 0.57% | 62 | 19/255 |
| Sandbox at 20% | 0.56% | 77 | 54/255 |
| Sandbox at 10% | 0.32% | 52 | 63/255 |
| MaterialStudy at 20% and 10%, after its Cut sample is cut | 0.018%, 0.012% | 172, 151 | 82/255 |

The differences against full detail lie on silhouettes and bound what a level switch changes. MaterialStudy's cut appears at the coarser levels, as the unit tests check for cut masks.

Web build in headless Chrome, Studio with a 1210×520 Scene view, Sandbox, measured once the Generated World stopped streaming (load averages 25-80; provisional).

Interleaved runs of builds with and without the change; fps per run, and the scene renderer's CPU time from the temporary timer.

| View | Pieces before → after | Scene renderer CPU before → after | fps before | fps after |
| --- | ---: | ---: | --- | --- |
| Play, game camera | 2,945 → 2,945 | 2.1 → 2.1 ms | 60, 60, 60 | 60, 60, 60 |
| Edit, 104% | 4,212 → 4,212 | 2.5 → 2.7 ms | 60, 60 | 48, 60 |
| Edit, 40% (default) | 25,723 → 12,683 | 24.3 → 8.4 ms | 36.3, 37.1, 35.6 | 60, 60, 60 |
| Edit, 20% | 70,081 → 16,945 | 83 → 8.9 ms | 13.9, 16.6 | 59.3, 59.8 |
| Edit, 10%, still streaming after 2 minutes | 147,394 → 18,239 | 108 → 9.5 ms | 7.7, 9.3 | 25.0, 25.3 |

At 10% the Scene view was still generating terrain; the rest of its frame is generation and other editor work. The one 48 fps run at 104% had the same scene CPU as the others.

### Re-measuring

On an idle machine, from a copy of `projects/arena2d` with zoom scenes (`SandboxZ40` copies Sandbox with `GameCamera width=4000.0 height=2500.0` on its Camera; 20% and 10% double and quadruple that):

```sh
make build-release BUILD_TARGET=chibi_ctl
cat > /tmp/z40.json <<'JSON'
{"operation": "preview_scene", "kind": "scene", "name": "SandboxZ40", "frames": 30,
 "width": 1600, "height": 1000, "render_frames": 120,
 "freeze_simulation": true, "pass_timing": true}
JSON
build-release/chibi_ctl --project-root /path/to/arena2d-copy --render-request /tmp/z40.json
```

Read `performance.material_pieces`, `median_ms` and the `Material raster` pass. For the old binary, set `CHIBI_RENDER_SHADER_DIR` to the old commit's `src/render/shaders`. For the web, `make web-serial`, serve the copy with `tools/web_serve.py`, wait until the Scene stops showing Generating world, and count `requestAnimationFrame` callbacks for a few seconds at the default zoom, then after 5 and 10 wheel notches out (20% and 10%).

### Gaps

- Wider views still draw more pieces: the cave's silhouette in pixels grows as the view widens. At 10% Sandbox draws 63k pieces at 1600×1000. A sparse field atlas or baked chunks would bound that further.
- Levels change with hysteresis, not a cross-fade; a switch only merges detail below a pixel.
- The Scene view streams terrain up to 4,096 units from its center, so wide zooms in the editor wait for generation, which costs more than drawing.
