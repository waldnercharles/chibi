# Named primitive-native argument fixtures

This document records the preserved fixture-only checkpoint. The separately allocated implementation and its current status are described in [Primitive native argument association](rich-compiler-native-names-implementation.md).

This is a test-only handoff, not an implementation or a passing compiler acceptance claim. The retained branch is `codex/rich-compiler-native-names-fixtures-20261024`, based on accepted main `4684d45bc78d37b89ac35ae94aa33f392157edc7`. The independently accepted array receiver-watch branch stays unchanged at `6dc54c24a436be7fabadeca365b96395dcadd434`.

## Bounded contract

Use existing non-overloaded `text_slice(Value: string, Start: int, Count: int)` and `text_byte_length(Value: string)` natives. Every valid fixture explicitly supplies every parameter. There is no compiler, runtime, target-layout, conversion-policy or bootstrap edit.

| Fixture | Literal result / diagnostic |
| --- | --- |
| Positional control: `text_slice("abcd", 1, 2)` | `["bc"]` |
| Labels in declaration order | `["bc"]` |
| Labels in order Count, Value, Start | `["bc"]` |
| Positional Value, then Count, Start labels | `["bc"]` |
| Unary `text_byte_length(Value: "abcd")` | `[4]` |
| Count/Value/Start argument expressions update a reference integer | `[321]` |
| Unknown Missing label | `Unknown argument 'Missing'`, 2:66 |
| Duplicate Start label | `Argument supplied more than once`, 2:66 |
| Extra fourth positional argument | `Too many arguments`, 2:55 |

The trace appends digits 3, 2, 1 while producing Count=2, Value="abcd", Start=1. It also checks the sliced value is "bc" before returning the trace. Evaluating expressions in parameter order would produce 213; evaluating twice changes the trace. Expectations are C literals, not outputs copied from another compiler run. The C stage-zero reference must independently satisfy the same literals and source coordinates.

**Defaults boundary:** an attempted missing-Count diagnostic was removed after the reference accepted that call. `NATIVE_PORT` in `src/capabilities/definition.h` sets `has_default = true`; the false column in `NATIVE_TEXT_PORT` is `default_owner`. This slice does not choose omission/default semantics or extend the authored `Port` representation. A missing-required-input diagnostic needs a separately established required-input interface; it must not be inferred from these natives.

## Files and handoff

- `test/language/package_compiler_chibi.c:6983–7139`: appended fixture table, reference and authored C/pico cases, two exported dispatch functions. Existing sample table and shared `run_samples` stay unchanged.
- `test/language/package_compiler_chibi.h:23–24`: declarations inside the include guard.
- `test/language/main.c`: selector recognition at 193–196, guard at 298–299, usage at 341–342, dispatch at 545–548.
- `tools/test_policy.py:353–357,564–566`: two selectors and explicit compiler-group exclusion for the pending authored acceptance case. Reference selector belongs to the compiler group; authored selector is opt-in until implemented.

These test locations require a small splice with the M2 author's changed dispatcher/tests. No shared compiler file is edited. Current M2 remote head was verified as `7096fdf1d97bad1d5f64b01a7bab8fa7a5ecb99f`; its semantics/artifact are not adopted.

For implementation ownership, allocate only the ordinary fixed-arity native path in `Steps.chibi` `CallStep`: argument scheduling around 1699–1734 and linked-call argument/cell association around 1911–1988 (line numbers on this base). `Compile.chibi:149–154` already supplies `Port.name`; `compiler_target.c:76` already exports it. Preserve source-order evaluation, associate values with declared ports afterward, and preserve the original argument node for C-string checks and diagnostics. Do not route authored calls, overloaded/variadic natives, constructors, defaults or float conversions through a newly expanded policy. The M2 owner must reconcile these sites against its current code before editing; this branch does not grant shared-file ownership.

## Validation and holds

Build with `make language-build JOBS=2`, then run through the normal supervisor:

```
CHIBI_LANGUAGE_BUILD_DIR=build-language python3 tools/test_policy.py tool language-package-compiler-chibi-native-arguments-oracle
CHIBI_LANGUAGE_BUILD_DIR=build-language python3 tools/test_policy.py tool language-package-compiler-chibi-native-arguments
```

The reference passes six compiled-and-executed programs on both VM and SLJIT host selections, plus three diagnostic checks. The authored selector compiles real current Chibi compiler source on both compiler backends, then executes its successful positional-control outputs on both host backends. Current source rejects all five named-call programs with unsupported named_argument syntax. Unknown label also receives that generic rejection; duplicate and excess arguments receive generic arity errors. The selector intentionally returns nonzero for these 16 mismatches, rather than normalizing missing behavior as expected rejection. The exact clean tested source checkpoint is `23a5236b01ada794d382c9554f6c5192d40643eb`: reference 1 case/126 assertions, 0.364s; authored 1 intentionally failing case/4834 assertions, 9.599s. Build configuration took 1.9s and build 3.6s. Full logs and driver SHA256 are in `docs/evidence/rich-compiler-native-names`. Independent fixture review found no remaining blockers.

This standalone Debug build has no sanitizer claim. Native LLVM, Wasm, recovery, fixedpoint, full aggregate groups, renderer and SDK acceptance are unrun. No bootstrap was regenerated or published. The only dependent action held is editing shared `CallStep`, pending parent allocation to the M2 compiler owner; there was no denial or uncertain mutation. Independent fixture preparation is complete.

Local ignored scratch: `.chibi/native-names` contains preparation script and logs; `build-language` contains the normal driver build, and `.chibi/logs` the normal build supervisor logs. No scratch executable or project copy was created.
