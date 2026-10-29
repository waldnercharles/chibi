#ifndef CHIBI_AUTHORING_SOURCE_REFACTOR_TEXT_H
#define CHIBI_AUTHORING_SOURCE_REFACTOR_TEXT_H

#include "language/semantic.h"

/* Text builders shared by the source minimizer. */
bool source_refactor_fail(char *, size_t, const char *);
void source_refactor_append(char **, const char *, ...);
char *source_refactor_take(char **);
bool source_refactor_contains(script_range, script_range);
char *source_refactor_quote(const script_catalog *, const char *);
char *source_refactor_slice(const char *, script_range);
bool source_refactor_type(const script_catalog *, script_type, bool reference, char **, char *, size_t);

#endif
