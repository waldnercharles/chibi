# Future Studio main merge candidate

Status: source-only, not applied and not executed. All artifacts live in this directory. No commit object or ancestry merge was created.

## Identities

- Base: `305053fc6d8db4ba9a73c50f09f474c6fde1c1eb`
- Current staged tree: `a7ebaed0a999bd97a38b9f9676f02f824beac9ed` (HEAD `03269586af3c333b92a83fb6ea55a04373ed190f`)
- Eligible incoming main: `df050886dd7721db63b40fdf5435f1cb20ceeb34`
- Incoming tested tree: commit `a3639ed69dc67a79c225b0f10c961941b7624e39`, same tree as incoming
- Mechanical unresolved merge tree: `6353232dde526cb3ead9db0964b5e994f01c6450`
- Resolved candidate tree: `fcc7edb14bfd4855c151e2e832a42b0ca39ab06f`

## Resolution

All 21 conflicting paths have exact resolved files in `result/`. Twenty use the current staged typed file unchanged. The remaining conflict, `tests/acceptance/main.c`, retains the typed dispatcher and adds the incoming SDK include, early `--sdk-values` route and help entry. Unconflicted incoming changes introduce `sdk_values.h` and its README section. The final difference from the current staged tree is exactly those three acceptance paths. Every production file and EXTRACTION.json are byte-identical to the staged typed tree.

PR92 native ownership and arena changes are already present in the typed implementation. `semantic-differences.json` records token comparisons for all incoming-changed production paths; differences are include ordering, typed asset readers/source policy, stronger read guards/local arena lifetimes, and approved parameter/override/origin/Reset behavior. The staged shared value-document graph producer failure propagation and declared type-name restoration already include the last PR92 fixes. Retain typed NAME embedded-NUL rejection. No production import is needed.

The incoming tests complement the current model helper with direct command-producer/consumer and optional application/native catalog coverage. Their declarations are available in the current paired engine API. They do not replace the local protocol-envelope or strengthened history tests.

## Preservation and source audit

- All six staged local paths remain represented. Five are exact; the README receives only the incoming SDK section.
- Parent reports model261 PASS6.152s on SDKc676 (`focused-model-result.json`); all tested C/header blobs remain identical. No run by this agent. The incoming SDK selector is separate; its prior main validation does not validate this candidate.
- All 476 extraction entries match candidate contents: 258 consumer hashes and 218 source fallback hashes. EXTRACTION.json remains byte-identical because no extracted production file changes; main.c is consumer-owned and has no extraction entry.
- No retired extension, reader header/function, dependency or SOURCE_TEXT_KDL was reintroduced. The existing `new_asset_text` constructor name is unrelated to the retired reader.
- `git diff --check` from current tree to candidate is clear. This is a source whitespace check, not runtime validation.
- Active index still writes exactly the input staged tree. No active source/index edits, build, test, application launch, commit, merge or stash occurred.

## Handoff

`state.json` records each conflict's result blob and SHA256, input/result identities and extraction audit. `candidate.patch` is the complete current-staged-tree to candidate delta. `conflict-resolutions.patch` is the unresolved mechanical tree to resolved candidate delta. `base/`, `current/`, `incoming/`, `initial/` hold immutable Git inputs for review; `incoming-diffs/` records base-to-incoming per-file changes.

After the parent saves the tested local checkpoint and begins the ordinary ancestry merge, the resolved files for all 21 conflicts are available in `result/`; unchanged automatic merge results are represented by the result tree. Check that the local checkpoint tree still equals the recorded current staged tree before using these resolutions. If documentation or source changes first, refresh this candidate rather than overwriting them.

Remaining validation: independent source review, then the parent's selected native/SDK/model/full-cold/Web routes on the actual merged candidate after the heavy lane is released. The model pass is recorded above from the parent receipt; no merged-candidate runtime pass is claimed here.

## README evidence refresh

The previous candidate and its README/state/patches are retained in `historical-536ea9/`. Current input changed only in the acceptance README after the parent recorded model261 PASS. The refreshed result differs from historical result only in that README; the complete delta from refreshed input remains exactly three acceptance paths. Production/EXTRACTION and all 476 previously audited extraction contents are byte-identical.
