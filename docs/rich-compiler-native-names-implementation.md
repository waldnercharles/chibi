# Primitive native argument association

Implementation branch: `codex/rich-compiler-native-names-20261024`. This is an ungenerated source checkpoint based on fixture head `f57675ec2fca2d67286bef0bb7497b49a7bf1811` and accepted main `4684d45bc78d37b89ac35ae94aa33f392157edc7`. The original fixture and array-watch branches remain unchanged. Historical red evidence is retained in `docs/evidence/rich-compiler-native-names`.

## Source delta

Only `tools/compiler/package_compiler/Steps.chibi` changes product behavior. Two small helpers identify fixed primitive inputs and resolve a written label by exact name first, then the existing unique loose-name rule. The ordinary native scheduling path uses the selected port's existing expected type but schedules each original value expression in written order. Unknown, ambiguous, duplicate and excess arguments fail at their original source nodes.

The existing local Work discriminator uses value 7 for this path; no Work layout, target interface, prepared format or runtime ABI changes. Once evaluation finishes, its values and original expression nodes are associated with declared ports. Host input cells and input watches remain in declared-port order. C-string validation uses the associated expression node, including parentheses, instead of the old written ordinal.

The path excludes overloads, variadics, constructors, inferred/container inputs and nonprimitive inputs. Numeric expressions retain the existing Expect/type/conversion behavior. No typed conversion branch changes. This does not add default evaluation or a missing-argument rule: native catalog defaults remain unchanged, and the authored compiler's existing incomplete-arity limitation remains. The supported addition is fully supplied calls.

## Fixtures

The original six programs and three diagnostics now pass: positional control, declared/reordered/mixed labels, unary label, source-order trace 321, unknown label, duplicate label and excess positional argument. Additional C/pico cases cover loose lowercase label matching, both positional/named and named/positional duplication, and original parenthesized expression locations for unary and reordered two-port C-string errors. These fail at 2:48 and 2:47 respectively, not at the label or call. The two-port private C-string native is a diagnostic-only fixture; no default-host execution claim is made for it. Successful public-native fixtures also compare source-point and input-watch metadata with stage zero. There are seven executable and seven diagnostic fixtures. All expected values/messages/coordinates are fixed C literals checked independently against stage zero.

The authored selector is restored to the normal compiler aggregate. The fixtures continue to compile actual Chibi compiler sources on both backend selections, execute successful outputs on both VM/SLJIT host selections, and compare diagnostics. These are source compiler tests, not tests of a regenerated checked-in compiler artifact.

## Composition ownership

Read-only references inspected before editing:

- M2 remote head `ab8c4a58f6cabc8b74e9d94c37fde41cb634c2ac`. Its numeric conversion/exact folding changes are outside these scheduling/association hunks. Its accepted array-watch source changes are not pulled into this isolated branch.
- Float checkpoint `83f9977baafe0390c6cee09c80e51eb5c19a24fb`. Its CallStep changes add explicit numeric conversion and widening, and move address-slot base `b` after conversion temporaries. Preserve those changes when composing: this delta permutes already-evaluated values before that loop; the float widening logic must operate on that port-associated `arguments[index]` and retain its later `b` placement.

Extract the bounded implementation with `git diff f57675ec2 <implementation-source-head> -- tools/compiler/package_compiler/Steps.chibi test/language/package_compiler_chibi.c tools/test_policy.py`. Do not replace another lane's files wholesale. The historical handoff document and this status document are documentation only. Root owns reconciliation with current M2/float sources and the final single bootstrap generation.

## Validation status

Exact clean tested source: `1108e4310dc862bf536bc860eb1dfa7e1a055f0b`. Independent source review found no blocker. All checks used the normal supervisor and the two-job standalone Debug build; no sanitizer claim.

| Selector suffix | Result | Cases / assertions | Wall seconds |
| --- | --- | --- | --- |
| native-arguments-oracle | pass | 1 / 163 | 0.353 |
| native-arguments | pass | 1 / 9739 | 10.142 |
| base compiler | pass | 2 / 16142 | 21.899 |
| compiled | pass | 1 / 11622 | 10.446 |
| collections | pass | 1 / 3102 | 11.689 |
| sharing | inherited failure | 1 / 11 | 9.783 |
| debug-free | pass | 1 / 1876 | 10.143 |
| scalar | pass | 3 / 10251 | 10.353 |

Sharing identifies `CollectScalar` (`ebbc43e46769862a`). Building and running the exact clean fixture baseline `f57675ec2` reproduces that same failure (11 assertions, 9.920s). This is not a new CallStep/helper failure, and no Scalar/runtime edit or relaxed assertion is included. M2 owns its repair; the overall sharing gate remains open.

Build configuration/build were 1.9s/3.8s before the first six checks, and 1.8s/4.1s after restoring the same source from the baseline investigation for the last two checks. Logs, both driver identities, source hashes and unchanged d2fd bootstrap hash are under `docs/evidence/rich-compiler-native-names-implementation`.

Product-only `git apply --check` succeeds separately against both inspected float and M2 reference files. This does not establish combined-source acceptance. Root must reconcile current shared files, rerun the combined checks, resolve the existing sharing gate, and coordinate one matching bootstrap generation/fixedpoint/recovery sequence. No seed was regenerated or published here. Native LLVM, Wasm, full groups, renderer and installed-SDK gates were not run.

Local ignored scratch remains in `.chibi/native-names` (scripts, logs and comparison copies); the normal `build-language` driver and `.chibi/logs` remain available. No scratch binary or project copy was created.
