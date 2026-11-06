# Single-statement control bodies

Run the actual authored compiler at O0 and O2:

```sh
CHIBI_CORE="$PWD/packages/core" /path/to/chibi test \
  test/native_aot/control_bodies --llvm /path/to/llvm/bin -O0
CHIBI_CORE="$PWD/packages/core" /path/to/chibi test \
  test/native_aot/control_bodies --llvm /path/to/llvm/bin -O2
```

The fixtures check nearest-if `else` binding, single-statement boundaries,
block-scoped declarations, nested `break` and `continue`, comments, and mixed
braced/unbraced bodies. Two rejection fixtures ensure body-local declarations
are unavailable after the body. Parser diagnostics and tree structure are covered by
`tools/compiler/source_parser/tests`.
