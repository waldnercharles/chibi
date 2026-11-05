# Fresh version-3 source-native replay

All five distinct public selections passed once under the recovered, accepted
current compiler and the freshly built version-3 backend. Independent terminal
review accepted **93 source-derived AOT value checks and 2,232 JIT value checks**,
plus separate source-rejection, ABI/copy/lifetime and backend-failure controls.

- nativecases: 45 AOT checks, 1,080 JIT checks.
- scalars: 15 AOT checks, 360 JIT checks.
- direct_calls: 23 AOT checks, 552 JIT checks.
- modules: 2 AOT checks, 48 JIT checks.
- foreign: 8 AOT checks, 192 JIT checks.

All 25 stages and five actual outer exits passed. There were no retries or
skipped stages. Each case performed Chibi caller construction, normal JIT
checks, deliberate PERF failure, C linking and linked C execution. Across the
five cases, 45 successful backend owners, five PERF failure owners and one
missing-foreign-symbol failure owner were checked separately from value counts.

## What those results establish

Successful modules start as exact Chibi source, use the retained parser and
signature reader, and pass through Produce/Verify/Pack into the same Cranelift
JIT/object lowering. They are not hand-built successful IR recipes. Merge-shape
inspection is read-only; buffer poisoning happens after the synchronous compile
call. Entry lookup, signature, short/exact copies, COW and owner lifetimes are
separate controls, not parser tests. The missing foreign symbol is valid source
rejected by backend resolution.

Actual native-main, copied object and linked AOT artifacts were independently
inspected. Static calls, twelve foreign GOTPCREL references, six foreign
descriptors and fourteen archive-function instances/getters reconciled with
actual linked bytes. Complete GCC header/link traces matched admitted inputs.
All process owners reached ECHILD with no timeout, surviving child, cleanup
error or signal. Source/tool/runtime fences and retained files remained exact.

The source baseline is commit `5b283f5873173f845e2120820711ac1461a2ad1f` plus the
accepted cleanup owner from `383d67bfd1da1b95fb514d9c01c5ed1c11e2f69c`. The compiler
is the accepted frozen `02c862874` build. Full artifact identities, individual
receipt/acceptance hashes and observed stage times are in
`source-native-replay-20261105.json`. Aggregate acceptance SHA-256 is
`025dc8e562a46c36ab9bef44bbb8e87dfb85ca4db67fcdb76ade91e16c41ff59`.

## Boundaries

This is Linux x86-64 System V developer-correctness evidence. It closes the five
public current-v3 replay selections, including the four older selections. It
does not qualify a complete compiler, other platforms, full language/runtime
parity, merge readiness, or a release. The nine optional identity-dependent
Python selectors remain explicitly skipped in their separate regression proof.

Original per-case work caps remain 179/119/29/59/29 seconds, with one second of
cleanup for each stage: 415 seconds of work plus five seconds of cleanup. Outer
validation and receipt I/O are outside that summed owner budget. Terminal ELF
inspection is not pre-execution artifact admission. Successful temporary
compiler/linker products deleted by unchanged tools have no retained-byte proof.

Stage times are correctness receipt facts, not a quiet comparative benchmark.
They establish neither a speedup nor the 100,000 lines-per-second goal. The
inactive version-4 source-census implementation is not covered by this result.
