# Workspace recovery and remaining validation

This record covers the existing takeover track, not the separate vNext/Rust work.
The published takeover checkpoint remains the recovery source of truth:
[`5d64ce8d5`](https://github.com/waldnercharles/chibi/commit/5d64ce8d5a97e4df2cbbcc0bd0fbbba17f724b41).
A replacement workspace has restored all 4,162 tracked entries at that commit,
with a clean checkout and a successful Git object integrity check. The previous
local compiler payloads and full proof archives are unavailable. The committed
Nov4 summaries remain historical evidence, not newly repeated tests.

## Recovered inputs

The exact `native-stages-linux` archive from
[run 41574174752](https://github.com/waldnercharles/chibi/actions/runs/41574174752/job/125714037928)
was retrieved again. Archive ID 13204584163 has SHA-256
`0c7fe2018c3b77085edf3708e99adc28279ab2f606a85f422595bbb2a4f4e5b6`.
Its stage3 and stage4 files are equal, 10,062,736 bytes, SHA-256
`6e867c8e5588509286a5709ecdca902e8aaeb6d3db51f1ea4efb1bb09b677d03`.
They remain quarantined provisional builders. The historical runner parity
failure is retained; retrieving an equal pair does not admit it as a compiler.

Official `libllvm22`, `llvm-22` and `lld-22` workspace packages were restored
from apt.llvm.org/trixie, version
`1:22.1.8~++20260613092233+e80beda6e255-1~exp1~20260613092250.77`.
Their official index sizes and SHA-256 digests were checked before unpacking.
No system package configuration was changed. Static dependency inspection found
no missing libraries. The tool sizes and timestamps match the published Nov4
cache record; the lost installation's binary hashes were not published, so
cryptographic identity to that installation is not claimed.

Exact source snapshots were restored for:

- Intermediate `14f7a46a047ab92acfe58398b3cdac25ab7d0312`, tree
  `342ada78280eaecde5a5a8ffa0845a74a729926d`, 3,400 tracked entries.
- Takeover `02c86287430def964445ec90185961e20319536d`, tree
  `780ba87ea9f29308f2f20a94381cdce8cc5d26ca`, 4,054 tracked entries.

Independent declaration enumeration recovered all 485 and 573 compiler test
names. Their sorted newline-separated hashes exactly match the prior observed
name inventories in the Nov4 results. This verifies the inventory, not test
execution.

## Next gates

The restored provisional builder and downloaded tools have not executed in this
workspace. A fresh bounded reconstruction is being prepared against the exact
intermediate source, then the exact takeover source. Each compiler needs its
own fixed-point comparison, complete compiler-test result, input identities and
clean process termination. Original build/test phase limits remain in place.
The new launch files are reconstructed from documented command forms and
parameters, not represented as recovered verbatim Nov4 controllers.

The last previously completed native phase was construction of the OwnedRecords
writer/resolver helpers. Their follow-on writer was never launched. Those lost
helper artifacts and their downstream parity work must be reconstructed and
validated before use. The stronger per-call Graph descendant proof remains a
failure; any separate whole-suite containment result must retain that distinction.

Actual Windows, Driver/Graph equivalence, current product/integration acceptance
and exact-head independent reviews remain open. PR #1331 is explicitly not ready
to merge. No release, seed pin, branch protection or hold is changed here.
