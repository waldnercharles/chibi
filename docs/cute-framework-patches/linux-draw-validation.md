# Linux validation of Cute empty-uniform drawing

The focused workflow uses Ubuntu 24.04, Clang18 ASan+UBSan, real glibc,
Xvfb and Mesa lavapipe. The assertions remain in the existing C/pico
`--draw-uniforms` case: one hidden 32x32 app, two layered circles and pixel
readback, with the existing readback wait and five assertions.

This workflow/runner is source preparation on renderer tree
`3c8deefa3063f6294db7121dfda6e8bcb6b0e52e`, based on ordinary main
`d0de4a84ecf50badcab661411470bdf6f3da5fa2`. The complete successor tree needs
independent review and exact-candidate checks. No Linux execution is claimed
by adding these files.

## Source and immutable inputs

A same-repository pull request touching the focused source paths triggers the
workflow. It checks out the exact event PR head, not the synthetic merge commit,
and records the actual tree and clean status. This permits the first reviewed
source-branch proof before the new workflow lands. GitHub only enables manual
workflow_dispatch once the workflow exists on the default branch. Later manual
runs require the exact expected source SHA and tree; source SHA must also equal
the dispatched github.sha. Fork PRs and pull_request_target are not used.

The job has read-only contents permissions and does not publish releases,
change pins, create PRs or dispatch other workflows. It installs the Linux
toolchain and repository-hashed CMake/Ninja tools, then runs:

```sh
python3 tools/validate_cute_zero_uniforms_linux.py \
  --source-sha "$SOURCE_SHA" --source-tree "$SOURCE_TREE" \
  --evidence "$EVIDENCE" --work "$WORK"
```

The job has a 60-minute hosted runner resource budget. It does not establish a
universal test deadline, alter the C case's readback wait, or add an inner timeout.

The runner downloads only the pinned linux-x64/address-undefined baseline
archive and checks its SHA256 before extracting it. It copies that tree into a
private cache. It configures exact-pinned dependency sources through the normal
third-party CMake path, builds only Cute, and replaces only the private Cute
library. Other baseline files must remain byte-identical. The digest-shaped
private directory becomes a lookup key after replacement; its contents are
not claimed to equal the published baseline digest.

On d0, the dependency baseline is third-party-2026-10-22.2; it does not contain
this guard. The separately pinned compiler-runtime .2 is an ABI20 compiler-host
product and does not supply the renderer fix. Matching rebuilt dependency
archives and atomic source/pin landing remain required for production.

## Identical red and green draws

The dependency configuration applies the reviewed patch list. The runner saves
the guarded Cute source, reverses only draw-zero-uniforms.patch for red, then
restores that exact file for green. Both use identical compile commands,
instrumentation, C test object, other libraries, selector and environment.
No forced declaration/header, test-only nonnull annotation or alternate backend
is used. The actual Cute object and archive must contain the UBSan nonnull
handler, and the actual driver link must name the private instrumented archive.
The private archive digest must equal the newly built Cute archive; the guard
must change the actual Cute object/archive and relinked driver bytes. The C test
retains its -UNDEBUG flag and exactly the same object bytes in both phases.

Both SDL video-driver environment aliases are x11; SDL_GPU_DRIVER is vulkan,
with exactly one lavapipe ICD. Discovery output and requested environment are
retained separately from any backend/device name the renderer itself emits.
An absent renderer device report remains unknown. There is no backend fallback.

The process chain is the existing test_policy native supervisor, xvfb-run, a
narrow Python recorder inside Xvfb, then the actual test_tools binary. The
recorder captures the binary PID, inherited process group, raw return code and
normalized exit before propagating it. Missing/incomplete recorder output
leaves the native result unknown, including Xvfb setup failure. Outer status
is never substituted for the native result.

The runner retains full supervisor output/exit separately. Cleanup success
requires status agreement with the native recorder, no cleanup failure or
traceback, and an empty recorded supervised process group after supervisor
exit. That inspection has process-group scope; test_policy owns Linux subreaper
cleanup of detached descendants. A cleanup failure is a job failure even when
the primary sanitizer report was expected.

Red requires the real null-argument UBSan diagnostic, s_process_command stack,
the exact unguarded memcmp source line and the C draw case. A literal memcmp
stack frame is not required because it may be an inline builtin. Green requires
native and supervisor exit0, clean cleanup, no sanitizer report and exactly:

```text
Summary: Passed: 1 Failed: 0 Total: 1 Suites: 0 Asserts: 5
```

## Evidence and remaining requirements

Artifacts are uploaded even on failure. They contain frozen source identity,
baseline tag/hash and file inventory, actual tool/system/glibc headers,
authoritative dependency CMake pins/patch order, guard patch and sorted patch
file hashes, preprocessed Cute source, exact source delta, actual compile/link commands,
object symbols/bytes, object/library/test/binary hashes, unchanged test object
and other dependency checks, rendering environment/discovery, command return
codes and full red/green logs with separate native/supervisor/cleanup outcomes.
No credentials or complete environment dump is recorded.

Static source parsing and workflow lint do not establish a Linux pass. The
five required local groups, actual Graph/CMake parity, ordinary current-source
guarded drawing, matching immutable archives, final compiler/typed integration
and clay visual acceptance remain outstanding. A successful focused software
draw would establish this regression only; it would not clear those holds or
authorize source/pin publication or merge.
