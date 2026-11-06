# Draw commands without uniform payloads

Keep this patch. At the pinned Cute revision, command batching compares uniform
payloads with `memcmp` even when both commands have zero bytes and null data
pointers. Where the libc declaration marks those pointer arguments nonnull,
as on Linux/glibc, UBSan can reject that call during an ordinary layered draw.

The patch skips only the byte comparison when the already-equal payload sizes
are zero. Commands with payloads still compare every byte. The remaining uniform
metadata and draw-state comparisons keep their existing behavior.

`--draw-uniforms` in the render-enabled C tooling driver draws two colored
circles in separate layers of a hidden 32 x 32 app. This reaches adjacent empty
uniform commands through Cute's real batching path, then reads the canvas and
checks both circles and the transparent background. Run the unpatched and
patched dependency with ASan and UBSan, including the dependency itself. For
the null-argument regression, verify that the actual libc declaration supplies
the nonnull contract and that the Cute draw object contains its UBSan handler.
An uninstrumented dependency or a declaration without that contract cannot
establish that result. Retain full native output, the native result, and the
supervisor's exit and cleanup result separately; a nonzero supervisor exit
alone does not establish the intended sanitizer failure.

The examined Darwin SDK declaration does not mark `memcmp` arguments nonnull.
Ordinary macOS ASan and UBSan therefore do not instrument this particular
contract check. A scratch diagnostic added an identical C ABI declaration with
`nonnull(1, 2)` only when compiling `cute_draw.cpp`. That diagnostic is separate
from the production configuration and does not replace Linux/glibc coverage.

The 2026-10-22 local checks used engine base
`1bc97b7305ee91102d3ee035e4852aa5341df31c` with repair tree
`619e6552f79cb64abf23aa60e11e72bf60606e51`:

- Ordinary guarded macOS ASan and UBSan passed one case and five assertions.
  The native process and supervisor exited successfully with no cleanup error.
  No forced declaration was used, and the Cute object had no nonnull handler.
- The scratch nonnull diagnostic reached the unguarded `s_process_command`
  `memcmp` at `cute_draw.cpp:5875` and reported the intended null argument.
  Supervisor cleanup then failed with `EPERM` and exit 2; the native exit was
  not retained. No owned process remained. This is diagnostic reproduction,
  not successful supervised acceptance.
- With only the guard changed, the same diagnostic test object, compiler
  options, forced declaration and other vendor archives passed one case and
  five assertions. The native process and supervisor exited successfully with
  no cleanup error.

These results belong to that earlier source tree. The integration on
`d0de4a84ecf50badcab661411470bdf6f3da5fa2` has not yet been built or tested.
Real Linux/glibc unpatched and patched checks remain pending.

The normal engine build consumes a prebuilt dependency archive. Changing the
patch list does not change an existing archive; publication requires matching
archives from the dependency owner and a fresh exact-candidate check.
