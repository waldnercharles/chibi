# Internal native runtime foundations

`tools/compiler/native_abi/Abi.chibi` is the shared, deliberately small target
contract consumed by the first LLVM emitter. It is not a public `core:` package,
a memory API, or a replacement for the existing managed runtime. Its names and
shape may be consolidated as compiler and library features land.

## Implemented boundary

- `LinuxX64()` selects `x86_64-unknown-linux-gnu` and a 64-bit pointer width.
- `Supported` accepts only that exact pair. It never selects the compiler host
  implicitly or treats equal pointer widths as ABI equivalence.
- `ScalarType` maps the existing compiler's plain bool and int storage ordinals
  to internal LLVM `i1` and `i32`. Empty means unsupported, including storage
  zero; it never means `void`. The caller must separately reject containers,
  nominal types and references before using this mapping.
- No LLVM data-layout string is guessed. The selected backend supplies the
  layout. A future layout-sensitive operation must first obtain and verify its
  actual target data and record the backend identity with the emitted artifact.

These are destination facts, independent of the host pointer width and of
`pin_value`, execution-context offsets and the prepared ABI. Internal `i1`
does not establish a foreign C bool calling convention. The first scalar
checkpoint does not establish the allocation/memory/output native island.

## Next native-memory boundary, not implemented

The smallest useful consumer remains a standalone Chibi entry point that
allocates bytes, stores and loads them, writes an explicit byte span through
a system output function, frees the allocation and returns a checked status.
The result must not link first-party C runtime objects or invoke hidden helpers.

Before adding this path, the frontend and LLVM owner must agree on a native
address type and target-sized counts. They must be distinct from managed
references, entities, integer IDs and ABI21 managed-pointer ownership. Do not
encode pointers as `uint64` just because this initial target is 64-bit.

The bounded Linux x64 foreign boundary can use the platform's `malloc`, `free`
and `write`: allocation returns a nullable native address; `free` consumes only
the live allocation base; output borrows an initialized byte range for the
duration of the call and reports its signed result. External signatures need
explicit destination-sized size/result types, calling convention and symbol
identity. Their platform contract must be verified before lowering is enabled.

Byte access needs a checked extent, unsigned byte values, and no access after
free. The program must handle allocation failure and incomplete output without
leaking its allocation. Passing a Chibi `string` as a C string or treating an
array's managed header as raw storage is not an implementation of this boundary.
No string, arithmetic or shared managed-value contract changes in this slice.

The follow-on acceptance includes out-of-bounds access, invalid signatures,
wrong target layouts and lifetime errors, plus an LLVM verifier run and a link
map/symbol inventory proving the executable is independent of first-party C.
Keep this work internal until its concrete consumers establish a useful library
API; do not pre-create an unconsumed general FFI or allocator framework.

## Validation

`test/fixtures/native-abi/Tests.chibi` contains three authored test cases for
an exact target, target mismatches, and supported/rejected scalar storage.
Their single check corpus lives beside them in `Checks.chibi`, whose public
`Tests()` entry returns zero on success or a contract-specific nonzero code.
The test package imports `project:native_abi`, exercising the production API
across a package boundary, including its named target result and record fields.
Production `tools/compiler/native_abi` contains only `Abi.chibi`; a compiler
consumer can include every file there without importing test-only natives.

Stage both packages for the ordinary package-test runner:

```sh
fixture_root=$(mktemp -d)
mkdir -p "$fixture_root/native_abi" "$fixture_root/native_abi_checks"
cp tools/compiler/native_abi/*.chibi "$fixture_root/native_abi/"
cp test/fixtures/native-abi/*.chibi "$fixture_root/native_abi_checks/"
chibi test "$fixture_root"
```

The minimal bootstrap compiler catalog omits the assert native. For its pure
runner, stage the same complete production package but only the test fixture's
`Checks.chibi` in `native_abi_checks/`, then invoke the existing JSON-protocol
entry `project:native_abi_checks:1:Tests:callable`. This selects a pure test
entry; it does not filter production package files or maintain a second corpus.

LLVM emitter integration, bootstrap and standalone native execution are
separate checks. A source review or an authored test file is not execution
evidence; report each exact tested revision and unavailable check separately.

### Bounded Linux evidence, 2026-10-22

The complete production package and the source-identical cross-package
`Checks.chibi` fixture were compiled and invoked
using the checksum-verified compiler host
`c8b1720c610c83aadb7048c22e251109ec7052b62675dc67d391ec2136ac9632`
and the main-base bootstrap compiler
`2fcbdbaf6b999d87b119a8e6195752dc2b5aca18016e9349b9f6ae1a5bfa2b37`.
Both bytecode and SLJIT executions returned `[0]`; each reported 91 function
entries and zero fallback/overflow. These fixture checks import `Target`,
`LinuxX64`, `Supported` and `ScalarType`, check returned record fields, and
reject foreign targets. The same corpus therefore checks cross-package use
rather than depending on a separate substitute consumer. A scratch-only change
from the int mapping `i32` to `i64` made the same checks return `[12]` on both
backends, establishing that the scalar check detects this fault. The repository
implementation was not changed for that negative control.

The minimal compiler catalog omits the assert native, so normal `@test`
wrappers and package-test discovery were not executed here. The shared pure
fixture was supplied without its assert wrappers to the host's existing JSON
invocation protocol; the complete production package was included. No full bootstrap, LLVM object/link, other platform,
format canonicality or broad Make-group result is claimed by these checks.
