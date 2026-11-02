# Compiler performance comparison

Source investigation and local measurements, 2 November 2026. The immediate
targets are unnecessary copying and repeated semantic lookups in Chibi. The
other compilers provide concrete implementation comparisons; this investigation
does not establish a cross-language compile-speed ranking.

The current profile is pinned to Chibi
`2ce2aef183b5c7310733afc20243615c2139c762`, tree
`c042b86c0aeeeb983e1bdac82329fdbaca61969a`, and native compiler SHA-256
`1db3ac16c9d451a89c1bb6b86c76dd7afa6ca03b8e6ff7b6ed926a93c80b4a69`.
The owner's later `1aaf0ef3e133b32dd6c774f31bcd1fc1a339e2a9` integration is
not measured by that profile. Both the sampled rebuild and a separate IR
capture reproduced the complete pinned compiler hash.

The examined external revisions are Zig 0.15.1
`3db960767d12b6214bcf43f1966a037c7a586a12`, Odin
`60a9eee29874117de1530f0e1f26e72aa410d1c1`, Rust development
`ae36fe22c7e590df40ffd53ac25fea35597c3f3d`, and Goose
`eadca409f68210893f366e60d0c2f0dc29a339be`. The Zig source is an archived
release, not current Codeberg main. The other pins were observed on their
official development branches. No external compiler was built or benchmarked.
The Zig pin uses LLVM 20 and the Rust pin LLVM 23.1.5; those are not matched
toolchains for Chibi's LLVM 22 measurements. [Zig toolchain](https://ziglang.org/download/0.15.1/release-notes.html#LLVM-20),
[Rust LLVM pin](https://github.com/rust-lang/llvm-project/blob/0a5579eb649d3634a9c30ac873300cc2944fa070/cmake/Modules/LLVMVersion.cmake).

| Compiler | Relevant ownership and semantic work | Actual CPU backend boundary |
| --- | --- | --- |
| Chibi | Flat indexed tables, but the observed native implementation eagerly clones some read-only array bindings. Callable, named-type and pointer-descriptor queries scan tables. | LLVM text pieces, bounded parallel `opt` → bitcode → `llc` pipelines, then an external linker. [Lookup](https://github.com/waldnercharles/chibi/blob/2ce2aef183b5c7310733afc20243615c2139c762/tools/compiler/package_compiler/Body.chibi#L1539), [pipeline](https://github.com/waldnercharles/chibi/blob/2ce2aef183b5c7310733afc20243615c2139c762/tools/compiler/native_link_host/Host.chibi#L45). |
| Zig | Canonical type/value IDs and indexed, per-function AIR with explicit buffer ownership. [InternPool](https://github.com/ziglang/zig/blob/3db960767d12b6214bcf43f1966a037c7a586a12/src/InternPool.zig#L7791), [AIR](https://github.com/ziglang/zig/blob/3db960767d12b6214bcf43f1966a037c7a586a12/src/Air.zig#L1). | Its own typed LLVM builder serializes binary bitcode, then LLVM loads, optimizes and emits the object in-process. It does not call LLVM's C API for every instruction. [Object emission](https://github.com/ziglang/zig/blob/3db960767d12b6214bcf43f1966a037c7a586a12/src/codegen/llvm.zig#L937). |
| Odin | Arena-backed AST/type pointers; retained type/value annotations and resolved/checked states avoid repeating established checking. This is not a uniformly indexed IR. [Ownership](https://github.com/odin-lang/Odin/blob/60a9eee29874117de1530f0e1f26e72aa410d1c1/src/parser.cpp#L212), [checked state](https://github.com/odin-lang/Odin/blob/60a9eee29874117de1530f0e1f26e72aa410d1c1/src/check_decl.cpp#L2217). | LLVM C API construction in separate module contexts, in-process passes and object emission, external linking. [Construction](https://github.com/odin-lang/Odin/blob/60a9eee29874117de1530f0e1f26e72aa410d1c1/src/llvm_backend_general.cpp#L90), [passes/emission](https://github.com/odin-lang/Odin/blob/60a9eee29874117de1530f0e1f26e72aa410d1c1/src/llvm_backend.cpp#L2803). |
| Rust | Interned types, keyed semantic queries, indexed MIR and explicit ownership transfer between some MIR stages. [Type interning](https://github.com/rust-lang/rust/blob/ae36fe22c7e590df40ffd53ac25fea35597c3f3d/compiler/rustc_middle/src/ty/context.rs#L217), [MIR ownership](https://github.com/rust-lang/rust/blob/ae36fe22c7e590df40ffd53ac25fea35597c3f3d/compiler/rustc_mir_transform/src/lib.rs#L788). | LLVM instruction APIs through FFI; optimization/object emission inside rustc; partitioned codegen units and an external final linker. Bitcode remains necessary for selected LTO/output/reuse modes. [Builder](https://github.com/rust-lang/rust/blob/ae36fe22c7e590df40ffd53ac25fea35597c3f3d/compiler/rustc_codegen_llvm/src/builder.rs#L96), [LLVM worker](https://github.com/rust-lang/rust/blob/ae36fe22c7e590df40ffd53ac25fea35597c3f3d/compiler/rustc_codegen_llvm/src/back/write.rs#L784). |
| Goose | Namespace maps select a declaration/overload set before specialization matching. A specialization owns its annotated body; ordinary child walks reference existing nodes. Specialization matching still scans a per-function vector, and types are not universally interned. [Scoped lookup](https://github.com/aardappel/goose/blob/eadca409f68210893f366e60d0c2f0dc29a339be/src/ast.h#L2825), [specialization matching](https://github.com/aardappel/goose/blob/eadca409f68210893f366e60d0c2f0dc29a339be/src/typecheck_calls.h#L1623). | Emits C. Quick runs compile/relocate it through bundled TinyCC in-process; executable builds invoke a C compiler. This is a frontend comparison, not another direct LLVM implementation. [TinyCC path](https://github.com/aardappel/goose/blob/eadca409f68210893f366e60d0c2f0dc29a339be/src/jit.h#L46), [build path](https://github.com/aardappel/goose/blob/eadca409f68210893f366e60d0c2f0dc29a339be/src/build.h#L149). |

The useful commonality is explicit ownership and reuse of established semantic
facts. None requires moving Chibi to a new backend or importing an incremental
compiler framework. Goose is particularly relevant to looking up the correct
declaration before inspecting specializations, rather than scanning an entire
program's callable table for each spelling.

The post-fix profile contains one nine-second window beginning about one second
into compilation: 7,575 main-thread samples, inside the frontend, before LLVM
children appeared. These are sample shares in the opening window, not whole
build percentages or invocation counts.

| Observed Chibi work | Evidence | Consequence |
| --- | --- | --- |
| `FindCallable` | 2,615 leaf samples, 34.52%. A separate full-run counter records 392,132 queries and 487,049,257 logical row visits, averaging 1,242 per query. | O(Q×F) work for Q queries over F callables. There were 87,319 misses; they alone visited 166,903,613 rows. Runtime table lengths ranged from 0 to 1,949. |
| `FindType` / `FindPointer` | 660 / 616 leaf samples, 8.71% / 8.13%. | Named-type and structural pointer queries are separate scan problems with different ordering and mutation rules. |
| `CtCollect` | Every nonnegative-node visit clones the full forest. The counter records 1,344 clones of 725,392 rows × 40 bytes: 38,997,073,920 payload bytes in one rebuild. Matched IR/disassembly identifies 548 exact copy samples. | About 39 GB of logical copying, although discovery needs only the current node and its children. This is not measured DRAM traffic or peak memory. |
| `CtCollect` subtree | 1,401 allocation-zeroing leaf samples and 2,126 total subtree samples, 28.07%. | An upper bound including useful work and callees. Do not add this inclusive share to the other rows or treat it as a forecast. |

`CtCollect` only reads its local forest. Its owner retains that forest for the
entire traversal, helpers do not replace it, and the output contains node
indices. Removing its local snapshot is therefore the smallest directly proved
removal of unnecessary work. The separate `FoldCalling` context construction
also clones a type table, but those zeroing samples do not establish its cost.
[Traversal and caller](https://github.com/waldnercharles/chibi/blob/2ce2aef183b5c7310733afc20243615c2139c762/tools/compiler/package_compiler/Comptime.chibi#L444).

The counter compiler added no per-row reporting: callable visits are derived
from each query's table length and first-match return index. The two functions'
captured IR bodies were checked before patching; the counted run reproduced
the complete uninstrumented `1db3` output. Counter elapsed times are diagnostic,
not before/after performance results.

A callable index must preserve first-match order and the `identity == 0`
filter. Its ordinal entries must account for generic finalization and entry
reordering. Type and pointer tables continue growing during semantic work, so
an index built once at module entry without updates would be incorrect. A
smaller one-row binding in `FindCallable` can remove redundant element loads
and string ownership operations, but leaves linear growth unchanged.

The earlier phase diagnostic is a different baseline: frozen `9934af8`, before
the two type-table snapshot fixes. Its 36.727-second instrumented rebuild spent
23.874 seconds in semantic/Code/facts construction, 0.040 seconds parsing,
0.126 seconds in native lowering, 1.061 seconds validating/emitting LLVM text,
and 10.960 seconds in elapsed LLVM pipelines. These exclusive intervals locate
the old cost; they do not describe the remaining cost after the fixes.
`StatementStep`'s three snapshot sites alone accounted for 207,045,775,872
cumulative logical payload bytes; `CallStep` is not included in that count.
These are not peak memory or measured physical DRAM traffic.

At the current `2ce` pin, the separate capture contains 48 `opt` inputs totaling
69,480,053 bytes, 49 `llc` calls and one link. Process counts do not measure
startup overhead. Chibi already parallelizes module pipelines. Zig's pinned
LLVM generation requires the main thread; Odin and Rust use their own module
partitioning and worker limits. Neither merging all modules nor adding threads
follows from this comparison. [Zig restriction](https://github.com/ziglang/zig/blob/3db960767d12b6214bcf43f1966a037c7a586a12/src/Zcu/PerThread.zig#L4478),
[Odin splitting](https://github.com/odin-lang/Odin/blob/60a9eee29874117de1530f0e1f26e72aa410d1c1/src/llvm_backend_general.cpp#L189),
[Rust scheduling](https://github.com/rust-lang/rust/blob/ae36fe22c7e590df40ffd53ac25fea35597c3f3d/compiler/rustc_codegen_ssa/src/back/write.rs#L1301).

One later code experiment has specific supporting evidence. Chibi emits stack
slots for scalar values and asks LLVM to promote them; the older LLVM profile
attributed 7.017 of 16.512 summed optimizer CPU seconds to SROA. This is worker
CPU work, not critical-path latency. Rust selects eligible
single-definition, dominating, non-address-taken locals for direct SSA operands.
Count such slots in one captured Chibi piece and test that IR shape before
changing the emitter. Keep mutable/address-taken values, failure/budget checks
and aggregate ownership intact. [Chibi slots](https://github.com/waldnercharles/chibi/blob/2ce2aef183b5c7310733afc20243615c2139c762/tools/compiler/llvm_native/Emit.chibi#L2473),
[Rust eligibility](https://github.com/rust-lang/rust/blob/ae36fe22c7e590df40ffd53ac25fea35597c3f3d/compiler/rustc_codegen_ssa/src/mir/analyze.rs#L18).

The controlled results already obtained are narrower than a CI speedup:

| Same-input comparison | Before → after | Interpretation |
| --- | --- | --- |
| `StatementStep` read-only type-table access, four balanced pairs | 35.024 → 31.494 s mean wall; 63.925 → 60.685 CPU s | 10.1% faster compiler rebuild in that window; output bytes identical. |
| `CallStep` read-only type-table access, four balanced pairs | 29.347 → 26.067 s mean wall; 57.635 → 53.960 CPU s | 11.2% faster in its separate window; output bytes identical. Do not combine the absolute averages across windows. |
| Final same-path SDK edit feedback, five edits per route | Native 0.159 → 0.167 s; installed legacy path 1.134 → 1.147 s mean wall | No material feedback improvement attributable to those fixes. |
| Same-inventory hosted CI, control `94cf5fb9` → candidate `f247e2ef` | 42m22s → 43m19s latency; 64m39s → 68m20s total runner work | No measured complete-CI improvement. Different hosted runs do not isolate a causal regression. [Control](https://github.com/waldnercharles/chibi/actions/runs/41493318021), [candidate](https://github.com/waldnercharles/chibi/actions/runs/41496306784). |

The two local compiler comparisons used the same M4 Mac, LLVM 22.1.8, frozen
sources and jobs=4 within each comparison. All paired emitted compilers were
byte-identical. Earlier and current profiles have different inputs and native
runtime features, so their absolute wall times are not an A/B comparison.

The next experiment is to remove only `CtCollect`'s read-only local snapshot.
For the frozen input, this predicts 1,344 fewer full-forest allocations and
38,997,073,920 fewer copied payload bytes. An elapsed-time prediction and paired
whole-rebuild measurements are still required. Preserve exact diagnostics, named tests, fixed-point
equality and both driver/graph parity checks. Then address the remaining
lookup scan with an owned declaration index or a smaller measured cleanup.

LLVM handoff work comes later: replay one captured piece with unchanged LLVM
22 options, separating parsing, passes, codegen, process and file costs. An
in-process helper can test that boundary without adopting it in the compiler.
Normal Chibi O2 must not be compared with Odin's minimal default, Rust's O0,
Zig's self-hosted backend or Goose's unoptimized TinyCC run. Goose `check`
also runs its optimizer and bounds-check elimination before stopping, so check
commands need matched phase boundaries. [Odin defaults](https://github.com/odin-lang/Odin/blob/60a9eee29874117de1530f0e1f26e72aa410d1c1/src/build_settings.cpp#L2126),
[Rust options](https://github.com/rust-lang/rust/blob/ae36fe22c7e590df40ffd53ac25fea35597c3f3d/src/doc/rustc/src/codegen-options/index.md),
[Goose check](https://github.com/aardappel/goose/blob/eadca409f68210893f366e60d0c2f0dc29a339be/src/main.cpp#L1613).

Local raw evidence is retained under
`/Users/cwaldner/Personal/chibi/.coordination/compile-ci-costs-20261102/evidence/`:
`code-profile-2ce/` contains immutable inputs, commands, hashes, samples, mapping,
IR and disassembly; `native-phase-plan/` contains the earlier phase observation;
`paired-compiler-results/` and `paired-targeted-results/` contain controlled
runs; `feedback-results/` and `hosted-ci-baseline/` contain feedback and CI
receipts; `compiler-comparison/` contains the four detailed source audits.
