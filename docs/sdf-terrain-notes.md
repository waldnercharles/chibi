# SDF terrain direction

The reference is Mike Turitzin's [dynamic SDF engine video](https://www.youtube.com/watch?v=il-TXbn5iMA). Its description, chapters and auto-generated transcript explain distance caching, interpolation, sparse bricks, geometry clipmaps and incremental updates. The following choices are our application of those ideas to a 2D game.

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

### Design

- Baked chunk impostors, as Noita's chunk textures or Unreal's HLOD proxies, would draw a quad per tile, but each tile would need all six material planes per texel, animated materials would freeze, and every cut would rebake a tile.
- Mipmapped masks keep the representation: a coarser level is another mask of SDF pieces, so materials, lighting inputs, destruction and coverage antialiasing work unchanged. This is the chosen design.
- `material_mask_reduce` halves a mask's resolution: each cell merges a 2×2 block and is solid where the block's mean distance, the field at its center, is inside, and marching squares rebuilds its silhouette. A mask has one material, so every level keeps it.
- `render/material_lod` keeps the levels of the masks a renderer drew last frame, builds them lazily from the next finer level, rebuilds them when a cut changes the mask's revision, and releases those the view no longer draws. Each level holds a quarter of the cells of the one below, so all of them add at most a third to the visible masks.
- A mask draws the finest level whose cells span at least a pixel (`RENDER_MATERIAL_LOD_PIXELS`), as a mipmap is picked so its texels cover pixels. Cells of a pixel or more draw exactly as authored. A level is held until its cells leave their range by a quarter octave, per cell size, so all masks of one cell size switch together and a view that hovers at a boundary does not alternate. A threshold of two pixels would halve the pieces again, but Swarmers' 1-unit cells span 1.3 pixels at Sandbox's game zoom in a 720p window and would draw coarser in the game.
- Masks whose dimensions stop halving evenly keep the coarsest even level, so a level never extends past its mask. A mask with a border keeps it as thick as authored: it coarsens only while whole cells still make up the border.
- Pieces of one mask carry the same 1 KB of material and shape parameters. The raster now uploads each run of equal parameters once, and a piece's `time[1]` names its block, so a piece uploads 336 bytes instead of 1,360. Output is pixel-identical.

### Gaps

- Wider views still draw more pieces: the cave's silhouette in pixels grows as the view widens. A sparse field atlas or baked chunks could bound that further.
- Levels change with hysteresis, not a cross-fade; a switch only merges detail below a pixel.
- The Scene view streams terrain up to 4,096 units from its center, so wide zooms in the editor wait for generation, which costs more than drawing.
