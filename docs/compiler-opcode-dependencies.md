# Narrow compiler opcode dependencies

## Measured pressure at `fa1b7b2`

The current compiler seed reproduces byte-for-byte with the checksum-verified
published host: `41905e8bc4afc3998cf48097a98d6c1ca0ce89ff2de6a6dadcffb6fb06f1c71e`.
Its prepared package is extremely close to the existing authored-layout work
limit. A scratch traversal of the complete prepared v20 wire format, applying
the unchanged reader/copy/finalize/import-fingerprint accounting, reconstructs:

| Shape | Layout work | Host validation |
|---|---:|---|
| Current compiler | 268,197,343 | passes |
| Current compiler plus one scalar-only probe | 268,197,343 | passes |
| Current compiler plus bounded native accessor | 270,618,556 | fails |

The limit is 268,435,456 units: only 238,113 (0.089%) remain. Of the baseline,
68,491,188 read/finalize units concern the 48-field `Ops` record. The accessor's
`NativePlan` union introduces no additional prepared record requirement; moving
or flattening that union does not solve the underlying near-limit compiler.
The accounting is source-reconstructed, not a patched runtime counter. Actual
unchanged host results agree with the threshold. Rejected bytes were inspected
only, never loaded with relaxed validation. No quota increase is proposed.

## Bounded change

Six private leaf constructors accept the entire `Ops` record but use precisely
one field. Give each the opcode it actually needs:

| Helper | Required field | Calls |
|---|---|---:|
| `ConstantCode` | `constant` | 24 |
| `CompletionCode` | `source_point` | 5 |
| `ExpressionCode` | `source_point` | 12 |
| `OccurrenceCode` | `source_point` | 2 |
| `PointCode` | `source_point` | 5 |
| `DefaultWatch` | `source_watch` | 2 |

This changes six private signatures and fifty calls in `Body.chibi`,
`Steps.chibi` and `Maps.chibi` only. Callers select the same existing field;
opcode values, diagnostics, effects, source ranges and debug/watch metadata
remain unchanged. No public semantic export, language feature, prepared ABI,
reference protocol or shader contract changes. This is narrower dependency
passing, not opaque data packing or a second frontend.

## Validation gate

The first checkpoint records the measured proposal only. The code experiment
must measure its regenerated closure with unchanged host limits, reproduce a
fixed point, compare old/new compilation and execution (including constants,
debug points/watches and negative cases), and receive independent review of
the exact candidate. The historical LLVM source-native proof is retained on
its own branch and is not a current-main compatibility claim. Bootstrap seed
publication and integration remain gated on that evidence.
