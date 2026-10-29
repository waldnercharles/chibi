# Procedural terrain volume

Approved 2026-10-16: plan and prototype a procedural 3D density field whose
play-plane slice is the authoritative destructible mask. Motivating game:
Voidweaver's basalt cave. The cutaway recess
([terrain-surface-geometry](terrain-surface-geometry.md)) stays as the
fallback; it cannot express arches, vaults, columns or stalactites in depth.

## Model

One authored field describes rock in 3D: X right, Y down the gameplay plane,
and a new `Field Z` that is depth behind the play plane, positive away from the
camera. Every existing 2D bake samples at Z = 0. `MaterialTerrain` therefore
bakes the play-plane slice from the same field, so its mask is that slice by
construction. The mask remains the only gameplay and saved state for
collision, damage, fragments, streaming and checkpoints. Nothing about
persistence changes.

`TerrainVolume` (optional, on the `MaterialTerrain` source) adds a derived
density grid behind the plane:

- **Base:** the same field and context sampled over the terrain rectangle
  and Z in [0, depth]. It is baked once per terrain incarnation: a dense
  coarse lattice every `voxel * refine` as Z slices through the row baker,
  then the fine samples every `voxel` of each coarse cell near a surface,
  through point baking. Elsewhere the density blends the coarse corners. A
  back wall at `depth` closes the volume unless `open_back`.
- **Carve:** wherever the base slice is solid but the live mask is air, the
  volume opens a cone, `e(x, y) - |z|`. Here `e` is the signed distance into
  that removed region, so the opening at the plane equals the mask exactly.
  Because the carve is a pure function of the base and the live mask, cuts,
  fragments, partial damage, streaming, reload and checkpoint restore all
  reproduce the same volume, and no edit log is saved.
- **Surface:** dual contouring per chunk, clipped behind the plane. The
  camera-facing section face is still the mask's own `MaterialSurface3D` cap,
  so the visible silhouette is the collision boundary. Normals come from the
  density gradient. Materials sample triplanar world position.
- **Lighting:** chunk meshes cast direct shadows. Indirect proxies come from
  fully solid voxel bricks, merged into boxes and capped at half the scene's
  4096. Openings in 3D stay open for GI. This removes the slab approximation and the
  rear-mesh opt-outs for scenes that use it.

Lifecycle: base volume per (field revision, parameters, seed, bounds, voxel
size). Per-chunk meshes are keyed by the revisions of the mask chunks under
their footprint plus the carve margin. Unchanged frames reuse buffers; a cut
re-meshes only the affected chunks; removal releases everything.

## Increments

1. `Field Z` op and node; 2D bakes unchanged and pinned by tests. `field_grid`
   gains `depth`, so Z is constant per slice and row reuse still holds.
2. `runtime/geometry/terrain_volume.[ch]`: slice baking, the carve function and
   surface nets with gradient normals. Tests cover a sphere field, the
   slice-equals-mask agreement, a carve opening only the cut, back-wall closure
   and determinism.
3. Integration: the `TerrainVolume` component, runtime ownership on the terrain
   record, scene3d chunk meshing/upload/retirement, shadows and brick GI
   proxies. Cut, reset and removal lifecycle is covered with CPU cases plus one
   bounded GPU case.
4. Voidweaver: a 3D lava-tube field with tubes receding in depth, an arch
   bridge, a chimney to the surface, stalactites via shifted copies, and the
   section cap from the mask. Native 1080p off/on/cut captures, traversal and
   reset.
5. Later: columnar jointing via a cellular (Voronoi) field op; chunked streaming
   of volume chunks; GPU-side meshing if cut latency demands it.

## Integration

The volume uses these shared modules:

- `Field Z` is appended to the field IR and node pack; `field_grid.depth`
  feeds it, so planar bakes are unchanged.
- `runtime/geometry/distance_transform.[ch]` is now shared by the recess and
  the volume carve. `runtime/geometry/terrain_volume.[ch]` holds slice baking,
  the removed-region carve, surface nets and brick boxes.
- `TerrainVolume` bakes with its terrain in `material_ecs_terrain.c` and is
  freed with it. The carve resyncs when any tile's content revision changes
  (`material_ecs_surface.c`), and `materials_terrain_volume` reads it.
- `render/scene3d_terrain_volume.[ch]` meshes 32-sample chunks, keyed by their
  carve footprint, and adapts brick size to fit half the proxy budget.
  `scene3d.c` submits the chunks as dynamic meshes with direct shadows and
  adds the boxes as indirect proxies.

Known limits:

- Fixed 2026-10-17: checkpoint-restored terrain lost its volume. A restore
  now keeps it and carves it again from the restored mask.
- Streaming is rejected.
- The bake runs on the main path at scene load, about two seconds for 4.8M
  samples.
- Superseded by real-time cuts below: the carve no longer resyncs the whole
  plane.
- No dedicated GPU case for volume chunk upload or retirement exists yet; the
  existing dynamic-mesh lifecycle is reused.

## Generation quality

The user asked for terrain that never looks pixelated, realistic stalagmites,
a convincingly 3D environment and a stylized basalt cave, using Fingal's Cave
photographs as reference: grouped hexagonal columns, stepped broken tops, a
ceiling of column ends, fanning vaults and joints with mineral staining.

Diagnosis of the current prototype:

1. Stair-stepped halos at silhouettes come from half-resolution fog without
   depth-aware upsampling.
2. Clay-like blobs come from surface nets on a 4-unit grid, which rounds every
   edge, so crisp facets are impossible.
3. Texture shimmer comes from missing mipmaps.
4. Stalactites and mounds are shifted copies on a lattice. Their silhouettes
   are plausible, but they are too coarse to model cones, rings or tips.

Recommended route (A), keeping the authoritative Z = 0 slice:

1. **Dual contouring:** replace surface nets. Place each cell vertex with a
   QEF over Hermite data (edge crossings plus normals), so planar column faces
   and broken ends keep sharp edges [Ju et al. 2002]. Normals come from the
   field itself, not grid differences.
2. **Sparse, finer sampling:** bake the field coarsely to find surface bricks,
   then resample only those at the 2-unit mask resolution, or finer for
   formations. Bake bricks in parallel and in the background; carve and
   re-mesh per brick.
3. **Basalt primitives in the field language:**
   - `Field Cell Edge`: exact distance to the Voronoi border by Quilez's
     two-pass bisector method, not F2 - F1, whose borders vary in width.
   - `Field Cell Hash`: a per-column value.
   - Relaxed, jittered seeds give mostly hexagonal and pentagonal cells.
     Columns are prisms with hashed heights, transverse joints and chamfered
     edges, oriented along an axis field so vaults can fan.
4. **Speleothems as placed SDF instances:**
   - Poisson-disk sample ceilings found from the field.
   - Each stalactite is a tapered cone with ring modulation and a drip tip.
     A matching stalagmite is cast to the floor below; some pairs fuse into
     columns.
   - Instances join the field by smooth union, so the play-plane slice and
     the carve include them [Cui et al. 2011; Computers & Graphics 2021].
5. **Rendering fixes:** depth- and normal-aware upsampling for fog, mipmaps
   with anisotropic filtering, and soft spot shadows. The front-of-plane
   caster rock (implemented, `TerrainVolume.front`) keeps the cutaway from
   leaking light.
6. **Stylized basalt material:** per-column tint and roughness from the cell
   hash, convex-edge highlights and cavity darkening from curvature, wet sheen
   on upward faces, and ochre-green staining in joints and ceiling pockets.

Alternative (B): render the SDF directly by sphere tracing a mipmapped brick
volume, as Claybook does [Aaltonen, GDC 2018]. Silhouettes are exact at every
resolution and destruction is a volume write. It means replacing scene3d's
mesh raster, shadow and GI inputs, so it is a later research track, not the
next step.

Increments:

1. Rendering fixes (fog upsampling, mipmaps) and dual contouring, with tests
   on sharp-edged prisms.
2. Cell ops and a basalt column field validated on a non-Voidweaver specimen.
3. Sparse fine bake.
4. Speleothem instances.
5. Voidweaver bay rebuilt as a basalt sea-cave composition.

Each increment has native 1080p captures compared against the reference
photographs.

Sources:

- [Dual Contouring of Hermite Data](https://www.cs.rice.edu/~jwarren/papers/dualcontour.pdf)
- [Manifold Dual Contouring](https://www.cs.wustl.edu/~taoju/research/dualsimp_tvcg.pdf)
- [Quilez, Voronoi edges](https://iquilezles.org/articles/voronoilines/)
- [Columnar rock tool, Houdini](https://80.lv/articles/building-a-columnar-rock-tool-with-houdini-unreal-engine)
- [Procedural 3D karst caves with speleothems](https://www.sciencedirect.com/science/article/abs/pii/S0097849321002132)
- [Cui et al., caves with stalactites and stalagmites](https://www.semanticscholar.org/paper/Procedural-generation-of-3D-cave-models-with-and-Cui-Chow/8365da9afd1f2b767f9947a1dd4940d75c656bf4)
- [Procedural 3D caves on the GPU](https://lucris.lub.lu.se/ws/files/6067634/5464988.pdf)
- [Depth-aware upsampling](http://c0de517e.blogspot.com/2016/02/downsampled-effects-with-depth-aware.html)
- [Claybook GDC 2018](https://media.gdcvault.com/gdc2018/presentations/Aaltonen_Sebastian_GPU_Based_Clay.pdf)
- [Infinigen](https://arxiv.org/pdf/2306.09310)

## Surface sampling and rendering

1. **Rendering fixes:** joint bilateral, depth-aware fog upsampling;
   mipmapped, anisotropic shader images sampled with `textureGrad` on their
   unwrapped coordinates; soft spot and point shadows from an emitter `size`.
2. **Dual contouring:** each cell vertex minimizes its edge-crossing tangent
   planes (QEF, gently pulled to their mass point and kept in the cell), with
   trilinear lattice gradients as normals. A box field keeps every vertex
   within 0.16 cells of its faces; corners still round by up to half a cell
   because the normals come from the lattice.
3. **Point baking:** `field_program_baker_bake_points` evaluates arbitrary
   positions in batches of 256 across the baker's workers. It matches the
   grid bake bit for bit at the same positions.
4. **Sparse fine sampling:** `TerrainVolume.refine` stores the coarse lattice
   densely and bakes `refine^3` fine samples only in coarse cells whose
   corners lie within a Lipschitz margin of zero (1.5 times the cell's
   half-diagonal plus two fine diagonals). Other cells blend their corners,
   which keeps their sign. A cell skips meshing and counts as a solid or open
   GI brick only when bounds that hold for the stored density prove it
   uniform, including the next cells' first samples, so no field can open a
   crack. On Voidweaver's 1100 x 760 x 360 bay at voxel 2, refine 4, 140,591
   of 590,000 cells (24%) bake fine samples: 9.6M samples instead of 38M
   dense. The refined mesh equals the dense fine mesh within 1e-4 on a sphere
   and a box field, with and without a cut.
5. **Columnar cells:** `Field Cell X`, `Field Cell Y` and `Field Cell Edge`
   are the Voronoi diagram of a unit triangular lattice whose sites move at
   most 0.25 (`Jitter`, seeded like noise). At that bound the nearest site is
   among seven lattice candidates and each cell borders only sites within two
   rings, so the search is local and exact; a brute-force test agrees to 1e-4.
   Sampling a field at its column's site turns walls into flat-sided prisms
   and floors into stepped column ends; `Cell Edge` cuts joints.
6. **Open back:** `TerrainVolume.open_back` leaves air the field has at the
   deepest layer open, so the sky shows through the cave.
7. **Sky materials and stars:** `Environment.sky_material` draws a surface
   material as the sky and lights the scene from it (see `rendering-3d.md`,
   Sky). The **Star Field** shader node draws point stars about a pixel wide
   at any field of view; a sky image cannot, since a 4096 x 2048 sky gives 11
   texels per degree. Planets and black holes are ordinary Geometry3D spheres
   with materials, so depth hides the stars behind them; spheres now draw a
   48 x 96 mesh, round when they fill the screen.

Known limits:

- Normals come from lattice differences, not the field; exact Hermite normals
  need field access at meshing time.
- Thin features farther than the margin from any coarse surface can vanish
  where a field changes faster than the Lipschitz assumption.
- Fog is homogeneous, so it must end before openings onto space.
- The perspective lens costs about 4.7 ms more in the surfaces pass than the
  orthographic camera. Voidweaver's space bay measures 27.1 ms median with
  its sun off and 38.6 ms on, mostly the sun's soft-shadow filtering.

## Cut updates

- **Slab:** `TerrainVolume.slab` (required, positive) caps how far cuts
  carve either side of the plane. Removed distances are held between six fine
  samples below zero, past which no surface reads them under the bake's
  slope bound, and the slab. Rock beyond `terrain_volume_reach` layers of the
  plane never changes.
- **Local carve:** `terrain_volume_removed` takes the window whose open
  samples changed and rewrites exactly the samples a whole-plane pass would
  change. `material_ecs_surface.c` keeps the last open plane and diffs it.
- **3D chunks:** chunks are 32 samples in depth as well. The renderer keeps
  the carve it last meshed; `terrain_volume_changed_layers` names the layers
  a change can reshape, and only chunks there re-mesh, on scene3d's task pool.
  Each column joins its chunks into one drawn mesh per side, so draw counts
  stay at one per column.
- **Boxes:** rock beyond the reach is fitted once; the slab is fitted per
  tile of chunk columns, refit only where it changed (see review fixes below).
- **Indirect light** keeps its history through re-meshes.
- **Fragments** from a volume terrain are about as deep as they are wide (up
  to twice the slab), straddling the plane, instead of 6-unit wafers.
- **Foreground:** `TerrainVolume.front_material` draws the rock ahead of the
  plane with that surface material; without it the rock only casts shadows.
  A masked material that opens around a `SurfaceInfluence` on the ship cuts
  the foreground away near the player.

## Restore and settings

- **Restore:** a checkpoint restore keeps the baked volume and clears the
  open plane it last carved, so the restore's surface sync carves the whole
  plane from the restored mask. Only regeneration discards a volume, and a
  new volume reports nothing until its first carve.
- **Settings:** the terrain hash covers `voxel`, `depth`, `front`, `slab`,
  `refine` and `open_back`. `elevation` and `front_material` only place and
  draw the volume, so editing them keeps cuts. Settings are validated, and
  the volume is baked, before the terrain's tiles are replaced, so invalid
  settings or a failed bake leave the terrain as it was.
- **Box coarsening:** groups of `span` bricks are narrower at a range's far
  faces instead of dropped, so the doubling always ends with at most `limit`
  boxes (one group covers the range). When the first span that fits leaves
  no box, the largest `limit` boxes of the span before it stay.
  `terrain_volume_boxes` never returns more than `limit`.
- **Box budget:** the volume holds at most 2048 proxies, half the scene's
  4096. The slab gets 1024 across tiles of chunk columns; a tile is one
  column until there are more than 1024 columns, then the smallest square of
  columns that leaves 1024 tiles or fewer. Rock beyond the reach takes the
  rest.
- **Fragments:** a piece split from a piece keeps straddling the plane; its
  elevation comes from its source's plane elevation, not its raised one.

## Authoring changes suggested by this work

- Done: fields as readable `.chibi` source with every op linked. Every field
  node but `Field Output` is a library call, pure functions call pure functions
  in their library and others, and seeded calls take the field's seed once
  inlined (seed 0 outside a field); see
  [procedural fields](material-engine.md#procedural-fields). A field can now be
  `Field X/Y/Z`, one library call and `Field Output`. Still open: arithmetic
  operators and local bindings in lowered functions, so expressions nest
  `field_add` calls.
- Hand-edited KDL graphs need generated IDs: an added node whose ID collides
  with another node's implicit pin IDs fails to load. Expression-form material
  and field sources would remove this whole class of error.
- Done: cellular field ops (`Field Cell X/Y/Edge`).
- Validation warnings for lights placed inside solid terrain, and a scene
  preview that shows the 3D field's play-plane slice next to the volume.
