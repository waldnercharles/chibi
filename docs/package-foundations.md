# Package foundations decision record

Status: string invariant approved on 2026-10-21; detailed APIs remain proposed. The owner selected
immutable, length-carrying, byte-preserving strings with explicit UTF-8 operations and embedded NUL.
Other semantics and fidelity remain decisions for the user, separately per package. Helpful language prerequisites precede dependent
implementations. This record covers bytes, strings and UTF-8; it is not an exhaustive Odin API review.

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

| Responsibility | Proposed owner |
| --- | --- |
| Value layout, string length/storage/lifetime, checked scalar conversion, allocation failure and intrinsics | Language/runtime, with private implementation modules |
| Binary operations, codecs, builders and portable text algorithms | `core:bytes`, `core:utf8`, `core:strings` |
| ECS declarations, entity identity and queries | Existing language contracts; explicit world inspection may use future `core:ecs` as recorded in PLAN.md |
| Rendering, input, audio, physics, assets and UI | Existing `game:*` collection direction in PLAN.md; each public package still needs its contract |
| Third-party declarations, target linkage, ABI adapters | `vendor:*`, introduced for concrete consumers |

Recommend no public `base:*` in the first wave. Document builtins in the language reference and keep runtime
layout/intrinsics private. Odin's [base collection](https://pkg.odin-lang.org/base/) includes
compiler-required runtime declarations. Copying that API would promise a compiler/runtime contract before a
Chibi consumer needs it. An expert public base collection is an alternative requiring an explicit
stability/access policy. Generic core must work without a world, renderer or gameplay catalog.

## Two concrete string models

Approved on 2026-10-21: immutable, length-carrying, byte-preserving `string`, with explicit UTF-8 operations.
Arbitrary binary buffers use `byte[]`. Both admit NUL. This follows existing byte-oriented compiler/string
algorithms and avoids making all native inputs, source files and filesystem names valid Unicode as a global
precondition. Exact encoded equality is not Unicode normalization.

The alternative is immutable valid-UTF8 `string`, also length carrying and allowing U+0000. It gives text APIs
an invariant, prevents byte slicing from creating malformed text and aligns with scalar `char`. It requires
validating all literals, native callbacks, deserialization and foreign/host inputs; strict scalar-boundary
slicing; changing empty-separator and cutset operations; and a separate representation for invalid external
names/source. Validating just a `FromBytes` function cannot establish that invariant. Neither model makes a
Unicode scalar equal to a displayed grapheme or implies normalization.

The byte-preserving recommendation is about a useful compiler/tool workflow, not just patch size: parsers and
diagnostics need exact byte preservation even when input is malformed, and binary formats need lossless round
trips. The valid-text alternative is preferable if the user wants every ordinary string consumer to rely on
Unicode validity and accepts that broader boundary audit. Package fidelity remains per package under either
model.

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
| `bytes.Compare`, `Index`, `Clone`, `Slice`, `Append` | Unsigned lexicographic order; `-1` search miss; independent copies; strict ranges; append through ref with value-copy isolation. Empty needles match offset zero. |
| Byte-buffer indexing/slicing | Checked bounds, arbitrary bytes, value semantics. Verify actual array slicing support first; functions can precede new syntax. |

Future direct `s[i]` syntax can return a checked `byte`, with an out-of-range call failure; a try operation
can report expected lookup failure. That syntax is optional and need not redefine `ByteAt`. Strict range
helpers should report invalid bounds rather than clamp; add one only for an approved consumer. Check `count <=
length - start` after validating `start`, avoiding overflowing sums.

Strings remain immutable and valid as values after return, assignment and container storage. Independent
values may share backing storage only when the runtime retains its owner. Copying is allowed initially. Public
borrowed views, pointers into temporary storage and mutable aliases wait for lifetime rules. Length-carrying
storage alone does not supply ownership.

Foreign C-string conversion must reject interior NUL and allocate/provide an owned terminator for an
explicitly documented lifetime. Converting a raw C pointer requires a trusted bound or a deliberate bounded
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

Current valid-string iteration yields a one-scalar string. Preserve that result type initially.
Malformed-string iteration needs an explicit decision and audit of the current decoder: recommend U+FFFD and
one-byte advance on invalid input so iteration progresses; strict failure is the alternative. This proposed
rule must not silently replace a different existing contract. Explicit Decode stays strict. A future loop
yielding `char` plus byte offset is separate ergonomics.

## Builders and compatibility

Recommend `strings.Builder` with AppendText, AppendByte, AppendChar, Build and Reset, optionally Reserve.
Byte/string appends preserve arbitrary bytes; AppendChar UTF-8 encodes a valid scalar. Build copies to an
independent string; reset, later mutation, copied builders and scope exit cannot affect old results. Copies
retain value semantics; ref operations mutate only the caller's value. Require amortized linear assembly and
O(n) Build, rather than repeated prefix copies. Zero-copy Take and explicit allocators need separate lifetime
policy.

An authored builder record containing `byte[]` needs container-valued record fields and correct copy/ref
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

The source audit found one-pointer, NUL-terminated strings throughout the value
system. Length-carrying strings require a coordinated runtime change, not only
new text helpers. UTF-8 validity is a separate semantic choice.

| Layer | Existing dependency and required work |
| --- | --- |
| Values and execution | [value.h](../src/types/value.h) and [scalar.h](../src/types/scalar.h) use `const char *`; [function.h](../src/execution/function.h) uses `uint64_t` execution cells. Interpreter, SLJIT and C-backend moves/comparisons must agree on the new representation. |
| Records and native fields | [type.c](../src/types/type.c) reads/writes `PIN_STR` fields as C pointers; stage one's [Compile.chibi](../tools/compiler/package_compiler/Compile.chibi) assigns text fields pointer size. Authored strings need explicit adapters to reflected native C-string fields. |
| Ownership, equality and keys | [array.c](../src/types/array.c) copies with `strlen`/`xstrdup`; [trace.c](../src/types/trace.c) traces one pointer. Copy/free/trace paths, [map.c](../src/types/map.c), [index.c](../src/types/index.c) and persistent keys must use byte lengths. Distinct suffixes after NUL must remain distinct values. |
| Formatting and transport | [text_format.c](../src/runtime/text_format.c) measures and emits C strings. [value_document_read.c](../src/types/value_document_read.c) rejects a document string whose explicit length differs from `strlen`; [value_document.c](../src/types/value_document.c) writes via a C-string helper. Preserve lengths through both directions. |
| Prepared persistence | [value_wire.c](../src/types/value_wire.c) already prefixes encoded strings with a length but derives it from a terminator and rejects interior NUL. Separate value-string encoding from metadata/name encoding, with bounded allocation and ownership on decode. |
| Compiler host and bootstrap | [compiler_host.c](../src/toolchain/compiler_host.c) measures string quotas with `strnlen`; [compiler_target.c](../src/toolchain/compiler_target.c) exports layout facts. Update host adapters, explicit semantic ABI invalidation and [bootstrap recovery](package-compiler.md) together. Unchanged struct sizes alone cannot prove compatibility. |

A two-word pointer/length value would change execution cells, calls and record
layouts. Use a pointer to an immutable descriptor for the first implementation,
with its length and copied bytes owned together. Keep the existing execution-cell
width and copying at ownership boundaries; shared slices can follow measured
need. This still changes the semantic ABI. A trailing zero may serve explicit
C adapters, but must never determine value length. Do not assume a hidden length
header exists before arbitrary C pointers: literals, native fields and foreign
pointers do not have one.

The smallest preparation step is to centralize runtime string construction,
length, byte access, equality and ownership while preserving current behavior.
Keep value strings distinct from symbol names and native C strings. Public NUL
support then waits until the storage, copy/trace, equality/hash, transport,
formatting and compiler-host paths agree. The byte-preserving contract is
approved; this implementation choice does not approve the proposed package APIs.

The 2026-10-21 audit found two boundaries that must move with storage:

- Native reflected C-string fields and authored string fields currently share
  `PIN_STR`. Field reads have no allocator and field writes have no failure
  result. Distinguish their storage explicitly and provide owned reads and
  checked writes; pointer size cannot distinguish the two. Generated native
  glue and direct field instructions must use the same adapters.
- Value strings and metadata currently share literal and prepared-string
  decoding. Split those paths so values preserve bytes while names and source
  text retain their intentional restrictions. Explicit-length JSON functions
  preserve escaped NUL, but ordinary JSON strings cannot round-trip arbitrary
  invalid UTF-8. Document persistence needs a lossless byte encoding; textual
  host APIs need an explicit rejection outcome. Neither may replace bytes
  silently.

The storage cutover must include ownership, native fields, literals, all execution
backends, equality/hash, formatting, prepared values, document transport and host
quotas together. Acceptance must retain `A\0B` after its source owner is freed,
distinguish it from `A` and `A\0C` as map keys, preserve malformed UTF-8 in byte
operations and binary persistence, and exercise each C-string adapter's NUL
failure. Current runtime strings still use the old representation.

[compiler_json.c](../src/toolchain/compiler_json.c) can already decode a JSON
string with NUL and return its byte length; downstream C-string users can lose
the suffix. That does not establish runtime support. Conversely,
[source_package_snapshot.c](../src/authoring/source_package_snapshot.c)
deliberately rejects source NUL. Preserve explicit source-language rejection
when runtime values gain NUL support; do not let parser input truncate or
silently change the source contract.

### Implementation sequence

1. Agree on relevant decisions below and the initial API subset per package.
2. Verify byte arrays, multiple results, ref writes and scalar conversions in
   both stages/backends. Fix demonstrated gaps before dependent functions.
3. Add length-carrying owned strings and NUL-safe creation/host/serialization
   paths. Preserve byte algorithms and migrate the Bytes result type together.
4. Implement strict UTF-8 policy in Chibi with narrow runtime conversion hooks.
5. Add container record fields if needed, then the value-semantics builder.
6. Complete the approved strings subset and bounded complexity checks.

These are dependencies, not mandatory separate PRs. Bytes and codecs can run in parallel once their shared
contracts are fixed. No public allocator/context, borrow checker, generic constraints, callable values or
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
strings.AppendChar(builder, '🐈')         // proposed ref parameter, current call style
label := strings.Build(builder)
strings.Reset(builder)                   // label remains stable
```

## Decisions to settle

| Decision | Recommendation | Alternative |
| --- | --- | --- |
| String invariant — approved 2026-10-21 | Byte preserving, immutable, length carrying, NUL allowed; explicit UTF-8 operations | Valid UTF-8 globally was considered and not selected |
| Length/offsets | Byte units; explicitly named scalar operations | Scalar indexing with traversal cost and separate byte-offset API |
| Access failures | Preserve ByteAt sentinel and Slice clamping; strict future syntax | Change existing helpers to checked failure/try variants and migrate callers |
| Ownership | Independent values; implementation copying allowed | Public retained-owner views after lifetime contract |
| Malformed iteration | Explicit strict codec; replacement in ordinary iteration after audit | Strict iteration failure |
| Characters | Existing char; preserve one-scalar-string loop result initially | Change loop yield to char with byte offset after caller audit |
| Builder | Value record, ref mutation, Build copy; container fields first | Byte-array append helpers until builder record support |
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
