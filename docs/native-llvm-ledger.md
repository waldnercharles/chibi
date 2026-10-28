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
  cancellation and a process-group kill. With `CHIBI_NATIVE_BUNDLE` it also
  emits each module it runs for macOS arm64 and Windows x64 and builds
  their objects, and CI's `native-targets` job links and runs them on
  `macos-15` and `windows-2022` against the same values
  ([test/native_aot/targets](../test/native_aot/targets/README.md)).
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

## Milestones

The milestones are the LLVM delivery plan's. This table records where each one
stands, [work packages](#work-packages) break the open work into pieces with
prerequisites and exit tests, and the rows under [Features](#features) record
each language feature. [How this meets PLAN.md](#how-this-meets-planmd) ties
the last milestones to the toolchain plan's acceptance and deletion rules.

| Milestone | State | What landed | Still open |
| --- | --- | --- | --- |
| M0 Inventory and baseline | done | This ledger, the dependency register and the baseline below | none |
| M1 Comparisons and guarded returns | done | The 96-source corpus, `language-scalar-native` | none |
| M2 Numeric semantics and scalar types | done | Every integer width under D1, `number` and `f64` under D3 | none |
| M3 Structured control flow and calls | in progress | Loops, integer switches, recursion under D4, literal parameter defaults (#704), multiple and named results (W2) | named arguments (W1) |
| M4 Aggregates and native ownership | in progress | Records of number and bool fields; unions of number and bool payloads; string values and equality (W3); package constants (W12); text natives (W4); the runtime context and `format_text` (W5); `assert`, whose failure fails the call from the VM (W11) | the rest of allocating text (W6), arrays (W7) and maps (W8) after pointers (#695) and collections (#694), managed fields (W9), pointers (W10), under D9 and D10 |
| M5 Modules and platform ABI | in progress | Scalar modules on macOS arm64 and Windows x64 (W15) | imports (W13), native catalog calls (W14), calls from the VM into native code (W16) |
| M6 AoT driver and diagnostics | in progress | `Driver.Object` and `Driver.Build`; `chibi build --native` (W17, #736); linking with `ld.lld` and no `cc` on Linux x86-64 (W18, #735), and cross-linking macOS arm64 and Windows x64 programs on Linux (`--target`); DWARF line tables with `-g` (W19) | workloads for strings and arrays (W20) |
| M7 Native self-hosting | not started | | the compiler-source census (W21), native stage one and two (W22), recovery without `cmake`, `clang` or `gcc` (W23) |
| M8 Game SDK and default adoption | not started | | game scenarios (W24), native game execution (W25), default adoption and removal (W26) |

## Work packages

Each package is one reviewable piece of the open work. "On main" means real
source runs natively on main; "pending PR" names an open pull request; and
"design" says whether its contract is decided (with the decision), proposed,
or open. A package marked parallel touches no file another lane is changing,
so a separate owner can take it; the others share the producer
(`Scalar.chibi`), the facts (`Facts.chibi`), the consumer and emitter
(`scalar_llvm`, `llvm_native`) and `test/language/package_compiler_chibi.c`,
which have one writer at a time, so they run in order in the LLVM lane.

| ID | Package | Prerequisites | Shared interfaces | Owner | Parallel | Exit tests | On main | Pending PR | Design |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| W1 | Named arguments | stage one's authored named arguments, which it rejects as unsupported syntax | Call rows' argument order | LLVM lane | no | corpus `named_argument` accepted; a module calling with names in every order and with defaults agrees with both VMs at O0 and O2, in the order the call step evaluates them | no | none | decided: follow the call step |
| W2 | Multiple and named results | records (done) | a results record per function with several named results | LLVM lane | no | corpus `named_result` accepted; a module returning two to four results of mixed types and reading them by name agrees at O0 and O2 | yes (#766) | none | decided: an internal LLVM struct, as records are. Several named results are a record of them; one is its value. The language has no destructuring, so callers read results by name |
| W3 | String values: literals, parameters, results, locals, `==` and `!=` | none | `native_abi.TextType`, the runtime's `pin_string` layout | LLVM lane | no | corpus `string_equality` accepted; a strings module (escapes, UTF-8, embedded NUL, empty, prefixes) agrees at O0 and O2; O2 objects have no undefined symbols; a C test pins `pin_string`'s layout | yes (#740) | none | decided: D10, by-value `{ ptr, i32 }` descriptors, no allocation |
| W4 | Text natives: byte length, byte reads, slices | W3 | native catalog input to `CollectScalar` | LLVM lane | no | 17 `core:strings` functions copied into a module (Compare, HasPrefix, Index, LastIndex, Count, EqualFold and others) agree with both VMs at O0 and O2 | yes (#764) | none | decided: a slice is a view of its source's bytes, as Odin's and Rust's string slices are; a byte outside is -1 and a slice clamps, as the runtime's natives do. `for` over a string waits: no census function uses it |
| W5 | Native runtime library and context | W3 | the hidden context parameter, `execution_native_context` (`src/execution/native.h`): a table of the runtime's operations, versioned with the export ABI (2) | LLVM lane with the runtime owner | partly: each later operation is one more context entry | a library that formats text loads with no undefined symbol and agrees with the VM in a scene; C static assertions pin the context and value layouts to `native_abi.ContextType()` and `ValueType()`; the VM frees each call's heap when it returns, so the native selectors leak nothing under ASan; an out-of-range index ends the program with its named message (with W7 and W11) | yes (#768): the context, its call-scoped heap and `format_text`; so far `language-native-calls` formats every scalar type and text through a context as the VM does and runs a C entry that formats in a Probe, `language-package-compiler-chibi-scalar` checks the format rows, 4 malformed variants, a formatted record's rejection and `llvm_native`'s format IR with 5 changed-plan rejections, and `language-package-compiler-chibi-native` runs a library that formats in a scene | none | decided: D9, D10 |
| W6 | Allocating text: `format_text`, joins, case conversion | W4, W5 | W5's context | LLVM lane | no | the same `core:strings` functions that build strings agree at O0 and O2, with no leak under ASan | partly (#768): `format_text` calls the VM's formatter through the context, so `ToLower`, `ToUpper`, `Repeat`, `Replace` and `ReplaceAll` agree with both VMs at O0 and O2 in `language-package-compiler-chibi-native`, as does `format_text` of each scalar type, widths, precision, hex, explicit indexes, escaped braces and an embedded NUL; joins wait for W7's arrays | none | decided: every text native that allocates calls the runtime, which formats as the VM does; the byte reads stay inline (W4) |
| W7 | Arrays (M4d) | #694, #695 merged; W5 | #694's copy, drop and make-unique operations; runtime spans | LLVM lane | no | copy independence, growth, empty arrays, out-of-range panics, `for` over arrays, at O0 and O2 and under ASan | no | #694, #695 (dependencies) | decided: D10 spans |
| W8 | Maps (M4e) | W7 | #694's map operations | LLVM lane | no | as W7, plus hashing and iteration order matching the VM | no | none | decided: D10 |
| W9 | Strings and arrays in records and unions (M4f) | W3, W7 | record layout with managed fields | LLVM lane | no | copy independence of nested values; corpus `struct` accepted | no | none | open: field copy and drop placement |
| W10 | Pointers (`^T`) in place of references | #695, #698 merged; W5 | #695's managed heap | LLVM lane | no | corpus `reference` replaced by a pointer case; cycles collected after native calls return | no | #695, #698 (dependencies) | decided: D10, quiescent collection |
| W11 | `assert` and native failure reporting | W5 | the context's failure slot, `execution_native_failure` (`native_abi.FailureType()`): the stable ID of the function whose assert failed and the assert's source range | LLVM lane | no | corpus `assert_before` accepted; a failing assert ends with its message and source position | yes (this PR): an Assert row lowers to a branch past a Fail, which names a failure constant in the context; each function that can fail, or calls one that can, returns its zero on failure, and its callers check the slot after the call, as Rust's `?` passes an error up; the VM then fails the call with its own assert's failure. `language-native-calls` fails a Checker behavior through a C entry, `language-package-compiler-chibi-scalar` checks the assert rows, 5 malformed variants, their IR and `llvm_native`'s Fail IR with 2 changed-plan rejections, and `language-package-compiler-chibi-native` runs a library whose asserts fail in a call and in its callee at O0 and O2 with the VM's status, message, source, module and function; `native-build` rejects an assert in a program | none | proposed: D6 |
| W12 | Package constants | none | Literal rows | LLVM lane | no | corpus `constant` accepted | yes (#751) | none | decided: a constant is its folded value |
| W13 | Imports and multi-file packages | W12 | the package graph; symbol identity | LLVM lane | no | corpus `import` and `qualified_call` accepted; two-package modules agree at O0 and O2 | no | none | open: symbol names and initialization order |
| W14 | Calls to the native catalog | W5 for managed arguments; pure scalar natives need none | a foreign call ABI per native | LLVM lane | no | corpus `native_call` replaced by pure math natives (`max`, `vec2`) agreeing at O0 and O2 | no | none | open: D5 foreign ABI |
| W15 | macOS arm64 and Windows x64 targets | none for scalars | `native_abi.Target`, the driver's layout check, CI legs | proposed lane | yes | `language-scalar-native`'s corpus and generated modules run natively on macOS arm64 and Windows x64 in CI; foreign `bool` decided | yes | none | decided: D5 foreign `bool` is `zeroext i1`; D7 admits LLVM 18's layouts |
| W16 | Calls from the VM into native code | exported scalar ABI (exists); W3 for the emitted wrappers; W5 for managed values | an exported symbol ABI and its `pin_value` conversion (`src/execution/native.h`); a loader in the player | "Call native code from running scenes" thread | yes | a running scene calls a natively compiled pure function and gets the VM's results; the editor keeps the VM. So far `language-native-calls` checks C entries with the export ABI against the VM on both backends, inspected calls staying in the VM and every rejected manifest, and `--program` checks a query predicate's bound call; `language-package-compiler-chibi-native` compiles a library source to a shared object at O0 and O2, loads it from a game's `natives/` folder and binds it to the scene's program, which then agrees with the VM, and `language-native-calls` binds manifests through the player's source registry; `native-build` exports a game with `chibi export --native` and real LLVM, keeps a rejected library in the VM with its reason, binds the shipped object in the player and matches the VM's world | the runtime bridge (#741); library sources, export wrappers and the manifest symbol (#753); the player loads `natives/` and binds each manifest to its library's module (#765); `chibi export --native` builds each library to `natives/<Name>.so` at O2 on Linux x86-64 | none | decided: D11, identity by stable function ID and exact signature; libraries for macOS and Windows follow D7 |
| W17 | The driver in `chibi build` | none (`EmitBytes` is frozen) | stable commands; driver receipts | proposed lane | yes | process tests for an explicit native mode: relocated SDK, paths with spaces, missing and wrong tools, interrupted builds, stale partials, kept prior outputs, and no silent VM fallback | yes: `chibi build --native`, one source per program; the `native-build` selector (#736) | none | decided: keep an explicit override until M8 |
| W18 | Linking without `cc` | W17 | the platform linker and runtime libraries, named in receipts | proposed lane | yes | an executable links with `cc`, `clang` and `gcc` unavailable, through a linker the receipt names | yes on Linux x86-64: `ld.lld -flavor gnu -static` and a start object shaped like Odin's no-CRT `_start` (#735); `native-build` runs it with an empty `PATH`. Linux also cross-links macOS arm64 programs with `ld.lld -flavor darwin` against a `libSystem` stub (as Zig does) and Windows x64 programs with `-flavor link` against a `kernel32` import library it writes (as rustc's `raw-dylib` does); `native-targets` runs them on `macos-15` and `windows-2022` | none | decided: rustc's direct `ld.lld` path on every target, linked from Linux (D7) |
| W19 | Debug information and native diagnostics | W3 (emitter churn) | `llvm_native`'s output | LLVM lane | no | a debugger's backtrace of a native panic shows source lines at O0 and O2 | yes: `chibi build --native -g` emits DWARF 4 line tables, rustc's `line-tables-only` shape, and IR without it is unchanged; `native-build` places a program's functions at their source lines with `llvm-symbolizer` at O0 and O2, and gdb's backtrace of a stack overflow shows them | none | decided: line tables only, as rustc; frames are named by their symbols, so source-named symbols follow in the LLVM lane; the panic report itself stays with D6 and D9 |
| W20 | Workloads and measurements | none | none | proposed lane, or the LLVM lane | yes | defined kernels (game math, compiler passes) with compile time, peak memory, size and run time recorded per milestone | in progress: four kernels and `native-workload-benchmark`, recorded under [Baseline and measurements](#baseline-and-measurements); a string kernel can follow W3; compiler-pass kernels wait for arrays (W7) | none | decided: correctness first, no promised speedups |
| W21 | Compiler-source census | none | `CollectScalar` rejections | proposed lane | yes | a selector runs the producer over each compiler function and records the first unsupported feature of each; the ledger records its counts, which fall as packages land | yes: `make native-census` writes the [census](native-compiler-census.md); counts are under [Census](#census) | none | decided |
| W22 | Native stage one and stage two | W1 to W14 as the census requires | the bootstrap generation | LLVM lane with the generation coordinator | no | a native stage one builds stage two and a checking rebuild with identical or normalized bytes, and the corpus gives the same results as the VM stages | no | none | decided: no C-frontend delegation |
| W23 | Recovery and installed proof without `cmake`, `clang` or `gcc` | W18, W22 | CI recovery job | LLVM lane | no | a CI job with those executables unavailable goes from a clean checkout through stage one and stage two and links a checked game | no | none | decided: PLAN.md's objective |
| W24 | Game scenarios | none | game fixtures | proposed lane | yes | deterministic scenarios for each feature the inventory finds in games | no | none | decided |
| W25 | Native game execution | W16, W14, W7 | W16's bridge | LLVM lane | no | representative scenes run with native functions and give the VM's traces; tasks stay in the VM (D10) | no | none | decided: D10, native functions never suspend |
| W26 | Default adoption and removal | all of the above | stable command defaults | owner decision | no | PLAN.md's deletion gate for each removed path | no | none | open: the owner decides the switch |
| W27 | Remove the producer's size limits | none | the limits in `Facts.chibi` | LLVM lane | no | each census function rejected by a limit passes the producer or stops at another package's feature, and modules past the old limits agree at O0 and O2 | yes (#754) | none | decided: removed, as Rust and Odin have none; the validator keeps structural checks |

Lanes:

- Ready now, beside the LLVM lane: W16 (VM-to-native calls),
  W17 and W18 (driver and linking), W20 (measurements), W21 (census) and W24
  (game scenarios). Each touches files the LLVM lane does not.
- In the LLVM lane, in order: W3, W12, W27, W4, W2, W5 and W11 (done), then
  W19. W6's joins follow W7. W1 follows stage one's named arguments.
- After #694 and #695 land: W7, W10, W8 and W9, in that order; W14 and W13
  follow.
- Coordinated centrally: changes to the facts schema, the prepared ABI and
  runtime symbol versions, bootstrap generations, and the default switch.

## Census

The [census](native-compiler-census.md) (W21) gives each of the 700 functions
in the self-hosted compiler's bootstrap closure the first rejection the
producer gives it, after flattening imports (W13) and constants (W12) into one
package. With `assert` (W11) and the runtime context and `format_text` (W5, #768) on top of text
natives (W4, #764), native strings (W3, #740), constants (W12, #751), native
libraries (#753), the producer's size limits removed (W27, #754) and managed
pointers in the compiler (#695, #698), 28 October 2026:

| Package | First blocker | Functions |
| --- | --- | ---: |
| W7 | array parameters, results and natives | 491 |
| none | passes the producer | 131 |
| W9 | strings and arrays in records and unions | 73 |
| none yet | other forms | 5 |

All 9 functions whose first blocker was `format_text` now pass. The closure
gained 55 functions: seven producer functions, five from multiple results (W2,
#766), one from W5 and one from W11, and 48 from managed pointers, of which 2 pass, 42 stop
at W7's arrays and 4 at W9's managed fields. The 5 others build a `string[]`
literal, which waits for W7. Of the 131 that pass, 71 need neither flattening.
In all, 370 functions name another package and 302 a package constant.

The producer checks every signature before any body, so a count shows the
first blocker only, and the next one appears when its package lands. Rerun
`make native-census` (about three minutes under ASan) after each package lands
and update this table.

## Features

| Feature | Semantic owner | State | Native gap | Runtime need | Selector and evidence |
| --- | --- | --- | --- | --- | --- |
| Integer and `bool` parameters, results, literals | `Compile.chibi` `Declare`, `Steps.chibi` `ConstantStep` | native | none in the subset; the entry still returns `int` | none | `language-scalar-native`, `Run.ps1` (IR only) |
| Direct same-source calls, nested and repeated | `Steps.chibi` `CalledFunction` | native | none in the subset | none | `language-scalar-native` |
| Six comparisons of every integer type, signed or unsigned | `Steps.chibi` `OperatorStep` | native | none in the subset (`bool` equality has its own row, strings are W3) | none | `language-scalar-native`: each width module compares every pair of its type's boundary values |
| `if`, `else`, `else if`, nested and early returns; the body must end in a return | `Body.chibi` `if_statement` | native | none in the subset | none | `language-scalar-native`: the control module runs every branch of nested, shadowing and early-returning functions; `lazy_arm` IR order |
| Locals, assignment, compound assignment, branch-local scope and shadowing | `Body.chibi`, `Steps.chibi` | native | none in the subset | none | `language-scalar-native`: the control module's `Steps`, `Low`, `Far` (shift counts clamped to a `u8` local's type) and `Shadow`; corpus `local`, `helper_statement` |
| `&&`, `\|\|`, `!` short circuit, `bool` `==` and `!=` | `Steps.chibi` | native | none in the subset | none | `language-scalar-native`: every left and right value of `Both`, `Either`, `Not`, `Same`, `Differ` and `Mixed`, against both VMs; IR checks the right operand of `&&` is behind a branch; corpus `logical_and`, `bool_comparison` |
| Comparing two untyped literals | `Steps.chibi` `OperatorStep` | native | none | none | `language-package-compiler-chibi-types` against stage zero, corpus `literal_comparison` |
| Integer arithmetic of every width: `+`, `-`, `*`, `/`, `%`, negation, complement, bitwise `&`, `\|`, `^`, parentheses | `Steps.chibi` `OperatorStep` | native | none in the subset | none: nothing fails (D1) | `language-scalar-native`: each width module runs every operator on every pair of its type's boundary values; corpus `int_operators`; every emitter operation at eight `int` edge values |
| Shifts, with constant and typed unsigned counts | `Steps.chibi` `ShiftStep` | native | none in the subset | none: a count past the width gives 0 or the sign fill (D1) | `language-scalar-native`: each width shifts its boundary values by counts through and past its width, and by a constant past every width; corpus `shift` |
| `T(v)` and `T(wrap: v)` between integer types, implicit widening at returns | `Steps.chibi` `CallStep` and return step, `src/runtime/conversions.c` | native | none in the subset | none: nothing fails (D1) | `language-scalar-native`: every pair of types in both modes at every type's edges; corpus `conversion`; a clamp lowered as a plain truncation fails it |
| `number`, `f64` parameters, results, literals, `+`, `-`, `*`, `/`, comparisons, `number(v)`, `f64(v)`, float-to-integer `T(v)`, mixed integer and float operands, float-to-double widening | `Number.chibi`, `Steps.chibi` `OperatorStep`, `FloatConstantStep` and `NumberCode`, `src/execution/machine.c` | native | floating negation, which stage one rejects (see [language friction](language-friction.md#stage-one-cannot-negate-a-float)); `number` parameters until D3's migration | none: IEEE at width, no fast-math (D3) | `language-scalar-native`: float and double modules run every operator and comparison on every pair of both zeros, the extremes, the least subnormal, both infinities and NaN, and convert to and from every integer type at its edges, against C's IEEE arithmetic; `language-package-compiler-chibi-scalar` checks the emitter's encodings, `fcmp une`, saturating intrinsics and rejections |
| `switch` on an integer value, arms tried in order, default arm wherever it is, a switch with a default whose arms all return as an end | `Body.chibi` `switch_statement` | native | none in the subset (switches on union variables are native, see the unions row) | none | `language-scalar-native`: the switch module runs every arm of switches on `int`, `u8` and `i64` values, a default written first, call and expression arm values, a repeated value, `break` and `continue` through a switch and a nested switch, against C and both VMs; 15 malformed switch rows |
| `while`, `while (true)` as an end, `for` over `int` ranges (`..`, `..<`, `for <`), `break`, `continue` | `Body.chibi` | native | `for` over arrays, maps and strings (M4) | none: native loops run unchecked, as Odin's do; the VM keeps its instruction budget (D4) | `language-scalar-native`: the loop module runs every range form to both ends of `int`, empty ranges, `u8` bounds, nested `break` and `continue` and an endless loop left by a return, against C and both VMs; malformed loop rows; a loop that never returns fails both VMs at their budget and runs natively until its deadline stops it; `llvm_native` emits a cycle without a CHECK |
| Direct and mutual recursion | `Body.chibi`, `src/execution/call.c` depth limit | native | none in the subset | stack: no depth counter, as Rust and Odin; every function probes its frame (`"probe-stack"="inline-asm"`, or a call to `__chkstk` on Windows, as MSVC and rustc do), so running out of stack faults in the guard page; in a host that calls `stack_overflow_init` (so far the Linux native test host), `src/host/stack_overflow.c` reports "stack overflow" and aborts, and a standalone driver-linked program ends with SIGSEGV (D4) | `language-scalar-native`: Sum, Even and Odd at every depth below 64, Fib, Nodes (recursion inside a loop) and Gcd against C and both VMs; Sum, Even and Odd 10,000 calls deep natively, and an endless recursion that aborts with "stack overflow" at O0 and O2 (the VMs run stage one's output, which has no depth limit; see [language friction](language-friction.md#stage-one-writes-no-call-depth-limit)); `llvm_native` IR checks for direct and mutual recursion and the probe attribute; corpus `recursion`, `mutual_recursion`, `recursive_guard` accepted and probed |
| Parameter defaults | `Compile.chibi` `LiteralDefault`, `Steps.chibi` call step | native | none in the subset: a default is a literal, filled in at the call site as the call step fills it | none | `language-scalar-native`: `defaults` calls functions that omit every argument, some arguments or none, with defaults of `int`, `bool`, `i8`, `u8`, `u64`, `number` and `f64`, against both VMs at O0 and O2; corpus `default_parameter` accepted and probed |
| Named arguments | `Steps.chibi` call step | stage one | argument reordering (W1) | none | corpus `named_argument` rejects |
| Multiple and named results: writable named results starting at zero, `return (a, b)` assigning each in order, a bare return or the end returning them, reads by name from a call or a local, one named result as its call's value | `Body.chibi` prologue and `NamedResultsStep`, `Steps.chibi` `NamedReturnStep` and member step | native: several results are a record of them, returned as an LLVM value; one is its value | named results of records beside other results, which a record of records would need | none | `language-scalar-native`: `named_results` returns two to four `int`, `bool`, `u8`, `i64`, `number` and `f64` results by values, bare returns and the end, and one `int` or record result, against both VMs at O0 and O2; corpus `named_result` accepted; `language-package-compiler-chibi-scalar` rejects a record among several results |
| Records of number and bool fields: literals in any field order, copies, field reads, field and compound field writes, record parameters, results and locals | `Steps.chibi` struct literal and member steps, `src/types/record.c` | native | none in the subset: a record is an LLVM value of its fields in declaration order, so a copy is a value and needs no allocator | none | `language-scalar-native`: the records module builds, copies, writes and passes `number`, `bool`, `u8`, `f64` and `i64` fields through calls, nested calls and a loop, and `nested_records` passes one through 50 nested calls, against both VMs at O0 and O2; `language-package-compiler-chibi-scalar` checks the record rows and 22 malformed variants; `llvm_native` IR checks and 8 changed-plan rejections; corpus `struct` rejects a `string` field |
| Unions of number and bool payloads: variants with and without payloads made, passed, returned and assigned, `is` and `is not` tests, switches on a union variable whose arms bind payloads, with a default arm or covering every variant | `Steps.chibi` union make and `MatchStep`, `Body.chibi` switch arms, `IsStep`, `src/types/union.c` | native | none in the subset: a union is an LLVM value of its int tag then all its variants' payloads, so a copy is a value and needs no allocator | none | `language-scalar-native`: the unions module makes, passes, returns, assigns, tests and switches on `number`, `int`, `bool`, `f64` and `u8` payloads, in a loop with `break` and `continue`, against both VMs at O0 and O2; `language-package-compiler-chibi-scalar` checks the union rows, 25 malformed variants and the subset's union rejections |
| Unions with string, array, union or record payloads, `is` bindings, switches on union expressions, and records with string, array, union or record fields | `Steps.chibi`, `src/types/record.c`, `union.c` | stage one | managed fields (M4) | allocator, copy | corpus `struct` rejects; `language-package-compiler-chibi-scalar` rejects each union form |
| Strings | `src/types/text.c`, `Facts.chibi` `StringType` | native: literals, parameters, results, locals, assignments, defaults, `==`/`!=`, the text natives `text_byte_length`, `text_byte_at` and `text_slice` (W4), and `format_text` through the runtime context (W5) | `for` over a string, joins (W6 after W7), strings in records and unions (W9), and exported string signatures (D5) | the context's `format_text` (W5): its text lives until the call from the VM returns | corpus `string_equality` accepted and `string_result` rejects a string entry; `language-scalar-native`'s strings module (escapes, UTF-8, an embedded NUL, empty text, prefixes, a default and a loop) at O0 and O2 against both VMs, with no undefined symbol; `language-package-compiler-chibi-scalar` checks the string rows and 10 malformed variants, and rejects a string record field and union payload, and `llvm_native`'s text constants, comparison and 5 changed-plan rejections; the same selector checks the text rows and 11 malformed variants, a named argument and another native, and `llvm_native`'s text reads and 7 changed-plan rejections; `language-package-compiler-chibi-native` runs `core:strings` functions over byte reads inside and outside a string, slices clamped at each end and slices of slices; a C static assertion pins `pin_string` to `native_abi.TextType()` |
| Arrays, bounds checks | `Steps.chibi`, `src/types/array.c` | stage one | managed arrays, bounds traps (M4) | allocator | corpus `array_result` rejects |
| Maps | `Maps.chibi` | stage one (restricted) | managed maps (M4) | hashing, allocator | corpus `map_result` rejects |
| Scoped pointer parameters | `References.chibi`, `src/execution/borrow.c` | stage one (partial) | borrow lowering (M4) | borrow checks | corpus `reference` rejects |
| Constants and constant expressions | `Compile.chibi` `FoldConstant`, exact arithmetic in `Number.chibi` | native: a package constant's read and a constant expression are literal rows of their folded values | none in the subset | none | `language-package-compiler-chibi-types` (`typed constants fold exactly`, rule rejections); corpus `constant` accepted; `language-scalar-native`'s `constants` module (int, `number`, `f64` and `u64` constants, out-of-order declarations and a shift count) at O0 and O2 against both VMs |
| Imports and multi-file packages | package graph, `Compile.chibi` | stage one | module composition (M5) | linker, initialization | corpus `import`, `qualified_call` reject |
| Natives such as `length` | native catalog | stage one | foreign call ABI (M5) | runtime context | corpus `native_call` rejects |
| `assert` | `Steps.chibi` intrinsic | native: a statement whose false condition fails the call from the VM with the VM's assert failure (W11) | an assert in a program, which `chibi build --native` links with no runtime to report it | the context's failure slot (W11) | corpus `assert_before` accepted, with named and positional arguments; `language-native-calls` and `language-package-compiler-chibi-native` fail a behavior through a library's assert and its callee's at O0 and O2 as the VM does |
| Generics and compile-time values | `src/language` | stage zero | needs stage-one support first | none | none |
| Exported generic specialization | none | excluded | none | none | none |
| Units | `src/language` unit analysis | stage zero | erased before lowering | none | none |
| Source diagnostics and positions | `Diagnose`, `scalar_facts` rows | native for the subset | native trap locations | debug info | corpus excerpts, `Validate` positions |
| Effects, hosts, roots | `ScalarEmits`, `src/execution/module_effects.c` | stage one | effect calls (M8) | game runtime | `ScalarEmits` rejects any but a record's construction, copies and field writes, a union's construction, tests and payload reads, and their roots |
| Entities, components, systems, queries | `Behavior.chibi`, `src/ecs` | stage one (behaviors) | game SDK ABI (M8) | ECS runtime | none |
| Waiting and tasks | `src/execution/program_tasks.c` | stage zero | coroutine lowering (M8) | scheduler | none |
| Behavior replacement and hot reload | `src/execution` | stage zero | stays on the VM; ORC/JIT is separate (obligation E) | none | none |
| Anonymous functions and closures | none | excluded | none | none | none |
| Array, union and nested-record fields in records | none | excluded | none | none | none |
| NUL bytes in source and multiline strings | none | excluded | none | none | corpus `nul` rejects; a `\u0000` escape is an ordinary byte of a native string |

## How this meets PLAN.md

[PLAN.md](../PLAN.md) owns the toolchain milestones. Its M5 self-hosts the
compiler on the VMs; this ledger's M7 makes that compiler native. Its
acceptance rules bind the last three milestones here:

- **M6** delivers PLAN.md's stable commands natively: `chibi build`, `run`,
  `test` and `export` keep their meanings, with an explicit native mode and
  override until M8 (W17). Exit: W17's process tests and W18's link with no
  `cc`.
- **M7** meets the objective's acceptance requirement: "Chibi must compile
  Chibi and drive build/link without requiring `cmake`, `clang`, or `gcc`
  executables", with the installed, relocated, clean-checkout proof running
  stage one and stage two and linking a checked game (W23). It also meets
  the definition of complete's bootstrap rule: a checksummed recovery
  artifact, and the old C compiler off the normal path (W22).
- **M8** meets the definition of complete for games: the installed SDK builds
  and exports a game natively, and remaining native code is a narrow binding
  with ownership tests at its boundary (W25). Switching defaults and removing
  an old path follow PLAN.md's M10 deletion gate (W26): name the replacement,
  enumerate every repository, SDK, CI, release and application caller, run
  the equivalence checks in [the toolchain inventory](toolchain-inventory.md),
  and keep the documented recovery path for the agreed migration period.
  The stage-zero C compiler and the VMs stay as recovery and editor runtimes
  until then (see the dependency register).

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
| LLVM `opt`, `llc` | external, declared | `Driver.Object`, `Driver.Build`, `chibi build --native`; `language-scalar-native` calls them directly | External input; identity recorded per run. |
| `ld.lld` | external, declared | `Driver.Build`, `chibi build --native`, `language-scalar-native` | The linker for every target (W18): flavor `gnu` for Linux x86-64, `darwin` for macOS arm64 and `link` for Windows x64, which also writes the `kernel32` import library; identity recorded per run. |
| `llvm-nm` | external, declared | `language-scalar-native` only | Validation only; not part of the authored path. |
| `llvm-symbolizer` | external, declared | `native-build` only | Validation only: checks `-g` line tables (W19). |
| Build-host file and process capabilities | `chibi-build-host` | `Driver.Object`, `Driver.Build`, `chibi build --native` | Runs the native driver (W17). |
| `clang -dynamiclib` (macOS), `lld-link` (Windows) | external, declared | `test/native_aot/targets/run.c` only | Validation only: they link the bundle's objects as shared libraries to call them. Programs link with `ld.lld` (W18). |

## Decision register

Each row separates the direction already set from what still needs a
decision.

| ID | Area | Direction set | Still needed | State |
| --- | --- | --- | --- | --- |
| D1 | Integer arithmetic | No integer operation fails at run time (owner, 24 October 2026, approving the integer semantics thread's contract): overflow wraps, `x / 0` is 0, `x % 0` is `x`, `MIN / -1` is `MIN`, `MIN % -1` is 0, a shift at or past the width gives 0 or the sign fill, `T(v)` between integers clamps and `T(wrap: v)` wraps. Mixed integer types, signed shift counts, inexact or out-of-range constants and a constant zero divisor are compile errors. Development builds report each site; shipped native code has no checks. | none | decided for M2 |
| D2 | Comparing two untyped literals | Follow stage zero. | none: each literal takes its spelled type. | decided in M1 |
| D3 | Floating math | `number` is a 32-bit float everywhere, on the CPU and the GPU, and `f64` appears only where a type spells it, such as long timers and the engine's total time. Float arithmetic is IEEE at its type's width: nothing computes at reduced precision or is fast-math-like. Typed `f32` to `f64` widening may be implicit. Typed `f64` to `f32` narrowing needs an explicit `number(value)` or `f32(value)` at assignments, arguments and returns alike, and rounds to nearest, ties to even: overflow gives a signed infinity, and NaN stays NaN with no payload guarantee. Nothing panics, clamps or checks precision. Untyped literals stay contextual, and one past the `number` range needs an explicit `f64` context, such as a declaration, `f64(value)` or an argument whose typed operands select an `f64` overload. Helpers stay Odin-style, with no blanket finite checks. Game code spells `number` and `int`, and sized types take Odin's names (`i8`-`u64`, `f32`, `f64`). (Owner, 24 October 2026.) | #687 gave stage one conversions, implicit widening and typed floating constants. Project version 35 makes `number` 32-bit, renames the sized types and the conversions to `number(value)` and `f64(value)`, and rejects implicit narrowing in stage zero and stage one; its migration renames them in sources and graph pins. Changing `number` from 64 to 32 bits moves recorded results in their low bits. Native lowering of `number` and `f64` arithmetic, comparisons and conversions is in the scalar path, IEEE at width with no fast-math flags. The engine has no total-time native yet; one should return `f64`. | implemented |
| D4 | Budget and recursion | Native loops and recursion run unchecked, as Odin's and Rust's do: an endless loop runs until its host or test deadline stops it, and no `mustprogress` lets LLVM remove it. Recursion has no depth limit: every native function probes its stack (`"probe-stack"="inline-asm"`, or `__chkstk` on Windows), so an overflow always faults in the guard page. A host that calls `stack_overflow_init` reports that fault as "stack overflow" and aborts, as Rust's runtime does; so far only the Linux native test host does. The VM keeps its instruction budget, which stops runaway scripts in the editor, and stage zero's VM output keeps its 64-call limit; stage one's has no limit yet. (Owner, 25 October 2026.) | Cancellation and scheduler interaction (M6). The named overflow report in production hosts: a standalone program the driver links installs no handler and ends with SIGSEGV, and no engine host runs native code yet. Threads other than the one that ran `stack_overflow_init`. On macOS and Windows, recursion below 64 calls and a program with a 256 KiB frame run in CI; deep recursion and overflow are untested. | decided for M3 |
| D5 | Native ABI | An explicit target, ownership and effect contract. Probes use a validation-only adapter, and internal ownership follows D10. Inside a module `bool` is a plain `i1`. An exported or foreign `bool` parameter or result is `zeroext i1`, as clang passes C's `_Bool` on Linux, macOS and Windows and as rustc and Odin pass theirs (owner, 27 October 2026). Windows code that uses floating point references `_fltused`, which the MSVC runtime defines; a program linked without it must define it. | The exported and foreign ABI of aggregates and strings, and the calling conventions they need (W14, W16). | open, needed for M5 |
| D6 | Failure handling | Observable errors, effects and cleanup are preserved. Proposed with W11, awaiting the owner: a failed assert fails the call from the VM, as the VM's own assert fails a behavior call, rather than aborting as Odin does. Native code stores the failure in the context and returns through every native frame, each caller checking the slot after a call of a function that can fail; native functions hold nothing to clean up, since the call's heap is freed when it returns. | The owner's choice; failures other than asserts (a `format_text` error still aborts, and D9's indexes); a native program's report, with W17 and W19. | proposed |
| D7 | Targets and defaults | Prove each target, and keep current behavior working. `native_abi` admits `x86_64-unknown-linux-gnu`, `arm64-apple-macosx11.0.0` (versioned, so the object records its platform, as Odin's default does) and `x86_64-pc-windows-msvc`, and `Driver.Target` admits each with the layout LLVM 18 gives it; `Driver.Object` emits objects for all three, and `Driver.Build` links programs for all three on Linux with `ld.lld` (W18). Line tables (W19) and libraries (W16) are Linux x86-64 only, and the emitter rejects them for the other two. The x86-64 layouts are the same from LLVM 18 to 21; the arm64 one changes with the major, so another major's arm64 layout is rejected. | The pinned LLVM for macOS and Windows hosts (CI builds their objects and programs with Linux's LLVM 18) and when native output becomes a default. | open, M6 and M8 |
| D8 | Regression budget | `Run.ps1` caps at 96 sources and 240 seconds (#683 raised it from 180). M1 reached 96, and W27 brought the corpus to 90 by dropping the cases on each side of the size limits. M2's folded-constant sources and its generated width and floating modules go in the C selector (`FOLDED_SOURCES`, `widths_agree`, `floats_agree`). | A measured larger cap. | M2 cases in the C selector; cap open |
| D9 | Out-of-range indexes | Native code panics, as Rust's and Odin's does: it ends the program with a named message. The VM keeps its recoverable error for the editor. The owner declined reading a zero value and dropping the write. (Owner, 26 October 2026.) | The message and the host hook it reports through, with D6. | decided for M4 |
| D10 | Native ownership | Native code has no ownership scheme of its own. It calls the runtime's value operations (copy, drop, make unique, allocate) at the copies and drops the compiler certifies, through a hidden first context parameter, `execution_native_context`, a table of the runtime's operations, as Odin's context carries its allocator. A library therefore leaves no symbol for its host to define: a `chibi_rt_*` symbol, the first plan, would need the player to export it to `dlopen`ed objects on Linux, macOS and Windows alike (changed with W5, 27 October 2026). It reads strings inline and arrays only through runtime spans. The context's frame carries the heap and the failure slot (W11); what the runtime gives a call lives until the call from the VM returns, and native functions never suspend, so the heap collects only while execution is quiescent. | Lands with M4c; arrays and maps wait for #695 and #694. | set for M4c |
| D11 | Calls from the VM into native code | A native library source (`Name :: library { ... }` of plain functions) compiles to an object that exports one symbol, `chibi_natives_<Name>`, a manifest of its pure scalar functions; every function is internal, and one wrapper per word signature reads and writes the words, as rustc and Odin export a crate's or package's API and keep the rest internal. Each export names a function by its stable ID (`script_catalog_id("symbol", "<library path>:1:<name>:callable")`, which its prepared module records) and its exact parameter and result types, and its entry reads and writes 64-bit words: integers sign- or zero-extended, `bool` as 0 or 1, `number` and `f64` as their bits. `execution_natives_bind` (`src/execution/native.c`) binds an export only to the effect-free authored function with that ID and signature, and rejects the whole manifest otherwise. A program given the table runs bound synchronous calls natively; tasks, inspected calls and unbound functions stay in the VM, and the editor binds nothing. The player loads every `<Library>` object in `natives/` under the resource root and binds its manifest to the module that defines its first export, once, after publication and before the first scene (`source_bind_natives`). `chibi export --native` builds the objects: the native driver's `Libraries` entry compiles each library source with `CollectScalar` and the emitter's `EmitLibrary`, optimizes at O2 and links a shared object with `ld.lld`; a library the emitter rejects stays in the VM with its diagnostic, and any other failure fails the export. Under export ABI 2 (W5) every entry takes the call's runtime context first, and `execution_natives_new` takes the VM's operations from the host, which owns the runtime. The program's context layout and the prepared ABI do not change. | Records, strings and arrays at the boundary (W5), and libraries for macOS and Windows (D7). | implemented on Linux x86-64 |

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

After M3 and M4's records and unions (25 and 26 October 2026, same container):

- `Run.ps1` passed 96 sources on four crossings in 211 s locally with M3
  and native recursion, and in 255 s with records. Main took 241 s locally
  and 177 s in CI, so this container runs about 1.36 times slower than CI.

After string interning (#719, 27 October 2026, same container):

- #714 raised the bound on bootstrap closures to 32 MiB. #719 then stores
  each metadata string once per module in prepared format 22, and the
  bootstrap closure went from 17,139,143 to 9,112,577 bytes, about half the
  16 MiB result bound.
- `language-package-compiler-chibi-bootstrap` takes 62 to 64 s in Release,
  as before interning. Peak memory went from 4,389 to 4,352 MiB, and from
  2,025 to 1,964 MiB for a bootstrap load.
- `Run.ps1` passed 96 sources on four crossings in 195 s locally, and both
  recovery stages reproduced the checked-in closure.
- The runtime pin is `compiler-runtime-prepared22-7b151ebc5`. The `ci-seed`
  label built, published and pinned it on the PR branch (see
  [starting CI with labels](testing.md#starting-ci-with-labels)).

W20's workloads (27 October 2026, main at `e66e308`, before W3's strings,
same container, Ubuntu LLVM 18.1.3, Release launcher and hosts):
`native-workload-benchmark` builds the four kernels in
`test/native_aot/workloads` with `chibi build --native`, and checks each
program's status against the one the VM gives. These are that commit's
numbers; a later milestone re-runs the selector and records its own.

| Kernel | O0 build | O2 build | O0 size | O2 size | O0 run | O2 run |
| --- | --- | --- | --- | --- | --- | --- |
| `integrate` | 3.20 s | 3.35 s | 6,720 B | 1,544 B | 719 ms | 88.1 ms |
| `collide` | 3.49 s | 3.38 s | 5,376 B | 2,432 B | 3,932 ms | 98.0 ms |
| `lex` | 3.31 s | 3.14 s | 4,656 B | 1,312 B | 383 ms | 18.4 ms |
| `recurse` | 3.15 s | 3.09 s | 2,192 B | 1,264 B | 63.5 ms | 7.8 ms |

- A build's time is almost all fixed cost: about 3.0 s pass before `opt`
  starts, while the build host compiles the driver and the compiler host
  collects facts and emits IR. `opt`, both `llc` runs and `ld.lld` take
  about 0.1 s together.
- The first build in a folder takes 7.0 to 7.9 s, because it also compiles
  the emitter closure.
- The peak resident memory of the largest process is 245 MiB for every
  kernel and mode, so it doesn't depend on the kernel.
- Programs are static, have no C library, and stay under 7 KB.

After native strings (W3, #740, 27 October 2026, same container):

- The bootstrap closure went from 9,112,577 to 9,120,544 bytes, and
  `language-package-compiler-chibi-bootstrap` still takes 62 s in Release.
- `Run.ps1` now covers 96 sources with 52 accepted. On this container it no
  longer finishes inside its 240 s budget, and neither does main: main
  reached 943 host invocations in 240 s and this branch 922, so strings cost
  about 2% more. CI, which runs about 1.36 times faster, ran main in 177 s.
- `language-package-compiler-chibi-native` takes about 4 minutes, as before.
