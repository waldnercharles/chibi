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
- A run of blank lines becomes one blank line.
- Indentation follows the nesting of blocks and brackets. A bracket that ends
  its line indents the next lines one level past that line; a bracket followed by
  an item aligns later lines with that item. A continued binary expression aligns
  with its first operand. With `use_tabs`, levels are tabs and alignment is spaces.
- Output has LF line ends, no trailing whitespace and one final newline.
- Comment and literal bytes are copied exactly.

## Options

| Field | Default | Meaning |
| --- | --- | --- |
| `indent_width` | 4 | Spaces per level, 1 to 16. Also the width a tab counts for when aligning. |
| `use_tabs` | false | One tab per level. |

An invalid width is rejected, never clamped. Wrapping to a preferred line width
is not implemented yet; the archived formatter's `preferred_width` returns in a
later change.

## Safety

The result is parsed again and compared with the input's tree: the same depth,
kind, named flag and field for every row that has width, and the same bytes in
every leaf. Anything else is `Rejected("Formatting would change the syntax
tree")`, and the input is untouched. Source with syntax errors, or over
`format.MaxBytes` (8 MiB), is rejected before any layout.

Empty source is returned unchanged; the archived formatter wrote a lone newline.
