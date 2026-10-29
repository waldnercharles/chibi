#ifndef CHIBI_ASSET_SOURCE_H
#define CHIBI_ASSET_SOURCE_H

#include "support/asset_value.h"
#include "support/code_completion.h"

#include <stdbool.h>
#include <stddef.h>

/* Shared declarative KDL language service. Byte ranges always refer to the original source,
 * including comments and whitespace. Arrays use support/array.h. */
typedef struct
{
    int start, end;
    const char *cls;
} asset_source_span;
typedef struct
{
    int start, end, line, column;
    char message[256];
} asset_source_diagnostic;
typedef struct
{
    char *text;
    av_doc *document;
    asset_source_span *spans;
    asset_source_diagnostic *diagnostics;
} asset_source;
typedef struct
{
    char *label;
    const char *detail;
    char *insert;
    code_completion_kind kind;
    int score; /* shared text matching rank; ties keep declaration order */
} asset_source_completion;
typedef struct
{
    int start, end;
    asset_source_completion *items;
} asset_source_completions;

asset_source *asset_source_parse(const char *text);
void asset_source_free(asset_source *source);
void asset_source_position(const asset_source *source, int offset, int *line, int *column);
/* Registry-aware completion works in incomplete source too. `path` supplies
 * asset-kind context; `names` optionally supplies project asset references. */
void asset_source_complete(
    const asset_source *source,
    int offset,
    const char *path,
    const char *const *names,
    int name_count,
    asset_source_completions *out
);
void asset_source_completions_free(asset_source_completions *out);
char *asset_source_hover(const asset_source *source, int offset);
/* Value at a source position, with its key when it has one. NULL on whitespace. */
const av_value *asset_source_value_at(const asset_source *source, int offset, const char **name);
/* Preserve unchanged source; scalar edits replace only their lexeme. Container
 * shape edits rewrite the smallest changed container, retaining surrounding comments.
 * Canonical source has nothing to preserve, so it stays canonical.
 * A failed reconciliation returns NULL with an error, never an unsafe rewrite. */
char *asset_source_reconcile(const char *text, const av_value *replacement, char *error, size_t size);

#endif
