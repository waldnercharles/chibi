# Frozen c5: bounded compiler diagnosis

## Findings

The cancelled 90-minute producer is not a reason to widen the limit. These distinct small-source experiments completed in about 21 seconds of measured controller time, using the verified c5 seed. They neither restarted the unresolved bootstrap nor ran generated programs.

1. **Direct c5 compilation of current compiler sources has a compatibility blocker.** An import-only `scalar_llvm` wrapper, covering 32 files / 1,122,111 bytes of source closure, rejects `ValueReach` at `tools/compiler/llvm_native/Emit.chibi:2074:13` after 1.926 seconds, before external LLVM. The rejected statement is `build^.used.maps.append(type);`. The diagnostic says: `scalar proof supports only locals, assignments, asserts, if statements, switches, loops and returns`.

   The entire `ValueReach` function text is identical in [the actual cancelled producer revision, 36506138](https://github.com/waldnercharles/chibi/blob/36506138dab6e97ccd9ebc99cd36e8b5eb96dcb3/tools/compiler/llvm_native/Emit.chibi#L2025-L2053). The same statement is at line 2034 there. This strengthens the compatibility concern, but it is not an exact-old-source execution or evidence that the hosted run reached this rejection. Preserve the validated bridge requirement; do not silently use the rejected direct path.

2. **The complete Driver build is substantially slower than emit-only, but that does not explain the whole-compiler stall.** On the same source, c5 `--emit-llvm` took 0.927 and 0.906 seconds; complete Driver compilation took 5.467 and 5.207 seconds. The emit-only route exits after frontend and LLVM-text generation, without opt, llc or linking. The difference between the means is about 4.42 seconds, 83% of complete-build wall time. This difference includes later driver work and is not an internal phase profile. Both emit outputs have identical hashes, as do both complete-build outputs.

3. **Repeated calls/parameters show superlinear cost in a small scalar workload.** Growing distinct integer functions, each called once in `Main`, from 1,024 to 4,096 increased emit time from 0.402 to 3.339 seconds (8.30x for 4x input). Uncalled zero-argument functions at those sizes took 0.148 and 0.564 seconds (3.82x). Distinct sequential locals in one scope took 0.125 and 0.655 seconds (5.23x). These are different mixes of parsing, lookup, checking and emission; they do not isolate one routine or reproduce the 90-minute stall.

4. **Two small real compiler-source closures are fast to compile with c5.** Import-only tokenizer and parser wrappers emitted successfully in 0.113 seconds (1 file / 30,253 bytes) and 0.314 seconds (8 files / 124,561 bytes), respectively. This measures compilation of those source packages and their dependencies, not execution of the tokenizer/parser on the whole compiler.

## Scope and method

The executing seed is SHA-256 `1d1d289a2429fe2ee2a75a71a959bf21590a02ff3ebf7cd8af1037a11bf1268e`, from `c5c0a1ac2e065b5e57feec0de6dd453c12fa267d`. Target source is the takeover checkpoint plus the small Driver jobs correction, `eff3d5ad85b80517596b751645d21d6f75f70d6c`. No compiler implementation was edited.

- Seventeen cases completed: sixteen successes and the explicit scalar-LLVM-source rejection above. Each case had a 14-second wall deadline plus the existing owned cleanup mechanism. No case reached its deadline. Aggregate controllers stop admitting new cases after 70 seconds. All three controllers reached terminal completed receipts with unchanged checked inputs.
- All invocations used jobs 2. Fifteen cases used `--emit-llvm`; only the two small Driver controls ran LLVM/linking. No generated output program was executed.
- Synthetic sources contain only integer functions/calls/locals. Real-package wrappers import `source_tokenizer`, `source_parser` or `scalar_llvm`, with `Main` returning zero. These are distinct source inputs, not a full `native_driver`/compiler rebuild or runtime fixture.
- Exact c5 collection checks and emits concrete functions from active imported packages, even if `Main` never calls them. Thus the import-only probes exercise those source closures; they do not merely emit an empty main. Generic templates instantiate only when used by concrete bodies.
- A paired emit/full/full/emit ordering was used for the Driver. Other cases are single observations, with fixed startup cost significant for subsecond results. No quiet-benchmark or controlled full-bootstrap speedup claim is made, especially while the historical process outcome remains unknown.
- Full command/output identities, timings and rejection facts are summarized in [c5-diagnostic-results.json](c5-diagnostic-results.json). Local raw receipts and source manifests are retained separately.

## Static candidates in the executing c5 implementation

These findings name real repeated work; the measurements above do not establish its share of the whole-compiler run.

- [Function declaration collection](https://github.com/waldnercharles/chibi/blob/c5c0a1ac2e065b5e57feec0de6dd453c12fa267d/tools/compiler/package_compiler/Compile.chibi#L2195-L2231) calls linear [FindCallable](https://github.com/waldnercharles/chibi/blob/c5c0a1ac2e065b5e57feec0de6dd453c12fa267d/tools/compiler/package_compiler/Body.chibi#L1539-L1548) for each new declaration. N unique names incur N(N-1)/2 comparisons; 4x N approaches 16x this component. A package/name index can preserve the source-order array while removing repeated searches.
- [Declared](https://github.com/waldnercharles/chibi/blob/c5c0a1ac2e065b5e57feec0de6dd453c12fa267d/tools/compiler/package_compiler/Body.chibi#L1948-L1957) similarly scans prior same-scope locals. The latest-local chains used here avoid the additional worst case of repeatedly reading the oldest binding.
- [CalledFunction and ScopedCalls](https://github.com/waldnercharles/chibi/blob/c5c0a1ac2e065b5e57feec0de6dd453c12fa267d/tools/compiler/package_compiler/References.chibi#L39-L145) resolve names through global callable arrays in multiple traversals. ScalarEmits also scans global params through AnyReference for each function. The called integer-function family combines these costs, so its 8.30x growth cannot be uniquely attributed to declaration lookup, call resolution or the parameter scan.
- Exact c5 `StatementStep` already lacks the earlier `types := table^` snapshot. Recommending that old fix again would not address this executing seed. The scalar-only probes also do not reach the reference/array fixed-point or rewritten-forest paths.

## Remaining obstacle and next action

There is no installed perf, gdb or lldb in this environment. The c5 binary has a symbol table but no DWARF sections; ordinary function symbols are numbered `chibi_scalar.*`, so precise source attribution additionally needs a trustworthy symbol/source map or an instrumented compiler executable. A source edit to the input compiler does not instrument the immutable c5 process compiling it.

The fast next step is to preserve the compatibility rejection as a bridge-admission case, and profile a permitted, bounded representative source once a suitable sampler and mapping or instrumented accepted builder are available. Keep the unrelated lost bootstrap gated until its original owner or a terminal receipt reconciles it. Do not start another whole compiler build, enlarge the 90-minute budget, or substitute Cranelift solely because that producer was cancelled. A different backend may help the measured small Driver iteration path; that is a separate, testable claim.
