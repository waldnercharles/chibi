# Bounded source-native Cranelift backend

The `chibi_vnext_backend` cdylib consumes the generated
`frontend/bridge/generated/arena.rs` view of the real typed Module. The accepted
path is retained Chibi parser/signature reader → typed Produce/Verify → generated
Pack → one Cranelift lowering for eager JIT and object output. The backend never
reads source, interprets an IR, runs a prepared VM or converts LLVM text. The
pinned old c5 seed uses LLVM to build the Chibi test caller; that is distinct
from compiling the caller's source-produced typed bodies with Cranelift.
Only Linux x86-64 System V is admitted.

## Accepted bounded result

The independently reviewed 2026-11-04 source-native proof passed all five
stages: Chibi caller compilation, normal JIT execution, owned environment-failure
execution, native C link and AOT execution. Its terminal receipt SHA-256 is
`af682cb96294c1aacbc31ed5fe6158b94bbbad2eb27b31fbfcf76774420f81be`.
That proof's backend shared library SHA-256 is
`2a668047f0fba593028670dcc4bde29e6d0ae65f15548bf3347d09fca14e11c1`.

The source fixture has 16 functions and 15 exports. Nine complete owners across
eight peer cycles exercised 288 function compilations across both consumers and
1,080 JIT value assertions. A separate C caller passed 45 checks against the
copied source-produced object. Coverage includes Echo/Join, renamed/+7 bodies,
both edges, multiple merge arguments, nested and early returns, full-width i64
values, wrapping addition, bool results, register/stack arguments, export
visibility, repeated ownership and object copy-out. Seven source-rejection
controls stop in Produce before Pack or backend entry. The separate
PERF_BUILDID_DIR failure returns an owned diagnostic and no callable/object
output. Source/tool postflight and owned child cleanup checks passed.

Actual ELF/runtime dependencies were reviewed. Rust/GCC linker traces and the
C link trace were observed; the pinned c5 driver's link-input selection was
reviewed without an observed trace of its transient linker inputs. Do not
upgrade that evidence into a c5 linker-trace claim. Earlier failed attempts
and the dependency-free ABI smoke retain their original limits and outcomes.
The smoke's initial Rust stage did not fully fence Python startup/import/cache
bytes; later checks do not retroactively establish those historical inputs.

This source-produced replacement retires the handwritten capability executable
and its separate C caller. The package name remains
`chibi-slice-backend-probe` solely to preserve package/lock identity; there is no
binary target. Historical receipts bind their frozen inputs, not later changes.
The final-Cargo rebuild and public reproduction have separate validation below.

The accepted subset is the [bounded producer](../frontend/typed/README.md), not
the whole [slice corpus](../slice-design.md). Calls, foreign calls from generated
bodies and the wider operation set remain unsupported. The experimental rule
that all admitted i64 additions wrap, including literal-only additions, is not
current-language constant-expression parity. There is no current-compiler,
full-compiler, real-tool, cross-platform or compilation-speed acceptance.

## Caller and ownership contract

The library target is cdylib, with no staticlib fallback. Keep the shared library
loaded until every result owner has been destroyed.

All symbols below use the platform C ABI. Counts are elements, not bytes;
all handles are opaque `void *` (Chibi rawptr).

1. `chibi_vnext_compile_both(words: const uint32_t *, word_count: uint64_t,
   literals: const int64_t *, literal_count: uint64_t,
   names: const uint8_t *, name_byte_count: uint64_t) -> void *`.
   Return an owned result after eager object and JIT completion, or an owned
   failure diagnostic. OOM aborts. No input pointers survive this call.
2. `chibi_vnext_status(owner) -> uint32_t`: 0 ready, 1 bad public argument,
   2 bad framing, 3 unsupported target, 4 backend/environment failure.
3. `chibi_vnext_diagnostic_length(owner) -> uint64_t` and
   `chibi_vnext_diagnostic_copy(owner, uint8_t *destination, uint64_t capacity)
   -> int32_t`. Diagnostics are UTF-8 bytes without a trailing NUL, empty on
   success. Copy returns 0 on success, 1 for a null owner, 2 for insufficient
   capacity, 3 for an invalid output pointer/extent.
4. `chibi_vnext_entry(owner, uint32_t function_id) -> const void *`: return
   null unless the result is ready and the one-based function ID is exported.
   The caller checks its expected C signature against the typed module before
   casting. The address stays valid only while this owner is alive.
5. `chibi_vnext_object_length(owner) -> uint64_t` and
   `chibi_vnext_object_copy(owner, uint8_t *destination, uint64_t capacity)
   -> int32_t`: object bytes, using the same copy statuses plus 4 for unavailable
   output. A failure result has no object. A too-small/invalid destination is
   not modified or truncated. Both length functions return zero for null owners
   or unavailable bytes.
6. `chibi_vnext_destroy(owner) -> int32_t`: destroy exactly once after all
   generated calls finish; returns 0, or 1 for null. Do not use this owner or
   any returned address after destruction.

Public length/null/alignment/isize extent checks precede slice construction.
Zero-length buffers need not have nonnull pointers. For admitted nonempty
buffers, the caller guarantees one readable allocation of the stated extent,
with no concurrent mutation during compile. Output destinations likewise name
writable, nonoverlapping caller storage. Checks cannot prove pointer validity,
prevent forged/stale handles or make double destruction safe.

The frame decoder checks version/counts/exact table framing. This is explicitly
an already-verified same-process model contract: the producer has run Chibi
Verify before packing. The backend does not accept or semantically validate
arbitrary unverified frames. It checks IDs/spans, admission bounds, kind decoding
and local value definitions where lowering consumes them, without rebuilding
Verify's ownership/dominance/type analysis or its scratch arrays. Broken internal
invariants and invalid CLIF assert and abort; they are not source diagnostics.
Backend/environment errors return owned diagnostics with no partial callable
publication. No global last-error state exists.

PERF_BUILDID_DIR is rejected with an owned BackendFailure before backend work.
Upstream Cranelift perf-map creation can panic on an OS failure; this bounded
backend admits no such side effect. It does not mutate the environment or catch
C-boundary panics. The caller must keep the environment stable during compile.

Both parameter and result bools receive explicit System V zero extension.
Every edge argument is forwarded, including both conditional edges. Every
body is compiled before JIT finalization and publication. The explicitly enabled
Cranelift verifier runs inside both pinned consumers' Context::compile; the
adapter does not repeat it. Verifier errors remain compiler-invariant failures.
One generic lowering serves JITModule and ObjectModule. Each new AOT artifact
requires an ordinary native link and execution; object emission alone is not
acceptance.

The result explicitly owns a JIT guard that calls JITModule::free_memory on
ordinary error exits and final destruction. Cranelift's default memory provider
intentionally leaks on plain Drop, so merely dropping JITModule is insufficient.
No concurrency/reentry or retained callback/task use is admitted. The Rust
library is compiled with panic=abort; no panic may unwind through Chibi/C.

## Focused tests and accounting

All three focused Rust tests in lib.rs passed without filtering: public
framing/count rejection covers null/nonzero pairs for each scalar buffer and
excessive counts on actual small buffers, with shared owned-diagnostic,
no-output and destroy checks; byte copy is exact or leaves
the destination unchanged; an injected finalization failure after real JIT
allocation calls the explicit free operation once. The last test lowers a tiny
typed function through the existing lowering, finalizes its code into the
provider's leak-on-Drop mappings, allocates a new current mapping, then injects
failure. Kernel /proc/self/maps metadata checks that explicit free releases the
finalized mapping and subsequent provider destruction releases the current one.
It never dereferences stale/fabricated pointers. This test-only memory provider
is not a production mode; source-produced acceptance is the separate proof
above. Generated enum/framing tests are owned by
frontend/bridge/tests/arena_tests.rs. A production library requires panic=abort;
Rust's unit-test harness uses its ordinary test panic behavior.

Current adapter accounting: borrowed name adaptation, then one instruction
traversal for Object and one for JIT. Duplicate semantic verification and its
4-bytes/value plus 1-byte/block scratch are removed. Each lowering reserves a
block map, a statically pinned 4-byte PackedOption<Value> per value, declaration
IDs, and two edge-argument buffers of at most 64 entries. With no call opcode,
each function is declared, lowered, compiled and cleared in source order in
one loop. Context signature buffers reserve known arity and are reused; there
is no preliminary declaration pass or staged signature vector. Cranelift's
required declaration-owned signature copies remain. Context and
FunctionBuilderContext are reused across functions; Cranelift's IR/code
allocations remain additional costs. Bounds and direct indexing keep adapter
work linear in rows and arguments. The current Echo/Join frame is 580 borrowed
bytes; none of these counts is a measured throughput result.

## Reproduction and remaining gates

The six direct Cranelift dependencies remain pinned to 0.137.1. Preserve
Cargo.lock and registry checksums. Use only the already authorized,
checksum-verified Rust 1.99.0 toolchain and existing cached crates, with
workspace-owned CARGO_HOME and fresh CARGO_TARGET_DIR. Do not install, fetch,
change system tools or shell profiles, or silently choose another version.

For developer reproduction, the focused commands from this directory are:

```sh
: "${CARGO:?set the absolute existing authorized Cargo executable}"
: "${RUSTC:?set the absolute existing authorized Rust compiler}"
: "${CARGO_HOME:?set the existing authorized offline crate cache}"
: "${CARGO_TARGET_DIR:?set a fresh workspace-owned target directory}"
export RUSTC CARGO_HOME CARGO_TARGET_DIR
"$CARGO" test --lib --locked --offline --jobs 2 -- --test-threads=1 &&
"$CARGO" build --lib --locked --offline --jobs 2
```

Use the corresponding existing native linker, flags and runtime search paths.
The production cdylib requires panic=abort; the unit-test harness uses Rust's
ordinary test panic behavior. A local rebuild does not reproduce the old
proof's independent tool/dependency admission.

The final library-only Cargo configuration has now passed a fresh independent
build/test proof, preserving Cargo.lock and all dependency pins. All three
backend tests passed at receipt SHA-256
`4cfeb855ecc526e51e984dd0e1105049cc982dfd7bdf842d2386e955541978a2`.
The production cdylib build receipt is
`1f15fb9958178aa3f497b35a38c5ebfb0c1ce5e20cb16c19feecbe407a61d968`;
its library SHA-256 is
`02e8957df08742cce7a2b40b79c21dd6569a93c442ab594eac044d6f750ea30e`.
The exact library, observed linker trace and recursive runtime dependencies
passed independent review. This is a new proof, not a rewrite of old receipts.

The [public source-native preparation and runner](../frontend/bridge/README.md#source-native-correctness-reproduction)
snapshot real sources and use an existing backend library. Their normal
five-stage workflow passed once with the final library above, followed by
independent terminal readback. The 54 Python preparation/generator/controller
controls also passed. A separate real-first-compilation cancellation control
reproduced the old interstage bug and confirmed the fixed runner stops before
any successor launch. This public correctness reproduction does not reproduce
the private proof's full tool/loader admission fences.

Full corpus coverage, generated foreign calls, representative real-tool/runtime
quality, phase/memory measurements, platform qualification and distribution
notice review remain later gates. No timing from these fixtures establishes
100,000 LOC/sec or a speedup.
