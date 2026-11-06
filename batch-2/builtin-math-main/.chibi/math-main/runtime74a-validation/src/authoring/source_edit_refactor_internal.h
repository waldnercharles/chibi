#ifndef CHIBI_SOURCE_EDIT_REFACTOR_INTERNAL_H
#define CHIBI_SOURCE_EDIT_REFACTOR_INTERNAL_H
#include "authoring/source_edit_internal.h"
bool source_refactor_fail(char *, size_t, const char *);
void source_refactor_append(char **, const char *, ...);
char *source_refactor_take(char **);
bool source_refactor_contains(script_range, script_range);
char *source_refactor_quote(const script_catalog *, const char *);
char *source_refactor_slice(const char *, script_range);
bool source_refactor_type(const script_catalog *, script_type, bool reference, char **, char *, size_t);
bool source_edit_inline(
    const script_analysis *,
    const script_catalog *,
    const authoring_source_presentation *,
    const authoring_edit_request *,
    source_edit_changes *,
    char *,
    size_t
);
#endif
