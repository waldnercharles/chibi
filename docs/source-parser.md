# Source parser

`tools/compiler/source_parser` reads Chibi source into the syntax tree the
compiler reads, without Tree-sitter: the rows, kinds, fields, named and extra
flags, and byte ranges Tree-sitter gave. It replaced the generated
`src/language/src/parser.c`, its scanner and `grammar.js`, and
`compiler_syntax.Parse` forwards to it.

## Entry

- `Parse(text) -> Tree` returns `Tree.Parsed(nodes, fields, is_named, is_extra,
  root)`, which `compiler_syntax.Parse` forwards. Kinds and fields are
  the integer constants of `syntax_diagnostics` (`syntax.CallExpression`,
  `syntax.FieldName`), with `KindNames` and `FieldNames` spelling them as the
  grammar does, and `KindName` spells any kind; a token leaf keeps its
  tokenizer kind (`tk.OpenParen`). Text the grammar does not read gives
  `Tree.Rejected(diagnostic)`: one diagnostic at the first token the grammar
  cannot read, which says what was expected and what was found, for example
  ``Expected `)`, found `return` `` or ``Expected the end of the statement, found
  `b` ``, cut to a diagnostic row's 191 bytes. A few common mistakes get a fix
  instead: ``import project:greet`` reports ``An import names its package in a
  string; write `import greet "project:greet"` ``. Lexical errors, such as an
  unterminated string, come from the tokenizer with its own messages. Recovery
  that keeps a partial tree is not written yet.

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

`chibi build test chibi-test-compiler` runs `tools/compiler/source_parser/tests`:
constructs the repository does not use, from the former Tree-sitter corpus, the
rejections and their positions, and the field and kind of the nodes some sources
give, named as the grammar names them. The compiler parses itself and the
samples on every compile.
