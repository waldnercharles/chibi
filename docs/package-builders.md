# String builders

Approved on 2026-10-21: a reusable builder whose `build()` returns an independent
immutable string snapshot. Later appends, clearing the builder, or modifying a
copied builder cannot change earlier strings.

Proposed copy contract, awaiting API review: the builder follows ordinary value
semantics; copying a nonempty builder is allowed and the copies can change
independently. Snapshot isolation alone does not settle this separate choice.

## API direction

The owner prefers method-style APIs. The proposed operations are:

| Operation | Contract |
| --- | --- |
| `builder.append(text)` | Mutate this builder by appending every string byte. |
| `builder.appendByte(value)` | Append one `byte`, including zero. |
| `builder.appendChar(value)` | Append the UTF-8 encoding of a checked `char`. |
| `builder.build()` | Return an immutable snapshot and keep the builder usable. |
| `builder.clear()` | Empty this builder; prior results and other copies remain unchanged. |

Illustrative calls, pending receiver syntax and record-storage prerequisites:

```chibi
builder.append("Player")
first := builder.build()
builder.append(" 2")
second := builder.build()
builder.clear()
// first is "Player", second is "Player 2", builder is empty.
```

Appending text and bytes does not validate or normalize UTF-8. A `char` already
represents a valid Unicode scalar; its encoding is one to four bytes.

The initial implementation should copy at `build()` and permit capacity reuse on
`clear()`. Require amortized linear assembly with geometric buffer growth, plus
O(n) work per snapshot. Repeatedly building every prefix still costs quadratic
total work. More efficient storage sharing may preserve this public contract,
but must first prove snapshot and copied-builder isolation.

## Source reuse and differences

- [.NET StringBuilder.ToString](https://github.com/dotnet/runtime/blob/492b4de1f27822eb6783f623b00be7be81f53f3e/src/libraries/System.Private.CoreLib/src/System/Text/StringBuilder.cs#L315)
  allocates a result and copies the builder's chunks. This is the snapshot
  precedent. Its implementation is MIT-licensed, but UTF-16 chunks and runtime
  copy helpers are not a direct fit for Chibi's byte buffers.
- [Go strings.Builder](https://github.com/golang/go/blob/05dbbf9175d021c5a61a69e3cc22abbd483d7fb0/src/strings/builder.go)
  supplies append/growth algorithms and useful test cases under BSD-3-Clause.
  Its zero-copy string conversion relies on Go's runtime, forbids copying a
  nonempty builder, and discards backing storage on reset. Those restrictions are
  not Chibi's chosen semantics.
- [Rust String](https://github.com/rust-lang/rust/blob/fa0b453578933678d3cee100069aab45876e6be6/library/alloc/src/string.rs)
  uses vector storage and ownership-controlled mutation. Its append and growth
  code is available under MIT or Apache-2.0; adapt byte-vector algorithms without
  importing Rust String's always-valid-UTF-8 invariant or consuming ownership API.
- [Swift collections](https://www.swift.org/documentation/articles/value-and-reference-types.html)
  provide the proposed value-copy precedent: an array, including one inside a
  struct, behaves as an independent value after assignment. Its
  [Array source](https://github.com/swiftlang/swift/blob/bf3cf2cfb69ebb025b4ffce538fe3cde027d5246/stdlib/public/core/Array.swift#L218)
  checks for unique storage before mutation and copies when necessary. This is
  a useful ownership implementation reference; its runtime and compiler hooks
  require adaptation to Chibi. .NET StringBuilder
  is a class and assignment shares the mutable object; its snapshot operation
  alone does not determine Chibi's builder-copy behavior.

Port suitable algorithm and test code into Chibi, retain attribution, and record
the pinned upstream revision. Keep unavoidable runtime allocation/copy hooks
narrow; do not introduce an opaque native builder object.

## Prerequisites

The 2026-10-21 audit at engine `73a6e2b7f` found three storage gaps:

1. Both compiler stages reject array-valued record fields. The reflected field
   representation lacks container/element metadata, and record snapshots,
   garbage collection and persistence need corresponding support. Merely
   accepting `byte[]` in the parser would not establish safe ownership.
2. The existing in-place array optimization applies to ordinary local arrays,
   not nested record fields. Reference writes copy the new value and its root.
   Supporting record layout alone would still leave repeated appends expensive.
3. Array clear currently creates a new empty array. Capacity-preserving reset
   needs an owned-buffer operation or a separate logical length.

A one-variant union can already hold a byte array and preserve value-copy
semantics. Its payload bindings are read-only; copying to a mutable local and
repacking it copies the buffer. This is a possible functional implementation,
but does not establish amortized-linear appends or capacity reuse. Prefer fixing
the useful record and ownership gaps before building on that representation.

Source anchors: `src/language/semantic_record.c`, `src/types/type.h`,
`src/types/array.c`, `src/types/trace.c`, `src/types/value_wire.c`,
`src/language/lower.c`, `src/execution/intrinsics.c`, and
`src/runtime/arrays.c`. The audited `.claude` result-location and argument-sharing
worktrees do not implement array-valued record fields. Their ownership changes
must be considered when implementing efficient nested mutation.

Method calls must follow the shared receiver API design. A length-aware
byte-buffer-to-string copy must preserve NUL and arbitrary bytes. Coordinate
managed field representation after draft PR 633's native string-field metadata;
the shared-string chat owns string storage and the receiver API. These are
prerequisites, not implemented builder capabilities.
