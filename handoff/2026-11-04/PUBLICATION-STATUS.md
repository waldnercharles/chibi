# Existing branch: publication checkpoint

On November 4, 2026, the 22 completed local source/test/documentation checkpoints
were published in their original order to `codex/resume-native-jobs-20261104`.
Initial publication head: `752df281116e74f29642117215e0a766f1e26d2e`.
Its tree `32b9025f14a76b7dc36e4cc4bcc53a24ea5a7894` exactly equals local
`5a6d81ce3233f448aa6d5752e2427320ee117b2f`. Every remote commit's tree, parent
and message, and the final branch ref, were read back. The connector generates
new commit metadata; [publication-history.json](publication-history.json) retains
the exact original-to-published commit mapping. The original local history is
also preserved in a verified Git bundle.

## Current limits

These are development checkpoints, not merge or release acceptance. PR #1331
remains explicitly not ready. No PR branch, release, seed pin or protection was
changed. The initial published head had no Actions runs when queried immediately
after publication; publication is not a claim of passing exact-head CI.

The separately validated current compiler has a raw fixed point and 573 exact
tests. Focused native-runner, command, nested-pointer, image, allocation-audit,
product-construction, archive, asset-bridge, cold-SDK, Graph, array-cookie and
equality evidence remains limited to each linked dated report. Actual Windows
execution, full graph-c runtime and ordinary VM/native/C engine integration
remain open. Fresh registered compiler-host construction and static inspection
now pass; host runtime remains separate. Cross-linking and PE inspection are not Windows
runtime acceptance.

The retained pre-correction strict compiler-host construction failed after 31 successful C
objects, at 19 nonconstant-initializer diagnostics in `text_definition.c`. It
produced no archives or host. See [HOST-INITIALIZER-FAILURE.md](HOST-INITIALIZER-FAILURE.md).
The archived source-only initializer proposal has a subsequent
[focused tested correction](NATIVE-DEFAULT-INITIALIZERS.md). The next fresh host
construction advanced to 44 successful objects before one strict JIT signedness
error; its [one-line correction and paired strict compile proof](JIT-SIGNED-OPERAND.md)
are now accepted. The following construction reached 46 successful objects,
then stopped at two signedness diagnostics in `jit_memory.c` and
`jit_registers.c`. Their [two-line correction and exact strict compile pairs](JIT-SIGNED-WIDTHS.md)
are independently accepted. The broader diagnostic remains failed: it found
additional source errors in `jit_suspend.c` and `runner.c`, then stopped on an
unadmitted canonical header spelling before its final unit. A fresh
[six-command compile continuation](JIT-PORTABILITY.md) now passes both remaining
strict pairs and the final two units, with the canonical alias bound in advance.
The old failure remains preserved. Those failed attempts produced no archives
or host. A fresh [complete registered construction](REGISTERED-HOST-CONSTRUCTION.md)
now passes all 88 compiles, 16 archives and the host link, with independent static
artifact admission. Real runtime parity remains pending. The permanent initializer
regression now has accepted source, compile and focused runtime evidence, as
described in [its report](NATIVE-DEFAULT-INITIALIZERS.md); this does not clear
the full graph-c or host gates.
Disabled graph-c ownership preparation remains preserved in
[unvalidated-preparations](unvalidated-preparations/README.md).
The pointer patch there remains unapplied and has two known findings. The
[sdk-reflect wrapper finding](SDK-REFLECT-OPEN-FINDING.md) has a subsequent
[focused correction](SDK-REFLECT-WRAPPER-VALIDATION.md); its PR review remains
open pending exact-head integration.

## Preservation boundary

The archival addendum preserves existing text source, tests, controller plans,
patches and technical findings. It does not execute or integrate the WIP.
Duplicate source trees, compiler binaries, object files and large proof archives
are not Git source payloads in this checkpoint. Their prior immutable local
archives and recorded hashes remain intact; no GitHub release was created.
