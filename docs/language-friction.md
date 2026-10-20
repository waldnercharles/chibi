# Language friction

Where writing Chibi was awkward, how existing languages handle the same need,
and what Chibi could adopt. Threads add entries when they hit friction; each
entry checks prior art before it suggests anything, and none of these is
decided or implemented. Examples come from the stage-one compiler
(`tools/compiler/package_compiler`) unless an entry says otherwise.

## Untyped integer literals are doubles

`xs = array_add(xs, 0)` appends a double, and `index := 0` declares a double,
because an untyped number literal is a `number`. Integer code writes
`int(0)` and `index: int = 0` instead; stage one rejects untyped locals for
this reason.

- Go and Odin give an untyped constant its type from context and otherwise
  default by spelling: `1` is `int`, `1.0` is `float64`/`f64`.
- Rust infers a literal's type from its uses and defaults to `i32` or `f64`.
- C, C++, C# and Python type a literal by its spelling: `1` is an integer and
  `1.0` a floating value.
- Zig keeps `comptime_int` until a use fixes the type; a runtime variable
  needs one.

Suggestion: keep contextual typing and default by spelling, as Go and Odin
do. The trade-off: behavior state such as `elapsed := 0` infers `number` today
so later assignments can hold fractions; with spelling defaults it needs
`elapsed := 0.0` or `elapsed: number = 0`.

## Records hold only int, bool and string fields

Authored records cannot hold arrays, records, unions, floating values or
64-bit integers. The compiler uses one-variant unions as records with arrays
(`Step`, `Decimal`), splits 64-bit payloads into `int` halves (`Low`, `High`,
`Join`) and keeps parallel arrays.

- C, C++, Rust, Odin, Zig, C#, F#, TypeScript, Python and Ruby all let a
  struct, record or object hold any value type.

Suggestion: record fields of every value type, as Odin's structs.

## Authored functions return one value

Natives return several named results (`compiler_json_string(s).valid`), but
authored functions return one. The compiler returns one-variant unions as
tuples and destructures them with `switch`, which must bind every field.

- Odin returns several values, optionally named:
  `proc() -> (value: int, ok: bool)`, with `value, ok := f()`; Go's
  `func() (value int, ok bool)` is the same idea.
- Rust, F#, Python, C# and TypeScript return tuples and destructure them; Zig
  returns anonymous structs.

Suggestion: Odin's named multiple results, which behaviors already spell
(`-> (Result: bool = false)`), with `a, b := F()` destructuring.

## uint64 has no ordering, subtraction or division

The runtime computes only addition, multiplication, equality and bit
operations on `uint64`, so magnitudes move to `int64` and comparisons are
rewritten.

- C, C++, Rust, Odin, Zig and C# provide every arithmetic and comparison
  operator on unsigned integers.

Suggestion: complete unsigned arithmetic and ordering in the verifier and
backends.

## No character literals

Bytes are compared by code: `48` for `'0'`, `46` for `'.'`, `101` for `'e'`.

- C, C++, C#, Odin and Zig have `'0'`; Rust has `'0'` and `b'0'`; Python uses
  `ord("0")`.

Suggestion: character literals typed by context (`byte` or `int`), as Odin
and Zig type `'0'`.

## Strings have no indexing or slicing syntax

Byte access goes through `strings.ByteAt`, `strings.ByteLength` and
`strings.Slice`. Strings carry no length, so each `ByteAt` measures its
string: scanning 1 MiB byte by byte took 19.9 s in a Release build on macOS
arm64, against 0.07 s through `strings.Bytes`, which reads every byte into an
`int[]` once (2026-10-19).

- Odin and Go index bytes with `s[i]` and slice with `s[a:b]`; Rust slices a
  `str` by byte offsets with `&s[a..b]` and reads bytes through
  `s.as_bytes()[i]`; Python and C# index characters with `s[i]`.

Suggestion: `s[i]` as a byte and `s[a:b]` as a string, with bounds checks.

## Stage one compiles only `for name in array`

The language now has `for i in first..last` with inclusive bounds, keyed
loops and `for item: ref in items` (#420), but stage one rejects all three, so
the compiler's own counting loops stay `while (index < n) { ...; index += 1; }`.

- Odin has `for i in 0..<n`, `for i in 0..=n` and `for x, i in xs`; Rust
  `for i in 0..n`; Zig `for (0..n) |i|`; Python `for i in range(n)`; C, C++
  and C# three-part `for`.

Suggestion: stage one compiles range and keyed loops. Chibi's inclusive
`first..last` makes the common count loop `0..n - 1`; Odin's half-open `0..<n`
would avoid the `- 1`.

## Stage one constants are literals only

Stage zero folds constant expressions, but stage one accepts only literal
constants, so a lookup table is a local array built on every call
(`PowerStep` in `Number.chibi`).

- Odin and Go fold constant expressions; Odin also has constant arrays. Zig
  evaluates `comptime` code, C++ `constexpr` and Rust `const` items.

Suggestion: stage one folds the constant expressions stage zero accepts, and
constant arrays such as `Steps :: [1, 3, 6]`.

## From the core:strings port

Porting Odin's `core:strings` to `packages/core/strings` (#503) hit these.
Where an entry above already covers the need, the port's example is listed
here with a pointer to it.

### Character literals

`ByteAt(path, index) == 47` stands for `'/'`, and `ToLower` tests
`c >= 65 && c <= 90`. See [No character literals](#no-character-literals).

### String indexing and slicing

Every byte read is `ByteAt(s, i)` and every slice `Slice(s, start, count)`.
See [Strings have no indexing or slicing syntax](#strings-have-no-indexing-or-slicing-syntax)
and the representation entry below.

### Integer locals need a type

The port declares two dozen counters as `i: int = 0;`. See
[Untyped integer literals are doubles](#untyped-integer-literals-are-doubles).

### Counting loops

Stage one compiles `core:strings`, so every scan, forward and reverse, is a
`while` with its own `i += 1` or `i -= 1`. See
[Stage one compiles only `for name in array`](#stage-one-compiles-only-for-name-in-array);
a reverse form, as Odin's `#reverse for` or Rust's `(0..n).rev()`, would cover
the reverse scans.

### Several results and swaps

Odin's `partition` returns `(head, match, tail)` and `replace` returns
`(output, was_allocation)`; the port returns a `Parts` record and drops
`was_allocation`. Odin's `tr, sr = sr, tr` takes a temporary. See
[Authored functions return one value](#authored-functions-return-one-value);
destructuring assignment would also cover swaps, as in Odin, Go and Python.

### Strings are NUL-terminated and carry no length

In `core:strings`, every `ByteAt` runs `strlen`, so each byte loop is
quadratic, and every `Slice` copies, so Odin's `s = s[i+n:]` inside `Count`,
`_split` and `replace` copies the rest of the string on each match. A string
cannot hold a NUL byte, so Odin's `trim_null` has no use.

- Odin, Go, Zig and Jai strings are a pointer and a length; `len(s)` and
  `s[i]` are O(1) and `s[a:b]` is a view. Rust's `&str` and C++'s
  `std::string_view` are the same pair; C# strings carry their length.

Suggestion: strings that carry their length and may hold any byte, with
slices that share the bytes once ownership allows it.

### No narrow unsigned types or wrapping arithmetic

Odin's Rabin-Karp `index` keeps a `u32` hash that wraps. `core:strings` keeps
it in `int64` and masks every step:
`h = (h * 16777619 + ByteAt(s, i) - pow * ByteAt(s, i - n)) & 0xFFFFFFFF`.
`byte` itself has only equality and bit operations.

- Odin, Go and C define unsigned overflow to wrap; Rust has
  `wrapping_mul` and `Wrapping<u32>`; Zig has `+%`, `-%` and `*%` beside
  checked `+`, `-` and `*`; C# wraps in `unchecked` code; Python and
  JavaScript mask (`& 0xFFFFFFFF`, `Math.imul`, `>>> 0`).

Suggestion: `u8`, `u16` and `u32` with full arithmetic, and Zig's wrapping
operators so ordinary arithmetic stays checked.

### Every top-level declaration is exported

Odin's private `_split` became the exported `SplitKeeping`, and the shared
line splitter the exported `SplitLinesKeeping`.

- Odin has `@(private)` and `@(private="file")`; Jai `#scope_file` and
  `#scope_module`; Rust, Zig and F# export only `pub` or public items; Go
  exports capitalized names; C has `static`; C# `internal` and `private`;
  TypeScript exports explicitly; Python marks private names with `_`.

Suggestion: an `@private` attribute, beside `@test`, that keeps a declaration
out of the package's exports and its reference page.

### No way to build a string

Concatenation is `format_text("{}{}", a, b)`, so `Join`, `Repeat`, `Replace`
and `ToLower` copy everything built so far on each append. No conversion makes
a string from a byte: `ToLower` slices its letter out of
`"abcdefghijklmnopqrstuvwxyz"`.

- Odin has `strings.Builder` and `string(bytes)`; Go `strings.Builder` and
  `string(b)`; Rust `String::push_str` and `String::from_utf8`; Zig
  `ArrayList(u8)`; C# `StringBuilder`; Python `"".join`; Ruby a mutable
  `String#<<`.

Suggestion: `string(bytes)` and `byte[](s)` conversions, and an in-place
`s = append(s, t)` on a local string, as arrays already update in place.

### A pure function cannot fail on purpose

Odin's `repeat` panics on a negative count. Purity is inferred since #420, and
`expect` has an effect, so calling it would make `Repeat` impure; `core:strings`
returns an empty string instead and hides the caller's mistake. Only a checked
conversion or an out-of-bounds read fails a pure call.

- Odin has `assert` and `panic`; Rust `assert!` and `panic!`; Zig `@panic`
  and `unreachable`; C `assert`; C# and F# throw.

Suggestion: a pure `assert(condition, message)` that fails the call as a
checked conversion does.

### No runes

`core:strings` works on bytes: `Cut` takes byte offsets, `EqualFold`,
`ToLower`, `ToUpper` and the space trims are ASCII, and `Split(s, "")` splits
into bytes. Odin's `rune_count`, `index_rune`, `reverse`, `expand_tabs`, the
justify procedures and `levenshtein_distance` are not ported.

- Odin has a `rune` type, `for r, i in s` that decodes UTF-8 and
  `core:unicode/utf8`; Go ranges over a string's runes; Rust has `char` and
  `s.chars()`; C# `Rune`; Python and Ruby strings are code points.

Suggestion: a `rune` type and Odin's decoding `for r, i in s`, then a
`core:utf8` package written in Chibi.

### No procedure values

Odin builds `trim_space` and `trim` on `trim_left_proc`, `index_proc` and
`fields_proc`, which take a procedure. `core:strings` repeats each loop
instead and leaves the `_proc` API out.

- Odin passes non-capturing `proc` values; C function pointers; Zig function
  types and `comptime` parameters; Rust `fn` pointers and closures; C#, F#,
  TypeScript, Python and Ruby lambdas.

Suggestion: non-capturing procedure-typed parameters, as Odin's `proc` values.

### Stage one has no reference parameters

Odin's `split_iterator(s: ^string, sep)`, `split_lines_iterator` and
`fields_iterator` advance the caller's string. The language has `ref`
parameters since #420, but stage one rejects them, and `core:strings`, which
stage one compiles, has no iterators.

- Odin `^T`, Rust `&mut T`, C# `ref`, Zig `*T` and C pointers; Python and
  Ruby use generators.

Suggestion: stage one compiles `ref` parameters of locals, then
`core:strings` gains Odin's iterators.

### No default parameter values

Odin's `cut(s, rune_offset := 0, rune_length := 0)` is `Cut(s, offset, count)`
with every argument required.

- Odin, C#, C++, Python, TypeScript and Ruby default trailing parameters;
  Rust and Zig don't.

Suggestion: constant defaults for trailing parameters; low priority.

### Library growth spends the compiler tests' budgets

At #503 the 16 KB package prepared to an 837 KB module, about 52 times its
source, mostly per-function debug metadata. Harnesses embed the shipped
`core:strings`: writing it with the authored writer on bytecode under ASan took
22 s, and stage one compiling it in the package compiler's imports sample added
about 24 s of CPU. The writer now writes modules above 512 KiB on the compiled
backend only, and the imports sample has its own selector, but each further
core package spends more of the compiler tests' 60-second budgets.

- Odin, Go, Rust and Zig keep debug information beside the code (DWARF, PDB)
  rather than in it, and release builds strip it.

Suggestion: keep debug metadata out of the prepared form the writer and the
compiler tests process, or make it optional, and give harnesses that only test
imports a fixed core fixture.

## Calls nest at most 64 deep

Every authored function shares a call-depth budget of 64 (#420). Stage-one
twins that walked a graph recursively, the union-members walk and the effect
analysis query restrictions, failed on deep inputs and became loops over an
explicit stack.

- Odin, C, C++, Rust and Zig recurse until the native stack runs out, often
  megabytes deep; Python's default limit is 1,000 frames; C# and F# grow to
  the thread's stack, and F# turns tail calls into jumps.

Suggestion: a deeper budget, counted in frame bytes rather than calls, or
guaranteed tail calls as F# has, so a recursive walk needs no hand-made stack.

## Templates are package-local

A template with `$T` parameters cannot be exported, so a generic helper is
copied into each package that needs it (#420).

- Odin's parametric procedures, Zig's `comptime` functions, Rust generics and
  C++ templates all export from their package, module or header.

Suggestion: export templates, instantiating each at the caller with the
callee package's declarations in scope, as Odin does.

## Build programs cannot format numbers

The build host offers `BuildJoin`, but neither `format_text` nor any number
conversion. `test/tools/GameEquivalence.chibi` therefore reports the first
differing frame by slicing its number out of the trace line, and it cannot
report how many lines it compared.

- Odin has `fmt.tprintf("%d", n)` and `strconv.itoa`; Go `strconv.Itoa`; Rust
  `n.to_string()` and `format!`; Zig `std.fmt.allocPrint`; C `snprintf`; C#
  `n.ToString()` and interpolation; Python f-strings; Ruby `n.to_s`.

Suggestion: one text library for every Chibi host, with `format_text` and
`core:strings` in build programs as in games.

## Build programs read files only as text

`BuildReadFile` returns a NUL-terminated string, so a build program cannot
read a binary file such as a `.chibip` artifact. Locating an artifact's module
records for the equivalence harness took a C function,
`player_prepared_modules`, although the stage-one compiler already writes
prepared modules in Chibi.

- Odin `os.read_entire_file` and Go `os.ReadFile` return `[]byte`; Rust
  `fs::read` returns `Vec<u8>`; Zig `readFileAlloc` returns `[]u8`; Python
  `open(path, "rb").read()` returns `bytes`.

Suggestion: `BuildReadBytes(path) -> byte[]` for build programs, so tools can
read prepared formats in Chibi.

## Build programs have their own string vocabulary

`tools/toolchain/Build.chibi` cannot import `core:strings` or any other
package. It has only the build host's natives (`BuildBefore`, `BuildAfter`,
`BuildStartsWith`, `BuildTrim`, `BuildLines`, `BuildJoin`), with no length,
index, contains or split. Moving `configure_inputs.py package-enabled` into it
meant writing `Contains` as `BuildBefore(text, part) != text` and parsing
`NAME[:TYPE]=VALUE` with nested before/after calls.

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

## Ordinary nouns are reserved, and the build host doesn't say where

A local named `entry` in `Build.chibi` failed with only "Unexpected syntax".
`entry` is a keyword, as are `exit`, `query` and `where`, and the build host's
diagnostic names no file, line, column or token. Finding it took bisecting the
file. #420 already made `join` contextual and dropped `domain`.

- Rust, Go, Odin, Zig, C#, TypeScript and Python report file, line and
  column; Rust and Python also name the token (Rust: "expected identifier,
  found keyword").
- C# keeps `where`, `join` and `select` contextual: they are keywords only
  inside a query expression. TypeScript does the same with `type` and `from`.
- Go's 25 keywords include none of these. Odin's include only `where`.

Suggestion: make declaration-only words contextual, and give build-host
syntax errors the parser's position and token.

## From the tokenizer port

Porting Odin's `core:odin/tokenizer` to `tools/compiler/source_tokenizer` hit
these. Where an entry above already covers the need, the tokenizer's example is
listed here with a pointer to it.

### Several results

`Scan` returns a token's kind, end and error as a `Lexeme` record. Stage zero
already compiles named results,
`Pair :: (x: int) -> (kind: int, end: int) { return (x, x + 1); }`, read as
`Pair(3).kind`, but stage one does not. See
[Authored functions return one value](#authored-functions-return-one-value).

### Character literals

Bytes are compared by code throughout, `c == 34` for `'"'`. See
[No character literals](#no-character-literals).

### Tables built per call

`Tokenize` builds its byte-class, keyword and terminator tables on every call.
See [Stage one constants are literals only](#stage-one-constants-are-literals-only).

### Reserved nouns

A local named `prototype` failed with "Unexpected syntax" at its line, since
#420 made `prototype` a reserved word; the tokenizer's flag became
`prototyping`. See
[Ordinary nouns are reserved, and the build host doesn't say where](#ordinary-nouns-are-reserved-and-the-build-host-doesnt-say-where).

### Hashing in a wrapping `u32`

Odin's keyword hash is FNV-1a in a wrapping `u32`; the port keeps it in
`uint64` and masks each step with `& 0xFFFFFFFF`. See
[No narrow unsigned types or wrapping arithmetic](#no-narrow-unsigned-types-or-wrapping-arithmetic).

### No enumerations

The tokenizer's 114 token kinds are numbered constants, `Ident: int : 3;`
through `Relationship: int : 113;`, and `KindNames` returns their spellings in
the same order by hand. A set of kinds, such as those that can end a statement,
is a `bool[]` filled from a list.

- Odin has `enum` with implicit values, enumerated arrays such as
  `[Token_Kind]string`, `bit_set[Flag]` and `fmt`'s names for values; Zig
  `enum` with `@tagName`; Rust enums with discriminants; C enums, often with
  X-macros for the names; C# `Enum.GetName`; TypeScript numeric enums with
  reverse mapping; Python `enum.Enum`.

Suggestion: Odin's `enum`, enumerated arrays and `bit_set`, with a name for
each value.

### Stage one switches only on union variants

Stage zero compiles `switch c { case 61: ... }`, but stage one matches only
`.Variant` arms, so the tokenizer's operator table is a chain of
`if (c == 61) { return Eq; }`.

- Odin switches on any integer or rune with ranges, `case 'a'..='z', '_':`;
  C and C++ `switch` on integers; Rust `match` with `b'a'..=b'z'`; Zig
  `switch` with `'a'...'z'`; C# relational patterns, `>= 'a' and <= 'z'`.

Suggestion: stage one compiles integer `switch`, then Odin's ranges.

### A `while (true)` loop does not end a function

`while (true) { ... return x; }` as a function's last statement is rejected
with "Union result 'Result' requires a value on every return path", so the
tokenizer's loop breaks out to a return after it.

- Odin's checker counts `for {}` without a `break` as terminating; Go's spec
  does the same for a `for` with no condition and no `break`; Rust's `loop`
  has type `!`; Zig and C# also treat code after `while (true)` without a
  `break` as unreachable.

Suggestion: a `while (true)` with no `break` ends the function, as in Odin.

### Calls and record snapshots are costly

Profiling the tokenizer showed record validation at the top: storing a record
in a local snapshots it, and each snapshot checks the record by rehashing its
type's layout (`pin_record_valid`, `type_layout_hash`); each call also prepares
an activation. Inlining the hot loops, reading byte classes from a table and
returning records only for rare tokens took the corpus run from 28 to 4
seconds on SLJIT in a sanitizer build. The bytecode backend is about eight
times slower still.

- Odin, C, Zig and Rust return small structs in registers or a result
  location and inline small procedures; C# returns structs by value; none
  hashes a type's layout at run time.

Suggestion: an implementation change rather than a language one: hash each
record type's layout once, and inline small pure functions.
