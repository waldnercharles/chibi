# WIP native shader foundation handoff — 2026-10-23

Repository: https://github.com/waldnercharles/chibi.git
Branch: `codex/chibi-shader-foundation-d0-handoff-20261023`
Source commit: `1c1599fa1ca2405e0ee1d6a57dc7a9146db3b252`
Exact source tree: `e4efa14d334f7dc5b6d465bf1906f77120992084`
Preserved pending merge parents: `d34def7202f842b6408f151df96ab5c7d1b343b1` and `d0de4a84ecf50badcab661411470bdf6f3da5fa2`.
This records the existing pending merge; it does not merge or publish main.
The original dirty branch/index/MERGE_HEAD remain unchanged locally.

## Scope and status

The published PR663 branch remains at `d34def7202f842b6408f151df96ab5c7d1b343b1` and is reused as an ancestor,
not duplicated. This checkpoint preserves its exact d0 integration plus handoff
records. `source-manifest.json` lists the17-path delta versus d0 with Git blobs
and SHA256. The seven shader implementation/test blobs remain identical to d34.
The evidence commit adds only this handoff directory; source_commit above is
the exact candidate without handoff files.

Active source supplies native owned CPU snapshots over supplied existing IR,
source-map/call-chain/principal-node lookup, exact ordered document guards,
nonfinite labels, transactional failure preservation and the sampled-SDF
failure diagnostic repair. It does not compile authored shaders or render the
proposed material. The public API still requires its caller to pair a source
map with the exact compiled generation; results are one scalar/vector node,
and document inputs are C strings.

Fresh source review found no integration defect in SDK/CMake/Graph registration,
selector guards/routes, dependencies or updated volume callers. This is not
current-candidate test/merge approval. PR663 stays draft. Historical d34 focused
and five required groups passed; fa1 tooling and runner rechecks failed.
Current d0 checks remain UNRUN. See test-status.json and exact raw receipts.
No tests, builds, formatting, generation or default selection ran for handoff.

## Records and recovery

- `receipts/`: original ignored text logs/manifests/review receipts. Original
  machine paths and binary hashes are historical evidence, not portable runtime
  dependencies or measured process-RSS claims. No binaries/build outputs included.
- `original-state.json`: original dirty merge state and path inventory.
- `recovery-fa1/`: separate superseded unapplied patches/plan/state with its own
  base and tree. Keep it recovery-only; never compose it into current source.
- `manifest.json`: SHA256/size for every handoff file except itself.

Authored Facts/SourceMap/current contracts are NOT active in this branch. They
are on `codex/chibi-shader-authored-handoff-20261023`, based on the same exact native source commit.

## Cloud continuation

Verify remote branch/source/tree and manifests first. Review the exact native
candidate and run focused --shader-source-debug, applicable SDK builds and the
required native/materials/projects/language/tools/format gates. If typed assets
or other main work lands first, adapt and re-review the resulting delta, then
bind checks to that new source. Do not reuse d34/fa1 results as d0 proof.
Do not infer authored-policy/material/GPU/Studio acceptance from C tests.

No merge, release, default-selection, held-ABI composition or denial is cleared.
Local owner will make no further overlapping implementation changes after this
handoff and will hold the original candidate while cloud confirmation is pending.
