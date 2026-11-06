# Texture import and visual-package budgets

Studio and `chibi_ctl` use the same authoring SDK to prepare texture derivatives,
measure their prepared cost, and publish a reviewed import. Authored material
graphs store virtual `content/...` image keys. Game projects choose their maps,
UV scale, displacement amplitude and material composition.

## Import a texture

`plan_image_import` is read-only. It accepts an absolute source PNG or JPEG path,
a new `content/...png` destination, `size` (`source`, `1k`, or `2k`) and `semantic`
(`color`, `scalar`, or `normal`). The default is a 1K color derivative. Sizing
preserves the source aspect ratio and never enlarges it.

```json
{
  "contract": 1,
  "operation": "plan_image_import",
  "project_root": "/path/to/project",
  "source": "/path/to/GreyRock01_4K_BaseColor.png",
  "destination": "content/textures/grey-rock/basecolor.png",
  "size": "1k",
  "semantic": "color"
}
```

Review the returned dimensions, source/output hashes, pixel bytes, prepared
bytes and visual budget. Submit the same fields with `operation: "import_image"`
and the returned `plan_hash` to apply it. Apply rereads the source and project,
rechecks the budget and refuses a stale plan or an existing destination.
The original file remains unchanged. A failed publication removes its temporary
output. Importing a file and changing a material's image reference are separate
actions; graph undo/redo restores the reference without deleting shared files.

Each imported PNG has a sibling `.texture.kdl` record with its source path,
maximum size and semantic. Sources inside the project use project-relative
paths; external sources retain their absolute path. Keep source files available
when reimporting. The image's `content/...png` key remains its runtime identity.

## Work with a texture in Studio

Textures appear in the existing Assets grid and list. Select one to inspect its
preview and import settings in Properties. Changes to maximum size and texture
type use the existing document undo/redo history. Save settings writes only the
configuration; Reimport saves pending settings and regenerates the PNG from the
recorded source. A failed source, conflict or budget check keeps the old PNG.
Successful reimport refreshes resident pixels and the asset preview.

Drag a texture from Assets into a material graph to create an Image Texture node.
Creation and its initial image reference are one undo step. Color textures start
with sRGB sampling; data and normal textures start with linear sampling. Existing
nodes keep their color space, filter and wrap choices on reimport. A normal map
still needs the material's normal-map conversion node.

Existing PNGs without import metadata can be browsed, previewed and used in
materials. Their inspector reports that no import source is recorded. Importing
through Studio creates the settings needed for reimport.

The SDK exposes metadata parse/serialize/read/save and reimport preparation in
`authoring/image_import_commit.h`. Metadata writes reject stale exact-file hashes.
Reimport preparation records the source, configuration, existing PNG and visual
budget; commit rechecks them before atomically replacing the derived PNG. Initial
import publishes both files exclusively and rolls back its new metadata if PNG
publication fails. This is not a multi-file power-loss transaction.

Use `color` for sRGB base color. It filters in linear light with premultiplied
alpha and encodes the result as sRGB. `scalar` filters data channels directly,
for example roughness or height. `normal` averages signed tangent-space vectors
and normalizes the result; it preserves the source's normal-axis convention.

Source files are bounded to 64 MiB encoded and 4096 pixels per side. Derivatives
are RGBA8 PNGs with at most 2048 pixels per side. Non-8-bit PNG imports are
explicitly refused. Grey Rock01's original 16-bit height therefore needs an
explicitly prepared 8-bit control; the importer never silently discards its
precision. Original scan files should remain outside the project's `content/`
tree until an intentional derivative is ready.

## What the budget measures

The prepared visual collection has a 64 MiB payload limit. Four 2048-square
RGBA8 images consume exactly 64 MiB in pixels alone, before image headers,
material programs, mesh data and other visual resources. Four 1024-square
images use 16 MiB in pixels. Compressed PNG file sizes do not predict this cost.

Preflight uses the same collection traversal and codecs as visual preparation.
It reports current and proposed per-kind totals, image pixel totals, hashes,
resource counts and whether the proposal fits. Proposals replace matching image
keys or add unique keys. Shared image keys count once.

The controller measures every saved prototype and scene. Studio measures its
resident visual catalogs and labels the exclusion of scene resources and
parameter records.
Full project compilation/export checks the complete saved project. Neither
report claims to measure the complete player envelope, retained CPU memory,
peak preparation memory or GPU allocation.

## Ownership

`Chibi::image_preparation` owns decoding, semantic filtering and derivative
encoding. `Chibi::shader_preparation` owns visual collection measurement.
`Chibi::image_import` owns filesystem planning and publication with a synchronous
budget callback. Offline project measurement owns and clears the process's
authoring catalogs, so a live Studio preview must use the resident-catalog
callback instead.

Studio's `.chibi` behavior owns the import form's actions and state. Its native
adapter supplies the operating-system file chooser and calls the shared SDK.
Texture choices use the existing graph document history and save path.

The native Grey Rock01 workflow is verified through import review, publication,
image selection, undo/redo, save and a fresh-process reopen. Studio's generic
choice resolver and both image-edit surfaces now accept string-valued image
references. See [checkpoint evidence](grey-rock-checkpoint.md) for exact scopes.

Filesystem publication currently supports POSIX hosts. Windows retains the
portable codec and budget APIs; file import returns an explicit unsupported
error until reparse-safe atomic publication is implemented there.
The browser builds the same authored form and reports that native texture import
is unavailable; a browser file-picker adapter is a separate remaining capability.
