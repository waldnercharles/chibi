# Portable byte libraries

Integration baseline on 2026-10-28: accepted main `7c6ca2f5d`, with owned string
descriptors, value arrays and prepared format 26.
[#795](https://github.com/waldnercharles/chibi/pull/795) records validation for
each revision. The acceptance table below preserves the initial implementation's
results on `21ba81a0a`.

This batch contains five complete packages with 34 public functions. Their
source declarations own the API. It separates packages that use existing
integer, array and string contracts from the remaining portable-library work.

| Import | Owning source | Public functions | Adjacent `@test` cases |
| --- | --- | ---: | ---: |
| `core:bytes` | [Bytes.chibi](../packages/core/bytes/Bytes.chibi) | 11 | 6 |
| `core:math/bits` | [Bits.chibi](../packages/core/math/bits/Bits.chibi) | 15 | 7 |
| `core:hash/fnv` | [Fnv.chibi](../packages/core/hash/fnv/Fnv.chibi) | 4 | 3 |
| `core:hash/adler32` | [Adler32.chibi](../packages/core/hash/adler32/Adler32.chibi) | 2 | 4 |
| `core:hash/crc32` | [Crc32.chibi](../packages/core/hash/crc32/Crc32.chibi) | 2 | 3 |

## Public operations

Bytes use `u8[]`. Offsets and lengths use `int`.

| Bytes operation | Result and behavior |
| --- | --- |
| `FromString(value: string) -> u8[]` | Copies every encoded byte into an independent array. |
| `ToString(value: u8[]) -> string` | Copies every byte into an independent immutable string. |
| `Compare(left: u8[], right: u8[]) -> int` | Unsigned lexicographic order, returning exactly -1, 0 or 1. A shorter equal prefix sorts first. |
| `Equal(left: u8[], right: u8[]) -> bool` | Equal lengths and equal bytes. |
| `IndexByte(data: u8[], value: u8) -> int`, `LastIndexByte(...) -> int` | First or last matching byte, or -1. |
| `Index(data: u8[], needle: u8[]) -> int`, `LastIndex(...) -> int` | First or last substring offset, including overlapping matches. A missing needle returns -1; an empty needle matches at 0 or `length(data)`, respectively. |
| `Contains(data: u8[], needle: u8[]) -> bool` | Whether `Index` finds a match. |
| `HasPrefix(data: u8[], prefix: u8[]) -> bool`, `HasSuffix(data: u8[], suffix: u8[]) -> bool` | Exact byte prefix/suffix checks. Empty prefixes and suffixes match. |

Conversions preserve embedded NUL and malformed UTF-8 without normalization or
replacement. They take O(n) time and output storage. Comparison and byte search
scan their inputs without temporary algorithm storage. Substring search uses
Knuth-Morris-Pratt, taking O(n+m) time and O(m) temporary integers. Empty and
one-byte needles need no prefix table. All operations preserve their inputs.

The bits functions operate on unsigned values. Names ending in `32` take `u32`;
names ending in `64` take `u64`.

| Bits operation | Result and behavior |
| --- | --- |
| `CountOnes32`, `CountOnes64` | Return the population count as `int`. |
| `LeadingZeros32`, `LeadingZeros64`, `TrailingZeros32`, `TrailingZeros64` | Return `int`; zero has 32 or 64 leading/trailing zeros. |
| `ByteSwap16(value: u16) -> u16`, `ByteSwap32`, `ByteSwap64` | Reverse byte order and return the input's width. |
| `Reverse32`, `Reverse64` | Reverse all bits and return the input's width. |
| `RotateLeft32`, `RotateLeft64`, `RotateRight32`, `RotateRight64` | Take an additional `count: u32`, reduce it modulo width, and return the input's width. |

Bits operations take bounded work for their fixed widths and use scalar storage.

| Hash/checksum operation | Result and behavior |
| --- | --- |
| `fnv.Fnv1a32(data: u8[]) -> u32`, `fnv.Fnv1a64(data: u8[]) -> u64` | FNV-1a with offset bases `0x811c9dc5` and `0xcbf29ce484222325`. |
| `fnv.Update32(previous: u32, data: u8[]) -> u32`, `fnv.Update64(previous: u64, data: u8[]) -> u64` | Continue from a previous hash using width-wrapping multiplication. |
| `adler32.Sum(data: u8[]) -> u32` | Adler-32, initialized to 1, modulo 65521. |
| `adler32.Update(previous: u32, data: u8[]) -> u32` | Continue from the previous checksum using bounded 5552-byte blocks and `u64` accumulators. |
| `crc32.Ieee(data: u8[]) -> u32` | CRC-32/ISO-HDLC, reflected polynomial `0xedb88320`, initial/final XOR `0xffffffff`. The empty checksum is 0. |
| `crc32.UpdateIeee(previous: u32, data: u8[]) -> u32` | Continue from the returned IEEE checksum. |

These are noncryptographic operations over raw bytes. Every update preserves
its previous value on empty input. Each algorithm uses O(n) work and constant
scalar storage without modifying the buffer. CRC uses eight scalar bit steps
per byte. Intentional FNV multiplication overflow retains the current integer
semantics and can produce development integer reports. Algorithm storage bounds
do not establish zero allocations or copies for an entire runtime invocation.

## Native and compiler dependencies

`bytes.ToString` alone requires the additive
[`text_from_bytes`](../src/runtime/text_definition.h) native. It accepts valid
boxed or packed `u8` arrays, including empty arrays, and constructs owned text.
Invalid native argument shapes/types fail without replacing the output.
[`strings.Bytes`](../packages/core/strings/Strings.chibi) continues to return
`int[]`; this batch does not migrate its callers or signature.

The text definition pack contains seven natives, `text_from_bytes` last, and
the compiler host, the build host and the value-only consumers all use it.
The bootstrap seed is compiled for that catalog, so adding a native means a
new runtime pin, first build host and seed.

[`Generate.chibi`](../tools/graph/asset_compiler/Generate.chibi) takes compiler
implementation inputs from `CompilerSources.txt`. It captures the complete
public core snapshot for packaging after compiling the compiler/facade inputs.
Captured source bytes still participate in the final consistency check. The
asset compiler's existing `ExplainCosts` facade from
[#788](https://github.com/waldnercharles/chibi/pull/788) is preserved.

[`Driver.chibi`](../tools/native_aot/driver/Driver.chibi) compiles the private scalar
emitter with its four compiler packages and `core:strings`. Compilation and
emitter cache identity share the same explicit core source list. Documentation
generation prepares the complete public core with the standalone catalog; its
parser capability stays private to the generator's separate catalog.

No compiler language source, prepared ABI, bootstrap closure/manifest or
consistency/recovery guard change is required by these packages. They add no
host filesystem, process, network or game capability.

## Deferred contracts

Endian, UTF-8, slash paths, hex and Base64 remain outside this independent batch.
Their complete contracts and the pending `Error?`, character, named-result and
`@fallible` work are preserved. This sequencing does not replace their errors
with sentinels or approve reduced implementations. Holds and approval requirements
for [#790](https://github.com/waldnercharles/chibi/pull/790) and other work remain in
force.

## Acceptance evidence

The 23 adjacent authored cases contain literal fixed-width answers, zero/all-bit
boundaries, empty inputs, overlapping searches, exhaustive short binary search
oracles, all byte values, incremental splits and long Adler block boundaries.
Native cases cover byte conversion storage/lifetime, boxed/packed inputs,
invalid arguments and compiler/public profile isolation.
The collection integration also covers shared boxed/packed backing, independent
mutations after detachment, retained strings after producer-arena destruction
and live-view rejection without output publication.

The package tests use explicit `return;`, matching the accepted authored string
regressions. During the initial implementation, implicit void fallthrough did
not set the completion flag required by the shared test runner. That capture
reported its default instruction-budget diagnostic; no budget was exhausted or
raised. Explicit returns retain every assertion and package API. The compiler
diagnosis is recorded separately from the package implementation.

The following measurements belong to the initial `21ba81a0a` implementation.
Integration results for the collections/runtime baseline are recorded in #795.

| Initial implementation proof | Result | Command wall time |
| --- | --- | --- |
| Native portable tests | 23 cases per BYTECODE/SLJIT plus four native boundaries; 225 outer assertions | 1.041 s |
| Wasm portable tests | 23 cases per BYTECODE/WASM plus four native boundaries; 225 outer assertions | 0.568 s |
| Installed native authored-compiler matrix | Both compiler backends and both target backends, 23 cases per pair; 4,916 outer assertions | 19.467 s |
| Wasm authored-compiler matrix | Both compiler backends and both target backends, 23 cases per pair; 7,151 outer assertions | 33.724 s |
| Packaged asset-reader suite | 12 cases, 110 assertions | 30.524 s |
| CLI integration, including existing cost explanations | 22 cases, 279 assertions | 9.873 s |
| Existing string/formatting compiler regressions | 6 outer cases, 14,834 assertions | 17.878 s |
| Native scalar emitter | 4 cases, 48,840 assertions | 24.170 s |

Test-command times include in-test preparation and exclude external builds.
The ordinary Release Graph build and compiler/native/desktop SDK packaging
pass with source-built asset generation enabled. Compiler language sources,
bootstrap files, prepared ABI and production work limits are unchanged. Formatting
and the selected fixture, package-discovery, native-call, parallel, generator,
syntax and semantic checks pass.

The public consumer exercises all 34 functions with literal and binary oracles.
Its authored-compiler calls require actual target-backend entries and reject
inspection/size fallback. The asset reader imports all five packages, converts
embedded NUL bytes, mutates the source buffer, and checks eight independent
unsigned document fields after releasing compiler storage.

Relocation copied all 1,287 normal SDK entries without changes. With the original
SDK prefix unavailable, relocated CMake targets, all 23 compiler-matrix cases,
the asset-reader suite and the external CLI consumer pass. The CLI executes
exactly one `PublicImports` test. The original prefix is restored afterward.
The installed/relocated core source directory resolves beneath each respective
SDK, rather than falling back to the repository.

Clang 22/Emscripten fixture portability fixes retain literal bytes and omitted
zero initialization, preserve the 64-bit descriptor assertions and add explicit
32-bit assertions. The warnings were reproduced from versioned baseline C
sources with the same strict toolchain flags; no warning was suppressed.
The complete material, project and language aggregates are not claimed by these
focused checks. Independent review selected the changed algorithm, native,
compiler, source-resource and SDK interactions under the batch-validation policy.

Results from the broader frozen ten-package batch remain historical evidence
for that separate revision. Its pending error and character prerequisites are
not dependencies of this candidate.

## Reference scope

The package inventory is pinned to official Odin
[`779c8c0a4778f994389e2a681ffc7b3cbd6c1f65`](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65).
The selected Chibi interfaces preserve its own value semantics and require no
allocator or raw-memory arguments. This is partial upstream coverage, not a
symbol-for-symbol port. FNV parameters and RFC 1950 checksum rules are linked
from their owning source files.
