# Sampled Blender Suzanne

This project keeps the original Blender Suzanne topology as provenance, applies
Catmull–Clark subdivision twice, then explicitly caps all four boundary loops.
The reference is the union of three closed, outward-oriented components (15,904
triangles). Stock Suzanne itself has 42 boundary edges. See reference/NOTICE.txt,
COPYING and provenance.json for licensing, exact geometry hashes and operations.

## Reproduce

```sh
blender --background --factory-startup --python projects/suzanne_sampled/convert.py -- 96 144
make build-release
cmake --build build-release --target test_suite
python3 projects/suzanne_sampled/author.py --preview
build-release/test_suite --model-sampled
build-release/test_suite --model-sampled-gpu "$PWD/projects/suzanne_sampled/content"
build-release/chibi --project-root projects/suzanne_sampled --open BlenderSuzanne
```

The converter pins Blender 4.3.2 and uses its bundled monkey primitive, not an
unverified external mesh. It writes deterministic numeric atlases, not images
intended for display. Repeating it in this environment yielded identical geometry,
PNG and sample-byte hashes. Python authoring uses public MCP, only creates missing
assets, validates saved graphs and captures three real application views.

`BlenderSuzanne` instances the 144³ sampled model through an editable Scale node.
The 96³ variant is retained for convergence comparisons. Geometry remains an
editable field composition: sampling metadata, transforms, booleans and named
parts are authored nodes; dense samples are separate assets rather than expanded
into thousands of shader nodes. Mesh exports are derivatives, not the source.

## Numeric format and limits

`Model Sampled SDF` takes literal Image, Half Size, Resolution, Range and Lipschitz
inputs. Resolution is 2–144. Each Z slice is an XY tile; tiles use row-major order
with ceil(sqrt(N)) columns. Lattice endpoints are -Half Size and +Half Size.
PNG RG stores a big-endian unsigned 16-bit number; B=0, A=255. Decode as
`(RG16 * 2/65535 - 1) * Range`. The sampler is linear-color, nearest, clamped;
eight explicit reads perform the same trilinear interpolation on CPU and GPU.
CPU hit tests, ownership and mesh baking resolve the same retained image data.

Atlas dimensions, encoding, finite metadata, strictly positive outer border and
a conservative gradient bound are validated before resource publication. The
filename is `<16 lowercase hex FNV-1a64 of all decoded RGBA bytes>.sdf.png` under
content/. It is immutable: changed pixels require a new name and source edit.
FNV is a cache identity, not a security guarantee; provenance also records SHA-256.
Invalid image reloads retain the last good pixels/textures; failed compilation
retains the last good program. Shader and hit graphs own independent image keys.

Outside the grid, the field equals the boundary interpolation plus distance to
the box. The positive border prevents an artificial boundary surface. The raw
field is not a mathematically exact signed distance: interpolation and union
introduce approximation. Surface evaluation and normals preserve raw values;
marching separately divides its step by `Lipschitz + 1`, conservatively covering
the extension. Transformed/combined fields retain this maximum bound. Sampled
models use up to 512 steps and no minimum step that could leap thin features.
A finite iteration budget can still miss difficult grazing intersections.

The sign recipe uses deterministic +X parity scanlines per closed component,
Y/Z ray offsets 1e-7/1.61803398875e-7 and advancement 1e-3 engine units after a
hit to avoid counting one face twice. Actual distance samples stay on the lattice.
Odd intersection counts fail conversion. Features below the grid spacing remain
an approximation; this is not a general exact mesh-to-SDF guarantee.

## Verification

Public bakeModel exports at cell size 1.2, max_axis_cells 256, max_triangles 349525,
max_samples 100000000 and max_work 100000000000. A cell size .8 exceeded the
existing 128 MiB mesh JSON limit; the limit was not changed. The one-off
Blender geometry comparison script has been retired.

The recorded geometry comparisons used 50,000 deterministic area-weighted surface samples per
direction and nearest reference triangles. Samples on component surfaces buried
inside another component are excluded from the source union boundary (45,779
remain). Extracted-to-reference distances target the closed triangle collection;
these sampled maxima are not Hausdorff bounds. The script also rendered the closed
reference from three views. Numerical results and actual app captures are kept
under .chibi/sampled-suzanne, outside the authored project.

## Orbiting-emitter demonstration

The separate `projects/sdf_lab/content/scenes/SdfSuzanneOrbitingEmitters.kdl`
scene copies the existing `SdfDepthCoverageCircle` fixture and replaces its
receiver with `SdfSampledSuzanne`. The original fixture is unchanged. The cyan
light keeps `SdfCoverageLightCircle`; the amber light uses the opposite phase
through the new `SdfSuzanneOppositeCircle` behavior. The emitters have radius six. Both orbit at radius 60 in
eight seconds at elevation 65. The demo adds low ambient fill (0.002) and exposure
3.0 to keep facial features readable; light transport remains the existing
projected depth-thickness approximation, not physically accurate 3D GI.

```sh
python3 projects/suzanne_sampled/author_emitters.py
build-release/chibi --project-root projects/sdf_lab --scene scenes/SdfSuzanneOrbitingEmitters.kdl --play
```

The authoring script validates all added assets and samples runtime transforms
at quarter turns; it verifies the original scene hash remains unchanged.

Measured software-Vulkan validation is recorded in `reference/validation.json`.
At fixed export cell size 1.2, source-to-extracted RMS falls from 0.0420 units
(96³) to 0.0319 (144³); reverse RMS falls from 0.0380 to 0.0290. Source width is
approximately 108 units. These are sampled accuracy observations, not guarantees
for every surface point or an exact-distance claim. CPU/GPU scalar samples matched
at all 256 tested Suzanne positions; normal component error was below 0.000439.
