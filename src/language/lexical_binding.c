#include "language/lexical_binding.h"

#include "support/xalloc.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

script_lexical_facts script_lexical_view(const script_analysis *analysis, int symbol_count)
{
    return (script_lexical_facts){ .scopes = analysis->scopes,
                                   .symbols = analysis->symbols,
                                   .functions = analysis->functions,
                                   .documents = analysis->documents,
                                   .symbol_count = symbol_count,
                                   .identity = analysis->identity };
}
int script_lexical_name(const script_lexical_facts *facts, int scope, const char *name)
{
    for (; scope >= 0; scope = facts->scopes[scope].parent) {
        for (int i = facts->symbol_count - 1; i >= 0; i--) {
            const script_symbol *symbol = &facts->symbols[i];
            if (symbol->scope == scope && symbol->kind != SCRIPT_FUNCTION &&
                symbol->kind != SCRIPT_MACRO && !strcmp(symbol->name, name)) {
                return i;
            }
        }
    }
    return -1;
}
int script_lexical_storage(const script_lexical_facts *facts, int scope, const char *name, script_symbol_kind kind)
{
    int found = -1;
    for (; scope >= 0 && found < 0; scope = facts->scopes[scope].parent) {
        for (int i = facts->symbol_count - 1; i >= 0; i--) {
            const script_symbol *symbol = &facts->symbols[i];
            if (symbol->scope == scope && !strcmp(symbol->name, name) &&
                (symbol->kind == kind ||
                 (kind == SCRIPT_STATE && symbol->kind == SCRIPT_SHARED_STATE))) {
                found = i;
                break;
            }
        }
    }
    return found;
}
int script_lexical_callable(const script_lexical_facts *facts, int scope, const char *name)
{
    int symbol = script_lexical_name(facts, scope, name);
    if (symbol >= 0 && facts->symbols[symbol].kind == SCRIPT_ACTION) {
        return symbol;
    }
    for (; scope >= 0; scope = facts->scopes[scope].parent) {
        for (int i = facts->symbol_count - 1; i >= 0; i--) {
            const script_symbol *candidate = &facts->symbols[i];
            if (candidate->scope == scope &&
                (candidate->kind == SCRIPT_FUNCTION || candidate->kind == SCRIPT_MACRO) &&
                !strcmp(candidate->name, name)) {
                return i;
            }
        }
    }
    return -1;
}
script_lexical_declaration script_lexical_declare(
    const script_lexical_facts *facts,
    script_lexical_context context,
    script_range range,
    const char *name,
    script_symbol_kind kind
)
{
    script_lexical_declaration out = { .existing = -1 };
    if (!strcmp(name, "Cancelled")) {
        script_diagnostic *error = &out.diagnostics[out.diagnostic_count++];
        error->range = range;
        snprintf(error->message, sizeof error->message, "'Cancelled' is reserved for the cancellation singleton");
    }
    int existing = script_lexical_name(facts, context.scope, name);
    bool package_declaration = context.document_count &&
                               context.scope == context.package_scope;
    if (package_declaration) {
        existing = -1;
        for (int i = facts->symbol_count - 1; i >= 0; i--) {
            const script_symbol *candidate = &facts->symbols[i];
            if (candidate->scope == context.scope && !strcmp(candidate->name, name)) {
                existing = i;
                break;
            }
        }
    } else if (kind == SCRIPT_FUNCTION || kind == SCRIPT_MACRO) {
        existing = -1;
        for (int i = facts->symbol_count - 1; i >= 0; i--) {
            const script_symbol *candidate = &facts->symbols[i];
            if (candidate->scope == context.scope &&
                (candidate->kind == SCRIPT_FUNCTION || candidate->kind == SCRIPT_MACRO) &&
                !strcmp(candidate->name, name)) {
                existing = i;
                break;
            }
        }
    }
    bool result_parameter = existing >= 0 &&
                            (kind == SCRIPT_RESULT || kind == SCRIPT_LOCAL) &&
                            facts->symbols[existing].kind == SCRIPT_PARAMETER;
    if (existing >= 0 && facts->symbols[existing].scope == context.scope &&
        !result_parameter) {
        script_diagnostic *error = &out.diagnostics[out.diagnostic_count++];
        error->range = range;
        snprintf(error->message, sizeof error->message, "Duplicate declaration '%s'", name);
        if (package_declaration) {
            error = &out.diagnostics[out.diagnostic_count++];
            error->range = facts->symbols[existing].declaration;
            snprintf(error->message, sizeof error->message, "Previous declaration '%s' is here", name);
        }
        out.existing = existing;
        return out;
    }
    const char *program = facts->identity ? facts->identity : "";
    size_t size = strlen(program) + strlen(name) + 32;
    char *identity = xmalloc(size);
    const char *domain = kind == SCRIPT_FUNCTION || kind == SCRIPT_MACRO
                           ? ":callable"
                       : result_parameter ? kind == SCRIPT_LOCAL ? ":local" : ":result"
                                          : "";
    if (package_declaration) {
        snprintf(identity, size, "%s:%d:%s%s", program, kind, name, domain);
        out.id = script_catalog_id("package-symbol", identity);
    } else if (context.specialization) {
        size = strlen(name) + strlen(domain) + 64;
        identity = xrealloc(identity, size);
        snprintf(
            identity,
            size,
            "%" PRIu64 ":%d:%s%s",
            context.specialization,
            range.start,
            name,
            domain
        );
        out.id = script_catalog_id("symbol", identity);
    } else if (
        context.document_count && context.function >= 0 &&
        facts->functions[context.function].symbol >= 0
    ) {
        uint64_t
            owner = facts->symbols[facts->functions[context.function].symbol].id;
        script_range scope = facts->scopes[context.scope].range;
        const char *key = facts->documents[scope.document].key;
        bool direct = context.scope == facts->functions[context.function].scope;
        size = strlen(key) + strlen(name) + 96;
        identity = xrealloc(identity, size);
        snprintf(
            identity,
            size,
            "%" PRIu64 ":%s:%d:%d:%s%s",
            owner,
            key,
            direct ? -1 : scope.start,
            kind,
            name,
            domain
        );
        out.id = script_catalog_id("package-local", identity);
    } else {
        snprintf(identity, size, "%s:%d:%s%s", program, context.scope, name, domain);
        out.id = script_catalog_id("symbol", identity);
    }
    free(identity);
    return out;
}
