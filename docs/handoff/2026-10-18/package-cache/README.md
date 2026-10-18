# Package-cache handoff checkpoint

This is the owner's requested 2026-10-18 checkpoint of existing work, published
as a draft for recovery and handoff. It is not a tested integration or approval
to execute, arm the hosted control, or merge. No new implementation, build,
product test, mutation run, or hosted run was performed for this checkpoint.

## Exact source and scope

- Repository: private `waldnercharles/chibi`.
- Target: `codex/toolchain-cleanup-20261017` at
  `c828177eb9d5cd8df52b5ec8f73516a9ea4cb5fb`, tree
  `09d520fe2a5723226126b0879faaee390e87b7b9`.
- Handoff branch: `codex/m5-package-cache-closure-20261018`.
- The target still contains the reviewed test baseline, Git blob
  `d240ca0b0ef302ece8536c8ad2a430daa927a04b`.
- `test/language/package_imports.c` is the exact reviewed candidate, Git blob
  `5420a82cca5bea1311e89deb0f044b76f36e3a72`, SHA-256
  `d9cbdda7356d84eb244ea275462a0221596c0764b6420b6c7817f767599cbfc5`.
- `.github/scripts/package-cache-validation.sh` is the exact reviewed private
  helper, Git blob `22974029824c5cf1d4548bf8402535964b9fa3c4`, SHA-256
  `5cb53825c0e8e525a2ad0bea8f0578fac5533bfde3cfc8484d7db926a1e0be74`.

The test adds sibling add/rename/remove closure invalidation and extends
invalid-generation nonpublication, repeated-failure, repair and retained-module
lifetime coverage. The test-only diff is 129 additions, one added case and an
extension of one existing case. No stale-result production defect was established.
Production cache code, selector registration, production supervisor and limits
remain unchanged. No project asset contract changes or migration are needed.

The public control is preserved only as `unarmed-public-control.yml` in this
documentation directory. It is not a live workflow. Its invalid candidate pins
and `PACKAGE_CACHE_REVIEWED: 'false'` are intentionally unchanged. No Studio
repository, PR81, workflow, saved environment, or desktop was modified or used.
The current private workflow inventory has main-only push/PR triggers relevant
to this publication; the draft targets the cleanup branch, not main.

## Evidence and limits

The frozen source review found no remaining source blocker and conditionally
admitted a first complete measurement only after exact integration, successful
separately admitted build and verified runner/configuration controls. The route
review approved the frozen design conditionally on final combined-tree and
released exact public-control review. Neither review is a runtime pass or an
approval of this final checkpoint as a tested integration.

Previously recorded evidence: exact source/blob reconciliation, patch and format
checks; 147 synthetic helper checks and 77 static workflow/source checks; and
19 independent route/inventory checks. These historical checks are not C/pico,
configuration, compilation, instrumentation, or runtime evidence. They were not
rerun for this checkpoint. Packaging verifies identities and remote publication
only. Actual assertion totals and runtime remain unmeasured.

The complete selector remains 12 cases, 59 preparations, 209 snapshot parses
plus one direct parse (210 total), 112 snapshot import declarations plus one
direct-source import (113 total), 189 preparation-loop package visits before
reuse and 48 direct authored calls. Also included: nine semantic-only analyses
and one explicit catalog snapshot. Bounds are at most four collection owners,
five files per snapshot, each fixed source below 1 KiB, one additionally retained
module handle and a 70-byte new fixed source. The original 2,000 instruction
allowance is per activation, not an aggregate nested-call cap. One unchanged
production 60-second deadline covers the complete selector and cleanup;
configuration and build are separately timed. The selector remains outside the
aggregate. No filesystem, child-process, network, GPU, service or sleep test
activity is added.

## Outstanding work before validation or landing

1. Refresh source target, related work and explicit holds. Preserve PR407's hold
   and exclude PR420 and their code/dependencies. This checkpoint imports neither
   lane and clears no hold.
2. Update `docs/testing.md` and the comment-only resource accounting in
   `tools/test_policy.py` to the complete totals above, preserving other owners'
   work. These updates were still pending and are deliberately not claimed done.
3. Independently review the resulting complete source/helper/docs/supervisor-
   comment tree and build dependencies. The production cache and supervisor
   executable functions must remain unchanged for this scoped regression.
4. Obtain coordinator release of the existing Studio PR81 control. Refresh its
   actual parent/base, complete workflow inventory and complete PR delta. Preserve
   ordinary-CI exclusion and publish-path isolation. Review final source/control
   SHA/tree pins and arming separately; this archive grants no control lease.
5. Separately admit configuration/build/verification and one complete first
   `language-package-imports` measurement with Debug Clang 18, assertions,
   ASan/UBSan and real SLJIT. Use the normal standalone `test_language` target.
   Keep the existing supervisor and full selector. Do not narrow, retry, extend
   limits or substitute fake backends to obtain a pass. A failure is diagnostic
   evidence requiring fresh review/admission.
6. Before any authorized merge, recheck required independent exact-head reviews,
   affected tests, integration, server rules and holds. Retain branches. This
   checkpoint makes no aggregate, Wasm, installed-product, stage-one/stage-two,
   production-bug-fix, or self-hosting-completion claim.

## Archive map and integrity

`inventory.json` lists every checkpoint file except itself, with bytes, SHA-256,
Git blob and mode. The existing artifacts below are retained byte-for-byte:

- `source.patch`, `source-findings.md`, `source-validation-plan.md`,
  `source-review.md` and `source-manifest.json`: original source packet.
- `resource-manifest.json`, `frozen-proposal-manifest.json`,
  `frozen-admission.json`: original local proposal/accounting records.
- `route-review.md`: independent review of that exact frozen proposal.
- `unarmed-public-control.yml`: unchanged control proposal, never installed here
  under `.github/workflows/`.

Original relative paths in frozen records describe the earlier local packet.
For recovery, `compiler-cache-closure/candidate/test/language/package_imports.c`
maps to the real test path above; its `independent-review.md` maps to
`source-review.md`; `package-cache-validation/proposal/package-cache-validation.sh`
maps to the real helper; its control maps to `unarmed-public-control.yml`; and
`review-cache-validation-route/independent-review.md` maps to `route-review.md`.
Historical statuses such as unpublished, unarmed, original base and pending
independent route review describe those frozen records' creation time. This
README and the draft PR provide the checkpoint context; those records are not
silently rewritten to claim later validation.

Copied reference dependencies, raw logs, generated binaries, duplicate reports,
and local synthetic harnesses are omitted. Their historical identities remain
in the frozen manifests. Repository reference files are recoverable at the exact
recorded commits/blobs. The two independent reviews and case-level source plan
carry the substantive evidence and remaining gates; omitted synthetic material
must not be represented as freshly rerun or as product coverage.
