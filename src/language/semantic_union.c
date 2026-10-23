#include "language/nominal_binding.h"
#include "language/semantic_internal.h"
#include "language/type_constructors.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
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
void script_analyzer_publish_type(analyzer *a, script_catalog_type type)
{
    apush(a->out->types, type);
    apush(a->catalog_types, type);
    a->catalog_view.types = a->catalog_types;
    a->catalog_view.type_count = acount(a->catalog_types);
    a->out->type_count = acount(a->out->types);
}
void script_analyzer_replace_type(analyzer *a, script_catalog_type type)
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
static int declared_union(analyzer *a, TSNode node)
{
    TSNode name = script_analyzer_field(node, "name");
    int symbol = script_analyzer_lookup(a, name);
    if (symbol < 0) {
        return -1;
    }
    const script_symbol *s = &a->out->symbols[symbol];
    script_range range = script_analyzer_range(a, name);
    if (s->kind != SCRIPT_TYPE || s->type.storage != PIN_UNION ||
        s->declaration.document != range.document ||
        s->declaration.start != range.start) {
        return -1;
    }
    return symbol;
}
void script_analyzer_union_types(analyzer *a, TSNode body)
{
    for (uint32_t i = 0; i < script_analyzer_named_count(body); i++) {
        TSNode node = ts_node_named_child(body, i);
        if (!script_analyzer_is(node, "union_declaration")) {
            continue;
        }
        TSNode token = script_analyzer_field(node, "name");
        char *name = script_analyzer_name_copy(a, token);
        script_lexical_facts facts = script_lexical_view(a->out, acount(a->out->symbols));
        script_range range = script_analyzer_range(a, token);
        script_nominal_declaration declaration = script_nominal_declare(
            &a->out->constants,
            &facts,
            (script_nominal_context){
                .lexical = { a->scope, a->function, a->package_scope, a->document_count, a->specialization },
                .program_kind = a->out->kind,
                .program_name = a->out->name,
                .pointer_size = sizeof(void *) },
            (script_nominal_request){ .kind = SCRIPT_NOMINAL_UNION,
                                      .name = name,
                                      .range = script_analyzer_range(a, node),
                                      .name_range = range }
        );
        for (int j = 0; j < declaration.diagnostic_count; j++) {
            apush(a->out->diagnostics, declaration.diagnostics[j]);
        }
        int symbol = declaration.existing;
        if (declaration.declare && symbol < 0) {
            apush(
                a->out->symbols,
                ((script_symbol){ .id = declaration.id,
                                  .name = name,
                                  .kind = SCRIPT_TYPE,
                                  .type = declaration.declaration_type,
                                  .declaration = range,
                                  .scope = a->scope,
                                  .initializer = -1,
                                  .function = -1,
                                  .writable = false })
            );
            symbol = acount(a->out->symbols) - 1;
            name = NULL; /* The analysis now owns this heap symbol name. */
        }
        free(name);
        if (!declaration.publish) {
            continue;
        }
        a->out->symbols[symbol].type = declaration.type;
        script_analyzer_publish_type(
            a,
            (script_catalog_type){ .id = declaration.id,
                                   .name = declaration.name,
                                   .source_name = declaration.source_name,
                                   .type = declaration.type,
                                   .authored_record = declaration.authored_record,
                                   .size = declaration.size }
        );
    }
}
void script_analyzer_union_fields(analyzer *a, TSNode body)
{
    for (uint32_t i = 0; i < script_analyzer_named_count(body); i++) {
        TSNode node = ts_node_named_child(body, i);
        if (!script_analyzer_is(node, "union_declaration")) {
            continue;
        }
        int symbol = declared_union(a, node);
        if (symbol < 0) {
            continue;
        }
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
            pin_value literal_value = { .type = PIN_STR };
            char *text;
            if (ts_node_is_null(literal)) {
                text = script_analyzer_name_copy(a, name);
            } else {
                literal_value = script_analyzer_string_value(a, literal);
                pin_string bytes = pin_text(literal_value);
                text = bytes.length && memchr(bytes.bytes, 0, bytes.length)
                         ? script_analyzer_text_copy(a, literal)
                         : xstrdup(bytes.bytes);
            }
            script_catalog_variant v = { .name = own(a, text),
                                         .type = type,
                                         .tagged = ts_node_is_null(literal),
                                         .is_literal = !ts_node_is_null(literal) };
            if (v.is_literal) {
                pin_string bytes = pin_text(literal_value);
                v.tag = script_catalog_id_bytes(
                    "union-literal",
                    bytes.bytes,
                    bytes.length
                );
                v.literal = literal_value;
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
        script_analyzer_replace_type(a, definition);
    }
    unsigned char *states = xcalloc((size_t)a->catalog->type_count + 1, sizeof *states);
    for (uint32_t i = 0; i < script_analyzer_named_count(body); i++) {
        TSNode node = ts_node_named_child(body, i);
        if (!script_analyzer_is(node, "union_declaration")) {
            continue;
        }
        int symbol = declared_union(a, node);
        if (symbol < 0) {
            continue;
        }
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
void script_analyzer_union_declarations(analyzer *a, TSNode body)
{
    script_analyzer_union_types(a, body);
    script_analyzer_union_fields(a, body);
}
static script_constructor_facts constructor_facts(const analyzer *a)
{
    return (script_constructor_facts){ .catalog = a->catalog,
                                       .symbols = a->out->symbols,
                                       .functions = a->out->functions,
                                       .function_count = acount(a->out->functions) };
}
static script_type apply_constructor(
    analyzer *a,
    const script_constructor_facts *facts,
    const script_constructor_plan *plan
)
{
    for (int i = 0; i < plan->diagnostic_count; i++) {
        apush(a->out->diagnostics, plan->diagnostics[i]);
    }
    if (plan->publish) {
        script_catalog_type definition = { 0 };
        bool copied = script_constructor_materialize(&a->out->constants, facts, plan, &definition);
        /* Active catalogs have valid, snapshot-compatible declaration values.
         * This is an ownership invariant, not a new source diagnostic. */
        assert(copied);
        if (!copied) {
            return script_analyzer_bad_type();
        }
        script_analyzer_publish_type(a, definition);
    }
    return plan->type;
}
script_type script_analyzer_union_type(analyzer *a, TSNode node, const script_type *members, int count)
{
    script_constructor_facts facts = constructor_facts(a);
    script_constructor_plan plan = script_constructor_union(
        &a->out->constants,
        &facts,
        members,
        count,
        sizeof(void *)
    );
    (void)node;
    return apply_constructor(a, &facts, &plan);
}
script_type script_analyzer_task_type(analyzer *a, script_type result, uint64_t function)
{
    script_constructor_facts facts = constructor_facts(a);
    script_constructor_plan plan = script_constructor_task(&a->out->constants, &facts, result, function);
    return apply_constructor(a, &facts, &plan);
}
script_type script_analyzer_await_type(analyzer *a, TSNode node, script_type success)
{
    script_constructor_facts facts = constructor_facts(a);
    script_constructor_plan plan = script_constructor_await(
        &a->out->constants,
        &facts,
        success,
        script_analyzer_range(a, node),
        sizeof(void *)
    );
    return apply_constructor(a, &facts, &plan);
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
    int named = script_analyzer_find_type(a, node, owner);
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
                                 .range = script_analyzer_range(a, node),
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
        TSNode value = script_analyzer_call_argument(argument);
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
                                .range = script_analyzer_range(a, argument) })
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
                                 .range = script_analyzer_range(a, node),
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
        pin_value literal = script_analyzer_string_value(a, node);
        pin_string text = pin_text(literal);
        result.tested = (script_type){
            .storage = PIN_STR,
            .flags = SCRIPT_TYPE_LITERAL,
            .parameter = script_catalog_id_bytes("union-literal", text.bytes, text.length)
        };
    } else {
        char *name = script_analyzer_text_copy(a, node);
        char *dot = strrchr(name, '.');
        if (dot) {
            *dot = 0;
            int index = script_analyzer_find_type(a, node, name);
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
                                    .range = script_analyzer_range(a, binding) })
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
    e->range = script_analyzer_range(a, node);
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
