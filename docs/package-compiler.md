# Authored package compiler

`tools/compiler/package_compiler` is the stage-one compiler as it grows. It
compiles a package's source files to a prepared closure, entirely in Chibi
after the raw Tree-sitter parser: the source snapshot and package graph, the
declarations, typing, lowering and the prepared writer. Stage zero still
builds the compiler itself; stage one is this compiler once it compiles the
compiler sources.

## Entry

`Compile(project: string, paths: string[], sources: string[], target: Target,
abi: uint64, names: string[]) -> Result` returns `Compiled(module, bytes)`, a
CHIBICLO closure of one module, or `Rejected(diagnostic)`. The target supplies
what Chibi cannot see: pointer width and value-header offsets (`Target`), the
prepared header ABI
(`execution_prepared_abi`) and the instruction names in ordinal order
(`execution_prepared_instruction_name`), which the compiler resolves by name.

## Supported language

One package of functions and records:

- `int`, `bool` and `string` values, and records of those, as parameters,
  results and locals; string literals and quoted names decode as JSON strings,
  as stage zero reads them;
- local declarations, assignment and compound assignment; `if`/`else`,
  `while`, `break`, `continue` and `return`;
- integer arithmetic, bitwise and comparison operators; string equality;
  `!`, `&&` and `||` with short-circuit evaluation;
- struct literals with every field, where the record type is known from the
  declared local, the result or the parameter; field reads;
- calls between the package's functions, including recursion.

Anything else is rejected with its source position. Record field defaults,
field assignment, assigning record parameters, imports, arrays, unions and
natives come next.

## Output

`Body.chibi` emits each function in one pass. A work stack visits statements
and expressions in evaluation order, and each expression pushes its typed
value. Scalar and string parameters move to locals and live in the locals
bank. Records follow stage zero's value model: a record value is a pointer to
its fields, a struct literal calls the runtime's construct intrinsic, and every
store into a local copies the record with the snapshot intrinsic into the data
cell that is the local. Record parameters are read in place. A record result
publishes its whole value header. Pointer slots are listed as roots, so a
suspended caller keeps its records. Calls pass arguments and results through
data cells and suspend the caller once each.
Every statement starts with a budget check, so every loop crosses one. The
definitions are canonical and unoptimized: the runtime verifies and finalizes
them on load. Function and record IDs, record type names and layout hashes
follow stage zero's scheme, so a host invokes the same entries and the
runtime accepts the same record requirements.

Runtime sources record each instruction's origin. Debugger points and watches
are not emitted yet; they must be before stage one becomes the normal compiler.

## Test

`tool language-package-compiler-chibi` compiles 20 sample packages on both
compiler backends (macOS arm64, Debug + ASan/UBSan, 2026-10-19: 2,118
assertions in 12.4 s). Every accepted closure loads in a fresh runtime host
and runs on bytecode and SLJIT with JSON arguments, and stage zero's own
closure of the same package must return the same JSON. Rejections check the
exact message and position.
