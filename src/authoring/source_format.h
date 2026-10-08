#ifndef CHIBI_AUTHORING_SOURCE_FORMAT_H
#define CHIBI_AUTHORING_SOURCE_FORMAT_H

#include "language/syntax.h"

#include <stddef.h>

/* Whitespace-only formatting of a complete canonical syntax snapshot. Returns
 * malloc-owned text. Comments and literal bytes remain exact; the snapshot is
 * borrowed and unchanged. Syntax errors or a changed parse return NULL with an
 * error. Limits: 8 MiB input/output, 256 syntax levels, 262144 tokens.
 * No catalog, graph, compiler or editor state is needed. */
char *authoring_source_format(const script_syntax *, char *error, size_t size);

#endif
