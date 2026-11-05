# Current compiler and archive validation

Tested source: `a661626d48172e3bfcda82c9dcde3018edf53eef`, tree `1ecc136fc8ab2a059430cfb6cefcd88217cf3710`. These are fresh results for the compiler produced by the [native product construction](NATIVE-PRODUCT-VALIDATION.md), not transferred acceptance from the older bootstrap compiler. Both phases and the full archive invalidation suite passed independent terminal review.

## Current compiler

The 11,576,744-byte native compiler, SHA256 `a0ded77510f72650cf9484b63721ecd980c41d9847bedaa6e512ec68f03c3cb3`, reproduced itself byte for byte. Direct raw comparison passed in the worker, after whole-owner ECHILD, and again in independent review. The generated self-output was never executed. Construction took 119.952 seconds externally observed, 119.744 seconds in the owner.

A separate fresh copy of the accepted compiler then ran the complete canonical suite: **573 distinct tests passed, zero failed**, across 20 packages and 70 source folders. Output was exactly the expected qualified PASS names and `573 tests, 0 failed`, totaling 33,873 bytes. Compiler stderr was empty. The canonical driver does not retain each individual test's stderr, so no separate per-test stderr claim is made. Externally observed duration was 69.159 seconds, owner duration 68.889 seconds.

Each native phase retained its original 240-second total and ten-second cleanup reserve. Ten fresh containment controls passed first, including forced death while exact linker temporary files were held open. All 640 inert control leaves ended at mode 0600. The revised narrow cleanup covers final `a.out` and one `a.out.tmp[0-9a-f]{7}` sibling in each known linker parent; malformed names, excess counts and mismatched evidence reject acceptance. Superseded unexecuted drafts remain preserved.

Both native phases ended with ECHILD, pipe EOF, no survivors, signals or errors, unchanged source and runtime fences, and exact final inventories. Private executable bytes, inode, size and mtime stayed unchanged through quarantine to 0600. The test scratch tree retained only its 22 expected empty directories; no test executable or linker partial remained. Original accepted artifacts remain 0600.

## Whole archive invalidation

The unchanged authored suite passed all seven cases: cold build, warm reuse, Windows-template edit, edited warm reuse, missing template rejection, restoration and restored warm reuse. Its assertions check preservation of prior output on rejection and real recovery. This tests Windows-template dependency tracking on Linux, not Windows execution.

The actual command exited 0 in 29.496 seconds, with the exact 89-byte PASS line and empty stderr. Owner duration was 29.330 seconds, within the original 300-second total and ten-second cleanup reserve. Nine fresh controls preceded it. Whole-owner containment, immutable input fences, complete final inventory and private executable quarantine passed independent review.

The authored shell trap removed seven-case internal logs and artifacts. No retained per-case output or individual timing claim is made. The successful clean terminal inventory remains valid; it does not establish complete forced-failure coverage for the linker temporary lifetime discovered later.

## Evidence and scope

Construction acceptance SHA256: `6d28793f0b25d37bcbe16c533bdfd4278368cea6f46b479a6bf7a387189775ba`. Compiler-test acceptance: `d7e752ca7eec78b60f5b65436a1bd32052fa22e73965ebcea3834de6eac85722`. Archive acceptance: `4cbe03de700baedd8157384cdf1afab2683da680157fd4cbb2452459dcb618e1`. The adjacent JSON records timings, exact identities and limitations.

The private archive proof is 22,579,503 bytes, SHA256 `d458f93e7053e7f48a5c447954ff4980a6b133dff321be967e0d4e3c809e86c8`. Independent readback accepted all 4,104 regular 0600 members, original stat/hash mappings and reconstructed source tree. External tool identities are provenance, not a portable runtime. No proof archive is published here.

The later `a26027facc2c0de6e92abc71a2641186da730544` checkpoint changes only two product documentation files and the CLI fixture. Compiler, core and compiler-test inputs are unchanged. These frozen-source results are not full exact-new-head integration acceptance. The CLI fixture repair has source review only; its full registered CLI/PTY run remains pending.

SDK installation/relocation, CLI/PTY, asset bridge, C integration, ordinary VM/SLJIT/native differential, actual Windows execution and final exact-head review remain separate gates. PR #1331, the terminal-unknown c5 bootstrap and excluded native 16-byte union investigation remain held. Stronger per-Capture descendant proof is still failed/unproven. Storage bounds are sampled, not measured transient peaks; process memory was not measured. No merge or release readiness is claimed.
