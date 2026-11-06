# Source tokenizer

`tools/compiler/source_tokenizer` is the first step of a Chibi-written front
end for the compiler and for a future Chibi language server. Tree-sitter, the
parser behind `compiler_syntax_parse`, stays for now; the differential test
keeps the tokenizer and `src/language/grammar.js` in agreement. The tokenizer
is a near one-to-one port of Odin's `core:odin/tokenizer` (`tokenizer.odin`
and `token.odin`); a parser modeled on `core:odin/parser` comes next.

## Entry

`Tokenize(text: string) -> Tokens` reads a whole source text and returns
`Tokens.Scanned` with aligned columns:

- `kinds`, `starts`, `ends`, `rows` and `columns` per token, ending with an
  `EOF` token at the end of the text. Offsets count bytes; rows and columns
  count from zero and columns count bytes, as Tree-sitter's do;
- `errors`, `error_offsets`, `error_rows` and `error_columns` per error. The
  error count is their length.

`KindNames() -> string[]` spells each kind, indexed by kind: `Invalid`, `EOF`,
`Comment`, `Ident`, `Integer`, `Float` and `String`, then each operator as it
is written and each keyword as a word. The package's constants name the kinds
(`Ident`, `Dollar`, `FatArrow` for `=>`, `ColonEq`, `If`, ...), in the order
of Odin's `Token_Kind`. The
keywords are those of `src/language/grammar.js`; since Odin's names for `!`,
`&` and `|` are `Not`, `And` and `Or`, the words `not`, `and` and `or` are
`NotKeyword`, `AndKeyword` and `OrKeyword`. `self`, `min` and `max`, which the
grammar matches only where it expects them, are identifiers. The parser
decides where a keyword can serve as a name, as after `.`.
`join`, `without` and `where` are ordinary identifiers.

## Tokens

The tokens are those of `grammar.js`:

- identifiers `[a-zA-Z_][a-zA-Z0-9_]*`, and keywords found through Odin's FNV-1a
  table, probed linearly because Chibi's keywords collide in its 512 slots;
- numbers: hexadecimal digits after `0x`, or decimal digits with an optional
  fraction and exponent. A fraction needs a digit, so `0..n` stays a range. A
  number with a fraction or exponent is a `Float`;
- strings in double quotes with JSON's escapes, `\uXXXX` included;
- line comments and block comments, which do not nest;
- operators and punctuation by longest match. `>>` and `>=` stay one token
  even where a `>` closes type arguments, as in `Array<Array<int>>` or
  `x: Array<int>= f()`; the parser splits them there. `>=>` reads as `>` and
  `=>`, which nothing else can mean.

## Statement terminators

Like Odin's `insert_semicolon`, a token that can end a statement lets the next
line break end it. The terminator is a zero-width `Semicolon` where
Tree-sitter's external scanner, `src/language/src/scanner.c`, places its
automatic terminator: before `}`, at the end of the text, or after a line
break, which a comment can hold, unless the next line starts with one of
`+-*/%&|^!<>=.,?:([`. It sits before any comments that follow.

A name, literal or closing bracket can end a statement, as can `?`, `>` and
`>>` at the end of a type, the jump words `break`, `continue`, `yield` and
`return`, the expression words `true`, `false`, `nil`, `null`, `state`, `event`,
`entry` and `unordered`, a contextual word such as `from`, which can be a name,
any word after `.`, and the colon of a label. An attribute's name, as in
`@test`, cannot.

Tree-sitter's scanner inserts only where its parser can take a terminator. Odin's
parser instead drops newline semicolons inside expressions by its expression
level. The tokenizer tracks open brackets: it inserts in the file, in blocks,
in declaration bodies and between a prototype's fields, but not in
parentheses, brackets, struct literals or a union's variants. A brace opens a
struct literal inside an expression, after an assignment, a constant's `::` or
the second `:` of a typed constant, `return`, `await`, `in` or an operator
other than `>`, after a `return .Name` outcome, and for a field's value in a
prototype. A prototype's component and a relationship's
body are literals after which nothing ends.

A line that starts with `{` continues the last one, so a body may open on the
next line, as Odin's parser also allows. These lines differ from Tree-sitter,
which the parser has to handle:

- `>` or `>>` as an operator at the end of a line gets a terminator, since the
  tokenizer cannot tell it from the end of type arguments; the parser skips a
  terminator after a binary operator;
- a statement that starts with a struct literal, or a value-less `return`
  followed by a line that starts with `{`, does not end the line before it.

## Errors

Errors follow Odin's tokenizer and its messages: an illegal character
(`illegal character '$': 36`, or `illegal UTF-8 encoding`, one error per
character where Odin adds a second), `String literal not terminated`,
`comment not terminated`, an unknown or unterminated escape, a non-hexadecimal
digit in `\u`, `illegal hexadecimal integer` and
`illegal floating-point exponent`. An unterminated string or comment names its
start, an escape its character and a number the byte after it, as in Odin; an
illegal character names its own offset rather than the next one. Scanning
continues after every error, and an illegal character keeps the terminator rule
of the token before it, as Odin keeps `insert_semicolon`. As in Odin, only ASCII
spacing separates tokens; Tree-sitter's grammar also skips Unicode spaces.

## Subset

The package keeps to the language `docs/package-compiler.md` lists for stage
one: integer, boolean and string values, records of those, a union with array
payloads, arrays, typed integer constants, `uint64` hashing, and the text and
array natives. Stage one has not compiled it yet. Calls and record results cost
more than the scanning itself, so the hot paths read class tables inline and
only numbers, strings, comments and illegal characters return a record. The
text is read once with `text_bytes`.

## Tests

`python3 tools/test_policy.py tool language-source-tokenizer-chibi` compares the
tokens of every `.chibi` file below `packages/`, `test/` and `tools/` with
Tree-sitter's; see `docs/testing.md`.
