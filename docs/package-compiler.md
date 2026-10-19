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
what Chibi cannot see: layout offsets (`Target`), the prepared header ABI
(`execution_prepared_abi`) and the instruction names in ordinal order
(`execution_prepared_instruction_name`), which the compiler resolves by name.

## Supported language

One package of functions with `int` and `bool` parameters and results; local
declarations, assignment and compound assignment; `if`/`else`, `while`,
`break`, `continue` and `return`; integer arithmetic, bitwise and comparison
operators; `!`, `&&` and `||` with short-circuit evaluation; and calls between
the package's functions, including recursion. Anything else is rejected with
its source position. Imports, records, arrays, strings, unions and natives
come next.

## Output

`Body.chibi` emits each function in one pass. A work stack visits statements
and expressions in evaluation order, and each expression pushes its typed
value. Parameters move to locals; scalars live in the locals bank; calls pass
arguments and results through data cells and suspend the caller once each.
Every statement starts with a budget check, so every loop crosses one. The
definitions are canonical and unoptimized: the runtime verifies and finalizes
them on load. Function IDs follow stage zero's scheme, so a host invokes the
same entries.

Runtime sources record each instruction's origin. Debugger points and watches
are not emitted yet; they must be before stage one becomes the normal compiler.

## Test

`tool language-package-compiler-chibi` compiles nine sample packages on both
compiler backends (macOS arm64, Debug + ASan/UBSan, 2026-10-19: 863
assertions in 6.7 s). Every accepted closure loads in a fresh runtime host and
runs on bytecode and SLJIT, and stage zero's own build of the same package must
return the same results. Rejections check the exact message and position.
