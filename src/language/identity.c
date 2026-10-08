#include "language/identity.h"

#include <string.h>

static bool valid(const script_analysis *a, script_record_kind kind, int record)
{
    if (!a || record < 0) {
        return false;
    }
    int count;
    switch (kind) {
        case SCRIPT_RECORD_SYMBOL: count = a->symbol_count; break;
        case SCRIPT_RECORD_FUNCTION: count = a->function_count; break;
        case SCRIPT_RECORD_EXPRESSION: count = a->expression_count; break;
        case SCRIPT_RECORD_STATEMENT: count = a->statement_count; break;
        case SCRIPT_RECORD_QUERY: count = a->query_count; break;
        case SCRIPT_RECORD_BINDING: count = a->binding_count; break;
        default: return false;
    }
    return record < count;
}
static uint64_t word(uint64_t hash, uint64_t value)
{
    for (int i = 0; i < 8; i++) {
        hash = (hash ^ (value & 255)) * UINT64_C(1099511628211);
        value >>= 8;
    }
    return hash;
}
uint64_t script_record_id(const script_analysis *a, script_record_kind kind, int record)
{
    if (!valid(a, kind, record) || !a->identity) {
        return 0;
    }
    uint64_t hash = script_catalog_id("source-occurrence", "");
    size_t length = strlen(a->identity);
    hash = word(hash, length);
    for (size_t i = 0; i < length; i++) {
        hash = (hash ^ (unsigned char)a->identity[i]) * UINT64_C(1099511628211);
    }
    hash = word(word(hash, kind), (unsigned)record);
    return hash ? hash : 1;
}
uint64_t script_record_meaning(const script_analysis *a, script_record_kind kind, int record)
{
    if (!valid(a, kind, record)) {
        return 0;
    }
    switch (kind) {
        case SCRIPT_RECORD_SYMBOL: return a->symbols[record].id;
        case SCRIPT_RECORD_FUNCTION: {
            const script_function *f = &a->functions[record];
            return f->symbol >= 0 ? a->symbols[f->symbol].id
                 : f->event       ? f->event
                                  : f->action;
        }
        case SCRIPT_RECORD_EXPRESSION: {
            const script_expression *e = &a->expressions[record];
            return e->instance    ? e->instance
                 : e->symbol >= 0 ? a->symbols[e->symbol].id
                 : e->member      ? e->member
                                  : e->capability;
        }
        case SCRIPT_RECORD_STATEMENT: {
            int symbol = a->statements[record].symbol;
            return symbol >= 0 ? a->symbols[symbol].id : 0;
        }
        case SCRIPT_RECORD_QUERY: return a->queries[record].component;
        case SCRIPT_RECORD_BINDING: return 0;
    }
    return 0;
}
