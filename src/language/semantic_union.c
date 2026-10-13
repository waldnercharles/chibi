#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/union.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *own(analyzer *a, const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = arena_alloc(&a->out->constants, size);
    memcpy(copy, text, size);
    return copy;
}
static script_catalog_variant own_variant(analyzer *a, script_catalog_variant value)
{
    if (value.name) {
        value.name = own(a, value.name);
    }
    if (value.is_literal) {
        value.literal.str = own(a, value.literal.str);
    }
    if (value.field_count) {
        script_catalog_field *fields = arena_alloc(
            &a->out->constants,
            (size_t)value.field_count * sizeof *fields
        );
        memcpy(fields, value.fields, (size_t)value.field_count * sizeof *fields);
        for (int i = 0; i < value.field_count; i++) {
            fields[i].name = own(a, fields[i].name);
        }
        value.fields = fields;
    }
    return value;
}
static void publish(analyzer *a, script_catalog_type type)
{
    apush(a->out->types, type);
    apush(a->catalog_types, type);
    a->catalog_view.types = a->catalog_types;
    a->catalog_view.type_count = acount(a->catalog_types);
    a->out->type_count = acount(a->out->types);
}
static void replace(analyzer *a, script_catalog_type type)
{
    for (int i = 0; i < acount(a->out->types); i++) {
        if (a->out->types[i].id == type.id) {
            a->out->types[i] = type;
        }
    }
    for (int i = 0; i < acount(a->catalog_types); i++) {
        if (a->catalog_types[i].id == type.id) {
            a->catalog_types[i] = type;
        }
    }
}
/* Owned payloads are snapshotted recursively. Reject cycles before any value
 * can enter that path; task and handle identities do not own their targets. */
static int payload_cycle(const script_catalog *catalog, script_type type, unsigned char *states, int depth)
{
    if (type.storage != PIN_UNION && type.storage != PIN_STRUCT) {
        return 0;
    }
    const script_catalog_type *record = script_catalog_type_by_id(catalog, type.nominal);
    if (!record) {
        return 0;
    }
    int index = (int)(record - catalog->types);
    if (states[index] == 1) {
        return 1;
    }
    if (states[index] == 2) {
        return 0;
    }
    if (depth == SCRIPT_MAX_DEPTH) {
        return 2;
    }
    states[index] = 1;
    for (int i = 0; i < record->field_count; i++) {
        int error = payload_cycle(catalog, record->fields[i].type, states, depth + 1);
        if (error) {
            return error;
        }
    }
    for (int i = 0; i < record->variant_count; i++) {
        const script_catalog_variant *variant = &record->variants[i];
        if (!variant->tagged) {
            int error = payload_cycle(catalog, variant->type, states, depth + 1);
            if (error) {
                return error;
            }
        }
        for (int j = 0; j < variant->field_count; j++) {
            int error = payload_cycle(catalog, variant->fields[j].type, states, depth + 1);
            if (error) {
                return error;
            }
        }
    }
    states[index] = 2;
    return 0;
}
void script_analyzer_union_declarations(analyzer *a, TSNode body)
{
    for (uint32_t i = 0; i < script_analyzer_named_count(body); i++) {
        TSNode node = ts_node_named_child(body, i);
        if (!script_analyzer_is(node, "union_declaration")) {
            continue;
        }
        TSNode name = script_analyzer_field(node, "name");
        int symbol = script_analyzer_symbol_new(
            a,
            name,
            SCRIPT_TYPE,
            (script_type){ .storage = PIN_UNION, .flags = SCRIPT_TYPE_UNION },
            false
        );
        script_symbol *s = &a->out->symbols[symbol];
        s->type.nominal = s->id;
        publish(
            a,
            (script_catalog_type){ .id = s->id,
                                   .name = own(a, s->name),
                                   .source_name = own(a, s->name),
                                   .type = s->type,
                                   .size = sizeof(void *) }
        );
    }
    for (uint32_t i = 0; i < script_analyzer_named_count(body); i++) {
        TSNode node = ts_node_named_child(body, i);
        if (!script_analyzer_is(node, "union_declaration")) {
            continue;
        }
        int symbol = script_analyzer_lookup(a, script_analyzer_field(node, "name"));
        script_type type = a->out->symbols[symbol].type;
        script_catalog_type definition = *script_catalog_type_by_id(a->catalog, type.nominal);
        script_catalog_variant *variants = NULL;
        for (uint32_t j = 0; j < script_analyzer_named_count(node); j++) {
            TSNode variant = ts_node_named_child(node, j);
            if (!script_analyzer_is(variant, "union_variant")) {
                continue;
            }
            TSNode literal = script_analyzer_field(variant, "literal");
            TSNode name = script_analyzer_field(variant, "name");
            char *text = script_analyzer_name_copy(a, ts_node_is_null(literal) ? name : literal);
            script_catalog_variant v = { .name = own(a, text),
                                         .type = type,
                                         .tagged = ts_node_is_null(literal),
                                         .is_literal = !ts_node_is_null(literal) };
            if (v.is_literal) {
                v.tag = script_catalog_id("union-literal", text);
                v.literal = (pin_value){ .type = PIN_STR, .str = v.name };
                v.type = (script_type){ .storage = PIN_STR,
                                        .flags = SCRIPT_TYPE_LITERAL,
                                        .parameter = v.tag };
            } else {
                char key[512];
                snprintf(key, sizeof key, "%llu:%s", (unsigned long long)type.nominal, text);
                v.tag = script_catalog_id("union-variant", key);
                v.type.parameter = v.tag;
            }
            free(text);
            for (int k = 0; k < acount(variants); k++) {
                if (variants[k].tag == v.tag) {
                    script_analyzer_diagnostic(a, variant, "Duplicate union alternative '%s'", v.name);
                }
            }
            script_catalog_field *fields = NULL;
            TSNode parameters = script_analyzer_field(variant, "parameters");
            for (uint32_t k = 0; k < script_analyzer_named_count(parameters); k++) {
                TSNode parameter = ts_node_named_child(parameters, k);
                if (ts_node_is_extra(parameter)) {
                    continue;
                }
                char *field_name = script_analyzer_name_copy(a, script_analyzer_field(parameter, "name"));
                script_catalog_field field = {
                    .id = script_catalog_id("union-field", field_name),
                    .name = own(a, field_name),
                    .type = script_analyzer_type_node(a, script_analyzer_field(parameter, "type")),
                    .offset = (size_t)acount(fields)
                };
                free(field_name);
                if (field.type.flags & (SCRIPT_TYPE_REFERENCE | SCRIPT_TYPE_UNTYPED)) {
                    script_analyzer_diagnostic(a, parameter, "Union payloads require an owned value type");
                }
                apush(fields, field);
            }
            v.field_count = acount(fields);
            if (v.field_count) {
                script_catalog_field *copy = arena_alloc(
                    &a->out->constants,
                    (size_t)v.field_count * sizeof *copy
                );
                memcpy(copy, fields, (size_t)v.field_count * sizeof *copy);
                v.fields = copy;
            }
            afree(fields);
            apush(variants, v);
        }
        definition.variant_count = acount(variants);
        if (!definition.variant_count) {
            script_analyzer_diagnostic(a, node, "A union needs at least one alternative");
        } else {
            script_catalog_variant *copy = arena_alloc(
                &a->out->constants,
                (size_t)definition.variant_count * sizeof *copy
            );
            memcpy(copy, variants, (size_t)definition.variant_count * sizeof *copy);
            definition.variants = copy;
        }
        afree(variants);
        replace(a, definition);
    }
    unsigned char *states = xcalloc((size_t)a->catalog->type_count + 1, sizeof *states);
    for (uint32_t i = 0; i < script_analyzer_named_count(body); i++) {
        TSNode node = ts_node_named_child(body, i);
        if (!script_analyzer_is(node, "union_declaration")) {
            continue;
        }
        int symbol = script_analyzer_lookup(a, script_analyzer_field(node, "name"));
        int error = payload_cycle(a->catalog, a->out->symbols[symbol].type, states, 0);
        if (error) {
            script_analyzer_diagnostic(
                a,
                node,
                error == 1 ? "Recursive owned union payloads are not supported"
                           : "Union payload nesting exceeds the supported depth"
            );
            break;
        }
    }
    free(states);
}
script_type script_analyzer_union_type(analyzer *a, TSNode node, const script_type *members, int count)
{
    script_catalog_variant *variants = NULL;
    for (int i = 0; i < count; i++) {
        const script_catalog_type
            *nested = (members[i].flags & SCRIPT_TYPE_UNION) &&
                              !(members[i].flags & SCRIPT_TYPE_BOXED_OPTIONAL) &&
                              members[i].container == PIN_CONTAINER_SINGLE
                        ? script_catalog_type_by_id(a->catalog, members[i].nominal)
                        : NULL;
        int n = nested ? nested->variant_count : 1;
        for (int j = 0; j < n; j++) {
            if (nested && members[i].parameter &&
                nested->variants[j].tag != members[i].parameter) {
                continue;
            }
            script_catalog_variant v = nested
                                         ? nested->variants[j]
                                         : (
                                               script_catalog_variant
                                           ){ .tag = script_type_tag(members[i]),
                                              .type = members[i] };
            bool exists = false;
            for (int k = 0; k < acount(variants); k++) {
                exists |= variants[k].tag == v.tag;
            }
            if (!exists) {
                apush(variants, v);
            }
        }
    }
    /* Stable structural identity, independent of spelling order. */
    for (int i = 1; i < acount(variants); i++) {
        for (int j = i; j > 0 && variants[j - 1].tag > variants[j].tag; j--) {
            script_catalog_variant tmp = variants[j];
            variants[j] = variants[j - 1];
            variants[j - 1] = tmp;
        }
    }
    uint64_t id = UINT64_C(14695981039346656037);
    for (int i = 0; i < acount(variants); i++) {
        id = (id ^ variants[i].tag) * UINT64_C(1099511628211);
    }
    script_type type = { .storage = PIN_UNION, .nominal = id, .flags = SCRIPT_TYPE_UNION };
    if (!script_catalog_type_by_id(a->catalog, id)) {
        char name[64];
        snprintf(name, sizeof name, "union:%llu", (unsigned long long)id);
        script_catalog_variant *copy = arena_alloc(
            &a->out->constants,
            (size_t)acount(variants) * sizeof *copy
        );
        for (int i = 0; i < acount(variants); i++) {
            copy[i] = own_variant(a, variants[i]);
        }
        publish(
            a,
            (script_catalog_type){ .id = id,
                                   .name = own(a, name),
                                   .type = type,
                                   .size = sizeof(void *),
                                   .variants = copy,
                                   .variant_count = acount(variants),
                                   .internal = true }
        );
    }
    afree(variants);
    (void)node;
    return type;
}
script_type script_analyzer_task_type(analyzer *a, script_type result, uint64_t function)
{
    uint64_t id = script_type_tag(result);
    if (!script_catalog_type_by_id(a->catalog, id)) {
        char name[64];
        snprintf(name, sizeof name, "task-result:%llu", (unsigned long long)id);
        script_catalog_field *fields = NULL;
        if (result.flags & SCRIPT_TYPE_RESULTS) {
            const script_catalog_capability
                *capability = script_catalog_capability_by_id(a->catalog, function);
            if (capability) {
                for (int i = 0; i < capability->result_count; i++) {
                    const script_catalog_parameter *field = &capability->results[i];
                    apush(
                        fields,
                        ((script_catalog_field){ .name = own(a, field->name),
                                                 .type = field->type })
                    );
                }
            } else {
                for (int i = 0; i < acount(a->out->functions); i++) {
                    const script_function *f = &a->out->functions[i];
                    if (f->symbol < 0 || a->out->symbols[f->symbol].id != function) {
                        continue;
                    }
                    for (int j = 0; j < acount(f->results); j++) {
                        const script_symbol *field = &a->out->symbols[f->results[j]];
                        apush(
                            fields,
                            ((script_catalog_field){ .name = own(a, field->name),
                                                     .type = field->type })
                        );
                    }
                    break;
                }
            }
        }
        script_catalog_field *owned_fields = NULL;
        if (acount(fields)) {
            owned_fields = arena_alloc(&a->out->constants, (size_t)acount(fields) * sizeof *owned_fields);
            memcpy(owned_fields, fields, (size_t)acount(fields) * sizeof *owned_fields);
        }
        publish(
            a,
            (script_catalog_type){ .id = id,
                                   .name = own(a, name),
                                   .type = result,
                                   .fields = owned_fields,
                                   .field_count = acount(fields),
                                   .internal = true }
        );
        afree(fields);
    }
    return (script_type){ .storage = PIN_TASK,
                          .nominal = function,
                          .parameter = id,
                          .flags = SCRIPT_TYPE_TASK };
}
script_type script_analyzer_await_type(analyzer *a, TSNode node, script_type success)
{
    const script_catalog_type
        *definition = success.flags & SCRIPT_TYPE_UNION
                        ? script_catalog_type_by_id(a->catalog, success.nominal)
                        : NULL;
    bool cancelled = success.flags & SCRIPT_TYPE_SINGLETON;
    for (int i = 0; definition && i < definition->variant_count; i++) {
        cancelled |= definition->variants[i].tag == PIN_UNION_CANCELLED_TAG;
    }
    if (cancelled) {
        script_analyzer_diagnostic(a, node, "Cancelled is reserved for task cancellation, not a successful task result");
    }
    script_type types[] = { success, script_cancelled_type() };
    return script_analyzer_union_type(a, node, types, 2);
}
static bool binding_condition(analyzer *a, TSNode node)
{
    for (int depth = 0; depth < SCRIPT_MAX_DEPTH; depth++) {
        TSNode parent = ts_node_parent(node);
        if (ts_node_is_null(parent)) {
            return false;
        }
        if (script_analyzer_is(parent, "if_statement") ||
            script_analyzer_is(parent, "while_statement")) {
            return ts_node_eq(node, script_analyzer_field(parent, "condition"));
        }
        if (script_analyzer_is(parent, "switch_arm")) {
            return ts_node_eq(node, script_analyzer_field(parent, "value"));
        }
        if (script_analyzer_is(parent, "is_expression")) {
            if (!ts_node_is_null(script_analyzer_field(parent, "negated"))) {
                return false;
            }
        } else if (script_analyzer_is(parent, "binary_expression")) {
            if (script_analyzer_operator_node(a, script_analyzer_field(parent, "operator")) !=
                SCRIPT_OP_AND) {
                return false;
            }
        } else if (
            !script_analyzer_is(parent, "parenthesized_expression") &&
            !script_analyzer_is(parent, "pattern_bindings") &&
            !script_analyzer_is(parent, "variant_pattern")
        ) {
            return false;
        }
        node = parent;
    }
    return false;
}
static const script_catalog_variant *variant_name(const script_catalog_type *type, const char *name)
{
    for (int i = 0; type && i < type->variant_count; i++) {
        if (type->variants[i].name && !strcmp(type->variants[i].name, name)) {
            return &type->variants[i];
        }
    }
    return NULL;
}
static const script_catalog_variant *qualified_variant(analyzer *a, TSNode node, script_type *type)
{
    if (!script_analyzer_is(node, "member_expression")) {
        return NULL;
    }
    char *owner = script_analyzer_qualified_name(a, script_analyzer_field(node, "object"));
    int symbol = script_analyzer_lookup_name(a, owner);
    int named = script_catalog_find_type(a->catalog, owner);
    free(owner);
    const script_catalog_type *definition = NULL;
    if (symbol >= 0 && a->out->symbols[symbol].kind == SCRIPT_TYPE) {
        definition = script_catalog_type_by_id(
            a->catalog,
            a->out->symbols[symbol].type.nominal
        );
    } else if (named >= 0) {
        definition = &a->catalog->types[named];
    }
    if (!definition || !(definition->type.flags & SCRIPT_TYPE_UNION)) {
        return NULL;
    }
    char *name = script_analyzer_name_copy(a, script_analyzer_field(node, "member"));
    const script_catalog_variant *v = variant_name(definition, name);
    free(name);
    *type = definition->type;
    return v;
}
int script_analyzer_union_constructor(analyzer *a, TSNode node)
{
    bool call = script_analyzer_is(node, "call_expression");
    TSNode callee = call ? script_analyzer_field(node, "function") : node;
    script_type type = { 0 };
    const script_catalog_variant *found = qualified_variant(a, callee, &type);
    if (!found || !found->tagged) {
        return -1;
    }
    script_catalog_variant v = *found;
    script_expression result = { .kind = SCRIPT_EXPR_UNION,
                                 .type = type,
                                 .range = script_analyzer_range(node),
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1,
                                 .entry = -1,
                                 .tag = v.tag };
    TSNode arguments = call ? script_analyzer_field(node, "arguments")
                            : (TSNode){ 0 };
    bool *used = xcalloc((size_t)v.field_count + 1, sizeof *used);
    int ordinal = 0;
    for (uint32_t i = 0; i < script_analyzer_named_count(arguments); i++) {
        TSNode argument = ts_node_named_child(arguments, i);
        if (ts_node_is_extra(argument)) {
            continue;
        }
        TSNode name = script_analyzer_field(argument, "name");
        TSNode value = script_analyzer_field(argument, "value");
        if (ts_node_is_null(value)) {
            value = argument;
        }
        int field = ordinal++;
        if (!ts_node_is_null(name)) {
            char *text = script_analyzer_name_copy(a, name);
            field = -1;
            for (int j = 0; j < v.field_count; j++) {
                if (!strcmp(v.fields[j].name, text)) {
                    field = j;
                }
            }
            free(text);
        }
        if (field < 0 || field >= v.field_count || used[field]) {
            script_analyzer_diagnostic(a, argument, "Unknown, duplicate or excess union payload argument");
            continue;
        }
        used[field] = true;
        int expression = script_analyzer_expression(a, value, v.fields[field].type);
        script_analyzer_require(a, value, v.fields[field].type, script_analyzer_expr_type(a, expression));
        apush(
            result.arguments,
            ((script_argument){ .value = expression,
                                .parameter = field,
                                .range = script_analyzer_range(argument) })
        );
        result.effects |= script_analyzer_expr_effects(a, expression);
    }
    for (int i = 0; i < v.field_count; i++) {
        if (!used[i]) {
            script_analyzer_diagnostic(
                a,
                node,
                "Missing union payload '%s'",
                v.fields[i].name
            );
        }
        apush(result.results, v.fields[i].type);
    }
    free(used);
    result.result_count = v.field_count;
    return script_analyzer_expr_add(a, result);
}
script_type script_analyzer_narrowed_type(const analyzer *a, int symbol, script_type fallback)
{
    for (int i = acount(a->narrowed) - 1; i >= 0; i--) {
        if (a->narrowed[i].symbol == symbol) {
            return a->narrowed[i].typed && a->narrowed[i].mutations ==
                                               a->out->symbols[symbol].mutations
                     ? a->narrowed[i].type
                     : fallback;
        }
    }
    return fallback;
}
void script_analyzer_invalidate_narrowing(analyzer *a, int symbol)
{
    for (int i = acount(a->narrowed) - 1; i >= 0; i--) {
        if (symbol < 0 || a->narrowed[i].symbol == symbol) {
            if (symbol < 0 && (a->narrowed[i].typed || a->narrowed[i].present)) {
                a->out->symbols[a->narrowed[i].symbol].mutations++;
            }
            a->narrowed[i].typed = false;
            a->narrowed[i].present = false;
        }
    }
}

int script_analyzer_pattern(analyzer *a, TSNode node, int subject)
{
    script_type source = script_analyzer_expr_type(a, subject);
    const script_catalog_type
        *owner = (source.flags & SCRIPT_TYPE_UNION) && source.container == PIN_CONTAINER_SINGLE
                   ? script_catalog_type_by_id(a->catalog, source.nominal)
                   : NULL;
    script_catalog_type owner_copy = owner ? *owner : (script_catalog_type){ 0 };
    owner = owner ? &owner_copy : NULL;
    script_expression result = { .kind = SCRIPT_EXPR_IS,
                                 .type = { .storage = PIN_BOOL },
                                 .range = script_analyzer_range(node),
                                 .left = subject,
                                 .right = -1,
                                 .entry = -1,
                                 .symbol = -1,
                                 .effects = script_analyzer_expr_effects(a, subject) };
    const script_catalog_variant *variant = NULL;
    if (script_analyzer_is(node, "variant_pattern")) {
        char *name = script_analyzer_name_copy(a, script_analyzer_field(node, "name"));
        variant = variant_name(owner, name);
        if (!variant || !variant->tagged) {
            script_analyzer_diagnostic(a, node, "Unknown union variant '%s'", name);
        }
        free(name);
    } else if (script_analyzer_is(node, "string")) {
        char *text = script_analyzer_name_copy(a, node);
        result.tested = (script_type){
            .storage = PIN_STR,
            .flags = SCRIPT_TYPE_LITERAL,
            .parameter = script_catalog_id("union-literal", text)
        };
        free(text);
    } else {
        char *name = script_analyzer_text_copy(a, node);
        char *dot = strrchr(name, '.');
        if (dot) {
            *dot = 0;
            int index = script_catalog_find_type(a->catalog, name);
            if (index >= 0) {
                variant = variant_name(&a->catalog->types[index], dot + 1);
            }
            *dot = '.';
        }
        if (!variant) {
            /* In switch arms a type is parsed as an ordinary identifier. */
            result.tested = script_analyzer_is(node, "type") ||
                                    script_analyzer_is(node, "union_type")
                              ? script_analyzer_type_node(a, node)
                              : script_analyzer_type_text(a, node, name);
        }
        free(name);
    }
    if (variant) {
        result.tested = variant->type;
        result.tested.parameter = variant->tag;
        result.tag = variant->tag;
        apush(result.pattern_tags, result.tag);
        TSNode bindings = script_analyzer_field(node, "bindings");
        int ordinal = 0;
        for (uint32_t i = 0; i < script_analyzer_named_count(bindings); i++) {
            TSNode binding = ts_node_named_child(bindings, i);
            if (ts_node_is_extra(binding)) {
                continue;
            }
            if (ordinal >= variant->field_count) {
                script_analyzer_diagnostic(a, binding, "Excess union pattern binding");
                break;
            }
            int symbol = script_analyzer_symbol_new(
                a,
                binding,
                SCRIPT_LOCAL,
                variant->fields[ordinal].type,
                false
            );
            if (!binding_condition(a, binding)) {
                script_analyzer_diagnostic(a, binding, "Pattern bindings require a matching if, while or switch arm");
            }
            apush(
                result.arguments,
                ((script_argument){ .value = subject,
                                    .parameter = symbol,
                                    .member = (uint64_t)ordinal,
                                    .range = script_analyzer_range(binding) })
            );
            ordinal++;
        }
        if (ordinal && ordinal != variant->field_count) {
            script_analyzer_diagnostic(a, node, "A union payload pattern must bind every field");
        }
    } else {
        uint64_t tag = script_type_tag(result.tested);
        const script_catalog_type
            *target = result.tested.flags & SCRIPT_TYPE_UNION
                        ? script_catalog_type_by_id(a->catalog, result.tested.nominal)
                        : NULL;
        if (owner) {
            for (int i = 0; i < owner->variant_count; i++) {
                bool match = owner->variants[i].tag == tag;
                match |= owner->variants[i].is_literal &&
                         !(result.tested.flags & SCRIPT_TYPE_LITERAL) &&
                         script_type_equal(owner->variants[i].type, result.tested);
                for (int j = 0; target && j < target->variant_count; j++) {
                    match |= owner->variants[i].tag == target->variants[j].tag;
                }
                if (match) {
                    apush(result.pattern_tags, owner->variants[i].tag);
                }
            }
        } else {
            script_type present = source;
            if (present.flags & SCRIPT_TYPE_BOXED_OPTIONAL) {
                present.flags &= ~(SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_BOXED_OPTIONAL);
            }
            if (script_type_equal(present, result.tested)) {
                apush(result.pattern_tags, tag);
            }
        }
        result.tag = acount(result.pattern_tags) ? result.pattern_tags[0] : 0;
    }
    result.pattern_tag_count = acount(result.pattern_tags);
    return script_analyzer_expr_add(a, result);
}
int script_analyzer_pattern_expression(analyzer *a, TSNode node)
{
    int subject = script_analyzer_expression(
        a,
        script_analyzer_field(node, "value"),
        (script_type){ .flags = SCRIPT_TYPE_UNTYPED }
    );
    int index = script_analyzer_pattern(a, script_analyzer_field(node, "type"), subject);
    bool extract = script_analyzer_is(node, "as_expression");
    script_expression *e = &a->out->expressions[index];
    e->range = script_analyzer_range(node);
    e->negated = !ts_node_is_null(script_analyzer_field(node, "negated"));
    if (extract) {
        e->kind = SCRIPT_EXPR_AS;
        e->type = e->tested;
        e->type.flags |= SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_BOXED_OPTIONAL;
    }
    TSNode binding = script_analyzer_field(node, "binding");
    if (!ts_node_is_null(binding)) {
        if (!binding_condition(a, node)) {
            script_analyzer_diagnostic(a, binding, "Pattern bindings require a matching if or while condition");
        }
        if (e->negated) {
            script_analyzer_diagnostic(a, binding, "A negated pattern does not bind a matching value");
        }
        script_type type = e->tested;
        int symbol = script_analyzer_symbol_new(a, binding, SCRIPT_LOCAL, type, false);
        a->out->expressions[index].symbol = symbol;
    }
    return index;
}
