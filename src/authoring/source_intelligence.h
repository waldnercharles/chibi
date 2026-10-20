#ifndef CHIBI_AUTHORING_SOURCE_INTELLIGENCE_H
#define CHIBI_AUTHORING_SOURCE_INTELLIGENCE_H

#include "authoring/source_navigation.h"

typedef struct
{
    script_range range;
    const char *cls; /* Static token class, usable by Studio and CLI renderers. */
} authoring_highlight;
typedef enum
{
    AUTHORING_COMPLETE_VARIABLE,
    AUTHORING_COMPLETE_CALLABLE,
    AUTHORING_COMPLETE_EVENT,
    AUTHORING_COMPLETE_TYPE,
    AUTHORING_COMPLETE_FIELD,
    AUTHORING_COMPLETE_KEYWORD,
} authoring_completion_kind;
typedef struct
{
    authoring_target target; /* Zero ID for a grammar keyword. */
    char *label, *detail, *insert;
    authoring_completion_kind kind;
    script_type type;
} authoring_completion;
typedef struct
{
    script_range range; /* Complete token replacement range, UTF-8 byte offsets. */
    authoring_completion *items;
    int count;
} authoring_completions;

/* Inputs borrow the immutable navigation generation. Returned arrays/text own
 * their storage, and remain valid after that generation is released. Invalid
 * drafts retain canonical syntax highlighting and any resolved semantic facts.
 * Completion never constructs a graph or executes authored code. */
authoring_highlight *authoring_source_highlights(const authoring_navigation *, int document, int *count);
/* Canonical lexical highlighting for static assets and drafts, without an
 * executable semantic analysis or navigation generation. Caller frees. */
authoring_highlight *authoring_source_syntax_highlights(const script_syntax *, int *count);
char *authoring_source_hover(const authoring_navigation *, int document, int offset);
void authoring_source_complete(
    const authoring_navigation *,
    int document,
    int offset,
    authoring_completions *out
);
void authoring_completions_free(authoring_completions *);

#endif
