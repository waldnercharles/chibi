# Source parser

`tools/compiler/source_parser` reads Chibi source into the tree
`compiler_syntax.Parse` returns today, without Tree-sitter: the same rows, kinds,
fields, named and extra flags, and byte ranges. It is the replacement for the
generated `src/language/src/parser.c`, its scanner and `grammar.js`. Nothing
calls it yet; switching `compiler_syntax` to it changes the compiler's native
list, so it needs a runtime re-pin and a seed refresh, and comes in its own
change.

## Entry

- `Parse(text) -> Tree` returns `Tree.Parsed(nodes, fields, is_named, is_extra,
  root)` with the same columns as `compiler_syntax.Tree`. Text the grammar does
  not read gives one `ERROR` node over the whole text; recovery that keeps a
  partial tree is not written yet.
- `Diagnose(text) -> syntax.Diagnostic[]` is empty when the text parses.
  Otherwise it holds one diagnostic at the first token the grammar cannot read,
  which says what was expected and what was found, for example
  ``Expected `)`, found `return` `` or ``Expected the end of the statement, found
  `b` ``. A few common mistakes get a fix instead: ``import project:greet``
  reports ``An import names its package in a string; write `import greet
  "project:greet"` ``. Lexical errors, such as an unterminated string, come from
  the tokenizer with its own messages.

## How it works

`source_tokenizer` produces the tokens, including the zero-width terminators
Tree-sitter's external scanner inserts. Every token becomes a leaf row first;
the parser then builds the interior nodes by recursive descent, with precedence
climbing for operators, and adopts the leaves as it goes. A node's range runs
from its first child to its last child, terminators included, as Tree-sitter's
does. Comments are extras: each goes into the innermost node whose range holds it,
and a trailing comment stays with the node that ends before it. Hidden rules
(`_name`, `_expression`, ...) add no row. Contextual words (`package`, `from`,
`not`, ...) become `identifier` where a name is expected, and any word becomes one
after a `.`.

Where Tree-sitter's GLR parser decides by dynamic precedence, the parser reads
tokens ahead (`SkipType`, `SkipBrackets`) before it commits. The cases it has to
match are named arguments against typed arguments, a function against a
constant that starts with parentheses, a label against a typed local, `return`
with an argument list against a parenthesized expression, and `not (` as a call.

## Files

- `Tree.chibi`: rows, cursor, adding children, expected-token messages.
- `Types.chibi`: types, units and measures.
- `Expressions.chibi`: operators, postfix forms, literals, arguments.
- `Statements.chibi`: blocks and statements.
- `Declarations.chibi`: files, packages and every declaration form.
- `Parser.chibi`: the entry points, comment placement and the final row order.

## Tests

`chibi build test language-source-parser-chibi` parses every
`.chibi` file below `packages/`, `test/` and `tools/` and the sources in
`test/language/syntax_corpus` (`*.chibi.txt`, so that package scans skip them)
with both parsers and compares the trees row by row in
`test/language/SourceParserDiff.chibi`; text Tree-sitter rejects must be
rejected too. The corpus has constructs the repository does not use, and some
invalid sources on purpose.
