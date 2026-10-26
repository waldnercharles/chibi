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
| Integer and `bool` parameters, results, literals | `Compile.chibi` `Declare`, `Steps.chibi` `ConstantStep` | native | none in the subset; the entry still returns `int` | none | `language-scalar-native`, `Run.ps1` (IR only) |
| Direct same-source calls, nested and repeated | `Steps.chibi` `CalledFunction` | native | none in the subset | none | `language-scalar-native` |
| Six comparisons of every integer type, signed or unsigned | `Steps.chibi` `OperatorStep` | native | `bool` and string equality | none | `language-scalar-native`: each width module compares every pair of its type's boundary values |
| `if`, `else`, `else if`, nested and early returns; the body must end in a return | `Body.chibi` `if_statement` | native | none in the subset | none | `language-scalar-native`: the control module runs every branch of nested, shadowing and early-returning functions; `lazy_arm` IR order |
| Locals, assignment, compound assignment, branch-local scope and shadowing | `Body.chibi`, `Steps.chibi` | native | none in the subset | none | `language-scalar-native`: the control module's `Steps`, `Low`, `Far` (shift counts clamped to a `u8` local's type) and `Shadow`; corpus `local`, `helper_statement` |
| `&&`, `\|\|`, `!` short circuit, `bool` `==` and `!=` | `Steps.chibi` | native | none in the subset | none | `language-scalar-native`: every left and right value of `Both`, `Either`, `Not`, `Same`, `Differ` and `Mixed`, against both VMs; IR checks the right operand of `&&` is behind a branch; corpus `logical_and`, `bool_comparison` |
| Comparing two untyped literals | `Steps.chibi` `OperatorStep` | native | none | none | `language-package-compiler-chibi-types` against stage zero, corpus `literal_comparison` |
| Integer arithmetic of every width: `+`, `-`, `*`, `/`, `%`, negation, complement, bitwise `&`, `\|`, `^`, parentheses | `Steps.chibi` `OperatorStep` | native | none in the subset | none: nothing fails (D1) | `language-scalar-native`: each width module runs every operator on every pair of its type's boundary values; corpus `int_operators`; every emitter operation at eight `int` edge values |
| Shifts, with constant and typed unsigned counts | `Steps.chibi` `ShiftStep` | native | none in the subset | none: a count past the width gives 0 or the sign fill (D1) | `language-scalar-native`: each width shifts its boundary values by counts through and past its width, and by a constant past every width; corpus `shift` |
| `T(v)` and `T(wrap: v)` between integer types, implicit widening at returns | `Steps.chibi` `CallStep` and return step, `src/runtime/conversions.c` | native | none in the subset | none: nothing fails (D1) | `language-scalar-native`: every pair of types in both modes at every type's edges; corpus `conversion`; a clamp lowered as a plain truncation fails it |
| `number`, `f64` parameters, results, literals, `+`, `-`, `*`, `/`, comparisons, `number(v)`, `f64(v)`, float-to-integer `T(v)`, mixed integer and float operands, float-to-double widening | `Number.chibi`, `Steps.chibi` `OperatorStep`, `FloatConstantStep` and `NumberCode`, `src/execution/machine.c` | native | floating negation, which stage one rejects (see [language friction](language-friction.md#stage-one-cannot-negate-a-float)); `number` parameters until D3's migration | none: IEEE at width, no fast-math (D3) | `language-scalar-native`: float and double modules run every operator and comparison on every pair of both zeros, the extremes, the least subnormal, both infinities and NaN, and convert to and from every integer type at its edges, against C's IEEE arithmetic; `language-package-compiler-chibi-scalar` checks the emitter's encodings, `fcmp une`, saturating intrinsics and rejections |
| `switch` on an integer value, arms tried in order, default arm wherever it is, a switch with a default whose arms all return as an end | `Body.chibi` `switch_statement` | native | `switch` on unions (M4) | none | `language-scalar-native`: the switch module runs every arm of switches on `int`, `u8` and `i64` values, a default written first, call and expression arm values, a repeated value, `break` and `continue` through a switch and a nested switch, against C and both VMs; 15 malformed switch rows |
| `while`, `while (true)` as an end, `for` over `int` ranges (`..`, `..<`, `for <`), `break`, `continue` | `Body.chibi` | native | `for` over arrays, maps and strings (M4) | none: native loops run unchecked, as Odin's do; the VM keeps its instruction budget (D4) | `language-scalar-native`: the loop module runs every range form to both ends of `int`, empty ranges, `u8` bounds, nested `break` and `continue` and an endless loop left by a return, against C and both VMs; malformed loop rows; a loop that never returns fails both VMs at their budget and runs natively until its deadline stops it; `llvm_native` emits a cycle without a CHECK |
| Direct and mutual recursion | `Body.chibi`, `src/execution/call.c` depth limit | native | none in the subset | stack: no depth counter, as Rust and Odin; every function carries `"probe-stack"="inline-asm"`, so running out of stack faults in the guard page; in a host that calls `stack_overflow_init` (so far the Linux native test host), `src/host/stack_overflow.c` reports "stack overflow" and aborts, and a standalone driver-linked program ends with SIGSEGV (D4) | `language-scalar-native`: Sum, Even and Odd at every depth below 64, Fib, Nodes (recursion inside a loop) and Gcd against C and both VMs; Sum, Even and Odd 10,000 calls deep natively, and an endless recursion that aborts with "stack overflow" at O0 and O2 (the VMs run stage one's output, which has no depth limit; see [language friction](language-friction.md#stage-one-writes-no-call-depth-limit)); `llvm_native` IR checks for direct and mutual recursion and the probe attribute; corpus `recursion`, `mutual_recursion`, `recursive_guard` accepted and probed |
| Parameter defaults | `Compile.chibi` `LiteralDefault`, `Steps.chibi` call step | native | none in the subset: a default is a literal, filled in at the call site as the call step fills it | none | `language-scalar-native`: `defaults` calls functions that omit every argument, some arguments or none, with defaults of `int`, `bool`, `int8`, `byte`, `uint64`, `float` and `double`, against both VMs at O0 and O2; corpus `default_parameter` accepted and probed |
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
| D3 | Floating math | `number` is a 32-bit float everywhere, on the CPU and the GPU, and `f64` appears only where a type spells it, such as long timers and the engine's total time. Float arithmetic is IEEE at its type's width: nothing computes at reduced precision or is fast-math-like. Typed `f32` to `f64` widening may be implicit. Typed `f64` to `f32` narrowing needs an explicit `number(value)` or `f32(value)` at assignments, arguments and returns alike, and rounds to nearest, ties to even: overflow gives a signed infinity, and NaN stays NaN with no payload guarantee. Nothing panics, clamps or checks precision. Untyped literals stay contextual, and one past the `number` range needs an explicit `f64` context, such as a declaration, `f64(value)` or an argument whose typed operands select an `f64` overload. Helpers stay Odin-style, with no blanket finite checks. Game code spells `number` and `int`, and sized types take Odin's names (`i8`-`u64`, `f32`, `f64`). (Owner, 24 October 2026.) | #687 gave stage one conversions, implicit widening and typed floating constants. Project version 35 makes `number` 32-bit, renames the sized types and the conversions to `number(value)` and `f64(value)`, and rejects implicit narrowing in stage zero and stage one; its migration renames them in sources and graph pins. Changing `number` from 64 to 32 bits moves recorded results in their low bits. Native lowering of `number` and `f64` arithmetic, comparisons and conversions is in the scalar path, IEEE at width with no fast-math flags. The engine has no total-time native yet; one should return `f64`. | implemented |
| D4 | Budget and recursion | Native loops and recursion run unchecked, as Odin's and Rust's do: an endless loop runs until its host or test deadline stops it, and no `mustprogress` lets LLVM remove it. Recursion has no depth limit: every native function probes its stack (`"probe-stack"="inline-asm"`), so an overflow always faults in the guard page. A host that calls `stack_overflow_init` reports that fault as "stack overflow" and aborts, as Rust's runtime does; so far only the Linux native test host does. The VM keeps its instruction budget, which stops runaway scripts in the editor, and stage zero's VM output keeps its 64-call limit; stage one's has no limit yet. (Owner, 25 October 2026.) | Cancellation and scheduler interaction (M6). The named overflow report in production hosts: a standalone program the driver links installs no handler and ends with SIGSEGV, and no engine host runs native code yet. Threads other than the one that ran `stack_overflow_init`. macOS and Windows behavior is untested. | decided for M3 |
| D5 | Native ABI | An explicit target, ownership and effect contract. `bool` stays internal for now, and probes use a validation-only adapter. | Pointer, collection and query ABI, and foreign `bool`. | open, needed for M4 and M5 |
| D6 | Failure handling | Observable errors, effects and cleanup are preserved. | Native diagnostics, unwinding, status and termination. A provisional trap is not the final answer. | open |
| D7 | Targets and defaults | Prove each target, and keep current behavior working. | The pinned LLVM (the driver measured LLVM 19's layout, and Ubuntu LLVM 18.1.3 gives the same one), the final linker, and when native output becomes a default. | open, M6 and M8 |
| D8 | Regression budget | `Run.ps1` caps at 96 sources and 240 seconds (#683 raised it from 180), and M1 reaches 96. M2's folded-constant sources and its generated width and floating modules go in the C selector (`FOLDED_SOURCES`, `widths_agree`, `floats_agree`), and its corpus edits keep 96. | A measured larger cap. | M2 cases in the C selector; cap open |

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

- Historical author-branch artifact at `cf24c7b66`: 13,611,518 bytes,
  SHA-256 `31d5c893…0a1ab6`, prepared format 21, manifest revision
  `c7b229fab`. The author generated it from an isolated pre-#678 source
  composition. These are historical inputs and claims, not the reconciled
  candidate's artifact or acceptance evidence; that pair was not adopted.
- The separately verified S candidate `072d9ee157` retains the production
  artifact from source-equivalent `8d389925`: 13,673,789 bytes, SHA-256
  `b1d902d3…6e5c71`, with manifest SHA-256 `4256f99d…3a363e`. Current-source
  self-reproduction, normal default assets and all 17 normal bootstrap cases
  passed on that source on Mac; root separately reported matching independent
  Linux results for exact9711 (39 selectors,279 cases,902,801 assertions). The supported
  direct-host recovery and production writer avoid omitting asset generation.
  The later compound-assignment/watch source checkpoint changes two compiler
  files; the retained pair is an honest earlier input and needs a new reviewed
  generation before combined acceptance. No runtime pin changed.
- `language-package-compiler-chibi-bootstrap` takes 50 s in Release, by its
  own wall time on this container (164 s before M2). Before the exact
  division stopped copying its arguments it took 842 s, 775 of them in stage
  two compiling itself, by the selector's phase timings (see
  [language friction](language-friction.md#stage-ones-element-stores-into-call-results-copy-arguments)).
  The selector now asserts that stage two's compiler marks no function but
  the listed ones as copying its arguments. That reads the module's sharing
  metadata; it neither counts allocations nor proves the timing.
