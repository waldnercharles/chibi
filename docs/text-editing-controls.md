# Authored text editing

`<code-editor>` is a reusable RmlUi text control. It owns platform text input,
selection, composition, scrolling, clipboard access and token painting. Documents,
file operations, undo grouping, completion choices and keyboard modes belong to
its authored consumer.

The control accepts `value`, `selection-start`, `selection-end`,
`selection-caret`, `readonly`, `disabled`, `wrap` and `highlights`. Selection
positions are UTF-8 byte offsets; `selection-caret` preserves which end is active.
Highlights are a JSON array of ordered, non-overlapping byte ranges with `start`,
`end` and `classes` (or `kind`). Invalid ranges fall back to ordinary text. Source
and highlighted runs are literal text, never parsed as markup.

`code-input`, `code-selection`, `code-command` and `code-commit` carry `value`,
`start`, `end`, `caret`, `line`, `column` and `command`. Line and column are
one-based logical positions. Native editing emits the full current value;
consumers must preserve it until their document service acknowledges adoption.
A successful event send does not acknowledge a document edit.

`request-command` accepts `copy`, `cut`, `paste` and `selectAll`.
Increment `request-sequence` for each request, including repeated commands.
`completion-open` routes completion navigation to commands; `completion-list`
identifies the authored list whose selected option should remain visible.

`keyboard-mode="delegated"` suppresses native key/text editing and emits:

- `code-key`: `key`, `control`, `shift`, `alt`, `meta`, `caret`, `buffer`.
- `code-text`: `glyph`, `caret`, `buffer`.

Modifier values are `0` or `1`; key names use the same canonical names as
`data-keys`. `glyph` contains the actual text input, preserving the keyboard
layout. The consumer defines the editing policy. Explicit clipboard requests
continue to work in delegated mode. Read-only controls suppress text mutations.

The existing text capability pack provides CPU operations independent of a UI:

| Function | Purpose |
| --- | --- |
| `text_cursor` | Character, word, WORD, line, content-start and document motions |
| `text_line` | Start/end/next boundaries and logical position |
| `text_slice` | Owned UTF-8 range |
| `text_replace` | Owned range replacement |
| `text_difference` | Minimal UTF-8 splice and removed/inserted character counts |
| `text_prefix_lines` | Linear-time selected-line prefix add/remove/toggle |

These functions bound text and results to 1 MiB and reject malformed UTF-8.
Motions count Unicode code points, not grapheme clusters. Combining marks can
therefore take separate character steps. Offsets inside a code point round down;
CRLF remains two preserved bytes and one logical line break. Text operations do
not normalize source or own a document's history.

Native regression selectors are `--code-control` (including headless RmlUi
input) and `--workspace-services` (CPU text operations). They establish control
and primitive behavior, separately from an authored editor's runtime acceptance.
