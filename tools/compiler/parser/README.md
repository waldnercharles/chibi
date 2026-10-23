# Authored parser: bounded parity slices

`Parser.chibi` owns syntax construction in Chibi. It reuses the public
`source_tokenizer.Tokenize` / `KindNames` APIs and the existing
`syntax_diagnostics.Node` record; it does not call `compiler_syntax_parse`,
Tree-sitter, a process, or an external parser.

This is **not selected by `source_snapshot`**. The existing parser remains the
production path until the complete grammar and recovery contract are covered.
No prepared ABI, project format, native binding, bootstrap, or asset is changed.

## Accepted subset

- A required package header and zero or more imports, literal constants and bounded ordinary functions.
- Import aliases, contextual names, optional `pub` on constants.
- Untyped constants with a single number, string, boolean, or nil/null literal.
- Ordinary functions with empty or explicitly typed parameters, one explicit
  unqualified result type, and exactly one return of a literal or identifier.
  Optional `pub`, trailing parameter comma and final declaration semicolon are
  accepted; type/name resolution is deliberately not performed.
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
current grammar boundary; `lexical` preserves the tokenizer's first diagnostic; `input` rejects embedded
NUL before tokenization, including NUL inside strings and comments.
These diagnostics are intentionally **not Tree-sitter recovery parity**. In
particular, tokenizer escape validation can reject strings the raw syntax
parser accepts before semantic validation. No recovery/error-node parity or
full-language acceptance is claimed.

The remaining grammar includes typed constants, unary/compound expressions,
function bodies beyond a single atom return, records/unions,
behavior/system/component/prototype declarations, generics and malformed-tree recovery. The existing tokenizer also
has documented parser-facing seams (context-dependent operator splitting and
brace/terminator handling); extending grammar must test those seams rather
than silently assuming lexical tokens are a parse tree.

## Ordinary function boundary

```text
[pub] name :: ([name : type (, name : type)* [,]]) -> type
{ return atom terminator } [;]
```

Here `type` is one unqualified ordinary/contextual name and `atom` is one
identifier, number, string, boolean or nil/null literal. No arrow, an empty
body, a bare return, `=>`, attributes, parameter defaults/comptime/source
clauses, traits, measured/qualified/container/generic/union types, named/multiple
results, unary/compound expressions, calls and additional statements reject
atomically. The lexical identifier `self` is not accepted as an ordinary
parameter name or lone type: the raw grammar treats it specially there. A
function named `self` or return value `self` retains the raw identifier facts.

A return can end explicitly or automatically, including immediately before
`}` on the same line. A real semicolon between result type and body rejects;
newlines/comments before the body are trivia. The tokenizer's coarse brace
heuristic treats a body after contextual result type `not` as a literal. The
parser repairs only the known return/body boundary: missing implicit return
ends before `}` and its preceding comments, and a synthetic post-body terminator
is discarded. Real semicolons are never discarded. A real return semicolon
after a line break rejects rather than accepting an empty statement.
This is private bounded syntax handling, not a token rewriting layer or API.

Comments after a simple parameter type belong to its parameter list, and after
a result type to the function. Comments after a body belong to the function
only when its optional declaration semicolon follows; otherwise they are file
siblings. These distinctions are part of the complete fact comparison.

## Concrete test consumer and temporary APIs

`test/fixtures/authored-parser/Parity.chibi` is the first consumer. Its 22 fixed
positive cases compare **every** fact against `compiler_syntax.Parse`. Twelve
negative cases check atomic rejection, diagnostic codes, byte offsets and exact
line/column positions. All 66 byte prefixes of a fixed 65-byte source check
fail-closed diagnostics or full accepted-fact parity, including UTF-8 truncation.
`CheckAccepted` and `CheckRejected` return zero on success and a one-based failing
case otherwise; `Parity(text)` returns the first mismatch class/node offset.
`Check` combines the foundation sets (negative failures add 100; prefix failures
add 200). `FunctionCases` adds 26 accepted cases, `FunctionRejections` adds 30
unsupported/malformed cases, and `CheckFunctionPrefixes` checks every byte
prefix of a UTF-8 function with contextual names and a trailing comment.
Their failures add 400, 500 and 600 respectively. These pure entry points
run through the existing compiler-host; they do not need an assertion native.

The fixture needs these project packages: `authored_parser`, `parser`,
`source_tokenizer`, `compiler_syntax`, `syntax_diagnostics`; `core:strings` is the
existing dependency of `syntax_diagnostics`. Only the fixture imports the raw
parser oracle. The [authored self-host aggregate](../../../test/selfhost/README.md)
invokes `Check` in the compiler-recovery workflow after stage-two recovery.
Existing Make/test-policy aggregate selectors are not wired by this change.

Parser phases, contextual-name recognition and append/link columns are private,
not a new public utility library. The append columns are temporary because
current authored record fields cannot contain arrays; they are assembled into
the existing node type once, rather than introducing a second public tree.
Consolidate a builder only when another real syntax producer needs it or the
record/container representation changes. Do not grow a generic parser framework.

## Function-slice WIP evidence

The first bounded function checkpoint compiles six authored sources using the
unchanged `fa1b7b2` compiler seed
`41905e8bc4afc3998cf48097a98d6c1ca0ce89ff2de6a6dadcffb6fb06f1c71e`
and the host below. `Check` returns `[0]` on both bytecode and SLJIT, with 254,679
selected-backend calls and zero fallback/overflow. The fixture SHA256 is
`241be56e0718e0f3460f99f6b0aad52a1a45c645823920def909d7fa28c59892`.
Independent full-fact/property review and additional negative controls remain
pending; this is an early WIP source checkpoint, not a production parser switch.

## Historical foundation checkpoint evidence

On Linux x64, the pure fixture compiles through the unchanged checked-in
compiler and checksum-pinned compiler host. The final source hashes are:

- `Parser.chibi`: `c3797ae42dc42ab8c8db0fd11715e744fea5782bd108ac01adccb41f2f54798f`
- `Parity.chibi`: `c3fbb8d453477fa6d4b5a8b674013541e6fde1cee90299afa587ee3f1949a8bd`
- Compiler seed: `2fcbdbaf6b999d87b119a8e6195752dc2b5aca18016e9349b9f6ae1a5bfa2b37`
- Linux host: `c8b1720c610c83aadb7048c22e251109ec7052b62675dc67d391ec2136ac9632`
- Compiled fixture: `1fbd185b4e4f203fb89b585695a998904cf4d89655c43b8134ed69e64c0a1b9e`

`Check` returns `[0]` on both bytecode and SLJIT. Host counters report 105,691
calls in the selected backend, zero calls in the other backend and zero
fallback/overflow. The input is six source files: this fixture, this parser,
`Tokenizer.chibi`, `compiler_syntax/Syntax.chibi`, `Diagnostics.chibi`, and
`core:strings/Strings.chibi`. Invoke the root identity
`project:authored_parser:1:Check:callable` with argument JSON `[]` after compiling
that project closure. The fixture folder must be mapped to `authored_parser`.

Two negative controls were observed before restoring the final sources:

- The first embedded-NUL regression returned `[111]` before the explicit input
  check was added; the final implementation rejects it at byte 22, line 2,
  byte column 8. A second case checks the column after a multibyte character.
- Forcing root start to zero in an isolated scratch copy returns `[3]`, detecting
  the leading-whitespace span mismatch. The restored source passes again.

The source and fixture also parse without errors under main's grammar at
`1bc97b7305ee91102d3ee035e4852aa5341df31c` with pinned Tree-sitter
`da6fe9beb4f7f67beb75914ca8e0d48ae48d6406`. No C implementation/test wrapper is
added by this slice; the temporary raw-fact oracle is outside the repository.

This is focused, Linux-only evidence. Current aggregate wiring is described
above; this historical checkpoint does not establish production integration,
legacy Make/test-policy dispatcher wiring, full grammar/recovery, source-free
self-rebuild or other-platform execution. Final integrated candidates still
require independent review and current execution evidence.
