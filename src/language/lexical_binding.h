#ifndef CHIBI_LANGUAGE_LEXICAL_BINDING_H
#define CHIBI_LANGUAGE_LEXICAL_BINDING_H

#include "language/semantic.h"

/* Borrowed analyzer facts. Symbol count is explicit because declarations use
 * this view before the analysis publishes its final counts. All referenced
 * indices retain the analyzer's scope/function/document invariants. */
typedef struct
{
    const script_scope *scopes;
    const script_symbol *symbols;
    const script_function *functions;
    const script_source_document *documents;
    int symbol_count;
    const char *identity;
} script_lexical_facts;
typedef struct
{
    int scope, function, package_scope, document_count;
    uint64_t specialization; /* Nonzero while analyzing one generic specialization. */
} script_lexical_context;
typedef struct
{
    int existing; /* -1 appends; otherwise reuse this symbol. */
    uint64_t id;  /* Published only when appending. */
    script_diagnostic diagnostics[3]; /* reserved, duplicate, related */
    int diagnostic_count;
} script_lexical_declaration;

script_lexical_facts script_lexical_view(const script_analysis *, int symbol_count);
int script_lexical_name(const script_lexical_facts *, int scope, const char *name);
int script_lexical_storage(const script_lexical_facts *, int scope, const char *name, script_symbol_kind);
int script_lexical_callable(const script_lexical_facts *, int scope, const char *name);
script_lexical_declaration script_lexical_declare(
    const script_lexical_facts *,
    script_lexical_context,
    script_range,
    const char *name,
    script_symbol_kind
);

#endif
