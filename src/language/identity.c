#include "language/identity.h"

#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>

struct script_occurrences
{
    const script_analysis *analysis;
    /* Per document, lazily: how many bytes before each offset are not
     * whitespace. */
    int **solid;
    int count;
};

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
uint64_t script_occurrence(const char *key, script_record_kind kind, int start, int end)
{
    uint64_t hash = script_catalog_id("source-occurrence", "");
    size_t length = strlen(key);
    hash = word(hash, length);
    for (size_t i = 0; i < length; i++) {
        hash = (hash ^ (unsigned char)key[i]) * UINT64_C(1099511628211);
    }
    hash = word(word(word(hash, kind), (unsigned)start), (unsigned)end);
    return hash ? hash : 1;
}
script_occurrences *script_occurrences_new(const script_analysis *a)
{
    script_occurrences *o = xcalloc(1, sizeof *o);
    o->analysis = a;
    o->count = a->document_count ? a->document_count : 1;
    o->solid = xcalloc((size_t)o->count, sizeof *o->solid);
    return o;
}
void script_occurrences_free(script_occurrences *o)
{
    if (!o) {
        return;
    }
    for (int i = 0; i < o->count; i++) {
        free(o->solid[i]);
    }
    free(o->solid);
    free(o);
}
static bool whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}
static const int *solid_offsets(script_occurrences *o, int document, const char *text, size_t *length)
{
    *length = strlen(text);
    if (!o->solid[document]) {
        int *solid = xmalloc((*length + 1) * sizeof *solid);
        int count = 0;
        for (size_t i = 0; i <= *length; i++) {
            solid[i] = count;
            count += i < *length && !whitespace(text[i]);
        }
        o->solid[document] = solid;
    }
    return o->solid[document];
}
static script_range record_range(const script_analysis *a, script_record_kind kind, int record)
{
    switch (kind) {
        case SCRIPT_RECORD_SYMBOL: return a->symbols[record].declaration;
        case SCRIPT_RECORD_FUNCTION: return a->functions[record].range;
        case SCRIPT_RECORD_EXPRESSION: return a->expressions[record].range;
        case SCRIPT_RECORD_STATEMENT: return a->statements[record].range;
        case SCRIPT_RECORD_QUERY: return a->queries[record].range;
        case SCRIPT_RECORD_BINDING: return a->bindings[record].range;
    }
    return (script_range){ 0 };
}
uint64_t script_record_id(script_occurrences *o, script_record_kind kind, int record)
{
    const script_analysis *a = o->analysis;
    if (!valid(a, kind, record) || !a->identity) {
        return 0;
    }
    script_range range = record_range(a, kind, record);
    int document = a->document_count ? range.document : 0;
    if (document < 0 || document >= o->count) {
        return 0;
    }
    const char *text = a->document_count ? a->documents[document].text : a->text;
    const char *key = a->document_count ? a->documents[document].key : a->identity;
    size_t length;
    const int *solid = solid_offsets(o, document, text ? text : "", &length);
    size_t start = range.start < 0 ? 0 : (size_t)range.start;
    size_t end = range.end < range.start ? start : (size_t)range.end;
    start = start < length ? start : length;
    end = end < length ? end : length;
    return script_occurrence(key ? key : "", kind, solid[start], solid[end]);
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
