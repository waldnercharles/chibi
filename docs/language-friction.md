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

## Untyped integer literals are doubles

Language. `xs = array_add(xs, 0)` appends a double, and `index := 0` declares
a double, because an untyped number literal is a `number`. Integer code writes
`int(0)` and `index: int = 0` instead; stage one rejects untyped locals for
this reason. `core:strings` declares two dozen counters as `i: int = 0;`
(#503). A floating value also narrows to an integer silently:
`x: int = 0; x += 1.5` leaves 1.

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

## Records hold only scalar fields

Language and runtime. Stage zero's records hold `int`, `bool`, `string`,
`byte`, `int64`, `uint64`, `float`, `double` and `number` fields, but no
arrays, unions or other records. Stage one accepts the integer, `bool` and
`string` fields but no floating ones. The compiler uses one-variant unions as
records with arrays (`Step`, `Decimal`) and keeps parallel arrays. The
prepared writer also pools 64-bit IDs in a `wide` array its records index,
from when stage one's records held only `int`, `bool` and `string` fields.

- C, C++, Rust, Odin, Zig, C#, F#, TypeScript, Python and Ruby all let a
  struct, record or object hold any value type.

Suggestion: record fields of every value type, as Odin's structs.

## Storing a `uint64` in a record field makes calls copy

Runtime. Stage zero passes a function's arguments without copying them only
when its analysis proves the function leaves the caller's storage alone. Raw
pointers have the `uint64` slot type, so the analysis takes any `uint64`
value for a pointer: once a function stores a `uint64` parameter into a field
of a local record, the next write to that record looks like a write through
the caller's pointer. That function, and every function that calls it, then
copies every argument on each call. One such store in `ConstantCode` took
stage one compiling itself from 25 to over 60 seconds; building the record in
one literal keeps its calls free.

- Odin, C, Zig and Rust type pointers apart from integers, so neither their
  aliasing rules nor their optimizers take a `u64` for an address.

Suggestion: give raw pointers their own slot type, so the analysis tells them
from `uint64` values.

## Stage one returns one value, and nothing destructures

Stage one, then language. Stage zero compiles named multiple results,
`Pair :: (x: int) -> (kind: int, end: int) { return(x, x + 1); }`, read as
`Pair(3).kind`; stage one returns one value. Neither compiler destructures
results into locals or assigns several targets at once.

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

Suggestion: stage one compiles named results, then both compilers gain
`a, b := F()` destructuring and `a, b = b, a`.

## Stage one computes less integer arithmetic than stage zero

Stage one. Stage zero computes every arithmetic and comparison operator on
`uint64`, `uint`, `uint16`, `int8` and `byte`, with unsigned ordering and
division. Stage one computes only addition, multiplication, equality and bit
operations on `uint64`, only equality and bit operations on `byte`, and has no
`uint`, `uint16` or `int8`, so magnitudes move to `int64` and comparisons are
rewritten. `core:strings` works in `int` for the same reason (#503).

- C, C++, Rust, Odin, Zig and C# provide every arithmetic and comparison
  operator on unsigned integers.

Suggestion: stage one computes what stage zero computes.

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

## Stage one has no character literals

Stage one. Stage zero has character literals and `char` since #530: `'a'`
takes an integer type from context, as in `c: byte = 'a'` or
`ByteAt(s, i) == '/'`, and is otherwise a `char`, a 32-bit Unicode scalar with
integer arithmetic. Stage one rejects them, and it compiles
the compiler, `core:strings` and the tokenizer, so those still compare bytes
by code: `48` for `'0'`, `46` for `'.'`, `101` for `'e'`; `core:strings` writes
`ByteAt(path, index) == 47` for `'/'` and tests `c >= 65 && c <= 90` (#503);
the tokenizer writes `c == 34` for `'"'` (#518).

- C, C++, C#, Odin and Zig have `'0'`; Rust has `'0'` and `b'0'`; Python uses
  `ord("0")`.

Suggestion: stage one types character literals from context as stage zero
does, then those sources replace their codes.

## Strings have no indexing or slicing syntax

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

## Stage one compiles only `for name in array`

Stage one. Stage zero has `for i in first..last` with inclusive bounds, keyed
loops and `for item: ref in items` (#420), and since #RANGES_PR half-open
`first..<last` and reverse `for <` loops over ranges and arrays, as the owner
decided on 2026-10-20 after Odin's `..<` and Jai's `for <`. Stage one rejects
all of them, so the compiler's own counting loops stay
`while (index < n) { ...; index += 1; }`. Since stage one compiles
`core:strings`, every scan there, forward and reverse, is a `while` with its
own `i += 1` or `i -= 1`, and a `continue` before the step hangs (#503).

- Odin has `for i in 0..<n`, `for i in 0..=n`, `for x, i in xs` and
  `#reverse for`; Rust `for i in 0..n` and `(0..n).rev()`; Zig
  `for (0..n) |i|`; Jai inclusive `for i: 0..n-1` and `for <` to reverse;
  Python `for i in range(n)`; C, C++ and C# three-part `for`.

Suggestion: stage one compiles range, keyed and reverse loops, then the
compiler and `core:strings` replace their counting `while` loops.

## Stage one constants are literals only

Stage one. Stage zero folds constant expressions, constant arrays and calls
whose body runs at compile time, `while` loops included, but stage one accepts
only literal constants. A lookup table is therefore a local array built on
every call: `PowerStep` in `Number.chibi`, and the tokenizer's byte-class,
keyword and terminator tables in `Tokenize` (#518).

- Odin and Go fold constant expressions; Odin also has constant arrays. Zig
  evaluates `comptime` code, C++ `constexpr` and Rust `const` items.

Suggestion: stage one folds the constant expressions stage zero accepts, and
constant arrays such as `Steps :: [1, 3, 6]`.

## From the core:strings port

Porting Odin's `core:strings` to `packages/core/strings` (#503) hit these.
Its examples of needs covered above are listed with those entries.

### Strings are NUL-terminated and carry no length

Runtime. In `core:strings`, every `ByteAt` runs `strlen`, so each byte loop is
quadratic, and every `Slice` copies, so Odin's `s = s[i+n:]` inside `Count`,
`_split` and `replace` copies the rest of the string on each match. A string
cannot hold a NUL byte, so Odin's `trim_null` has no use.

- Odin, Go, Zig and Jai strings are a pointer and a length; `len(s)` and
  `s[i]` are O(1) and `s[a:b]` is a view. Rust's `&str` and C++'s
  `std::string_view` are the same pair; C# strings carry their length.

Suggestion: strings that carry their length and may hold any byte, with
slices that share the bytes once ownership allows it.

### Every top-level declaration is exported

Language. Odin's private `_split` became the exported `SplitKeeping`, and the
shared line splitter the exported `SplitLinesKeeping`.

- Odin has `@(private)` and `@(private="file")`; Jai `#scope_file` and
  `#scope_module`; Rust, Zig and F# export only `pub` or public items; Go
  exports capitalized names; C has `static`; C# `internal` and `private`;
  TypeScript exports explicitly; Python marks private names with `_`.

Decision (2026-10-20): a declaration is private to its package, every file of
it, unless `pub` exports it, as in Rust and Zig: `pub Join :: (...)`. Game
projects' `library` blocks follow once they become packages.

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

### No procedure values

Language. Odin builds `trim_space` and `trim` on `trim_left_proc`,
`index_proc` and `fields_proc`, which take a procedure. `core:strings` repeats
each loop instead and leaves the `_proc` API out.

- Odin passes non-capturing `proc` values; C function pointers; Zig function
  types and `comptime` parameters; Rust `fn` pointers and closures; C#, F#,
  TypeScript, Python and Ruby lambdas.

Suggestion: non-capturing procedure-typed parameters, as Odin's `proc` values.

### Stage one has no reference parameters

Stage one. Odin's `split_iterator(s: ^string, sep)`, `split_lines_iterator`
and `fields_iterator` advance the caller's string. The language has `ref`
parameters since #420, but stage one rejects them, and `core:strings`, which
stage one compiles, has no iterators.

- Odin `^T`, Rust `&mut T`, C# `ref`, Zig `*T` and C pointers; Python and
  Ruby use generators.

Suggestion: stage one compiles `ref` parameters of locals, then
`core:strings` gains Odin's iterators.

### Stage one has no default parameter values

Stage one. Odin's `cut(s, rune_offset := 0, rune_length := 0)` is
`Cut(s, offset, count)` with every argument required. Stage zero already
compiles `Add :: (a: int, b: int = 10) -> int` and `Add(1)`.

- Odin, C#, C++, Python, TypeScript and Ruby default trailing parameters;
  Rust and Zig don't.

Suggestion: stage one compiles the constant defaults stage zero accepts; low
priority.

### Library growth spends the compiler tests' budgets

Toolchain. At #503 the 16 KB package prepared to an 837 KB module, about 52
times its source, mostly per-function debug metadata. Harnesses embed the
shipped `core:strings`: writing it with the authored writer on bytecode under
ASan took 22 s, and stage one compiling it in the package compiler's imports
sample added about 24 s of CPU. The writer now writes modules above 512 KiB on
the compiled backend only, and the imports sample has its own selector, but
each further core package spends more of the compiler tests' 60-second
budgets.

- Odin, Go, Rust and Zig keep debug information beside the code (DWARF, PDB)
  rather than in it, and release builds strip it.

Suggestion: keep debug metadata out of the prepared form the writer and the
compiler tests process, or make it optional, and give harnesses that only test
imports a fixed core fixture.

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

## Templates are package-local

Language. A template with `$T` parameters cannot be called from another
package, so a generic helper is copied into each package that needs it
(#420). A `library` block's templates already specialize for qualified
callers.

- Odin's parametric procedures, Zig's `comptime` functions, Rust generics and
  C++ templates all export from their package, module or header.

Suggestion: export templates, instantiating each at the caller with the
callee package's declarations in scope, as Odin does.

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
before/after calls.

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

- Rust, Go, Odin, Zig, C#, TypeScript and Python report file, line and
  column; Rust and Python also name the token (Rust: "expected identifier,
  found keyword").
- C# keeps `where`, `join` and `select` contextual: they are keywords only
  inside a query expression. TypeScript does the same with `type` and `from`.
- Go's 25 keywords include none of these. Odin's include only `where`.

Suggestion: make declaration-only words contextual, and give build-host
syntax errors the parser's position and token.

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

### Stage one switches only on union variants

Stage one. Stage zero compiles `switch c { case 61: ... }`, but stage one
matches only `.Variant` arms, so the tokenizer's operator table is a chain of
`if (c == 61) { return Eq; }`.

- Odin switches on any integer or rune with ranges, `case 'a'..='z', '_':`;
  C and C++ `switch` on integers; Rust `match` with `b'a'..=b'z'`; Zig
  `switch` with `'a'...'z'`; C# relational patterns, `>= 'a' and <= 'z'`.

Suggestion: stage one compiles integer `switch`, then Odin's ranges.

### A function can end without returning

Language. `while (true) { ... return x; }` as a function's last statement is
rejected with "Union result 'Result' requires a value on every return path",
so the tokenizer's loop breaks out to a return after it. A function with a
scalar result, meanwhile, may fall off its end and return zero:
`F :: (n: int) -> int { if n > 0 { return 1; } }` gives `F(-1) == 0`.

- Odin's checker counts `for {}` without a `break` as terminating; Go's spec
  does the same for a `for` with no condition and no `break`, and both reject
  a function with results that can fall off its end. Rust's `loop` has type
  `!`; Zig and C# also treat code after `while (true)` without a `break` as
  unreachable.

Decision (2026-10-20): a function with results ends in a terminating
statement, as in Go and Odin: a `return`, an `if`/`else` or `switch` whose
every arm terminates, or a `while (true)` with no `break` out of it. Otherwise
the compiler reports "Missing return". A bare `return` still gives the named
results' current values.

### Calls and record snapshots are costly

Runtime. Profiling the tokenizer showed record validation at the top: storing
a record in a local snapshots it, and each snapshot checks the record by
rehashing its type's layout (`pin_record_valid`, `type_layout_hash`); each
call also prepares an activation. Inlining the hot loops, reading byte classes
from a table and returning records only for rare tokens took the corpus run
from 28 to 4 seconds on SLJIT in a sanitizer build. The bytecode backend is
about eight times slower still.

- Odin, C, Zig and Rust return small structs in registers or a result
  location and inline small procedures; C# returns structs by value; none
  hashes a type's layout at run time.

Suggestion: an implementation change rather than a language one: hash each
record type's layout once, and inline small pure functions.

## Game projects use library blocks, not packages

Language. Tools and `core:` are Odin-style packages (`package strings`,
`import strings "core:strings"`), but game projects declare
`MovementActions :: library { ... }` blocks that the project catalog resolves
and whose functions still read `entry.direction`. Visibility, imports and
templates therefore work differently in a game than in a package.

- Odin, Go and Zig have one unit, the package or file, for libraries and
  programs alike; Rust has crates and modules for both.

Suggestion: a game's libraries become packages, with `entry` gone as
`docs/design-notes.md` decided, so `pub` and imports mean the same everywhere.

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
