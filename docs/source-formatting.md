# Shared Chibi layout formatting

`Chibi::authoring` exports `authoring/source_format.h`. The existing printer is
one authoritative whitespace-only implementation for native and WebAssembly SDK
consumers. It needs an immutable `script_syntax` snapshot, not a catalog, graph,
compiler, controller or editor. All returned source is reparsed and checked for
complete CST equivalence, including exact leaf bytes, comments and field names.
Invalid drafts fail without mutating the input or publishing partial edits.

## Options

Start with `authoring_format_defaults()` and override the fields you need:

- `indent_width`: 1–16 spaces per indentation level; default 4.
- `use_tabs`: emit one tab per indentation level; default false.
- `preferred_width`: 0–4096; default 0 retains existing expression line-break
  choices. A positive width enables grouping and wrapping.

A null options pointer selects the defaults. Invalid widths return an error;
they are never clamped or silently replaced. The existing
`authoring_source_format` entry point delegates to these defaults and keeps its
successful output byte-for-byte unchanged.

Positive widths flatten groups that fit at their actual starting column. Longer
argument lists, parameter lists, arrays and records break after the opening
bracket, at their own commas, and before the closing bracket. Parenthesized
expressions also break after binary operators. Nested groups make their own fit
decisions. Existing expression line breaks inside these groups are replaced by
these deterministic decisions. Statement boundaries and comment-associated line
breaks remain protected.

Width is a preference, not a maximum: a long literal, comment, identifier or an
expression without a supported break opportunity can exceed it. UTF-8 bytes
count as columns; an indentation tab counts as `indent_width` columns. This is
not Unicode display-cell measurement. Output whitespace uses LF. Comment and
literal bytes, including embedded CRLF, are copied exactly. This API does not
sort arguments, shorten literals, or remove keywords.

## Applying edits and moving a caret

`authoring_source_format_with_options` returns malloc-owned text and can also
fill an optional `authoring_format_map`. Its edits are ordered, non-overlapping,
half-open byte ranges in both the original and formatted source. Every replaced
slice is whitespace. There are no edits for unchanged gaps.

For each edit, replace original `[start, end)` with formatted
`[new_start, new_end)`. Apply edits from last to first, or reconstruct the output
from the unchanged spans and replacement slices. Editors should apply the
complete result as one undo transaction only after their own source-generation
check. The map describes precisely this source snapshot; it is not valid after
unrelated edits.

`authoring_format_map_position` maps an offset in `[0, source_size]`. Unchanged
token offsets stay at the same byte within that token. Replaced gap endpoints
map to the corresponding replacement endpoints. For an insertion, or an offset
strictly inside replaced whitespace, `AUTHORING_FORMAT_BEFORE` chooses the
replacement start and `AUTHORING_FORMAT_AFTER` chooses its end. Consumers remain
responsible for supplying valid UTF-8 caret boundaries.

Free text with `free` and the map with `authoring_format_map_free`. Free an old
map before passing it as an output again. Failure leaves the map empty. Existing
`authoring_source_metadata_remap` remains the separate identity/metadata check;
a caret map does not replace its source-hash and CST validation.

## Semantic simplification and integration

The semantic minimizer remains a separate operation with its own catalog,
document identity, semantic equivalence and comment-preservation checks. If a
caller chooses that policy, minimize first, parse the result, then format it.
A layout map describes only the minimizer's output to the formatted text; it does
not map offsets from before semantic rewriting. Do not silently compose those
coordinate spaces or weaken the CST check to accept token deletions.

This SDK slice does not choose a new canonicalization policy, configuration-file
schema, CLI flags, Studio pin or RML/RCSS dependency. Project configuration and
explicit option precedence, atomic CLI write/check integration, editor undo and
original-to-final mapping across semantic rewrites need their own caller review
and validation. No project version bump is needed: this is an additive SDK API
and whitespace-only transformation, with no persisted contract migration.
