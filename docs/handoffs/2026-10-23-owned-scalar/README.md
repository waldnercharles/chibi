# Owned scalar leaf: retained WIP checkpoint

Base: `d0de4a84ecf50badcab661411470bdf6f3da5fa2`.
Branch: `codex/chibi-owned-scalar-producer-20261023` (retain).

This first checkpoint establishes a supported leaf record and transport
capability only. It does **not** implement or validate the semantic producer,
prove native output, change the compiler default, regenerate the bootstrap,
change the runtime ABI or limits, or clear review/integration holds.

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
- Offsets count bytes with an exclusive end. Lines and columns count source
  bytes from one. The declaration contains the body; the body contains the
  return; the return contains its expression.
- Initial producer admission will exclude unary negative literals, parentheses,
  parameters/references, named or non-int results, containers/nominal types,
  attributes, imports, additional declarations and additional statements.
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
matrix, bootstrap fixed point, native object execution or CI were run here.

Next: derive the accepted record through shared snapshot/declaration/body/effect
semantics, sharing the existing literal type/range decision with `ValueStep`,
then independently replay positive and fail-closed cases on both backends.
