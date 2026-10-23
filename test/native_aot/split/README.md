# Checked scalar-plan composition diagnostic (WIP)

These are **test-only authored adapters**, not a permanent/public IR or a
production compiler entry. They test source → shared frontend → checked scalar
plan → existing LLVM emitter in separately prepared closures. They do not
establish that separate processes are required or authorize wider compiler,
runtime, typed-memory, foreign-call or ABI21 changes.

The tested combined current-frontend scalar-proof closure still exceeds its
unchanged aggregate preparation-storage budget. The smaller frontend and
emitter closures pass the same loader guards. No compiler/runtime limits were
raised, and no Python/C semantic lowering or metadata repacking was used.

## Build and invocation boundary

Use the existing compiler-host `CompileClosure` protocol with explicit source
selection, rather than globbing unrelated compiler prototypes. Give each root
its package-relative virtual path below. All ordinary compiler sources come
from the bootstrap source manifest; include core strings once.

- Front root `test_native_front/Front.chibi`: ordinary compiler sources, the
  bounded `package_compiler/Native.chibi` accessor, `native_abi/Abi.chibi`,
  and this root. It does **not** import `llvm_native` or `native_compiler`.
- Back root `test_native_back/Back.chibi`: `llvm_native/Emit.chibi`,
  `native_abi/Abi.chibi`, core strings and this root.
- Byte probe root `native_byte_probe/Probe.chibi`: core strings and this root.

`project:test_native_front:1:Lower:callable` accepts project, path, source,
triple, pointer_bits, main, step_budget, returning one int[]. Its **complete JSON
stdout file**, `[int[]]`, is the unchanged argument file for
`project:test_native_back:1:Emit:callable`. No harness interpretation of opcode,
type, slot or source metadata is needed or permitted by this experiment.

This diagnostic accepts **NUL-free source**. Front rejects the first embedded
NUL before the shared parser, preserving its byte offset/range and document0.
Its guard counts byte columns from1 and resets them after LF; CR is a byte,
so CRLF advances the line at LF. Textual escaped-zero sequences are not NUL
bytes. This narrower admission avoids an inherited current-frontend host
nonreturn observed for `ret<NUL>urn`; it does not repair the shared parser or
claim structured rejection for every possible malformed source. Diagnostic
byte transport itself still preserves NUL and arbitrary byte values.

The back entry returns one string[]: diagnostic, start, end, line, column,
document, LLVM IR. Successful output has six empty strings before IR; rejected
output has empty IR. This is a logical Chibi string[]: the existing document
writer represents an invalid-UTF-8 diagnostic as a `{"bytes":[...]}` JSON value
rather than coercing or dropping its bytes. Valid UTF-8 diagnostics and emitted
ASCII LLVM IR remain JSON strings. The harness may extract IR for LLVM verification/object
execution and compare diagnostics, but the admission and mapping stay Chibi.

The byte probe returns raw int[] bytes, avoiding reliance on rendered text.
`Bytes` must return `[0,127,128,195,169,255]`; `Diagnostic` must reproduce the
UTF-8 bytes of its authored diagnostic string.

## Test-local packet

Success header (10 words): version1, status0, source-byte-length, main0/1,
positive CHECK budget, result storage, parameter count, slot count, local count,
instruction count. Follow with parameter storage words, slot `(kind,storage)`
pairs, local pairs, then 16-word instruction rows:

```
shared opcode index, type, output, left, right, target,
int(wrap: item.payload), offset, has_text0/1, value_storage,
value_container, value_record, start, end, line, column
```

After the rows: name count and `(byte length, byte words)` for each opcode name.
Names are self-describing index targets, not stable frontend ordinals.
Renumbered/duplicate supported names can represent equivalent safe plans; used
unsupported names reject. The symbolic mapping is the existing scalar
`NativeOpcode` mapping and references the existing emitter's constants.

Failure packet: version1, status1, source-byte-length, start, end, line, column,
document, message-byte-count, message bytes. Pre-source failure uses neutral
range/source length. The original shared frontend still checks nominal,
reference, container, default-value, effect and runtime-service metadata before
NativePlan creation. The transport retains every Code/Slot admission field used
by the original EmitPlan; its consumer validates them before constructing the
emitter's smaller Instruction record.

Bounds are explicit and checked before extent arithmetic/indexing: packet
length ≤20000; source bytes ≤16384; parameter/slot/local/instruction counts
≤8/128/64/256, with at least one instruction; positive CHECK budget; at most
64 names of 1–32 uppercase ASCII/underscore bytes. Diagnostic byte values are
0–255 and message length is 1–16384. Source ranges satisfy
`0 <= start <= end <= source length`; line/column are 0 through source length+1
(0 permits synthetic positions); diagnostic document is -1 or0. Exact extent
is required, with no trailing words. The existing emitter remains responsible
for scalar types, canonical booleans, operand indices, dominance and control
flow, wrapping arithmetic and checked failures.

Producer status1 rejection and malformed/unsupported status0 rejection are
separate test outcomes. This encoding is diagnostic: it exposes internal
frontend names/indices, not canonical semantic facts for shader or other
consumers. Do not freeze its word offsets as a public API.

## Recorded evidence and provenance

The measured current-frontend source base was
`fa1b7b2bb297eacbbed6aec6f03c4260355db97c`; its compiler files remain unchanged in
`5c28191b3ea53afa232c34e37b14fd9b01024aef`. The existing f190 native accessor
required only binding the added Body.Emitted effects payload and rejecting
nonzero effects. That reserved-file delta is **not published by this test-only
checkpoint**; a current-main integration still needs compiler-owner agreement.
The retained f190 branch is not silently relabeled as current main.

The runtime host came from successful
[owner runtime-seed run 39883209896](https://github.com/waldnercharles/chibi/actions/runs/39883209896),
Linux artifact12470793845. ZIP SHA256:
`7f5764f4e15752c71b7b4cf6b82404cbac515c161fef4a00420689a83f0532bd`;
inner ELF64 x86-64 host SHA256:
`0d0783e39e8547853c1310945ea4d508c05ae4e8174d4a976e499853b136be1d`.
Its libc/libm dependencies were verified before execution. The ordinary
current-frontend compiler seed was
`41905e8bc4afc3998cf48097a98d6c1ca0ce89ff2de6a6dadcffb6fb06f1c71e`.

| Frozen authored source | SHA256 |
|---|---|
| Front.chibi | `95221a7f3a4b348992790f7cf27115758e55508950301afc9751f02f80f5f122` |
| Back.chibi | `cb9dc0e9ae0227dbe72eab20b4e5f273e27113faabb807467ae2ae78cb75a19e` |

The admitted front/back closure hashes were
`a8b1793f23f4add35d427ad8177c1685c5fdb41790197bbf154918a06f4544dd` and
`a859ab34adbee34772150f4e87a3482e4415b42d97706cb3a540733b5cf6aaa5`.
They are evidence identities, not checked-in generated artifacts. The NUL-free
front compiled with 20,066,931 actual SLJIT calls and zero fallback; the unchanged
back used 1,334,347. On the literal42 source the stages reported 1,847 and123
actual calls respectively on each selected VM, with zero fallback. The prior
pre-NUL-guard source/artifact/evidence was retained separately, not overwritten.

The existing 16-source corpus in `../proof/Proof.chibi` passed both authored
stage backends with zero fallback. LLVM19 parsed/verified the resulting IR and
emitted objects at O0 and actual `default<O2>`, then standalone native programs
ran: 18 exact scalar successes and 14 expected SIGILL terminations. The 32
ordinary current-frontend bytecode/SLJIT comparisons used the same source and
CHECK budgets: successful values matched exactly; the deliberate failure cases
returned nonzero with `compiler execution did not return`. Those VM logs do not
identify an exact trap cause or report selected-backend counters, so no detailed
VM/native failure-cause or diagnostic equality is claimed. Native checks retain
unused failing arithmetic and exact budget exhaustion. Native objects had no
undefined first-party runtime symbols.

Chibi performs the source semantics, packet admission/opcode mapping and LLVM
IR emission. A **validation-only local harness** then calls the installed
LLVM19 C API to verify/optimize IR and emit objects; `cc` is used only as a
linker driver, with no generated or first-party C source in the native result.
That harness is not production Chibi-to-LLVM interop or link orchestration.
The current first-party compiler host and this local harness remain migration
debt; the experiment neither replaces the current runtime nor completes an
all-Chibi toolchain. Ordinary third-party native startup/linker dependencies
are distinct from the absent first-party Chibi runtime in these scalar objects.

The focused byte roundtrip passed on both backends for NUL,127,128,195,169,255
and a multibyte diagnostic, compared through `text_bytes`.

Independent exact-candidate behavioral review passed the bounded diagnostic:
- Both front/back artifacts rebuilt byte-for-byte under both compiler backends.
- Per backend: 82 source admissions (8 emit,74 reject),688 packet checks and15
  malformed typed-JSON cases, including simultaneous table maxima and strict
  canonical truncations; unsupported metadata/type/op/range/count cases reject.
- Per backend: 14 NUL placements,4 escaped-zero controls,3 raw-byte NUL cases,
  and5 existing syntax-position oracles pass. The guard's byte coordinates
  agree with existing LF/CRLF/CR/multibyte conventions on NUL-free analogues.
- All256 byte values, UTF-8/NUL and16,384-byte diagnostic roundtrips pass.
- Independent native replay: 21 source programs plus table-boundary probes,
  46 LLVM-verified O0/`default<O2>` objects,36 full-value successes and16 exact
  SIGILL traps. Its48 ordinary current-fa VM comparisons establish matched
  success values or nonreturn, not detailed trap-cause/counter equality.
- All164 prior source-admission packet/diagnostic/IR identities and all21
  independent native source/packet/object/results remain unchanged by the guard.

The preserved independent review report SHA256 is
`675fafdca5ccda2a6aea65ad357fff658f7b3ded36bd0a6b5bab0d54b60b2eba`.
The intended diagnostic difference from historical f190 is the oversized-body
case: Front now rejects with `native scalar plan exceeds transport limits`
before the later emitter's table-size diagnostic. Producer acceptance is not
sufficient without consumer/emitter validation, and this self-describing packet
is not source-authentication evidence. Independent source/doc review is separate
from these behavioral results. This remains a WIP test-only checkpoint, not
current-main integration, merge readiness or a completed native-island gate.

The repository formatter, normal test dispatcher and platform CI were **not
run for this test-only checkpoint**. Focused compiled proof and independent
validation described here are not a substitute for those integration gates. No new
workflow run or compiler seed was published.

## Integration recommendation

The preparation failure is dependency-sensitive, not proof that canonical
retained storage alone exceeds the cap. The Native-only compiler module is
byte-identical to that in the failed combined closure. `package.c` loads all
modules and recursively resolves imports against one budget. `prepared.c`
reserves preparation scratch and live-set growth before reconciling canonical
storage; #665 also prepays both temporary record-proof tables. Their scratch
is refunded only after module acceptance. A generic storage failure does not
prove every later layout/verifier check passed.

A compiler owner should decide: (1) the native-only entry and checked facts it
owns, retaining source diagnostics/effects without exporting private Code/Slot
IDs as canonical shader facts; (2) dependency factoring versus separately
prepared phase lifetime, based on measured total peak; and (3) an intentional
in-process or versioned interchange representation. The scratch encoding alone
settles none of those decisions.

Current capability limits explain this experiment's primitive envelope:
`package_compiler/Compile.chibi` rejects named results containing strings,
unions or containers and restricts ordinary record fields to scalar types;
`compiler_host.c` invokes root exports only; `value_document_read.c` rejects a
scalar union argument. No language feature, document reader or host was changed
to bypass those limits. Production integration needs its own owner-approved
source changes, exact-head independent review and unchanged-budget corpus gate.
Foreign allocation/typed memory/output/free remains a separate native-island
milestone; this is still a runtime-free **scalar** native proof only.
