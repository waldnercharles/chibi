#ifndef CHIBI_AUTHORING_SOURCE_MINIMIZE_H
#define CHIBI_AUTHORING_SOURCE_MINIMIZE_H

#include "language/semantic.h"

/* Shortest accepted spelling of one executable source: semicolons, argument
 * labels, default arguments, inferable types, aliases and empty clauses are
 * dropped. A rewrite is kept only when the re-analyzed program is unchanged.
 * Returns malloc-owned text; source that does not analyze cleanly is copied. */
char *authoring_source_minimize(const char *text, const script_catalog *, const char *identity);

#endif
