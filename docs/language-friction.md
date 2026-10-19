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
