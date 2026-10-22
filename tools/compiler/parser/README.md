# Authored parser: first parity slice

`Parser.chibi` owns syntax construction in Chibi. It reuses the public
`source_tokenizer.Tokenize` / `KindNames` APIs and the existing
`syntax_diagnostics.Node` record; it does not call `compiler_syntax_parse`,
Tree-sitter, a process, or an external parser.

This is **not selected by `source_snapshot`**. The existing parser remains the
production path until the complete grammar and recovery contract are covered.
No prepared ABI, project format, native binding, bootstrap, or asset is changed.

## Accepted subset

- A required package header and zero or more imports and literal constants.
- Import aliases, contextual names, optional `pub` on constants.
- Untyped constants with a single number, string, boolean, or nil/null literal.
- Explicit semicolons and tokenizer-produced automatic terminators.
- Comments retained as named extra nodes, including comments within declarations.
- Source offsets and zero-based rows/columns counted in bytes, including UTF-8
  in strings/comments and CRLF inputs.

A successful `Parsed` contains the existing preorder fact contract: node kind,
byte span, byte position, parent/first-child/next-sibling links, error/missing/
has-error flags, field name, named flag and extra flag. Missing links are `-1`.
Hidden automatic terminators extend declaration spans but do not add nodes;
explicit semicolons remain unnamed children. Boolean/nil wrappers retain their
unnamed keyword children. Root spans include trailing whitespace.

`Rejected` returns a code and the existing diagnostic shape with one-based
line/column and document zero. It publishes no partial tree. `syntax` identifies
incomplete/malformed supported declarations; `unsupported` identifies the
current grammar boundary; `lexical` preserves the tokenizer's first diagnostic.
These diagnostics are intentionally **not Tree-sitter recovery parity**. In
particular, tokenizer escape validation can reject strings the raw syntax
parser accepts before semantic validation. No recovery/error-node parity or
full-language acceptance is claimed.

The remaining grammar includes typed constants, unary/compound expressions,
functions, records/unions, behavior/system/component/prototype declarations,
statements, generics and malformed-tree recovery. The existing tokenizer also
has documented parser-facing seams (context-dependent operator splitting and
brace/terminator handling); extending grammar must test those seams rather
than silently assuming lexical tokens are a parse tree.

## Concrete test consumer and temporary APIs

`test/fixtures/authored-parser/Parity.chibi` is the first consumer. Its 22 fixed
positive cases compare **every** fact against `compiler_syntax.Parse`. Ten
negative cases check atomic rejection, diagnostic codes and byte offsets.
`CheckAccepted` and `CheckRejected` return zero on success and a one-based failing
case otherwise; `Parity(text)` returns the first mismatch class/node offset.
The two `@test` functions wrap those checks for the authored test runner.

The fixture needs these project packages: `authored_parser`, `parser`,
`source_tokenizer`, `compiler_syntax`, `syntax_diagnostics`; `core:strings` is the
existing dependency of `syntax_diagnostics`. Only the fixture imports the raw
parser oracle. Build/test dispatcher wiring is a coordinated later change;
placing this fixture here does not make existing aggregate selectors execute it.

Parser phases, contextual-name recognition and append/link columns are private,
not a new public utility library. The append columns are temporary because
current authored record fields cannot contain arrays; they are assembled into
the existing node type once, rather than introducing a second public tree.
Consolidate a builder only when another real syntax producer needs it or the
record/container representation changes. Do not grow a generic parser framework.

## Checkpoint evidence

The initial WIP source and fixture parse without error using the checked-in
`1bc97b7305ee91102d3ee035e4852aa5341df31c` generated grammar and pinned
Tree-sitter `da6fe9beb4f7f67beb75914ca8e0d48ae48d6406`. This is syntax-only
evidence, not an execution/parity pass. Authored compilation and both backend
checks are still required. No C implementation/test wrapper is added by this
slice; the temporary raw-fact oracle is outside the repository.
