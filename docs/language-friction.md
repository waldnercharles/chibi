# Language friction

Where writing Chibi was awkward, how existing languages handle the same need,
and what Chibi could adopt. Threads add entries when they hit friction; each
entry checks prior art before it suggests anything. An entry that already
covers a need takes the new example, with the PR that found it, instead of a
new entry. Examples come from the stage-one compiler
(`tools/compiler/package_compiler`) unless an entry says otherwise.

Each entry starts with its class:

- **Language**: both compilers. Stage zero builds stage one, so stage one's own
  source can use a feature only once stage zero accepts it.
- **Stage one**: stage zero already has it; stage one does not yet.
- **Runtime**: a value representation or an implementation cost, not syntax.
- **Toolchain**: build programs, test harnesses and the prepared form.

A **Decision** records the owner's choice; nothing here is implemented unless
the entry says so.

The [package library plan](package-library-plan.md) prioritizes prerequisites
for selected Odin-inspired packages. Add package evidence to the matching entry
here; the plan records scheduling and package decision records own proposed APIs.

## Managed pointer boundaries

Language and runtime. The approved managed-pointer implementation is local to
`codex/managed-pointers-20261022`; it is not yet merged. The
[contract and validation ledger](managed-pointers.md) distinguish supported
operations from remaining limits.

- `&array[index]` and `&map[key]` need an identity rule when a container grows,
  removes an entry or changes through another value copy. Address-taking
  currently rejects these locations. A pointer to the whole container remains
  a separate operation.
- A scoped pointer parameter can name temporary frame storage, so taking its
  address makes it an ordinary managed pointer instead; `F(pointer)` already
  lends a managed target to a scoped parameter.
- A presence guard on a nullable local can be tracked. Repeated mutable field
  reads cannot assume an alias left the field unchanged. Bind the pointer to a
  local before checking and dereferencing it.
- Ordinary native calls and ECS storage have no managed-heap root contract.
  They reject pointers, including pointers hidden in aggregate values.
- Hot replacement must retain both the old allocations and their type layouts.
  Keeping bytes alive alone is insufficient when tracing depends on a record
  shape that a later catalog can replace.
- Query component references retain exact native schema closures, including
  nested records, strings, offsets and enum widths. Removing a component or
  destroying its entity or world permanently invalidates the original target;
  a later add has a new identity. Invalid access produces an execution failure
  and diagnostic. Authored catch or checked recovery operations remain absent.
  Native query value snapshots use the same owned schema representation.
  Ordinary `&native_local` and address-taking on authored records with inline
  record fields remain rejected by the language's address-taking rules. Flat
  authored records and recursive links through pointer fields are supported.
- Stage one's body emitter receives a fixed type catalog. Its address prepass
  conservatively declares a pointer to every declared type, and one to each
  of those, for each package that takes an address, including unused
  combinations. Until 2026-10-30 it added a layer for each file with an
  address, so the package compiler's module carried 14,350 pointer types; it
  now carries 1,695. Demand-driven constructor publication or a type
  inference pass would declare only the pointers addresses use; measure
  compile time and catalog size when doing so.
- Delayed continuations currently describe all parent local banks as captures.
  Those banks must stay alive until action completion, when references are
  materialized. More precise capture metadata could reduce retained memory;
  lexical scope exit alone cannot clear these banks safely.
- Native boundary validation scans boxed record arrays before each call. A
  self-compilation profile on this branch spent most samples validating array
  element headers, even after complete pointer-free packed layouts gained a
  constant-time path. An array-index operation only needs to expose its selected
  element, but native calls currently have no argument-access contract. A future
  explicit validator for canonical array indexing could check only that element;
  it must be carried through binding and ABI validation. Purity alone does not
  prove that a native callback cannot retain an argument, and a cache cannot
  assume external code leaves boxed element headers unchanged.
- Action dispatch currently validates native arguments in the backend and
  again in the public action helper, with no callback between them. A follow-up
  can remove the duplicate walk while preserving direct-helper validation and
  failure classification. This is separate from the measured array-index cost.

Decision (2026-10-22): pointer assignment shares a target, value assignment
copies a value, and unreachable cycles are collected automatically. Users do
not need `move`, a `shared` creation form, or weak links to break cycles.
Odin supplies the familiar `^T`, `&value` and `pointer^` spelling. Go's managed
pointers provide a lifetime precedent; Nim's ORC implementation was inspected
before choosing Chibi's existing tracing collector. These are semantic
references, not a claim that their implementations were ported.

## Flat typed queries and iterators

Language and runtime. Decision (owner, 2026-10-22): systems and explicit query
iteration share a flat typed binding list. `in` generates relationship rows;
`from` binds a component on an existing source. `T` is a read-only component
value and `^T` is managed reference access. Ordinary inferred intermediates
and `if` filters compose in source order. Systems receive `dt` implicitly in
the body; helpers receive it explicitly. `join`, `without`, runtime `where`
query clauses and system Tick payload headers are removed.

`query(...)` describes reusable iteration with captured outer values. Each
loop owns an independent cursor and performs cleanup on early exits. Systems
also own scheduling and independent continuations per root/target tuple.
Grouped source bindings are a future exploration, recorded in
[design notes](design-notes.md#decisions-owner-2026-10-22).

Project version 38 migrates legacy live-component query parameters to pointers,
flattens clauses and makes bare whole-component value/lvalue access explicit
with `name^`. Migration preserves lexical shadows, comments and literals.
Ambiguous optional whole-value reads and obsolete body query declarations
require an explicit author rewrite; the migration stages all sources before
replacing any. [Query reference](code-language.md#components-and-systems).

## Stage one rejects untyped locals

Stage one. Before #558 an untyped number literal was a `number`, so
`xs = array_add(xs, 0)` appended a double and `index := 0` declared one.
Integer code wrote `int(0)` and `index: int = 0` instead, and stage one
rejects untyped locals for this reason. `core:strings` declares two dozen counters as `i: int = 0;`
(#503). A floating value also narrowed to an integer silently:
`x: int = 0; x += 1.5` left 1.

- Go and Odin give an untyped constant its type from context and otherwise
  default by spelling: `1` is `int`, `1.0` is `float64`/`f64`.
- Rust infers a literal's type from its uses and defaults to `i32` or `f64`,
  but never turns `0` into a float.
- C, C++, C#, Jai, Swift and Python type a literal by its spelling: `1` is an
  integer and `1.0` a floating value.
- Zig keeps `comptime_int` until a use fixes the type; a runtime variable
  needs one.
- Go, Odin, Jai, Rust and C# convert a float to an integer only explicitly.

Decision (2026-10-20): a literal takes its type from context first, and
otherwise from its spelling: `0` is `int` and `0.0` is `number`. Converting a
floating value to an integer needs `int(x)`, which keeps today's truncating,
saturating conversion; integer to floating stays implicit. Behavior state such
as `elapsed := 0` followed by `elapsed += dt` then fails at the `+=` and
becomes `elapsed := 0.0`. Inferring the type from later uses, as Haskell does,
was rejected: an edit far from the declaration would silently switch `/`
between integer and floating division.

Implemented for stage zero in #558, with "Numeric literal has a fraction,
which int cannot hold" for `count * 0.5` and "convert with int(value)" for
a floating value stored in an integer. Stage one still rejects an untyped
local such as `i := 0`, though its type is now settled.

The stage-zero formatter widens the gap. `chibi_ctl --format` rewrites
behavior state `steps: int = 0` as `steps := 0`, which stage one rejects with
"state needs a number type". The Counter and Controls stories therefore keep
their declarations as written (#565). gofmt and rustfmt never remove a type
annotation.

Suggestion: stage one accepts untyped locals typed by spelling, then the
compiler and `core:strings` drop their `i: int = 0` declarations.

## Records hold only scalar fields

Language and runtime. Stage zero's records hold `int`, `bool`, `string`,
`byte`, `int64`, `uint64`, `float`, `double` and `number` fields, but no
arrays, unions or other records; since #567 stage one accepts the same
scalar fields. The compiler uses one-variant unions as
records with arrays (`Step`, `Decimal`) and keeps parallel arrays. The
prepared writer also pools 64-bit IDs in a `wide` array its records index,
from when stage one's records held only `int`, `bool` and `string` fields.
A map type's declaration in stage one keeps its value type in three fields,
`value_storage`, `value_container` and `value_record`, because `TypeDecl`
cannot hold a `ValueType`.
The build graph's `c.Library` (M6) cannot hold its sources or flags, so every
graph function takes them as two arrays beside the record, and a library's
description is three functions (`Support`, `SupportSources`, `SupportFlags`)
instead of one value.

- C, C++, Rust, Odin, Zig, C#, F#, TypeScript, Python and Ruby all let a
  struct, record or object hold any value type.

Suggestion: record fields of every value type, as Odin's structs.

## Ordinary writes make calls copy their arguments

Historical runtime issue. Before the managed-pointer and copy-on-write work,
stage zero passed arguments without copying only when its analysis proved the
function left the caller's storage alone. Two ordinary writes defeated that
proof:
- Raw pointers have the `uint64` slot type, so the analysis takes any
  `uint64` value for a pointer. Once a function stores a `uint64` parameter
  into a field of a local record, the next write to that record looks like a
  write through the caller's pointer.
- A function that assigns to an array parameter, as `code = array_add(code,
  x)` does, writes the caller's array.

Such a function, and every function that calls it, then copies every
argument on each call. One store in `ConstantCode` and one push in
`ComponentReferenceStep` each took stage one compiling itself from 25 to over
60 seconds. In the stage-one maps PR, `StatementStep` passed its `code`
to a map step that appended to it, which took the same compile from 28 to
117 seconds; the step now starts its own code. Building the record in one literal, and appending to a fresh
local array, keep the calls free. `tool language-package-compiler-chibi-sharing`
now fails when any function but those that run once per compile copies its
arguments.

The managed-pointer increment hit the same issue on 2026-10-22: its new
helpers caused 17 unexpected copying functions, and a process sample during
self-compilation showed repeated record-array/string snapshots. Fresh
`append`/`extend` results and local bindings for reassigned parameters restore
the existing sharing check without exemptions. The current collection model
keeps ordinary arrays and maps independent through copy-on-write. Union
construction copies each payload as a value; explicit pointers and views retain
their shared targets. Copying a collection descriptor need not copy its backing.
Failure-observation callbacks also disable argument sharing at runtime, even
when that static check passes. Keep the compiler fixture's diagnostic observer
opt-in when measuring or validating normal compiler execution.

- Odin, C, Zig and Rust type pointers apart from integers, and pass slices
  and arrays by reference or by an explicit copy, so neither their aliasing
  rules nor their optimizers take a `u64` for an address or a local append
  for a caller's write.

Suggestion: give raw pointers their own slot type, so the analysis tells them
from `uint64` values, and let a function that assigns to an array parameter
work on its own copy without making its callers copy.

## Nothing destructures results

Language. Both compilers compile named multiple results,
`Pair :: (x: int) -> (kind: int, end: int) { return(x, x + 1); }`, read as
`Pair(3).kind`; stage one compiles them since the Results story. Neither
compiler destructures results into locals or assigns several targets at
once.

- The compiler returns one-variant unions as tuples and destructures them with
  `switch`, which must bind every field.
- Odin's `partition` returns `(head, match, tail)` and `replace` returns
  `(output, was_allocation)`; the `core:strings` port returns a `Parts` record
  and drops `was_allocation`, and Odin's `tr, sr = sr, tr` takes a temporary
  (#503).
- The tokenizer's `Scan` returns a token's kind, end and error as a `Lexeme`
  record (#518).

Other languages:

- Odin returns several values, optionally named:
  `proc() -> (value: int, ok: bool)`, with `value, ok := f()`; Go's
  `func() (value int, ok bool)` is the same idea.
- Rust, F#, Python, C# and TypeScript return tuples and destructure them; Zig
  returns anonymous structs.
- Odin, Go and Python swap with `a, b = b, a`.

Suggestion: both compilers gain `a, b := F()` destructuring and
`a, b = b, a`.

## An integer vector times a float reads the wrong bits

Runtime. A numeric aggregate native combines a record with a scalar field by
field. Since #558 made `40` an `int`, `vec2(x, y) * 40` passed an int to a
floating record, which read its bits as a float until the self-hosting
thread's fix widened the integer (#557). The reverse still misreads: an
integer record such as an `ivec2` scaled by `0.5` reads the double's bits as
an int. Floating to integer is now explicit everywhere else.

- Odin, Rust, Zig and Go refuse `[2]i32 * 0.5` without a conversion; GLSL
  and HLSL convert the integer vector to a floating one.

Suggestion: stage zero rejects an integer aggregate with a floating scalar,
asking for a conversion of the aggregate or the scalar, as it does for
`int * 0.5`.

## Integer overflow wraps silently

Language. Integer arithmetic wraps without a sign: `int` 2147483647 + 1 is
negative, `byte(3) - byte(200)` is 59 and `uint` 0 - 1 is 4294967295. A
conversion that does not fit fails instead: `byte(300)` fails the call. Code
that wants wrapping masks by hand: `core:strings` keeps Odin's wrapping `u32`
Rabin-Karp hash in `int64`,
`h = (h * 16777619 + ByteAt(s, i) - pow * ByteAt(s, i - n)) & 0xFFFFFFFF`
(#503), and the tokenizer keeps its FNV-1a keyword hash in `uint64` masked to
32 bits (#518).

- Odin, Go, Jai and C define unsigned overflow to wrap.
- Swift traps on overflow and wraps with `&+`, `&-` and `&*`.
- Rust panics in debug builds and has `wrapping_mul` and `Wrapping<u32>`; Zig
  traps in safe builds and wraps with `+%`, `-%` and `*%`.
- C# wraps by default and checks in `checked` code, with `unchecked(expr)` to
  wrap inside it; Python and JavaScript mask (`& 0xFFFFFFFF`, `Math.imul`,
  `>>> 0`).

Decision (2026-10-20): overflow fails the call, as a checked conversion does,
on every backend. `T(wrap: expr)` makes the arithmetic written inside it wrap,
as C#'s `unchecked(expr)`: `hash = uint64(wrap: (hash ^ b) * Prime)`. Trapping
can be relaxed later without breaking code; wrapping could not be tightened
later without breaking code that relied on it.

Decision (2026-10-21): shifts reject a negative count or a count greater than
or equal to the operand width, including inside `T(wrap: expr)`. Shifted-out
bits are discarded. Implemented locally across the VM, SLJIT, generated C,
Wasm and typed constant folders. A regression also preserves earlier writes
when an unused shift fails in an optimized function. Default checked arithmetic
and lexical wrapping remain pending. See the [numeric contract](package-numeric-contract.md).

Decision (2026-10-24): no integer operation fails at run time, and zero answers
the cases that have no answer. This replaces both decisions above. Arithmetic
wraps; `x / 0` is 0 and `x % 0` is `x`; the signed minimum divided by -1 wraps
to the minimum, with remainder 0; a shift count at or past the width shifts
every bit out; `T(v)` clamps between integers and `T(wrap: v)` keeps the low
bits. Safety comes from compile-time rules (no mixed integer types, unsigned
shift counts, exact constants that must fit, no constant zero divisor) and
from development builds, which report each overflow, zero divisor and clamp
once per site without stopping. The runtime part and stage zero's
compile-time rules are implemented; stage one's compile-time rules and
development reports follow.

## Stage one has no character literals

Stage one. Stage zero has character literals and `char` since #530: `'a'`
takes an integer type from context, as in `c: byte = 'a'` or
`ByteAt(s, i) == '/'`, and is otherwise a `char`, a 32-bit Unicode scalar with
integer arithmetic. Stage one rejects them, and it compiles
the compiler, `core:strings` and the tokenizer, so those still compare bytes
by code: `48` for `'0'`, `46` for `'.'`, `101` for `'e'`; `core:strings` writes
`ByteAt(path, index) == 47` for `'/'` and tests `c >= 65 && c <= 90` (#503);
the tokenizer writes `c == 34` for `'"'` (#518).

Update (2026-10-27): stage one accepts nominal `char` parameters, ordinary and
named results, and locals. `char(integer)` checks Unicode scalar validity
before narrowing; a `char` widens to `int`, `u32`, `i64` or `u64`, and explicit
integer conversions extract its value. Exact decimal literals such as
`char(65.0)` take the same integer context as stage zero. Character literals,
char arithmetic, generic native results retaining char, char container
elements, fields, payloads, pointers, optionals, parameter defaults and typed
constants remain unsupported. Untyped
compound decimal expressions such as `char(64.0 + 1.0)` and projecting a named
char result after widening it to an integer also remain unsupported.

- C, C++, C#, Odin and Zig have `'0'`; Rust has `'0'` and `b'0'`; Python uses
  `ord("0")`.

Suggestion: stage one types character literals from context as stage zero
does, then those sources replace their codes.

## Strings have no indexing or slicing syntax

Historical gap. PR #633 merged length-carrying strings. The value-sequence
branch adds checked byte indexing, copied half-open ranges and explicit retained
views; see [value sequences](value-sequences.md) for implementation status.
The measurements below describe the earlier representation.

Language. Byte access goes through `strings.ByteAt`, `strings.ByteLength` and
`strings.Slice`; every byte read in `core:strings` is `ByteAt(s, i)` and every
slice `Slice(s, start, count)` (#503). Strings carry no length, so each
`ByteAt` measures its string: scanning 1 MiB byte by byte took 19.9 s in a
Release build on macOS arm64, against 0.07 s through `strings.Bytes`, which
reads every byte into an `int[]` once (2026-10-19).

- Odin and Go index bytes with `s[i]` and slice with `s[a:b]`; Rust slices a
  `str` by byte offsets with `&s[a..b]` and reads bytes through
  `s.as_bytes()[i]`; Python and C# index characters with `s[i]`.

Suggestion: `s[i]` as a byte and `s[a:b]` as a string, with bounds checks.
Constant time needs the representation in
[Strings are NUL-terminated and carry no length](#strings-are-nul-terminated-and-carry-no-length).

## Stage one's optionals stop at locals, results and parameters

Stage one. Stage zero has `T?`, `nil`, presence tests that narrow, `as T` and
`a ?? b`. Stage one's `ReadType` reads `?` and compiles `int? = nil`, `int? = 4`,
`return nil`, `return n` in an `int?` function, optional parameters, `==`/`!=`
`nil`, presence tests that narrow and `a ?? b`, for a scalar element. It does
not yet compile:

- assigning `nil` or a plain value to an optional local after its declaration
  (the store copies a union that has no cell);
- a plain value passed where an optional parameter is expected (only an
  optional, or `nil`, is accepted);
- a string, record or array element, an optional record field, or `as T`.

The compiler still marks absence with sentinels instead: `-1` for a missing
node, field or record, and `{storage: 0}` for no type.

- Odin has `Maybe(T)` with `or_else`; Zig `?T` with `orelse` and `if (x) |v|`;
  Swift `T?` with `??` and `if let`; Rust `Option<T>` with `unwrap_or`;
  C# and TypeScript `T?` with `??`.

Suggestion: finish the forms above, then the compiler replaces its `-1`
sentinels where a value may be absent.

## Stage one's maps stop at single values and local writes

Stage one. Since the stage-one maps PR, stage one compiles `[K: V]` locals,
parameters and results with a number, bool or string key and a number, bool,
string or record value. It also compiles `["k": v]` and `[:]` of a declared
type, `m[k]` with stage zero's zero-value fallback, `m[k] = v`, `delete m[k]`
and `m.delete(k)` on a local map, `k in m`, `k not in m`, `m.has(k)`, and
`for k, v in m`. It does not compile:

- an inferred map literal, such as `m := ["a": 1]` ("map literal needs a
  [K: V] type"); stage one declares map types before bodies, from the types a
  package spells;
- maps of arrays, unions or maps;
- `m[k] op= v`;
- a store or removal through a parameter, a record field or a state.
- `length(m)`, which reports "argument differs from its parameter type";
  `test/graph/SDKInstallTests.chibi` counts its manifest's records beside the
  map instead. Odin's `len`, Go's `len` and Rust's `len()` all take a map.

The compiler's own name lookups still scan parallel arrays, and the tokenizer
builds Odin's keyword table by hand from an `int[]` (#518).

- Odin has `map[K]V`; Go `map[K]V`; Rust `HashMap`; Zig `std.HashMap`;
  C# `Dictionary`; Python `dict`.

Suggestion: stage one declares a map type a literal infers when it meets one,
and compiles the remaining forms. Then the compiler's name lookups use maps,
once a regenerated bootstrap artifact can compile them. Dispatch on a byte or
an operator stays a `switch`, as in Odin, since a map would hash on every
lookup.

## The compiler counts with `while`

Stage one. Stage zero has `for i in first..last` with inclusive bounds, keyed
loops, and since #542 half-open
`first..<last` and reverse `for <` loops, as the owner decided on 2026-10-20
after Odin's `..<` and Jai's `for <`. Since #559 stage one compiles the
range, keyed and reverse forms too, and the compiler's
own counting loops and `core:strings`' scans are still
`while (index < n) { ...; index += 1; }`, where a `continue` before the step
hangs (#503). The authored tokenizer now recognizes `..<` with independently pinned byte
spans and inclusive/spaced controls, so its corpus can read those half-open
ranges too.

- Odin has `for i in 0..<n`, `for i in 0..=n`, `for x, i in xs` and
  `#reverse for`; Rust `for i in 0..n` and `(0..n).rev()`; Zig
  `for (0..n) |i|`; Jai inclusive `for i: 0..n-1` and `for <` to reverse;
  Python `for i in range(n)`; C, C++ and C# three-part `for`.

Suggestion: `core:strings` and the compiler replace their counting `while`
loops with range loops.

## Stage one has no constant arrays

Stage one. Since #564 stage one folds constant integer expressions, in
any order and across imports, but stage zero also folds constant arrays and
calls whose body runs at compile time, `while` loops included. A lookup
table is therefore a local array built on every call: `PowerStep` in
`Number.chibi`, and the tokenizer's byte-class, keyword and terminator tables
in `Tokenize` (#518).

- Odin and Go fold constant expressions; Odin also has constant arrays. Zig
  evaluates `comptime` code, C++ `constexpr` and Rust `const` items.

Stage one also rejects a string constant: `Contained :: "sdk: ..."` in
`test/graph/SDKInstallTests.chibi` failed with "constant value must be an
integer expression", so the diagnostic it matches and the manifest's name
became functions that return the text. Odin, Go, Zig and Rust all have
string constants.

Suggestion: stage one folds constant arrays such as `Steps :: [1, 3, 6]`
and string constants, then calls stage zero runs at compile time.

## From the core:strings port

Porting Odin's `core:strings` to `packages/core/strings` (#503) hit these.
Its examples of needs covered above are listed with those entries.

### Strings are NUL-terminated and carry no length

Historical gap, resolved by merged PR #633 for representation and byte access.
The value-sequence branch adds retained immutable backing, copied ranges and
explicit views; combined builds and runtime coverage are recorded separately in
[value sequences](value-sequences.md). The measurements below predate #633.

Runtime. In `core:strings`, every `ByteAt` runs `strlen`, so each byte loop is
quadratic, and every `Slice` copies, so Odin's `s = s[i+n:]` inside `Count`,
`_split` and `replace` copies the rest of the string on each match. A string
cannot hold a NUL byte, so Odin's `trim_null` has no use. On 2026-10-20 the
package compiler's debugger occurrences counted whitespace with `ByteAt` from
each document's start; compiling the compiler, whose largest file is 165 KB,
`strlen` under `ByteAt` took about a fifth of a profile, and reading
`strings.Bytes` once per document cut stage two's compile of itself from about
18 to 14 s.

Slicing runs `strlen` over the whole source string too, so the compiler's
`Text` of a node costs time in proportion to its document's length, and
compiling a file grows quadratically with its size. Appending the map steps
(about 450 lines) to `Steps.chibi`, already 3,000 lines, took stage one
compiling itself from 28 to 41 seconds; the same steps in their own
`Maps.chibi` took 30.

- Odin, Go, Zig and Jai strings are a pointer and a length; `len(s)` and
  `s[i]` are O(1) and `s[a:b]` is a view. Rust's `&str` and C++'s
  `std::string_view` are the same pair; C# strings carry their length.

Suggestion: strings that carry their length and may hold any byte, with
slices that share the bytes once ownership allows it.

Decision (2026-10-21): strings are immutable, carry their byte length and may
hold arbitrary bytes, including NUL. UTF-8 validation and decoding are explicit
operations; validity is not a global string invariant. Storage, lifetime and
boundary migration are tracked in [package foundations](package-foundations.md).
The representation cutover is merged; retained backing and the explicit
copy/view surface are implemented in the unpublished value-sequence candidate.
Its source-build evidence and remaining validation are recorded in
[value sequences](value-sequences.md).

### Stage one does not check pub

Stage one. Before #552 every top-level declaration was exported, so Odin's
private `_split` became the exported `SplitKeeping`, and the shared line
splitter the exported `SplitLinesKeeping`.

- Odin has `@(private)` and `@(private="file")`; Jai `#scope_file` and
  `#scope_module`; Rust, Zig and F# export only `pub` or public items; Go
  exports capitalized names; C has `static`; C# `internal` and `private`;
  TypeScript exports explicitly; Python marks private names with `_`.

Decision (2026-10-20): a declaration is private to its package, every file of
it, unless `pub` exports it, as in Rust and Zig: `pub Join :: (...)`. Game
projects' `library` blocks follow once they become packages.

Implemented for stage zero in #552: it exports only `pub` declarations and
names a private one an import reaches, and `core:strings` keeps its two
splitters to itself. Stage one originally parsed `pub` without restricting
imported access.

Implemented locally on 2026-10-20 in `codex/odin-package-foundations-20261020`:
stage one rejects imported private functions, constants and named types while
allowing helpers across files of the same package. Focused imports checks pass
on bytecode and SLJIT. This is an unmerged change. Prepared modules retain private
callable entries, as stage zero does; source visibility does not restrict an
explicit host invocation. Private union access gets a clearer private-name
diagnostic than stage zero's current unknown-callable/value diagnostic.

### No way to build a string

Language. Concatenation is `format_text("{}{}", a, b)`, so `Join`, `Repeat`,
`Replace` and `ToLower` copy everything built so far on each append. Neither
`"a" + "b"` nor `append(s, t)` compiles, and no conversion makes a string from
a byte: `ToLower` slices its letter out of `"abcdefghijklmnopqrstuvwxyz"`.

- Odin has `strings.Builder` and `string(bytes)`; Go `strings.Builder` and
  `string(b)`; Rust `String::push_str` and `String::from_utf8`; Zig
  `ArrayList(u8)`; C# `StringBuilder`; Python `"".join`; Ruby a mutable
  `String#<<`.

Suggestion: `string(bytes)` and `byte[](s)` conversions, and an in-place
`s = append(s, t)` on a local string, as arrays already update in place.

### Stage one cannot compile assert

Stage one. Since #534, stage zero has a pure
`assert(condition, message)` in place of `expect`: it fails the call as a
checked conversion does, keeps its caller pure, and fails compilation when it
fails while the compiler evaluates a constant. Stage one does not compile it,
so `core:strings`, which stage one compiles, still returns an empty string
where Odin's `repeat` panics on a negative count, hiding the caller's mistake.

Suggestion: stage one lowers `assert` to the same `ASSERT` instruction, then
`Repeat` asserts its count.

Update (2026-10-20): stage one now lowers `assert` to stage zero's `ASSERT`
wherever the target links the core pack, as a game does. The compiler host
links no core pack, so neither stage compiles `assert` in packages the host
compiles. `Repeat` can't assert its count until the host links it.

### No runes

Language. `core:strings` works on bytes: `Cut` takes byte offsets,
`EqualFold`, `ToLower`, `ToUpper` and the space trims are ASCII, and
`Split(s, "")` splits into bytes. Odin's `rune_count`, `index_rune`,
`reverse`, `expand_tabs`, the justify procedures and `levenshtein_distance`
are not ported. `for c in s` already visits each Unicode scalar, but as a
one-scalar string.

- Odin has a `rune` type, `for r, i in s` that decodes UTF-8 and
  `core:unicode/utf8`; Go ranges over a string's runes; Rust has `char` and
  `s.chars()`; C# `Rune`; Python and Ruby strings are code points.

Suggestion: once `char` exists (see [No character literals](#no-character-literals)),
`for c, i in s` yields a `char` and its byte offset, as Odin's decoding loop,
then a `core:utf8` package written in Chibi.

### Passing functions into synchronous algorithms

Language. Odin builds `trim_space` and `trim` on `trim_left_proc`,
`index_proc` and `fields_proc`, which take a procedure. `core:strings` repeats
each loop instead and leaves the `_proc` API out.

- Odin passes non-capturing `proc` values; C function pointers; Zig function
  types and `comptime` parameters; Rust `fn` pointers and closures; C#, F#,
  TypeScript, Python and Ruby lambdas.

Decision (owner, 2026-10-27): first export generic package functions, then accept
named functions and capture-free inline functions as synchronous, non-escaping
arguments. The local implementation is under validation; this entry does not
claim a merged SDK or both compiler stages passing.

Use `predicate: fn(value: T) -> bool` to state the required signature. A caller
can pass a named function or `(value: int) => value > 0` directly as an argument.
Existing named declarations express reusable callbacks, but neither inference
nor a named declaration describes a callable parameter's input/result contract.
That is why this stage adds a callable type and inline argument form. It does
not add stored callback values or a capture lifetime API.

The compiler resolves the callback during specialization and emits direct
calls. It still propagates the callback's effects. Passing functions therefore
does not promise purity, full inlining, or allocation-free sorting. Initial
inline functions cannot capture runtime locals; callable parameters cannot be
returned, stored, queued or passed to an unknown host. Later capture support
must preserve the already selected shared mutable bindings. See
[passing functions](code-language.md#passing-functions) for the current scope.

### Stage one's scoped pointer parameters reach locals and fields only

Stage one. Odin's `split_iterator(s: ^string, sep)`, `split_lines_iterator`
and `fields_iterator` advance the caller's string. Version 37 replaced `ref`
parameters with `^T` pointers: a `^T` parameter whose pointer never outlives
the call is scoped, and both compiler stages lend it the caller's location
through the reference ABI. A callee reads, assigns and compound-assigns
`p^`, reads and writes `p.field`, and passes `p` on. A caller passes `&x` for
a local or parameter, `&x.field` of a local record, a managed pointer, or
its own scoped parameter. A number, bool or string whose address a scoped
argument takes lives in a data cell, as stage zero keeps every local. Stage
one still refuses:

- a pointer to an array element or a map entry;
- `v[i] = x` and `m[k] = x` through a scoped parameter;
- a scoped parameter with a default.

`core:strings`, which stage one compiles, has no iterators yet.

- Odin `^T`, Rust `&mut T`, C# `ref`, Zig `*T` and C pointers; Python and
  Ruby use generators.

Calls mark the location they lend: `Nudge(&v)` may write `v`, as in Swift,
Odin and Zig. Whether the parameter is scoped or managed is inferred, so the
caller writes the same `&v` either way.

Authoring refuses to inline a callable with scoped pointer parameters. Its
expansion would declare a local alias initialized from the argument's value,
but local declaration lowering does not create a reference to that argument.
Supporting local reference aliases is a prerequisite for preserving caller
mutation when inlining these calls; the original call remains intact.

Suggestion: `core:strings` gains Odin's iterators, then stage one compiles
references to elements and entries. Keep ownership transfers automatic:
ordinary library users should never need a `move` keyword. Resource-handle
sharing and cleanup still need their own API decisions.

### Library growth increases compiler-test execution time

Toolchain. At #503 the 16 KB package prepared to an 837 KB module, about 52
times its source, mostly per-function debug metadata. Harnesses embed the
shipped `core:strings`: writing it with the authored writer on bytecode under
ASan took 22 s, and stage one compiling it in the package compiler's imports
sample added about 24 s of CPU. The writer now writes modules above 512 KiB on
the compiled backend only, and the imports sample has its own selector, but
each further core package adds to compiler-test execution time.

- Odin, Go, Rust and Zig keep debug information beside the code (DWARF, PDB)
  rather than in it, and release builds strip it.

Suggestion: keep debug metadata out of the prepared form the writer and the
compiler tests process, or make it optional, and give harnesses that only test
imports a fixed core fixture.

### A small change can exceed a function's verifier storage budget

Toolchain and runtime. During the package-visibility prerequisite on 2026-10-20,
adding the private-member check to the compiler's large `EmitBody` function made
stage-zero preparation fail with "definition exceeds verifier storage budget".
The focused imports check failed during preparation in 1.537 seconds, before
running package cases. Moving the check to the existing `ValueStep` and
`CallStep` functions kept the change within the existing limit.

`src/execution/function_builder.c` bounds its defined-operands matrix:
instruction count times `(slot count / 64 + 1)` 64-bit words. A source edit can
increase either dimension, so source line count does not predict the limit.
The diagnostic reports neither dimension nor the estimated storage. On
2026-10-24 the query and pointer work grew `EmitBody` to 36,186 instructions
and 15,491 slots, a 70.4 MB matrix, so the bound rose from 64 MiB to 256 MiB.
A decoded module also counts this matrix in its preparation reservation, which
now reserves only the largest function's verification scratch, since functions
are verified one at a time.

- The [JVM Code attribute](https://docs.oracle.com/javase/specs/jvms/se25/html/jvms-4.html#jvms-4.7.3)
  has an explicit per-method limit of fewer than 65,536 code bytes. Chibi's
  constraint instead depends on verification work, which needs its own
  explanation and measurements.

Suggestion: include the function, instruction count, slot count, required bytes
and allowed bytes in the diagnostic. Investigate a less dense verifier state
representation if measured compiler functions keep reaching the limit. Keep
the verification bound and split `EmitBody` into small owning functions: the
matrix grows with the product of its instructions and slots.

## Calls nest at most 64 deep

Runtime. Every authored function shares a call-depth budget of 64 (#420).
Stage-one twins that walked a graph recursively, the union-members walk and
the effect analysis query restrictions, failed on deep inputs and became loops
over an explicit stack.

- Odin, C, C++, Rust and Zig recurse until the native stack runs out, often
  megabytes deep; Python's default limit is 1,000 frames; C# and F# grow to
  the thread's stack, and F# turns tail calls into jumps.

Suggestion: a deeper budget, counted in frame bytes rather than calls, or
guaranteed tail calls as F# has, so a recursive walk needs no hand-made stack.
Native code has no limit (decision D4): it recurses as deep as the stack
allows, as Rust and Odin do.

## Stage one writes no call depth limit

Compiler. Stage zero gives every function the 64-call depth limit above, but
stage one writes 0, no limit, in each prepared function's header
(`Compile.chibi` and `bounded_compiler/Prepare.chibi`). On the VMs a function
of stage one's output that only calls itself grows memory until the process
is killed; the instruction budget does not stop it. Native code has no limit
either (decision D4), but it probes its stack, so an overflow always faults
in the guard page; a host that installs `stack_overflow_init` reports it by
name and aborts.

- Odin and C have no limit and overflow the stack; Rust probes its stack and
  aborts with "has overflowed its stack"; Python stops at 1,000 frames with an
  exception.

Suggestion: write the same limit from stage one. The compiler package itself
runs on stage one's output, so check first that it stays within the limit,
or raise the limit as the entry above suggests.

## format_text drops arguments past twelve

Runtime. `format_text` renders at most 12 arguments
(`FMT_TEXT_MAX_ARGS` in `src/runtime/text_format.c`). A thirteenth
placeholder renders as empty text with no error from the compiler or the
runtime. M2's native integer work hit this in
`tools/compiler/llvm_native/Emit.chibi`: a 13-argument instruction template
lost an operand, so the emitter splits such templates into several calls.

Suggestion: check literal format strings against their arguments and report
excess placeholders. This is a documented limitation, not a formatter change
included in the M2 reconciliation.

## Templates are package-local

Language. A template with `$T` parameters cannot be called from another
package, so a generic helper is copied into each package that needs it
(#420). A `library` block's templates already specialize for qualified
callers.

- Odin's parametric procedures, Zig's `comptime` functions, Rust generics and
  C++ templates all export from their package, module or header.

Suggestion: export templates, instantiating each at the caller with the
callee package's declarations in scope, as Odin does. Until then `pub` does
not export a template either (#552).

## Constant calls don't infer template type bindings

Language. Constant evaluation does not infer the `$T` binding in a template
parameter such as `value: $T`. In a package declaring the language guide's
`Identity :: (value: $T) -> T { return value }`, `One :: Identity(1)` fails
with "Unknown type 'T'", reported at `$T` rather than at the call. A function
body calling `Identity(1)` specializes (#528).

Templates with concrete parameter types can run at compile time. For example,
`Times :: (value: int, $N: int) -> int { return value * N }` lets
`One :: Times(2, 3)` bind both integer arguments before evaluating the body.
Type-returning templates also support compile-time evaluation.

- Zig calls generic functions from `comptime` code and constant initializers;
  Jai's `#run`, D's compile-time function evaluation and C++ `constexpr`
  function templates run templates too. Odin has no compile-time calls.

Suggestion: infer type bindings for a template called from a constant as a
runtime call does, then evaluate the specialization. Until then, diagnose
the call that needs an inferred template type binding, rather than reporting
an unknown type in its declaration.

## A pattern test fails as a call argument

Language. `found := r is .Found` and `if Find(n) is .Found { ... }` compile,
but `assert(r is .Found, "...")` reports "Unknown type '.Found'".

- Rust's `matches!(r, R::Found(_))`, C#'s `r is Found` and Odin's
  `_, ok := r.(Found)` are expressions usable anywhere a value is.

Suggestion: an `is` pattern is a `bool` expression wherever a value may
appear.

## Build programs cannot format numbers

Toolchain. The build host offers `build.join`, but neither `format_text` nor
any number conversion. `test/tools/GameEquivalence.chibi` therefore reports
the first differing frame by slicing its number out of the trace line, and it
cannot report how many lines it compared.

- Odin has `fmt.tprintf("%d", n)` and `strconv.itoa`; Go `strconv.Itoa`; Rust
  `n.to_string()` and `format!`; Zig `std.fmt.allocPrint`; C `snprintf`; C#
  `n.ToString()` and interpolation; Python f-strings; Ruby `n.to_s`.

Suggestion: one text library for every Chibi host, with `format_text` and
`core:strings` in build programs as in games.

Implemented (#606): build programs have the text and array natives, so
`format_text` and `length`, and import `core:strings` when the host is given
the core collection. `GameEquivalence.chibi` still slices numbers out of
trace lines until it moves to them.

## Build programs read files only as text

Toolchain. `build.read_file` returns a NUL-terminated string, so a build program
cannot read a binary file such as a `.chibip` artifact. Locating an artifact's
module records for the equivalence harness took a C function,
`player_prepared_modules`, although the stage-one compiler already writes
prepared modules in Chibi.

- Odin `os.read_entire_file` and Go `os.ReadFile` return `[]byte`; Rust
  `fs::read` returns `Vec<u8>`; Zig `readFileAlloc` returns `[]u8`; Python
  `open(path, "rb").read()` returns `bytes`.

Suggestion: `build.read_bytes(path) -> byte[]` for build programs, so tools can
read prepared formats in Chibi.

## Build programs have their own string vocabulary

Toolchain. `tools/toolchain/Build.chibi` cannot import `core:strings` or any
other package. It has only the build host's natives (`build.before`,
`build.after`, `build.starts_with`, `build.trim`, `build.lines`, `build.join`),
with no length, index, contains or split. Moving `configure_inputs.py
package-enabled` into it meant writing `Contains` as
`build.before(text, part) != text` and parsing `NAME[:TYPE]=VALUE` with nested
before/after calls. `tools/toolchain/StageOne.chibi` first escaped sources as
JSON strings with a before/after loop per special character, but build values
are never freed, so each replacement copied the whole text and an 800-line
source exceeded the 32 MB value limit; `chibi-compile` now writes the JSON.
The M6 graph met the same limit: an up-to-date build of 362 sources reads 6 MB
of compile records, and splitting each `stamp path` line with `build.before`
and `build.after` exceeded 32 MB, so records now keep the path and the stamp
on separate lines, and the host allows 256 MB of arena blocks (about 128 MB of
values), since the next slices about double the sources. Freeing values a program no longer reaches would remove
the ceiling.

- Zig's `build.zig` is ordinary Zig: the whole standard library
  (`std.mem.indexOf`, `std.StringHashMap`) plus the `std.Build` API.
- Cargo's `build.rs` is an ordinary Rust program with `std`
  (`str::split_once`, `HashMap`).
- Go's `go generate` runs ordinary Go programs. Odin has no build scripts;
  its tools are ordinary programs that import `core:strings`. Jai's build is
  a metaprogram written in the full language with its modules.
- MSBuild tasks are ordinary C#, and Rake tasks are ordinary Ruby.

Suggestion: run build programs as ordinary Chibi packages that import the
`core:` collections, with process and file access in one build-only
capability package, instead of a separate string vocabulary.

Partly implemented (#606): a build program is now a package and may
import `core:`. The `build.` string natives remain until `Build.chibi`,
`StageOne.chibi` and `GameEquivalence.chibi` use `core:strings` instead.
That waits on cheaper strings
([Strings are NUL-terminated and carry no length](#strings-are-nul-terminated-and-carry-no-length)).
Splitting the 25 KB `build/CMakeCache.txt` into lines and testing each takes
0.9 s with `strings.SplitLines` in a Debug host with sanitizers, against 0.2 s
with `build.lines`. A 50 KB cache ran out of the host's then 100,000-step budget,
because each split copies the rest of the text.
The M6 graph's `compile_commands.json` writer escaped every argument with two
`strings.ReplaceAll` calls: with 218 commands of about forty arguments the run
took 7 s, and 2.4 s once it asks `build.before` whether the text holds a quote
or backslash first and escapes only the rare argument that does. Rewriting
Cute's 96 KB of codec headers with three `strings.ReplaceAll` calls took 11 s,
so the build host gained `build.replace`.

## A host's natives can't form a package

Language and toolchain. Moving the build host's natives under `build.` (#523)
named each one `build.read_file` in the host's catalog, the way an imported
package's functions are named `strings.Join`. That is all a host can do: it
cannot declare a package that programs import. So a build program uses `build.`
without an import, and a local named `build` doesn't shadow it; `Build.chibi`
renamed its build-directory parameters to `buildDir` to stay readable. Catalog
lookups ignore case and underscores, so `Build.ReadFile` and `build.readfile`
resolve too. Natives are snake_case (`array_add`) and package functions
PascalCase, so the same operation is `build.join` beside `strings.Join`.

- Odin's `core:os` and Go's `os` are packages a program imports. Odin
  declares foreign procedures inside the package, Go declares body-less
  functions implemented in assembly, and a local named `os` shadows the import
  in both.
- A `build.zig` receives the build API as a `b: *std.Build` parameter; Cargo's
  `build.rs` imports `std::fs` and `std::process` with `use`.
- Odin, Go, Zig, Rust, C#, TypeScript and Python look names up exactly,
  case included.

Suggestion: let a host register its natives as a package (`host:build`) that
programs import like `core:strings`, through the same import namespace so
locals shadow it; look catalog names up exactly; and use one case convention
for package members.

Clock-first foundation: ordinary source-backed `host:` packages can wrap selected canonical kernels,
with exact private kernel lookup in the owning package and normal lexical
imports for callers. Registration and runtime grants are separate. Both compiler
stages preserve the kernel identity, ABI and effects; no prepared-format change
is needed. The public clock API awaits result parity owned by the results
workstream. Native-only declarations and conversion of `build.*` remain open.
See the [host-package contract](host-packages.md).

## Ordinary nouns are reserved, and the build host doesn't say where

Language and toolchain. A local named `entry` in `Build.chibi` failed with
only "Unexpected syntax". `entry` is a keyword, as are `exit`, `query` and
`where`, and the build host's diagnostic names no file, line, column or
token. Finding it took bisecting the file. #420 already made `join`
contextual and dropped `domain`. A local named `prototype` in the tokenizer
failed the same way, since #420 made `prototype` a reserved word; the flag
became `prototyping` (#518). The prepared writer's tables of events and
states could not be named `events`, `event` or `state`, nor a loop variable
`state`; they became `event_declarations`, `event_id` and `retained`.
A map literal step's loop variable named `entry` failed the same way, as the
whole file's "Unexpected syntax" at 1:1; it became `pair`.
The build graph's `c` package (M6) named a parameter `library`, the legacy
declaration keyword; four functions failed with only "Unexpected syntax"
until bisecting found it, and it became `target`. Its next slice hit it twice
more, as a record field and as a local, which became `owner` and `built`.
The build identity's version reader named a parameter `macro`; the graph
failed with only "Unexpected syntax", and `chibi fmt --check` only said the
file had syntax errors, until bisecting copies of the file found it; it
became `define` (#650).

The build graph's ported tests named a parameter and a local `entry` twice
more (`test/graph/DiscoveryTests.chibi`, `CTests.chibi`); the build host now
says where ("Expected a name, found `entry`"), so it took a rename, not a
bisection.

The 2026-10-22 flat-query change removes the `join`, `without` and query
`where` clauses and their keyword reservations. The other examples above
remain separate parser and diagnostic concerns.

- Rust, Go, Odin, Zig, C#, TypeScript and Python report file, line and
  column; Rust and Python also name the token (Rust: "expected identifier,
  found keyword").
- C# keeps `where`, `join` and `select` contextual: they are keywords only
  inside a query expression. TypeScript does the same with `type` and `from`.
- Go's 25 keywords include none of these. Odin's include only `where`.

Suggestion: make declaration-only words contextual, and give build-host
syntax errors the parser's position and token.

## A struct literal takes its type only from a declaration

Language. In the build graph's `c` package, `array_add(running, {source: s,
handle: h})` fails with "Struct literal needs an explicit record or reflected
type", although `running` is a `Running[]`. The literal needs a typed local
first: `compile: Running = {source: s, handle: h};`.

- Odin infers a compound literal's type from its parameter or element type;
  Zig's `.{ ... }` coerces to the parameter's type; C# 9 has target-typed
  `new()`; Swift infers enum cases and literals from context.

Suggestion: type a struct literal from the parameter, element or return type it
fills, as Odin does.

## Stage one cannot negate a float

Language. Stage one rejects `-x` for a `number` or `f64` with "operator does
not apply to this operand", although the runtime's `BF_NEG` negates floats
and both VMs implement it. The native scalar path (M2) rejects it too. `0.0 - x` is not a
substitute: it gives +0 for x = +0 where negation gives -0, and IEEE leaves
the sign of NaN open for subtraction but defines it for negation.

- Odin, C, Go, Rust and Zig all negate floats; LLVM spells it `fneg`, which
  only flips the sign bit and is exact for every value, NaN included.

Suggestion: lower floating `-x` to `BF_NEG` in stage one and to `fneg`
natively. Floating `%` stays rejected, as Odin and Go reject it; `math.mod`
or an explicit `fmod`-style helper covers it.

## There is no compound shift assignment

Language. Every other binary integer operator has a compound assignment, but
`x <<= n` and `x >>= n` are unexpected syntax in both stages, so a shift is
written `x = x << n`. Both stages already type a compound assignment through
the binary operator's step, and a shift's count rules would carry over.

- Odin, C, Go, Rust and Zig all have `<<=` and `>>=`.

Suggestion: add `<<=` and `>>=` to the grammar and both stages' compound
operator tables; the scalar producer then reads them as it reads `+=`.

## An untyped array element is `int` to stage zero and `double` to stage one

Language. `storages := [6, 1, 2];` is an `int[]` to stage zero but a
`double[]` to the self-hosted compiler, which then rejects indexing with "an
untyped number is a double; give the array an integer type". The selectors
compile the scalar packages with stage zero, so only `Run.ps1` in
`compiler-recovery.yml` caught it on #700.

- Odin and Go type `[]int{6, 1, 2}` from the element type, and default an
  untyped constant by spelling: `6` is an integer.

Suggestion: stage one types an untyped integer literal in an array by its
spelling, as #558 decided for locals, so both compilers agree.

## A build host reads `Build.chibi` with the syntax it was built with

Toolchain. `chibi build` runs an existing `build/sdk/chibi-build-host`, which
parses `tools/toolchain/Build.chibi` with the compiler it was built from. When
the map syntax changed (`[string: string] = [:]` for `Map<string, string> = {}`),
a host built before the change failed the whole build with only "Unexpected
syntax". So `Build.chibi` keeps its overrides in parallel arrays, and a build
program can use new syntax only after every host that will run it has been
rebuilt. Nothing rebuilds the host when its compiler sources change.

- Go builds its toolchain with an older release (Go 1.22 needs 1.20), and
  `cmd/dist` keeps to what that release accepts.
- Rust's bootstrap compiles the standard library with the previous beta and
  marks code for either compiler with `cfg(bootstrap)`.
- Zig's `build.zig.zon` names a `minimum_zig_version`, and an older `zig`
  reports it rather than a parse error.

Suggestion: rebuild the host through stage zero when its compiler sources are
newer than it, and let a build program declare the language version it needs,
as Zig does, so an old host names the problem.

## From the tokenizer port

Porting Odin's `core:odin/tokenizer` to `tools/compiler/source_tokenizer`
(#518) hit these. Its examples of needs covered above are listed with those
entries.

### No enumerations

Language. The tokenizer's 114 token kinds are numbered constants,
`Ident: int : 3;` through `Relationship: int : 113;`, and `KindNames` returns
their spellings in the same order by hand. A set of kinds, such as those that
can end a statement, is a `bool[]` filled from a list.

- Odin has `enum` with implicit values, enumerated arrays such as
  `[Token_Kind]string`, `bit_set[Flag]` and `fmt`'s names for values; Zig
  `enum` with `@tagName`; Rust enums with discriminants; C enums, often with
  X-macros for the names; C# `Enum.GetName`; TypeScript numeric enums with
  reverse mapping; Python `enum.Enum`.

Suggestion: Odin's `enum`, enumerated arrays and `bit_set`, with a name for
each value.

### A switch arm takes one value

Language. Since #561 both compilers switch on an integer,
`switch c { case 61: ... }`, but an arm takes exactly one value, so the
tokenizer's operator table stays a chain of `if (c == 61) { return Eq; }`
and a letter test is `c >= 97 && c <= 122`.

- Odin switches on any integer or rune with lists and ranges,
  `case 'a'..='z', '_':`; C and C++ stack `case` labels; Rust `match` with
  `b'a'..=b'z'`; Zig `switch` with `'a'...'z'`; C# relational patterns,
  `>= 'a' and <= 'z'`.

Suggestion: arms take lists and ranges, `case 'a'..'z', '_':`, as in Odin,
then the tokenizer switches on its bytes.

### Stage one does not check that functions return

Stage one. Since #541, stage zero rejects a function with results that
can fall off its end, with "Missing return", and counts a `while (true)`
without a `break` as an end, as Go and Odin do; the tokenizer's loop no longer
needs to break out to a return after it (#518). Stage one compiles only what
stage zero has checked, but on its own it would accept such a function.
The scalar proof's producer applies the rule itself for its subset (M3), so
native code never sees a function that can fall off its end.

Suggestion: stage one applies the same terminating-statement rule, and the
scalar producer's `ScalarEnds` gives way to it.

### Stage one's element stores into call results copy arguments

Stage one. A function whose local array holds an authored call's result and
then takes an element store (`rest := Shift(rest); rest[0] = 1`) is one the
sharing analysis cannot prove safe in stage one's build, though stage zero's
build of the same source shares. The function then copies every argument on
each call, and so does every caller. In M2 the exact division in `Number.chibi`
had this shape, and stage two compiled itself in 775 s instead of 37 s. The
division now adds the bit instead of storing it, and the self-compile check
fails when any function of stage one's build of the compiler copies.

Suggestion: stage one's element store keeps the local's array as stage zero's
does, so the analysis sees that it owns it.

### Calls and record snapshots are costly

Runtime. Profiling the tokenizer showed record validation at the top: storing
a record in a local snapshots it, and each snapshot checks the record by
rehashing its type's layout (`pin_record_valid`, `type_layout_hash`); each
call also prepares an activation. Inlining the hot loops, reading byte classes
from a table and returning records only for rare tokens took the corpus run
from 28 to 4 seconds on SLJIT in a sanitizer build. The bytecode backend is
about eight times slower still. On 2026-10-20 a profile of stage one compiling
the compiler put over half of its time in `pin_record_valid` and
`type_layout_hash`, mostly under value snapshots. A per-thread memo of
validated descriptors, keyed by address and a checksum of their bytes, answered
94% of the bootstrap's 63 million checks, yet saved no time on Release or
Debug: checksumming a descriptor costs about as much as validating it. A hit
that costs O(1) would have to trust a stamp of the header alone and stop
catching a field changed in place. So copies now trust the descriptor: a
record is validated where it enters (creation, prepared loading, value
documents), and copies check only that rows share their array's layout and
that nested catalog types are still plain. Stage one then compiled the
compiler in 7.6 s instead of 23.7 s on Release SLJIT. Deep copies of the rows
and strings passed between passes are the largest cost left: about a third of
stage one's samples are snapshots its code makes to retain a local or field,
and union constructors copying every payload array, even when the source is
a temporary that is never read again.

- Odin, C, Zig and Rust return small structs in registers or a result
  location and inline small procedures; C# returns structs by value; none
  hashes a type's layout at run time.
- Swift's arrays are copy-on-write, so passing one shares its storage until a
  write; Rust moves owned values and borrows the rest; Odin and Zig pass large
  structs by pointer.

Suggestion: an implementation change rather than a language one: hash each
record type's layout once (done), and inline small pure functions. For the
copies, a read-only borrow for parameters or copy-on-write arrays.

## Game projects use library blocks, not packages

Language. Tools and `core:` are Odin-style packages (`package strings`,
`import strings "core:strings"`), but game projects declare
`MovementActions :: library { ... }` blocks that the project catalog resolves.
Until project version 28 their functions read `entry.direction`, and until
version 29 they wrapped their bodies in `after entry`. Visibility, imports and
templates therefore work differently in a game than in a package.

- Odin, Go and Zig have one unit, the package or file, for libraries and
  programs alike; Rust has crates and modules for both.

Suggestion: a game's libraries become packages, now that functions no longer
use `entry`, so `pub` and imports mean the same everywhere.

## Stage one has no record field defaults

Stage one. A struct literal names every field, so a new field is spelled in
every literal of its record. Adding `written` to `Host`, which only a
component reference's make sets, added `written: false` to eleven literals in
`Steps.chibi`. Stage zero already fills an omitted field from its declared
default and reports `Missing required record field` for one without.

- Zig gives fields default values (`written: bool = false`) that literals may
  omit; Odin, Go and C zero every field a literal omits; Swift's memberwise
  initializers take declared defaults; Rust names every field or spreads
  `..Default::default()`.

Suggestion: stage one compiles stage zero's constant field defaults, as Zig
declares them, so `Host` declares `written: bool = false` and only the make
names it.

## The bootstrap artifact pins every column of the host's natives

Toolchain. The bootstrap artifact records the six target values it was
compiled for, and recovery compares them, as JSON, with the host's. A
descriptive field added to each native row, such as the operator a native
implements, so differs from every artifact and forces a rewrite of the 7 MB
closure, though the closure links natives only by identity and ABI. Motion's
operators went into the behavior input instead.

- Zig's `zig1.wasm` targets WASI and is rewritten only when the language
  changes; Go's bootstrap needs only an older Go release.

Suggestion: recovery compares what the closure links: the value layout,
prepared ABI, instruction names, and each native's identity, ABI and ports.

## An old Python breaks inherited host deadlines on macOS

Toolchain. During the 2026-10-21 package-foundation checks, the system Python
3.9.6 reported a process-relative `time.monotonic()` value near zero. Native
hosts use `CLOCK_UPTIME_RAW`, whose value was over one million seconds. Passing
the supervisor's absolute deadline to the build host therefore failed before
source loading with `build source could not be read`.

The installed `/opt/homebrew/bin/python3.12` used the same clock epoch as the
native host. Selecting its `libexec/bin` directory in the task's PATH allowed
the documented bootstrap workflow to finish. Emscripten also needed
`EMSDK_PYTHON` set to that interpreter. No global interpreter settings were changed.

Suggestion: toolchain startup should validate the supported Python clock
contract and diagnose an expired inherited deadline separately from a source
read failure. The current workaround is explicit interpreter selection.

## A string literal cannot span lines

Language. A string literal ends at its line, and nothing marks one as raw, so
text with its own line structure becomes an array of quoted lines with `\n`
and escaped quotes. The documentation site's stylesheet in
`tools/docs/site/Site.chibi` is 35 such lines, and `Head` escapes every
attribute quote of its HTML (#535).

- Odin and Go have raw strings in backticks that may span lines; Rust has
  `r#"..."#`; Zig has `\\` line literals; C# has `"""` raw string literals;
  Python has triple quotes; Jai has `#string` here-strings; C joins adjacent
  literals.

Suggestion: Odin's backtick raw string, which may span lines and takes no
escapes.

## Declarations have no documentation or grouping form

Language. A description is an ordinary `//` comment, and nothing groups a
behavior's variables. When `.chibi.meta` sidecars were dropped, each file
description became a leading comment, and each variable category became a
`// State` or `// Wave` line above its variables (see
`test/fixtures/content/behaviors/WaveBehavior.chibi`). A tool cannot tell those
comments from any other, so Studio cannot show a declaration's description in
hover, completion or the graph, or group variables by category (#563).

- Go and Odin treat a comment directly above a declaration, with no blank line,
  as its documentation; `go doc` and Odin's `core:odin/ast` attach it to the
  declaration. Rust and Zig use `///` for an item and `//!` for the enclosing
  module or file. C# uses `///` XML comments, and Python a docstring as the
  first statement.
- For grouping, Unity has `[Header("Wave")]`, Unreal `UPROPERTY(Category =
  "Wave")` and Godot `@export_group("Wave")`; all three are attributes the
  inspector reads.

Suggestion: Go and Odin's rule, a comment block directly above a declaration
documents it, which needs no new syntax; the frontend keeps it on the
declaration for the presentation and the language server. Leave grouping to a
later attribute syntax.

## Event payloads have no single typed declaration

Language. An event's payload is declared in three places: the engine's events
in a C table, a project's events in KDL, and each test file's events in its own
C catalog. Old handlers could also leave a field untyped. The version 30
migration therefore needed three type sources and a frozen copy of the engine
table (#565). Converting the tests' handlers then needed each test catalog's
names and types, and several disagreed with the engine's `Tick`
(`DeltaSeconds`, `cells`, `Elapsed`), so the conversion had to set the frozen
table aside for them (the PR that removed the old handler forms).

- Rust passes a struct such as `struct Tick { dt: f32 }` to a typed handler;
  Odin can model events as `Event :: union { Tick, … }`; C# derives payloads
  from `EventArgs`. In each, the payload is a type in source, and tests reuse
  the production type, so a field rename reaches every use through the type
  checker.

Suggestion: declare an event's payload as a record in `.chibi` source, which
the compilers, Studio, migrations and tests then share.

### Saved text still uses a C-string JSON boundary

Runtime boundary checkpoint (2026-10-21). Ordinary `string` values preserve
arbitrary bytes, but the existing save text API persists a JSON object of UTF-8
text keys and values. Its authored adapter rejects embedded NUL before calling
`save_set`, and the store validates JSON encoding before changing an entry or
revision, rejecting malformed UTF-8. Reading a missing entry can still return
an arbitrary-byte fallback string without persisting it. This limitation belongs
to the save format, not to the `string` invariant. Lossless binary saved values
need a separately reviewed encoding/API contract; no replacement or truncation
is performed at the existing boundary.

### Preview diagnostic JSON still requires UTF-8

Runtime boundary checkpoint (2026-10-21). Execution logs and preview log storage
retain explicit byte lengths, including embedded NUL. The existing controller
JSON transport can escape NUL but still cannot serialize malformed UTF-8 as a
JSON string. Serialization failure remains explicit; bytes are not replaced or
silently truncated. A lossless transport encoding needs its own response-contract
review and is not implied by the runtime string invariant.

### Record construction repeats some string copies

The descriptor cutover gives authored string fields owned storage, including
when an input string already belongs to the destination arena. The bounded
row-array attribution fixture records 45 additional copied text bytes per
constructed row from five nine-byte field copies, plus five empty terminators
once per nonempty invocation. This remains linear, but same-owner retention
could remove redundant copies.
Review it with shared slices and escaped-value lifetimes; keeping an arbitrary
borrowed pointer would not preserve the ownership contract.

## A program that imports the parser nears the compile budget

Toolchain. `chibi fmt` is a program whose closure holds the stage-one
tokenizer, parser and formatter, about 7,000 lines. Compiling it takes about
26 s and close to the 4 billion interpreted instructions a package closure may
use (`CLOSURE_COMPILE_BUDGET`, `src/authoring/asset_compiler_host.c`): roughly
500,000 instructions per line, paid on every run because programs are not
cached. Adding the 330-line `source_highlight` package exceeded the budget
("shared instruction budget exhausted"), so the formatter playground runs a
second program for highlighting, and every `chibi fmt` invocation, including
the pre-commit hook and each batch of a whole-tree check, waits for the compile
(2026-10-30, `cwaldner/chibi-formatter`).

- Go compiles and caches each package once (`GOCACHE`); `gofmt` starts in
  milliseconds. Zig caches compiled units by content hash, Rust incrementally
  by query, and Odin compiles a whole program in well under a second.

Suggestion: cache a program's prepared closure by the hash of its sources, so a
second `chibi fmt` starts at once, and profile where stage one spends its
instructions per line.
