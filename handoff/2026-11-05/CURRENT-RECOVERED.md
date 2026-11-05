# Current compiler recovered, 2026-11-05

The frozen Linux x86-64 compiler from source `02c86287430def964445ec90185961e20319536d` is independently accepted again. This follows the fresh [intermediate recovery](INTERMEDIATE-RECOVERED.md), with no restart of the unknown original c5 bootstrap.

## Actual result

- Fresh stage3 and stage4 are directly byte-for-byte equal: SHA256 `7b17b5bfdaed144e0949afd065db723bb45c8bd55b356cb70910d6df42e6d266`, 11,576,992 bytes. This is exactly the historical accepted current compiler, not a replacement identity.
- All 573 unique canonical compiler tests passed with the sole summary `573 tests, 0 failed`. No filters or skips were added.
- Every phase reached observed exit 0 and ECHILD, with no signals, errors or surviving children. Independent review reconciled the actual compiler ELF, raw fixed point, all source/tool/runtime identities, exact test names, split raw output and complete filesystem cleanup.
- The original artifact remains mode 0600. Subsequent work uses a separately verified executable copy and its own applicable limits.

Observed phase totals were 114.112 seconds for stage2, 104.269 for stage3, 109.269 for stage4, and 69.410 for all 573 tests. Each build kept its original 120-second total limit; these new current-build phases reserved two seconds for cleanup inside that limit. Tests kept 180 seconds with a ten-second cleanup reserve. No launched deadline was extended. These are correctness-run observations, not quiet benchmarks.

## Scope and preservation

Builds used jobs 2 and CPU affinity [0,1]. The canonical test CLI has no --jobs flag and falls back to eight object jobs under the explicit empty PATH; CHIBI_TEST_WORKERS=2 limits test executions. Passing individual-test stderr is discarded by the unchanged driver. This proof establishes empty compiler stderr, not a stronger individual-test-stderr guarantee.

The accepted source is frozen 02c862874. Later development-branch changes, including the new POSIX stamp code, require their own tests. The historical 7482 runner failure, excluded native union leak investigation, Windows and broader runtime/integration gates, and PR #1331's explicit hold remain separate. No release or merge is implied.

The current source/proof package preserves the full frozen source, all three fresh compiler outputs and machine-readable construction/test records. All 4,170 members and live inputs were reverified after packaging. It is 32,541,186 bytes, below 32 MiB. The separately preserved intermediate package records the preceding bootstrap and 485 tests. The packager did not upload either archive or execute any generated program.

Exact artifact, receipt, timing and archive identities are in [current-recovery-results.json](current-recovery-results.json). Official dependency inputs are in [recovery-inputs.json](recovery-inputs.json).
