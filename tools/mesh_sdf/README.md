# Local mesh to sampled SDF

CUT-53 adds a CPU-only converter for **triangulated Wavefront OBJ**. It reuses the
Suzanne recipe's RG16 PNG encoder and the existing `Model Sampled SDF` node. The
Suzanne subdivision, capping, orientation and parity sampling remain specific to
`projects/suzanne_sampled/convert.py`; they are not applied to imported meshes.

## Small example

Requires Linux, Python 3 and Blender **4.3.2** with its bundled NumPy. No pip install
is needed. Blender setup belongs to CUT-52. `BLENDER` or `--blender` selects its
executable. Example from the repository root:

```sh
python3 tools/mesh_sdf/convert.py test/fixtures/mesh_sdf/box.obj /tmp/local-box \
  --axes blender --scale 2 --padding 1 --resolution 17
```

The committed `projects/local_mesh_sampled` project contains the 17³ result and
MCP-authored `LocalBox` model (no scene or runtime rendering dependencies).
The CC0 example is a closed asymmetric box, deliberately sized to reveal axis
swaps: source extents `(2,4,6)` become engine extents `(4,12,8)`. The output has
`content/sdf/<pixel-fnv1a64>.sdf.png` and `provenance.json`. The source and PNG also
have SHA-256 digests. Identical inputs, flags, converter and Blender build produce
identical output bytes; elapsed time is reported on stdout, outside provenance.

To author the converted directory as a project through public MCP:

```sh
printf 'version 5\nname "Local box"\ncontent "content"\n' > /tmp/local-box/project.kdl
python3 tools/mesh_sdf/author.py /tmp/local-box --controller "$PWD/build/chibi_ctl" --name Box
build/chibi_ctl --project-root /tmp/local-box --format
```

`author.py` searches the node catalog, creates a sampled-volume → named part →
output graph, validates the model and returns its inspection. Use a new model
name; it does not overwrite existing models. For an existing project, copy the
content-addressed PNG into its `content/sdf/` and set the five sampled-node inputs
from provenance: Image, Half Size, Resolution, Range and Lipschitz. There is no
runtime format change, editor integration or mesh-to-SDF MCP endpoint in this slice.

## Input and geometry contract

- `v x y z` and triangle `f` records are supported, including slash-separated and
  relative negative vertex indices. `vt`, `vn`, object/group, smoothing and
  material declarations are ignored; geometry is taken solely from positions
  and face winding. Other directives and non-triangular faces fail explicitly.
  Export triangulated geometry with object transforms already baked. OBJ has no
  scene transform, unit or axis metadata; normals/materials are not converted.
- `--axes blender` means `engine = scale * (x,z,-y) + translate`.
  `--axes engine` means `engine = scale * (x,y,z) + translate`.
  Scale is a required positive number of **engine units per source unit**;
  translation and padding use engine units. There is no recentering, auto-sizing,
  negative scale, or normalization. The atlas bounds are origin-centered with
  `half_size = max(abs(transformed_vertices)) + padding` on each axis.
- Resolution is cubic in **sample count**, not physical dimensions: each axis
  has N endpoint samples, spacing `2*half_size/(N-1)`. Atlas tiles progress in Z;
  rows/columns within a tile progress in Y/X. R is the high byte, G the low byte;
  decode as `(uint16 / 65535 * 2 - 1) * range`.
- Require closed two-manifold geometry, coherent **outward** winding for each
  edge-connected shell, nondegenerate triangles, and no unused vertices.
  Boundary/non-manifold edge counts, disconnected vertex fans, inconsistent
  winding and nonpositive component volumes produce actionable errors. No
  holes are filled, vertices welded, faces flipped or other repairs performed.
- **Assumption:** each shell is non-self-intersecting. This slice does not prove
  absence of triangle intersections. Solid-angle winding determines inside;
  the minimum signed distance across shells implements a union (as in Suzanne).
  Nested shells do not create cavities. Overlapping shells are accepted under
  this union convention; the resulting field is a signed implicit union, not
  necessarily the exact Euclidean distance to the union boundary in overlaps.
  Unsupported self-intersecting input must be repaired upstream explicitly.
- Transformed vertices are rounded to Blender BVH's float32 storage **before**
  topology, volume, bounds and sign checks. Provenance records the maximum rounding
  error. Geometry that degenerates after rounding is rejected. Extremely tiny triangles
  (cross-product length ≤1e-12), nonpositive/tiny shell volumes (≤1e-12), nonfinite
  vertices and transformed coordinates beyond ±1e6 are rejected. Detail below
  grid spacing can be lost. A grid with no negative samples fails with advice to
  adjust resolution/placement; decoded borders must remain strictly positive.

Before any PNG is written, metadata is checked against the native
`shader_volume_contract` (including its float32 representation): half-size must
be in `(1e-5, 1e6]`, range in `(0, 1e6]`, and Lipschitz in `[1, 16]`. The computed
conservative Lipschitz value has a floor of 1. Oversized padding can violate
half-size or range even for a tiny valid mesh and is rejected before publication.

## Budgets, provenance and publication

The launcher is required: invoking `worker.py` directly bypasses resource limits.
Defaults are 60 seconds including input preparation and Blender startup, 4096 MiB child virtual address
space (Linux `RLIMIT_AS`), and 20 million voxel×triangle sign evaluations. CLI
flags `--max-seconds`, `--max-memory-mib`, and `--max-work` override those budgets.
Fixed limits are 3..128 samples per axis, 8 MiB source bytes, and 100,000 vertices
or triangles. Input is opened nonblocking, checked as a regular file using that
same descriptor, then read with a hard limit of 8 MiB plus one byte. FIFOs and
devices are rejected; growth after the descriptor check cannot bypass the byte
limit. A launcher deadline also interrupts stalled input preparation.
Sampling proceeds one grid row at a time; it does not allocate a
voxel×triangle array. Large/complex meshes may exceed the deliberately small
work budget. Increase a budget explicitly only after considering the work.

The destination must not exist. A bounded subprocess works in a temporary sibling
folder; only a completed PNG plus provenance directory is renamed into place.
Errors, timeouts and memory failures return nonzero and clean temporary files;
no incomplete asset is published. As with ordinary filesystem tools, externally
killing the launcher or loss of power can leave a hidden staging directory.

Provenance records exact source and converter hashes, Blender executable SHA-256/build/NumPy/zlib
versions, settings/budgets, transform, mesh bounds and geometry hash, component
volumes/orientation, sign assumptions, no-repair policy, actual sample/sign work,
and atlas dimensions, quantization error, border minimum and conservative
finite-difference Lipschitz bound. FNV pixel addressing matches the engine;
SHA-256 hashes identify the source and encoded output independently.

## Focused checks

```sh
python3 -m unittest discover -s test -p test_mesh_sdf.py -v
CHIBI_CTL="$PWD/build/chibi_ctl" python3 -m unittest discover -s test -p test_mesh_sdf.py -v
make test-fast TEST_ARGS=--model-sampled
```

The Python checks cover all samples of a tiny analytic box, anisotropic bounds,
axis mapping, explicit scale/translation, repeated byte determinism, concave and
disconnected/overlapping/nested-shell sign cases, open and
inconsistently wound/non-manifold/inward meshes, invalid settings and work/time/
memory exhaustion with no published output. Metadata-limit, FIFO/device, growing
file and input-preparation deadline regressions cover publication failure paths. With `CHIBI_CTL`, they also author,
validate, inspect and bake the result through public MCP. A synthetic 5³ field
compares the shared encoder byte-for-byte with the pinned PR151 Suzanne encoder,
without regenerating Suzanne. These CPU checks do not establish GPU performance
or representative-hardware rendering acceptance (CUT-44/CUT-51).
