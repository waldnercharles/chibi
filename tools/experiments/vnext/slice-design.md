# Bounded typed-body slice

Status: caller fixtures and boundary design, not a compiler implementation or an accepted native result. Source inspection uses `02c86287430def964445ec90185961e20319536d`. The external-backend-first plan governs this experiment; Cranelift leads the candidates but is not selected by an unmeasured preference. A custom emitter is not the default.

## What runs today

Run `python3 tools/experiments/vnext/slice_check.py` from the repository root. It checks manifest/source bookkeeping and compiles/runs the C fixture with an existing `cc`, entirely in a temporary directory. It never launches Chibi, downloads dependencies or attempts builder recovery. A successful result explicitly says `chibi_parse_typecheck_jit_aot: not_run`.

The host probe checks its own i64 extremes, bool results, eight-argument function, wrapping position-weighted sum and observation hook. This is **C-to-C fixture validation only**, not proof of a Chibi C ABI, parser, typed IR, JIT, AOT or compilation throughput. No reference interpreter has been added: there is no implemented typed-body consumer to justify maintaining another engine.

`slice_cases.json` is the single corpus registry. Each case supplies an explicit source closure relative to this directory, the project root for `project:` imports, and either call/argument/result triples or a rejection stage, reason and source anchor. Each negative case is a separate compilation, not one package containing all rejected files. Diagnostic reasons are categories, not promises of exact existing wording. The source anchor is for location checking, not a parser substitute. Values are exact integers or bools; a future JavaScript consumer must not round i64 JSON numbers through `Number`.

The first file is the entry package. Call entries name `pub` functions in that package, not a new source export syntax. Both native harnesses must call those functions using their actual typed signatures. A fixture may be correct even when its result is not an OS exit code. Preserve full-width results in the test transport.

## Caller coverage

- Scalars: parameter/result i64 boundaries, wrapping runtime addition and multiplication, bool negation, forward resolution, comparisons, both branches and a mutable-local join.
- Packages: explicit import alias, public call and private-member rejection.
- Foreign calls: C `int64_t` and `_Bool`, eight position-weighted integer arguments crossing register/stack boundaries on x86-64, signed extremes, left-to-right argument evaluation and short-circuit effect suppression.
- Rejections: argument type/count, condition/result type, missing return, unknown name, private member, duplicate parameter, out-of-range literal, unsupported array and unresolved foreign symbol.

The array case is an explicitly unsupported **slice** feature, not a declaration that arrays are removed from Chibi. Tasks, pointers, records, generic/comptime execution, loops, division/remainder/shifts, implicit conversions, default/named arguments and retained state are also outside this first slice. Reject unsupported input before emission; never silently run it through the old compiler. Current syntax is retained. All 15 registered source files have now passed the retained parser in the [separate parser experiment](frontend/README.md). They have not passed Chibi typechecking or source-to-native execution.

Runtime add/multiply wrap at 64 bits and comparisons are signed. Bool is distinct from i64 and there is no implicit truthiness. This follows the current numeric contract for these operations, rather than adopting C signed-overflow behavior. Literal range errors remain compile-time errors. The wider constant/runtime arithmetic-policy decision remains open; this slice does not settle it. Source: [numeric contract](../../../docs/package-numeric-contract.md), [language reference](../../../docs/code-language.md#types).

## Reuse boundary in the current source

All paths below are relative to the repository root.

1. `tools/compiler/source_tokenizer/Tokenizer.chibi:156,188` already returns parallel indexed token facts. `source_parser/Parser.chibi:17,28` calls it and produces `source_parser/Tree.chibi:46` raw nodes/fields/flags. Retain this authored parser, including byte ranges and diagnostics. Do not write a Python/C/Rust parser for the experiment. Preserve existing sentinel conventions at the boundary, then translate indices once.
2. `package_syntax/Extract.chibi:63`, `package_routes/Routes.chibi:165` and `package_graph/Graph.chibi:120` separate headers, routing and dependency order. The graph explicitly bounds inputs to 256 files. That is acceptable for this 15-file corpus but must be removed or recorded before a real compiler workload, not mistaken for scalable proof.
3. `signature_reader/Reader.chibi:515,788` already reads type/signature syntax from the raw tree. `callable_headers/Headers.chibi:678` registers headers separately from bodies. These are the preferred retained signature seam: admit all explicitly typed scalar headers before any body, assign dense function/type/name IDs, then resolve calls against that immutable catalog. Precheck this slice's allowed declaration forms; no body-dependent signatures are admitted.
4. Do not call every existing broad registration path indiscriminately. `callable_headers/Headers.chibi:100` (`Refresh`) rebuilds several catalogs; repeated whole-catalog rebuilds need a batch publication path before a large workload. `package_compiler/Compile.chibi:2199` computes `ScopedParameters` before its `FunctionSignature` calls, which is unnecessary for this pointer-free slice. `FunctionSignature:2877` remains a source/test reference, not an additional signature authority on the new path.
5. The replacement begins at body checking. `package_compiler/Body.chibi:2596` (`EmitBody`) simultaneously checks and produces prepared slots/cells/code/roots; `Scalar.chibi:4368` calls it, then performs another lowering. Move retained scalar typing/resolution decisions into the new typed-body producer. It must **not** require prepared emission first or rediscover operands from source in a backend.
6. `compiler_facts/Facts.chibi` is a sparse facts view; `scalar_lowering/Lower.chibi` targets prepared storage and has a separate expression schema. Neither becomes canonical by renaming it. Their tests/rules can be migrated; their output is not another mandatory hop. `native_abi/Abi.chibi:101` expressly warns that an internal bool type is not a foreign ABI promise.

One future implementation path is therefore: retained parser and package/header resolution → one scalar typed-body producer → verifier → target ABI lowering → Cranelift function construction → JIT or object output. The verifier and both consumers see the same semantic decisions. Source access after typing is solely for diagnostics. Do not use a second parser, prepared VM execution or an LLVM text round trip as an unnoticed prerequisite.

## Canonical body contract, version 0 proposal

This is a bounded implementation contract to validate, not a public file format or a precommitted large IR framework. Use compile-owned contiguous arrays and typed 32-bit indices with zero meaning absent. Reject count overflow at admission. Reserve from parsed counts; release compile/scratch storage after finalization. Backend code/object owners have separate lifetimes.

| Table | Essential facts and proposed budget |
| --- | --- |
| Function | Module/name/signature IDs, body block span, source origin. Internal versus foreign membership is separate. |
| Signature | Ordered parameter type span and one result type; only i64 and bool. No inferred waits or body-derived calling convention. |
| Value | 8 bytes: type ID and defining instruction or block-parameter index, with the definition class encoded. |
| Instruction | 24 bytes: opcode, result ID, two operands, payload index and origin ID. Literal i64 bits and variable call operands live in side arrays. |
| Block | Ordered parameter/value span, instruction span and exactly one terminator; entry block parameters are function parameters. |
| Origin | Document ID and start/end byte offsets, 12 bytes; diagnostic lines/columns are resolved when needed. |

Budgets are targets, not measured layouts. Pin implemented C records with static assertions and count corresponding Chibi storage before claiming these sizes. Do not copy a type onto every use; each Value owns its type. Function/name IDs are assigned in deterministic source/package order; resolution tables may be temporary. There is no identity hashing in the per-instruction loop.

Closed operation set for these callers: i64/bool constant, wrapping i64 add/multiply, signed i64 greater-than, i64 equality, bool not, resolved direct call, foreign call, jump, conditional branch and return. `&&` becomes branches, not eager boolean arithmetic. A foreign call is effectful and ordered even if its result is unused. An internal call conservatively carries the callee's effects until a later measured simplification needs more precision. No general effect framework is required here.

Local assignment updates the checker's local-to-value map. At a join, explicit block arguments carry differing values; there are no untyped stack slots for a backend to re-infer. For `Join`, the true edge supplies `left`, the false edge supplies `right`, and the merge block receives one i64 argument then adds 1. Emit that fact once. Avoid a general SSA-repair pass for this acyclic branch-only slice.

Required verifier gates:

- Every ID/span is in range and belongs to this function/module as appropriate; every referenced signature is admitted and complete.
- Exactly one definition per value; types agree with op signatures; operands dominate uses. Block-parameter arity/types agree on every edge.
- Every block has exactly one final terminator; branch conditions are bool, return type matches, and every reachable exit returns. Reject instructions after a terminator.
- Calls use resolved function IDs and source-ordered argument values. Arity, module visibility and duplicate names were already diagnosed during resolution, not guessed by the backend.
- Foreign declarations resolve to an explicitly supplied fixture symbol with the same target signature before anything becomes callable. An absent symbol is a diagnostic, never address zero.
- A malformed typed body is an internal compiler assertion. Imported serialized experimental IR, if added later, is external input and requires checked diagnostics instead. No serialized IR input is needed now.

Verifier rejection tests must eventually mutate real produced bodies: bad use/definition, cross-function ID, wrong argument/block-parameter type, non-dominating use, missing terminator and unreachable output after return. Handwritten IR examples alone do not prove source lowering.

## Batched native boundary and first target

Start with the observed host, Linux x86-64 System V. This is a bounded experiment target, not a withdrawal of Windows/macOS requirements. Submit an immutable module's function/signature/block/value arrays once through a versioned C ABI. Rust-owned state remains opaque; no Rust ABI, per-instruction FFI, exceptions/panics unwinding through C, or borrowed pointers retained after the compile call. Return diagnostics and a result handle owned by the caller until explicit destruction.

Use target-aware C calling conventions for imported functions and harness-visible exports. Internal bool values are canonical 0/1; the adapter must apply the target's actual parameter/result representation and extension rules rather than assuming CLIF's chosen integer type is a C ABI. The C fixture exercises that boundary only once Chibi-generated calls exist. Eight i64 arguments test stack placement on this first target. Narrow integer, float, aggregate, pointer, callback, reentry, stack-probing and unwind fixtures are required before those features are claimed, not implicitly covered by this corpus.

JIT: declare the whole module and foreign symbols, compile **all** required bodies eagerly, verify, resolve relocations and finalize code before obtaining an entry pointer. Keep the result owner alive through all calls; retire it only after callers finish. No compilation on first call and no interpreter fallback. AOT: use those same lowered bodies, emit native object bytes and link with the C fixture archive plus the minimal entry harness. Resolve every expected import and execute every corpus call; linked bytes alone are not acceptance.

No C archive is checked in. Once a real compiler consumer exists, build `slice_probe.c` without `CHIBI_SLICE_PROBE_SELFTEST` to `slice_probe.o`, then archive it as `libslice_probe.a` with the target's existing archive tool. Both consumers must bind this same compiled fixture: explicit symbol addresses for JIT, native relocations/linking for AOT. Do not map `probe` to the system C library. The current Python runner deliberately produces only the standalone fixture selftest.

## Smallest next step and acceptance stop

Current status: the separately documented [Cranelift capability probe](slice_backend/README.md) has passed its six JIT and six linked C AOT checks using hand-authored bodies. Official Rust 1.99.0 and pinned Cranelift 0.137.1 dependencies were installed with explicit authorization in an isolated workspace. The [retained-parser experiment](frontend/README.md) also passed its 22 syntax/tree checks, using the exact eight current parser files compiled by the pinned c5 seed. These separate results do not establish source lowering: the new typed-body producer has not run together with either backend consumer. There is still no accepted current Chibi builder in this task. Native recovery has an uncertain outcome owned elsewhere and must not be retried here.

1. Obtain an accepted builder from the coordinating worker, with exact executable/revision and its verification. A separately fenced experiment may compile the unchanged parser closure with the pinned old seed; its results establish only that specific parser experiment, never current compiler admission or full-corpus typing. Do not retry the uncertain full bootstrap.
2. Preserve the mutually compatible 0.137.1 pins for `cranelift-codegen`, `cranelift-frontend`, `cranelift-module`, `cranelift-native`, `cranelift-jit` and `cranelift-object`, together with Cargo.lock, archive checksums and the verified Rust 1.99.0 toolchain. The pinned release requires Rust 1.97 or later. Do not silently choose a new version or dependency set.
3. Add only the scalar header/body path above and batched adapter. First run Echo and Join through both outputs, then all 25 calls and 11 rejections. Confirm the foreign negative fails before callable publication/object execution. Destroy JIT owners and rerun to test cleanup. Full rebuild timing includes parser, signatures, all body work, codegen and JIT finalization or object/link startup.
4. Grow to a named real tool only after this slice works. Measure phase time, emitted instructions, temporary bytes, peak memory and runtime behavior against the existing complete-compilation baseline. These tiny fixtures are correctness gates, not a 100,000 LOC/sec result or a representative workload. The milestone remains open until actual JIT/AOT execution and a real tool pass.

## Dependency and distribution checks

Cranelift's repository documents native x86-64 and AArch64 support, including Windows x64 and macOS AArch64 calling conventions. That is not end-to-end Chibi platform qualification. Each selected OS needs real object/link/execution, ABI, executable-memory and unwind/debug tests. Native object output is not browser Wasm output. Keep the optional optimized-release and browser paths as explicit later decisions. [Upstream target/status documentation](https://github.com/bytecodealliance/wasmtime/blob/main/cranelift/README.md)

The pinned Cranelift package declares `Apache-2.0 WITH LLVM-exception`; dependency manifests and license declarations were inventoried during setup. A distribution notice audit remains required. Preserve required notices, including transitive dependencies. Chibi's current [license boundary](../../../LICENSE.md) assigns standalone compiler/tools GPL-3.0-only and runtime/reusable libraries LGPL-3.0-only. Keep compiler-only code out of a reusable runtime accidentally. Prebuilt backend/runtime distribution should eventually let ordinary users compile without Rust or a C development toolchain. This is a packaging review requirement, not a new license exception. [Upstream package license](https://github.com/bytecodealliance/wasmtime/blob/main/cranelift/codegen/Cargo.toml)

The JIT owner cannot be destroyed while its code or function pointers are in use. Object finalization resolves relocations into an object, not a runnable application. These are distinct completion gates. [JITModule contract](https://docs.rs/cranelift-jit/latest/cranelift_jit/struct.JITModule.html), [ObjectModule contract](https://docs.rs/cranelift-object/latest/cranelift_object/struct.ObjectModule.html)

## Engineering review of this checkpoint

Transform: a 15-file, 25-call, 11-rejection corpus and one C observation fixture into explicit future acceptance inputs. Consumers are the future JIT/AOT harnesses; today's runner checks bookkeeping and the fixture only. This is a cold probe, not a compiler hot path. Minimum work is one read per registered source plus one C compile/run, expected well below a second on this host; actual Chibi/backend costs are unknown and remain to measure. Manifest validation is linear in input bytes for the bounded corpus; duplicate shared module reads are one extra tiny file, not a production loading design.

What to delete later: migrate this corpus registry into the real native differential harness rather than keeping a second test registry; remove the standalone-only gate once real ABI calls cover it. Do not delete the current production compiler before its consumers migrate.

What to measure: retained parser/header costs, repeated header-catalog publication, bytes/value, verifier cost, adapter batching, eager compilation/finalization/linking, code size and runtime performance on a real tool. No speedup is claimed.

What to leave alone: production parser, runtime and the existing takeover branch. The experiments live on their separate development branch; no production dependency or seed pin changes are part of this checkpoint.
