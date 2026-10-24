# Native LLVM ledger

This ledger tracks how far ordinary Chibi source reaches native code through
LLVM. Each feature lists the code that owns its meaning, what is still missing
natively, the runtime it needs, any open decision, and the selector that
checks it. A feature is native only when real source reaches an object through
`CollectScalar`, `scalar_llvm` and `Driver.Object`. A hand-written plan that
`llvm_native` emits does not count.

Three kinds of evidence exist, and only the last runs `Driver.Object`:

- `Run.ps1`, in the compiler recovery job on all three platforms, runs the
  producer and the consumer for each of the 96 corpus sources on the four
  bytecode and SLJIT crossings. It checks that each crossing gives the same
  rows, classification and IR. It validates and emits IR and builds no
  object.
- `language-scalar-native` (`--package-compiler-chibi-native`, Linux x86-64)
  runs `opt` and `llc` itself, with the argument vectors the driver uses,
  then links each object with `ld.lld` and calls it. It is the differential
  check against both VMs, and does not run `Driver.Object`. Each tool and
  each library's calls run under `chibi_process_capture` with a deadline,
  cancellation and a process-group kill.
- Actual `Driver.Object` runs are recorded one by one under
  [Baseline and measurements](#baseline-and-measurements). They cover
  representative cases, not every accepted source.

Each feature has one of these states:

- **native**: real source runs natively and agrees with both VMs;
- **stage one**: the authored package compiler supports it, and native code
  does not yet;
- **stage zero**: only the C frontend (`src/language`) supports it;
- **excluded**: the language itself does not have it
  ([current limits](code-language.md#current-limits));
- **decision**: blocked on a decision in the register below.

## Features

| Feature | Semantic owner | State | Native gap | Runtime need | Selector and evidence |
| --- | --- | --- | --- | --- | --- |
| `int` and `bool` parameters, results, literals | `Compile.chibi` `Declare`, `Steps.chibi` `ConstantStep` | native | none in the subset | none | `language-scalar-native`, `Run.ps1` (IR only) |
| Direct same-source calls, nested and repeated | `Steps.chibi` `CalledFunction` | native | recursion | none | `language-scalar-native` |
| Six signed `int` comparisons | `Steps.chibi` `OperatorStep` | native | other widths, unsigned, `bool` and string equality | none | `language-scalar-native` probes at `INT32_MIN`/`INT32_MAX` |
| One guard with a returning arm and a final return | `Body.chibi` `if_statement` | native | `else`, several guards, general control flow (M3) | none | `language-scalar-native`, `lazy_arm` IR order |
| Comparing two untyped literals | `Steps.chibi` `OperatorStep` | native | none | none | `language-package-compiler-chibi-types` against stage zero, corpus `literal_comparison` |
| `int` arithmetic, negation, complement, bitwise `&`, `\|`, `^`, parentheses | `Steps.chibi` `OperatorStep` | native | other widths | none: nothing fails (D1) | `language-scalar-native`: corpus `int_operators` probes at `INT32_MIN`, -7, -1 and 0; every emitter operation at eight edge values |
| Shifts | `Steps.chibi` `ShiftStep` | stage one | unsigned counts in the subset; the emitter already lowers them | none: a count past the width gives 0 or the sign fill (D1) | `language-package-compiler-chibi-types` (`shift counts clamp`), `language-scalar-native` (emitter only); corpus `shift` rejects |
| Other integer widths, `float`, `double` | `Number.chibi`, `src/runtime/arithmetic.c` | stage one | type mapping, conversions, float ABI | none | `language-package-compiler-chibi-types` (VM only) |
| Clamping and `wrap:` conversions | `Steps.chibi` `CallStep`, `src/runtime/conversions.c` | stage one | conversion operations; a clamp must not become a plain cast | none: nothing fails (D1) | VM only |
| Locals, assignment, compound assignment | `Body.chibi`, `Steps.chibi` | stage one | slot lowering exists in `llvm_native`, not connected | none | corpus `local` rejects |
| `while`, `for`, `break`, `continue` | `Body.chibi` | stage one | loops, budget checks per iteration (M3) | budget | VM only |
| `&&`, `\|\|`, `!` short circuit | `Steps.chibi` | stage one | lazy lowering (M3) | none | corpus `logical_and` rejects |
| Recursion | `Body.chibi`, depth limit | decision (D4) | native depth policy | stack or depth counter | corpus `recursion`, `recursive_guard` reject |
| Parameter defaults | `Steps.chibi` call step | stage one | call-site fill-in | none | corpus `default_parameter` rejects |
| Named arguments | `Steps.chibi` call step | stage one | argument reordering | none | corpus `named_argument` rejects |
| Multiple and named results | `Body.chibi`, `Steps.chibi` | stage one | aggregate return ABI (M4) | struct storage | corpus `named_result` rejects |
| Records and unions | `Steps.chibi`, `src/types/record.c`, `union.c` | stage one | layout, copying, ownership (M4) | allocator, copy | corpus `struct` rejects |
| Strings | `src/types/text.c` | stage one | managed text (M4) | allocator, refcount | corpus `string_result` rejects |
| Arrays, bounds checks | `Steps.chibi`, `src/types/array.c` | stage one | managed arrays, bounds traps (M4) | allocator | corpus `array_result` rejects |
| Maps | `Maps.chibi` | stage one (restricted) | managed maps (M4) | hashing, allocator | corpus `map_result` rejects |
| References and `ref` loops | `References.chibi`, `src/execution/borrow.c` | stage one (partial) | borrow lowering (M4) | borrow checks | corpus `reference` rejects |
| Constants and constant expressions | `Compile.chibi` `FoldConstant`, exact arithmetic in `Number.chibi` | stage one | constant reads | none | `language-package-compiler-chibi-types` (`typed constants fold exactly`, rule rejections); corpus `constant` rejects |
| Imports and multi-file packages | package graph, `Compile.chibi` | stage one | module composition (M5) | linker, initialization | corpus `import`, `qualified_call` reject |
| Natives such as `length`, `format_text` | native catalog | stage one | foreign call ABI (M5) | runtime library | corpus `native_call` rejects |
| `assert` | `Steps.chibi` intrinsic | stage one | trap with message | failure reporting | corpus `assert_before` rejects |
| Generics and compile-time values | `src/language` | stage zero | needs stage-one support first | none | none |
| Exported generic specialization | none | excluded | none | none | none |
| Units | `src/language` unit analysis | stage zero | erased before lowering | none | none |
| Source diagnostics and positions | `Diagnose`, `scalar_facts` rows | native for the subset | native trap locations | debug info | corpus excerpts, `Validate` positions |
| Effects, hosts, roots | `ScalarEmits`, `src/execution/module_effects.c` | stage one | effect calls (M8) | game runtime | `ScalarEmits` rejects any |
| Entities, components, systems, queries | `Behavior.chibi`, `src/ecs` | stage one (behaviors) | game SDK ABI (M8) | ECS runtime | none |
| Waiting and tasks | `src/execution/program_tasks.c` | stage zero | coroutine lowering (M8) | scheduler | none |
| Behavior replacement and hot reload | `src/execution` | stage zero | stays on the VM; ORC/JIT is separate (obligation E) | none | none |
| Anonymous functions and closures | none | excluded | none | none | none |
| Array, union and nested-record fields in records | none | excluded | none | none | none |
| Embedded NUL and multiline strings | none | excluded | none | none | corpus `nul` rejects |

## Dependency register

These are the first-party C services and external tools the path still
needs. Each one says who uses it and what replacing it requires.

| Capability | Code | Callers | Replacement criterion |
| --- | --- | --- | --- |
| Tree-sitter parse to snapshot | `src/toolchain/compiler_syntax.c` | compiler host, `Compile`, `CollectScalar` | A native parser or a narrow generated binding that gives the same snapshot. |
| JSON decoding natives | `src/toolchain/compiler_json.c` | package compiler, host transport | Chibi decoder with the same results, or a generated binding. |
| Compiler host invocation | `src/toolchain/compiler_host.c` | `Run.ps1`, build host, C tests | Native compiler entry points (M7). |
| Bootstrap and closure artifacts | `src/toolchain/compiler_bootstrap.c`, `compiler_artifact.c` | `make bootstrap-artifact`, recovery | Native stage chain with fixed-point evidence (M7). |
| Target layout facts | `src/toolchain/compiler_target.c` | `Target` argument | Target description owned in Chibi, checked against the platform ABI (M5). |
| Bytecode and SLJIT VMs | `src/execution` | all authored tools, reference results | Stays as the reference and the editor runtime. |
| Stage-zero compiler | `src/language` | compiles the compiler; differential reference | Kept until the native bootstrap passes its gates (M7). |
| Runtime arithmetic, conversions, comparisons | `src/runtime/{arithmetic,conversions,comparisons}.c` | VMs | Native code matches them; they stay for the VMs. |
| LLVM `opt`, `llc` | external, declared | `Driver.Object`; `language-scalar-native` calls them directly | External input; identity recorded per run. |
| `ld.lld`, `llvm-nm` | external, declared | `language-scalar-native` only | Validation only; not part of the authored path. |
| Build-host file and process capabilities | `chibi-build-host` | `Driver.Object` | Native driver (M6). |

## Decision register

Each row separates the direction already set from what still needs a
decision.

| ID | Area | Direction set | Still needed | State |
| --- | --- | --- | --- | --- |
| D1 | Integer arithmetic | No integer operation fails at run time (owner, 24 October 2026, approving the integer semantics thread's contract): overflow wraps, `x / 0` is 0, `x % 0` is `x`, `MIN / -1` is `MIN`, `MIN % -1` is 0, a shift at or past the width gives 0 or the sign fill, `T(v)` between integers clamps and `T(wrap: v)` wraps. Mixed integer types, signed shift counts, inexact or out-of-range constants and a constant zero divisor are compile errors. Development builds report each site; shipped native code has no checks. | none | decided for M2 |
| D2 | Comparing two untyped literals | Follow stage zero. | none: each literal takes its spelled type. | decided in M1 |
| D3 | Floating math | Odin-style type-preserving helpers, with no blanket finite checks and no automatic double intermediates. | Typed floating widening and narrowing, where stage zero and stage one disagree. | open, needed for M2 |
| D4 | Budget and recursion | Execution stays bounded. | Where native checks go, call-depth behavior, cancellation. | open, needed for M3 |
| D5 | Native ABI | An explicit target, ownership and effect contract. `bool` stays internal for now, and probes use a validation-only adapter. | Pointer, collection and query ABI, and foreign `bool`. | open, needed for M4 and M5 |
| D6 | Failure handling | Observable errors, effects and cleanup are preserved. | Native diagnostics, unwinding, status and termination. A provisional trap is not the final answer. | open |
| D7 | Targets and defaults | Prove each target, and keep current behavior working. | The pinned LLVM (the driver measured LLVM 19's layout, and Ubuntu LLVM 18.1.3 gives the same one), the final linker, and when native output becomes a default. | open, M6 and M8 |
| D8 | Regression budget | `Run.ps1` caps at 96 sources and 180 seconds, and M1 reaches 96. | A measured larger cap, or M2 cases go in the C selector instead. | open, needed for M2 |

## Baseline and measurements

Baseline: main at `3436279` (24 October 2026), on Linux x86-64 in the cloud
container with Ubuntu LLVM 18.1.3.

- Bootstrap compiler: 12,510,053 bytes, SHA-256 `57d3e2f9…abbfd5`. Both
  recovery stages reproduce it.
- `Run.ps1` passed 79 sources (21 accepted) in 105.7 s after local
  recovery.
- `language-package-compiler-chibi-scalar`: 64 s under ASan.
- `Driver.Object` built the O0 object of a 42-returning module in 10.3 s.

After M1:

- The corpus has 96 sources, 31 accepted.
- `language-scalar-native` runs 94 of them. The two 64-deep cases go past the
  stage-zero fixture's call depth limit, so only `Run.ps1` checks their IR;
  `most_operands` also has the `Driver.Object` run below. The selector
  takes 11 s in Release and 91 s under ASan.
- `language-package-compiler-chibi-scalar` also checks 13 malformed variants
  of guarded rows and 9 of arithmetic rows.
- `Run.ps1` passed 96 sources on four crossings in 116 s, and both recovery
  stages reproduced the bootstrap compiler (the owner's independent review
  of `8968748`).
- Actual `Driver.Object` runs from that review: `min`, `clamp`, `lazy_arm`
  and `most_operands`, each built at O0 and O2 with no undefined symbols
  (8 of 8).
- `language-scalar-native` checks that a hung tool and hung generated code
  each time out within 10 seconds of a short deadline, and that SIGTERM
  cancels a run without counting as a timeout.

After M2's compile rules (24 October 2026, same container):

- Bootstrap compiler: 13,372,687 bytes, SHA-256 `5a74dccb…a7ef43`, prepared
  format 21. The checked-in format-20 compiler cannot recover the new
  sources, so `make bootstrap-artifact` built the Release tree through the
  stage-zero path, with `BUILD_HOST` naming no host.
- `language-package-compiler-chibi-bootstrap` takes 50 s in Release. Before
  the exact division stopped copying its arguments it took 842 s, 775 of
  them in stage two compiling itself (see
  [language friction](language-friction.md#stage-ones-element-stores-into-call-results-copy-arguments)).
