#ifndef CHIBI_AUTHORING_SOURCE_INTELLIGENCE_H
#define CHIBI_AUTHORING_SOURCE_INTELLIGENCE_H

#include "authoring/source_completion.h"
#include "authoring/source_navigation.h"

typedef struct
{
    script_range range;
    const char *cls; /* Static token class, usable by Studio and CLI renderers. */
} authoring_highlight;

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

#endif
