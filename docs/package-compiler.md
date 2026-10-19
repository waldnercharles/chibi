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
  its ports' storage and container, and its capability ID and ABI. A port of
  storage 0 takes the type the call infers; -1 marks one the compiler cannot
  pass. The test builds these tables from the compiler host's packs.

## Supported language

Packages of functions, records, unions and constants, and the packages they
import:

- `int`, `bool` and `string` values, records of those, unions whose variants
  carry them, and arrays of any of these, as parameters, results and locals;
  string literals and quoted names decode as JSON strings, as stage zero reads
  them;
- local declarations, assignment and compound assignment; `if`/`else`,
  `while`, `foreach`, `break`, `continue` and `return`;
- integer arithmetic, bitwise and comparison operators; string equality;
  `!`, `&&` and `||` with short-circuit evaluation;
- struct and array literals, where the type is known from the declared local,
  the result or the parameter; field reads; bounds-checked element reads;
- element assignment, compound included, and `xs = array_add(xs, x)` and
  `xs = append(xs, ys)` on a local array, which update it in place;
- union values `U.V` and `U.V(a, b)`, with one positional value per payload
  field, and `switch` over a union with `case .V:`, `case .V(a, b):` binding
  every payload field, and a default `case:`. Bindings are immutable. As in
  stage zero, a switch without a default arm must cover every variant, and a
  repeated variant or default arm is rejected;
- calls between the package's functions, including recursion, and calls of
  natives with one result or none, such as `length` and `slice`. A native's
  inferred ports take the one type its arguments give them, matched by name
  as the catalog matches names;
- `int` constants whose value is an integer literal, `-` allowed, typed
  (`Name: int : 5;`) or not (`Name :: 5;`). A use is the value itself; a local
  hides a constant of the same name;
- imports of `project:` and collection packages such as `core:strings`:
  `alias.F(...)` calls an imported function, `alias.C` reads an imported
  constant, and `alias.T` names an imported record or union in types,
  literals and `alias.U.V` variants. A local hides an alias of the same name.

Anything else is rejected with its source position. Constants of other types
or computed values, `is` patterns, record field defaults, field assignment,
assigning record or array parameters, named payload arguments, literal
variants, natives with several results or variadic ones, `uint64` and
conversions come next.

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
- An element read checks `0 <= index < count`, as stage zero does.
- In-place updates use the array push, extend and store intrinsics on the
  local's header address.
- A `foreach` loop walks its own copy of the array, so the body may change the
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

`tool language-package-compiler-chibi` compiles 47 samples on both compiler
backends (macOS arm64, Debug + ASan/UBSan, 2026-10-19: 6,515 assertions in
34.3 s under the supervisor, with other test runs loading the machine). A sample is package `facts`, optionally with a `lib` package and
`core:strings`. Every accepted closure loads in a fresh runtime host and runs
on bytecode and SLJIT with JSON arguments, and stage zero's own closure of the
same packages must return the same JSON; calls that must fail,
such as out-of-bounds reads, fail on all three. Rejections check the exact
message and position.

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

The bytecode interpreter's dispatch is now 43% of the profile and record
validation 13%. They come before stage one compiles the compiler.
