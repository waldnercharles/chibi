# Flat owned-record cell fixtures

This is an authored fixture inventory, not a passing-test or platform claim.
Run `sh owned-records.sh NATIVE_COMPILER LLVM_BIN WORK` only after the exact
source inventory and execution setup have been independently approved. The
runner does not invoke `run.sh`, any task selector or an ordinary auto-build.

## Source scope

`source/OwnedRecords.chibi` contains only direct scalar/bool/Text/pointer
record cells. Its pointer-linked cycles are two flat records. It creates no
map, union array, nested aggregate cell or collector-bearing record array.
The temporary byte arrays in Text() produce counted strings; they are not
cell fields. Metadata depth is at most two record types, not a new depth gate.

Main covers constructor evaluation once; borrowed literal construction;
counted and embedded-NUL Text; alias and self-assignment; snapshots before
replacement during later operand evaluation; scoped Text-field and record
writes; boxed-variable and whole-cell replacement; empty Text; escaped return
and parameter cells; distinct Int/Text nominal fields with code 37 becoming
42; direct bool/i64 fields; 2000 discarded owners; and 1000 discarded cycles.
Failure owns a record and pointee before its deliberate assertion. CountedCopies
performs 4096 whole-record replacements and must stay within 32 allocations.

The narrow negative source `source/rejected/OwnedArrayRecordCell.chibi` retains
a pointer-array field and must remain rejected. The old flat OwnedRecordCell
rejection is represented by the positive WordLength kernel instead.

## Exact proposed native inventory

- Main: O0/O2 × normal/forced collection × callbacks off/on = 8 cases.
- Failure: the same 8 configurations, requiring assertion status 134 and zero
  final live allocations through SourceProbe's existing cleanup oracle. The
  focused verifier also requires the intended terminal assertion's exact source
  byte range, cross-checked against emitted metadata and its actual entry store.
  Its receipt binds frozen source/IR hashes; each invocation verifies all four
  ordered children and replays stdout/stderr without filtering diagnostics.
- CountedCopies: O0/O2 × normal/forced collection = 4 cases, at most 32 total
  allocations and zero final live allocations.
- Images: reused-old-pointee and writer-created-pointee × O0/O2 × positive and
  one-site missing-import control = 8 cases.

Total: 28 runtime invocations, plus the narrow source rejection. Existing
Optional Text and nested-descriptor tests remain required regression controls,
but are not silently selected by this runner.

Both images are source-generated. Record.chibi's extracted prefix contains
Write and Literal but no constructor. RecordOwned.chibi adds MakeNumber,
which creates a typed Number record. A scalar `^int` cell has null layout and
no per-image metadata lease, so it would not prove the latter requirement.
The source-generated shape selects the observer's record types and semantic
tag; no guessed field or pointee tag is substituted. The shared allocation
audit's counter is host-owned and survives incoming-image unload.

The positive reused-pointee case requires exactly two old-image leases and
zero incoming-image leases before unloading the writer. It inspects all Text
owners and bytes again afterward. The writer-created-pointee case requires
one old and one incoming lease after collection, then releases the root and
requires both counts to become zero before unloading either image. Every
successful case requires zero final audited allocations.

The strict negative removes exactly one `ValueImport` call in the actual
source Write function. It does not clobber read-only bytes or skip acquisition,
drop, validation, publication or cleanup. The observer checks both nonempty
Text owners before byte access and checks the empty field's length. Missing
ownership cleans up with both images mapped, verifies zero allocation/lease
counts, and returns 33 before either image close. Process exit reclaims mappings.

## Focused Windows image inventory

`RunOwnedRecordImagesWindows.ps1 -Compiler NATIVE_COMPILER -Llvm LLVM_BIN`
selects only the eight image cases listed above. It builds two helpers, then
old/writer/broken DLLs at O0/O2 for the reused-old and writer-created pointee
modes. It never calls the broader managed/task runners or SourceProbe. Each
positive requires status 0 without diagnostics; each one-site bypass requires
status 33 and exactly the missing-Text-owner diagnostic. An unexpected status
or diagnostic fails the runner and preserves its command logs and artifacts.

The builder uses WindowsX64, the existing shared-image link plan and actual
generated module/start imports. The compiler supplies its existing family
exports; explicit fixture exports expose only the adapters used by the host.
The host uses `core:sys/windows` OpenLibrary/LibrarySymbol/CloseLibrary and the
same owner, byte, allocation and typed-lease checks as the POSIX path. Windows
outputs end in `.dll`; existing POSIX `.so` paths are unchanged. The CI step
retains the existing fixed-point producer guard and overall Windows deadline.

This is an authored Windows port, not a passing-test claim. Its focused local
validation should cross-link both DLL modes at O0/O2, inspect PE imports/exports
and the host's loader dependencies, and run the eight unchanged Linux cases.
Cross-linking and PE inspection cannot establish real Windows unload behavior.

## Remaining acceptance gates

The exact final OwnedRecords.Main source still needs ordinary/native comparison.
Ordinary dependency installation/download permission is a separate gate; this
runner neither requests it nor starts an ordinary build. Real Windows execution
of the eight image cases remains required. The 20 OwnedRecords semantic,
assertion and counted-copy cases, plus the narrow pointer-array-field rejection,
are additional Windows acceptance work not selected by the image runner.
Supported-platform verification, retained-layout migration, product gates and
broader native admission remain separate work.
