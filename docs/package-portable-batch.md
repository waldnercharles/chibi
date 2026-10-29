# Portable core packages

Approved on 2026-10-27. This batch adds authored packages to the engine's
`packages/core` collection. It preserves value semantics and uses existing
`u8[]`, scalar records, optionals and named results. Public fallible
operations return ordinary values followed by a package-owned optional error.
The owner selected that error convention in the parallel recoverable-results
workstream. There is no generic Result requirement or success-before-use rule.

The implementation starts from `c086475d5971a762af25f3fa6395a9bff7534b7a`.
The candidate is now integrated with accepted main `286f69ddf`.
Source is implemented locally; complete validation, publication and main
integration remain separate claims. The owning declarations give exact
signatures. This document records the selected subset and deliberate choices,
not symbol-for-symbol Odin parity.

## Public operations

| Import | Selected operations |
| --- | --- |
| `core:bytes` | `FromString`, `ToString`, `Compare`, `Equal`, `IndexByte`, `LastIndexByte`, `Index`, `LastIndex`, `Contains`, `HasPrefix`, `HasSuffix` |
| `core:encoding/endian` | `ReadU16LE/BE`, `ReadU32LE/BE`, `ReadU64LE/BE`, corresponding `Write` functions |
| `core:utf8` | `Decode`, `DecodeString`, `Validate`, `ValidateString`, `Count`, `Encode` |
| `core:path/slashpath` | `IsAbsolute`, `Base`, `Split`, `Extension`, `Stem`, `Clean`, `Join` |
| `core:math/bits` | `CountOnes32/64`, `LeadingZeros32/64`, `TrailingZeros32/64`, `Reverse32/64`, `ByteSwap16/32/64`, `RotateLeft32/64`, `RotateRight32/64` |
| `core:hash/fnv` | `Fnv1a32/64`, `Update32/64` |
| `core:hash/adler32` | `Sum`, `Update` |
| `core:hash/crc32` | `Ieee`, `UpdateIeee` |
| `core:encoding/hex` | `Encode`, `EncodeUpper`, `Decode` |
| `core:encoding/base64` | `Encode`, `Decode`, `EncodeUrl`, `DecodeUrl` |

Each fallible package exposes an `Error` with scalar `kind` and `offset`
fields, plus named error-kind constants. A missing error is `nil`. Byte offsets
are zero-based. Output-size errors use offset -1. Allocation failure retains
the runtime's existing invocation-failure behavior.

```chibi
package example
import base64 "core:encoding/base64"

pub Read :: (input: string) -> int {
    decoded := base64.Decode(input);
    if decoded.error {
        return -1;
    }
    return length(decoded.data);
}
```

### Bytes and endian

Strings and buffers preserve every byte, including NUL and malformed UTF-8.
`FromString` and `ToString` produce independent values. `strings.Bytes` keeps
its existing `int[]` signature and compiler callers.

Comparison returns exactly -1, 0 or 1 in unsigned lexicographic order.
Search returns an offset or -1. An empty needle matches zero for `Index` and
the buffer length for `LastIndex`. KMP substring search takes O(n+m) time and
O(m) integer scratch storage; empty and one-byte needles need no scratch.
Comparison, byte scans and prefix/suffix checks use constant auxiliary space.

Endian reads return `(value, error)` and zero on error. Writes return an
optional error, validate the full range before mutation, and leave the buffer
unchanged on failure. Offset below zero or beyond the buffer is InvalidOffset;
an otherwise valid offset with insufficient remaining bytes is ShortBuffer.
Wire order is explicit and reads may be unaligned. The operations do bounded
fixed-width work. Mutating an array may detach its value storage in O(n).

### UTF-8

`char` remains the language's Unicode scalar type. Decode returns named
`value`, `width`, `status` and `error` outputs. Success consumes one to four
bytes; End is normal, has zero width and no error. InvalidOffset, Malformed
and Truncated errors have zero width and consume nothing. The returned scalar
is meaningful on Success. Validation and Count identify the start of the first
ill-formed sequence. Count returns zero on error.

Available invalid bytes take precedence over missing bytes: E2 28 is malformed,
while E2 82 is truncated. Reject overlong encodings, surrogates, stray
continuations and scalars above U+10FFFF. NUL, U+FFFD, noncharacters and
unassigned scalars are valid. No replacement, normalization, grapheme counting
or BOM removal occurs. String wrappers read bytes directly without converting
the complete string into an array. Validate and Count are O(n), with constant
algorithmic storage. Encode returns a one-to-four-byte array.

### Slash paths

Paths are lexical byte strings using `/`. Backslashes, colons, NUL and
malformed UTF-8 are ordinary bytes. `C:\\foo` is relative; cleaning
`//server/share` yields `/server/share`. There is no drive, UNC, filesystem,
symlink or containment interpretation.

Clean collapses repeated slashes and dot segments, preserves unresolved leading
relative `..`, and clamps rooted traversal at `/`. Empty cleans to `.`.
Join skips empty elements and cleans the result; all-empty input returns empty.
A later rooted element does not discard earlier elements. Output-size failure
returns empty text and an error. Base strips trailing slashes; empty Base is
`.` and a root's Base is `/`. Split preserves separators exactly. Extension
uses the last dot in the final element, including dotfiles. Stem removes that
extension. Scans and buffer assembly are O(n). Returned strings are values.

The private compiler resolver is unchanged. Its absolute-root, 4096-byte,
backslash and intermediate-escape restrictions are not public path rules.

### Bits, hashes and codecs

Bits use concrete unsigned widths. Leading/trailing zero counts of zero equal
the width. Rotation counts are reduced modulo width. Algorithms use bounded
scalar work and constant storage.

FNV-1a uses standard width-specific offset bases and wrapping multiplication.
Adler-32 starts at 1. CRC-32 IEEE uses reflected polynomial 0xEDB88320 and the
standard initial/final complement; empty input returns zero. Update functions
accept the previous returned value directly. Algorithms scan O(n) bytes with
scalar state. CRC uses eight bit steps per byte and does not build a table
per call. Development builds may report intentional hash overflow under the
current numeric contract; results do not change.

Hex encodes lowercase by default, offers uppercase output and accepts either
case when decoding. Decoders require complete pairs and reject prefixes,
whitespace and invalid characters.

Standard Base64 requires padding. URL Base64 uses `-_` and forbids padding.
Decoders reject mixed alphabets, whitespace, NUL, incorrect padding and nonzero
unused trailing bits. Missing input reports the input length; noncanonical
trailing bits report their encoded character's offset. Codecs validate input
before allocating decoded output. Errors expose an empty output, never a
partial prefix. Length calculations check bounds before arithmetic. All
transforms are O(n) with O(n) output storage. String construction can add a
linear copy and boxed-array scratch.

These are checksums and noncryptographic hashes. Cryptography, streaming,
custom alphabets, caller-owned codec destinations, generics, sorting, containers,
OS APIs and broader math remain outside this batch.

## Integration and acceptance

One integration owner handles the public text conversion adapter, SDK core
installation, build-tree resources and test registration. Package agents own
disjoint directories. The normal SDK and launcher now stage core recursively,
matching the build-host profile. The private compiler retains its original
text capability profile; it does not gain `text_from_bytes`. Public package
consumers and the values-only evaluation profile use the expanded public text
pack. Compiler/facade generation reads manifest-owned implementation inputs;
all public core/game sources remain captured, hashed and packaged separately. No bootstrap artifact is
regenerated by this batch.

Adjacent `Tests.chibi` files contain the algorithm assertions. The
`language-package-portable` selector embeds the actual sources, explicitly
discovers core tests, and runs the shared authored executor on bytecode and
SLJIT. Default project-only discovery remains unchanged. A separate consumer
compiles public imports through the authored compiler and executes its closure
against the public native catalog. Its native route loads the validated compiler
bootstrap. Its Wasm route invokes the embedded current compiler through the
shared fixture for that target, then loads the emitted consumer closure. It
needs no host-checkout filesystem or native-bootstrap provenance claim.
Native boundary cases cover boxed, packed
and empty arrays, output ownership after input retirement, failed conversions,
and the private/public text-profile distinction. A values-only runtime case
executes binary conversion after source retirement and requires the seed-private
loader to reject that artifact. Values-only calls use a canonical pure-budget
scope even in a compiled load configuration; compiled package execution is
proved separately by the authored test suite.

Acceptance uses literal wire bytes, Unicode boundaries, RFC vectors, published
hash/checksum answers, incremental equivalence, atomic failures, copy isolation
and allocation-free length boundaries. Round trips supplement those oracles.
Installed and relocated SDK imports, affected Wasm execution, current-source
compiler recovery and selected native checks are separate validation claims.
Record actual results and elapsed time; no global timing admission rule applies.

The candidate includes the accepted Rhythm/VisualMotion retirement and authored
strings migration. Shared collection discovery and the authored fixture runner
are now main-owned; this batch adds no second runner. The focused native driver
passes 65 public-API authored tests on each of bytecode and SLJIT, plus five
native representation/profile/discovery cases and 295 outer assertions.
Four duplicate endian wrapper cases were merged into
the original wire/bounds tests without dropping their unique assertions. The
focused driver explicitly configures `CHIBI_TEST_ASSET_COMPILER_HOST=OFF`;
asset-host integration is not covered by that build.

Each of the five deferred packages has a package-owned `Error` struct. Marking
it `@error` (Utf8, Endian, Hex, Base64 and slashpath) waits for the checker in
#813; callers already test the error. Nine UTF-8 assertions check errors unconditionally, and Hex's rejection
helper checks the error before its data-length early return. All 65 cases and
their existing oracles remain. The pass counts below predate `@error` and were
not rerun with the combined error checker.

| Selected check | Result | Command wall time, fixture setup included |
| --- | --- | --- |
| `language-package-portable` | 65 authored tests per backend; 5 native cases | 1.365 s |
| `language-package-core` | 6 cases, 14,834 assertions; includes authored strings/formatting | 16.787 s |
| `language-package-tests` | 6 cases, 338 assertions | 0.238 s |
| `language-compiler-fixture` | 9 cases, 414 assertions | 1.781 s |
| `wasm-package-portable` | 65 authored tests per BYTECODE/WASM; 5 native cases | 1.271 s |
| `language-docs-markdown` | 14 cases, 1,778 assertions | 1.424 s |
| `text-format` | 2 native boundary cases, 16 assertions | 0.096 s |
| `language-package-portable-costs` | 12 measured BYTECODE/SLJIT calls; 130,109 assertions | 0.856 s |
| Wasm `--package-portable-costs` | 12 measured BYTECODE/WASM calls; 130,109 assertions | 0.890 s |

Native-call, asset-packaging prefix and parallel boundary checks last passed on
the `c4f75f125` candidate. Their results remain historical evidence rather than
new execution claims for the strings-main combination.

Configuration/build times are excluded from that table. On the `c4f75f125`
candidate, the public consumer in `test/packages/portable_consumer/Consumer.chibi`
passed through the native front end. The runtime-only SDK built and installed
with compiler/native/desktop products disabled; relocated CMake metadata found
all ten package directories. A separately built native verifier loaded the installed core
and passed the public-import tests. This verifies the runtime profile's source
installation; it is not a full compiler-enabled SDK distribution pass.

Measurements use a monotonic clock and 4096 and 16384 input bytes, once per
workload and backend.
They exclude preparation/input setup, include result detachment, and count only
selected same-thread managed-value copies, not all allocation or memory traffic.
The validation workload performs buffer validation, string validation and scalar
counting over the same mixed ASCII/NUL/multibyte text. Removing per-scalar result
records reduced its 16 KiB record-copy count from 7,667,992 bytes to 196, with
zero copied input text/array payload in that scope. KMP's counted buffer copies
remain 1548 bytes at both input sizes for fixed 32-byte needles. Base64's
16 KiB encoded-result path counts 240,404 copied text bytes, including wrappers
and detachment. Those are implementation observations, not zero-allocation or
universal throughput guarantees. Exact samples are retained in the task logs.

The native and Wasm authored-compiler consumers currently reject the same named
result with `named result type is not supported yet` at line 52, column 60,
as expected on the accepted base. Wasm now reaches that semantic check through
the direct source fixture; it no longer fails at host filesystem access or
whole-compiler serialization. Neither compiler selector passes yet.
Full compiler-enabled builds, source recovery, `@error` acknowledgment,
nominal-char parity and full compiler-enabled installed SDK checks
remain separate gates owned or supplied by the coordinated prerequisite lanes.
LLVM-native completeness does not gate portable VM support. No full-suite pass
is claimed by this checkpoint.

## Prior art and provenance

The reference is official Odin commit
[`779c8c0a4778f994389e2a681ffc7b3cbd6c1f65`](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65),
especially bytes, encoding/endian, unicode/utf8, path/slashpath, math/bits,
hash, encoding/hex and encoding/base64. Chibi retains its own value semantics
and checked public operations, rather than exposing raw-memory or allocator APIs.
Adapted source retains the upstream notice and is marked as altered.

UTF-8 validity follows [Unicode 17 table 3-7](https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-3/#G31703).
Codec rules and fixed vectors follow [RFC 4648](https://www.rfc-editor.org/rfc/rfc4648.html).
Strict zero-consumption UTF-8 errors and strict Base64 defaults are deliberate
Chibi choices; they are not claims of identical Odin behavior.
