#ifndef CHIBI_TEXT_EDIT_H
#define CHIBI_TEXT_EDIT_H

/* A replacement of the bytes [start, end) of a text, as a language server's
 * TextEdit: an insertion when start == end, a deletion when `text` is "". */
typedef struct text_edit
{
    int start, end;
    const char *text;
} text_edit;

/* An owned copy of `text` with `edits` applied. The edits refer to `text`,
 * in any order, and must not overlap. */
char *text_edits_apply(const char *text, const text_edit *edits, int count);

#endif
