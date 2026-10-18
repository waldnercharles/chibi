# Compiler package-cache closure audit and bounded regression proposal

## Result

No stale-result production defect was established in the current in-process package-generation cache. The smallest useful increment is deterministic regression coverage in its existing C/pico owner, test/language/package_imports.c. The candidate is test-only: 129 added lines, one added case and an extension of the existing invalidation case. No production source, selector registry, deadline, default backend, observer or cache policy changes.

Exact active base verified through the GitHub connector on 2026-10-18:
- Branch: codex/toolchain-cleanup-20261017
- Commit: ace559c9c294a450c95a3a730dcd6d9f1b5ce13c
- Base package_imports.c Git blob: d240ca0b0ef302ece8536c8ad2a430daa927a04b
- Candidate Git blob: 5420a82cca5bea1311e89deb0f044b76f36e3a72
- Candidate SHA-256: d9cbdda7356d84eb244ea275462a0221596c0764b6420b6c7817f767599cbfc5
- Patch SHA-256: 1f9ef589275efe1c600fac889e7f03d78e5195370f205047c3233ffd87526d23

Exact-base AGENTS.md was read and retained; its Git blob is 8ada0dce2353a4954732f2ecd5126981aab27306. Its C/pico owner, unchanged 60-second complete-selector, independent admission, no held-code import and honest-coverage requirements govern this proposal.

The complete inspected-file identities are in source-manifest.json. All 32 base files were verified against connector-returned upstream Git blob identities. Working files remain in this one task directory.

## Ownership and prior work

The open PR scan found #443 native adapter, #444 semantic component fixtures, #445 compiler transport, #446 reader-state composition and #447 scalar lowering as distinct owned lanes. Their source and shared registration files are unchanged. #441, #439, held #407 and excluded #420 were also visible. None is imported or used as a dependency. main is not substituted for the active branch. Existing #391 introduced the generation-cache concept; the current package-specific implementation and tests already cover its direct/transitive invalidation model.

## Existing correctness evidence from source inspection

1. src/authoring/source_package_prepare.c:117-172 selects prior entries by package identity, requires a valid prior generation, then compares backend, nonzero binding generation and callback/user identity, retained native metadata, exact bounded default evidence, document/import/dependency counts, every document path/key/text and every import document/alias/target identity.
2. That same file:174-188 sorts and retains direct dependency generations by package identity. :257-261 collects every graph edge, including blank imports. A child change replaces its immutable generation; this changes its parent's comparison and propagates transitively. Reused generations retain dependencies independently of collection owners.
3. src/authoring/source_packages.c:119-142 canonicalizes sibling order by path; package preparation maps imports into this local document order. Input enumeration changes need not invalidate a generation, while a member's path/key/text change does.
4. src/authoring/source_package_snapshot.c owns exact source bytes, rejects NUL and invalid UTF-8, resolves explicit collection identities and constructs a complete graph before preparation. Missing targets, bad paths, duplicate identities and cycles cannot silently reuse an old generation.
5. src/authoring/source_catalog_snapshot.c compares native types, fields/layouts, relationships, capability signatures/effects/flow metadata and defaults. Unrepresentable or oversized default evidence disables reuse rather than accepting a truncated key. Existing language-incremental cases cover backend, callback-generation, catalog and default changes; package-core covers actual native-generation changes; package-imports already checks owned union defaults.
6. src/execution/prepared.c recursively fingerprints canonical imported executable serialization, including further imports; it rejects saved import identity/fingerprint mismatches. Prepared format version, pointer width and computed target ABI are validated. This is existing bounded compatibility fingerprinting, not a collision-free or adversarial cryptographic integrity guarantee.
7. src/toolchain/compiler_artifact.c sorts closed container rows by identity, rejects duplicates/missing root/trailing bytes and clears external import resolution. runtime/behaviors/package.c constructs the collection only after every record and module-set check succeeds; failure releases partial modules and does not charge the caller's remaining budget. The existing actual compiler-closure artifact fixture proves deterministic input-order serialization. Transport/previous-native-ABI additions remain owned by #445.

These are source-inspection conclusions; no new runtime pass is claimed. The package cache is process-local. It does not persist compiler versions across processes. test/language/compiler_fixture.c currently creates fresh package owners with previous=NULL, so this change does not claim an existing persistent cache of authored compiler outputs.

The public launcher has a distinct mtime/path-based project artifact freshness contract in src/host/chibi_main.c. This audit does not represent that M0 launcher path as content-addressed and does not redesign it as part of the M5 package-cache increment.

## Candidate behavior coverage

New package_imports_cached_closure_tracks_sibling_membership_and_paths:
- Builds app -> middle -> math plus an unrelated package on bytecode and real SLJIT.
- Reverses offered source order with a live previous owner; all four generations must be reused and output stays correct.
- Adds an otherwise unused math sibling: math, middle and app must be replaced; unrelated stays identical.
- Renames that sibling from math/extra.chibi to math/extra-renamed.chibi with identical contents and unchanged canonical member order: the same dependency closure must be replaced and unaffected package reused.
- Removes the sibling: closure replacement repeats, the removed function is absent, and current output remains correct after all previous owners retire.
- Uses one new 70-byte source and existing fixtures, <=5 files and <=4 live owners.

Extended existing invalidation case:
- Checks every failed dependency/caller has diagnostics and no published module or exports; unrelated generation alone is reused.
- Prepares the same invalid edit from the failed owner and proves invalid generations are not cache hits.
- Repairs from the failed owner and proves only the unrelated valid package is reused.
- Retires failed and successful old owners; both the recovered module and separately retained prior valid module still return the expected result.

## Static checks completed

- Verified all 32 inspected base Git blob identities, including exact test-source base.
- git apply --check --whitespace=error against the exact base: passed.
- clang-format 22.1.8 --dry-run --Werror: passed.
- Static case membership, includes, original limits and whitespace checks: passed.
- All original 11 cases remain; the existing owner now registers 12.
- No C compilation, Chibi preparation, runtime execution, hosted workflow, mutation run or publication occurred.
- Available local GCC does not make this partial connector snapshot a buildable product checkout. Missing complete dependency/generated-header setup is disclosed rather than replaced with fake product-validation evidence.

## Required review and next admitted check

Independent exact-source/workload review found no remaining blocker after correcting the path-only rename control. The review is saved in independent-review.md (SHA-256 fca56408a1ecf8326aa5cdf6c1afe6f23fcdea7ae89241c63642eab9cc6bd86d) and conditionally admits the first complete measurement after exact-source integration, a separately admitted successful build and verified runner/configuration controls. Then run the complete existing language-package-imports selector once under its unchanged production 60-second supervisor on a clean exact reviewed candidate. Keep configuration/build timing separate; use the existing native compiler-enabled language driver, assertions and appropriate sanitizer/real-SLJIT configuration. This route remains outside the aggregate; no subset, deadline increase, sanitizer waiver, fake backend or new test executable is proposed.

A red-before production fix is not claimed: there is no established production defect. Optional review-only mutation proposals are listed in validation-plan.md; they must receive their own complete workload admission before execution and must never enter the candidate.

Shared docs/testing.md should be updated at integration to the exact table below, preserving other owners' changes. This local patch deliberately does not claim ownership of that shared document or its current concurrent edits.

