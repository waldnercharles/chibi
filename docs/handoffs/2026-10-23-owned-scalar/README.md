# Owned scalar producer: retained WIP checkpoint

Base: `d0de4a84ecf50badcab661411470bdf6f3da5fa2`.
Branch: `codex/chibi-owned-scalar-producer-20261023` (retain).

The first retained checkpoint, `e540fd698f0cac3a25ba011242efc6743ad2ef3f`,
established the leaf and synthetic transport capability. This checkpoint adds
the narrow shared-semantic producer and authored acceptance/rejection tests.
It does **not** prove native output or same-process owned storage, change the
compiler default, regenerate the checked-in bootstrap, change runtime ABI or
limits, or clear review/integration holds.

## Narrow contract

`tools/compiler/scalar_facts/Facts.chibi` owns the flat authored
`NullaryPureInt32Return` record. Only `accepted: true` certifies an ordinary
zero-parameter function returning `int`, known effect-free execution, and a
complete body consisting of exactly one direct integer-literal return.
The result is a typed `int`, not an untyped constant occurrence or bytecode slot.
This is internal, opt-in and deliberately not a general IR or shader API.

- Accepted records have an empty diagnostic and all four diagnostic positions
  zero. All four declaration/body/return/expression ranges belong to `text`.
- Rejected records retain source/diagnostic only: `value` and all function range
  fields are zero; `name` is empty. They must never reach emission.
- `path` is the supplied source path. `key` is the shared source snapshot's
  canonical document key, not a private node or symbol index. It is empty when
  snapshot construction failed. `text` is the exact supplied source bytes.
- Active offsets count bytes with an exclusive end. Active lines and columns
  count source bytes from one. Inactive fields are canonical zero, not ranges. The declaration contains the body; the body contains the
  return; the return contains its expression.
- Producer admission excludes unary negative literals, parentheses,
  parameters/references, named or non-int results, containers/nominal types,
  attributes, imports, return outcomes, additional declarations and additional
  statements.
  Direct numeric spelling still must pass ordinary compiler typing and bounds.
- A record's strings belong to its invocation value graph. A shallow
  `pin_value` copy is **not** retention. The existing host can produce an owned
  JSON snapshot and reconstruct the same flat type in another invocation.

A speculative nested Source/Range/union representation was rejected by the
unchanged compiler's ordinary record-field rule. It was not adopted.

## Transport fixture, not semantic proof

`test/scalar_facts/probe/Probe.chibi` deliberately constructs synthetic fields
using the text `42`; those fields are not a compiler certification.
`test/scalar_facts/consumer/Read.chibi` imports only the leaf contract and checks
every field. The programs are compiled separately. The producer host exits
before the consumer starts. Its original JSON result-array envelope is passed
unchanged as the consumer's one-argument input, without semantic repacking.

All four producer/consumer backend combinations passed: bytecode/bytecode,
bytecode/SLJIT, SLJIT/bytecode and SLJIT/SLJIT. Counters show one entry in each
requested backend and zero fallback. Official `chibi-compile --format --check`
passes for the exact leaf and transport fixtures. `checkpoint.json` records
source, host, seed, artifact and transferred-envelope hashes.

This demonstrates cross-process owned JSON transport for this flat shape, not
a same-process producer-lifetime/storage test. The runtime lane owns that gate.
No full native/material/project/language/tools Make groups, SDK/platform
matrix, native object execution or CI were run here.

## Shared semantic producer

`package_compiler.CollectScalar(project, path, text, target, names)` returns the
leaf record directly. `target` and `names` are the existing bootstrap manifest's
`target[0]` and `target[2]`; neither is exported in the proof. The host entry is
`project:package_compiler:1:CollectScalar:callable`.

The producer uses the ordinary source snapshot and package declaration pass.
Capability gates reject unsupported syntax; they never evaluate a source
expression. The admitted body then passes ordinary `EmitBody`, including the
return type check, and must have zero actual semantic effects and no calls,
host operations, cells or roots. Unknown effects are not treated as pure.
Only after that does the same `ReadIntegerLiteral` decision used by ordinary
`ValueStep` provide the context-typed, bounds-checked result. The second call
reuses one semantic implementation; it is not a second evaluator. No prepared
instruction, private Code/Slot/Value/Name/Work identity or instruction payload
is read to reconstruct meaning.

The only existing shared source edit extracts ValueStep's literal parse/range/
bits decision into that helper. Its messages and diagnostic ranges are retained.
Private Value and Body layouts are unchanged. A first-NUL source-byte guard
precedes the temporary C-string parser binding and diagnoses that byte exactly.
Attributes and return outcomes are explicitly rejected rather than erased.

## Exact focused verification

The frozen authored tests in `test/scalar_facts/cases/Cases.chibi` passed 43
cases on both bytecode and SLJIT. The separately compiled leaf-only checker
receives each original producer result envelope unchanged, asserts canonical
accepted/rejected fields, exact source retention, byte positions/ranges and
complete declaration/body/return/expression containment. Its negative statuses
identify failed assertions without adding a test-only native capability.

Five accepted cases cover 42, zero, int32 maximum, hexadecimal spelling, and
UTF-8 comments/whitespace. Thirty-eight rejections cover integer overflow,
64-bit parse overflow, fractional/exponent spellings including 1.0 and 1e0,
unary negatives, parentheses, arithmetic, branching, local and extra statements,
unknown calls, parameters/references, bool/int64/string/container/nominal/named
results, types/constants/second functions/imports, attributes, empty/bare/missing
returns, malformed input, return outcomes, and embedded NUL after UTF-8 text.

Ordinary compiler differential checks passed:

- Twelve accepted int/int64/uint64/int8/byte/bool/float literal cases emit
  byte-identical prepared closures from the unchanged seed and this candidate;
  the resulting functions execute correctly on both VMs.
- Ten rejected numeric cases preserve the seed's exact diagnostics and ranges,
  including both signed bounds, byte bounds, uint64 overflow and float spellings.
- Candidate stages one, two and three reproduce **12,300,558 bytes**, all SHA-256
  `6a15507da9397cf20f9fd3753631bf75b2beb9782e37bfd6ab5319873d27c2e0`.
  This is a temporary proof artifact; the checked-in seed is untouched.
- Official formatting checks and `git diff --check` pass on the frozen source.

`checkpoint.json` records the exact source map/order, implementation and test
hashes, compiler input and stage hashes, backend counters, 43 semantic cases,
and 22 ordinary parity controls. The independent reviewer owns the separate
exact-source replay. No unchecked rich-game handoff, shader proposal, private
native bridge, quota adjustment or permanent C adapter is included.

Next: complete independent producer replay and the separately owned leaf-only
LLVM consumer/lifetime adapter gates; retain this branch and all integration
holds until that exact combination is approved.
