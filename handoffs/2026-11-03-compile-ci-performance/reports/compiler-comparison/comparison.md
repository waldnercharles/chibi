# Compiler performance comparison

Source investigation and local measurements, 2 November 2026. The immediate
targets are unnecessary copying, repeated type construction and semantic lookups in Chibi. The
other compilers provide concrete implementation comparisons; this investigation
does not establish a cross-language compile-speed ranking.

## Current implementation and measured results

The latest correction is [f11df263](https://github.com/waldnercharles/chibi/commit/f11df26379b2250ab34d07fda140acfc546de353), tree `38a468ea8f28fcbfccb783227bd030459d01a11c`, based on the coordinator's `4d23` integration. Repeated address-using packages treated synthetic optional/collection descriptors as new pointer roots; their generated pointer-array/view optionals became roots in the next package. The correction removes that automatic expansion and seeds only the concrete addressed value parameters that need boxing. Explicit spelling and existing body demand paths remain. This follows the useful prior-art lesson of retaining canonical semantic facts and constructing required types, without importing a new interning framework. It changes 16 production lines and removes one, with two regressions in the existing producer runner.

A declaration-only probe with 1/2/4/8 small imported address-using packages produces 261/810/6,588/unknown baseline rows versus 174/174/174/174 candidate rows. Baseline N=8 reached its original 300-second deadline before a final count; it was not retried. The N=4 count is 97.36% lower, not a 97.36% compiler-speed claim. This directly measures unused frontend work rather than inferring that LLVM or process spawning dominates. Exploratory full compiler constructions took 211.51 and 28.11 seconds, but these were not balanced comparisons. Controlled runs are prepared and held until the shared Mac has an agreed quiet window.

The candidate reproduces itself byte for byte (`7de54a35886f07a5089dbfa1e790278732fb34fc8601762193eb17ffcfffdff2`), passes all 443 compiler tests and the unchanged native managed-pointer suite, and passes the real ordinary bytecode/SLJIT controls with exclusive backend counters and no fallback. The growth-negative producer builds and then fails at the intended new growth assertion. The exclusions-only control builds and passes growth, then fails at the exact new parameter-demand assertion; the complete candidate passes both checks and all prior 154 scalar corpus cases / 169 malformed variants. Original source inputs remain unchanged in every run; the managed suite's three generated CLI cache outputs and its initial overbroad inventory-guard failure are explicitly preserved. A stronger optional-array VM case still fails on both baseline and candidate and remains outside this correction. Exact evidence is in `pointer-construction-4d23/`. Final composed-head fixed point, both parity checks and CI are still required; no complete-CI speedup is established.

The measured projection fix is committed as [1f86bd5a6](https://github.com/waldnercharles/chibi/commit/1f86bd5a690c8c398d8d63d649c473cfecfceec9) and is being composed by the native-default owner. It borrows an array record for an adjacent numeric/bool field read, preserving ownership for managed selections. The change adds seven lowering lines and extends the existing Fields regression. Its canonical compiler tree is `a40c6625f16a5b095145b0f2a1781a70493456c5`; an inaccurate precomputed tree field is preserved and corrected in the final-review receipt. No measured source byte changed.

Four balanced pairs used identical immutable original776 sources, LLVM 22.1.8, target, options and jobs 4 on the same Mac. Both phases completed within 22:58:40–23:06:40 UTC.

| Workload | Mean wall before/after | Mean CPU before/after | Result |
| --- | ---: | ---: | --- |
| Full compiler rebuild |37.823→29.740s|89.158→81.100s|21.37% lower wall; all four pairs faster |
| Same 429 tests/18 packages |12.255→11.260s|12.955→12.265s|8.12% lower wall; identical names/order/stdout |

RSS remains roughly 2.68 GB for rebuilds; no stable memory improvement is established. These are output-cold rebuilds with the OS file cache retained. They are not measurements of full CI or the owner's newer composed source.

Fresh generated code proves the work removed: FindCallable's primitive identity/package reads no longer keep/drop the record's string owner. Its managed-name path retains ownership. Function size falls 452→332bytes and static atomic increment/decrement loop sites fall 4+4→2+2. Dynamic query/row-visit counts for this current source remain unknown. The preceding post-COW baseline sample had lookup leaves at 74.63% of a nine-second opening frontend window, not of the whole compile.

Candidate P2=P3 is exact. Control and candidate pass the full existing managed-values O0/O2 programs, rejection cases, direct Probe and real C reference wrappers. All 429 compiler tests have identical outcomes with no skips. Three intentionally broken lifetime variants build, pass the earlier O0 cases and fail during actual Fields O0 execution with the unchanged allocation audit's exit 99. No O2 negative-control or exact assertion-coordinate claim is made. Real native fixed-point, Driver parity and Graph parity pass on exact 1f86; fresh composed-head CI remains required.

A post-change five-second early frontend sample has 4,155 main-thread observations. FindType has 24.26%, SourceCallable 23.44% and EnsurePointer 7.12% of that window. SourceCallable contains an inlined FindCallable scan, and EnsurePointer contains an inlined FindPointer scan. Standalone symbol samples therefore do not measure all lookup work. The first observed real opt child was born about 12.25 seconds after compiler birth, with later opt/llc overlap; polling gaps preclude an exact phase boundary or completed-tool count. The proof child omits --jobs despite the outer interface requesting four; canonical pairs explicitly used four. The next code experiment should count repeated resolver requests at already-resolved callers while retaining lookup precedence. See `parity-1f86bd5a6/stage2-profile/assessment.md`.

Ordinary native feedback remains a fraction of a second: completed quiet-window arms observed 214–327ms cold,66–75ms warm and205–232ms after a one-function edit. All 60 build/cache/edit/rejection/recovery outcomes passed. The final A2 arm ended 2.467 seconds after the planned window and is excluded from timing comparisons; no balanced ABBA feedback speedup is claimed.

The prior art is informing this work through explicit ownership and reuse of resolved facts. The two earlier type-table read fixes removed unnecessary table copies and measured 10.1% and11.2% improvements in separate windows. The new guard removes ownership work for data the caller never consumes. Goose's scoped lookup and Odin/Rust's retained semantic facts guide the next experiment: count repeated resolver requests and eliminate already-known resolutions before considering a declaration index. Rust's SSA-local lowering and the LLVM boundary remain later experiments. The CtCollect experiment below is still private and inconclusive.

Raw results: `paired-compiler-results/primitive-projection-776-v1/report.json`, `projection-validation-776/canonical-results-claim-audit.json`, `projection-qualification-776/`, and `projection-validation-776/final-review/`, under the evidence directory listed below. Existing historical measurements remain labeled by their own source revision.

The subsequent correctness correction [f20d6c7](https://github.com/waldnercharles/chibi/commit/f20d6c7c0dc299a1b25c33217620290a1873ccc9), tree `a89cb767cb2aa79ff6507a77d0d8e8f40e65bd6a`, prevents a readonly outer record snapshot from overwriting a recursive call's retained write. It carries the existing finalized parameter write effect into native lowering and preserves the reference ABI, conservative foreign/legacy behavior and writable-alias publication. It is not a performance result. P1=P2 is byte-identical (`8fbcee7957507b243469ba76ba80970938f3a6cc56ba1bdb6d935eda37b617e1`). All four focused retained cases pass; the full generation suites pass at O0/O2, all 437 current compiler tests pass, the corrected producer passes all 154 corpus cases, and the existing backend runner passes. Both ordinary alias orders return 22 on native O0/O2, bytecode and SLJIT. An unsupported adjacent-literal spelling in the new producer test was caught and corrected without changing the 293-byte fixture or assertions; the initial failure is retained. See `retained-publication-candidate-a122-v2/` and `retained-publication-validation-a122/` for exact inputs, ordered review and execution receipts. The coordinator imported this as `4d23cceeed2233b1896bb0473a1dff1c6f4a00c6`; its newer combined source still needs its own validation.

A separate read-only SHA audit found a concrete ownership lead: a preserved compound-update fixture recorded 32,768 additional allocation callbacks for 4,096 blocks, exactly eight chain updates per block, while its plain-store variant recorded none. Preserved machine code places a record/array ownership retain before uniqueness in the compound form. The verified a122 compiler sources retain that structural split, but the historical compiler/source/Audit identity is incomplete. These are allocation observations, not a measured speedup or a reproduced current-compiler result. GC receiver roots are a separate requirement and must stay. The coordinator owns the core SHA preload/plain-store change; a future compiler experiment should first reproduce this on an agreed immutable boundary and preserve receiver-changing RHS and computed-index semantics. See `sha256-compound-cow-audit/assessment.md`.

The earlier profile discussed below is pinned to Chibi
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

The `CtCollect` experiment has now been measured separately on frozen `1aaf`.
Its control compiler is SHA-256
`8dc3dbd3ae9a769917c1432947dd15528da5b44dae3d01d346b1bda5825ba134`;
the private candidate is
`b299d5b1edd61d53993ea57c4c4e40a869298a8fee0b6c4370b80c89ff5700be`.
The control reproduced itself from original sources; candidate stages two and
three reproduced the candidate. Actual control disassembly confirms a full
`40N + 8` allocation and `40N` copy at entry; candidate IR and machine code
remove it. The earlier 1,344-call/39-GB count remains specific to `2ce/1db3`:
dynamic counts for `1aaf` were not collected.

The recorded fresh-rebuild prediction was 2–7%. Four balanced uninstrumented
pairs used identical original `1aaf` inputs, LLVM 22.1.8 and jobs=4. Every emitted
compiler reproduced the complete `8dc3` hash. Mean wall time was
55.993 → 54.337 seconds and mean reported CPU 96.335 → 94.935 seconds;
maximum process RSS stayed approximately 5.55 GB. Paired wall savings were
−0.209, +4.423, +4.666 and −2.253 seconds. Two pairs improved and two regressed;
the 3.0% mean reduction does not establish a reliable whole-rebuild speedup.

The same 414-test command at explicit O0 was measured in four balanced pairs:
12.505 → 11.956 seconds mean wall, with savings of 1.575, 0.398, 0.095 and
0.126 seconds. All ordered names and outcomes matched. The first pair accounts
for most of that difference. Existing process isolation and default workers
were retained. Eighteen package compilations are source-derived counts; package
binary hashes were not observed because this runner deletes its outputs.

All sixteen measured commands completed inside the agreed 20:03:11–20:13:11 UTC
quiet window; the last test result was written at 20:13:07.156. No hold extension
was used. These local results do not demonstrate complete-CI improvement.

Additional validation passed all 414 native compiler tests, five supported
comptime positives, three exact diagnostics, and the imports/comptime fixture.
One nested-record/array fixture is native-unsupported on both the control and
candidate, with the same rejection. Its nine unchanged assertions pass through
the existing candidate asset service and C bytecode host using a private integer
entry adapter; changing only its first original assertion to false produces the
expected assertion failure. The earlier unsuccessful void-entry attempt remains
in the receipts. No production runner or backend behavior changed.

The candidate remains private and is not a validated compiler-speed win. The
next lookup experiment should first eliminate requests whose target or local
classification is already known. Only then consider a compile-owned declaration
index, preserving first-match order, scope and ordinal-reordering rules.

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
`comptime-forest-candidate/` contains the `1aaf` experiment, paired 414-test
results, quiet-window timestamps, disassembly and VM assertion receipts.
