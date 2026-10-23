# WIP collection and shared-string handoff

Date: 2026-10-23. Repository: https://github.com/waldnercharles/chibi.
Owner chat: `01a160a6-14c8-7393-bae2-5de4ed95921a`.

This is a development checkpoint for dot's cloud implementation team. It is
not an accepted implementation, merge, release, or matching bootstrap claim.
The owner requested checkpointing existing work without finishing features or
starting builds/tests. No feature edits, builds or tests ran for this handoff.
Only Git snapshot construction, receipt packaging and integrity checks ran.

Local implementation and validation are held while cloud verifies the remote
checkpoint. I will make no further overlapping implementation changes after
handoff. Original branches, dirty working files, index stages and pending merge
state remain in place. No other owner's branch was changed.

## Active source

The active source is the exact reviewed tree
`97c1656f3c42a42ad3b349ccd7cb0768f940c1e8`, retained separately on
`codex/wip-collections-reviewed-20261023`. The main handoff branch adds only
this evidence directory to that source. Start with `docs/value-sequences.md`,
`docs/code-language.md`, and this packet's manifests.

The original development branch is `codex/value-array-cow-20261022` at
`329d8fb046612caa17f4aa99ee61f96512a2fa60`, with an existing pending pointer
merge head `f04d9d37822edb44a8fb1d1f6f68e5546eb8e049`. Its index was conflict
free but differed from its working files. The original index, both earlier
tested candidates, an intermediate correction and reviewed tree each have an
exact source commit in the handoff history. These are archival commits created
now, not claims that the implementation was committed or tested at those times.
See `snapshot-index.json` for every tree/commit/parent mapping.

Each archival commit has one parent. The existing pending merge heads are
recorded in manifests and retained on dedicated base refs; no merge was
performed to checkpoint them. The full changed-path lists and source blob
inventories are in `manifests/`. The active source changes 215 paths relative
to its original base, including its three formerly untracked source/doc files.

### Approved contract implemented in the active candidate

- Ordinary arrays/maps are independent values with copy-on-write backing;
  nested writes isolate ordinary values. Pointer elements retain target identity.
- Bracket ranges and `.slice(range)` copy outer ranges. Bounds are strict
  half-open byte/element ranges with boxed optional results; a valid empty range
  is distinct from failure. Existing start/count slice APIs retain clamping.
- Mutable array views require `p := &a; v := p.view(range)`. They retain the
  pointer and fixed indices, follow the current array at that pointer, and
  check the whole window against current bounds. Ordinary `a.view` is rejected.
- Strings are immutable arbitrary bytes. String views retain immutable backing;
  clones and copied ranges detach byte storage.
- Import, value copy, borrow, transfer, mutation and historical capture have
  separate runtime operations. Caller-owned managed heaps must survive results.

## ABI and dependencies

The active collection source uses prepared ABI22. Its bundled portable compiler
artifact is the pointer prerequisite's ABI21 artifact, bootstrap version6,
minimum2. It is preserved intentionally and is not usable as the final matching
ABI22 bootstrap. Do not confuse bootstrap manifest version with prepared ABI.
No bootstrap was regenerated for this handoff.

This collection ABI22 draft is incompatible with the separate query ABI22 draft;
equal version numbers do not prove compatible layouts or wire schemas. Newer
main, pointer/task/Builder and query work were not imported here. The existing
pointer prerequisite is fully retained on
`codex/wip-collections-pointer-base-20261023`; newer pointer/task checkpoints
must be obtained through their separate handoff. The retired shared-object
semantics are not the active array contract.

Common integration still needs review of the actual pointer/root/path layout,
one frozen `pin_record` carrier rather than a competing query `pin_schema`,
one arena cleanup/accounting protocol, original-incarnation ECS handles and
task cancellation, context layout and target fingerprints. Preserve owned
arguments/results, value copy/drop, managed tracing and all lifetime guards.
Host-call `owned_arguments` is serialized after result count and before outcome
count; invocation `owned_arguments` follows its task flag. Source interfaces
must be reconciled before generating one final artifact.

Sequence operations use IDs51-61 with54 reserved;62-79 remain unallocated;
80-95 are reserved for queries (80-88 assigned in that draft). No new ID was
allocated during handoff. Broader pointer/Builder/ref retirement remains with
its own candidate.

No ignored authored source was found. Build directories, `.chibi` caches,
`compile_commands.json`, the component-ID lock, raw Git indices and machine
binaries are excluded. Existing tracked portable compiler artifacts remain.
Portable manifests preserve index entries and conflict blobs, not host index
binary formats. Receipts retain original absolute paths as provenance;
`receipt-map.json` maps them to files in this packet. Historical source-build
helpers require adaptation to new paths/toolchains and are not runnable cloud
validation commands as supplied. Their executable was already removed.

## Tests and reviews

All counts below belong to the named exact source tree. They do not establish
that reviewed97c or a combined pointer/query tree passes execution.

| Source tree | Evidence |
| --- | --- |
| `cb5cbcfe904e40d89cc94f9351a2107f90556fa0` | Fresh stage-zero native build completed. `types`:26 passed,1 failed,4845 assertions,0.828s. All11 new runtime cases passed; a wrongly changed retained-byte fixture assertion failed. Remaining selectors unrun. |
| `0dff9103d948502256244a97fdaa3f77b8817d66` | Intermediate fixture correction; no execution receipt claimed. |
| `f90c191e8be1305f4ea05344e7f01fbf155889c8` | `types`:27 passed,0 failed,4861 assertions,0.550s. `record-runtime`:6 passed,2 failed,834 assertions,0.659s; all3 resolver cases passed. Two older harnesses omitted a caller-owned heap. |
| `97c1656f3c42a42ad3b349ccd7cb0768f940c1e8` | Harness/cost corrections are source reviewed; format22 and whitespace checks passed. No build/test execution on this tree; syntax was not rerun after that correction. |

The fresh cb5 native build used Release, sanitizers OFF and two jobs. The f90c
tools driver was rebuilt before its types run; the unchanged language driver
was from the earlier build. Binary hashes and logs are retained, binaries are not.
Earlier source-only compiler preparation accepted42 files in30 authored
packages in7.793s including helper linking. Its file-hash receipt is preserved;
it did not run the compiler, reproduce a bootstrap or establish97c coverage.

Native semantic/execution groups, corrected record-runtime rerun, authored
collections/sharing and bootstrap generation/reproduction remain unrun.
No broad engine/render/Studio, deployment or full combined-source readiness
claim is made. The prior failures remain in the packet; source fixes are not
substitutes for reruns.

Independent local reviews covered the resolver increment and final five-file
harness/cost delta. The pointer owner and overall coordinator also reviewed
the latter with no blocker. Exact reviewed patch:
`native-harness-and-union-cost.patch`, SHA256
`5efb000487d9fc09d92016efb4db7a5bbb54b0fe5a43c5449119b717b0741a0e`.
The resolver patch SHA256 is
`b64d7ebb95cbdb485a2daf4d11b5161c8213d4dd8cd507405a01f6aaceec9a28`.
Review details and limits are in `reviews.md`.

## Separate recovery drafts

`codex/wip-shared-array-recovery-20261023` preserves the superseded shared-array
object model, with its own base, pending pointer merge, original index and live
tree `77d42a44b0583cfcf45a03c37f4c38477071e935`. It is recovery-only, has source
ABI22 plus the old ABI21 artifact, and must not be overlaid on97c. The archive
named `cow-candidate-source.tar` actually matches this older source tree;
the verified Git recovery snapshot replaces any need to distribute that archive.
Its historical default-sanitizer build was not a final-source or runtime test;
no matching completed tests are claimed.

`codex/wip-string-slices-recovery-20261023` preserves the earlier immutable-string
prototype. Base `8b62b1cce22ee2f339b01eeecec4c952e769294d`, pending merge head
`17aa9ad383fddb7eeb223e2f1ee3445d0648670c`, raw working tree
`3c2ae5c66d3310477810817e96a18968ffb96cf6`. Its11 unresolved paths and all33
conflict-stage entries are recorded in its manifest and retained as three
index-stage commits in its separate history. Its source says ABI20; no matching
artifact or runtime-validation claim is made. The pending parent is retained on
`codex/wip-string-slices-merge-base-20261023`. Do not resolve or combine this
draft merely to recover files.

Patches under `receipts/` and `original-state/` are unapplied recovery/evidence
material, not additional active source. Some span old contracts or pending
merges. Read their corresponding manifests rather than applying them wholesale.
AppleDouble metadata and redundant source tar archives are excluded.

## Cloud next steps and holds

1. Verify the pushed refs, commit parents, source trees and receipt hashes.
   Select reviewed97c as the collection source; keep both older drafts separate.
2. Reconcile the collection, pointer/task and query candidates on one reviewed
   common source. Preserve all failures, lifetime/ownership checks and existing
   PR/merge/release/deployment holds. No checks or protected settings were waived.
3. Build the matching native drivers, then run record-runtime, semantic and
   execution through the production dispatcher. Investigate substantive
   failures; preserve exact source/binary/log receipts.
4. Run authored collection and sharing parity and any other affected gates.
   After common source and ABI settle, regenerate and reproduce one matching
   compiler bootstrap. Complete the repository's required validation before
   proposing landing. Cloud owns subsequent implementation and validation.

This checkpoint changes no product decision and grants no merge/release
permission. Combined-source compatibility, forced-GC coverage for the final
harness delta, final bootstrap and remaining validation are still open.
