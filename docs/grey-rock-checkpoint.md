# Grey Rock01 authoring checkpoint

This checkpoint implements shared terrain/rendering corrections and one Studio
texture workflow. Voidweaver owns its ship, environment composition and visual
acceptance. The engine owns terrain state, collision, derived geometry, image
preparation and import validation; Studio owns the authoring interaction.

## Texture asset lifecycle completion, 2026-10-16

The same isolated candidates now implement reusable texture entries in Assets,
cached thumbnails and inspector previews, persistent import settings, explicit
same-key reimport, and dragging a texture into a material. Inspector actions and
view state are authored in `TextureAsset.chibi`. Native Studio services reuse
`asset_code` history, the thumbnail cache and graph commands; the installed SDK
owns metadata validation and file publication. No new CLI commands, selectors,
tool registries or document kinds were added.

Import writes a derived PNG and sibling `.texture.kdl` containing its original
source, maximum size and color/data/normal semantic. The image's `content/...`
key remains its identity. Save settings persists configuration; Reimport saves
pending settings and regenerates pixels at that key after conflict and budget
checks. Existing material references and sampling settings remain unchanged.
Dropping an asset creates one initialized Image Texture node with one Undo step.
Pixel reimport is a disk publication, not an undoable graph edit.

The compact inspector uses existing controls and typography, with Save settings
and Reimport fixed above its scrolling preview, source and settings. The design
applies the requested Emil, minimal GPT Taste and Studio UX-laws guidance.

Current validation supersedes the earlier import-only results below:

| Check | Verified scope | Result |
| --- | --- | --- |
| `--image-import-commands` | Seven CPU cases, including metadata persistence, stale source/settings/target rejection, same-key reimport and replacement budget | Passed, 15.607 s |
| `--studio-texture-import` | 46 parent + eight fresh-process frames; Assets preview, settings undo/redo/save, drag undo/redo, image swap, reimport and reopen | Passed, 8.432 s supervised |
| Native SDK and Studio | Installed public SDK only; no private engine source includes | Passed |
| Web SDK and Studio | Serial WebGL2 package/build/install | Passed |
| Browser UI | 1280×720 Assets entry and actual preview; scrolling settings and fixed actions | Visually checked; file import not supported |

The native scenario modifies a disposable source copy, reimports it, verifies
changed pixels and unchanged material files/references, then verifies settings,
nodes, preview and pixels in a fresh process. The original Grey Rock source hash
is unchanged. Test setup and both processes fit the shared 60-second supervisor;
configuration/build time is separate. Full suites remain quarantined, not passed.
The final source passed independent ownership/atomicity review and formatting.

Both packages record dirty source snapshot
`fec74811911654171ae0cc25b6a998dbb2a3fa5f76ed4ea4beec60f989053d0f`
over engine revision `22966e126be426dd89c587363d654b6344dc727a`.
Native package fingerprint:
`5402aaef98329c6b6e812c50e36b9c044d147c4214f3cd69797f0a51da22aa29`;
native engine build ID:
`5e4766745e0c00b9022c0a3e0d90037cfc9d9a390e255e2d911ce5503a17dbb9`.
Web package fingerprint:
`14333bc9c14da8df423dbef155e6d5bdc5cae04e50fab5904c40e7d08fa19a3e`;
web engine build ID:
`b3c2e7ee959f6d4b5db6c2f352c0e1c6643ff737cd50e19a6b9e7046813aedcd`.
Final web artifact SHA-256 values:

```text
data  e0c1cc838f8d675d213913a38c348343c2e81442f810ab2aaac74bbe4fa41f73
js    5e0fba1318a78dbcd9f97471ab4fc265c94021e85fe3b751459e94f1148ac50d
wasm  843c403feb5f78ca22080365cef352015f805322f41e0bdae710b601d9338373
html  8cbae2b7b714f92cc333a9d56f12a93cfe100741415826d818c0fee2d2987b60
```

Evidence remains under `.chibi/architecture/`: `texture-acceptance.log`,
`studio-texture-import.json`, `external-studio-native-acceptance.json`,
`grey-rock-studio.png` and `texture-assets-browser.jpg`. The temporary browser
tab, server and source fixture are removed after verification; its disposable
workspace may remain in the loopback origin's IndexedDB. Candidate builds,
packages and captures remain for review. The candidates are uncommitted and
unmerged; original worktrees and Voidweaver visuals are unchanged.

Existing PNG/JPEG files without import metadata can be browsed, previewed and
dragged. Their settings and Reimport are unavailable until imported through the
workflow. Source relinking and texture rename/delete/reference navigation are
not implemented. Native POSIX import/reimport remains the supported publication
path; browser/Windows publication and 16-bit height import remain unsupported.
The 64 MiB visual-package cap and 1K default remain unchanged.

## Initial checkpoint identity

Work is isolated in `.worktrees/grey-rock-checkpoint/chibi` and the sibling
`chibi-studio`, on `codex/grey-rock-checkpoint-20261016`. The engine's indexed
dependency tree is `71d3d5832061d3e09edf53feb463ddde7113f7e8`, over revision
`22966e126be426dd89c587363d654b6344dc727a`. Checkpoint edits remain uncommitted;
the original working trees are preserved. Studio starts at `07dccf7`.

The installed native SDK has build ID
`7fc5e7fead820f3109dd1e2a95325fa70604724c330627b3807d8236bc27de9f`,
with source snapshot SHA-256
`42db66c5f095ef80963704dbff825b5d69f9cdd194cb47c59da6311292e8264c`.
Its manifest explicitly records a dirty source tree. Later documentation edits
are independent of this packaged source identity. Studio consumes that installed
SDK; it has no private engine source include paths.

## Implemented behavior

- Terrain preserves material coordinates through fracture, synchronizes surface
  settings and shader bindings to resident chunks, and preserves edited masks.
  Detached fragments retain their captured material state.
- Spotlight rays clip correctly across the near plane and canvas, include
  endpoint occlusion, and traverse long rays using image dimensions. Composite
  reuses the fullscreen image binding for spots so Cute's generic WebGL2 shader
  stays within 16 samplers, preserving its pass count and filtering.
- Shared image import prepares color, scalar or normal derivatives, measures
  prepared visual cost, and publishes only a matching reviewed plan. Publication
  rejects stale plans, existing destinations and unsafe paths.
- Studio's authored import form offers 1K, 2K and supported source dimensions.
  Image choices store virtual `content/...` keys and use document undo/redo and
  save. Importing a shared file is separate from undoing a material reference.

Four decoded 2048-square RGBA8 images alone require 64 MiB. The unchanged package
limit also includes prepared headers and other visual resources. Preflight
rejects that proposal; 1K is the default. Studio's live display measures resident
visual catalogs and states that scene resources and parameter records are
excluded. Full saved-project export remains authoritative. See
[the import API and budget contract](image-import.md).

## Focused verification

All test invocations use the existing production 60-second supervisor. Build and
configuration time are separate. Full native/render suites remain quarantined.

| Check | Result | Elapsed |
| --- | --- | --- |
| `--material-surface-mesh` | Nine CPU cases, including authored Height and seam normals | 1.772 s |
| `--material-mesh-cache` | Reset retires old geometry; rebuilt geometry is reused | 3.049 s |
| `--material-terrain-seams` | Bounded GPU seam coverage | 3.848 s |
| `--hrc-spot` | 26 GPU readbacks, repeated after WebGL sampler repair | 1.757 s |
| `--terrain-mesh-persistence` | Two fresh processes restore cuts, collision and mesh hashes | 2.408 s |
| `--terrain-storage-commands` | Shared save/load command checks | 2.148 s |
| `--image-import` | Seven CPU cases, including four-map budget and stored-DEFLATE regressions | 2.124 s |
| `--image-import-commands` | Three controller/filesystem cases | 4.132 s |
| `--image-import-grey-rock` | Untouched original 4K scan imports as 978×1024; source unchanged | 7.317 s |
| Language program dispatcher | 127 cases, including Studio project migration | 0.930 s |
| Installed SDK consumers | Public material and image APIs compile with their declared dependencies | Passed |
| `--studio-texture-import` | Final native import/history/save/reopen cycle, 32 frames | 5.118 s |

The original Grey Rock01 PNG exposed a pinned CutePNG stored-DEFLATE decoder
bug. A build-local dependency patch fixes stored-block input advancement,
alignment and bounds. Tiny independent malformed/valid stored-block fixtures
and the real scan verify the repair; this is not a general decoder security
audit. The shared dependency cache is unchanged.

The native `--studio-texture-import` selector passes 32 hidden frames across two
fresh host processes. Real SDL input drives review, publication, image choice,
undo, redo and save; only the operating-system chooser result is injected. The
second process verifies the saved virtual reference and already-resident pixel
hash against the saved derivative. Source bytes remain unchanged. The form and
final rendered texture were inspected from captured images. Local provenance and
images are in `.chibi/architecture/`, including `studio-texture-import.json`,
`grey-rock-studio.png.review.png` and `grey-rock-studio.png`.
Extraction bookkeeping was refreshed afterward for four changed authored UI
files; their tested content did not change.

The serial WebGL2 Studio build also packages and installs successfully. A bounded
1280×720 in-app browser check opens Assets and the texture form, checks popup
placement and styled fields, and confirms that the repaired build no longer logs
the texture-unit shader link error. This is UI/startup verification, not browser
import or complete browser rendering acceptance. An existing concurrent
`FS.syncfs` warning remains outside this checkpoint.

Final browser artifacts have these SHA-256 hashes:

```text
js    aa8a3bc93d28977eb242ae279bb618b8bbad0e46676e20fef92c9130c9339611
wasm  9a6ded4d9ca9fbe300dfd526b75f26359366c7f2e15d36b10e8cb04cda6cfcf1
data  fe845aab6186f51b4d3b9b1a3a59428b26ecc9d727c98932a4e67560c7a7ad86
```

The web SDK engine build ID is
`b8b7faee6156752f37755088fe6bbf43b534df4b6b12d8e6fc6f1c6931790b69`;
its package fingerprint is
`9fde9a0221b60fd879505621c9c119f9e638bbccb072469bff986057edae0298`.

Independent reviews covered import ownership/publication, decoder repair,
terrain/material lifetimes, spotlight composition and bounded UI admission.
No merge, push, deployment or full-suite acceptance occurred. The temporary
consumer build and formatting clone were removed; candidate builds, packages and
evidence remain for review. The browser tab and local server were closed. Its
disposable `starter-2` workspace may remain in that loopback origin's IndexedDB.

## Current limits

Filesystem import supports native POSIX hosts. Windows publication and the
browser file-picker adapter report explicit unsupported results. Browser build
success does not establish browser import support.

The importer accepts 8-bit PNG or JPEG and writes bounded RGBA8 PNG derivatives.
Grey Rock01's 16-bit height scan requires an explicit 8-bit control for this
checkpoint; no silent precision conversion occurs. This work does not establish
complete material authoring, 3D terrain editing or final game visual quality.
