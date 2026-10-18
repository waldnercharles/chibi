#ifndef CHIBI_LANGUAGE_NAMED_BINDING_H
#define CHIBI_LANGUAGE_NAMED_BINDING_H

#include "language/lexical_binding.h"

/* Raw borrowed analyzer/catalog tables; no lookup decisions are supplied.
 * Counts remain explicit while the analyzer is constructing its output. */
typedef struct
{
    script_lexical_facts lexical;
    const script_catalog *catalog, *native;
    const script_catalog_type *own_types;
    int own_type_count;
    const script_package_import *imports;
    int import_count, document_count, package_scope;
    const script_use *uses;
    int use_count;
} script_named_facts;
typedef struct
{
    int scope;
    script_range range;
} script_named_request;
typedef struct
{
    script_type type;
    bool has_use, has_diagnostic;
    script_use use;
    script_diagnostic diagnostic;
} script_named_resolution;

int script_named_find_type(const script_named_facts *, int scope, int document, const char *name);
/* Parsed consumes a decoded ordinary leaf name and exact raw child spellings.
 * Constructor arguments, structural unions/tasks and measured signatures are
 * separate policies. Text preserves the existing fallback annotation grammar. */
script_named_resolution script_named_parsed(
    const script_named_facts *,
    script_named_request,
    const char *name,
    const char *const *tokens,
    int token_count
);
script_named_resolution script_named_text(
    const script_named_facts *,
    script_named_request,
    const char *text
);

#endif
