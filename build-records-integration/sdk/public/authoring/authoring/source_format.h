#ifndef CHIBI_AUTHORING_SOURCE_FORMAT_H
#define CHIBI_AUTHORING_SOURCE_FORMAT_H

#include "language/syntax.h"

#include <stddef.h>

typedef struct
{
    int indent_width;    /* 1..16 spaces per level (or tab display width). */
    int preferred_width; /* 0 preserves line breaks; otherwise 1..4096 columns. */
    bool use_tabs;       /* One tab per indentation level, no alignment tabs. */
} authoring_format_options;

authoring_format_options authoring_format_defaults();

typedef struct
{
    int start, end;         /* Half-open byte range in the original source. */
    int new_start, new_end; /* Replacement range in the returned text. */
} authoring_format_edit;

typedef struct
{
    authoring_format_edit *edits;
    int count, source_size, formatted_size;
} authoring_format_map;

typedef enum
{
    AUTHORING_FORMAT_BEFORE,
    AUTHORING_FORMAT_AFTER,
} authoring_format_affinity;

/* Whitespace-only formatting of a complete canonical syntax snapshot. Returns
 * malloc-owned text. Comments and literal bytes remain exact; the snapshot is
 * borrowed and unchanged. Syntax errors or a changed parse return NULL with an
 * error. Limits: 8 MiB input/output, 256 syntax levels, 262144 tokens.
 * No catalog, graph, compiler or editor state is needed. Default output is LF,
 * four spaces, and the existing line breaks within expressions. A continued
 * line aligns with the first item of its bracket, or indents one level when
 * the bracket ends its line; continued operands align with the first one. */
char *authoring_source_format(const script_syntax *, char *error, size_t size);

/* NULL options selects defaults. Width reflows argument/parameter lists, arrays,
 * records and parenthesized expressions; it is a preference, not a hard limit.
 * UTF-8 bytes count as columns, except indentation tabs count as indent_width.
 * Comments, literals and indivisible expressions can exceed preferred_width.
 * Output line endings are LF; embedded comment/literal bytes stay exact.
 *
 * Optional map receives owned, ordered, non-overlapping whitespace replacements.
 * Copy text[new_start:new_end] over source[start:end], working from last to first,
 * to reproduce the result. Free a previous map before reusing it. Failure clears
 * the map and leaves the input unchanged; no partial edits are published. */
char *authoring_source_format_with_options(
    const script_syntax *,
    const authoring_format_options *,
    authoring_format_map *,
    char *error,
    size_t size
);
void authoring_format_map_free(authoring_format_map *);
/* Maps a byte offset in [0, source_size]. Unchanged text retains its byte offset
 * within the token. Replaced gap endpoints map to their corresponding endpoints;
 * affinity chooses a side for offsets inside replaced whitespace and insertions. */
int authoring_format_map_position(const authoring_format_map *, int offset, authoring_format_affinity);

#endif
