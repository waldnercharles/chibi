# Surface detail for declarative models

Use geometry for the silhouette, recesses, protrusions and attachment points.
Use materials for fine grooves, molded-plastic grain, printed labels and paint.
Normal detail changes shading. It does not move the SDF surface, its depth or
its compiled bounds. The existing material Height input separately adds relief
to the 2.5D height/shadow representation.

The `PolaroidSurfaceDetail` study this page describes was retired with the
example projects on 2026-10-17 ([examples](examples.md)). Edit a project's
model, material, prototype and scene KDL directly, or use Studio. The former
`author-detail.mjs` recipe and MCP transport are retired as well.

Previewing a material alone cannot verify model geometry. The reports under `build/model-evidence`
record earlier captures; they are not results of the current migration.

## Nodes and coordinates

| Node | Inputs | Output |
| --- | --- | --- |
| Shader Planar Mapping | Position, Scale, Offset, Axis | UV |
| Shader Cylindrical Mapping | Position, Scale, Offset, Axis | UV |
| Shader Normal Map | Color, UV, Strength, Distance, Convention | World normal |
| Shader Bump | Height, Strength, Distance, Normal | World normal |
| Shader Triplanar Texture | Position, Sharpness, Distance, Image, Color Space, Filter, Wrap | Color, Alpha |
| Shader Decal Mask | Position, Half Size, Feather, Facing | Mask |

Position defaults to PartPosition for projection, triplanar and decal nodes.
Mapping3D remains the position transform operation; its rotations are degrees.
ObjectPosition uses the complete model's authored coordinates. PartPosition
uses the winning named Part's coordinate frame. WorldPosition is the world
frame. The renderer supplies their coordinate gradients, including actor and
part rotation, reflection and nonuniform scale. Detail evaluates those mappings
at the hit, after the geometry march.

Tag a shape with Model Part before placing that part when its texture should
have a centered local origin. Putting Part outside a Translate instead retains
the translated construction coordinates. The Polaroid example deliberately
keeps the baseline's latter convention so the badge UVs remain unchanged.

Planar mapping projects X onto `(z,-y)`, Y onto `(x,z)`, and Z onto `(x,-y)`.
Axis defaults to Y. It then applies componentwise Scale and Offset. Cylindrical
U is one revolution around Axis; V is the coordinate along that axis. Use its
Scale and Offset controls for repeats and placement.

Normal Map decodes linear RGB in the selected OpenGL or DirectX convention.
Images put (0, 0) at their top-left, so OpenGL's green tilts the normal up the
image, toward -v, and DirectX's down it, toward +v. Maps from Blender,
Substance's OpenGL preset, Unity, Godot and glTF use OpenGL; a new node starts
in DirectX, the convention value 0 that materials saved before the labels
were corrected meant. It builds a UV cotangent frame on the geometric normal;
on an imported mesh in the lit 3D pass, the frame follows the mesh's vertex
tangents (see `docs/rendering-3d.md`).
This is a tangent-space map, not a raw object-space normal-vector transform.
A common UV scale leaves normal strength unchanged; unequal scales and mirrored
UVs affect the corresponding tangent components. Fully collapsed UVs preserve
the base normal. A rank-one mapping can still supply one tangent component.

`OpenGL Tangent (+Y)` uses a unit orthogonal frame instead: texture density
and shear do not change normal strength. glTF imports select this mode. They
transform the Image Texture UV, keep the Normal Map UV at its mesh default,
and preserve supplied tangents or generate missing ones from the transformed
normal coordinates. Reimport an older model to get this material mode, or
select it on a hand-made normal map intended for a unit tangent frame.

Normal image ancestors must explicitly select **Linear data**. Image Texture
and Triplanar Texture share the existing image variant/settings contract.
Height data should also use Linear data, as the examples do. Color photographs
and painted color maps use sRGB color.

For cylindrical normal maps, connect Cylindrical Mapping directly to Normal
Map's UV pin. Its U seam correction uses the actual Scale.x period. Arbitrary
math downstream of cylindrical UVs is rejected for this derivative operation;
put placement and scale into the mapping's controls or transform Position with
Mapping3D. Affine planar UVs are never wrapped during derivative estimation.

Bump's Height is world-unit relief used to compute a normal. Distance is the
positive world-unit spacing of symmetric derivative samples. Choose it small
relative to the intended feature, with enough spacing to survive floating point
rounding. Zero Strength preserves the supplied normal. Bump can sample a
Triplanar Texture height expression. Sampling another Bump, Normal Map or Decal
Mask as a derivative expression is rejected with a source-node diagnostic.
Cyclic graph dependencies use the normal graph cycle diagnostic.

Decal Mask clips a mapped position to positive Half Size. Feather fades inward
from its box edge. Facing defaults to -1, disabling face rejection; otherwise
it is a minimum dot product between the geometric normal and mapped +Z. Blend
the decal color with the base color using this mask. Connect it to Opacity only
when a genuine cutout is intended.

## Examples and costs

`ModelSurfaceDetailStudy` has smooth, procedural-bump and modeled-ridge tiles
on the top row. The lower row has tangent normals, image height and triplanar
paint. Smooth and normal-only variants bind the identical `DetailTile` model;
only the modeled-ridge reference adds geometry.

`ModelMappingDetailStudy` shows the finite label and cylindrical image wrap.
`PolaroidSurfaceDetailStudy` compares the preserved photograph reconstruction
with a new variant. Four minor modeled marks become materials: the exposure
dash, the OneStep badge, the film-door nameplate and the accessory-shoe central
slot. The housing, recessed optics, controls and silhouette geometry remain.
The badges use the baseline's attributed photograph and UV homographies.

The public inspection captured during authoring reports:

| Model | Source nodes | Distance IR nodes | Hit IR nodes | Named parts |
| --- | ---: | ---: | ---: | ---: |
| PolaroidOneStep baseline | 154 | 903 | 982 | 26 |
| PolaroidSurfaceDetail | 136 | 833 | 896 | 22 |

These are geometry complexity counts, not a frame-time speedup. Material work
increases. Every derivative operation uses six evaluations of its reachable
upstream expression. A triplanar Color or Alpha operation samples its one image
three times. Bump sampling a triplanar height includes those three samples in
each of its six height evaluations, plus the base graph evaluation. Its static
texture-call bound is at least 21. Reusing an image keeps resource count small;
it does not remove those sample calls.

Shared inspection reports the conservative texture-call bound and upstream
expression sample count separately from authored/compiled node counts. These
bounds exclude GPU compiler reuse, disabled branches, SDF march repetition and
lighting. Existing limits remain 1024 material IR nodes, 32 material uniform
slots, eight image variants and 512 ramp vec4s. Geometry uses its separate
32-slot parameter budget.

The saved 64 by 64 RGBA8 maps are example data. `DetailHeight` and `DetailNormal`
are linear data maps; `DetailChecker` and `DetailLabel` are color examples. The
former `--refresh-new-materials` recipe is retired. Edit the saved
`DetailPolaroid*` material KDL and validate those assets to change their detail.
