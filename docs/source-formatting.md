# Chibi source formatting

`tools/compiler/source_format` formats Chibi source with a pass over the
stage-one parser's tree. It replaces the C formatter archived with `chibi fmt`
on 2026-10-29: with the default options the layout rules are the same, now
written in Chibi over `source_parser`'s tree, which keeps every comment as an
extra with its byte range. The package is part of the compiler tree, so the
compiler can format its own sources without C.

```chibi
format.Format(text: string, options: format.Options) -> format.Result
format.FormatParsed(text, rows, fields, named, root, options) -> format.Result   // text already parsed
format.ParseOptions(base: format.Options, pairs: string) -> (options: format.Options, error: string)
```

`Result` is `Formatted(text)` or `Rejected(message)`. `ParseOptions` reads
`NAME=VALUE` pairs separated by spaces or line breaks, with `#` comments, over
`base` (usually `format.Defaults()`), and validates the result.

## What it changes

The bytes between tokens, and with `semicolons` set, explicit `;`. Each gap
becomes nothing, spaces, or a line break followed by a margin.

- Line breaks the author wrote stay, apart from the ones the grammar forces
  after `{`, a statement or declaration, and before a closing brace, and the
  ones wrapping chooses when `preferred_width` is set.
- Indentation follows the nesting of blocks and brackets. A bracket that ends
  its line indents the next lines past that line; a bracket followed by an item
  aligns later lines with that item. A continued binary expression aligns with
  its first operand. With `use_tabs`, levels are tabs and alignment is spaces.
- Output has one line-end style, no trailing whitespace, and a final line end
  unless `final_newline` is off.
- Comment and literal bytes are copied exactly.
- Code between a `// chibi fmt: off` comment and a `// chibi fmt: on` comment
  keeps its spacing as written.

## Options

Defaults reproduce the archived formatter, so already formatted code stays
byte for byte. [The options survey](formatter-options-survey.md) compares each
with the same setting in 19 other formatters; the playground explains them
next to the result.

| Name | Values (default) | Meaning |
| --- | --- | --- |
| `preferred_width` | 0, 20 to 400 (0) | The column wrapping aims for. 0 keeps the author's breaks inside brackets. Otherwise a bracket group that fits stays on one line, and one that does not breaks after the bracket, after each comma of its own list and before the closer; parenthesized operands break after their operators. |
| `magic_trailing_comma` | bool (true) | With wrapping, a group whose last item ends in a comma always breaks. |
| `use_tabs` | bool (false) | One tab per level. |
| `indent_width` | 1 to 16 (4) | Columns per level; with tabs, the width a tab counts for when aligning. |
| `continuation_indent` | 0 to 16 (0) | Columns for a continued line that is not aligned; 0 is one level. |
| `align_after_open_bracket` | bool (true) | Continued lines inside a bracket align with its first item; off, they take the continuation. |
| `align_operands` | bool (true) | Continued operands align with the first operand; off, they take the continuation. |
| `indent_case_labels` | bool (true) | `case` labels one level inside their `switch`; off, level with it. |
| `space_in_braces` | bool (false) | Struct literals as `{ a: 1 }`. |
| `space_in_parens` | bool (false) | Calls, parameter lists and grouping as `( a, b )`. |
| `space_in_brackets` | bool (false) | Array literals as `[ 1, 2 ]`. |
| `spaces_before_trailing_comment` | 1 to 4 (1) | Spaces before a comment that ends its line. |
| `max_blank_lines` | 0 to 3 (1) | Longer runs of blank lines shrink to this. |
| `blank_lines_between_declarations` | 0 to 3 (0) | The fewest blank lines between top-level declarations when either spans lines; 0 adds none. A comment directly above a declaration belongs to it. |
| `keep_blank_lines_at_block_edges` | bool (true) | Off removes blank lines just inside braces. |
| `empty_blocks` | keep, collapse, expand (keep) | `{}` as written, collapsed, or over two lines. |
| `keep_one_line_blocks` | bool (false) | A block written on one line stays on one line. |
| `align_assignments` | bool (false) | Operators of assignments on consecutive lines line up. |
| `align_field_types` | bool (false) | Types of consecutive record and component fields line up. |
| `align_constants` | bool (false) | Types of consecutive typed constants line up. |
| `align_struct_literal_values` | bool (false) | Values of a struct literal written one field per line line up. |
| `align_trailing_comments` | bool (false) | Trailing comments on consecutive lines start in one column. |
| `semicolons` | keep, omit, always (keep) | Explicit `;` as written, dropped where a line break ends the statement, or always written. Package and import lines never take one. |
| `line_ending` | lf, crlf (lf) | Between lines; `Options.crlf` in the API. |
| `final_newline` | bool (true) | The text ends with a line break. |

Alignment pads runs of consecutive lines with the same margin; a blank line or
a line without the anchor ends a run. An invalid value is rejected, never
clamped.

Not supported yet, each because the token check must first allow exactly that
change: trailing commas, import sorting, removing redundant parentheses, and
formatting a line range.

## chibi fmt

```sh
chibi fmt [--check | --staged] [--option NAME=VALUE]... [PATH...]
```

Formats `.chibi` files in place. Options come from the defaults, then a
`.chibi-format` file of `NAME=VALUE` lines at the repository root, then
`--option` flags. A PATH is a file or a directory, which stands for the files
git tracks below it; none means the working directory. `--check` writes
nothing, prints each file that would change and exits 1 if any would, or if any
file cannot be formatted. `--staged` formats the files staged for commit and
stages the result; a file with unstaged edits is only checked, since staging it
would stage those edits. `.githooks/pre-commit` runs it with the `chibi` it
finds in `build/sdk`, `build-release/sdk` or `PATH`, so enable the hooks once
with `git config core.hooksPath .githooks`.

The SDK build compiles `apps/fmt`, with the formatter's packages beside it,
and the launcher embeds the closure; the command runs it as a program of its
own (`program.embedded`), so that no other command loads it. Costs on this
checkout (Apple M-series, release SDK, measured 2026-10-30 at a load average
of about 35 on 14 cores):

- Start: about 0.1 s (`chibi fmt --help`; `chibi version` takes 20 ms), with
  nothing compiled: loading the 2.7 MB closure takes most of it. Compiling the
  program is a step of the SDK build: about 12 s of CPU on the compiler host's
  SLJIT, 38 s on bytecode, where a start used to spend 26 s or more. It takes
  about 45 million calls and close to the 4 billion instructions a package
  closure compiled at run time may use (`CLOSURE_COMPILE_BUDGET` in
  `src/authoring/asset_compiler_host.c`), roughly 500,000 interpreted
  instructions per source line of the closure. Adding the 330-line
  highlighter to it exceeded that budget, so the playground's host is a
  separate program.
- Formatting: about 3 s for the 4,000-line `Body.chibi`; parsing dominates.
  One small file takes about 0.25 s in all, 40 ms of it for `git rev-parse`.
- Memory: a program's garbage is collected only between runs, and a parse costs
  about 3 KB of heap per source byte, so one process formats at most 400 KB of
  source. A larger job runs its first batch itself and each later batch in a
  child `chibi fmt`.
- The whole tree, 914 files and 4 MB, takes about 2 minutes with `--check`
  (95 s of CPU), formatting nearly all of it.

After a change to the formatter, `tools/build.sh build --release --target
chibi` compiles the program again; the packages' own tests run with `chibi test
tools/compiler`.

First whole-tree result (2026-10-30): 369 of 859 files would change, 3 cannot
be formatted. 351 of the 369 are the dense one-line fixtures in
`test/compiler/samples` (553 files); ten are in `tools/compiler`, eight
elsewhere. The other 490 files, among them `packages/core`, `archive` and
`test/fixtures`, are already formatted. The three that could not be formatted
were fixtures in `test/compiler/samples/fallible` whose `if p^ {}` could not be
followed by a line break: the tokenizer read the `^` of a postfix dereference
as binary xor and opened a struct literal at the brace. That is fixed.

## Safety

The result is tokenized again and compared with the input's tokens: the same
kinds in the same order, zero-width statement terminators included, and the same
bytes in every token. An explicit `;` and the terminator a line break inserts
are the same token to the parser and compare equal. Where only terminators
after a closing brace differ, the output is parsed and its tree compared.
Anything else is `Rejected`, and the input is untouched. The parser reads only
the token stream, so an identical stream gives an identical tree, comments
included; the check needs no second parse, which dominates the formatter's
memory. Source with syntax errors, or over `format.MaxBytes` (8 MiB), is
rejected before any layout.

Empty source is returned unchanged; the archived formatter wrote a lone newline.

## Playground

`tools/docs/playground` shows the formatter on the repository's sources with
any options, highlighted by `tools/compiler/source_highlight` as the
documentation site highlights code; see its README to run it.
