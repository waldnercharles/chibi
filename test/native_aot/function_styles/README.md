# Function styles

Run `sh test/native_aot/function_styles/check.sh COMPILER LLVM_BIN` to exercise
the actual authored compiler at O0 and O2.

The fixtures check named `fn` definitions, bare written result types, expression
bodies with explicit and inferred results, generic specialization,
void expression bodies, and scoped single-statement `for` bodies with
`break` and `continue`.

The separate rejection fixture checks that a declaration in an unbraced `for`
body remains local to that body. Parser shape and diagnostics are covered by
`tools/compiler/source_parser/tests/FunctionStyles.chibi`. Formatter syntax and
fixed-point checks are in the package-private `source_format/FunctionStyleTests.chibi`.
