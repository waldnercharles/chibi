# Documentation highlighting

Chibi fences are highlighted at build time by `tools/compiler/source_highlight`,
which the documentation renderer (`tools/docs/markdown`, archived with the site)
and the formatter playground (`tools/docs/playground`) share. The renderer
consumes source byte ranges, escapes the original text and inserts spans,
closed at each line end; it never reconstructs source from token spellings. Other languages
remain escaped plain text. No browser highlighter, CDN or new grammar is used.

## Syntax roles

Both current compiler stages parse with `src/language/grammar.js` through
Tree-sitter. The docs reuse the existing `compiler_syntax.Parse` adapter and
its node kinds, fields and ranges. This is a temporary dependency: the
long-term goal is an authored Chibi parser, including tooling. It does not
commit that parser to Tree-sitter's implementation or tree representation.

The scope model follows established syntax highlighters, including
[OLS's Odin TextMate grammar](https://github.com/DanielGavin/ols/blob/ae85feb27b6e26e14fd7593a36b11f48f693ed38/editors/vscode/syntaxes/odin.tmLanguage.json)
and [Tree-sitter Odin's queries](https://github.com/tree-sitter-grammars/tree-sitter-odin/blob/d2ca8efb4487e156a60d5bd6db2598b872629403/queries/highlights.scm).
Those grammars are references, not copied syntax definitions. The Chibi
classifier uses the actual Chibi parse tree, not uppercase-name guesses or
the assumption that every receiver before a dot is a namespace.

| Chibi role | Source context | Theme scope |
| --- | --- | --- |
| namespace | Package/library declaration names and explicit import aliases | `entity.name.namespace` |
| type | Type declarations, binders and names in type syntax | `entity.name.type` |
| builtin | Builtin spellings within type syntax | `support.type` |
| function | Callable declarations and syntactic call targets | `entity.name.function` |
| property | Member/field names and argument labels; deliberately neutral | `variable.other` |
| constant | Constant declarations, union variants and literal constants | `variable.other.constant`, `constant.language` |
| attribute | Attribute names | `entity.other.attribute-name` |
| keyword | Grammar keywords | `keyword`, `storage` |
| operator | Operators and declaration bindings | `keyword.operator` |
| number | Numeric literals | `constant.numeric` |
| string | String and character literals | `string` |
| comment | Line and block comments | `comment` |

Unresolved identifiers, variables and parameters keep the theme foreground.
A call target is a syntactic function role, not proof that name resolution
finds a function. A bare reference to a type or constant is not recognized
through its spelling alone. Imported receivers can be shadowed; they are not
globally recolored as namespaces. Semantic name resolution would require
imports and declarations that an example fragment need not contain.

## Fragments and validation

The parser gives no tree for text it cannot read. For a fragment that does
not parse as a complete source file, the renderer also tries an isolated
package header and then an empty trailing body. These contexts let
declaration-only reference signatures acquire their normal roles. The first
text that parses gives the roles; a fragment no context makes readable
renders as escaped plain text. There are at most three parses.
Synthetic prefix/suffix bytes never appear in rendered output. Unexpected
overlapping or cross-boundary leaf ranges fall back to escaped plain text.

This is presentation only. The docs compiler still validates the **original
snippets** independently, including package analysis, with the original
standalone native catalog. Parser helper natives are available only to the
renderer, not to examples. Highlighting never makes a rejected example pass.

`test/language/docs_markdown.c` records representative role expectations,
fragment recovery, escaping and exact source round trips on both execution
backends. These fixtures are a reusable comparison baseline when the authored
parser replaces this adapter. They do not establish full language/parser
parity: the compiler's syntax, diagnostics and language corpus need their own
differential coverage.

## GitHub Light Default / Dark Default

The code palette and role mapping come from
[GitHub VS Code Theme 6.3.5](https://github.com/primer/github-vscode-theme/tree/cd78e5e4e7bcf132a6f428ae0f32264bb1b729cf),
specifically `src/theme.js` and `src/colors.js`, and its pinned dependency
[Primer primitives 7.10.0](https://github.com/primer/primitives/tree/f82864eb33c37f8624704bd996bc21b97d3c311b).
The site uses the exact Light Default and Dark Default colors, including code
foreground/background, rather than approximating that palette. Site chrome
is unchanged. Neutral roles intentionally share the code foreground.

The complete MIT notice is in `third_party/github-theme/NOTICE` and embedded
in generated CSS so the public output retains both upstream attributions.
Theme tests check exact role colors and contrast against each code background.
The system color preference selects the theme; highlighting adds no script.
