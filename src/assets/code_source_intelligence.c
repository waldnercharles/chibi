#include "assets/code_source_intelligence.h"

#include "assets/code_calls.h"
#include "assets/code_program.h"
#include "assets/code_source.h"
#include "graph/model/registry.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/text_search.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int context_start;
    char *label, *detail, *insert;
    code_completion_kind kind;
    bool typed, qualified_callables;
    code_semantic_type type;
} proposal;
typedef struct
{
    code_semantic_symbol *symbols;
    code_semantic_use *uses;
    code_semantic_call *calls;
    code_completion_context *contexts;
    proposal *proposals;
} intelligence;

static void append(char **text, const char *part)
{
    size_t n = strlen(*text), size = strlen(part);
    *text = xrealloc(*text, n + size + 1);
    memcpy(*text + n, part, size + 1);
}
char *code_intelligence_callable_signature(
    const char *name,
    const pin_spec *inputs,
    int input_count,
    const pin_spec *outputs,
    int output_count
)
{
    char *text = xstrdup(name);
    for (int side = 0; side < 2; side++) {
        append(&text, side ? " -> (" : "(");
        const pin_spec *ports = side ? outputs : inputs;
        int count = side ? output_count : input_count;
        for (int i = 0; i < count; i++) {
            if (i) {
                append(&text, ", ");
            }
            append(&text, ports[i].name);
            append(&text, ": ");
            const char *type = ports[i].kind == PIN_EXEC ? "exec" : ports[i].type_name;
            if (!type) {
                type = pin_type_name(ports[i].type);
            }
            append(&text, type ? type : "flow");
            if (ports[i].container == PIN_CONTAINER_ARRAY) {
                append(&text, "[]");
            }
            if (ports[i].container == PIN_CONTAINER_SET) {
                append(&text, " set");
            }
        }
        append(&text, ")");
    }
    return text;
}

static intelligence *facts(const code_source *source)
{
    return source->intelligence;
}
static intelligence *writable(code_source *source)
{
    if (!source->intelligence) {
        source->intelligence = xcalloc(1, sizeof(intelligence));
    }
    return source->intelligence;
}
static char *copy(const char *text)
{
    return text ? xstrdup(text) : NULL;
}
static code_semantic_type copy_type(code_semantic_type type)
{
    type.type_name = copy(type.type_name);
    return type;
}
void code_intelligence_clear(code_source *source)
{
    intelligence *index = facts(source);
    if (!index) {
        return;
    }
    for (int i = 0; i < acount(index->symbols); i++) {
        code_semantic_symbol *symbol = &index->symbols[i];
        free((char *)symbol->name);
        free((char *)symbol->signature);
        free((char *)symbol->description);
        free((char *)symbol->asset_name);
        free((char *)symbol->type.type_name);
    }
    for (int i = 0; i < acount(index->contexts); i++) {
        free((char *)index->contexts[i].expected.type_name);
    }
    for (int i = 0; i < acount(index->proposals); i++) {
        proposal *item = &index->proposals[i];
        free(item->label);
        free(item->detail);
        free(item->insert);
        free((char *)item->type.type_name);
    }
    afree(index->symbols);
    afree(index->uses);
    afree(index->calls);
    afree(index->contexts);
    afree(index->proposals);
    free(index);
    source->intelligence = NULL;
}
static const code_semantic_symbol *by_id(const intelligence *index, uint64_t id)
{
    for (int i = 0; index && i < acount(index->symbols); i++) {
        if (index->symbols[i].id == id) {
            return &index->symbols[i];
        }
    }
    return NULL;
}
const code_semantic_symbol *code_intelligence_symbols(const code_source *source, int *count)
{
    intelligence *index = facts(source);
    *count = index ? acount(index->symbols) : 0;
    return index ? index->symbols : NULL;
}
const code_semantic_symbol *code_intelligence_symbol_by_id(const code_source *source, uint64_t id)
{
    return by_id(facts(source), id);
}
void code_intelligence_symbol(code_source *source, const code_semantic_symbol *symbol)
{
    intelligence *index = writable(source);
    assert(symbol->id && symbol->name);
    assert(symbol->start >= 0 && symbol->end >= symbol->start);
    code_semantic_symbol value = *symbol;
    value.name = copy(symbol->name);
    value.signature = copy(symbol->signature);
    value.description = copy(symbol->description);
    value.asset_name = copy(symbol->asset_name);
    value.type = copy_type(symbol->type);
    for (int i = 0; i < acount(index->symbols); i++) {
        if (index->symbols[i].id != symbol->id) {
            continue;
        }
        free((char *)index->symbols[i].name);
        free((char *)index->symbols[i].signature);
        free((char *)index->symbols[i].description);
        free((char *)index->symbols[i].asset_name);
        free((char *)index->symbols[i].type.type_name);
        index->symbols[i] = value;
        return;
    }
    apush(index->symbols, value);
}
void code_intelligence_use(code_source *source, code_semantic_use use)
{
    assert(use.symbol_id && use.start >= 0 && use.end >= use.start);
    intelligence *index = writable(source);
    for (int i = 0; i < acount(index->uses); i++) {
        code_semantic_use *existing = &index->uses[i];
        if (existing->start == use.start && existing->end == use.end) {
            existing->symbol_id = use.symbol_id;
            existing->role |= use.role;
            return;
        }
    }
    apush(index->uses, use);
}
void code_intelligence_call(code_source *source, code_semantic_call call)
{
    assert(call.target_id && call.start >= 0 && call.end >= call.start);
    intelligence *index = writable(source);
    for (int i = 0; i < acount(index->calls); i++) {
        code_semantic_call existing = index->calls[i];
        if (existing.caller_id == call.caller_id && existing.target_id == call.target_id &&
            existing.start == call.start && existing.end == call.end) {
            return;
        }
    }
    apush(index->calls, call);
}
void code_intelligence_context(code_source *source, const code_completion_context *context)
{
    assert(context->start >= 0 && context->end >= context->start);
    code_completion_context value = *context;
    value.expected = copy_type(value.expected);
    apush(writable(source)->contexts, value);
}
void code_intelligence_location(code_source *source, uint64_t id, uint64_t graph_id, uint64_t node_id, uint64_t pin_id)
{
    intelligence *index = writable(source);
    for (int i = 0; i < acount(index->symbols); i++) {
        code_semantic_symbol *symbol = &index->symbols[i];
        if (symbol->id != id) {
            continue;
        }
        symbol->graph_id = graph_id;
        symbol->node_id = node_id;
        symbol->pin_id = pin_id;
        return;
    }
    assert(false && "location requires a declared semantic symbol");
}
static uint64_t remap(uint64_t id, const uint64_t *from, const uint64_t *to, int count)
{
    if (!id) {
        return 0;
    }
    for (int i = 0; i < count; i++) {
        if (from[i] == id) {
            return to[i];
        }
    }
    return id;
}
void code_intelligence_remap_native(code_source *source, const uint64_t *from, const uint64_t *to, int count)
{
    intelligence *index = facts(source);
    for (int i = 0; index && i < acount(index->symbols); i++) {
        code_semantic_symbol *symbol = &index->symbols[i];
        if (symbol->asset_name) {
            continue;
        }
        symbol->graph_id = remap(symbol->graph_id, from, to, count);
        symbol->node_id = remap(symbol->node_id, from, to, count);
        symbol->pin_id = remap(symbol->pin_id, from, to, count);
    }
}
void code_intelligence_offer(
    code_source *source,
    int context_start,
    const char *label,
    const char *detail,
    const char *insert,
    code_completion_kind kind,
    const code_semantic_type *type
)
{
    proposal item = { .context_start = context_start,
                      .label = xstrdup(label),
                      .detail = copy(detail),
                      .insert = copy(insert),
                      .kind = kind,
                      .typed = type != NULL };
    if (type) {
        item.type = copy_type(*type);
    }
    apush(writable(source)->proposals, item);
}
void code_intelligence_offer_qualified(
    code_source *source,
    int context_start,
    const char *label,
    const char *detail,
    const char *insert
)
{
    code_intelligence_offer(source, context_start, label, detail, insert, CODE_COMPLETE_FUNCTION, NULL);
    intelligence *index = writable(source);
    index->proposals[acount(index->proposals) - 1].qualified_callables = true;
}
static bool contains(int start, int end, int offset)
{
    return start <= offset && offset < end;
}
static const code_semantic_symbol *canonical(const intelligence *index, const code_semantic_symbol *symbol)
{
    for (int depth = 0; symbol && symbol->definition_id && depth < 64; depth++) {
        const code_semantic_symbol *next = by_id(index, symbol->definition_id);
        if (!next || next == symbol) {
            break;
        }
        symbol = next;
    }
    return symbol;
}
const code_semantic_symbol *code_intelligence_symbol_at(const code_source *source, int offset)
{
    intelligence *index = facts(source);
    if (!index) {
        return NULL;
    }
    const code_semantic_symbol *found = NULL;
    int width = 0;
    for (int i = 0; i < acount(index->uses); i++) {
        code_semantic_use use = index->uses[i];
        if (!contains(use.start, use.end, offset)) {
            continue;
        }
        const code_semantic_symbol *symbol = by_id(index, use.symbol_id);
        if (symbol && (!found || use.end - use.start < width)) {
            found = symbol;
            width = use.end - use.start;
        }
    }
    if (found) {
        return canonical(index, found);
    }
    for (int i = 0; i < acount(index->symbols); i++) {
        const code_semantic_symbol *symbol = &index->symbols[i];
        if (contains(symbol->start, symbol->end, offset)) {
            return canonical(index, symbol);
        }
    }
    return NULL;
}
bool code_intelligence_definition(const code_source *source, int offset, asset_source_span *range)
{
    const code_semantic_symbol *symbol = code_intelligence_symbol_at(source, offset);
    if (symbol && symbol->start == symbol->end && symbol->owner_id) {
        symbol = code_intelligence_symbol_by_id(source, symbol->owner_id);
    }
    if (!symbol || symbol->asset_name || symbol->start == symbol->end) {
        return false;
    }
    *range = (asset_source_span){ symbol->start, symbol->end, "tok-definition" };
    return true;
}
static void range_add(asset_source_span **ranges, int start, int end)
{
    if (start == end) {
        return;
    }
    for (int i = 0; i < acount(*ranges); i++) {
        if ((*ranges)[i].start == start && (*ranges)[i].end == end) {
            return;
        }
    }
    apush(*ranges, ((asset_source_span){ start, end, "tok-reference" }));
}
asset_source_span *code_intelligence_references_for(const code_source *source, uint64_t id)
{
    asset_source_span *ranges = NULL;
    const code_semantic_symbol
        *symbol = canonical(facts(source), code_intelligence_symbol_by_id(source, id));
    if (!symbol) {
        return NULL;
    }
    range_add(&ranges, symbol->start, symbol->end);
    intelligence *index = facts(source);
    for (int i = 0; i < acount(index->uses); i++) {
        code_semantic_use use = index->uses[i];
        const code_semantic_symbol *resolved = canonical(index, by_id(index, use.symbol_id));
        if (resolved && resolved->id == symbol->id) {
            range_add(&ranges, use.start, use.end);
        }
    }
    for (int i = 1; i < acount(ranges); i++) {
        asset_source_span value = ranges[i];
        int j = i;
        while (j && ranges[j - 1].start > value.start) {
            ranges[j] = ranges[j - 1];
            j--;
        }
        ranges[j] = value;
    }
    return ranges;
}
asset_source_span *code_intelligence_references(const code_source *source, int offset)
{
    const code_semantic_symbol *symbol = code_intelligence_symbol_at(source, offset);
    return symbol ? code_intelligence_references_for(source, symbol->id) : NULL;
}
char *code_intelligence_hover(const code_source *source, int offset)
{
    const code_semantic_symbol *symbol = code_intelligence_symbol_at(source, offset);
    if (!symbol) {
        return NULL;
    }
    const char *type = symbol->type.type_name ? symbol->type.type_name
                                              : pin_type_name(symbol->type.type);
    if (!type) {
        type = "flow";
    }
    const char *signature = symbol->signature ? symbol->signature : "";
    const char *description = symbol->description ? symbol->description : "";
    size_t size = strlen(symbol->name) + strlen(type) + strlen(signature) +
                  strlen(description) + 64;
    char *text = xmalloc(size);
    snprintf(
        text,
        size,
        "%s: %s%s%s%s%s%s",
        symbol->name,
        type,
        symbol->type.container == PIN_CONTAINER_ARRAY ? "[]"
        : symbol->type.container == PIN_CONTAINER_SET ? " set"
        : symbol->type.container == PIN_CONTAINER_MAP ? " map"
                                                      : "",
        *signature ? "\n" : "",
        signature,
        *description ? "\n" : "",
        description
    );
    return text;
}
static bool compatible(code_semantic_type expected, code_semantic_type actual)
{
    if (expected.container == actual.container && expected.type >= PIN_I8 &&
        expected.type <= PIN_F64 && actual.type >= PIN_I8 && actual.type <= PIN_F64) {
        return true;
    }
    if (expected.type != actual.type || expected.container != actual.container) {
        return false;
    }
    if ((expected.type != PIN_HANDLE && expected.type != PIN_STRUCT &&
         expected.type != PIN_ENUM) ||
        !expected.type_name || !*expected.type_name) {
        return true;
    }
    return actual.type_name && !strcmp(expected.type_name, actual.type_name);
}
static code_completion_kind completion_kind(code_symbol_kind kind)
{
    switch (kind) {
        case CODE_SYMBOL_FUNCTION:
        case CODE_SYMBOL_MACRO: return CODE_COMPLETE_FUNCTION;
        case CODE_SYMBOL_EVENT: return CODE_COMPLETE_FUNCTION;
        case CODE_SYMBOL_COMPONENT:
        case CODE_SYMBOL_TYPE: return CODE_COMPLETE_TYPE;
        case CODE_SYMBOL_FIELD: return CODE_COMPLETE_FIELD;
        default: return CODE_COMPLETE_VALUE;
    }
}
static char *name_insert(const char *name)
{
    if (isalpha((unsigned char)*name) || *name == '_') {
        const char *p = name + 1;
        while (isalnum((unsigned char)*p) || *p == '_') {
            p++;
        }
        if (!*p && !code_program_reserved(name)) {
            return xstrdup(name);
        }
    }
    av_doc *doc = av_doc_new();
    char *quoted = av_json_write(av_str(doc, name));
    av_doc_free(doc);
    return quoted;
}
enum
{
    COMPLETION_EXACT = 10000,
    COMPLETION_PREFIX = 5000,
    COMPLETION_LOCAL = 300,
    COMPLETION_MEMBER = 200,
    COMPLETION_CONTEXT = 100
};
static void offer(
    asset_source_completions *out,
    const char *prefix,
    const char *label,
    const char *detail,
    const char *insert,
    code_completion_kind kind,
    int relevance
)
{
    int score = text_search_match(prefix, label, NULL, 0);
    if (score < 0) {
        return;
    }
    /* Source spelling takes priority over fuzzy catalog matches. Context
     * relevance orders candidates within a spelling tier. */
    if (*prefix && !strncmp(label, prefix, strlen(prefix))) {
        score += !strcmp(label, prefix) ? COMPLETION_EXACT : COMPLETION_PREFIX;
    }
    score += relevance;
    for (int i = 0; i < acount(out->items); i++) {
        if (!strcmp(out->items[i].label, label)) {
            return;
        }
    }
    apush(
        out->items,
        ((asset_source_completion){ .label = xstrdup(label),
                                    .detail = detail,
                                    .insert = copy(insert),
                                    .kind = kind,
                                    .score = score })
    );
}
typedef struct
{
    const node_desc *descriptor;
    char *name, *detail, *insert, *qualified_detail;
} native_proposal;
static native_proposal *native_proposals;
static int native_count = -1;
static bool native_shadowed(const code_source *source, const native_proposal *proposal)
{
    intelligence *index = facts(source);
    for (int i = 0; i < acount(index->symbols); i++) {
        const code_semantic_symbol *symbol = &index->symbols[i];
        if (symbol->asset_name || symbol->node_id ||
            (symbol->kind != CODE_SYMBOL_FUNCTION && symbol->kind != CODE_SYMBOL_MACRO)) {
            continue;
        }
        if (!strcmp(symbol->name, proposal->name) ||
            !strcmp(symbol->name, proposal->descriptor->name)) {
            return true;
        }
    }
    return false;
}
static char *native_signature(const char *name, const node_desc *descriptor)
{
    pin_spec *inputs = NULL, *outputs = NULL;
    for (int i = 0; i < descriptor->input_count; i++) {
        if (descriptor->inputs[i].kind == PIN_DATA) {
            apush(inputs, descriptor->inputs[i]);
        }
    }
    for (int i = 0; i < descriptor->output_count; i++) {
        if (descriptor->outputs[i].kind == PIN_DATA) {
            apush(outputs, descriptor->outputs[i]);
        }
    }
    char *signature = code_intelligence_callable_signature(
        name,
        inputs,
        acount(inputs),
        outputs,
        acount(outputs)
    );
    afree(inputs);
    afree(outputs);
    return signature;
}
static void offer_native(const code_source *source, asset_source_completions *out, const char *prefix)
{
    int count = registry_node_count();
    if (native_count != count) {
        for (int i = 0; i < acount(native_proposals); i++) {
            free(native_proposals[i].name);
            free(native_proposals[i].detail);
            free(native_proposals[i].qualified_detail);
            free(native_proposals[i].insert);
        }
        afree(native_proposals);
        for (int i = 0; i < count; i++) {
            const node_desc *descriptor = registry_node(i);
            char name[256];
            bool alias = code_call_name(descriptor, name, sizeof name);
            char *insert = alias ? xstrdup(name) : NULL;
            if (!alias) {
                av_doc *document = av_doc_new();
                char *quoted = av_json_write(av_str(document, descriptor->name));
                size_t size = strlen(quoted) + 9;
                insert = xmalloc(size);
                snprintf(insert, size, "engine[%s]", quoted);
                free(quoted);
                av_doc_free(document);
            }
            native_proposal proposal = { .descriptor = descriptor,
                                         .name = xstrdup(insert),
                                         .insert = insert,
                                         .detail = native_signature(insert, descriptor) };
            apush(native_proposals, proposal);
        }
        native_count = count;
    }
    for (int i = 0; i < acount(native_proposals); i++) {
        native_proposal *proposal = &native_proposals[i];
        if (!native_shadowed(source, proposal)) {
            offer(out, prefix, proposal->name, proposal->detail, proposal->insert, CODE_COMPLETE_FUNCTION, 0);
            continue;
        }
        av_doc *document = av_doc_new();
        char *quoted = av_json_write(av_str(document, proposal->descriptor->name));
        size_t size = strlen(quoted) + 9;
        char *insert = xmalloc(size);
        snprintf(insert, size, "engine[%s]", quoted);
        if (!proposal->qualified_detail) {
            proposal->qualified_detail = native_signature(insert, proposal->descriptor);
        }
        offer(out, prefix, insert, proposal->qualified_detail, insert, CODE_COMPLETE_FUNCTION, 0);
        free(insert);
        free(quoted);
        av_doc_free(document);
    }
}
void code_intelligence_complete(const code_source *source, int offset, asset_source_completions *out)
{
    memset(out, 0, sizeof *out);
    intelligence *index = facts(source);
    if (!index || offset < 0 || offset > (int)strlen(source->text)) {
        return;
    }
    const code_completion_context *context = NULL;
    for (int i = 0; i < acount(index->contexts); i++) {
        const code_completion_context *candidate = &index->contexts[i];
        if (candidate->start > offset || offset > candidate->end) {
            continue;
        }
        int priority = candidate->ecs_event                      ? 6
                     : candidate->numeric_type                   ? 5
                     : candidate->ecs_member && candidate->typed ? 4
                     : candidate->ecs_member                     ? 3
                     : candidate->reference_value                ? 2
                     : candidate->qualified_callables            ? 1
                                                                 : 0;
        int current = !context                              ? -1
                    : context->ecs_event                    ? 6
                    : context->numeric_type                 ? 5
                    : context->ecs_member && context->typed ? 4
                    : context->ecs_member                   ? 3
                    : context->reference_value              ? 2
                    : context->qualified_callables          ? 1
                                                            : 0;
        if (!context || priority > current ||
            (priority == current && candidate->end - candidate->start <=
                                        context->end - context->start)) {
            context = candidate;
        }
    }
    if (!context) {
        return;
    }
    out->start = context->start;
    out->end = context->end;
    char *prefix = xmalloc((size_t)(offset - out->start + 1));
    memcpy(prefix, source->text + out->start, (size_t)(offset - out->start));
    prefix[offset - out->start] = 0;
    for (int i = 0; i < acount(index->symbols); i++) {
        const code_semantic_symbol *symbol = &index->symbols[i];
        if ((symbol->kind == CODE_SYMBOL_FUNCTION || symbol->kind == CODE_SYMBOL_MACRO) &&
            !symbol->asset_name && !symbol->renameable) {
            continue;
        }
        if (!(context->kinds & CODE_SYMBOL_MASK(symbol->kind))) {
            continue;
        }
        if (context->receiver_id) {
            if (symbol->owner_id != context->receiver_id) {
                continue;
            }
        } else if (symbol->owner_id && symbol->kind == CODE_SYMBOL_FIELD) {
            continue;
        }
        if (!context->receiver_id && symbol->kind == CODE_SYMBOL_EVENT &&
            symbol->owner_id) {
            const code_semantic_symbol *owner = by_id(index, symbol->owner_id);
            if (owner && owner->kind == CODE_SYMBOL_FIELD) {
                continue;
            }
        }
        if (offset < symbol->scope_start || offset > symbol->scope_end) {
            continue;
        }
        bool path_member = context->ecs_member &&
                           ((symbol->type.type == PIN_STRUCT && symbol->type.type_name &&
                             registry_type_by_name(symbol->type.type_name)) ||
                            (symbol->type.type == PIN_HANDLE && symbol->type.type_name &&
                             !strcmp(symbol->type.type_name, "ecs.entity")));
        if (context->typed && !path_member &&
            !compatible(context->expected, symbol->type)) {
            continue;
        }
        bool shadowed = false;
        for (int j = 0; j < acount(index->symbols); j++) {
            const code_semantic_symbol *inner = &index->symbols[j];
            if (inner->id == symbol->id || strcmp(inner->name, symbol->name) ||
                !(context->kinds & CODE_SYMBOL_MASK(inner->kind)) ||
                (!context->receiver_id && inner->kind == CODE_SYMBOL_FIELD &&
                 inner->owner_id) ||
                (context->receiver_id && inner->owner_id != symbol->owner_id)) {
                continue;
            }
            if (inner->scope_start > symbol->scope_start &&
                inner->scope_end <= symbol->scope_end &&
                offset >= inner->scope_start && offset <= inner->scope_end) {
                shadowed = true;
                break;
            }
        }
        if (!shadowed) {
            char *insert = symbol->asset_name ? xstrdup(symbol->name)
                                              : name_insert(symbol->name);
            offer(
                out,
                prefix,
                symbol->name,
                symbol->signature
                    ? symbol->signature
                    : (symbol->type.type_name ? symbol->type.type_name
                                              : pin_type_name(symbol->type.type)),
                insert,
                completion_kind(symbol->kind),
                context->receiver_id || symbol->kind == CODE_SYMBOL_LOCAL ||
                        symbol->kind == CODE_SYMBOL_PARAMETER
                    ? COMPLETION_LOCAL + symbol->completion_priority
                    : COMPLETION_CONTEXT + symbol->completion_priority
            );
            free(insert);
        }
    }
    if (context->typed && !context->receiver_id) {
        for (int i = 0; i < acount(index->symbols); i++) {
            const code_semantic_symbol *member = &index->symbols[i];
            if (member->kind != CODE_SYMBOL_FIELD || !member->owner_id ||
                !compatible(context->expected, member->type)) {
                continue;
            }
            const code_semantic_symbol *owner = by_id(index, member->owner_id);
            if (!owner || offset < owner->scope_start || offset > owner->scope_end) {
                continue;
            }
            char *a = name_insert(owner->name), *b = name_insert(member->name);
            size_t size = strlen(a) + strlen(b) + 2;
            char *insert = xmalloc(size);
            snprintf(insert, size, "%s.%s", a, b);
            size_t label_size = strlen(owner->name) + strlen(member->name) + 2;
            char *label = xmalloc(label_size);
            snprintf(label, label_size, "%s.%s", owner->name, member->name);
            offer(
                out,
                prefix,
                label,
                member->description ? member->description : member->type.type_name,
                insert,
                CODE_COMPLETE_FIELD,
                COMPLETION_MEMBER
            );
            free(a);
            free(b);
            free(insert);
            free(label);
        }
    }
    for (int i = 0; i < acount(index->proposals); i++) {
        const proposal *item = &index->proposals[i];
        bool numeric = !strcmp(item->label, "number");
        if (context->numeric_type && item->kind == CODE_COMPLETE_TYPE) {
            for (int type = PIN_BOOL; type <= PIN_F64; type++) {
                numeric |= !strcmp(item->label, pin_type_name((pin_type)type));
            }
        }
        if ((context->numeric_type && !numeric) ||
            (context->reference_value && item->kind == CODE_COMPLETE_FIELD) ||
            (context->ecs_member && strcmp(item->insert, "norm()")) ||
            item->context_start != context->start ||
            item->qualified_callables != context->qualified_callables ||
            (context->typed && item->typed &&
             !compatible(context->expected, item->type))) {
            continue;
        }
        offer(out, prefix, item->label, item->detail, item->insert, item->kind, COMPLETION_CONTEXT);
    }
    if (context->native_callables) {
        offer_native(source, out, prefix);
    }
    free(prefix);
    for (int i = 1; i < acount(out->items); i++) {
        asset_source_completion value = out->items[i];
        int j = i;
        while (j && out->items[j - 1].score < value.score) {
            out->items[j] = out->items[j - 1];
            j--;
        }
        out->items[j] = value;
    }
}
static bool identifier(const char *name)
{
    if (!(isalpha((unsigned char)*name) || *name == '_')) {
        return false;
    }
    for (const char *p = name + 1; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_') {
            return false;
        }
    }
    static const char *const reserved[] = {
        "behavior", "library", "system",   "state",  "on",      "if",
        "else",     "for",     "in",       "true",   "false",   "null",
        "self",     "event",   "function", "macro",  "return",  "value",
        "int",      "float",   "bool",     "string", "entity",  "set",
        "destroy",  "after",   "shared",   "bind",   "without", "nil"
    };
    for (size_t i = 0; i < sizeof reserved / sizeof *reserved; i++) {
        if (!strcmp(name, reserved[i])) {
            return false;
        }
    }
    return true;
}
char *code_intelligence_rename(const code_source *source, int offset, const char *name, char *error, size_t size)
{
    const code_semantic_symbol *symbol = code_intelligence_symbol_at(source, offset);
    if (!symbol || !symbol->renameable || symbol->start == symbol->end) {
        snprintf(error, size, "The selected symbol has no editable declaration");
        return NULL;
    }
    if (!name || !identifier(name)) {
        snprintf(error, size, "Rename requires a nonreserved identifier");
        return NULL;
    }
    intelligence *index = facts(source);
    for (int i = 0; i < acount(index->symbols); i++) {
        const code_semantic_symbol *other = &index->symbols[i];
        if ((other->kind == CODE_SYMBOL_LABEL) != (symbol->kind == CODE_SYMBOL_LABEL)) {
            continue;
        }
        if (other->kind == CODE_SYMBOL_FIELD && other->owner_id != symbol->owner_id) {
            continue;
        }
        if (other->id != symbol->id && !strcmp(other->name, name) &&
            other->scope_start <= symbol->scope_end &&
            symbol->scope_start <= other->scope_end) {
            snprintf(error, size, "Rename would collide with or capture %s", name);
            return NULL;
        }
    }
    asset_source_span *ranges = code_intelligence_references(source, offset);
    text_edit *edits = NULL;
    for (int i = 0; i < acount(ranges); i++) {
        bool implicit_alias = false;
        for (int j = 0; j < acount(index->uses); j++) {
            code_semantic_use use = index->uses[j];
            if (use.start != ranges[i].start || use.end != ranges[i].end) {
                continue;
            }
            const code_semantic_symbol *raw = by_id(index, use.symbol_id);
            implicit_alias |= raw && raw->kind == CODE_SYMBOL_EVENT &&
                              raw->definition_id && !raw->renameable &&
                              strcmp(raw->name, symbol->name);
        }
        if (!implicit_alias) {
            apush(edits, ((text_edit){ ranges[i].start, ranges[i].end, name }));
        }
    }
    char *result = text_edits_apply(source->text, edits, acount(edits));
    afree(edits);
    afree(ranges);
    if (!result) {
        snprintf(error, size, "Resolved reference ranges overlap");
    }
    return result;
}
char *code_intelligence_rename_with_metadata(
    const code_source *source,
    int offset,
    const char *name,
    char **metadata,
    char *error,
    size_t size
)
{
    *metadata = NULL;
    char *text = code_intelligence_rename(source, offset, name, error, size);
    if (!text) {
        return NULL;
    }
    char *original = code_source_metadata_export_source(source);
    char *clean = code_source_persist(source);
    char *adjusted = code_source_metadata_reanchor(original, clean, text);
    free(original);
    free(clean);
    code_source *candidate = code_source_parse_context(text, NULL, &source->context);
    bool valid = candidate->asset && adjusted &&
                 code_source_metadata_restore(candidate, adjusted);
    free(adjusted);
    if (valid) {
        *metadata = code_source_metadata_export_source(candidate);
    } else {
        snprintf(error, size, "Renamed source could not preserve its authoring identities");
        free(text);
        text = NULL;
    }
    code_source_free(candidate);
    return text;
}
code_semantic_call *code_intelligence_calls_for(const code_source *source, uint64_t id, bool callers)
{
    code_semantic_call *calls = NULL;
    intelligence *index = facts(source);
    for (int i = 0; index && i < acount(index->calls); i++) {
        code_semantic_call call = index->calls[i];
        if ((callers ? call.target_id : call.caller_id) == id) {
            apush(calls, call);
        }
    }
    return calls;
}
code_semantic_call *code_intelligence_calls(const code_source *source, int offset, bool callers)
{
    const code_semantic_symbol *symbol = code_intelligence_symbol_at(source, offset);
    return symbol ? code_intelligence_calls_for(source, symbol->id, callers) : NULL;
}
static const char *highlight(code_symbol_kind kind)
{
    switch (kind) {
        case CODE_SYMBOL_FUNCTION:
        case CODE_SYMBOL_MACRO:
        case CODE_SYMBOL_EVENT: return "tok-function";
        case CODE_SYMBOL_COMPONENT:
        case CODE_SYMBOL_TYPE: return "tok-type";
        case CODE_SYMBOL_FIELD: return "tok-field";
        case CODE_SYMBOL_PARAMETER: return "tok-parameter";
        default: return "tok-variable";
    }
}
asset_source_span *code_intelligence_highlights(const code_source *source)
{
    asset_source_span *ranges = NULL;
    intelligence *index = facts(source);
    for (int i = 0; index && i < acount(index->symbols); i++) {
        code_semantic_symbol symbol = index->symbols[i];
        if (symbol.start < symbol.end) {
            apush(
                ranges,
                ((asset_source_span){ symbol.start, symbol.end, highlight(symbol.kind) })
            );
        }
    }
    for (int i = 0; index && i < acount(index->uses); i++) {
        code_semantic_use use = index->uses[i];
        const code_semantic_symbol *symbol = by_id(index, use.symbol_id);
        if (symbol) {
            apush(
                ranges,
                ((asset_source_span){ use.start, use.end, highlight(symbol->kind) })
            );
        }
    }
    return ranges;
}
