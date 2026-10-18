# Full-selector validation proposal

Status: independently source-reviewed and conditionally admitted for one complete first measurement after exact-source integration, a successful separately admitted build and verified runner/configuration controls. Not executed. See independent-review.md (SHA-256 fca56408a1ecf8326aa5cdf6c1afe6f23fcdea7ae89241c63642eab9cc6bd86d). This is source accounting, not a timing claim.

## Exact command and unchanged contract

Use the repository's existing native compiler-enabled language-driver build. Build/configuration are separately timed and are not charged to, hidden inside or substituted for the test deadline. Execute the complete existing selector:

CHIBI_LANGUAGE_BUILD_DIR=<exact-reviewed-build> python3 -I -B tools/test_policy.py tool language-package-imports

One unchanged production supervisor deadline of 60 seconds covers the entire selector, including all cases and cleanup. Do not execute an ad hoc individual-case binary, split this selector, retry a timeout, increase its deadline or substitute a stubbed SLJIT. No Wasm or broader aggregate claim follows from this route. First actual measurement and required affected builds need independent admission.

## Full source accounting

The following fixed entries are counted over both native backends unless the existing case intentionally uses only bytecode. "Package visits" counts preparation-loop visits before reuse decisions, not executed analysis. Offered files are parsed once by the snapshot constructor even when their generation is later reused.

| Case | Preparations | Offered files/parses | Snapshot import declarations | Package visits before reuse | Direct authored calls |
| --- | ---: | ---: | ---: | ---: | ---: |
| Transitive calls | 2 | 10 | 6 | 10 | 4 |
| File-local aliases | 2 | 8 | 4 | 6 | 4 |
| Self imports | 2 | 4 | 2 | 2 | 2 |
| Name/blank-binding rejection | 10 | 30 | 16 | 20 | 0 |
| Owned record/union/constants | 2 | 6 | 2 | 4 | 6 |
| Stable IDs/order/relocation | 6 | 18 | 12 | 18 | 6 |
| Incremental invalidation/failure/recovery (extended) | 14 | 70 | 42 | 70 | 12 |
| Sibling membership/paths (new) | 10 | 44 | 20 | 40 | 10 |
| Constrained generic | 2 | 4 | 2 | 4 | 2 |
| Owned union defaults | 3 | 3 | 0 | 3 | 0 |
| Constant exported defaults | 6 | 12 | 6 | 12 | 2 |
| Malformed public exports | 0 | 0 | 0 | 0 | 0 |
| Total | 59 | 209 | 112 | 189 | 48 |

The malformed-public-exports case additionally performs one direct parse of a source with one import declaration and nine semantic-only analyses using that binding. Thus 112 counts snapshot import declarations; the entire selector also parses one direct-source import declaration. The owned-union-default case additionally makes one explicit catalog snapshot. Thus the entire selector has 210 source parses, not 209; the 209 table entries are the package snapshot parses.

Original admitted documentation describes 45 preparations, 145 offered files, 80 imports, at most 129 package visits and 34 direct calls. Delta: 14 preparations, 64 offered files, 32 imports, 60 package visits and 14 direct calls. The new fixture body is exactly 70 bytes. Every offered fixed source remains below 1 KiB, every snapshot has <=5 files, and <=4 package collection owners coexist. Up to one explicitly retained executable module also exists, using the existing retained variable and cleanup contract. Immutable dependency generations are retained through modules/owners as before.

For new sibling coverage per backend: five preparations with file counts 4,4,5,5,4 and four packages each; reuse counts are 0,4,1,1,1. It produces five direct calls. For the failure extension per backend: two extra five-file preparations (repeated invalid and repaired), both with one reused package; two extra direct calls. The earlier invalid edit now has additional failure-publication assertions but no extra preparation.

All 48 authored calls retain the existing instruction budget argument of 2,000. This budget is per activation; it is not an aggregate work cap across nested helpers. Finite fixtures and the one production deadline provide the overall bound. No processes, filesystem test activity, network, engine frames, GPU activity, service executions or sleeps are added. The full route's existing tests likewise have no filesystem/process/GPU work.

## Intended assertions

- Pure input enumeration permutation preserves all four generation pointers.
- Added, renamed and removed imported package sibling invalidate that package and its two dependent ancestors; unrelated generation is reused.
- Both native backends return expected fixed values with no inspection-bytecode fallback.
- Failed package/caller generations have diagnostics and publish neither executable module nor exports.
- Failed generations are not reused even when the failed request repeats exactly.
- A repaired request does not pull a stale valid generation through failed state.
- Retained valid and newly recovered executables remain callable after owner teardown.

No exact assertion-count claim is made before execution. It depends on the owning helper and package cases; report actual pico totals from the admitted exact-source run.

## Optional future mutation checks

Only after independent admission, one bounded complete selector per separate scratch mutation could establish regression sensitivity:
1. Remove prior valid-generation guard in reusable(): repeated invalid-edit generation identity/reuse assertions should fail.
2. Disable direct dependency pointer comparison in reusable(): changed-leaf/dependent generation assertions should fail.
3. Disable document path/key comparison while retaining text comparison: same-content sibling rename should fail the new closure-generation assertions.
4. Force reusable() false: reversed-source reuse assertion fails, guarding the performance-default reuse contract.

These are hypotheses from source review, not executed red evidence, and no mutation is included in the submitted patch. Do not reconstruct prohibited broader suites or repeatedly execute until success.

## Proposed integration documentation text

The language-package-imports entry should describe twelve fixed C cases, adding imported sibling membership/path changes and repeated invalid-generation nonpublication/recovery. Update totals to 59 preparations, 209 offered files, 112 snapshot import declarations, at most 189 package visits and 48 direct calls; retain the additional one parse/nine semantic-only analyses and one catalog snapshot, four-owner/five-file bounds, unchanged per-activation budget, no-files/process/GPU declaration and focused-route-outside-aggregate status. Coordinate with the shared docs owner.

