# Opt-in owned scalar projection → LLVM object

This is a bounded consumer checkpoint, not the default compiler or a general
semantic IR. The consumer imports only `scalar_facts`, the reviewed pure LLVM
emitter, `native_abi`, and core strings. It does not import `package_compiler`,
private `Code`/`Slot` records, the historical `Native` accessor, or the diagnostic
integer-packet adapters. The existing authored `Driver.Object` is unchanged.

## Contract and entry points

`scalar_facts.NullaryPureInt32Return` is the exact producer-owned leaf from
`e540fd698f0cac3a25ba011242efc6743ad2ef3f`, source SHA-256
`d84b75ce7ffc8fdacb02ae22e50830b75cacb7164c27fa673b75b42a6b100d24`.
The accepted record certifies one ordinary nullary `int` function with known
zero effects whose **entire body** is one direct integer-literal return.
Unsupported signatures, managed values, calls, assignments, additional
statements and control flow are not newly supported here.

`tools/compiler/scalar_llvm/Emit.chibi` provides:

- `Validate(projection) -> string`: empty means the bounded representation is
  valid, not that its provenance is authenticated.
- `Emit(projection) -> Result`: `Rejected` preserves the producer's complete
  canonical rejected record, `Invalid` explains a malformed/unsupported
  representation, and `Emitted` contains LLVM text.
- `EmitBytes(projection) -> byte[]`: the same emission as raw counted bytes;
  rejected/invalid records return an empty array, without deliberate traps.

The consumer bounds source text to 16,384 bytes, checks canonical accepted and
rejected fields, half-open ranges, nonempty nested accepted body ranges, and
LF-based one-based byte positions. It does **not** parse the source again or
prove that a caller-fabricated accepted value corresponds to that text. The
normal pipeline must bind the actual trusted producer artifact, source bytes,
and unchanged result envelope. The tests deliberately show that a coherent
fabricated value is not an authentication mechanism.

The fixed backend fixture policy is `chibi_scalar() -> i32`, with one existing
entry `CHECK` and positive budget 1. Neither symbol nor budget is inferred from
the source function name; this adds no public ABI, general runtime budget,
arithmetic or control-flow semantics. The admitted literal is passed directly
to the existing emitter's typed constant/return instructions.

## Phase and tool boundary

The existing compiler-host serializes the producer result into an owned JSON
output before producer-process exit. Its exact output bytes are then supplied
as the consumer's argument envelope. No host/Python semantic projection or
source-to-IR field mapping is used. The separately compiled consumer contains
only the leaf and backend dependencies. This demonstrates a process-separated
owned handoff; it does **not** establish same-process retention after frontend
release or measure preparation high-water storage.

Invoke `project:scalar_llvm:1:EmitBytes:callable` through the existing
compiler-host `--output-bytes FILE` option to obtain raw LLVM IR, not a JSON
string extracted by a harness. The current CLI rejects an empty raw result
before publication (`the compiler entry returned no bytes`); failed emission
leaves an absent output absent and preserves an existing output. Use `Emit`
separately when typed diagnostics are required. These are existing host
semantics, not a new transport or host wrapper.

Feed that file to the existing authored driver:

```text
chibi-build-host --root REPO --source tools/native_aot/Driver.chibi \
  --core REPO/packages/core --compiler REPO/tools/compiler \
  --compiler-core REPO/packages/core --entry Object --timeout-ms 120000 -- \
  ABS_INPUT_LL ABS_FRESH_OUTPUT_DIRECTORY ABS_OPT ABS_LLC 0
```

Mode `2` instead runs LLVM's actual `verify,default<O2>,verify` pass pipeline.
The driver publishes its existing four-file object receipt without inventing
`main`, a source wrapper, or a linker step. See [driver contract](../driver/README.md)
for trusted-tool, target/layout, no-replace and interruption limits.

## Focused evidence

The consumer and authored `Tests()` compile identically with bytecode and
SLJIT compilers. `Tests()` returns 0 on both, with 7,305 selected calls and no
fallback/overflow. The independently retained pure emitter/ABI baseline is
`fd64d8349a8bf6ad89308100f315be9524897452`; this checkpoint does not change it.

- Consumer artifact SHA-256:
  `eb415eb0647079e5863adcdae4a55f965348c755284a6782d61b327ddcef4e93`.
- Authored test artifact SHA-256:
  `4bb419f22a79861281ae24eb7bdba694b0522f59ed645d280bb9cf762e67b120`.
- Actual producer artifact SHA-256:
  `6a15507da9397cf20f9fd3753631bf75b2beb9782e37bfd6ab5319873d27c2e0`;
  producer source checkpoint
  `a4df25ea53c47f2740bce368607957b98fd81c38`. Its full source mapping is in the
  [immutable producer checkpoint](https://github.com/waldnercharles/chibi/blob/a4df25ea53c47f2740bce368607957b98fd81c38/docs/handoffs/2026-10-23-owned-scalar/checkpoint.json);
  producer semantic review/integration remains separately owned.

The exact producer's 43 source cases were replayed with both producer VMs,
then each unchanged output envelope was consumed on both VMs (172 crossings).
Five sources were accepted (42, zero, `INT_MAX`, hexadecimal, and comments);
38 were rejected. All rejected records retained every source/diagnostic field,
returned no raw IR, and preserved prior output bytes. All four successful
crossings per source emitted identical IR. Real unchanged `Driver.Object`
O0/O2 runs produced ten objects, with no undefined symbols; validation-only
native calls returned the expected full int32 values. This is actual producer
and consumer execution, not fabricated instruction-table source coverage.

The `42` source is exactly `package scalar\nMain :: () -> int { return 42; }`.
Its source SHA-256 is
`08cd7eefd36c9b36cd7856e82a7039832dbd27e1ff931d0e08bb198037dad78b`,
unchanged producer envelope SHA-256
`d3661d6c2b02ee20029bdbc3cefa05003f8018c406e38055137f293b79400b76`,
and raw IR SHA-256
`bd60479b447badd2a29144b89a0b00820555557697cfacd11270772485ed1f93`.

The independent coordinator rebuilt the exact consumer and tests on both VMs,
reproduced all 172 crossings and ten real O0/O2 object/native results, and
passed nine additional malformed-representation controls per VM. Those controls
returned typed `Invalid` and left absent raw outputs absent; the genuine
rejected-source runs also preserved existing outputs.
Independent report SHA-256:
`62bfd0169d12b22a22a27f74223608c19fb1545463ffe0caad79c068e849334b`.
This closes the bounded consumer review gate, not production integration.

For focused compilation through the existing `CompileClosure` entry, map
`tools/compiler/{scalar_llvm,scalar_facts,llvm_native,native_abi}` into those
same project-relative package directories and include core strings. Select
`scalar_llvm/Emit.chibi` as the first/root source. The test closure additionally
maps this directory's `Tests.chibi` to `scalar_llvm_tests/Tests.chibi` and selects
that as root; invoke `project:scalar_llvm_tests:1:Tests:callable`. No unrelated
compiler prototype packages or historical native adapters are required.

The official Linux SDK formatter passes the new Chibi files. Normal repository
C test dispatchers, full platform builds and CI were not run for this opt-in
checkpoint.
The current C compiler/build hosts and validation harness remain migration
debt: semantic lowering and LLVM orchestration are authored Chibi, but this
checkpoint does not replace the current runtime. Validation-only shared-library
linking/native invocation is not production Chibi interop; no first-party C
source, C transpilation or C shim is linked into the emitted object.

## Portable recovery regression

The local integration deliberately refreshes the checked-in compiler artifact
and bootstrap manifest through the existing host's supported `--seed` command,
with revision `a4df25ea53c47f2740bce368607957b98fd81c38` and compiler SHA-256
`6a15507da9397cf20f9fd3753631bf75b2beb9782e37bfd6ab5319873d27c2e0`.
It does not change the target ABI, runtime pin, source/package guards or default
backend selection. The additional scalar entry remains explicitly invoked.

After the unchanged two-stage self-build, `Run.ps1` binds the current pinned
runtime, recovered stage2, compiler source/package inventories and regression
sources. It compiles `Regression.chibi` on both VMs and requires identical
prepared bytes. Authored `Cases` supply the 43 sources and semantic assertions;
`Regression.Check` checks the complete consumer rejection or emitted result.
The PowerShell runner performs only transport/identity/execution checks: it
preserves uint64 target identities with `JsonNode`, checks exact requested
source identity, and forwards the original producer stdout file unchanged.

Every source traverses all four producer/consumer VM combinations. Successful
calls require selected-backend entries with no reported fallback or overflow.
Expected empty raw-output failures have no counter line in the existing host;
their matching successful authored check executes `EmitBytes` on the selected
backend. Those raw failures must report exactly the empty-result diagnostic,
leave absent output absent and preserve a prior output in each VM crossing.
The runner owns each child and its cleanup, with a 120-second maximum per call
inside a 180-second overall budget; workflow permissions, matrix and job
timeouts are unchanged.

The comparison step checks portable regression artifacts, ordered source
identities and emitted LLVM IR across the three configured platforms. It does
not compare runtime binaries or native objects across platforms. This wiring
does not itself claim three-OS execution: local Linux validation and independent
review must be recorded for the exact candidate, and real matrix CI remains a
separate gate. Native `Driver.Object` execution and the separate test-only
same-process lifetime regression are not silently folded into this portable
workflow or represented as phase-memory measurements.
