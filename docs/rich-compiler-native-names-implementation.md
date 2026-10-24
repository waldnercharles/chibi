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

Focused reference and authored selectors run through `tools/test_policy.py`; build uses `make language-build JOBS=2`. Before the source checkpoint, the focused reference passed 163 assertions and authored acceptance passed 9641 assertions, including both compiler backends. Independent source review found no blocker. Exact clean source identity and final selected-check evidence will be recorded beside this document. Standalone Debug is unsanitized. A successful source check does not claim combined M2/float acceptance, matching generated artifact, fixedpoint or recovery. Those gates remain pending coordinator work. No seed is generated or published here.
