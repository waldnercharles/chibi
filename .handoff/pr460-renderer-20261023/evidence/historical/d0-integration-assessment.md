# Renderer repair on current main d0: source-only assessment

The reviewed renderer repair combines cleanly with
`d0de4a84ecf50badcab661411470bdf6f3da5fa2` in a scratch-only Git index. There
are no source conflicts or new functional changes in this preparation. No
build, test, publication validation helper, dispatch or publication was run.
This does not establish exact-candidate validation or eligibility.

| Source identity | Exact value |
| --- | --- |
| Live worktree HEAD, unchanged | `fa1b7b2bb297eacbbed6aec6f03c4260355db97c` |
| Live staged renderer tree, unchanged | `adabdc085fa2d78f7f7a9977e0adeec1a353853f` |
| Live staged diff SHA256, unchanged | `10f7d3477f4ddbadd01abc63a49ad192f7e1a2a245ba12759e1091efbb495281` |
| Proposed ordinary-main base | `d0de4a84ecf50badcab661411470bdf6f3da5fa2` |
| d0 base tree | `3d0b4c546201728d27ceffcf602215bd9f9a0f98` |
| d0 plus the frozen adab repair, before documentation label update | `92b30ef6b685b018d4e129dd44fcfec6af47e585` |
| Prospective source tree, with current-base documentation label | `3c8deefa3063f6294db7121dfda6e8bcb6b0e52e` |
| Prospective patch SHA256 | `dd934f4efe304047fa18049f524c2cf60850752cc1019e46d712f2bcada3549e` |

The prospective tree has no commit or branch. `d0-repair-source.diff` is its
reviewable patch against d0. It was prepared with `GIT_INDEX_FILE` pointing to
`d0-assessment.index`, never the live index. The patch note's base label changes
from fa1 to d0 and still says that integration has not been built or tested.
Every other added/deleted patch line matches the independently reviewed adab
repair. The real draw test and one-condition Cute patch retain their exact
SHA256s `0185276a320e96f3b89143280bef0944fd3f19d67624092076d032f0815bd92d`
and `ebaedd6bb02f5daaa481062a6e6e663f40e0fcee562975afeecb55caabfcf7d2`.
The live index bytes, status, HEAD and stash reference were checked unchanged.

## Ordinary main ancestry and composition

fa1 is an ancestor of d0. d0 is the ordinary GitHub merge for PR653, with
parents `5c28191b3ea53afa232c34e37b14fd9b01024aef` and
`8f9dbf5e8edb68269c820bc99baf84bf18bc9c17`. Its ancestry includes the baseline
archive-pin commit `3c9db9b8b8c9103d35c8f82bfc21f2bf5b113e98`, compiler runtime
repair/source `5c28191b3ea53afa232c34e37b14fd9b01024aef`, Graph SDK source
`8c5a6e6f0c5437f8f0a06d25c2e311fa1031f977` and Linux OpenGL role correction.
These already-landed sources remain intact in the prospective tree.

The prospective diff contains exactly the same 13 renderer-owned paths:
patch/docs, C draw fixture, existing CMake/Graph source inventory, existing
dispatcher, GPU selector and policy routes. No workflow, pin, SDK dependency
implementation, compiler resource or other owner source is changed. The
original PR460 head aa7 and typed migration310 are not d0 ancestors. No clay
fixture, clay_fields.c or clay_lab.c exists in the prospective tree. The typed
clay draft remains separate, unapplied scratch content.

For a future ordinary source commit, d0 must be its actual parent or ancestor,
with only reviewed eligible source changes added. A tree object and a dirty
fa1 worktree do not supply that commit or the publisher's source binding. If
main moves before the future release, rebase/review the combination and freeze
a new binding rather than treating this d0 assessment as current proof.

## SDK and Graph interactions

Six owned files also changed between fa1 and d0: docs/testing.md,
third_party/README.md, test/tools/CMakeLists.txt, test/tools/main.c,
tools/graph/engine/Targets.chibi and tools/test_policy.py. Their merges are
clean. The renderer addition preserves d0's --third-party-publish,
--build-process-files and --build-sdk paths, publication recovery docs and
Graph SDK inventory entries. The draw fixture is still owned by
chibi_render_consumer in both inventories; no extra library or executable is
introduced. Actual Graph/CMake command parity remains unexecuted.

d0's sdk/vendor_dependencies.cmake classifies both discovered Linux OpenGL
paths and imported targets as system roles. The repair does not override that
logic or resurrect the old source-vendor warning wrapper. A new dependency
archive and installed SDK still need to retain its corrected link interface.

The Graph explicit profile and compiler-tools package preserve the supported
--third-party override. Compiler-tools packaging checks exact archive paths in
the private host link records; a different private prefix cannot be labelled
as the baseline archive. The d0 Graph packaging document states that Linux
test-driver compilation/linking uses the CMake harness at this checkpoint.
The scratch Linux drawing recipe already configures and builds test_tools with
CMake, which matches that boundary. Keep it on that route; do not infer Linux
Graph driver support from Release Graph host packaging. No command in the
recipe has been executed for the prospective d0 tree.

## Distinct pinned products and renderer proof

d0 pins all nine dependency target/variant rows to
`third-party-2026-10-22.2`. The local tag resolves to
`1bc97b7305ee91102d3ee035e4852aa5341df31c`, whose Cute patch list has no
draw-zero-uniforms patch. d0's own patch list also has no guard. The coordinator
has audited the baseline archives; the provided conclusion is that .2 does
not contain this fix. This source assessment did not download or re-audit
release bytes. A new source patch list does not change those prebuilt archives.

`compiler-runtime-2026-10-22.2` is a separate compiler-host product. Its manifest
names source5c28191 and tree94ebca0, and the execution schema is ABI20. That
runtime suffix does not mean the Cute repair exists in dependency .2. No
compiler runtime or ABI update is proposed here.

The genuine Linux/glibc controlled red/green drawing leg remains required:
instrument Cute and the same C test, use the real libc nonnull declaration,
retain object instrumentation and exact source/library/driver hashes, and
keep full native output and native/supervisor/cleanup outcomes separate.
The scratch-only Darwin annotation diagnostic is not Linux coverage. Generic
native, Studio two-frame or Graph packaging CI does not execute the new
--draw-uniforms regression. An ordinary current-candidate guarded draw and
matching rebuilt archive/pin evidence also remain outstanding.

## Current prepared-source publication route

The merged third-party workflow and tools/publish_third_party.py now support
atomic source-plus-pins publication. The supported future preparation is:

1. Freeze a tested, independently reviewed same-repository source commit ahead
   of exact current main. Its source PR must be open, non-draft, uniquely bound
   to that branch/head/main and eligible under all actual holds/checks. The
   original held draft PR460 does not meet that condition merely because it
   contains related assets. The repair needs an eligible source composition.
2. The publication owner selects a fresh release tag and dispatches the source
   branch with paired source_pr and main_sha inputs. Workflow GITHUB_SHA,
   remote branch, PR source/base and actual main must continue to match. No
   branch, PR or main drift can reuse the old binding/rebuilt release identity.
3. The publisher builds the full native/web archive matrix from that source.
   It rechecks bindings, verifies original release/artifact digests, and creates
   one archives.tsv-only child commit on the frozen source, then a draft
   integration PR containing source and pins together. It leaves the source
   PR unchanged. It refuses unexpected pin branches/PRs, closed PR reuse and
   forced branch replacement. Original-job retry uses original artifacts;
   rebuilding different bytes requires a new tag.
4. Independent exact-candidate validation and every explicit hold remain in
   force before landing the source/pin combination. Valid pin-row syntax from
   tools/check_publish_pins.sh is a prerequisite check, not proof that archive
   bytes contain the guard or that rendering passed.

No step above has been authorized for execution by this source-only task.
The publisher's --validate-only mode was not run either.

## Risks still to resolve

- Current d0 evidence cannot be inherited from old base1bc/tree619 macOS runs.
  The prospective source tree has had no build, format, group or drawing run.
  Coordinator-scheduled exact-candidate checks, all five required groups and
  actual Graph/CMake parity remain pending.
- Baseline dependency .2 is unguarded. Ordinary linking to it would leave the
  production defect present despite the checked-in patch. Private instrumented
  overrides need truthful private provenance; publication needs freshly rebuilt
  matching immutable archives and pins, landed atomically with the source.
- d0 does not integrate typed migration310 or clay source. The complete draft
  needs the final compiler/typed/material checkpoint, reviewed C test/build
  adaptation, actual preparation and clay image acceptance. It is not part of
  this proposed repair tree.
- The prepared-source route binds live main and a real eligible PR/commit.
  This assessment is a scratch tree on a frozen historical d0; it does not
  satisfy those live bindings, clear the original renderer hold or authorize
  publication/landing.

`d0-repair-snapshot.json` retains source/file hashes, ancestry checks, patch
comparison and unchanged live-candidate receipts. `d0-reference/` contains the
exact inspected d0 sources. All scratch remains under the existing PR460 task
directory; the primary checkout and other owners' files are untouched.
