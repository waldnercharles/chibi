# Offline material channel baking

`chibi_ctl` accepts `bake_material` through its ordinary JSON-lines interface.
This operation evaluates a saved, library-linked compiled Chibi material on the CPU. It
creates no files, starts no GPU context, and does not modify the source.

```sh
printf '%s\n' '{"contract":1,"operation":"bake_material","kind":"material","name":"SurfaceBands","width":32,"height":32,"channel":"roughness"}' | build/bin/chibi_ctl --project-root test/fixtures/feature-coverage
```

Use `path` instead of `kind`/`name` to identify a material by its saved asset path.
Omit `channel` to return all channels: `base_color`, `emission`, `roughness`,
`metallic`, `normal`, `opacity`, `unlit`, `height`, and `occlusion`. Channel names
are defined by the shared shader bake kernel, and unknown names are rejected.

Dimensions default to 32 and must be integers from 1 through 128. Sampling uses
pixel centers over `uv_min`/`uv_max` (default `[0,0]`/`[1,1]`). Both maximum
coordinates must exceed their minimum. Optional `origin`, `axis_u`, and `axis_v`
define the plane's origin and full edges; defaults are `[0,0,0]`, `[1,0,0]`, and
`[0,1,0]`. Row zero follows the V edge from the origin. World, object and part
coordinates coincide. The kernel derives local Position using object XZ.
The plane normal supplies Normal, while View Direction is fixed to positive Z.
`time` defaults to zero. Beat and surface influence are zero; scene lighting and
rendered appearance are not baked.

The result records sample dimensions, plane/UV coordinates, time, source asset
identity/hash, resolved parameter identities/values, and channels. Each channel
contains its name, component count, encoding, flat row-major float data and hash.
Values are raw linear float32 samples, without normalization, clamping, color
conversion or image quantization. Color channels are linear color; other scalar
channels are data. Normal is the raw Surface Output direction in the material
sampling frame (world, object and part frames coincide here). Its zero-vector
sentinel means use the geometry normal. It is not a normalized tangent-space
normal texture or a ready-to-export normal PNG. No sentinel resolution or
normalization is performed. Per-channel `semantic` metadata records this distinction. No texture files, UV unwrap, image atlas or material-instance asset is
created. Alpha settings already folded into the material's Opacity remain in
that output.

`overrides` follows the existing scoped identity shape:

```json
{"asset":"Stone","scope":"","parameter_id":12,"value":[0.4]}
```

This first operation supports parameters owned by the root material, including its local function graphs.
`parameter_id` is the source node ID returned in `parameters`; `asset` must match
the material and `scope` must be empty. Unknown identities, duplicate overrides,
unsupported scopes and wrong component counts are rejected. Omitted parameters
use lowered shader defaults. Images use the existing CPU project image resolver;
missing or invalid dependencies fail the bake rather than substituting pixels.
Source images still use the existing RGBA8 image contract. Float output preserves
procedural values; high-precision image input, texture export codecs and baked
asset publication remain later stages of the environment roadmap.

Outputs must be finite. A conservative node-work bound includes derivative
resampling overhead and caps evaluation at 16,777,216 estimated node visits.
This estimate is a work-admission limit, not a measured instruction count or
latency promise. Oversized work is rejected before evaluation.

Channel hashes are FNV-1a over float32 bits serialized least-significant byte
first, in returned component order. They provide repeatability evidence for the
same source, parameters, images, context and implementation. They are not a
cryptographic provenance hash or a promise of identical floating-point results
on every platform. CPU bake tests do not establish GPU rendering parity.

Input provenance is separate from the root source hash. `linked_graph_hash`
fingerprints the portable prepared shader IR, including linked library code,
parameter defaults and image sampler settings; `linked_graph_format` records the
prepared format version. `image_dependencies` records each key, sampler settings,
dimensions and decoded-pixel hash. The pixel hash covers two little-endian uint32
dimensions followed by straight-alpha RGBA8 bytes. These input hashes use
`fnv1a64-bytes`, distinct from the float-bit channel hash algorithm. Changing an
image can leave the root source hash unchanged while changing its dependency
fingerprint and sampled channel values. All hashes remain noncryptographic.

Each request admits at most eight distinct image source keys, checked before
opening any dependency. Sampler variants sharing a key count once. Aggregate
decoded RGBA8 pixels must fit 64 MiB; each unique image is charged immediately
after decoding and before evaluation or provenance hashing. The existing image
loader also limits each image side to 2048, so a failed aggregate admission can
transiently hold at most one additional 16 MiB decoded image. A failure returns
no baked channels. These limits apply independently of sample/node-work limits.
