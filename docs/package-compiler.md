# Authored package compiler

`tools/compiler/package_compiler` is the stage-one compiler as it grows. It
compiles packages' source files to a prepared closure, entirely in Chibi
after the raw Tree-sitter parser: the source snapshot and package graph, the
declarations, typing, lowering and the prepared writer. Stage zero still
builds the compiler itself; stage one is this compiler once it compiles the
compiler sources.

## Entry

`Compile(project: string, paths: string[], sources: string[],
collections: Collection[], target: Target, abi: uint64, names: string[],
natives: Native[], ports: Port[], identities: uint64[]) -> Result` returns
`Compiled(module, bytes)`, a CHIBICLO closure with one module per package, or
`Rejected(diagnostic)`. The files may span several packages; `collections` are
the roots `core:` and other collection imports resolve against. The closure's
root module is the package of the first file. The target supplies what Chibi
cannot see:

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

Packages of functions, records and unions, which may import one another.
`alias.Name` names a function, record or union of an imported package, and
`alias.U.V` one of its union's variants. A local hides an import alias of the
same name from its declaration on; stage zero hides the alias in the whole
block, so the two agree on every program stage zero accepts.

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
  as the catalog matches names.

Anything else is rejected with its source position. Typed constants, `uint64`,
`int64` and `byte` values with conversions, field assignment and the variadic
`format_text` come next, because the compiler's own sources use them. Then
`is` patterns, record field defaults, assigning record or array parameters,
named payload arguments, literal variants and natives with several results.

## Output

Each package compiles to its own prepared module, after the packages it
imports, so a dependency's declarations and bytes are ready first. A module
imports its direct dependencies by module ID and the fingerprint of their
prepared bytes; a call into another package references that import and the
callee's function ID. Imported references are written as the runtime writes
them, so a dependency re-serializes to the bytes its importer fingerprinted.
A module lists the records of every package it reaches, directly or not,
because it may hold their values.

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

Runtime sources record each instruction's origin. Debugger points and watches
are not emitted yet; they must be before stage one becomes the normal compiler.

## Test

`tool language-package-compiler-chibi` compiles 42 samples on both compiler
backends (macOS arm64, Debug + ASan/UBSan, 2026-10-19: 6,345 assertions in
34.4 s). Most are one package; the import samples add a dependency chain
three packages deep, packages declaring the same names, and `core:strings`.
Every accepted closure loads in a fresh runtime host and runs on bytecode and
SLJIT with JSON arguments, and stage zero's own closure of the same packages
must return the same JSON; calls that must fail, such as out-of-bounds reads,
fail on all three. Rejections check the exact message and position.

Compiling is slow: the arrays sample, about 50 lines, takes 1.4 s on SLJIT and
2.8 s on bytecode in this build. Retiring a call no longer re-measures every
spare frame and its data cells, and an array copy checks each distinct record
once, which cut compile time by about an eighth. The remaining cost is the
interpreter itself, per-call setup of large frames, and one record layout check
per record copy; they come before stage one compiles the compiler.
