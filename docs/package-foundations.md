# Package foundations decision record

This is the historical foundation proposal. The later approved
[portable byte-library decision](package-byte-libraries.md) owns the implemented
bytes, bits and checksum subset. Other proposals below retain their stated status.

Status: string invariant approved on 2026-10-21; detailed APIs remain proposed. The owner selected
immutable, length-carrying, byte-preserving strings with explicit UTF-8 operations and embedded NUL.
The merged string design specifies one public `string` type and a descriptor behind the existing
pointer, followed by shared slices. The descriptor implementation is local to
the isolated package-foundations worktree and passes its selected integration
checks; publication evidence is in the [package library plan](package-library-plan.md).
Other semantics and fidelity remain decisions for the user, separately per package. Helpful language prerequisites precede dependent
implementations. This record covers bytes, strings and UTF-8; it is not an exhaustive Odin API review.
Discuss each new semantic step with the owner before implementing it. The approved invariant and migration
direction do not approve the pending API and edge-case proposals below.

## Baseline and ownership

Reference Odin commit
[`779c8c0a4778f994389e2a681ffc7b3cbd6c1f65`](https://github.com/odin-lang/Odin/tree/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65),
returned by `git ls-remote` during research. Generated package pages can advance independently. Package
inventories should link pinned declarations.

Chibi source inspected at `4a751bd5f`: [Strings.chibi](../packages/core/strings/Strings.chibi),
[text_format.c](../src/runtime/text_format.c), [language reference](code-language.md), [friction
log](language-friction.md), and character cases in [semantic.c](../test/language/semantic.c). The bridge uses
`strlen`; `Slice` clamps and copies; `Bytes` returns `int[]`; `ByteAt` returns `-1` outside bounds. Strings
helpers use byte offsets and ASCII case/space rules. `Split(s, "")` splits bytes and `Count(s, "")` counts
byte boundaries. Existing `char` represents a Unicode scalar, including U+0000. These are source observations,
not fresh test results. Friction suggestions remain proposals; implementation must verify gaps in both
compiler stages.

The later [merged string design](https://github.com/waldnercharles/chibi/blob/a9d87eaf1a28ff15c710d8475af1a8b722d98bfa/docs/design-notes.md#strings-carry-their-length)
records the representation and implementation phases. The descriptor cutover is implemented locally;
validation and integration status are recorded in the package library plan. ByteLength and ByteAt use
stored lengths. Slice still copies its selected bytes; shared backing remains a separate phase.

| Responsibility | Proposed owner |
| --- | --- |
| Value layout, string length/storage/lifetime, checked scalar conversion, allocation failure and intrinsics | Language/runtime, with private implementation modules |
| Binary operations, codecs, builders and portable text algorithms | `core:bytes`, `core:utf8`, `core:strings` |
| ECS declarations, entity identity and queries | Existing language contracts; explicit world inspection may use future `core:ecs` |
| Rendering, input, audio, physics, assets and UI | `game:*`; each public package still needs its contract |
| Third-party declarations, target linkage, ABI adapters | `vendor:*`, introduced for concrete consumers |

Recommend no public `base:*` in the first wave. Document builtins in the language reference and keep runtime
layout/intrinsics private. Odin's [base collection](https://pkg.odin-lang.org/base/) includes
compiler-required runtime declarations. Copying that API would promise a compiler/runtime contract before a
Chibi consumer needs it. An expert public base collection is an alternative requiring an explicit
stability/access policy. Generic core must work without a world, renderer or gameplay catalog.

## Approved string model and considered alternative

Approved on 2026-10-21: immutable, length-carrying, byte-preserving `string`, with explicit UTF-8 operations.
Arbitrary binary buffers use `byte[]`. Both admit NUL. This follows existing byte-oriented compiler/string
algorithms and avoids making all native inputs, source files and filesystem names valid Unicode as a global
precondition. Exact encoded equality is not Unicode normalization.

The considered alternative was immutable valid-UTF8 `string`, also length carrying and allowing U+0000. It
gives text APIs an invariant, prevents byte slicing from creating malformed text and aligns with scalar `char`. It requires
validating all literals, native callbacks, deserialization and foreign/host inputs; strict scalar-boundary
slicing; changing empty-separator and cutset operations; and a separate representation for invalid external
names/source. Validating just a `FromBytes` function cannot establish that invariant. Neither model makes a
Unicode scalar equal to a displayed grapheme or implies normalization.

The approved byte-preserving model supports compiler/tool workflows: parsers and
diagnostics need exact byte preservation even when input is malformed, and binary formats need lossless round
trips. The valid-text alternative would require every ordinary string consumer to rely on Unicode validity
and the broader boundary audit above. That alternative was not selected. Package fidelity remains a separate
decision for each package.

## Proposed first-wave operations

Names below are proposed; existing names are retained where their semantics are useful. Exact signatures and
upstream inclusions need package approval.

| Operation | Recommended contract under byte-preserving strings |
| --- | --- |
| `strings.ByteLength(s)` | O(1) encoded byte count. `int` index domain; reject unsupported lengths at creation before overflow. |
| `strings.ByteAt(s, i)` | Preserve current `int` result, `0..255` or `-1` outside bounds. It must become O(1). This avoids changing sentinel callers just to add packages. |
| `strings.Bytes(s)` | Independent `byte[]` copy, including zero bytes. Intentional signature migration from `int[]`. |
| `strings.FromBytes(data)` | Independent string copy of every byte, without UTF-8 validation. No NUL truncation. |
| `strings.Slice(s, start, count)` | Preserve current clamping and independent value semantics, including cuts inside UTF-8. Negative start/count clamp to zero, start past end clamps to end, excessive count to remaining length. |
| `strings.Index` / `LastIndex` | Exact byte-substring search; `-1` if absent. Empty needle at zero / byte length respectively. No normalization or locale rules. |
| `Split(s, "")`, `Count(s, "")` | Preserve byte splitting / byte count plus one. Scalar variants belong in explicitly Unicode operations. |
| `bytes.Compare`, `Index`, `Clone`, `Slice`, `Append` | Unsigned lexicographic order; `-1` search miss; independent copies; strict ranges; append through a pointer parameter with value-copy isolation. Empty needles match offset zero. |
| Byte-buffer indexing/slicing | Checked bounds, arbitrary bytes, value semantics. Verify actual array slicing support first; functions can precede new syntax. |

Future direct `s[i]` syntax can return a checked `byte`, with an out-of-range call failure; a try operation
can report expected lookup failure. That syntax is optional and need not redefine `ByteAt`. Strict range
helpers should report invalid bounds rather than clamp; add one only for an approved consumer. Check `count <=
length - start` after validating `start`, avoiding overflowing sums.

Strings remain immutable and valid as values after return, assignment and container storage. Independent
values may share backing storage only when the runtime retains its owner. Copying is allowed initially. Public
borrowed views, pointers into temporary storage and mutable aliases wait for lifetime rules. Length-carrying
storage alone does not supply ownership.

Approved on 2026-10-21: reject a provably embedded-NUL value at a C-string-only
use during compilation. Validate values whose contents are unknown before a
native call or field write. A rejected operation does not call C or change its
destination. New public APIs accepting external data return an explicit,
recoverable error, with the offending byte offset where available. This is
distinct from the existing invocation-failure diagnostic, which stops the
current invocation. Proven-safe values can omit redundant runtime validation.
Ordinary Chibi strings and length-aware APIs continue to preserve every byte.
Successful adapters provide a terminator with an explicitly documented lifetime.
Converting a raw C pointer requires a trusted bound or a deliberate bounded
terminator scan. Filesystem/OS adapters separately specify their native encoding and errors; no generic string
conversion may silently truncate, normalize or replace bytes.

## UTF-8 and character behavior

Keep existing scalar `char`; do not add `rune` as another type/alias for Odin spelling. A scalar is
`0..0x10FFFF` excluding `0xD800..0xDFFF`. `char(n)` stays checked; wrapping/clamping is not a valid scalar
conversion. U+0000 is data.

| Proposed operation | Results and errors |
| --- | --- |
| `utf8.Validate(data: byte[])` | Success and first invalid byte offset; offset `-1` on success. Empty data succeeds. |
| `utf8.Decode(data: byte[], offset: int)` | Scalar, width, status: success, end, malformed, truncated, invalid offset. Failure width is zero; scalar U+0000 is ignored unless success. |
| `utf8.Encode(value: char)` | One to four bytes; every legal `char` succeeds. |
| `utf8.Count(text: string)` | Count plus success/error offset; arbitrary strings need validation during decoding. It is O(n). |

Reject overlong sequences, encoded surrogates, values above U+10FFFF, stray continuations and incomplete
sequences. Decode consumes nothing on failure. Use ordinary multiple results or a small scalar-field result
record; verify status enum support in both compilers. Generic Result types, propagation syntax and exception
machinery are not prerequisites. OOM remains runtime allocation failure, not a newly invented optional result
for every helper.

Current valid-string iteration yields a one-scalar string. The current decoder rejects malformed UTF-8 with
`String iterator requires valid UTF-8`. Recommend preserving that strict behavior and result type during the
storage migration. Replacement with U+FFFD and one-byte advance remains an unapproved alternative requiring
discussion before implementation. The explicit Decode proposal stays strict. A future loop
yielding `char` plus byte offset is separate ergonomics.

## Builders and compatibility

Recommend `strings.Builder` with AppendText, AppendByte, AppendChar, Build and Reset, optionally Reserve.
Byte/string appends preserve arbitrary bytes; AppendChar UTF-8 encodes a valid scalar. Build copies to an
independent string; reset, later mutation, copied builders and scope exit cannot affect old results. Copies
retain value semantics; pointer-parameter operations mutate only the caller's value. Require amortized linear assembly and
O(n) Build, rather than repeated prefix copies. Zero-copy Take and explicit allocators need separate lifetime
policy.

An authored builder record containing `byte[]` needs container-valued record fields and correct copy/pointer
behavior in both stages. Approve that prerequisite before its dependent builder. If it is a large step, ship
codecs and byte-array append helpers first. Do not add temporary opaque native builder handles.

Current helpers remain byte/ASCII operations unless individually changed. Unicode casing, whitespace,
normalization and grapheme segmentation require separate data-version/fidelity decisions. Review `Cut`'s
forgiving offset/count semantics separately; its spelling does not imply Odin or Go separator semantics.
`Repeat` should fail for negative counts and preflight output size, subject to compiler-host assertion
support. Record and approve that behavior change.

Required compatibility audit: native string storage/copy/free, prepared values, serialization, formatting,
hashing/equality and C bridges; compiler/tokenizer `Bytes` consumers changing `int[]` to `byte[]`; any new
strict index API callers; and NUL handling throughout host calls. Existing Slice/Split/Count semantics remain
explicit fidelity choices, not accidental Unicode promises. Rewrite repeated format_text assembly after
builder support. Follow AGENTS.md's direct asset migration rule; only bump project version for a named used
authored contract requiring rewriting, not merely for runtime representation changes.

## Prerequisite order and call sketches

### String representation boundary

The initial source audit found one-pointer, NUL-terminated strings throughout
the value system. The local cutover replaces that representation across these
layers. UTF-8 validity remains separate from the byte-preserving string invariant.

| Layer | Local descriptor implementation |
| --- | --- |
| Values and execution | [value.h](../src/types/value.h) stores `PIN_STR` in `.text`, pointing to a descriptor. `PIN_NAME` retains its C-string `.str`. Execution cells remain one pointer wide; moves, copies and comparisons use the descriptor. |
| Records and native fields | [type.c](../src/types/type.c) distinguishes authored descriptor fields with `text_descriptor`. Native C fields use checked adapters; compiler catalogs carry the corresponding `c_string` marker. |
| Ownership, equality and keys | [array.c](../src/types/array.c), [trace.c](../src/types/trace.c), [map.c](../src/types/map.c) and [index.c](../src/types/index.c) preserve explicit lengths and owned bytes. Suffixes after NUL participate in equality and keys. |
| Formatting and transport | [text_format.c](../src/runtime/text_format.c) preserves lengths. Document strings preserve NUL; malformed UTF-8 uses the lossless `{"bytes":[...]}` representation. Names retain their restrictions. |
| Prepared persistence | [value_wire.c](../src/types/value_wire.c) separates raw value bytes from metadata strings and charges descriptor storage to decode budgets. Prepared format 20 carries the new field marker. |
| Compiler host and bootstrap | Host quotas use stored lengths; target layout facts and semantic ABI include the new metadata. Bootstrap regeneration and [recovery](package-compiler.md) accompany publication. Unchanged struct sizes do not imply compatibility. |

A two-word pointer/length value would change execution cells, calls and record
layouts. The merged design uses one pointer to an immutable descriptor,
`{const char *bytes; uint32_t length}`, over an owned backing buffer. Keep the
existing execution-cell width. Its third phase makes slices share the parent's
bytes, retaining that backing for the slice's lifetime. Initial copying at
ownership boundaries is migration staging, not a replacement for the recorded
shared-slice direction. This still changes the semantic ABI. A trailing zero may
serve explicit C adapters, but must never determine value length. Do not assume a hidden length
header exists before arbitrary C pointers: literals, native fields and foreign
pointers do not have one.

Construction, byte access and equality are centralized in [text.h](../src/types/text.h).
Value strings remain distinct from symbol names and native C strings. The
byte-preserving contract is approved; this implementation does not approve the
proposed package APIs.

The 2026-10-21 cutover addresses two boundaries identified by the initial audit:

- `pin_read_field` and `pin_write_field` accept an arena and return an error.
  `text_descriptor` distinguishes authored descriptors from native C pointers.
  Authored reads borrow descriptors; native reads construct owned descriptors;
  native writes validate before mutation. Generated glue and field instructions
  use the same storage distinction.
- Values and metadata have separate literal and prepared-string decoding.
  Values preserve bytes while names and source text retain their restrictions.
  Document persistence uses a lossless byte encoding for malformed UTF-8;
  textual host boundaries reject unsupported values without replacing bytes.

The storage cutover must include ownership, native fields, literals, all execution
backends, equality/hash, formatting, prepared values, document transport and host
quotas together. Acceptance must retain `A\0B` after its source owner is freed,
distinguish it from `A` and `A\0C` as map keys, preserve malformed UTF-8 in byte
operations and binary persistence, and exercise each C-string adapter's approved
NUL outcome. The in-progress cutover must pass these checks before it is
reported as implemented.

[json_string](../tools/compiler/json_string/JsonString.chibi) keeps strict Unicode
decoding for metadata in `Metadata` and permits arbitrary literal bytes in `Value`;
both return explicit lengths. Conversely,
[source_package_snapshot.c](../src/authoring/source_package_snapshot.c)
deliberately rejects source NUL. Preserve explicit source-language rejection
while runtime values support NUL; do not let parser input truncate or
silently change the source contract.

### Implementation sequence

The merged design records three phases. This checkpoint combines accessor
preparation with the descriptor cutover so the value ABI changes together:

1. Route C value-string readers and producers through accessors. Preserve the
   current representation and behavior; accessors still measure C strings.
   This preparation does not expose embedded-NUL support.
2. Switch accessors and producers to descriptors, migrate native C-field
   adapters, and update ownership, literals, prepared/document formats and
   host boundaries together. Bump the prepared format and rebuild the
   bootstrap through the documented procedure. Implement the approved static
   and runtime C-NUL boundary safeguards as part of that cutover.
3. Make Slice share retained backing bytes. ByteAt and ByteLength already use
   the stored length in constant time after the cutover. Add direct compiler
   byte paths where useful; shared slice ownership remains pending.

Static C-NUL checks cover values that the compiler can prove. Stage one's
current proof is limited to literal and parenthesized uses within its supported
constant subset. Runtime adapters validate unknown values before native effects.

New package operations follow their own semantic reviews. In particular,
changing `strings.Bytes` from `int[]` to `byte[]`, adding `FromBytes`, and
choosing UTF-8 decoder result signatures remain proposed. They are not implicit
parts of the representation approval. Builders need their own approved API
and container-valued record fields before implementation.

Later package dependencies do not require one PR per operation. Bytes and codecs
can run in parallel once their shared contracts are fixed. No public
allocator/context, borrow checker, generic constraints, callable values or
Unicode tables are required for this selected subset. New syntax is optional unless a reproducer shows a
correctness or performance blocker. Track each discovered limitation in the existing friction log with
workaround, affected package and cost.

Conceptual proposed calls, not compilable fixtures: APIs, result destructuring and builder record support
still require verification/implementation.

```chibi
bytes := strings.Bytes("Aé")              // byte[]; three bytes
part := strings.Slice("Aé", 1, 1)         // one byte; invalid UTF-8 allowed
valid, badOffset := utf8.Validate(bytes)  // true, -1
text := strings.FromBytes([65, 0, 66])    // three bytes, embedded NUL
builder: strings.Builder = {}
strings.AppendChar(&builder, '🐈')        // proposed pointer parameter
label := strings.Build(builder)
strings.Reset(&builder)                  // label remains stable
```

## Decisions to settle

| Decision | Recommendation | Alternative |
| --- | --- | --- |
| String invariant — approved 2026-10-21 | Byte preserving, immutable, length carrying, NUL allowed; explicit UTF-8 operations | Valid UTF-8 globally was considered and not selected |
| Length/offsets | Byte units; explicitly named scalar operations | Scalar indexing with traversal cost and separate byte-offset API |
| Access failures | Preserve ByteAt sentinel and Slice clamping; strict future syntax | Change existing helpers to checked failure/try variants and migrate callers |
| Ownership — shared-slice direction recorded in merged design | Immutable values; slices retain backing ownership; copying can stage the migration | Public borrowed views still need a separate lifetime contract |
| Native C-string interior NUL — approved 2026-10-21 | Diagnose known-invalid uses during compilation; validate unknown values before native effects; expose recoverable errors in new public APIs | Do not truncate or abort the process on invalid input |
| Malformed iteration — no replacement approved | Preserve current strict failure; explicit Decode proposal is also strict | U+FFFD and one-byte advance after a separate decision |
| Byte/string conversion signatures — proposed | Review `Bytes -> byte[]` and `FromBytes` with package consumers | Retain current `Bytes -> int[]` until that review |
| Characters | Existing char; preserve one-scalar-string loop result initially | Change loop yield to char with byte offset after caller audit |
| Builder | Value record, pointer-parameter mutation, Build copy; container fields first | Byte-array append helpers until builder record support |
| Base/allocators | Private runtime hooks, runtime-managed values | Public expert base and explicit allocation policy with concrete consumers |
| Fidelity | Approve and track individual package/API subsets | Broader Odin equivalence with all required language/runtime contracts |

## Primary comparisons

### String guarantees compared on 2026-10-21

The owner requested these comparisons, then selected byte-preserving strings
with explicit UTF-8 operations on 2026-10-21.

| Language | Representation and UTF-8 guarantee | Consequence |
| --- | --- | --- |
| [Odin](https://odin-lang.org/docs/overview/#string-type) | Pointer plus byte length. Text convention and rune iteration use UTF-8, but strings can contain invalid sequences; [UTF-8 validation](https://pkg.odin-lang.org/core/unicode/utf8/#valid_string) is explicit. | Byte indexing/slicing and Unicode decoding are distinct operations. Converting a byte slice to a string does not establish valid text. |
| [Zig 0.15.2](https://ziglang.org/documentation/0.15.2/#String-Literals-and-Unicode-Code-Point-Literals) | Byte arrays/slices, commonly `[]const u8`, rather than a distinct string primitive. Literals carry a trailing zero and can contain invalid UTF-8 via byte escapes. | Slicing/indexing operates on bytes. A byte slice does not promise Unicode validity. |
| [Rust str](https://doc.rust-lang.org/std/primitive.str.html#invariant) and [String](https://doc.rust-lang.org/std/string/struct.String.html#utf-8) | Borrowed `&str` is pointer plus byte length; `String` owns its buffer. Both require valid UTF-8. Arbitrary bytes use byte slices/vectors. | Safe byte-to-text conversion validates; string slicing uses byte offsets but requires UTF-8 boundaries. The invariant still does not provide grapheme indexing or normalization. |
| [Jai creator demonstration](https://www.youtube.com/watch?v=mGe5d6dPHAU&t=1736s) | In Jonathan Blow's 2018-02-13 demo, strings have a count and data pointer and can hold binary-file data, including zero bytes. At 34:22 he explains byte lengths and an additional terminator on literals. | The binary-file example supports the inference that valid UTF-8 is not a type invariant, despite UTF-8 being the native text encoding. This is historical creator evidence; a current normative compiler guarantee and current slicing rules were not verified. |

Jai evidence was obtained from the creator video's original automatic captions,
using transcript-only retrieval. Relevant passages are 28:56–29:35 (count/data
and binary input), 34:22–34:33 (byte length/literal terminator), and 39:06–40:11
(UTF-8 convention and embedded zeros). Do not promote those historical facts to
verified current compiler behavior without checking a current distribution.

Pointer-plus-length storage does not itself choose an encoding invariant:
Rust uses it too. NUL is also separate from UTF-8 validity; U+0000 is valid
UTF-8, and [Rust string literals allow it](https://doc.rust-lang.org/reference/expressions/literal-expr.html#simple-escapes).
Chibi can use a byte-preserving model while retaining its own immutable,
runtime-owned value semantics; adopting Odin or Jai's pointer/lifetime model is
not required.

### Allocation and ownership

[Odin strings at the pinned
revision](https://github.com/odin-lang/Odin/blob/779c8c0a4778f994389e2a681ffc7b3cbd6c1f65/core/strings/strings.odin)
separates allocation-backed clones and non-owning pointer conversions; Chibi needs explicit ownership even
when it chooses different APIs. [Go strings](https://go.dev/ref/spec#String_types) are immutable byte
sequences; [Rust str](https://doc.rust-lang.org/std/primitive.str.html#invariant) requires valid UTF-8. Both
alternatives above have established uses. [Zig 0.15.2 memory
rules](https://ziglang.org/documentation/0.15.2/#Memory) require documenting ownership/lifetime and
conventionally pass allocators; [Odin context](https://odin-lang.org/docs/overview/#implicit-context-system)
uses scoped defaults. Neither allocation model follows automatically from adopting their package algorithms.
