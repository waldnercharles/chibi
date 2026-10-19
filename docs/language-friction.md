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
`strings.Slice`.

- Odin and Go index bytes with `s[i]` and slice with `s[a:b]`; Rust slices a
  `str` by byte offsets with `&s[a..b]` and reads bytes through
  `s.as_bytes()[i]`; Python and C# index characters with `s[i]`.

Suggestion: `s[i]` as a byte and `s[a:b]` as a string, with bounds checks.

## No counted loops

Counting loops are `while (index < n) { ...; index += 1; }`.

- Odin has `for i in 0..<n`, Rust `for i in 0..n`, Zig `for (0..n) |i|`,
  Python `for i in range(n)`, and C, C++ and C# three-part `for`.

Suggestion: Odin's `for i in 0..<n` and `for i in 0..=n`, plus an index in
`foreach`, as `for x, i in xs`.

## Stage one constants are literals only

Stage zero folds constant expressions, but stage one accepts only literal
constants, so a lookup table is a local array built on every call
(`PowerStep` in `Number.chibi`).

- Odin and Go fold constant expressions; Odin also has constant arrays. Zig
  evaluates `comptime` code, C++ `constexpr` and Rust `const` items.

Suggestion: stage one folds the constant expressions stage zero accepts, and
constant arrays such as `Steps :: [1, 3, 6]`.

## Strings are NUL-terminated and carry no length

In `core:strings`, every `ByteAt` runs `strlen`, so each byte loop is
quadratic, and every `Slice` copies, so Odin's `s = s[i+n:]` inside `Count`,
`_split` and `replace` copies the rest of the string on each match. A string
cannot hold a NUL byte, so Odin's `trim_null` has no use.

- Odin, Go, Zig and Jai strings are a pointer and a length; `len(s)` and
  `s[i]` are O(1) and `s[a:b]` is a view. Rust's `&str` and C++'s
  `std::string_view` are the same pair; C# strings carry their length.

Suggestion: strings that carry their length and may hold any byte, with
slices that share the bytes once ownership allows it.

## No narrow unsigned types or wrapping arithmetic

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

## Every top-level declaration is exported

Odin's private `_split` became the exported `SplitKeeping`, and the shared
line splitter the exported `SplitLinesKeeping`.

- Odin has `@(private)` and `@(private="file")`; Jai `#scope_file` and
  `#scope_module`; Rust, Zig and F# export only `pub` or public items; Go
  exports capitalized names; C has `static`; C# `internal` and `private`;
  TypeScript exports explicitly; Python marks private names with `_`.

Suggestion: an `@private` attribute, beside `@test`, that keeps a declaration
out of the package's exports and its reference page.

## No way to build a string

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

## A pure function cannot fail on purpose

Odin's `repeat` panics on a negative count. `expect` is not `pure`, so
`core:strings` returns an empty string instead and hides the caller's mistake;
only a checked conversion or an out-of-bounds read fails a pure call.

- Odin has `assert` and `panic`; Rust `assert!` and `panic!`; Zig `@panic`
  and `unreachable`; C `assert`; C# and F# throw.

Suggestion: a pure `assert(condition, message)` that fails the call as a
checked conversion does.

## No runes

`core:strings` works on bytes: `Cut` takes byte offsets, `EqualFold`,
`ToLower`, `ToUpper` and the space trims are ASCII, and `Split(s, "")` splits
into bytes. Odin's `rune_count`, `index_rune`, `reverse`, `expand_tabs`, the
justify procedures and `levenshtein_distance` are not ported.

- Odin has a `rune` type, `for r, i in s` that decodes UTF-8 and
  `core:unicode/utf8`; Go ranges over a string's runes; Rust has `char` and
  `s.chars()`; C# `Rune`; Python and Ruby strings are code points.

Suggestion: a `rune` type and Odin's decoding `for r, i in s`, then a
`core:utf8` package written in Chibi.

## No procedure values

Odin builds `trim_space` and `trim` on `trim_left_proc`, `index_proc` and
`fields_proc`, which take a procedure. `core:strings` repeats each loop
instead and leaves the `_proc` API out.

- Odin passes non-capturing `proc` values; C function pointers; Zig function
  types and `comptime` parameters; Rust `fn` pointers and closures; C#, F#,
  TypeScript, Python and Ruby lambdas.

Suggestion: non-capturing procedure values as `pure fn` parameters, as Odin's.

## No reference parameters

Odin's `split_iterator(s: ^string, sep)`, `split_lines_iterator` and
`fields_iterator` advance the caller's string, so `core:strings` has no
iterators.

- Odin `^T`, Rust `&mut T`, C# `ref`, Zig `*T` and C pointers; Python and
  Ruby use generators.

Suggestion: `ref` parameters of locals, or the iterator's state as a second
result once functions return several.

## No default parameter values

Odin's `cut(s, rune_offset := 0, rune_length := 0)` is `Cut(s, offset, count)`
with every argument required.

- Odin, C#, C++, Python, TypeScript and Ruby default trailing parameters;
  Rust and Zig don't.

Suggestion: constant defaults for trailing parameters; low priority.
