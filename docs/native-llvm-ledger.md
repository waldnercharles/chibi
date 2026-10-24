# Native LLVM ledger

This ledger tracks how far ordinary Chibi source reaches native code through
LLVM. Each feature lists the code that owns its meaning, what is still missing
natively, the runtime it needs, any open decision, and the selector that
checks it. A feature is native only when real source reaches an object through
`CollectScalar`, `scalar_llvm` and `Driver.Object`. A hand-written plan that
`llvm_native` emits does not count.

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
| `int` and `bool` parameters, results, literals | `Compile.chibi` `Declare`, `Steps.chibi` `ConstantStep` | native | none in the subset | none | `language-scalar-native`, `Run.ps1` |
| Direct same-source calls, nested and repeated | `Steps.chibi` `CalledFunction` | native | recursion | none | `language-scalar-native` |
| Six signed `int` comparisons | `Steps.chibi` `OperatorStep` | native | other widths, unsigned, `bool` and string equality | none | `language-scalar-native` probes at `INT32_MIN`/`INT32_MAX` |
| One guard with a returning arm and a final return | `Body.chibi` `if_statement` | native | `else`, several guards, general control flow (M3) | none | `language-scalar-native`, `lazy_arm` IR order |
| Comparing two untyped literals | `Steps.chibi` `OperatorStep` | native | none | none | `language-package-compiler-chibi-types` against stage zero, corpus `literal_comparison` |
| Arithmetic, negation, bitwise, shifts | `Steps.chibi` `OperatorStep`, `Number.chibi` | decision (D1) | overflow and division contract, all widths | trap or wrap policy | corpus `arithmetic` rejects today. It is M2's first fixture. |
| Other integer widths, `float`, `double` | `Number.chibi`, `src/runtime/arithmetic.c` | stage one | type mapping, conversions, float ABI | none | `language-package-compiler-chibi-types` (VM only) |
| Checked, `wrap:` and `clamp:` conversions | `Number.chibi`, `src/runtime/conversions.c` | stage one | conversion operations; must not become plain casts | trap path | VM only |
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
| Constants | `Compile.chibi` constant folding | stage one | constant reads | none | corpus `constant` rejects |
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
| LLVM `opt`, `llc` | external, declared | `Driver.Object`, `language-scalar-native` | External input; identity recorded per run. |
| `ld.lld`, `llvm-nm` | external, declared | `language-scalar-native` only | Validation only; not part of the authored path. |
| Build-host file and process capabilities | `chibi-build-host` | `Driver.Object` | Native driver (M6). |

## Decision register

Each row separates the direction already set from what still needs a
decision.

| ID | Area | Direction set | Still needed | State |
| --- | --- | --- | --- | --- |
| D1 | Integer arithmetic | Checked and wrapping arithmetic, approved but not implemented. | The exact operations and default syntax, failure behavior, and how existing projects migrate. Docs describe wrapping, and `llvm_native` wraps with provisional traps. Neither one decides it. | open, needed for M2 |
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
  stage-zero fixture's call depth limit and run in `Run.ps1` instead. It
  takes 11 s in Release and 91 s under ASan.
- `language-package-compiler-chibi-scalar` also checks 13 malformed variants
  of guarded rows.
