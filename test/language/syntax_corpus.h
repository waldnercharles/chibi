#ifndef CHIBI_TEST_SYNTAX_CORPUS_H
#define CHIBI_TEST_SYNTAX_CORPUS_H

#include <stdbool.h>

enum
{
    SYNTAX_CORPUS_INVALID_COUNT = 6,
    SYNTAX_CORPUS_RECOVERY = 6,
    SYNTAX_CORPUS_UTF8 = 7,
    SYNTAX_CORPUS_DELETE = 8,
    SYNTAX_CORPUS_PRECEDENCE = 9,
    SYNTAX_CORPUS_CALLS = 10,
    SYNTAX_CORPUS_ERROR_CHILDREN = 11,
    SYNTAX_CORPUS_CRLF_UTF8 = 12,
    SYNTAX_CORPUS_CASE_COUNT = 14,
    SYNTAX_CORPUS_EDIT_COUNT = 2,
    SYNTAX_CORPUS_MAX_SOURCE_BYTES = 2048,
};
typedef struct
{
    const char *name, *text;
    bool valid;
} syntax_corpus_case;
typedef struct
{
    const char *name;
    int source, start, end; /* Independent edits, byte offsets in source case. */
    const char *replacement, *after;
} syntax_corpus_edit;

extern const syntax_corpus_case syntax_corpus_cases[SYNTAX_CORPUS_CASE_COUNT];
extern const syntax_corpus_edit syntax_corpus_edits[SYNTAX_CORPUS_EDIT_COUNT];
#endif
