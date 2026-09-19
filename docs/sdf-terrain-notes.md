# SDF terrain direction

The reference is Mike Turitzin's [dynamic SDF engine video](https://www.youtube.com/watch?v=il-TXbn5iMA). Its description, chapters and auto-generated transcript explain distance caching, interpolation, sparse bricks, geometry clipmaps and incremental updates. The following choices are our application of those ideas to a 2D game. See [the performance follow-up](neon-material-performance.md) for measured priorities.

| Technique | Decision for this engine |
| --- | --- |
| Interpolate signed samples, rather than occupied pixels | Implemented: retain cave field values, reconstruct subcell contours, then evaluate polygon SDFs with antialiasing at the material raster's resolution. |
| SDF subtraction for destruction | Implemented: circular cuts update nearby signed samples after the material's damage threshold is reached. Cells still own connectivity, resistance and Box2D collision. |
| Cache work between edits | Implemented: rebuild the mask's contour only when it changes. Merge full interior tiles into rectangles. Cull outside the padded HRC view before preparing terrain draw records. |
| Sparse brick atlas on the GPU | Potential next optimization if terrain submission dominates. It could replace many contour primitives with a field lookup. It needs revision tracking, border samples and measured upload costs. |
| Clipmaps | Defer until streaming and zoom need multiple detail levels. Today's camera has a bounded view and fixed terrain sampling; a multilevel cache would add complexity without demonstrated benefit. |
| Temporary/non-destructive CSG tools | Possible later gameplay feature. It needs edit ownership and collision updates; it is not part of this visual pass. |

[Chris Green's Valve paper](https://steamcdn-a.akamaihd.net/apps/valve/2007/SIGGRAPH2007_AlphaTestedMagnification.pdf) explains why interpolating distance samples preserves edges better than filtering binary coverage. Its screen-space antialiasing approach is relevant to keeping our boundaries crisp. It also describes the limitation of a single distance channel at sharp corners; increasing display resolution cannot recover missing topology.

[Söderlund, Evans and Akenine-Möller's paper](https://jcgt.org/published/0011/03/06/paper-lowres.pdf) compares SDF-grid traversal and intersection methods, including sparse brick storage and continuous normals. Their fastest reported path uses RTX hardware for 3D ray traversal. Our top-down 2D surface pass does not need that traversal. Sparse storage and coherent boundary samples transfer more directly; replacing HRC or requiring hardware ray tracing is unnecessary here.

[Losasso and Hoppe's geometry clipmaps](https://hhoppe.com/geomclipmap.pdf) keep nested grids around the viewer and update them incrementally as the view moves. That is relevant to future large-world streaming, but their terrain is a height field. Our caves need a 2D signed field with holes, so we would borrow the cache/update strategy rather than their terrain representation.

## Current approximations

The procedural cave field preserves its zero crossing but is not a globally exact Euclidean distance field. It is used for contour extraction, not safe sphere-tracing steps. Circular subtraction is restricted to damaged regions. Physics and fragment connectivity retain their two-unit terrain cells; tiny holes below that scale and perfectly matching smooth collision are not provided. Chunk boundary samples currently clamp at the edge, so a small contour kink can remain at a storage boundary. Field halos and synchronized boundary edits would address that if visible in motion.

Liquids use an isodensity boundary with an analytic gradient to limit antialiasing to roughly a pixel. Exhaust uses smooth unions of circles; burning fuel uses smaller separate hot cores. Cold gas uses a translucent density field stretched along flow. These presentations share particle physics and HRC material textures.
