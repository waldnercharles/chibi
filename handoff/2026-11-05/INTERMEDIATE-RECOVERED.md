# Intermediate compiler recovered, 2026-11-05

The fresh Linux x86-64 reconstruction of source `14f7a46a047ab92acfe58398b3cdac25ab7d0312` is independently accepted for use as the next current-source builder. This follows the input-only snapshot in [RECOVERY.md](RECOVERY.md).

## Actual result

- Stage3 and stage4 are directly byte-for-byte equal and reproduce the previously accepted intermediate SHA256 `0a43d0a2b91f6c5e36afc8ba1ea586fe938d6fbb39b687acb3e3ee442bcab7dc` (10,633,736 bytes).
- All 485 exact, unique canonical compiler tests passed, with the sole summary `485 tests, 0 failed`. No filters or skips were added.
- The build/test commands reached observed exit 0. Every owned phase ended at ECHILD with zero signals, errors or surviving children. Original and copied source, tools, runtime closure and proof identities were independently rechecked.
- The original generated compiler stays mode 0600. New execution requires a deliberately verified copy and its own bounded gate.

The phase totals were 95.505 seconds for stage2, 98.672 for stage3, 101.322 for stage4, and 49.880 for all 485 tests. Each build retained its 120-second total limit; tests retained 180 seconds. These are correctness-run observations, not quiet benchmark measurements.

## Exact scope and limitations

The recovered public 7482 CI artifact was used only as a quarantined provisional builder. Its historical failed runner contract is not waived by this distinct exact-source reconstruction. The old unknown c5 bootstrap was not restarted or signaled. The excluded native union leak investigation was not run.

Builds used jobs 2 on CPU affinity [0,1]. The canonical test CLI has no --jobs option: its object compilation falls back to 8 jobs with empty PATH, while CHIBI_TEST_WORKERS=2 limits test executions. The driver discards stderr from passing individual tests; this proof establishes empty compiler stderr, not a stronger individual-test-stderr guarantee.

This proves the frozen 14f7 intermediate only. Current 02c862 compiler recovery and 573 tests are next, separately admitted work. The development branch's later POSIX stamp changes are not part of this source snapshot. Windows, broader runtime/integration gates, and PR #1331's explicit hold remain open.

A local preservation package contains the full frozen source, three compiler outputs and machine-readable construction/test proof. Every archive member and live input was checked after packaging. The original seed ZIP is a separate companion to keep each file below 32 MiB. No upload or release was performed. Package and detailed result identities are in [intermediate-recovery-results.json](intermediate-recovery-results.json); official dependency inputs remain in [recovery-inputs.json](recovery-inputs.json).
