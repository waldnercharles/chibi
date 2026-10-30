# Chibi source formatting

`tools/compiler/source_format` formats Chibi source as a whitespace-only pass
over the stage-one parser's tree. It replaces the C formatter archived with
`chibi fmt` on 2026-10-29: the layout rules are the same, now written in Chibi
over `source_parser`'s tree, which keeps every comment as an extra with its byte
range. The package is part of the compiler tree, so the compiler can format its
own sources without C.

```chibi
format.Format(text: string, options: format.Options) -> format.Result
```

`Result` is `Formatted(text)` or `Rejected(message)`.

## What it changes

Only the bytes between tokens. Each gap becomes one of: nothing, one space, or
a line break followed by a margin.

- Line breaks the author wrote stay, apart from the ones the grammar forces
  after `{`, a statement or declaration, and before a closing brace.
- A run of blank lines shrinks to `max_blank_lines`.
- Indentation follows the nesting of blocks and brackets. A bracket that ends
  its line indents the next lines one level past that line; a bracket followed by
  an item aligns later lines with that item. A continued binary expression aligns
  with its first operand. With `use_tabs`, levels are tabs and alignment is spaces.
- Output has one line-end style, no trailing whitespace and one final line end.
- Comment and literal bytes are copied exactly.

## Options

| Field | Default | Meaning |
| --- | --- | --- |
| `indent_width` | 4 | Spaces per level, 1 to 16. Also the width a tab counts for when aligning. |
| `use_tabs` | false | One tab per level. |
| `max_blank_lines` | 1 | The most blank lines kept between two lines, 0 to 3. |
| `crlf` | false | CRLF between lines instead of LF. Bytes inside comments and strings are copied exactly. |

An invalid width or blank-line count is rejected, never clamped. Wrapping to a preferred line width
is not implemented yet; the archived formatter's `preferred_width` returns in a
later change.

## chibi fmt

```sh
chibi fmt [--check | --staged] [PATH...]
```

Formats `.chibi` files in place with the defaults. A PATH is a file or a
directory, which stands for the files git tracks below it; none means the
working directory. `--check` writes nothing, prints each file that would
change and exits 1 if any would, or if any file cannot be formatted.
`--staged` formats the files staged for commit and stages the result; a file
with unstaged edits is only checked, since staging it would stage those edits.
`.githooks/pre-commit` runs it with the `chibi` it finds in `build/sdk`,
`build-release/sdk` or `PATH`, so enable the hooks once with
`git config core.hooksPath .githooks`.

The command runs `apps/fmt` as a program of its own, installed in
`share/chibi/apps/fmt` with the formatter's packages beside it, so that no other
command compiles them. Costs on this checkout (Apple M-series, release SDK):

- Start: about 23 s, all of it compiling the formatter's packages (stage zero
  compiles about 3 s per thousand lines).
- Formatting: about 3 s for the 4,000-line `Body.chibi`; parsing dominates.
- Memory: a program's garbage is collected only between runs, and a parse costs
  about 3 KB of heap per source byte, so one process formats at most 400 KB of
  source. A larger job runs its first batch itself and each later batch in a
  child process, and each child pays the start again.
- The whole tree, 859 files and 4 MB, takes about 5 minutes with `--check`.

First whole-tree result (2026-10-30): 369 of 859 files would change, 3 cannot
be formatted. 351 of the 369 are the dense one-line fixtures in
`test/compiler/samples` (553 files); ten are in `tools/compiler`, eight elsewhere. The other 490
files, among them `packages/core`, `archive` and `test/fixtures`, are already
formatted. The three that cannot be formatted are fixtures in
`test/compiler/samples/fallible`, where a line break after a `{}` block changes
the tree.

## Safety

The result is tokenized again and compared with the input's tokens: the same
kinds in the same order, zero-width statement terminators included, and the same
bytes in every token. Anything else is `Rejected("Formatting would change the
token stream")`, and the input is untouched. The parser reads only the token
stream, so an identical stream gives an identical tree, comments included; the
check needs no second parse, which dominates the formatter's memory (about
1.4 KB of heap per source byte). Source with syntax errors, or over
`format.MaxBytes` (8 MiB), is rejected before any layout.

Empty source is returned unchanged; the archived formatter wrote a lone newline.
