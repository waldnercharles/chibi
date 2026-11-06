# Current-source reproduction and old-seed transition diagnosis

Frozen production source:25cad452ae87beb899d1f2b43bb30a10c09af498,
tree e6633caa3bd369eb4ed54ec5e1a39f7dced27931. Evidence-only head before the run:
8d3899253dc011a9086dc9165ca03fac1fb36791, tree b78a1da3d1be6adf4468eb7a18c89672c4321095.
No production source changed during this diagnostic. PR693 remains separate.

## Current compiler reproduces itself

The existing `language-package-compiler-chibi-bootstrap` fixture passed
1 case /18,895 assertions in17.422 seconds with CHIBI_BOOTSTRAP_SEED explicitly
unset. It retained target equality, strict stage-two/stage-three byte equality,
argument-sharing checks and all ordinary sample/literal comparisons. One extra
assertion checks diagnostic capture writes after buffer cleanup.

Both outputs are13,673,789 bytes, SHA256:
`b1d902d35c4468e6de7844e9d29e4e0010dfeac2964c6e84926fcea98a6e5c71`.
They were independently prepared through current stage zero, not accepted by
waiting for arbitrary later convergence from the old seed.

The scratch observation.patch only copies the live request, target and two
output buffers before they are freed. No production seed writer ran. All C
assertions and ordinary budgets remain. Native objects are unchanged from the
reviewed d459 build; the exact current C fixture is the sole substituted object.
The linked old asset artifact is unused by this bootstrap selector, so this is
not a successful normal asset build. Identity/compile/link/results receipts
record the precise source composition.

## Exact old-seed failure explained

The captured request matches recovery's original project, core, ordered20
source paths, target and debug=false. Only Body.chibi and Steps.chibi source
hashes differ from the original8d manifest. Two supported host artifact
invocations captured exactly the two generations rejected by recovery, keeping
its comparison and no-write-on-mismatch behavior intact. No recovery retry or
new manifest was used.

- Retained8d input:13,663,537 bytes,9bb4f52e14c3ec7570b0054bc2526d1c9bd073b22cda17cb993ffde107f53ce1.
- Transition-first:13,673,789 bytes,b230dbdee1a30b58f9806a37437804505bad09d9b6e4678e971ef269eb4b5fe8.
- Transition-second:13,673,789 bytes,b1d902d35c4468e6de7844e9d29e4e0010dfeac2964c6e84926fcea98a6e5c71.

Transition-second is byte-identical to both independently produced current-source
outputs. Transition-first differs by precisely15 bytes in five instruction-origin
records in project:package_compiler; all other bytes in all12 modules match.
Each difference is start+1, end-1, column+1, line unchanged, recording the inner
expression instead of its enclosing parentheses. Number.chibi locations:

| Function | Line | Old range | Current range |
|---|---:|---:|---:|
| FloatFromDouble |338|10878..10894|10879..10893|
| FloatFromDouble |359|11529..11545|11530..11544|
| DecimalBits |480|15796..15828|15797..15827|
| DecimalBits |484|15984..16022|15985..16021|
| MagnitudeSubtract |677|21851..21865|21852..21864|

`divergence.json` records module and closure offsets, source expressions, decoded
fields and exact encoded bytes. The decoder uses the production closure framing
and prepared_source ULEB128/zigzag field layout, validates each source prefix,
checks the corresponding current source text, and accounts for every changed
byte. This directly connects the difference to Body's reviewed parenthesis
traversal. No opcode, operand, other origin field or other module byte differs.
Independent read-only review verified the same field-level result and hashes.

## Preserved evidence and remaining gates

All four compiler outputs remain locally under
`.chibi/stagezero-validation/self-reproduction-25cad/captures/`, with request and
target; capture-identities-complete.json records every byte count/hash. Compiler
binaries are deliberately not published here. Raw request.json and target.json
are copied byte-for-byte, preserving integer precision; use them with matching
runtime21 host and retained8d input to independently reproduce the transition.
Commands, observation patch and comparison script are included. These are
Mac Release results, not new Linux or sanitizer coverage.

No compiler-source workaround is proposed: current self-reproduction passes.
The next coordinated step is a current-source scratch seed capture through the
supported production writer, then recovery and the mandatory default asset
build. That step has NOT run and remains pending root coordination. The old
recovery rejection is correct and unchanged. Public checked prepared20 seed,
runtime pin and all frozen8d/d459 receipts remain unchanged. No PR688, capacity
increase, PR693 source/seed adoption or release occurred.
