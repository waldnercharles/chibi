# Bounded source-native Cranelift backend

The `chibi_vnext_backend` cdylib consumes the generated
`frontend/bridge/generated/arena.rs` view of the real typed Module. The path is
retained Chibi parser/signature reader → typed Produce/Verify → generated
Pack → one Cranelift lowering for eager JIT and object output. The backend never
reads source, interprets an IR, runs a prepared VM or converts LLVM text. The
historical c5 seed and the separately admitted current compiler use LLVM to
build Chibi test callers; that is distinct
from compiling the caller's source-produced typed bodies with Cranelift.
Only Linux x86-64 System V is admitted. The historical version-2 source added bool
literals, wrapping multiply, signed greater-than, bool not and direct calls.
Its pure-source proof, focused Rust backend tests, production library build
and three exact-source private native proofs passed, including JIT,
owned environment-failure handling and linked C execution. The subsequent
package-aware checkpoint below also passed independent full-chain acceptance.

## Version-3 foreign-binding source checkpoint

The current source consumes version 3 and adds integer equality plus imported
functions through the same DirectCall lowering used for local calls. The sparse
ordered Foreign table drives Linkage::Import declarations; imports have no body
and publish no entry. Every source import must resolve against an exact
caller-supplied library, symbol and scalar signature before either native
consumer is constructed. An intermediate version-3 backend passed all seven
focused Rust tests and its production cdylib construction/artifact review. The
current source then improved descriptor-error context and added seven cases
inside the existing invalid-argument test. The corrected source now passes
fresh construction, actual-artifact review and all seven unfiltered Rust tests.
The corrected source's fresh non-test panic-abort cdylib also passed construction
and actual-artifact review. The original-source foreign execution result is
recorded below. Earlier version-1/2 evidence applies only to its own frozen
inputs, not to the four older native selections under current version 3.

The corrected Rust source SHA-256 is
`625f40f6020251be06175a735c57fffd1e456985c40200b2edfa298bcee71947`.
Its tested executable is
`cb2b436238b20c414668cfd73f389cd44f710ea1aebcafc7e8f5c984c9076378`.
The complete runtime receipt is
`c1c50f00bc50b3763f0ebc51646ab294201ed6f5a40c59c4b02be81456d9a709`,
with independent acceptance
`c242fb74c447198c88d290dcf158e4abffbf237abc9be5f5731a0d79d012b4ea`.
This narrow test run had seven passes, no skipped or filtered cases, empty
stderr and complete process cleanup. It includes explicit-pointer JIT fixtures,
but does not establish original-source foreign JIT/AOT or compilation speed.

The separate current-v3 Arena framing suite also passes all eight tests,
including imported membership rows, with no skipped or filtered cases. Its
executable SHA-256 is
`90fa7cd8f01c79c6f569e2642ec8aad7072b89596cd5473cd31e4708e761248d`;
test receipt
`2adddb45a7954ba0bd46e9fb05aea2835336ad3511380a32bd279ab4e304fcf6`
and independent acceptance
`b9bb069c2b6e7efc67e0095ad3e1391404413b4a636a2b438c5974e653ddc3c1`
bind the two exact current framing/view sources. This is separate from both
the seven backend tests and the source-to-native gate.

The backend contains no fixture symbol catalog, loader or process-symbol
fallback. It registers real, nonnull caller-owned code addresses for resolved
imports. Cranelift 0.137.1 installs dlsym in with_isa, so a newest-first lookup
guard panics on any unexpected backend lookup rather than falling through.
Missing/mismatched bindings instead return ordinary owned diagnostics with the
source symbol, library, document label and byte span before any object or JIT
output exists. The existing explicit JIT free, partial-failure cleanup,
PERF_BUILDID_DIR rejection and finalization-before-publication rules remain.

## Version-3 foreign native result

On 2026-11-04 the exact original foreign source passed five separately admitted
stages: Chibi harness compilation, normal JIT, the expected PERF_BUILDID_DIR
failure, C link and linked C execution. Independent aggregate acceptance passed,
with SHA-256
`e5b5d6eacc20e4058801c2ca45b4f93f394ba93d005d00c373d9cb4adf329539`.
The [normalized evidence](../evidence/foreign-20261104.json) records source,
tool, prerequisite, receipt and artifact identities without copying raw receipts
or claiming a runnable reproduction specification.

Five Chibi definitions call six real C imports. Eight registry expectations
produced **192 JIT value checks across nine ready owners** and **eight linked C
assertions**. They cover full-width signed values, bool ABI/results, eight-argument
passing, left-to-right nested calls, discarded-call effects and both short-circuit
edges. One additional normal-run owner rejected `chibi_slice_absent` with the
exact document 100/span 73..105 diagnostic and no callable/object output. A
separate PERF control likewise returned one owned failure without output.

The admitted current builder (`7b17b5bfdaed144e0949afd065db723bb45c8bd55b356cb70910d6df42e6d266`)
uses LLVM 22 to compile the O0, jobs=1 Chibi harness. Cranelift 0.137.1 compiles
the source-produced typed bodies. The non-test backend library SHA-256 is
`90f794a3adeea2b82894029b53faaa370f8fa866a09e1a9357e71ea4d05580ca`;
its independent construction/artifact acceptance is
`6281c0953e57744d2eba3b985ff20b359bb1d6ad6d3fff0e0b12c6ca8a6b814f`.
Both native links use the same real C archive,
`d6b16ff09a1bc6a230c727199a24041ca033fc014441e37384e8113011179fee`.

The copied 1,696-byte object has five exports, six imports, 361 code bytes and
12 GOTPCREL call relocations. The final 8,792-byte executable preserves that
code and 295 selected archive-code bytes after relocation, and needs only libc
and the loader. The actual C trace and 32 headers were checked. C-link stderr
is **not empty**: it includes the disclosed MPFR 4.2.1-header/4.2.2-runtime
warning. Review found no compile error and retained the already-fenced MPFR
library; this O0 caller uses integer/bool operations. Chibi/JIT/PERF streams
and AOT stderr were empty; AOT stdout was the exact eight-check success record.

The first native attempt failed before backend compilation because its test
oracle expected span end 104; the retained parser includes the zero-width
semicolon at 105. Commit `d38edd3a7702ef3a73b5441c19aaaa32549a8aa3`
corrected only three test/generator files and added the pure-source regression.
Its failed receipt and closure remain preserved; the original fixture and
production implementation were unchanged.

This is a bounded Linux x86-64 System V correctness result using a separately
admitted private controller. The public runner remains c5-pinned and its whole-
run workflow was not rerun. Current-v3 nativecases/scalars/direct_calls/modules
coverage, a small real tool, full compiler/runtime and platform qualification,
and non-incremental 100,000 LOC/sec remain open. Phase 2 is not complete; the
47.001-second O0 and 96.516-second O2 baselines are unchanged.

## Package-aware checkpoint

This historical package-aware checkpoint used the unchanged version-2 Model,
Verify, Pack, backend library, C ABI and dependency pins. The exact
original [modules Main](../slice_cases/modules/Main.chibi),
[Math](../slice_cases/modules/math/Math.chibi) and
[private-import rejection](../slice_cases/rejected/private_import.chibi) now
reach the same native harness through ordered `typed.Source[]` inputs.

All four selections passed Chibi compilation, normal JIT, expected
PERF_BUILDID_DIR failure, C link and linked C execution. Independent terminal
acceptance checked all 20 exact stage chains, final source/tool/runtime fences,
artifacts, loader dependencies and linked function bodies. The result covers
46 source functions, **2,040 normal JIT value checks, 36 normal owners and
85 linked C assertions**, plus 17 source rejections in each normal/PERF set
(34 total). The four C executables emitted their exact 45/15/23/2-check success
records with empty stderr.

The independent acceptance-summary SHA-256 is
`670612cdd07998b8ccff02ede3dc0accba41a88502004068d596e7a9c207a9b1`.
The [normalized evidence](../evidence/package-aware-20261104.json) records exact
source, per-stage receipt and artifact hashes. It is a public summary, not a
runnable reproduction specification or a copy of the raw receipts.

The modules object exports only Apply. Dependency Double and Hidden are local
`.vnext.2` and `.vnext.3`; its 59 code bytes contain one direct Apply-to-Double
call and no external helper, initializer or fallback. The earlier three copied
objects are byte-identical to their accepted pre-package counterparts. Actual
C link readback preserved all 46 function bodies and resolved all 24 direct-call
operands; final executables require only libc and the loader. These are bounded
code/ABI observations, not a runtime quality or throughput result. C linker
traces were observed; c5 transient linker selection remains static evidence,
not an observed trace.

The native proof uses exact public-preparer output and a separately admitted
private controller, the same c5 seed and the same backend library. The public
whole-run workflow was not rerun. The separately accepted package-aware pure
producer test used the current O2 builder; neither test admits the complete
current compiler, self-hosting or a default-toolchain switch. At that checkpoint
the original foreign calls/`missing_symbol`, equality/`&&`
and a real C call from Chibi remained open; the current version-3 result above
covers that separate bounded gate. These version-2 receipts do not validate
current-v3 nativecases/scalars/direct_calls/modules. Real tool/compiler workloads,
non-Linux execution and 100,000 LOC/sec remain open. Historical results below
and existing O0/O2 baselines are unchanged.

## Version-2 verification status

The [six-stage pure-source proof](../frontend/typed/README.md#accepted-version-2-pure-transform-result)
passed the pre-package producer/verifier, generated Pack and seven standalone Rust
framing tests. A separate fresh Rust proof passed the test build, all three
focused tests unfiltered and the production panic-abort cdylib build. It kept
the pinned toolchain, Cargo.lock and dependencies unchanged. Independent review
checked the exact library, exports, observed linker inputs and recursive runtime
closure before any integration use.

The version-2 Rust test receipt SHA-256 is
`2e538153d005b4cd39b9a01b89486bd35665a212158693cb65f62d5aaa4a6008`;
the cdylib build receipt is
`e5c7af513c1f536298d64f34dbd8825ab28cb19a5c6838395b8b922e28ea116d`.
The resulting library SHA-256 is
`39fc5a692bf4a7b220a48976245319595dcd1b1de2b60a16ea47771c17140790`.

All three cases passed five separately admitted stages: Chibi caller compilation,
normal JIT execution, the expected PERF_BUILDID_DIR failure, C link and AOT
execution. These are fresh exact-source private proofs using public-preparer
output, not a rerun of the public `run_native.py` whole-run workflow.

| Case | JIT value assertions | Linked C checks | Registry source rejections |
| --- | ---: | ---: | ---: |
| nativecases | 1,080 | 45 | 0 |
| scalars | 360 | 15 | 9 |
| direct_calls | 552 | 23 | 0 |

Each case exercised nine complete owners; each separate PERF control returned
an owned diagnostic with no callable/object output. NativeCases also retained
its seven separate producer-rejection controls. The 23 supplemental oracles
cover nested/private/forward/self/mutual and zero/32-argument calls, mixed scalar
ABI positions, true/false literals, and signed strict greater-than. Tiny
terminating recursion does not qualify depth or stack exhaustion.

Independent terminal readback verified all 15 receipt/launch/approval chains,
exact artifacts and input/process cleanup. Chibi/JIT/PERF streams were empty;
each C executable emitted its exact success JSON with empty stderr. C compiler
and linker traces were intentionally verbose and separately reviewed, including
the GCC MPFR header/library-version banner warning. The c5 driver's selected
link inputs were checked statically; no observed trace of its transient link
inputs is claimed.

The independent acceptance-summary SHA-256 is
`fcd1941e5827b69005adbf7fcc34cc698b1127d5dff5dce2a648b70b3be3039c`.
It binds all three case readbacks and 15 stage receipts, closed on 2026-11-04
before the later documentation and generator-comment updates.

Only Linux x86-64 System V is in scope. These bounded source-generated results
do not admit the current compiler, self-hosting, the full original corpus or
representative runtime/compilation performance. Historical version-1 results
below remain bound to their own frozen inputs.

## Accepted bounded result

The independently reviewed version-1 source-native proof on 2026-11-04 passed
all five stages: Chibi caller compilation, normal JIT execution, owned environment-failure
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

The accepted version-1 subset was the then-current
[bounded producer](../frontend/typed/README.md), not the whole
[slice corpus](../slice-design.md), and excluded direct/foreign calls and several
scalar operations. Version 2 implements direct calls and the remaining original
scalar operations, with its validation status recorded above. The subsequent
package-aware and version-3 foreign implementations have separate proof status
above; neither broadens this version-1 result. The experimental rule that all
admitted i64 additions/multiplications wrap, even literal-only expressions, is not current-language constant-expression parity.
There is no current-compiler, full-compiler, real-tool, cross-platform or
compilation-speed acceptance.

## Caller and ownership contract

The library target is cdylib, with no staticlib fallback. Keep the shared library
loaded until every result owner has been destroyed.

All symbols below use the platform C ABI. Counts are elements, not bytes;
all handles are opaque `void *` (Chibi rawptr).

1. `chibi_vnext_compile_both(words: const uint32_t *, word_count: uint64_t,
   literals: const int64_t *, literal_count: uint64_t,
   names: const uint8_t *, name_byte_count: uint64_t,
   bindings: const ChibiVnextForeignBinding *, binding_count: uint64_t) -> void *`.
   Return an owned result after eager object and JIT completion, or an owned
   failure diagnostic. OOM aborts. Metadata borrows end during this call;
   imported code remains caller-owned until every JIT owner is destroyed.
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

[foreign.h](foreign.h) is the common C declaration and layout contract. Append
`bindings: rawptr, binding_count: u64` to the six existing Chibi foreign-call
parameters. A descriptor is 64 bytes/alignment 8 on Linux x86-64 System V:
library pointer/length at offsets 0/8, symbol pointer/length at 16/24,
parameter-type pointer/count at 32/40, u32 result type at 48, natural four-byte
padding, then code pointer at 56. Both Rust and C pin every offset. Scalar IDs
come from the generated typed schema: bool=1, i64=2. Code addresses must have
the exact declared System V signature, including zero-extended bool parameters
and results. Pointer checks cannot prove executable storage or signature truth.

At most 32 descriptors are accepted, including unused bindings. Every supplied
descriptor is validated: library spelling is 1..256 UTF-8 bytes, symbol spelling
is 1..65,536 UTF-8 bytes, neither contains NUL, parameter count is at most 32,
all parameter/result types are scalar, and code addresses are nonnull. Repeated
symbols are rejected even across libraries or with identical signatures,
because AOT link names cannot distinguish them. NULL/zero descriptor arrays
support modules without imports. Metadata is never retained; the caller must
keep imported code loaded and callable through all generated calls and owner
destructions. The copied object contains ordinary external relocations, not
copied code addresses, and requires its own native link against the provider.

The current frame decoder requires version 3 and checks counts/exact framing.
Version-1 and version-2 input is rejected, with no compatibility translation. This is explicitly
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

Both parameter and result bools receive explicit System V zero extension,
including direct-call signatures. The candidate declares every function before
lowering any body; private, forward, self and mutual calls use dense IDs and
never recursively compile bodies. Every edge argument is forwarded, including
both conditional edges. Tiny terminating recursion cases do not qualify
runtime recursion depth or stack exhaustion. Every body is compiled before
JIT finalization and publication. The explicitly enabled
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

The three earlier focused Rust tests in lib.rs passed without filtering in both
the historical version-1 proof and the separate version-2 proof above. Public framing/count
rejection covers null/nonzero pairs for each scalar buffer and
excessive counts on actual small buffers, with shared owned-diagnostic,
no-output and destroy checks; byte copy is exact or leaves
the destination unchanged; an injected finalization failure after real JIT
allocation calls the explicit free operation once. The last test lowers a tiny
typed function through the existing lowering, finalizes its code into the
provider's leak-on-Drop mappings, allocates a new current mapping, then injects
failure. Kernel /proc/self/maps metadata checks that explicit free releases the
finalized mapping and subsequent provider destruction releases the current one.
It never dereferences stale/fabricated pointers. This test-only memory provider
is not a production mode; source-produced acceptance is a separate
gate recorded under each version's result. Generated enum/framing tests are owned by
frontend/bridge/tests/arena_tests.rs. A production library requires panic=abort;
Rust's unit-test harness uses its ordinary test panic behavior.

Four added tests cover malformed descriptor pointers/counts and
metadata, duplicate symbols across libraries, exact library/symbol/signature
resolution with source-origin diagnostics and no outputs, a compiled import/call
and equality wrapper after every metadata buffer is poisoned/dropped, and the
no-fallback lookup guard. The
existing three tests remain, including injected finalization-failure cleanup.
The [current seven-test result](#version-3-foreign-binding-source-checkpoint)
passes all of them. The lifetime test executes its generated wrapper against a real Rust C-ABI function;
its synthetic model is only a focused backend test. The eight original foreign
oracles and missing-symbol rejection have the separate source integration
result above; these focused Rust tests do not establish it themselves.

Current adapter accounting: descriptors occupy 64 bytes each (384 bytes for the
six-import fixture, 2,048 bytes maximum), with temporary borrowed views and an
8-byte resolved address per import. Metadata validation runs once. Duplicate
checking costs at most 496 symbol comparisons and source resolution at most
1,024 library/symbol comparisons; signatures compare at most 32 scalar IDs per
match. These bounded scans never occur per instruction. Symbol lengths remain
bounded by the 65,536-byte source admission; pathological long common prefixes
can raise duplicate comparison traffic to about 31 MiB, and are not measured
here. Both function passes consume sparse import membership/declaration linkage
without adding a function flag table. No speed claim follows from these counts.

The retained adapter does borrowed name adaptation, then one instruction
traversal for Object and one for JIT. Duplicate semantic verification and its
4-bytes/value plus 1-byte/block scratch are removed. Each lowering reserves a
block map, a statically pinned 4-byte PackedOption<Value> per value, declaration
IDs, and two edge-argument buffers of at most 64 entries. Version 2 adds one
reused call-operand buffer of at most 32 entries and a dense, body-local
FuncRef cache of at most 32 slots, importing only referenced callees. The
declaration pass is necessary for forward/recursive calls; body lowering and
compilation then proceed once per function per output, in source order.
Context signature buffers reserve known arity and reuse declaration-owned ABI
facts without a separate staged signature vector. Cranelift's required
declaration-owned signature copies remain. Context and FunctionBuilderContext
are reused; Cranelift's IR/code allocations remain additional costs. Work is
linear in rows/arguments plus the explicitly bounded per-body cache clearing.
The current Echo/Join frame is 588 borrowed bytes. No measured speed claim
follows: `compile_both` intentionally compiles both outputs and is not a fair
single-build benchmark. LLVM could consume the same neutral typed facts through
a separate lowering; no LLVM consumer or measurement is included here.

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

The version-1 final library-only Cargo configuration passed a fresh independent
build/test proof, preserving Cargo.lock and all dependency pins. All three
backend tests passed at receipt SHA-256
`4cfeb855ecc526e51e984dd0e1105049cc982dfd7bdf842d2386e955541978a2`.
The production cdylib build receipt is
`1f15fb9958178aa3f497b35a38c5ebfb0c1ce5e20cb16c19feecbe407a61d968`;
its library SHA-256 is
`02e8957df08742cce7a2b40b79c21dd6569a93c442ab594eac044d6f750ea30e`.
The exact library, observed linker trace and recursive runtime dependencies
passed independent review. This was a separate proof, not a rewrite of old
receipts; it does not validate the current version-2 source.

The [public source-native preparation and runner](../frontend/bridge/README.md#source-native-correctness-reproduction)
snapshot real sources and use an existing backend library. Their normal
five-stage version-1 workflow passed once with the final library above, followed
by independent terminal readback. The 54 Python preparation/generator/controller
controls also passed. A separate real-first-compilation cancellation control
reproduced the old interstage bug and confirmed the fixed runner stops before
any successor launch. This public correctness reproduction does not reproduce
the private proof's full tool/loader admission fences.

The three version-2 runtime matrices passed as listed in
[verification status](#version-2-verification-status). The original
registry still has 25 calls and 11 rejections. The two original package calls
and `private_import` have the separate historical version-2 checkpoint above;
eight foreign calls and `missing_symbol` have the current version-3 result.
The four older selections still need fresh current-v3 native acceptance.
The 23 supplemental call oracles extend coverage; historical results cannot be
combined into full current-v3 corpus acceptance.

Full current-v3 corpus coverage, representative real-tool/runtime quality,
phase/memory measurements, platform qualification and distribution notice review
remain later gates. No timing from these fixtures establishes
100,000 LOC/sec or a speedup.
