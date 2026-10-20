# Authored package compiler

`tools/compiler/package_compiler` is the stage-one compiler as it grows. It
compiles packages' source files to a prepared closure, entirely in Chibi
after the raw Tree-sitter parser: the source snapshot and package graph, the
declarations, typing, lowering and the prepared writer. Stage zero still
builds the compiler itself; stage one is this compiler once it compiles the
compiler sources.

## Entry

`Compile(project: string, collection_names: string[], collection_paths:
string[], paths: string[], sources: string[], target: Target, abi: uint64,
names: string[], natives: Native[], ports: Port[], identities: uint64[]) ->
Result` returns `Compiled(module, bytes)`, a CHIBICLO closure with a module
for every package, or `Rejected(diagnostic)`. `paths` and `sources` hold the
files of every package, those of imported collections such as `core` under
their collection's path; the package of `paths[0]` is the closure's root. The
target supplies what Chibi cannot see:

- `Target`: pointer width, the offsets of a value header's and an array's
  members, and the failure kinds a host call and a failed assertion report;
- the prepared header ABI (`execution_prepared_abi`);
- the instruction names in ordinal order
  (`execution_prepared_instruction_name`), which the compiler resolves by name;
- the native functions it links, as its catalog describes them: each name,
  its ports' names, storage and container, and its capability ID and ABI. A
  port of storage 0 takes the type the call infers, one for all such ports of
  a call, as a generic port does; -2 takes each argument's own type, as a
  wildcard port such as `format_text`'s values does; -1 marks one the
  compiler cannot pass. A variadic native's last parameter takes any number
  of arguments. The test builds these tables from the compiler host's packs.

`CompileClosure` takes the same arguments and returns only the closure's
bytes, or none when `Compile` rejects the input, for a host that takes one
`byte[]` result (`compiler_host_invoke_bytes`).

## Supported language

Packages of functions, records, unions and constants, and the packages they
import:

- `int`, `bool` and `string` values, records of those, `byte`, `int64` and
  `uint64` values, unions whose variants carry any of them, and arrays of any
  of these, as parameters, results and locals; string literals decode as
  JSON strings, as stage zero reads them;
- integer literals, decimal or hexadecimal, typed as stage zero types them: by
  the declared local, assignment target, result, parameter, field, payload,
  element or index, or by the other operand. A literal whose type nothing
  gives, as in `x := 5`, is a double in stage zero, and stage one rejects it.
  A literal outside its type, such as `byte` 256, is rejected;
- `T(value)`, `T(wrap: value)` and `T(clamp: value)` for the integer types:
  the checked form fails the call when the value does not fit, `wrap` keeps
  its low bits and `clamp` saturates. Mixed integer operands widen to the type
  that holds both, and local declarations, assignments to locals and returns
  widen implicitly; `int` or `int64` with `uint64` needs a conversion;
- local declarations, assignment and compound assignment; `if`/`else`,
  `while`, `for name in array`, `break`, `continue` and `return`; range,
  keyed and `ref` loops reject;
- integer arithmetic, bitwise and comparison operators where the runtime
  computes them: all of them for `int` and `int64`, only addition,
  multiplication, equality and bit operations for `uint64`, and only equality
  and bit operations for `byte`, as stage zero's verifier allows. String
  equality, and `!`, `&&` and `||` with short-circuit evaluation;
- struct and array literals, where the type is known from the declared local,
  the result or the parameter. With no type expected, as for `xs := [a, b]` or
  an argument of a native such as `resize([int(0)], n)`, an array literal's
  first item gives its element type, as in stage zero; field reads;
  bounds-checked element reads;
- element assignment, compound included, and `xs = array_add(xs, x)` and
  `xs = append(xs, ys)` on a local array, which update it in place. As in
  stage zero, the native is recognized by its capability identity;
- field assignment, compound included, on a local record: `x.f = v` writes as
  stage zero writes it, through the reference make, field and write
  intrinsics, which copy the record with the new field, so no other value
  shares the old storage, and copy a string into it. A narrower integer
  value converts to the field's type. A record or array parameter whose
  value, fields or elements the function writes is copied into a cell of its
  own on entry, so the caller's value never changes; the others are read in
  place. For-loop elements, like pattern bindings, cannot be assigned;
- union values `U.V` and `U.V(a, b)`, with one positional value per payload
  field, and `switch` over a union with `case .V:`, `case .V(a, b):` binding
  every payload field, and a default `case:`. Bindings are immutable. As in
  stage zero, a switch without a default arm must cover every variant, and a
  repeated variant or default arm is rejected;
- `value is .V` and `value is not .V`, and `value is .V(a, b)` binding the
  payload in an if or while condition, alone or joined by `&&`, as stage zero
  allows bindings. They last through the consequence or loop body, not the
  else branch;
- calls between the package's functions, including recursion, and calls of
  natives, such as `length`, `slice` and `format_text`. A native's generic
  ports take the one type its arguments give them, its wildcard ports each
  argument's own type, and a variadic native's last parameter any number of
  arguments; natives are matched by name as the catalog matches names. A
  native with several results, such as `compiler_json_string`, gives a value
  whose `.name` reads one result, directly or through a local:
  `decoded := compiler_json_string(text); decoded.valid`;
- integer constants whose value is an integer literal, `-` allowed, with their
  type (`Mask: uint64 : 0xFF;`). An untyped constant, `Name :: 5;`, is a
  double in stage zero, and stage one rejects it. A use is the value itself;
  a local hides a constant of the same name;
- imports of `project:` and collection packages such as `core:strings`:
  `alias.F(...)` calls an imported function, `alias.C` reads an imported
  constant, and `alias.T` names an imported record or union in types,
  literals and `alias.U.V` variants. A local hides an alias of the same name.

Anything else is rejected with its source position. Floating and other
integer types, implicit widening of call arguments, element stores, literal
members, payloads and in-place natives, constants of other types or computed
values, type patterns, record field defaults, fields of array elements, named
payload arguments and literal variants come next. A literal passed to a
wildcard port, as in `format_text("{}", 1)`, is a double in stage zero, and
stage one rejects it. One check stage zero makes at compile time happens at
run time here: a checked conversion of a named constant that does not fit
fails the call.
Locals follow stage zero's scopes: blocks, switch arms, whose statements form
a block, and if and while statements, which hold their condition's bindings.
A name declared twice in one scope, such as `x := 1; x := 2;` or `.Pair(a, a)`,
is a duplicate declaration, and an inner scope may hide an outer name. One
difference remains, and only for programs stage zero rejects: stage zero
declares a local for its whole scope, so `n := n + 1` with a parameter `n` is
a cyclic dependency there, while stage one reads the parameter.

## Output

`Body.chibi` emits each function in one pass. A work stack visits statements
and expressions in evaluation order, and each expression pushes its typed
value. `EmitBody` keeps the tables and emits control flow; every other node is
a step in `Steps.chibi` that returns only the instructions and rows it adds,
numbered from the tables' sizes. A single emitter function exceeded the
verifier's storage budget for one definition, instructions times slots.

Scalar and string parameters move to locals and live in the locals bank.
Records and arrays follow stage zero's value model:

- A record or array value is a pointer to its storage.
- A struct or array literal calls the runtime's construct intrinsic.
- Every store into a local copies the value with the snapshot intrinsic into
  the data cell that is the local. Record and array parameters are read in
  place.
- An element read checks `0 <= index < count` and then reads the array's
  representation, as stage zero does: a nonzero stride means the elements are
  inline in `data`, where a record element is its own address; otherwise
  `items` holds value headers. The target supplies the `data` and `stride`
  offsets. A record element's boxed pointer is loaded only past a branch,
  through an address local.
- In-place updates use the array push, extend and store intrinsics on the
  local's header address.
- A `for` loop walks its own copy of the array, so the body may change the
  original; its continues jump to the increment.
- A record or array result publishes its whole value header.
- A union value is a scalar in the locals bank, built by the union-make
  intrinsic from its variant's tag and payload. A switch saves its subject in
  a local; each arm tests it with union-test and reads its bindings with
  union-get, and a record or array binding is read in place. Union IDs and
  variant tags follow stage zero's scheme.
- Pointer slots are listed as roots, so a suspended caller keeps its values.

Calls pass arguments and results through data cells; authored calls suspend
the caller once each, and native calls are host calls with the catalog's
capability ID and ABI. Every statement starts with a budget check, so every
loop crosses one. The definitions are canonical and unoptimized: the runtime
verifies and finalizes them on load, and a rejected definition's error now
names the verifier's own reason. Function and record IDs, record type names
and layout hashes follow stage zero's scheme, so a host invokes the same
entries and the runtime accepts the same record requirements.

Each package is its own module, assembled dependencies first:

- A call into another package goes through an import row, added on the first
  such call, as stage zero adds them. The row names the callee's module ID and
  the fingerprint of its prepared bytes, which the runtime checks on load. The
  reference names the callee's entry ID.
- Record requirements cover every record of the package and of each package
  it reaches, under the declaring package's IDs and type names. Stage zero
  lists only its own records and the types its direct imports export; the
  runtime accepts the extra rows. Unions have no requirement rows, and a
  variant's tag is the same in every module.
- The closure has one row per package, named by its identity.

Runtime sources record each instruction's origin. Debugger points and watches
are not emitted yet; they must be before stage one becomes the normal compiler.

## Test

Three selectors compile 85 samples on both compiler backends, each under its
own 60 s ceiling (macOS arm64, Debug + ASan/UBSan, 2026-10-19):

- `tool language-package-compiler-chibi`: scalars, control flow, records,
  arrays, strings and natives, and every rejection (10,537 assertions in
  33.2 s);
- `tool language-package-compiler-chibi-types`: unions, constants, patterns,
  scopes, integer types and inferred arrays (2,940 assertions in 28.2 s);
- `tool language-package-compiler-chibi-imports`: the imports sample, which
  compiles all of `core:strings` (1,946 assertions in 44.2 s).

A sample is package `facts`, optionally with a `lib` package and
`core:strings`. Every accepted closure loads in a fresh runtime host and runs
on bytecode and SLJIT with JSON arguments, and stage zero's own closure of the
same packages must return the same JSON; calls that must fail, such as
out-of-bounds reads, fail on all three. Rejections check the exact message and
position. In the push and extension samples, `Build(count)` must update its
local array in place: doubling the count at most doubles the values it copies.
Until stage one matched the array natives by capability identity, every update
copied the whole array, and the stage-two compiler grew past 4 GB compiling the
compiler's sources.

Compiling is slow. In this build, compiling all the samples takes about 17 s
of the selector's run. The arrays sample, about 50 lines, takes 0.9 s on SLJIT
and 2.6 s on bytecode; the imports sample, three packages, takes 1.1 s and
3.2 s. Two runtime changes cut the selector from 34 s to 28 s:

- A running call stack keeps up to 8 MiB of retired frames, evicting the least
  recently retired, and settles back to the 64 KiB idle budget. Before, the
  frames of the compiler's step functions, 0.2 to 0.7 MB each, were set up
  afresh on every call.
- Copying a record descriptor into another arena copies its bytes instead of
  re-deriving and re-hashing its layout.

`make test-bootstrap` builds an unsanitized Release `test_language` and runs
`tool language-package-compiler-chibi-bootstrap`. Stage zero prepares stage
one, which compiles the compiler's complete source set, 14 files in 11
packages, to a 6.2 MB closure: stage two. The compiler host loads stage two
and runs its `CompileClosure` on the same sources and arguments, as JSON, and
the bytes must equal stage one's. Stage two then compiles every sample, and
each `Compile` result, closure or rejection, must project to the same JSON as
stage one's; the existing selectors check stage one's against stage zero and
run its closures on bytecode and SLJIT. The run takes about 30 s, with a peak
memory footprint of about 5 GB (macOS arm64, 2026-10-19). Most of that is
stage one, stage zero's build, writing the package compiler's module: that one
`prepared_writer.Write` frame grows to 2.7 GB, against 92 MB for stage two's
build of the same function on the same module. Stage two alone, loaded in a
fresh process, compiles the compiler in 7.9 s with a 3.6 GB peak footprint;
each copy of its 5.6 MB closure is about 224 MB as a `byte[]` of 40-byte
values. Under Debug and the sanitizers one
compile takes about two minutes, about 40% of it validating record
descriptors on every snapshot, so this check runs only on the Release build.

Stage two needs the compiler host's larger closure bounds: the package
compiler's module needs about 113 M units of record-layout work and decodes to
about 80 MB with its verification scratch, past the 64 MiB defaults of every
prepared read, and its 6.2 MB closure, as a `byte[]` of 40-byte values, is
past the host's former 1,000,000-value byte result.
