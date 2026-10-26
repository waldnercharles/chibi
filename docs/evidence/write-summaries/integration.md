# Integration with accepted native defaults

The history-preserving source merge is
`646ca77c4e99d430e39ab4d04c2f06e2246f832b`, with parents
`135037029a165e4ef3819af47dbe1a39a43324e4` (#711) and
`ad5d2ddab604d41e52bbda8308e4bfbb40c1d6cd` (accepted main after #709).

The WriteSummaries source and focused regression remain byte-identical to the
previous #711 source. Main's Scalar.chibi and scalar_facts/Facts.chibi remain
byte-identical, as do its default-argument corpus and folded defaults fixture.
The shared C test file merged without source conflicts. The integration adds
no behavior beyond preserving those two accepted changes.

Only the bootstrap artifact and manifest conflicted. The source checkpoint
retained accepted main's exact pair temporarily, preserving its real provenance.
Its original Compile.chibi entry was not hand-edited to claim new compiler
identity. The reviewed artifact was then regenerated from the clean combined
source revision above using the documented bootstrap seed test and host --seed
workflow.

The unchanged checksum-pinned Mac runtime first recovered that combined source
from the temporary main seed in two isolated source-only invocations. Both
stages were byte-identical. Normal bootstrap generation independently produced
the same compiler, and the bootstrap test passed 23,630 assertions.

The combined compiler is **16,156,989 bytes**, SHA-256
`abcff3f6ac05937a3f4f6f033bd8a49b6edd05b992ecddb466b4fb105a89f28f`.
The manifest SHA-256 is
`57458a820535c967c5ddadca5d250e98ae2e204da8bb46709b3405c4ce5aba13`.
It records generating revision `646ca77c4e99d430e39ab4d04c2f06e2246f832b`
and all 20 source identities. Relative to main's manifest, only revision,
closure identity and the Compile.chibi source entry change. The source list,
entry, roots, version, minimum, format and full target are unchanged. The
runtime pin and scalar identity harness are unchanged.

The integration is validated with focused WriteSummaries, portable scalar,
imports and library checks, bootstrap reproduction, ASan/UBSan ownership
coverage, and final source-only pinned recovery. Hosted validation remains the
unchanged three-platform self-build/scalar matrix and cross-platform comparison.
Linux x86-64 native scalar execution, including the folded defaults fixture at
O0/O2, belongs to the normal native CI selector; it cannot run on the local
arm64 Mac. Individual outcomes are reported in the PR and its exact-head CI.

The original measurement files are unchanged and describe base
`c2ad1f31917829ddf2342ff53a3efdd32412dc1a` and source commit
`884d335e8ab01a9ba644af1a65c82d8e8309bf2f`. No new timing campaign was run.
The earlier 3.74% selected-byte reduction and inconclusive timing result are
not measurements of this combined tree.

Completed local checks ([commands and source identities](integration-checks.json)):

| Configuration | Selector | Cases | Assertions |
|---|---|---:|---:|
| Release | write-summaries | 1 | 781 |
| Release | scalar | 3 | 22,789 |
| Release | imports-compiled | 2 | 13,231 |
| Release | libraries | 13 | 17,407 |
| Release | bootstrap | 1 | 23,630 |
| ASan/UBSan | write-summaries | 1 | 781 |
| ASan/UBSan | scalar | 3 | 22,789 |
| ASan/UBSan | borrow | 27 | 848 |
| ASan/UBSan | program/retention/capture/query | 167 | 12,450 |

No sanitizer diagnostic was reported. Full native/material/project/tool/language
groups, GPU checks and Linux native execution were not run locally.
