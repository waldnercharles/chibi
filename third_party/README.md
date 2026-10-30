# Third-party libraries

The engine downloads its third-party libraries as one prebuilt archive per
target and variant, published as assets of a GitHub release of this
repository. Nothing is fetched or compiled from upstream during a normal build.

- `archives.tsv` pins each archive by SHA-256: target, variant, release tag
  and hash. Assets are named `third-party-<target>-<variant>.tar.gz`.
- `tools/toolchain/Build.chibi` downloads the archives for its host with the
  GitHub CLI (`gh`), checks each hash and unpacks it into
  `.git/chibi/third_party/<sha256>` in the clone's Git directory, so every
  worktree of the clone links the same copy. It then removes archives that no
  worktree's `archives.tsv` pins. A checkout without Git keeps them in
  `.chibi/third_party/`. `cmake/third_party.cmake` loads the archive for the
  build's variant, and downloads it itself when CMake runs without
  `Build.chibi`. That fallback, like the rest of CMake, is temporary.
- Targets: `macos-arm64`, `linux-x64`, and `web`, `web-threads` and
  `web-player` for Emscripten. Windows waits for the build host.
- Variants: `release`, `address-undefined` (ASan and UBSan) and `thread`
  (ThreadSanitizer). Sanitized builds link the matching variant; every other
  build links `release`.

An archive holds static libraries with their headers and licences, and
`third-party.cmake`, which defines an imported `Chibi::vendor_<library>` target
for each:

- Cute Framework, with SDL3, PhysicsFS, its own Box2D and Box3D and, on Linux,
  s2n-tls; RmlUi, with FreeType.
- Box2D, Box3D, enkiTS (not in `web` or `web-player`), SLJIT (native only),
  and tree-sitter. The SDK exports them as
  `Chibi::box2d`, `Chibi::box3d`, `Chibi::task_scheduler`,
  `Chibi::execution_sljit` and `Chibi::syntax_runtime`.
  They keep the flags the SDK compiled them with: Box2D and Box3D without
  floating-point contraction and with SIMD only in `web-threads` among the web
  targets, enkiTS with three task priorities, SLJIT with its W^X allocator.

The SDK installs copies of the libraries it links. The archive also holds Cute's
math headers, image and Vorbis decoders and runtime shader compiler header under
`source/cute_framework/`, which the SDK compiles against directly.

## Building the archives

The archives are published and pinned in `archives.tsv`; nothing here builds them any more.
The CMake archive builder and its workflow are in `archive/third_party/CMakeLists.txt` and
`archive/.github/workflows/third-party.yml`. The patch directories stay here as the inputs
for rebuilding them from upstream (see `archive/README.md`).

## Patches

Cute Framework (`cute_framework/`; `docs/cute-framework-patches` explains each):
interior-border, imgui-render-target, eight-targets, web-no-asyncify,
gles-ubo-instance-scope, instance-buffer, web-webgpu-device,
gles-range-draw-state, sound-free-queue-music-volume (the free-source queue
grows by its byte size and the music volume applies once), audio-device
(`cf_audio_get_sdl_device`), sdlgpu-used-vertex-streams (SDL_GPU binds only
vertex streams with reflected attributes).

SDL (`sdl3/`): wayland-seat-pointer, a backport of SDL 79b40ad3; seats without
a `wl_pointer` get no cursor requests.

RmlUi (`rmlui/`) gains web behavior it lacks:
- color-mix: CSS `color-mix()`, so theme roles derive from a few tokens.
- radio-groups: HTML radio groups, which need a name.
- data-bindings: Vue bindings; `v-model` leaves an input being edited alone
  and empty style values unset.
- flex-text: anonymous flex items, so text inside a flex container shows.
- same-style: setting a style to the value it holds lays nothing out.
- data-if-handler: presence keeps a leaving element shown while its exit
  transitions run (`src/ui/motion.c`).
- variable-source: resolved `var()` values keep their authored source.
- suppressed-text-font-layout: text with locally managed content still lays
  out after font changes.
- variable-unit-invalidation: variables resolving to dp lengths recompute when
  the pixel ratio changes.
- box-shadow-blur: a blurred box-shadow blurs its whole shape before the box
  clips it.
- input-events: text fields send `input` on each edit and `change` once on
  commit; `data-value` follows input, `data-value-lazy` follows change.
- data-when: Vue's `v-if` builds its element while true and removes it while
  false, where `data-if` (`v-show`) only hides it.
- event-propagation: input and change propagation can be stopped
  (`src/ui/controls`).
- variable-line-height: a font-size or line-height set by `var()` follows its
  variable.
- move-keeps-data: an element moved under the same data model keeps its views
  bound.
- data-template: `data-template` keeps an element's content unbuilt for native
  code to mount while it shows.
- context-update-profile: optional `Context::Update` boundary timings.
- document-source: empty authored containers keep relative URLs without
  updating unrelated models.
- document-layout: fitted geometry refreshes without repeating per-frame
  callbacks.

Box3D (`box3d/`): block-alignment, the block allocator rounds element sizes up
to pointer alignment, since a freed element stores a pointer.

SLJIT (`sljit/`): arm64-immediate-negation, ARM64 negates immediates in
unsigned arithmetic, so negating the most negative value is defined.
