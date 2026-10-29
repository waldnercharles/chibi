# Odin package source inventory

Checkpoint: 2026-10-20. Chibi baseline: `4a751bd5fb2bb414ec867f089acae9b9736e1d9e`.
Odin reference: [`779c8c0a4778f994389e2a681ffc7b3cbd6c1f65`](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65).

This is a package planning map. It does not audit individual symbols or approve
API names, error behavior, allocation, ownership, or exact Odin parity. Each
package needs a separate feature review and fidelity decision before implementation.
Language features that remove repeated work come first; waves below are proposed
ordering, conditional on those decisions.

## Counting rule and reproduction

The pinned GitHub recursive tree contains 4,455 entries and reports
`truncated: false`. Select blobs whose paths end in `.odin` under `base/`,
`core/`, or `vendor/`; take each distinct parent directory. This gives **355
source directories: 4 base, 271 core, 80 vendor**. Every selected directory is
listed below once, in lexical order, with a link to its pinned upstream source.

These are source directories, not a claim of 355 public library packages. The
rule deliberately includes test/example programs, internal helpers, generated
ISA tables, code generators, and platform-specific directories. Directory roles
are a planning classification based on paths, not parsed Odin export metadata.
The documentation-site counts reported during exploration (4/176/54) use another
scope and may describe a different revision; they are not the denominator here.
A symbol/export audit and documentation-filter reconciliation remain future work.
No directory is dropped merely because it appears private or tool-only.

Reproduce against the immutable upstream tree, without cloning vendor binaries:

```sh
curl -fsSL 'https://api.github.com/repos/odin-lang/Odin/git/trees/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65?recursive=1' -o odin-tree.json
jq -r '.tree[] | select(.type == "blob") | .path | select(test("^(base|core|vendor)/.*\\.odin$")) | sub("/[^/]+$"; "")' odin-tree.json | sort -u
```

Reject a response with `truncated: true` before counting. Group the output by
its first path component to obtain 4/271/80. The tree API is the source of the
inventory; [base](https://pkg.odin-lang.org/base/),
[core](https://pkg.odin-lang.org/core/), and
[vendor](https://pkg.odin-lang.org/vendor/) are discovery aids whose live content
must not silently replace this pin.

## Coverage, waves, and gates

Coverage is **P** (partial) for strings and the selected bytes, bits and hash
subsets in the [portable byte-library candidate](package-byte-libraries.md).
Hash coverage is limited to FNV-1a, Adler-32 and IEEE CRC-32 in separate Chibi
packages. Other rows are **N** (no corresponding public Chibi package).
Source coverage does not establish merged, installed or backend acceptance;
the package decision records those checks separately.
Runtime C facilities, private build-host operations, and compiler implementation
modules do not count as a public package port. In particular, the internal
`tools/compiler/source_tokenizer` is related work for `core/odin/tokenizer`,
not a public Odin tokenizer package. Existing native math, physics, image and
platform code is implementation evidence to assess when those packages are scoped.

| Wave | Proposed work |
| --- | --- |
| W0 | Agree shared semantics; implement useful language/compiler prerequisites and prove them in both compilers. Base-runtime mapping is investigated here; exposing a `base:` collection remains undecided. |
| W1 | Portable text, bytes, paths, Unicode foundations, basic numeric conversions, slices, sorting, hashing, and containers after their gates pass. |
| W2 | Formatting, structured data, codecs, and broader numeric packages after foundational contracts and dependencies are available. |
| W3 | Memory, native ABI, files, processes, clocks, and platform services with explicit host capabilities. |
| W4 | Threads, synchronization, asynchronous IO, and networking after lifecycle/cancellation rules are agreed. |
| W5 | Vendor bindings, cryptography, SIMD, compiler tooling, and specialized formats/backends after separate feasibility and fidelity review. |

Waves identify an earliest planning slot, not an upstream dependency graph or a
promise that all packages in a wave can proceed independently. Role `Tool/generated`
or `Test/example` inherits its owning family's dependencies and is normally dealt
with alongside that family. Bindings may be selected earlier when a concrete
consumer and the required ABI contract exist.

The following inventory-local gates name questions to resolve. Their presence
on a row does not assert that Chibi lacks every feature named. Review current
source and both compiler paths, record which parts already work, and implement
only the approved missing prerequisites. Cross-reference the foundations proposal
and [`language-friction.md`](language-friction.md) before opening language work.

| Gate | Required contract review |
| --- | --- |
| G1 | Byte versus Unicode text; string length, embedded NUL, indexing/slicing, views/copies, builders, and UTF-8 error policy. |
| G2 | Results and errors, multiple outputs, EOF/partial IO, formatting/conversion failures, and panic/assert behavior. |
| G3 | Generic package functions/types, aggregate fields, procedure parameters, iteration, mutable references, and comparator/hash APIs. |
| G4 | Integer widths, overflow, shifts, byte buffers, endian rules, numeric precision, vectors/matrices, and target reproducibility. |
| G5 | Allocation/lifetimes, borrowing/sharing, pointers, layout/alignment, calling conventions, opaque handles, and foreign callbacks. |
| G6 | Host capability access, target availability, filesystem/process/platform errors, resource cleanup, and SDK packaging. |
| G7 | Thread/task ownership, blocking versus async operations, synchronization, cancellation, shutdown, and scheduler interaction. |
| G8 | Algorithm selection, threat model, entropy, constant-time behavior, vetted implementation provenance, and known-answer/interoperability checks. |
| G9 | Compiler/runtime boundary, reflection metadata, intrinsics, diagnostics, AST ownership, tooling scope, and bootstrap constraints. |

## Base source directories (4)

| Upstream source directory | Role | Coverage | Family | Earliest wave | Gates |
| --- | --- | --- | --- | --- | --- |
| [base/builtin](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/base/builtin) | Candidate | N | Language/runtime | W0 | G5, G9 |
| [base/intrinsics](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/base/intrinsics) | Candidate | N | Language/runtime | W0 | G5, G9 |
| [base/runtime](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/base/runtime) | Candidate | N | Language/runtime | W0 | G5, G9 |
| [base/sanitizer](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/base/sanitizer) | Candidate | N | Language/runtime | W0 | G5, G9 |

## Core source directories (271)

| Upstream source directory | Role | Coverage | Family | Earliest wave | Gates |
| --- | --- | --- | --- | --- | --- |
| [core/bufio](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/bufio) | Candidate | N | Text/IO | W2 | G1, G2 |
| [core/bytes](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/bytes) | Candidate | P | Text/IO | W1 | G1, G2 |
| [core/c](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/c) | Candidate | N | Memory/ABI | W3 | G3, G5, G6 |
| [core/c/libc](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/c/libc) | Candidate | N | Memory/ABI | W3 | G3, G5, G6 |
| [core/c/libc/tests](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/c/libc/tests) | Test/example | N | Memory/ABI | W3 | G3, G5, G6 |
| [core/compress](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/compress) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/compress/gzip](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/compress/gzip) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/compress/shoco](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/compress/shoco) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/compress/zlib](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/compress/zlib) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/container/avl](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/container/avl) | Candidate | N | Collections | W1 | G1, G3, G4 |
| [core/container/bit_array](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/container/bit_array) | Candidate | N | Collections | W1 | G1, G3, G4 |
| [core/container/handle_map](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/container/handle_map) | Candidate | N | Collections | W1 | G1, G3, G4, G5 |
| [core/container/intrusive/list](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/container/intrusive/list) | Candidate | N | Collections | W1 | G1, G3, G4, G5 |
| [core/container/lru](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/container/lru) | Candidate | N | Collections | W1 | G1, G3, G4 |
| [core/container/pool](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/container/pool) | Candidate | N | Collections | W1 | G1, G3, G4, G5 |
| [core/container/priority_queue](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/container/priority_queue) | Candidate | N | Collections | W1 | G1, G3, G4 |
| [core/container/queue](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/container/queue) | Candidate | N | Collections | W1 | G1, G3, G4 |
| [core/container/rbtree](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/container/rbtree) | Candidate | N | Collections | W1 | G1, G3, G4 |
| [core/container/small_array](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/container/small_array) | Candidate | N | Collections | W1 | G1, G3, G4 |
| [core/container/topological_sort](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/container/topological_sort) | Candidate | N | Collections | W1 | G1, G3, G4 |
| [core/container/xar](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/container/xar) | Candidate | N | Collections | W1 | G1, G3, G4 |
| [core/crypto](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_aes](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_aes) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_aes/ct64](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_aes/ct64) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_aes/hw](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_aes/hw) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_bigint](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_bigint) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_blake2](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_blake2) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_chacha20](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_chacha20) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_chacha20/ref](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_chacha20/ref) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_chacha20/simd128](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_chacha20/simd128) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_chacha20/simd256](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_chacha20/simd256) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_edwards25519](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_edwards25519) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_edwards25519/tools](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_edwards25519/tools) | Tool/generated | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_fiat](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_fiat) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_fiat/field_curve25519](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_fiat/field_curve25519) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_fiat/field_curve448](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_fiat/field_curve448) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_fiat/field_p256r1](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_fiat/field_p256r1) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_fiat/field_p384r1](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_fiat/field_p384r1) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_fiat/field_poly1305](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_fiat/field_poly1305) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_fiat/field_scalar25519](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_fiat/field_scalar25519) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_fiat/field_scalarp256r1](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_fiat/field_scalarp256r1) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_fiat/field_scalarp384r1](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_fiat/field_scalarp384r1) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_mldsa](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_mldsa) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_mlkem](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_mlkem) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_sha3](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_sha3) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_subtle](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_subtle) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_weierstrass](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_weierstrass) | Internal helper | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/_weierstrass/tools](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/_weierstrass/tools) | Tool/generated | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/aead](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/aead) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/aegis](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/aegis) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/aes](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/aes) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/argon2id](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/argon2id) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/blake2b](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/blake2b) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/blake2s](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/blake2s) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/chacha20](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/chacha20) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/chacha20poly1305](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/chacha20poly1305) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/deoxysii](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/deoxysii) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/ecdh](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/ecdh) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/ecdsa](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/ecdsa) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/ed25519](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/ed25519) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/hash](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/hash) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/hkdf](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/hkdf) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/hmac](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/hmac) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/kmac](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/kmac) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/legacy/keccak](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/legacy/keccak) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/legacy/md5](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/legacy/md5) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/legacy/sha1](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/legacy/sha1) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/mldsa](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/mldsa) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/mlkem](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/mlkem) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/noise](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/noise) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/pbkdf2](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/pbkdf2) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/poly1305](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/poly1305) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/ristretto255](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/ristretto255) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/rsa](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/rsa) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/sha2](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/sha2) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/sha3](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/sha3) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/shake](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/shake) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/siphash](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/siphash) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/sm3](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/sm3) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/tuplehash](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/tuplehash) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/turboshake](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/turboshake) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/x25519](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/x25519) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/x448](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/x448) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/crypto/x509](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/crypto/x509) | Candidate | N | Cryptography | W5 | G1, G3, G4, G5, G8 |
| [core/debug/pe](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/debug/pe) | Candidate | N | Host/platform | W3 | G2, G5, G6 |
| [core/debug/trace](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/debug/trace) | Candidate | N | Host/platform | W3 | G2, G5, G6 |
| [core/diff](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/diff) | Candidate | N | Text/IO | W1 | G1, G2 |
| [core/dynlib](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/dynlib) | Candidate | N | Memory/ABI | W3 | G3, G5, G6 |
| [core/dynlib/example](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/dynlib/example) | Test/example | N | Memory/ABI | W3 | G3, G5, G6 |
| [core/encoding/asn1](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/encoding/asn1) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/encoding/base32](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/encoding/base32) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/encoding/base64](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/encoding/base64) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/encoding/cbor](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/encoding/cbor) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/encoding/csv](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/encoding/csv) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/encoding/endian](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/encoding/endian) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/encoding/entity](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/encoding/entity) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/encoding/entity/example](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/encoding/entity/example) | Test/example | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/encoding/hex](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/encoding/hex) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/encoding/hxa](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/encoding/hxa) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/encoding/ini](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/encoding/ini) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/encoding/json](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/encoding/json) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/encoding/pem](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/encoding/pem) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/encoding/uuid](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/encoding/uuid) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/encoding/uuid/legacy](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/encoding/uuid/legacy) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/encoding/varint](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/encoding/varint) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/encoding/xml](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/encoding/xml) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/encoding/xml/example](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/encoding/xml/example) | Test/example | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/flags](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/flags) | Candidate | N | Text/IO | W2 | G1, G2 |
| [core/flags/example](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/flags/example) | Test/example | N | Text/IO | W2 | G1, G2 |
| [core/fmt](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/fmt) | Candidate | N | Text/IO | W2 | G1, G2 |
| [core/hash](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/hash) | Candidate | P | Collections | W1 | G1, G3, G4 |
| [core/hash/xxhash](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/hash/xxhash) | Candidate | N | Collections | W1 | G1, G3, G4 |
| [core/image](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/image) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/image/bmp](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/image/bmp) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/image/jpeg](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/image/jpeg) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/image/netpbm](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/image/netpbm) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/image/png](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/image/png) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/image/qoi](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/image/qoi) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/image/tga](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/image/tga) | Candidate | N | Data/codec | W2 | G1, G2, G3, G4 |
| [core/io](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/io) | Candidate | N | Text/IO | W2 | G1, G2 |
| [core/log](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/log) | Candidate | N | Text/IO | W2 | G1, G2 |
| [core/math](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/math) | Candidate | N | Numeric | W2 | G3, G4 |
| [core/math/big](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/math/big) | Candidate | N | Numeric | W2 | G3, G4 |
| [core/math/bits](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/math/bits) | Candidate | P | Numeric | W2 | G3, G4 |
| [core/math/cmplx](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/math/cmplx) | Candidate | N | Numeric | W2 | G3, G4 |
| [core/math/ease](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/math/ease) | Candidate | N | Numeric | W2 | G3, G4 |
| [core/math/fixed](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/math/fixed) | Candidate | N | Numeric | W2 | G3, G4 |
| [core/math/linalg](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/math/linalg) | Candidate | N | Numeric | W2 | G3, G4 |
| [core/math/linalg/glsl](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/math/linalg/glsl) | Candidate | N | Numeric | W2 | G3, G4 |
| [core/math/linalg/hlsl](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/math/linalg/hlsl) | Candidate | N | Numeric | W2 | G3, G4 |
| [core/math/noise](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/math/noise) | Candidate | N | Numeric | W2 | G3, G4 |
| [core/math/rand](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/math/rand) | Candidate | N | Numeric | W2 | G3, G4 |
| [core/mem](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/mem) | Candidate | N | Memory/ABI | W3 | G3, G5, G6 |
| [core/mem/tlsf](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/mem/tlsf) | Candidate | N | Memory/ABI | W3 | G3, G5, G6 |
| [core/mem/virtual](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/mem/virtual) | Candidate | N | Memory/ABI | W3 | G3, G5, G6 |
| [core/nbio](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/nbio) | Candidate | N | Concurrency/network | W4 | G2, G5, G6, G7 |
| [core/net](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/net) | Candidate | N | Concurrency/network | W4 | G2, G5, G6, G7 |
| [core/odin](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/odin) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/odin/ast](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/odin/ast) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/odin/doc-format](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/odin/doc-format) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/odin/format](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/odin/format) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/odin/parser](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/odin/parser) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/odin/printer](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/odin/printer) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/odin/tokenizer](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/odin/tokenizer) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/odin/tool-format](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/odin/tool-format) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/os](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/os) | Candidate | N | Host/platform | W3 | G2, G5, G6 |
| [core/path](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/path) | Candidate | N | Text/IO | W1 | G1, G2 |
| [core/path/filepath](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/path/filepath) | Candidate | N | Text/IO | W1 | G1, G2 |
| [core/path/slashpath](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/path/slashpath) | Candidate | N | Text/IO | W1 | G1, G2 |
| [core/prof/spall](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/prof/spall) | Candidate | N | Host/platform | W3 | G2, G5, G6 |
| [core/reflect](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/reflect) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/relative](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/relative) | Candidate | N | Memory/ABI | W3 | G3, G5, G6 |
| [core/rexcode](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/ir](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/ir) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/ir/spirv](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/ir/spirv) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/ir/spirv/tablegen](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/ir/spirv/tablegen) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/ir/spirv/tests](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/ir/spirv/tests) | Test/example | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/ir/wasm](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/ir/wasm) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/arm32](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/arm32) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/arm32/tablegen](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/arm32/tablegen) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/arm32/tablegen/generated](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/arm32/tablegen/generated) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/arm32/tests](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/arm32/tests) | Test/example | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/arm32/tools](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/arm32/tools) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/arm64](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/arm64) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/arm64/tablegen](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/arm64/tablegen) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/arm64/tablegen/cpp-compiler](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/arm64/tablegen/cpp-compiler) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/arm64/tablegen/generated](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/arm64/tablegen/generated) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/arm64/tests](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/arm64/tests) | Test/example | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/arm64/tools](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/arm64/tools) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/mips](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/mips) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/mips/tablegen](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/mips/tablegen) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/mips/tablegen/generated](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/mips/tablegen/generated) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/mips/tests](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/mips/tests) | Test/example | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/mips/tools](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/mips/tools) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/mos6502](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/mos6502) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/mos6502/tablegen](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/mos6502/tablegen) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/mos6502/tablegen/generated](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/mos6502/tablegen/generated) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/mos6502/tests](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/mos6502/tests) | Test/example | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/mos6502/tools](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/mos6502/tools) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/mos65816](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/mos65816) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/mos65816/tablegen](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/mos65816/tablegen) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/mos65816/tablegen/generated](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/mos65816/tablegen/generated) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/mos65816/tests](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/mos65816/tests) | Test/example | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/mos65816/tools](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/mos65816/tools) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/ppc](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/ppc) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/ppc/tablegen](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/ppc/tablegen) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/ppc/tablegen/generated](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/ppc/tablegen/generated) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/ppc/tests](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/ppc/tests) | Test/example | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/ppc/tools](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/ppc/tools) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/ppc_vle](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/ppc_vle) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/ppc_vle/tablegen](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/ppc_vle/tablegen) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/ppc_vle/tablegen/generated](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/ppc_vle/tablegen/generated) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/ppc_vle/tests](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/ppc_vle/tests) | Test/example | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/ppc_vle/tools](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/ppc_vle/tools) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/riscv](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/riscv) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/riscv/tablegen](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/riscv/tablegen) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/riscv/tablegen/cpp-compiler](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/riscv/tablegen/cpp-compiler) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/riscv/tablegen/generated](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/riscv/tablegen/generated) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/riscv/tests](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/riscv/tests) | Test/example | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/riscv/tools](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/riscv/tools) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/rsp](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/rsp) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/rsp/tablegen](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/rsp/tablegen) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/rsp/tablegen/generated](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/rsp/tablegen/generated) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/rsp/tests](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/rsp/tests) | Test/example | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/rsp/tools](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/rsp/tools) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/x86](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/x86) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/x86/tablegen](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/x86/tablegen) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/x86/tablegen/cpp-compiler](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/x86/tablegen/cpp-compiler) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/x86/tablegen/generated](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/x86/tablegen/generated) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/x86/tests](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/x86/tests) | Test/example | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/rexcode/isa/x86/tools](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/rexcode/isa/x86/tools) | Tool/generated | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/simd](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/simd) | Candidate | N | Numeric | W5 | G4, G5, G6 |
| [core/simd/arm](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/simd/arm) | Candidate | N | Numeric | W5 | G4, G5, G6 |
| [core/simd/x86](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/simd/x86) | Candidate | N | Numeric | W5 | G4, G5, G6 |
| [core/slice](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/slice) | Candidate | N | Collections | W1 | G1, G3, G4 |
| [core/slice/heap](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/slice/heap) | Candidate | N | Collections | W1 | G1, G3, G4 |
| [core/sort](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sort) | Candidate | N | Collections | W1 | G1, G3, G4 |
| [core/strconv](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/strconv) | Candidate | N | Text/IO | W1 | G1, G2, G4 |
| [core/strconv/decimal](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/strconv/decimal) | Candidate | N | Text/IO | W1 | G1, G2, G4 |
| [core/strings](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/strings) | Candidate | P | Text/IO | W1 | G1, G2 |
| [core/sync](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sync) | Candidate | N | Concurrency/network | W4 | G2, G5, G6, G7 |
| [core/sync/chan](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sync/chan) | Candidate | N | Concurrency/network | W4 | G2, G5, G6, G7 |
| [core/sys/darwin](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/darwin) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/darwin/CoreFoundation](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/darwin/CoreFoundation) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/darwin/CoreGraphics](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/darwin/CoreGraphics) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/darwin/CoreText](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/darwin/CoreText) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/darwin/DiskArbitration](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/darwin/DiskArbitration) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/darwin/FSEvents](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/darwin/FSEvents) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/darwin/Foundation](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/darwin/Foundation) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/darwin/IOKit](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/darwin/IOKit) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/darwin/ObjectiveC](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/darwin/ObjectiveC) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/darwin/Security](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/darwin/Security) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/freebsd](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/freebsd) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/info](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/info) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/kqueue](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/kqueue) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/linux](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/linux) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/linux/uring](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/linux/uring) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/llvm](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/llvm) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/orca](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/orca) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/posix](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/posix) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/unix](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/unix) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/valgrind](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/valgrind) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/wasm/js](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/wasm/js) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/wasm/wasi](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/wasm/wasi) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/sys/windows](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/sys/windows) | Platform | N | Host/platform | W3 | G2, G5, G6 |
| [core/terminal](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/terminal) | Candidate | N | Host/platform | W3 | G2, G5, G6 |
| [core/terminal/ansi](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/terminal/ansi) | Candidate | N | Host/platform | W3 | G2, G5, G6 |
| [core/testing](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/testing) | Candidate | N | Compiler/tooling | W5 | G1, G3, G5, G9 |
| [core/text/edit](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/text/edit) | Candidate | N | Text/IO | W2 | G1, G2, G3 |
| [core/text/i18n](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/text/i18n) | Candidate | N | Text/IO | W2 | G1, G2, G3 |
| [core/text/match](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/text/match) | Candidate | N | Text/IO | W2 | G1, G2, G3 |
| [core/text/regex](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/text/regex) | Candidate | N | Text/IO | W2 | G1, G2, G3 |
| [core/text/regex/common](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/text/regex/common) | Candidate | N | Text/IO | W2 | G1, G2, G3 |
| [core/text/regex/compiler](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/text/regex/compiler) | Candidate | N | Text/IO | W2 | G1, G2, G3 |
| [core/text/regex/optimizer](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/text/regex/optimizer) | Candidate | N | Text/IO | W2 | G1, G2, G3 |
| [core/text/regex/parser](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/text/regex/parser) | Candidate | N | Text/IO | W2 | G1, G2, G3 |
| [core/text/regex/tokenizer](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/text/regex/tokenizer) | Candidate | N | Text/IO | W2 | G1, G2, G3 |
| [core/text/regex/virtual_machine](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/text/regex/virtual_machine) | Candidate | N | Text/IO | W2 | G1, G2, G3 |
| [core/text/scanner](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/text/scanner) | Candidate | N | Text/IO | W2 | G1, G2, G3 |
| [core/text/table](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/text/table) | Candidate | N | Text/IO | W2 | G1, G2, G3 |
| [core/thread](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/thread) | Candidate | N | Concurrency/network | W4 | G2, G5, G6, G7 |
| [core/time](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/time) | Candidate | N | Host/platform | W3 | G2, G5, G6 |
| [core/time/datetime](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/time/datetime) | Candidate | N | Host/platform | W3 | G2, G5, G6 |
| [core/time/timezone](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/time/timezone) | Candidate | N | Host/platform | W3 | G2, G5, G6 |
| [core/unicode](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/unicode) | Candidate | N | Text/IO | W1 | G1, G2, G3 |
| [core/unicode/tools](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/unicode/tools) | Tool/generated | N | Text/IO | W1 | G1, G2, G3 |
| [core/unicode/tools/ucd](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/unicode/tools/ucd) | Tool/generated | N | Text/IO | W1 | G1, G2, G3 |
| [core/unicode/utf16](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/unicode/utf16) | Candidate | N | Text/IO | W1 | G1, G2, G3 |
| [core/unicode/utf8](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/unicode/utf8) | Candidate | N | Text/IO | W1 | G1, G2, G3 |
| [core/unicode/utf8/utf8string](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/unicode/utf8/utf8string) | Candidate | N | Text/IO | W1 | G1, G2, G3 |

## Vendor source directories (80)

| Upstream source directory | Role | Coverage | Family | Earliest wave | Gates |
| --- | --- | --- | --- | --- | --- |
| [vendor/ENet](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/ENet) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/OpenEXRCore](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/OpenEXRCore) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/OpenGL](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/OpenGL) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/box2d](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/box2d) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/box3d](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/box3d) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/cgltf](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/cgltf) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/commonmark](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/commonmark) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/compress/lz4](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/compress/lz4) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/curl](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/curl) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/darwin/AVFoundation](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/AVFoundation) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/AppKit](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/AppKit) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/AudioToolbox](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/AudioToolbox) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/CloudKit](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/CloudKit) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/CoreHaptics](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/CoreHaptics) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/CoreLocation](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/CoreLocation) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/CoreMedia](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/CoreMedia) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/CoreVideo](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/CoreVideo) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/EventKit](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/EventKit) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/Foundation](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/Foundation) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/GameController](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/GameController) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/GameKit](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/GameKit) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/LocalAuthentication](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/LocalAuthentication) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/MapKit](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/MapKit) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/Metal](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/Metal) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/MetalFX](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/MetalFX) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/MetalKit](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/MetalKit) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/ModelIO](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/ModelIO) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/QuartzCore](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/QuartzCore) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/darwin/UIKit](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/darwin/UIKit) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/directx/d3d11](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/directx/d3d11) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/directx/d3d12](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/directx/d3d12) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/directx/d3d_common](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/directx/d3d_common) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/directx/d3d_compiler](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/directx/d3d_compiler) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/directx/dxc](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/directx/dxc) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/directx/dxgi](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/directx/dxgi) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/egl](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/egl) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/fontstash](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/fontstash) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/ggpo](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/ggpo) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/glfw](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/glfw) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/glfw/bindings](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/glfw/bindings) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/kb_text_shape](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/kb_text_shape) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/libc](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/libc) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/libc-shim](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/libc-shim) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/lua/5.1](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/lua/5.1) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/lua/5.2](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/lua/5.2) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/lua/5.3](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/lua/5.3) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/lua/5.4](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/lua/5.4) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/microui](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/microui) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/miniaudio](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/miniaudio) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/nanovg](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/nanovg) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/nanovg/gl](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/nanovg/gl) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/portmidi](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/portmidi) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/raylib](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/raylib) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/raylib/rlgl](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/raylib/rlgl) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/sdl2](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/sdl2) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/sdl2/image](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/sdl2/image) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/sdl2/mixer](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/sdl2/mixer) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/sdl2/net](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/sdl2/net) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/sdl2/ttf](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/sdl2/ttf) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/sdl3](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/sdl3) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/sdl3/image](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/sdl3/image) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/sdl3/mixer](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/sdl3/mixer) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/sdl3/ttf](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/sdl3/ttf) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/stb/easy_font](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/stb/easy_font) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/stb/image](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/stb/image) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/stb/rect_pack](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/stb/rect_pack) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/stb/sprintf](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/stb/sprintf) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/stb/truetype](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/stb/truetype) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/stb/vorbis](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/stb/vorbis) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/vulkan](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/vulkan) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/wasm/WebGL](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/wasm/WebGL) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/wgpu](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/wgpu) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/wgpu/glfwglue](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/wgpu/glfwglue) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/wgpu/sdl2glue](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/wgpu/sdl2glue) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/wgpu/sdl3glue](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/wgpu/sdl3glue) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/windows/GameInput](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/windows/GameInput) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/windows/XAudio2](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/windows/XAudio2) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/windows/wasapi](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/windows/wasapi) | Platform | N | External binding | W5 | G5, G6 |
| [vendor/x11/xlib](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/x11/xlib) | Candidate | N | External binding | W5 | G5, G6 |
| [vendor/zlib](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/vendor/zlib) | Candidate | N | External binding | W5 | G5, G6 |

## Branch coordination

The foundations worktree starts at `4a751bd5f`, which includes
[#600](https://github.com/waldnercharles/chibi/pull/600)'s record-validation
performance change. Its measurements are historical evidence for that change,
not measurements of future package ports.

At this inventory checkpoint, [#569](https://github.com/waldnercharles/chibi/pull/569)
(prebuilt third-party acquisition) and stacked
[#571](https://github.com/waldnercharles/chibi/pull/571) (prebuilt SDK libraries)
remain open drafts. Their build, archive, header and licence ownership contracts
must be reconciled before vendor-package integration. A native library in an
archive does not establish a public Chibi `vendor:` binding.

The unmerged `cwaldner/first-class-vectors` branch at `be5ab05b9686` in
`.claude/worktrees/serene-shaw-b1576c` contains proposed vector/matrix semantics;
review it before numeric work rather than duplicating it. The old
`cwaldner/m5-package-imports` branch at `8cb2911ec1e8` has two unmerged commits
with alternate import/alias-hiding work; current main already has imports, so
compare the remaining semantics before reuse.

Preserve the dirty reference-sharing work in
`.claude/worktrees/trusting-davinci-8d48d9/src/execution/module_effects.c`.
It changes reference-read taint propagation and may bear on G5. Do not fold it
into a package branch without its own review. The primary checkout and old
terrain worktrees also contain unrelated changes and are unsuitable baselines.

This inventory neither imports open work nor clears holds. In particular,
[#441](https://github.com/waldnercharles/chibi/pull/441)'s explicit DO NOT MERGE
instruction remains applicable. Recheck live heads, dirty state, and PR holds
before assigning or integrating an implementation lane.
