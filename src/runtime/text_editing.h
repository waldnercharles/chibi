#ifndef CHIBI_TEXT_EDITING_H
#define CHIBI_TEXT_EDITING_H
#include <stdbool.h>
#include <stddef.h>

#define TEXT_EDITING_MAX_BYTES (1024 * 1024)
/* UTF-8 byte offsets. Motions count Unicode code points, not grapheme clusters;
 * combining sequences may take several character steps. Interior offsets round
 * down. CRLF is one line break. Word classes are ASCII identifiers, Unicode
 * code points and punctuation, separated by Unicode/ASCII whitespace. */
typedef struct
{
    int start, end, next, line, column;
} text_line_view;
bool text_editing_line(const char *, int offset, text_line_view *, char *error, size_t);
bool text_editing_cursor(
    const char *,
    int offset,
    const char *unit,
    int count,
    int goal_column,
    int *out,
    char *error,
    size_t
);
bool text_editing_slice(const char *, int start, int end, char **out, char *error, size_t);
bool text_editing_replace(
    const char *,
    int start,
    int end,
    const char *replacement,
    char **out,
    char *error,
    size_t
);
typedef struct
{
    int start, end, removed, inserted;
    char *replacement;
} text_difference;
bool text_editing_difference(const char *, const char *, text_difference *, char *, size_t);
bool text_editing_prefix_lines(
    const char *,
    int,
    int,
    const char *,
    const char *,
    char **,
    int *,
    int *,
    char *,
    size_t
);
#endif
