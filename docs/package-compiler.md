# Authored package compiler

`tools/compiler/package_compiler` is the stage-one compiler as it grows. It
compiles a package's source files to a prepared closure, entirely in Chibi
after the raw Tree-sitter parser: the source snapshot and package graph, the
declarations, typing, lowering and the prepared writer. Stage zero still
builds the compiler itself; stage one is this compiler once it compiles the
compiler sources.

## Entry

`Compile(project: string, paths: string[], sources: string[], target: Target,
abi: uint64, names: string[], natives: Native[], ports: Port[],
identities: uint64[]) -> Result` returns `Compiled(module, bytes)`, a CHIBICLO
closure of one module, or `Rejected(diagnostic)`. The target supplies what
Chibi cannot see:

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

One package of functions and records:

- `int`, `bool` and `string` values, records of those, and arrays of either,
  as parameters, results and locals; string literals and quoted names decode
  as JSON strings, as stage zero reads them;
- local declarations, assignment and compound assignment; `if`/`else`,
  `while`, `foreach`, `break`, `continue` and `return`;
- integer arithmetic, bitwise and comparison operators; string equality;
  `!`, `&&` and `||` with short-circuit evaluation;
- struct and array literals, where the type is known from the declared local,
  the result or the parameter; field reads; bounds-checked element reads;
- element assignment, compound included, and `xs = array_add(xs, x)` and
  `xs = append(xs, ys)` on a local array, which update it in place;
- calls between the package's functions, including recursion, and calls of
  natives with one result or none, such as `length` and `slice`. A native's
  inferred ports take the one type its arguments give them, matched by name
  as the catalog matches names.

Anything else is rejected with its source position. Record field defaults,
field assignment, assigning record or array parameters, imports, unions,
natives with several results or variadic ones, and conversions come next.

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

`tool language-package-compiler-chibi` compiles 29 sample packages on both
compiler backends (macOS arm64, Debug + ASan/UBSan, 2026-10-19: 4,579
assertions in 18.0 s). Every accepted closure loads in a fresh runtime host
and runs on bytecode and SLJIT with JSON arguments, and stage zero's own
closure of the same package must return the same JSON; calls that must fail,
such as out-of-bounds reads, fail on all three. Rejections check the exact
message and position.

Compiling is slow: the arrays sample, about 50 lines, takes 1.4 s on SLJIT and
2.8 s on bytecode in this build. Retiring a call no longer re-measures every
spare frame and its data cells, and an array copy checks each distinct record
once, which cut compile time by about an eighth. The remaining cost is the
interpreter itself, per-call setup of large frames, and one record layout check
per record copy; they come before stage one compiles the compiler.
