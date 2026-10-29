#include "language/lexical_binding.h"
#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *script_analyzer_qualified_name(analyzer *a, TSNode node)
{
    if (script_analyzer_is(node, "argument_name")) {
        char *name = xstrdup("");
        for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
            TSNode child = ts_node_named_child(node, i);
            if (ts_node_is_extra(child)) {
                continue;
            }
            char *part = script_analyzer_name_copy(a, child);
            size_t length = strlen(name), size = length + strlen(part) + 2;
            name = xrealloc(name, size);
            snprintf(name + length, size - length, "%s%s", length ? "." : "", part);
            free(part);
        }
        return name;
    }
    if (script_analyzer_is(node, "engine_reference")) {
        return script_analyzer_name_copy(a, script_analyzer_field(node, "name"));
    }
    if (!script_analyzer_is(node, "member_expression")) {
        return script_analyzer_name_copy(a, node);
    }
    char *left = script_analyzer_qualified_name(a, script_analyzer_field(node, "object"));
    char *right = script_analyzer_name_copy(a, script_analyzer_field(node, "member"));
    size_t size = strlen(left) + strlen(right) + 2;
    char *name = xmalloc(size);
    snprintf(name, size, "%s.%s", left, right);
    free(left);
    free(right);
    return name;
}
static TSNode unparenthesized_parent(TSNode node)
{
    TSNode parent = ts_node_parent(node);
    while (script_analyzer_is(parent, "parenthesized_expression")) {
        parent = ts_node_parent(parent);
    }
    return parent;
}
int script_analyzer_callable_symbol(const analyzer *a, const char *name)
{
    script_lexical_facts facts = script_lexical_view(a->out, acount(a->out->symbols));
    return script_lexical_callable(&facts, a->scope, name);
}

int script_analyzer_parameter_index(const script_catalog_parameter *parameters, int count, const char *name)
{
    int found = -1;
    for (int i = 0; i < count; i++) {
        if (!strcmp(parameters[i].name, name)) {
            return i;
        }
        if (script_name_matches(parameters[i].name, name)) {
            found = found == -1 ? i : -2;
        }
    }
    return found;
}
static int variadic_parameter(const script_catalog_capability *capability, const char *name, int argument_count)
{
    if (!capability->variadic || !capability->parameter_count) {
        return -1;
    }
    const char *end = name + strlen(name), *digits = end;
    while (digits > name && isdigit((unsigned char)digits[-1])) {
        digits--;
    }
    if (digits == end) {
        return -1;
    }
    int ordinal = 0;
    for (const char *p = digits; p < end; p++) {
        int digit = *p - '0';
        if (ordinal > argument_count || digit > argument_count - ordinal * 10) {
            return -1;
        }
        ordinal = ordinal * 10 + digit;
    }
    size_t size = (size_t)(digits - name);
    char *first = xmalloc(size + 2);
    memcpy(first, name, size);
    memcpy(first + size, "0", 2);
    bool matches = script_name_matches(
        capability->parameters[capability->parameter_count - 1].name,
        first
    );
    free(first);
    return matches ? capability->parameter_count - 1 + ordinal : -1;
}
int script_analyzer_authored_parameter_index(
    const analyzer *a,
    const script_function *function,
    const char *name
)
{
    for (int i = 0; i < acount(function->parameters); i++) {
        if (!strcmp(a->out->symbols[function->parameters[i]].name, name)) {
            return i;
        }
    }
    return -1;
}
/* Keep an explicit ref wrapper until its parameter has been resolved. */
TSNode script_analyzer_call_argument(TSNode argument)
{
    if (!script_analyzer_is(argument, "named_argument") &&
        !script_analyzer_is(argument, "typed_argument")) {
        return argument;
    }
    TSNode value = script_analyzer_field(argument, "value");
    return ts_node_is_null(value) ? script_analyzer_field(argument, "source") : value;
}

int script_analyzer_reference_argument(analyzer *a, TSNode node, const script_catalog_parameter *parameter)
{
    char *name = script_analyzer_qualified_name(a, node);
    uint64_t id = 0;
    const char *canonical = name;
    int index = -1;
    if (!script_analyzer_is(node, "identifier") &&
        !script_analyzer_is(node, "member_expression")) {
        script_analyzer_diagnostic(a, node, "Symbol references use identifiers, not string names");
    }
    if (parameter->reference_kind == SCRIPT_REFERENCE_PROTOTYPE) {
        index = script_analyzer_find_capability(a, node, name, SCRIPT_CAPABILITY_PROTOTYPE);
        if (index >= 0) {
            id = a->catalog->capabilities[index].id;
            canonical = a->catalog->capabilities[index].name;
        }
    } else if (parameter->reference_kind == SCRIPT_REFERENCE_RELATIONSHIP) {
        if (!script_analyzer_is(node, "identifier")) {
            script_analyzer_diagnostic(a, node, "A relationship argument must name a relationship symbol");
        } else if (!strcmp(name, "ChildOf")) {
            id = SCRIPT_CHILD_OF;
        } else {
            for (int i = 0; i < a->catalog->relationship_count; i++) {
                if (!strcmp(a->catalog->relationships[i].name, name)) {
                    id = a->catalog->relationships[i].id;
                    index = i;
                    break;
                }
            }
        }
    } else {
        index = script_analyzer_find_type(a, node, name);
        if (index >= 0 && a->catalog->types[index].component) {
            id = a->catalog->types[index].id;
            canonical = a->catalog->types[index].key
                          ? a->catalog->types[index].key
                          : a->catalog->types[index].name;
        }
    }
    if (!id) {
        script_analyzer_diagnostic(
            a,
            node,
            "%s %s reference '%s'",
            index == -2 ? "Ambiguous" : "Unknown",
            parameter->reference_kind == SCRIPT_REFERENCE_PROTOTYPE
                ? "prototype"
            : parameter->reference_kind == SCRIPT_REFERENCE_RELATIONSHIP
                ? "relationship"
                : "component",
            name
        );
    } else {
        script_analyzer_use(
            a,
            node,
            -1,
            parameter->reference_kind == SCRIPT_REFERENCE_PROTOTYPE ? SCRIPT_HANDLE
                                                                    : SCRIPT_TYPE_USE,
            id
        );
    }
    script_expression value = {
        .kind = SCRIPT_EXPR_CONSTANT,
        .type = script_reference_type(parameter->reference_kind),
        .range = script_analyzer_range(a, node),
        .symbol = -1,
        .left = -1,
        .right = -1,
        .capability = id,
        .constant = { .type = script_reference_type(parameter->reference_kind).storage,
                      .str = xstrdup(canonical) }
    };
    if (parameter->reference_kind == SCRIPT_REFERENCE_RELATIONSHIP) {
        free((void *)value.constant.str);
        value.constant.u64 = id;
    }
    free(name);
    return script_analyzer_expr_add(a, value);
}
int script_analyzer_symbol_reference(analyzer *a, TSNode node, script_type expected)
{
    script_reference_kind kind = script_reference_kind_for_type(expected);
    if (kind == SCRIPT_REFERENCE_NONE) {
        char *name = script_analyzer_qualified_name(a, node);
        int found = 0;
        if (script_catalog_find_capability(a->catalog, name, SCRIPT_CAPABILITY_PROTOTYPE) >=
            0) {
            kind = SCRIPT_REFERENCE_PROTOTYPE;
            found++;
        }
        int type = script_catalog_find_type(a->catalog, name);
        if (type >= 0 && a->catalog->types[type].component) {
            kind = SCRIPT_REFERENCE_COMPONENT;
            found++;
        }
        for (int i = 0; i < a->catalog->relationship_count; i++) {
            if (!strcmp(a->catalog->relationships[i].name, name)) {
                kind = SCRIPT_REFERENCE_RELATIONSHIP;
                found++;
            }
        }
        if (!strcmp(name, "ChildOf") && kind != SCRIPT_REFERENCE_RELATIONSHIP) {
            kind = SCRIPT_REFERENCE_RELATIONSHIP;
            found++;
        }
        free(name);
        if (!found) {
            return -1;
        }
        if (found > 1) {
            script_analyzer_diagnostic(a, node, "Ambiguous symbol reference; provide a Prototype, Component or Relationship type");
        }
    }
    script_catalog_parameter parameter = { .reference_kind = kind,
                                           .type = script_reference_type(kind) };
    return script_analyzer_reference_argument(a, node, &parameter);
}
int script_analyzer_member_expression(analyzer *a, TSNode node, script_type expected)
{
    TSNode object = script_analyzer_field(node, "object"),
           member = script_analyzer_field(node, "member");
    if ((script_analyzer_is(object, "identifier") ||
         script_analyzer_is(object, "member_expression")) &&
        script_analyzer_lookup(a, object) < 0) {
        int reference = script_analyzer_symbol_reference(a, node, expected);
        if (reference >= 0) {
            return reference;
        }
    }
    char *name = script_analyzer_name_copy(a, member);
    int imported_constant = script_analyzer_package_constant(a, node);
    if (imported_constant >= 0) {
        script_symbol value = a->out->symbols[imported_constant];
        script_analyzer_use(a, member, imported_constant, SCRIPT_READ, value.id);
        free(name);
        return script_analyzer_expr_add(
            a,
            (script_expression){
                .kind = SCRIPT_EXPR_SYMBOL,
                .type = value.type,
                .range = script_analyzer_range(a, node),
                .symbol = imported_constant,
                .left = -1,
                .right = -1,
                .entry = -1,
                .member = value.id,
            }
        );
    }
    bool hidden = false;
    if (a->import_count && script_analyzer_is(object, "identifier")) {
        char *qualified = script_analyzer_qualified_name(a, node);
        hidden = script_analyzer_private_member(a, node, qualified);
        if (hidden) {
            script_analyzer_diagnostic(a, node, SCRIPT_PRIVATE_MEMBER, qualified);
        }
        free(qualified);
    }
    if (hidden) {
        free(name);
        return script_analyzer_expr_add(
            a,
            (script_expression){ .kind = SCRIPT_EXPR_ERROR,
                                 .type = script_analyzer_bad_type(),
                                 .range = script_analyzer_range(a, node),
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1 }
        );
    }
    if (script_analyzer_is(object, "builtin_reference") &&
        script_analyzer_text_is(a, object, "state")) {
        int symbol = script_analyzer_lookup_storage(a, member, SCRIPT_STATE);
        if (symbol < 0) {
            script_analyzer_diagnostic(a, member, "Unknown declared value '%s'", name);
            free(name);
            return script_analyzer_expr_add(
                a,
                (script_expression){ .kind = SCRIPT_EXPR_ERROR,
                                     .type = script_analyzer_bad_type(),
                                     .range = script_analyzer_range(a, node),
                                     .symbol = -1,
                                     .left = -1,
                                     .right = -1 }
            );
        }
        script_analyzer_resolve_initializer(a, symbol);
        script_analyzer_use(a, member, symbol, SCRIPT_READ, 0);
        script_symbol value = a->out->symbols[symbol];
        free(name);
        return script_analyzer_expr_add(
            a,
            (script_expression){ .kind = SCRIPT_EXPR_SYMBOL,
                                 .type = value.type,
                                 .range = script_analyzer_range(a, node),
                                 .symbol = symbol,
                                 .left = -1,
                                 .right = -1,
                                 .capability = value.capability,
                                 .writable = value.writable,
                                 .effects = value.kind == SCRIPT_STATE || value.kind == SCRIPT_SHARED_STATE
                                              ? SCRIPT_EFFECT_READ
                                              : 0 }
        );
    }
    int left = script_analyzer_expression(a, object, (script_type){ .flags = SCRIPT_TYPE_UNTYPED });
    for (int depth = 0;
         depth < SCRIPT_MAX_DEPTH &&
         script_analyzer_expr_type(a, left).storage == PIN_POINTER &&
         script_analyzer_expr_type(a, left).container == PIN_CONTAINER_SINGLE;
         depth++) {
        left = script_analyzer_dereference(a, object, left);
    }
    script_expression receiver = a->out->expressions[left];
    script_expression result = { .kind = SCRIPT_EXPR_MEMBER,
                                 .range = script_analyzer_range(a, node),
                                 .symbol = receiver.symbol,
                                 .left = left,
                                 .right = -1,
                                 .type = script_analyzer_bad_type(),
                                 .effects = receiver.effects };
    if (receiver.type.flags & SCRIPT_TYPE_QUERY_ROW) {
        const script_catalog_type
            *row = script_catalog_type_by_id(a->catalog, receiver.type.nominal);
        int index = row ? script_catalog_find_field(row, name) : -1;
        if (index < 0) {
            script_analyzer_diagnostic(a, member, "Query row has no field '%s'", name);
        } else {
            result.kind = SCRIPT_EXPR_QUERY_FIELD;
            result.type = row->fields[index].type;
            result.member = row->fields[index].id;
            script_analyzer_use(a, member, -1, SCRIPT_READ, result.member);
            result.offset = (size_t)index;
            result.symbol = -1;
            result.writable = false;
            if (receiver.kind == SCRIPT_EXPR_SYMBOL && receiver.symbol >= 0 &&
                (result.type.flags & SCRIPT_TYPE_OPTIONAL)) {
                int proof = -1;
                for (int i = 0; i < acount(a->out->symbols); i++) {
                    if (a->out->symbols[i].query_row_owner == receiver.symbol + 1 &&
                        a->out->symbols[i].query_field == index) {
                        proof = i;
                        break;
                    }
                }
                if (proof < 0) {
                    apush(
                        a->out->symbols,
                        ((script_symbol){ .id = result.member,
                                          .name = xstrdup("__query_field"),
                                          .kind = SCRIPT_LOCAL,
                                          .type = result.type,
                                          .scope = a->scope,
                                          .initializer = -1,
                                          .function = -1,
                                          .disabled = true,
                                          .query_row_owner = receiver.symbol + 1,
                                          .query_field = index,
                                          .declaration = { .start = -1, .end = -1 } })
                    );
                    proof = acount(a->out->symbols) - 1;
                }
                a->out->symbols[proof]
                    .mutations = a->out->symbols[receiver.symbol].mutations;
                result.symbol = proof;
                if (script_analyzer_present(a, proof)) {
                    result.type.flags &= ~SCRIPT_TYPE_OPTIONAL;
                }
            }
        }
        free(name);
        return script_analyzer_expr_add(a, result);
    }
    if ((receiver.type.flags & SCRIPT_TYPE_TASK) &&
        receiver.type.container == PIN_CONTAINER_SINGLE) {
        script_analyzer_diagnostic(a, member, "Await this task to read '%s'", name);
        free(name);
        return script_analyzer_expr_add(a, result);
    }
    if (receiver.type.flags & SCRIPT_TYPE_UNION) {
        const script_catalog_type
            *type = script_catalog_type_by_id(a->catalog, receiver.type.nominal);
        for (int i = 0; type && i < type->variant_count; i++) {
            const script_catalog_variant *v = &type->variants[i];
            if (!receiver.type.parameter || receiver.type.parameter != v->tag) {
                continue;
            }
            for (int j = 0; j < v->field_count; j++) {
                if (!strcmp(v->fields[j].name, name)) {
                    result.kind = SCRIPT_EXPR_UNWRAP;
                    result.type = v->fields[j].type;
                    result.tag = v->tag;
                    result.offset = (size_t)j;
                    result.symbol = -1;
                    free(name);
                    return script_analyzer_expr_add(a, result);
                }
            }
        }
        script_analyzer_diagnostic(a, member, "Narrow this union to a matching variant before reading '%s'", name);
        free(name);
        return script_analyzer_expr_add(a, result);
    }
    const script_expression *initializer = script_analyzer_result_source(a, left);
    int receiver_function = receiver.symbol >= 0
                              ? a->out->symbols[receiver.symbol].function
                              : -1;
    if (receiver_function < 0 && initializer &&
        (initializer->kind == SCRIPT_EXPR_CALL || initializer->kind == SCRIPT_EXPR_JOIN)) {
        receiver_function = initializer->right;
    }
    const script_catalog_capability
        *initializer_capability = initializer ? script_catalog_capability_by_id(
                                                    a->catalog,
                                                    initializer->capability
                                                )
                                              : NULL;
    bool output_root = receiver.kind == SCRIPT_EXPR_SYMBOL ||
                       receiver.kind == SCRIPT_EXPR_CALL ||
                       receiver.kind == SCRIPT_EXPR_UNWRAP ||
                       receiver.kind == SCRIPT_EXPR_JOIN;
    bool named_output = output_root && initializer_capability &&
                        script_analyzer_parameter_index(
                            initializer_capability->results,
                            initializer_capability->result_count,
                            name
                        ) >= 0;
    if (output_root && receiver_function >= 0) {
        const script_function *function = &a->out->functions[receiver_function];
        for (int i = 0; i < acount(function->results); i++) {
            named_output |= !strcmp(a->out->symbols[function->results[i]].name, name);
        }
    }
    if ((receiver.type.flags & SCRIPT_TYPE_RESULTS) || named_output) {
        const script_catalog_capability *capability = script_catalog_capability_by_id(
            a->catalog,
            named_output && initializer ? initializer->capability
                                        : receiver.type.nominal
        );
        const script_catalog_parameter *parameters = NULL;
        int count = 0;
        if (capability) {
            bool event = capability->kind == SCRIPT_CAPABILITY_EVENT;
            parameters = event ? capability->parameters : capability->results;
            count = event ? capability->parameter_count : capability->result_count;
        }
        int index = capability
                      ? script_analyzer_parameter_index(parameters, count, name)
                      : -1;
        if (index >= 0) {
            result.type = parameters[index].type;
            const script_expression *resolved = initializer;
            if (resolved && index < resolved->result_count) {
                result.type = resolved->results[index];
            }
            result.member = script_catalog_id("port", name);
            result.offset = (size_t)index;
            result.writable = (result.type.flags & SCRIPT_TYPE_REFERENCE) != 0;
        } else if (receiver_function >= 0) {
            script_function *function = &a->out->functions[receiver_function];
            bool payload = function->kind == SCRIPT_FUNCTION_HANDLER ||
                           function->kind == SCRIPT_FUNCTION_CONTINUATION ||
                           function->kind == SCRIPT_FUNCTION_MACRO_ENTRY;
            int *members = payload ? function->parameters : function->results;
            for (int i = 0; i < acount(members); i++) {
                script_symbol *symbol = &a->out->symbols[members[i]];
                if (!strcmp(symbol->name, name)) {
                    result.type = initializer && i < initializer->result_count
                                    ? initializer->results[i]
                                    : symbol->type;
                    result.member = symbol->id;
                    result.offset = (size_t)i;
                    break;
                }
            }
        }
        if (receiver.symbol >= 0) {
            const script_symbol *symbol = &a->out->symbols[receiver.symbol];
            result.writable |= symbol->kind == SCRIPT_LOCAL && receiver.writable;
        }
        if (result.type.flags & SCRIPT_TYPE_ERROR) {
            script_analyzer_diagnostic(a, member, "Unknown result/payload '%s'", name);
        }
        free(name);
        return script_analyzer_expr_add(a, result);
    }
    if (receiver.type.storage == PIN_HANDLE &&
        receiver.type.nominal == a->out->owner_type) {
        int index = script_analyzer_find_type(a, node, name);
        if (index >= 0 && a->catalog->types[index].component) {
            const script_catalog_type *type = &a->catalog->types[index];
            result.kind = SCRIPT_EXPR_COMPONENT;
            result.type = type->type;
            result.type.nominal = type->id;
            result.capability = type->id;
            result.member = type->id;
            result.effects |= SCRIPT_EFFECT_READ;
            result.writable = !type->derived;
            free(name);
            return script_analyzer_expr_add(a, result);
        }
        uint64_t relationship = !strcmp(name, "ChildOf") ? SCRIPT_CHILD_OF : 0;
        for (int i = 0; !relationship && i < a->catalog->relationship_count; i++) {
            if (!strcmp(a->catalog->relationships[i].name, name)) {
                relationship = a->catalog->relationships[i].id;
            }
        }
        if (relationship) {
            TSNode parent = ts_node_parent(node);
            while (script_analyzer_is(parent, "parenthesized_expression")) {
                parent = ts_node_parent(parent);
            }
            if (!script_analyzer_is(parent, "for_statement") ||
                !ts_node_is_null(script_analyzer_field(parent, "first"))) {
                script_analyzer_diagnostic(a, node, "Relationship targets require a streaming for-in loop");
            }
            result.kind = SCRIPT_EXPR_RELATION;
            result.type = (script_type){ .storage = PIN_HANDLE,
                                         .nominal = a->out->owner_type,
                                         .container = PIN_CONTAINER_ARRAY };
            result.capability = relationship;
            result.effects |= SCRIPT_EFFECT_READ;
            result.writable = false;
            free(name);
            return script_analyzer_expr_add(a, result);
        }
        script_analyzer_diagnostic(a, member, "%s component '%s'", index == -2 ? "Ambiguous" : "Unknown", name);
        free(name);
        return script_analyzer_expr_add(a, result);
    }
    if ((receiver.type.flags & SCRIPT_TYPE_OPTIONAL) &&
        !script_analyzer_present(a, receiver.symbol)) {
        script_analyzer_diagnostic(a, member, "Optional component requires a presence guard before member access");
    }
    if (script_analyzer_math_swizzle(a, member, left, name, &result)) {
        free(name);
        return script_analyzer_expr_add(a, result);
    }
    const script_catalog_type
        *type = script_catalog_type_by_id(a->catalog, receiver.type.nominal);
    int index = type ? script_catalog_find_field(type, name) : -1;
    if (index >= 0 && type->fields[index].constant) {
        const script_catalog_field *f = &type->fields[index];
        script_analyzer_use(a, member, -1, SCRIPT_READ, f->id);
        free(name);
        pin_value value = { .type = PIN_STR };
        const char *error = pin_value_text(&a->out->constants, &value, f->name, strlen(f->name));
        if (error) {
            script_analyzer_diagnostic(a, member, "%s", error);
        }
        return script_analyzer_expr_add(
            a,
            (script_expression){ .kind = SCRIPT_EXPR_CONSTANT,
                                 .type = f->type,
                                 .range = result.range,
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1,
                                 .member = f->id,
                                 .constant_snapshot = true,
                                 .constant = value }
        );
    }
    if (index >= 0) {
        const script_catalog_field *f = &type->fields[index];
        result.type = f->type;
        result.member = f->id;
        result.offset = f->offset;
        result.read = f->read;
        result.write = f->write;
        result.writable = f->writable && receiver.writable;
        result.effects |= type->component ? SCRIPT_EFFECT_READ : 0;
        const script_catalog_capability
            *getter = script_catalog_capability_by_id(a->catalog, f->read);
        if (getter) {
            result.effects |= getter->effects;
        }
        if (f->read_key) {
            result.effects |= SCRIPT_EFFECT_READ;
        }
        if (receiver.kind == SCRIPT_EXPR_MEMBER && receiver.read) {
            result.writable = false;
        }
        script_analyzer_use(a, member, receiver.symbol, SCRIPT_READ, f->id);
    } else {
        script_analyzer_diagnostic(
            a,
            member,
            "%s reflected member '%s'",
            index == -2 ? "Ambiguous" : "Unknown",
            name
        );
    }
    (void)expected;
    free(name);
    return script_analyzer_expr_add(a, result);
}
static int component_reference(analyzer *a, TSNode node)
{
    TSNode arguments = script_analyzer_field(node, "arguments");
    script_expression result = { .kind = SCRIPT_EXPR_COMPONENT,
                                 .type = script_analyzer_bad_type(),
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1,
                                 .range = script_analyzer_range(a, node),
                                 .effects = SCRIPT_EFFECT_READ,
                                 .writable = true };
    TSNode argument = ts_node_named_child(arguments, 0);
    char *name = script_analyzer_name_copy(a, argument);
    int index = script_analyzer_find_type(a, node, name);
    if (index < 0 || !a->catalog->types[index].component ||
        a->out->owner_symbol < 0 || script_analyzer_named_count(arguments) < 1 ||
        script_analyzer_named_count(arguments) > 2) {
        script_analyzer_diagnostic(a, node, "Component reference requires a registered component, optional field and an owner");
    } else {
        const script_catalog_type *type = &a->catalog->types[index];
        result.type = type->type;
        result.type.nominal = type->id;
        result.capability = result.member = type->id;
        result.writable = !type->derived;
        int owner = a->out->owner_symbol;
        result.left = script_analyzer_expr_add(
            a,
            (script_expression){ .kind = SCRIPT_EXPR_SYMBOL,
                                 .symbol = owner,
                                 .type = a->out->symbols[owner].type,
                                 .left = -1,
                                 .right = -1,
                                 .range = script_analyzer_range(a, node) }
        );
    }
    free(name);
    int reference = script_analyzer_expr_add(a, result);
    if (index >= 0 && script_analyzer_named_count(arguments) == 2) {
        TSNode field_node = ts_node_named_child(arguments, 1);
        char *field_name = script_analyzer_name_copy(a, field_node);
        char *part = field_name;
        while (*part) {
            char *next = strchr(part, '.');
            if (next) {
                *next = 0;
            }
            script_expression receiver = a->out->expressions[reference];
            const script_catalog_type
                *type = script_catalog_type_by_id(a->catalog, receiver.type.nominal);
            int field = type ? script_catalog_find_field(type, part) : -1;
            if (field < 0) {
                script_analyzer_diagnostic(a, field_node, "Unknown component field '%s'", part);
                break;
            }
            const script_catalog_field *member = &type->fields[field];
            reference = script_analyzer_expr_add(
                a,
                (script_expression){ .kind = SCRIPT_EXPR_MEMBER,
                                     .range = script_analyzer_range(a, node),
                                     .type = member->type,
                                     .symbol = -1,
                                     .left = reference,
                                     .right = -1,
                                     .member = member->id,
                                     .offset = member->offset,
                                     .read = member->read,
                                     .write = member->write,
                                     .writable = receiver.writable && member->writable,
                                     .effects = SCRIPT_EFFECT_READ }
            );
            if (!next) {
                break;
            }
            part = next + 1;
            if (!*part) {
                script_analyzer_diagnostic(a, field_node, "Component field path requires a member after '.'");
            }
        }
        free(field_name);
    }
    return reference;
}
static int fallback_expression(analyzer *a, TSNode node, script_type expected)
{
    script_expression result = { .kind = SCRIPT_EXPR_FALLBACK,
                                 .type = expected,
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1,
                                 .range = script_analyzer_range(a, node) };
    TSNode arguments = script_analyzer_field(node, "arguments");
    int values[] = { -1, -1, -1 };
    for (uint32_t i = 0; i < script_analyzer_named_count(arguments); i++) {
        TSNode argument = ts_node_named_child(arguments, i);
        if (ts_node_is_extra(argument)) {
            continue;
        }
        TSNode name_node = script_analyzer_field(argument, "name");
        TSNode value = script_analyzer_call_argument(argument);
        int ordinal = 0;
        if (!ts_node_is_null(name_node)) {
            char *name = script_analyzer_name_copy(a, name_node);
            ordinal = !strcmp(name, "binding") ? 1
                    : !strcmp(name, "default") ? 2
                                               : -1;
            free(name);
        } else {
            value = argument;
        }
        if (ordinal < 0 || values[ordinal] >= 0) {
            script_analyzer_diagnostic(a, argument, "Unknown or repeated fallback clause");
            continue;
        }
        script_type context = expected;
        context.flags &= ~SCRIPT_TYPE_REFERENCE;
        int index = script_analyzer_expression(a, value, context);
        script_type actual = script_analyzer_expr_type(a, index);
        if (ordinal == 0 && actual.flags & SCRIPT_TYPE_REFERENCE) {
            ordinal = 1;
        }
        if (values[ordinal] >= 0) {
            script_analyzer_diagnostic(a, argument, "Repeated fallback binding");
        }
        values[ordinal] = index;
        actual.flags &= ~SCRIPT_TYPE_REFERENCE;
        if (result.type.flags & SCRIPT_TYPE_UNTYPED) {
            result.type = actual;
        }
        script_analyzer_require(a, value, result.type, actual);
        if (ordinal == 1 && !a->out->expressions[index].writable) {
            script_analyzer_diagnostic(a, value, "Fallback binding requires a writable value");
        }
        if (ordinal == 2 &&
            (script_analyzer_expr_effects(a, index) ||
             (a->out->expressions[index].kind != SCRIPT_EXPR_CONSTANT &&
              a->out->expressions[index].kind != SCRIPT_EXPR_STRUCT &&
              a->out->expressions[index].kind != SCRIPT_EXPR_ARRAY))) {
            script_analyzer_diagnostic(a, value, "Fallback default requires a compatible literal");
        }
        apush(
            result.arguments,
            ((script_argument){ index, ordinal, 0, script_analyzer_range(a, argument) })
        );
    }
    if (values[1] < 0 && values[2] < 0) {
        script_analyzer_diagnostic(a, node, "Fallback requires a binding or default");
    }
    result.left = values[0] >= 0 ? values[0]
                : values[1] >= 0 ? values[1]
                                 : values[2];
    result.effects = script_analyzer_expr_effects(a, result.left);
    return script_analyzer_expr_add(a, result);
}

static const script_catalog_type *component_argument(
    const analyzer *a,
    const script_catalog_capability *capability,
    const script_expression *call
)
{
    if (capability->component) {
        return script_catalog_type_by_id(a->catalog, capability->component);
    }
    for (int i = 0; i < acount(call->arguments); i++) {
        script_argument source = call->arguments[i];
        if (source.parameter >= 0 && source.parameter < capability->parameter_count &&
            capability->parameters[source.parameter].reference_kind ==
                SCRIPT_REFERENCE_COMPONENT) {
            return script_catalog_type_by_id(
                a->catalog,
                a->out->expressions[source.value].capability
            );
        }
    }
    return NULL;
}

static bool field_argument(
    analyzer *a,
    script_expression *call,
    const script_catalog_capability *capability,
    const script_function *function,
    bool *seen,
    char *name,
    TSNode argument
)
{
    char *dot = strchr(name, '.');
    if (!dot) {
        return false;
    }
    *dot = 0;
    int parameter = capability ? script_analyzer_parameter_index(
                                     capability->parameters,
                                     capability->parameter_count,
                                     name
                                 )
                  : function
                      ? script_analyzer_authored_parameter_index(a, function, name)
                      : -1;
    *dot = '.';
    if (parameter < 0) {
        return false;
    }
    script_type type = capability
                         ? capability->parameters[parameter].type
                         : a->out->symbols[function->parameters[parameter]].type;
    if (type.flags & SCRIPT_TYPE_REFERENCE) {
        script_analyzer_diagnostic(a, argument, "Pointer parameter members cannot be named");
        seen[parameter] = true;
        return true;
    }
    if ((type.flags & SCRIPT_TYPE_UNTYPED) ||
        (type.storage == PIN_STRUCT && !type.nominal)) {
        for (int i = 0; capability && i < acount(call->arguments); i++) {
            script_argument source = call->arguments[i];
            if (source.parameter < 0 || source.parameter >= capability->parameter_count) {
                continue;
            }
            if (capability->parameters[source.parameter].reference_kind !=
                SCRIPT_REFERENCE_COMPONENT) {
                continue;
            }
            const script_catalog_type *component = script_catalog_type_by_id(
                a->catalog,
                a->out->expressions[source.value].capability
            );
            if (component) {
                type = component->type;
                type.nominal = component->id;
            }
        }
    }
    const script_catalog_type *record = script_catalog_type_by_id(a->catalog, type.nominal);
    int field = record ? script_catalog_find_field(record, dot + 1) : -1;
    if (field < 0) {
        script_analyzer_diagnostic(a, argument, "Unknown structured argument '%s'", name);
        return true;
    }
    int aggregate = -1;
    for (int i = 0; i < acount(call->arguments); i++) {
        if (call->arguments[i].parameter == parameter) {
            aggregate = call->arguments[i].value;
        }
    }
    if (aggregate < 0) {
        aggregate = script_analyzer_expr_add(
            a,
            (script_expression){ .kind = SCRIPT_EXPR_STRUCT,
                                 .partial = true,
                                 .type = type,
                                 .range = script_analyzer_range(a, argument),
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1 }
        );
        apush(
            call->arguments,
            ((script_argument){ aggregate, parameter, 0, script_analyzer_range(a, argument) })
        );
    }
    if (a->out->expressions[aggregate].kind != SCRIPT_EXPR_STRUCT) {
        script_analyzer_diagnostic(a, argument, "Whole and field arguments cannot be combined");
        return true;
    }
    TSNode value = script_analyzer_call_argument(argument);
    if (ts_node_is_null(value)) {
        value = script_analyzer_field(argument, "source");
    }
    script_catalog_field selected = record->fields[field];
    int expression = script_analyzer_expression(a, value, selected.type);
    script_analyzer_require(a, value, selected.type, script_analyzer_expr_type(a, expression));
    script_expression *structure = &a->out->expressions[aggregate];
    for (int i = 0; i < acount(structure->arguments); i++) {
        if (structure->arguments[i].member == selected.id) {
            script_analyzer_diagnostic(a, argument, "Duplicate structured argument '%s'", name);
        }
    }
    apush(
        structure->arguments,
        ((script_argument){ expression, field, selected.id, script_analyzer_range(a, argument) })
    );
    structure->argument_count = acount(structure->arguments);
    structure->effects |= script_analyzer_expr_effects(a, expression);
    call->effects |= structure->effects;
    seen[parameter] = true;
    return true;
}

static int overloaded_capability(analyzer *a, const char *name, TSNode arguments, int **values)
{
    int count = (int)script_analyzer_named_count(arguments);
    if (!*values) {
        *values = xmalloc((size_t)(count ? count : 1) * sizeof **values);
        for (int i = 0; i < count; i++) {
            (*values)[i] = -1;
        }
    }
    int found = -1, best_cost = INT_MAX, best_literal_cost = INT_MAX;
    for (int c = 0; c < a->catalog->capability_count; c++) {
        script_catalog_capability row = a->catalog->capabilities[c];
        const script_catalog_capability *candidate = &row;
        const char *full = candidate->qualified_name ? candidate->qualified_name
                                                     : candidate->name;
        if (!script_analyzer_native_capability(a, candidate->id) ||
            candidate->kind != SCRIPT_CAPABILITY_FUNCTION ||
            (!script_name_matches(full, name) &&
             !script_name_matches(candidate->name, name))) {
            continue;
        }
        bool *seen = xcalloc(
            (size_t)(candidate->parameter_count ? candidate->parameter_count : 1),
            sizeof *seen
        );
        bool matches = true;
        int cost = 0, literal_cost = 0;
        int ordinal = 0;
        for (int i = 0; matches && i < count; i++) {
            TSNode argument = ts_node_named_child(arguments, (uint32_t)i);
            if (ts_node_is_extra(argument)) {
                continue;
            }
            TSNode key = script_analyzer_field(argument, "name");
            int parameter = ordinal++;
            if (!ts_node_is_null(key)) {
                char *argument_name = script_analyzer_name_copy(a, key);
                parameter = script_analyzer_parameter_index(
                    candidate->parameters,
                    candidate->parameter_count,
                    argument_name
                );
                free(argument_name);
            }
            if (parameter < 0 || parameter >= candidate->parameter_count ||
                seen[parameter]) {
                matches = false;
                break;
            }
            seen[parameter] = true;
            script_type wanted = candidate->parameters[parameter].type;
            TSNode annotation = script_analyzer_field(argument, "type");
            script_type actual;
            if (!ts_node_is_null(annotation)) {
                actual = script_analyzer_type_node(a, annotation);
            } else if (candidate->parameters[parameter].reference_kind != SCRIPT_REFERENCE_NONE) {
                continue;
            } else {
                if ((*values)[i] < 0) {
                    TSNode value = ts_node_is_null(key)
                                     ? argument
                                     : script_analyzer_field(argument, "value");
                    if (ts_node_is_null(value)) {
                        value = script_analyzer_field(argument, "source");
                    }
                    (*values)[i] = script_analyzer_expression(
                        a,
                        value,
                        (script_type){ .flags = SCRIPT_TYPE_UNTYPED }
                    );
                }
                actual = script_analyzer_expr_type(a, (*values)[i]);
            }
            /* An untyped number never converts to text, so it cannot pick a
             * text overload. */
            bool text = wanted.storage == PIN_STR &&
                        script_type_numeric(actual) && !script_type_char(actual) &&
                        (actual.flags & SCRIPT_TYPE_UNTYPED);
            matches = !text && wanted.container == actual.container &&
                      script_catalog_type_assignable(a->catalog, wanted, actual);
            if (matches && script_type_numeric(wanted) &&
                script_type_numeric(actual) && wanted.storage != actual.storage) {
                /* Concrete operand widths choose the kernel first: 64-bit
                 * integers prefer f64 and the rest prefer number. Literal
                 * defaults break ties without widening a typed float operand. */
                bool wide = actual.storage == PIN_I64 || actual.storage == PIN_U64;
                pin_type preferred = wide ? PIN_F64 : PIN_F32;
                if (actual.flags & SCRIPT_TYPE_UNTYPED) {
                    literal_cost += wanted.storage == PIN_F32 ? 0 : 1;
                } else {
                    cost += wanted.storage == preferred ? 1 : 2;
                }
            }
        }
        for (int i = 0; matches && i < candidate->parameter_count; i++) {
            matches = seen[i] || candidate->parameters[i].has_default;
        }
        free(seen);
        if (matches && (cost < best_cost ||
                        (cost == best_cost && literal_cost <= best_literal_cost))) {
            found = cost < best_cost || literal_cost < best_literal_cost ? c : -2;
            best_cost = cost;
            best_literal_cost = literal_cost;
        }
    }
    return found;
}

static void require_reference_type(analyzer *a, TSNode node, script_type expected, script_type actual)
{
    if (((expected.flags | actual.flags) & SCRIPT_TYPE_ERROR) ||
        script_type_equal(
            script_catalog_canonical_type(a->catalog, expected),
            script_catalog_canonical_type(a->catalog, actual)
        )) {
        return;
    }
    if (!script_catalog_type_assignable(a->catalog, expected, actual)) {
        script_analyzer_require(a, node, expected, actual);
        return;
    }
    script_analyzer_diagnostic(a, node, "Pointer argument requires the exact parameter type");
}

/* The location a scoped pointer parameter aliases: `&place`, a scoped
 * parameter passed on, or the target of a managed pointer. */
int script_analyzer_scoped_argument(analyzer *a, TSNode value, script_type type)
{
    if (script_analyzer_is(value, "address_expression")) {
        return script_analyzer_expression(a, script_analyzer_field(value, "operand"), type);
    }
    int index = script_analyzer_expression(a, value, (script_type){ .flags = SCRIPT_TYPE_UNTYPED });
    const script_expression *e = &a->out->expressions[index];
    TSNode inner = value;
    while (script_analyzer_is(inner, "parenthesized_expression")) {
        inner = script_analyzer_field(inner, "expression");
    }
    /* `p^` of a scoped `p` reads its target, which a pointer argument then
     * dereferences; only `p` itself passes the location on. */
    if (e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0 &&
        (a->out->symbols[e->symbol].type.flags & SCRIPT_TYPE_REFERENCE) &&
        !script_analyzer_is(inner, "dereference_expression")) {
        return index;
    }
    if (e->type.storage == PIN_POINTER && e->type.container == PIN_CONTAINER_SINGLE) {
        return script_analyzer_dereference(a, value, index);
    }
    if (!(e->type.flags & SCRIPT_TYPE_ERROR)) {
        script_analyzer_diagnostic(a, value, "Pointer parameter requires &value or a pointer");
    }
    return index;
}
/* The local or reference parameter a reference argument reaches. */
static int local_root(const analyzer *a, int expression)
{
    while (expression >= 0) {
        const script_expression *e = &a->out->expressions[expression];
        if (e->kind == SCRIPT_EXPR_SYMBOL) {
            const script_symbol *s = e->symbol >= 0 ? &a->out->symbols[e->symbol]
                                                    : NULL;
            bool forwarded = s && s->kind == SCRIPT_PARAMETER &&
                             (s->type.flags & SCRIPT_TYPE_REFERENCE);
            return s && (s->kind == SCRIPT_LOCAL || forwarded) ? e->symbol : -1;
        }
        bool path = e->kind == SCRIPT_EXPR_MEMBER || e->kind == SCRIPT_EXPR_INDEX;
        if (!path) {
            return -1;
        }
        expression = e->left;
    }
    return -1;
}
/* A reference parameter that reaches a task boxes what its callers pass. */
void script_analyzer_finish_boxes(analyzer *a)
{
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < acount(a->out->expressions); i++) {
            const script_expression *e = &a->out->expressions[i];
            const script_function *f = e->kind == SCRIPT_EXPR_CALL &&
                                               !e->task && e->right >= 0
                                         ? &a->out->functions[e->right]
                                         : NULL;
            if (!f || f->kind != SCRIPT_FUNCTION_AUTHORED) {
                continue;
            }
            for (int j = 0; j < acount(e->arguments); j++) {
                int p = e->arguments[j].parameter;
                int root = p >= 0 && p < acount(f->parameters) &&
                                   a->out->symbols[f->parameters[p]].boxed
                             ? local_root(a, e->arguments[j].value)
                             : -1;
                if (root >= 0 && !a->out->symbols[root].boxed) {
                    a->out->symbols[root].boxed = changed = true;
                }
            }
        }
    }
}
/* T(value) converts to T. Integer narrowing clamps unless the argument is
 * named wrap, and a constant must fit T. A non-numeric T only supplies the
 * expected type. */
/* A conversion to an integer type reads an integer literal, in parentheses
 * too, as int64, or uint64 above int64. It leaves a fraction untyped, which
 * wrap rejects and an exact conversion types as T; other operands give their
 * literals int64. */
script_type script_analyzer_conversion_operand(const analyzer *a, TSNode value)
{
    while (script_analyzer_is(value, "parenthesized_expression")) {
        value = script_analyzer_field(value, "expression");
    }
    if (!script_analyzer_is(value, "number")) {
        return (script_type){ .storage = PIN_I64 };
    }
    char *text = script_analyzer_text_copy(a, value);
    bool hex = strpbrk(text, "xX") != NULL;
    script_type type = { .flags = SCRIPT_TYPE_UNTYPED };
    if (hex || !strpbrk(text, ".eE")) {
        /* Too large for uint64 saturates, and the literal reports its range. */
        uintmax_t parsed = strtoumax(text, NULL, hex ? 16 : 10);
        type = (script_type){ .storage = parsed > INT64_MAX ? PIN_U64 : PIN_I64 };
    }
    free(text);
    return type;
}
/* A literal, or a unary or binary expression, converted to an integer type
 * from a typed operand. */
static bool integer_operand(TSNode value, script_type operand, script_type type)
{
    while (script_analyzer_is(value, "parenthesized_expression")) {
        value = script_analyzer_field(value, "expression");
    }
    return !(operand.flags & SCRIPT_TYPE_UNTYPED) && type.storage >= PIN_I8 &&
           type.storage <= PIN_U64 &&
           (script_analyzer_is(value, "number") ||
            script_analyzer_is(value, "unary_expression") ||
            script_analyzer_is(value, "binary_expression"));
}
static int conversion(analyzer *a, TSNode node, script_type type)
{
    TSNode arguments = script_analyzer_field(node, "arguments"), value = { 0 };
    int count = 0;
    for (uint32_t i = 0; i < script_analyzer_named_count(arguments); i++) {
        TSNode child = ts_node_named_child(arguments, i);
        if (!ts_node_is_extra(child)) {
            value = child;
            count++;
        }
    }
    behavior_cast_mode mode = BEHAVIOR_CAST_EXACT;
    if (count != 1) {
        script_analyzer_diagnostic(a, node, "Conversion takes one value");
        return script_analyzer_expr_add(
            a,
            (script_expression){ .kind = SCRIPT_EXPR_CONSTANT,
                                 .type = script_analyzer_bad_type(),
                                 .range = script_analyzer_range(a, node),
                                 .left = -1,
                                 .right = -1,
                                 .symbol = -1,
                                 .entry = -1 }
        );
    }
    if (script_analyzer_is(value, "named_argument")) {
        TSNode label = script_analyzer_field(value, "name");
        mode = script_analyzer_text_is(a, label, "wrap") ? BEHAVIOR_CAST_WRAP
                                                         : BEHAVIOR_CAST_EXACT;
        if (mode == BEHAVIOR_CAST_EXACT) {
            script_analyzer_diagnostic(a, label, "A conversion argument may only be named wrap");
        }
        value = script_analyzer_field(value, "value");
    }
    bool character = script_type_char(type);
    if (character && mode != BEHAVIOR_CAST_EXACT) {
        script_analyzer_diagnostic(a, node, "char converts only with char(value)");
        mode = BEHAVIOR_CAST_EXACT;
    }
    /* char(literal) checks the literal as the conversion would check it.
     * An untyped integer operand converted to an integer type is an int64,
     * or uint64 beyond it, so a constant outside T does not fit T. */
    script_type operand = script_analyzer_conversion_operand(a, value);
    script_type literal = character ? (script_type){ .storage = PIN_I64 }
                        : mode == BEHAVIOR_CAST_EXACT &&
                                  !integer_operand(value, operand, type)
                            ? type
                            : operand;
    script_expression result = { .kind = SCRIPT_EXPR_CAST,
                                 .type = type,
                                 .range = script_analyzer_range(a, node),
                                 .left = script_analyzer_expression(a, value, literal),
                                 .right = -1,
                                 .symbol = -1,
                                 .entry = -1,
                                 .conversion = mode };
    script_type actual = script_analyzer_expr_type(a, result.left);
    bool numeric = script_type_numeric(actual) && script_type_numeric(type);
    bool integers = numeric && actual.storage <= PIN_U64 && type.storage <= PIN_U64;
    if (mode != BEHAVIOR_CAST_EXACT && !integers &&
        !(actual.flags & SCRIPT_TYPE_ERROR)) {
        script_analyzer_diagnostic(a, node, "wrap converts between integer types");
    }
    if (character && numeric && !integers) {
        script_analyzer_diagnostic(a, node, "char converts from an integer");
    }
    if (!numeric && script_catalog_type_assignable(a->catalog, type, actual)) {
        return result.left;
    }
    if (!numeric && !(actual.flags & SCRIPT_TYPE_ERROR)) {
        script_analyzer_diagnostic(a, node, "Conversion requires numeric values or the value's own type");
    }
    /* Constant integer conversions are checked with the other constants. */
    pin_value constant;
    if (character && integers && !script_type_char(actual) &&
        !script_analyzer_scalar_constant(a, result.left, actual.storage, &constant)) {
        uint64_t bits = behavior_scalar_bits(constant);
        bool negative = behavior_scalar_signed(constant.type) && (int64_t)bits < 0;
        if (negative || bits > 0x10FFFF || (bits >= 0xD800 && bits <= 0xDFFF)) {
            script_analyzer_diagnostic(a, node, "Constant does not fit in char");
        }
    }
    result.effects = script_analyzer_expr_effects(a, result.left);
    return script_analyzer_expr_add(a, result);
}
/* `xs.f(...)` calls the array function f with xs first. */
const script_catalog_capability *script_analyzer_array_method(analyzer *a, TSNode member)
{
    char *name = script_analyzer_name_copy(a, member);
    const script_catalog_capability *found = NULL;
    for (int i = 0; !found && i < a->catalog->capability_count; i++) {
        const script_catalog_capability *candidate = &a->catalog->capabilities[i];
        if (candidate->kind == SCRIPT_CAPABILITY_FUNCTION && candidate->parameter_count &&
            candidate->parameters[0].type.container == PIN_CONTAINER_ARRAY &&
            script_analyzer_native_capability(a, candidate->id) &&
            script_name_matches(candidate->name, name)) {
            found = candidate;
        }
    }
    free(name);
    return found;
}
/* The array functions that return their array changed; `slice` returns a new one. */
static const char *const ARRAY_CHANGERS[] = {
    "append",  "add_unique",   "extend",      "set_array_elem",
    "insert",  "remove_index", "remove_item", "clear",
    "reverse", "swap",         "resize",
};
bool script_analyzer_array_changer(const script_catalog_capability *function)
{
    for (size_t i = 0; i < sizeof ARRAY_CHANGERS / sizeof *ARRAY_CHANGERS; i++) {
        if (script_name_matches(function->name, ARRAY_CHANGERS[i])) {
            return true;
        }
    }
    return false;
}
static bool requires_pointer_value(const script_catalog *catalog, script_type type, int depth)
{
    if (type.container == PIN_CONTAINER_VIEW) {
        return !(type.flags & SCRIPT_TYPE_OPTIONAL);
    }
    if (type.container != PIN_CONTAINER_SINGLE || depth >= SCRIPT_MAX_DEPTH) {
        return false;
    }
    if (type.storage == PIN_POINTER) {
        return !(type.flags & SCRIPT_TYPE_OPTIONAL);
    }
    const script_catalog_type *record = type.storage == PIN_STRUCT
                                          ? script_catalog_type_by_id(catalog, type.nominal)
                                          : NULL;
    for (int i = 0; record && i < record->field_count; i++) {
        if (requires_pointer_value(catalog, record->fields[i].type, depth + 1)) {
            return true;
        }
    }
    return false;
}
/* Whether a value can hold a view: itself, or an element, key, field or union
 * payload. A pointer stops the walk, since comparing one compares identity
 * and reads nothing. Each named type is walked once: one already seen either
 * held no view or is still being walked further up. */
static bool holds_view(const script_catalog *catalog, script_type type, uint64_t **seen)
{
    if (type.container == PIN_CONTAINER_VIEW) {
        return true;
    }
    if (type.container != PIN_CONTAINER_SINGLE) {
        return (type.container == PIN_CONTAINER_MAP &&
                holds_view(catalog, script_catalog_key_type(catalog, type), seen)) ||
               holds_view(catalog, script_catalog_element_type(catalog, type), seen);
    }
    if (type.storage != PIN_STRUCT && type.storage != PIN_UNION) {
        return false;
    }
    const script_catalog_type *record = script_catalog_type_by_id(catalog, type.nominal);
    if (!record) {
        return false;
    }
    for (int i = 0; i < acount(*seen); i++) {
        if ((*seen)[i] == record->id) {
            return false;
        }
    }
    apush(*seen, record->id);
    for (int i = 0; i < record->field_count; i++) {
        if (holds_view(catalog, record->fields[i].type, seen)) {
            return true;
        }
    }
    for (int i = 0; i < record->variant_count; i++) {
        const script_catalog_variant *v = &record->variants[i];
        if (!v->tagged && holds_view(catalog, v->type, seen)) {
            return true;
        }
        for (int j = 0; j < v->field_count; j++) {
            if (holds_view(catalog, v->fields[j].type, seen)) {
                return true;
            }
        }
    }
    return false;
}
static int sequence_expression(
    analyzer *a,
    TSNode node,
    TSNode object,
    TSNode bounds,
    script_sequence_operation operation,
    int receiver
)
{
    script_expression result = { .kind = SCRIPT_EXPR_SEQUENCE,
                                 .range = script_analyzer_range(a, node),
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1,
                                 .member = operation };
    result.left = receiver >= 0
                    ? receiver
                    : script_analyzer_expression(a, object, (script_type){ .flags = SCRIPT_TYPE_UNTYPED });
    script_type receiver_type = script_analyzer_expr_type(a, result.left);
    script_type pointee = receiver_type.storage == PIN_POINTER &&
                                  receiver_type.container == PIN_CONTAINER_SINGLE
                            ? script_catalog_pointee_type(a->catalog, receiver_type)
                            : script_analyzer_bad_type();
    bool pointer_view = operation == SCRIPT_SEQUENCE_VIEW &&
                        pointee.container == PIN_CONTAINER_ARRAY;
    for (int depth = 0;
         !pointer_view && script_analyzer_is(node, "call_expression") &&
         depth < SCRIPT_MAX_DEPTH &&
         script_analyzer_expr_type(a, result.left).storage == PIN_POINTER &&
         script_analyzer_expr_type(a, result.left).container == PIN_CONTAINER_SINGLE;
         depth++) {
        result.left = script_analyzer_dereference(a, object, result.left);
    }
    result.type = pointer_view ? pointee : script_analyzer_expr_type(a, result.left);
    bool array = result.type.container == PIN_CONTAINER_ARRAY ||
                 result.type.container == PIN_CONTAINER_VIEW;
    /* A view reads its pointer's array; an owned array is the caller's value. */
    bool through_view = result.type.container == PIN_CONTAINER_VIEW;
    bool text = result.type.container == PIN_CONTAINER_SINGLE &&
                result.type.storage == PIN_STR;
    if (pointer_view && (receiver_type.flags & SCRIPT_TYPE_OPTIONAL)) {
        script_analyzer_diagnostic(a, object, "Narrow an optional pointer before creating an array view");
    }
    if (operation == SCRIPT_SEQUENCE_VIEW && array && !pointer_view) {
        script_analyzer_diagnostic(a, object, "Mutable array views require a pointer to an array");
    }
    if ((!array && !text) || (result.type.flags & SCRIPT_TYPE_OPTIONAL)) {
        script_analyzer_diagnostic(a, object, "Sequence operation requires a nonoptional array, view or string");
        result.type = script_analyzer_bad_type();
    }
    if (result.type.flags & SCRIPT_TYPE_LITERAL) {
        result.type.flags &= ~SCRIPT_TYPE_LITERAL;
        result.type.parameter = 0;
    }
    if (array) {
        result.type.container = operation == SCRIPT_SEQUENCE_VIEW
                                  ? PIN_CONTAINER_VIEW
                                  : PIN_CONTAINER_ARRAY;
    }
    result.effects = script_analyzer_expr_effects(a, result.left);
    if (through_view) {
        result.effects |= SCRIPT_EFFECT_READ;
    }
    if (operation == SCRIPT_SEQUENCE_LENGTH) {
        result.type = (script_type){ .storage = PIN_I32 };
    }
    if (operation == SCRIPT_SEQUENCE_SLICE || operation == SCRIPT_SEQUENCE_VIEW) {
        script_type integer = { .storage = PIN_I32 };
        TSNode start = script_analyzer_field(bounds, "start"),
               end = script_analyzer_field(bounds, "end");
        result.right = script_analyzer_expression(a, start, integer);
        int last = script_analyzer_expression(a, end, integer);
        script_analyzer_require(
            a,
            start,
            integer,
            script_analyzer_expr_type(a, result.right)
        );
        script_analyzer_require(a, end, integer, script_analyzer_expr_type(a, last));
        apush(result.arguments, ((script_argument){ last, 0, 0, script_analyzer_range(a, end) }));
        result.effects |= script_analyzer_expr_effects(a, result.right) |
                          script_analyzer_expr_effects(a, last);
        result.tag = script_type_tag(result.type);
        result.type.flags |= SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_BOXED_OPTIONAL;
    }
    return script_analyzer_expr_add(a, result);
}
int script_analyzer_sequence(analyzer *a, TSNode node, TSNode object, TSNode bounds, script_sequence_operation operation)
{
    return sequence_expression(a, node, object, bounds, operation, -1);
}
static bool sequence_receiver(analyzer *a, script_type type)
{
    for (int depth = 0; depth < SCRIPT_MAX_DEPTH && type.storage == PIN_POINTER &&
                        type.container == PIN_CONTAINER_SINGLE;
         depth++) {
        type = script_catalog_pointee_type(a->catalog, type);
    }
    return type.container == PIN_CONTAINER_ARRAY ||
           type.container == PIN_CONTAINER_VIEW ||
           (type.container == PIN_CONTAINER_SINGLE && type.storage == PIN_STR);
}
static bool migrated_self(const analyzer *a, const script_catalog_capability *capability)
{
    if (!a->migration_import || a->out->kind == SCRIPT_PROGRAM_LIBRARY ||
        !capability || capability->id != script_catalog_id("capability", "Self") ||
        capability->operation != SCRIPT_OPERATION_OWNER ||
        !script_catalog_operation_valid(capability) || a->out->owner_symbol < 0 ||
        script_analyzer_lookup_name(a, "self") != a->out->owner_symbol) {
        return false;
    }
    const script_symbol *owner = &a->out->symbols[a->out->owner_symbol];
    return owner->kind == SCRIPT_GLOBAL &&
           owner->id == script_catalog_id("owner", a->out->identity) &&
           script_type_equal(capability->results[0].type, owner->type);
}
/* The capability `emit` sends events through, or NULL when the catalog has
 * none. */
static const script_catalog_capability *event_sender(const analyzer *a)
{
    for (int i = 0; i < a->catalog->capability_count; i++) {
        if (a->catalog->capabilities[i].event_sender) {
            return &a->catalog->capabilities[i];
        }
    }
    return NULL;
}
/* The target of `emit target.Event(...)`, or -1 for `emit Event(target, ...)`,
 * whose first argument is the target. */
static int event_receiver(analyzer *a, TSNode target, const script_catalog_capability *sender)
{
    if (!script_analyzer_is(target, "member_expression")) {
        return -1;
    }
    assert(sender->parameter_count > 0);
    TSNode object = script_analyzer_field(target, "object");
    script_type type = sender->parameters[0].type;
    int receiver = script_analyzer_expression(a, object, type);
    script_analyzer_require(a, object, type, script_analyzer_expr_type(a, receiver));
    return receiver;
}
/* An argument's own effects; an argument that can hold a view is also read
 * through that view's pointer. */
static unsigned argument_effects(const analyzer *a, int expression)
{
    unsigned effects = script_analyzer_expr_effects(a, expression);
    uint64_t *seen = NULL;
    if (holds_view(a->catalog, script_analyzer_expr_type(a, expression), &seen)) {
        effects |= SCRIPT_EFFECT_READ;
    }
    afree(seen);
    return effects;
}
int script_analyzer_call_expression(analyzer *a, TSNode node, script_type expected)
{
    bool emitting = script_analyzer_is(node, "emit_expression");
    TSNode target = script_analyzer_field(node, emitting ? "event" : "function"),
           arguments = script_analyzer_field(node, "arguments");
    char *name = script_analyzer_qualified_name(a, target);
    if (!emitting && !strcmp(name, "fallback")) {
        free(name);
        return fallback_expression(a, node, expected);
    }
    if (!emitting && !strcmp(name, "component")) {
        free(name);
        return component_reference(a, node);
    }
    const script_catalog_capability *sender = emitting ? event_sender(a) : NULL;
    script_catalog_capability sender_signature = sender
                                                   ? *sender
                                                   : (script_catalog_capability){ 0 };
    sender = sender ? &sender_signature : NULL;
    if (emitting && !sender) {
        script_analyzer_diagnostic(a, node, "This program cannot send events");
        free(name);
        return script_analyzer_expr_add(
            a,
            (script_expression){ .kind = SCRIPT_EXPR_ERROR,
                                 .type = script_analyzer_bad_type(),
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1,
                                 .range = script_analyzer_range(a, node) }
        );
    }
    bool imported = !emitting && script_analyzer_import_namespace(a, target, name);
    int receiver = -1;
    bool qualified = script_analyzer_callable_symbol(a, name) >= 0 ||
                     script_catalog_find_capability(a->catalog, name, SCRIPT_CAPABILITY_FUNCTION) !=
                         -1 ||
                     script_catalog_find_capability(a->catalog, name, SCRIPT_CAPABILITY_ACTION) !=
                         -1;
    if (script_analyzer_is(target, "member_expression")) {
        TSNode object = script_analyzer_field(target, "object");
        int symbol = script_analyzer_is(object, "identifier")
                       ? script_analyzer_lookup(a, object)
                       : -1;
        if (symbol >= 0 &&
            !(a->out->symbols[symbol].type.flags & SCRIPT_TYPE_NAMESPACE)) {
            qualified = false;
        }
    }
    if (!emitting && !imported && !qualified &&
        script_analyzer_is(target, "member_expression")) {
        TSNode member = script_analyzer_field(target, "member");
        char *method = script_analyzer_name_copy(a, member);
        int count = script_analyzer_named_count(arguments);
        TSNode argument = count ? ts_node_named_child(arguments, 0) : (TSNode){ 0 };
        bool clone = !strcmp(method, "clone");
        bool length = !strcmp(method, "length");
        bool range = script_analyzer_is(argument, "range_expression");
        bool view = !strcmp(method, "view");
        bool slice = !strcmp(method, "slice");
        free(method);
        bool sequence = clone || length || view || (slice && range);
        TSNode object = script_analyzer_field(target, "object");
        if (sequence) {
            receiver = script_analyzer_expression(a, object, (script_type){ .flags = SCRIPT_TYPE_UNTYPED });
            sequence = sequence_receiver(a, script_analyzer_expr_type(a, receiver));
        }
        if (sequence) {
            bool single = clone || length;
            if (count != (single ? 0 : 1) || (!single && !range)) {
                script_analyzer_diagnostic(
                    a,
                    arguments,
                    single ? "This sequence method takes no arguments"
                           : "Range slicing requires one start..<end range"
                );
                free(name);
                return script_analyzer_expr_add(
                    a,
                    (script_expression){ .kind = SCRIPT_EXPR_ERROR,
                                         .type = script_analyzer_bad_type(),
                                         .range = script_analyzer_range(a, node),
                                         .left = -1,
                                         .right = -1,
                                         .symbol = -1 }
                );
            }
            free(name);
            return sequence_expression(
                a,
                node,
                script_analyzer_field(target, "object"),
                argument,
                length  ? SCRIPT_SEQUENCE_LENGTH
                : clone ? SCRIPT_SEQUENCE_CLONE
                : view  ? SCRIPT_SEQUENCE_VIEW
                        : SCRIPT_SEQUENCE_SLICE,
                receiver
            );
        }
    }
    int symbol = emitting ? -1
               : imported ? script_analyzer_package_function(a, target, name)
                          : script_analyzer_callable_symbol(a, name);
    const script_catalog_capability *capability = sender;
    int function_index = symbol >= 0 ? a->out->symbols[symbol].function : -1;
    if (symbol >= 0 && function_index < 0 && a->out->symbols[symbol].callable_binding) {
        capability = script_catalog_capability_by_id(
            a->catalog,
            a->out->symbols[symbol].capability
        );
    }
    int entry_index = -1;
    if (sender) {
        receiver = event_receiver(a, target, sender);
    }
    int *resolved_arguments = NULL;
    int specialized = emitting
                        ? -1
                        : script_analyzer_generic_call(a, node, symbol, &resolved_arguments);
    if (specialized >= 0) {
        function_index = specialized;
        symbol = a->out->functions[specialized].symbol;
    }
    if (symbol < 0 && !emitting) {
        int specialized_import = script_analyzer_package_specialize(a, node, name, &resolved_arguments);
        if (specialized_import >= 0) {
            function_index = specialized_import;
            symbol = a->out->functions[specialized_import].symbol;
        }
    }
    if (symbol < 0 && !emitting) {
        int imported = -1;
        if (script_analyzer_generic_import_call(a, node, name, &imported, &capability, &resolved_arguments) &&
            imported >= 0) {
            free(name);
            free(resolved_arguments);
            return imported;
        }
    }
    if (symbol < 0 && !capability && !emitting && !imported &&
        script_analyzer_is(target, "identifier")) {
        int math = script_analyzer_math_call(a, node, name, &resolved_arguments);
        if (math >= 0) {
            free(name);
            free(resolved_arguments);
            return math;
        }
    }
    if (symbol >= 0 && a->out->symbols[symbol].kind == SCRIPT_ACTION) {
        script_analyzer_resolve_initializer(a, symbol);
        capability = script_catalog_capability_by_id(
            a->catalog,
            a->out->symbols[symbol].capability
        );
        int initializer = a->out->symbols[symbol].initializer;
        if (initializer >= 0 && a->out->expressions[initializer].kind == SCRIPT_EXPR_CALL &&
            a->out->expressions[initializer].right >= 0) {
            function_index = a->out->expressions[initializer].right;
            capability = NULL;
        }
    }
    if (!capability && function_index < 0) {
        int index = script_analyzer_find_capability(a, node, name, SCRIPT_CAPABILITY_FUNCTION);
        if (index == -2) {
            index = overloaded_capability(a, name, arguments, &resolved_arguments);
        }
        if (index == -1) {
            index = script_analyzer_find_capability(a, node, name, SCRIPT_CAPABILITY_ACTION);
        }
        if (index >= 0) {
            capability = &a->catalog->capabilities[index];
        } else if (!imported && script_analyzer_is(target, "member_expression")) {
            TSNode object = script_analyzer_field(target, "object");
            int action = script_analyzer_lookup(a, object);
            if (action >= 0 && a->out->symbols[action].kind == SCRIPT_ACTION) {
                script_analyzer_resolve_initializer(a, action);
                const script_catalog_capability *candidate = script_catalog_capability_by_id(
                    a->catalog,
                    a->out->symbols[action].capability
                );
                char *entry = script_analyzer_name_copy(a, script_analyzer_field(target, "member"));
                for (int i = 0; candidate && i < candidate->entry_count; i++) {
                    if (script_name_matches(candidate->entries[i], entry)) {
                        capability = candidate;
                        symbol = action;
                        entry_index = i;
                        break;
                    }
                }
                int initializer = a->out->symbols[action].initializer;
                int owner = initializer >= 0 &&
                                    a->out->expressions[initializer].kind == SCRIPT_EXPR_CALL
                              ? a->out->expressions[initializer].right
                              : -1;
                for (int i = 0; owner >= 0 && i < acount(a->out->functions); i++) {
                    const script_function *child = &a->out->functions[i];
                    if (child->kind == SCRIPT_FUNCTION_MACRO_ENTRY &&
                        child->parent == owner && child->entry &&
                        script_name_matches(child->entry, entry)) {
                        symbol = action;
                        capability = NULL;
                        function_index = owner;
                        entry_index = i;
                        break;
                    }
                }
                free(entry);
            }
            if (!capability && function_index < 0) {
                if (receiver < 0) {
                    receiver = script_analyzer_expression(a, object, (script_type){ .flags = SCRIPT_TYPE_UNTYPED });
                }
                for (int depth = 0;
                     depth < SCRIPT_MAX_DEPTH &&
                     script_analyzer_expr_type(a, receiver).storage == PIN_POINTER &&
                     script_analyzer_expr_type(a, receiver).container == PIN_CONTAINER_SINGLE;
                     depth++) {
                    receiver = script_analyzer_dereference(a, object, receiver);
                }
                char *method = script_analyzer_name_copy(a, script_analyzer_field(target, "member"));
                uint64_t receiver_type = script_analyzer_expr_type(a, receiver).nominal;
                for (int i = 0; i < a->catalog->capability_count; i++) {
                    const script_catalog_capability *candidate = &a->catalog->capabilities[i];
                    if (!script_analyzer_native_capability(a, candidate->id)) {
                        continue;
                    }
                    if (script_catalog_canonical_type(
                            a->catalog,
                            (script_type){ .nominal = candidate->receiver }
                        )
                                .nominal ==
                            script_catalog_canonical_type(a->catalog, (script_type){ .nominal = receiver_type })
                                .nominal &&
                        receiver_type && candidate->method_name &&
                        script_name_matches(candidate->method_name, method)) {
                        if (capability) {
                            script_analyzer_diagnostic(a, target, "Ambiguous receiver method '%s'", method);
                            capability = NULL;
                            break;
                        }
                        capability = candidate;
                    }
                }
                free(method);
            }
            if (!capability && function_index < 0 &&
                (script_analyzer_expr_type(a, receiver).container == PIN_CONTAINER_ARRAY ||
                 script_analyzer_expr_type(a, receiver).container == PIN_CONTAINER_VIEW)) {
                TSNode member = script_analyzer_field(target, "member");
                capability = script_analyzer_array_method(a, member);
            }
        }
        /* Numeric type names always convert; other types yield to a
         * capability of the same name, such as a literal node. */
        bool numeric = script_type_numeric(script_builtin_type(name));
        script_type converted = (index == -1 || (index >= 0 && numeric)) &&
                                        !imported &&
                                        script_analyzer_is(target, "identifier")
                                  ? script_analyzer_conversion_type(a, target, name)
                                  : script_analyzer_bad_type();
        if (function_index < 0 && !(converted.flags & SCRIPT_TYPE_ERROR) &&
            (index == -1 || numeric)) {
            free(name);
            free(resolved_arguments);
            return conversion(a, node, converted);
        }
        if (!capability && function_index < 0 &&
            script_analyzer_private_member(a, target, name)) {
            script_analyzer_diagnostic(a, target, SCRIPT_PRIVATE_MEMBER, name);
        } else if (!capability && function_index < 0) {
            script_analyzer_diagnostic(a, target, "%s callable '%s'", index == -2 ? "Ambiguous" : "Unknown", name);
        }
    }
    if (capability && capability->state_access != SCRIPT_STATE_ACCESS_NONE) {
        script_analyzer_diagnostic(a, target, "Read or assign the state by name");
    }
    script_catalog_capability selected = { 0 };
    script_catalog_parameter *selected_parameters = NULL;
    uint64_t selected_event = 0;
    if (capability && capability->event_sender && !emitting) {
        script_analyzer_diagnostic(a, target, "Send events with emit target.Event(...)");
    } else if (emitting) {
        TSNode event_name = script_analyzer_is(target, "member_expression")
                              ? script_analyzer_field(target, "member")
                              : target;
        char *spelled = script_analyzer_name_copy(a, event_name);
        int event = script_analyzer_find_capability(a, node, spelled, SCRIPT_CAPABILITY_EVENT);
        free(spelled);
        if (event < 0) {
            script_analyzer_diagnostic(a, event_name, "%s event declaration", event == -2 ? "Ambiguous" : "Unknown");
        } else {
            const script_catalog_capability *payload = &a->catalog->capabilities[event];
            selected_event = payload->id;
            script_analyzer_use(a, event_name, -1, SCRIPT_HANDLE, payload->id);
            for (int i = 0; i < capability->parameter_count; i++) {
                apush(selected_parameters, capability->parameters[i]);
            }
            for (int i = 0; i < payload->parameter_count; i++) {
                apush(selected_parameters, payload->parameters[i]);
            }
            selected = *capability;
            selected.parameters = selected_parameters;
            selected.parameter_count = acount(selected_parameters);
            capability = &selected;
        }
    }
    if (capability && !a->migration_import) {
        if (capability->component &&
            (capability->component_access == SCRIPT_COMPONENT_ACCESS_READ ||
             capability->component_access == SCRIPT_COMPONENT_ACCESS_WRITE ||
             capability->component_access == SCRIPT_COMPONENT_ACCESS_HAS)) {
            script_analyzer_diagnostic(a, target, "Generated component accessors are import-only; use entity.Component properties");
        }
        if (capability->id == script_catalog_id("capability", "Self")) {
            script_analyzer_diagnostic(a, target, "Use self instead of Self()");
        }
    }
    /* Argument analysis may import another template environment and grow the
     * catalog. Signature spans are arena-owned; the catalog row can move. */
    if (capability && capability != &selected) {
        selected = *capability;
        capability = &selected;
    }
    const script_function *function = function_index >= 0
                                        ? &a->out->functions[function_index]
                                        : NULL;
    int parameter_count = capability ? capability->parameter_count
                        : function   ? acount(function->parameters)
                                     : 0;
    bool *seen = xcalloc((size_t)(parameter_count ? parameter_count : 1), sizeof *seen);
    script_expression result = {
        .kind = SCRIPT_EXPR_CALL,
        .range = script_analyzer_range(a, node),
        .symbol = symbol,
        .left = receiver,
        .right = function_index,
        .entry = entry_index,
        .type = script_analyzer_bad_type(),
        .capability = capability ? capability->id
                    : function && function->symbol >= 0
                        ? a->out->symbols[function->symbol].id
                    : symbol >= 0 ? a->out->symbols[symbol].id
                                  : 0,
        .member = selected_event,
        .effects = capability ? capability->effects : 0
    };
    /* Legacy Self imports become the same bound self expression. Only a
     * library's self reads its caller's invocation; behavior/system self is
     * stable. Keep the catalog contract and the migration oracle exact. */
    if (migrated_self(a, capability)) {
        result.effects &= ~(unsigned)SCRIPT_EFFECT_INVOCATION_READ;
    }
    if (capability && a->out->kind == SCRIPT_PROGRAM_LIBRARY &&
        (capability->id == script_catalog_id("capability", "Self") ||
         (capability->component && capability->component_entity < 0))) {
        result.effects |= SCRIPT_EFFECT_INVOCATION_READ;
    }
    if (symbol >= 0 && a->out->symbols[symbol].kind == SCRIPT_ACTION &&
        a->out->symbols[symbol].initializer >= 0) {
        result.instance = a->out->symbols[symbol].id;
    }
    int ordinal = 0;
    if (receiver >= 0 && capability && parameter_count) {
        apush(
            result.arguments,
            ((script_argument){ receiver, 0, 0, script_analyzer_range(a, target) })
        );
        seen[0] = true;
        ordinal = 1;
        result.effects |= argument_effects(a, receiver);
    }
    for (uint32_t i = 0; i < script_analyzer_named_count(arguments); i++) {
        TSNode argument = ts_node_named_child(arguments, i);
        if (ts_node_is_extra(argument)) {
            continue;
        }
        if (resolved_arguments && resolved_arguments[i] == -2) {
            continue;
        }
        TSNode name_node = script_analyzer_field(argument, "name"),
               value = script_analyzer_call_argument(argument);
        int parameter = ordinal++;
        if (!ts_node_is_null(name_node)) {
            char *argument_name = script_analyzer_qualified_name(a, name_node);
            if (script_analyzer_named_count(name_node) > 1 &&
                field_argument(a, &result, capability, function, seen, argument_name, argument)) {
                free(argument_name);
                function = function_index >= 0 ? &a->out->functions[function_index]
                                               : NULL;
                continue;
            }
            parameter = script_analyzer_callable_parameter_index(a, symbol, argument_name);
            if (parameter == -2) {
                parameter = capability
                              ? script_analyzer_parameter_index(capability->parameters, parameter_count, argument_name)
                          : function
                              ? script_analyzer_authored_parameter_index(a, function, argument_name)
                              : -1;
            }
            if (parameter == -1 && capability) {
                parameter = variadic_parameter(
                    capability,
                    argument_name,
                    (int)script_analyzer_named_count(arguments)
                );
            }
            if (parameter < 0) {
                script_analyzer_diagnostic(
                    a,
                    name_node,
                    "%s argument '%s'",
                    parameter == -2 ? "Ambiguous" : "Unknown",
                    argument_name
                );
            }
            free(argument_name);
        } else {
            value = argument;
        }
        if (ts_node_is_null(value)) {
            value = script_analyzer_field(argument, "source");
        }
        script_type
            type = parameter >= 0 && parameter < parameter_count
                     ? capability
                         ? capability->parameters[parameter].type
                         : a->out->symbols[function->parameters[parameter]].type
                     : (script_type){ .flags = SCRIPT_TYPE_UNTYPED };
        /* A prior concrete array argument binds its generic element before
         * analyzing later literals, including receiver.append({...}). */
        if (capability && (type.flags & SCRIPT_TYPE_UNTYPED) && type.parameter) {
            for (int j = 0; j < acount(result.arguments); j++) {
                const script_argument *bound = &result.arguments[j];
                if (bound->parameter < 0 || bound->parameter >= parameter_count ||
                    capability->parameters[bound->parameter].type.parameter !=
                        type.parameter) {
                    continue;
                }
                script_type actual = script_analyzer_expr_type(a, bound->value);
                if (actual.container != PIN_CONTAINER_ARRAY &&
                    actual.container != PIN_CONTAINER_VIEW) {
                    continue;
                }
                /* The parameter, not the argument, says whether it is a
                 * reference: `ref xs` binds the element, not a ref element. */
                unsigned reference = type.flags & SCRIPT_TYPE_REFERENCE;
                if (type.container == PIN_CONTAINER_SINGLE) {
                    type = script_catalog_element_type(a->catalog, actual);
                } else if (type.container == PIN_CONTAINER_ARRAY) {
                    type = actual;
                }
                type.flags = (type.flags & ~SCRIPT_TYPE_REFERENCE) | reference;
                break;
            }
        }
        if (capability && capability->component_access == SCRIPT_COMPONENT_ACCESS_WRITE &&
            type.storage == PIN_STRUCT && !type.nominal) {
            const script_catalog_type *component = component_argument(a, capability, &result);
            if (component) {
                type = component->type;
                type.nominal = component->id;
            }
        }
        bool requires_ref = (type.flags & SCRIPT_TYPE_REFERENCE) != 0;
        TSNode annotation = script_analyzer_field(argument, "type");
        if (!ts_node_is_null(annotation)) {
            script_type annotated = script_analyzer_type_node(a, annotation);
            if (requires_ref) {
                /* `name: ^T = &value` spells the pointer the caller passes. */
                script_type pointee = script_catalog_pointee_type(a->catalog, annotated);
                if (pointee.flags & SCRIPT_TYPE_ERROR) {
                    script_analyzer_diagnostic(a, annotation, "Pointer parameter requires a ^T annotation");
                }
                require_reference_type(a, annotation, type, pointee);
            } else {
                script_analyzer_require(a, annotation, type, annotated);
                type = annotated;
            }
        }
        bool reference = capability && parameter >= 0 && parameter < parameter_count &&
                         capability->parameters[parameter].reference_kind !=
                             SCRIPT_REFERENCE_NONE &&
                         (script_analyzer_is(value, "identifier") ||
                          script_analyzer_is(value, "string")) &&
                         (script_analyzer_is(value, "string") ||
                          script_analyzer_lookup(a, value) < 0);
        if (capability && parameter >= 0 && parameter < parameter_count &&
            capability->parameters[parameter].reference_kind != SCRIPT_REFERENCE_NONE) {
            type = script_reference_type(capability->parameters[parameter].reference_kind);
        }
        int expression_index = !reference && resolved_arguments &&
                                       resolved_arguments[i] >= 0
                                 ? resolved_arguments[i]
                             : reference ? script_analyzer_reference_argument(
                                               a,
                                               value,
                                               &capability->parameters[parameter]
                                           )
                             : requires_ref
                                 ? script_analyzer_scoped_argument(a, value, type)
                                 : script_analyzer_expression(a, value, type);
        /* Overload resolution read the argument without a type; a literal
         * takes the chosen parameter's, so an f64 one keeps every digit. */
        if (!reference && resolved_arguments && resolved_arguments[i] >= 0) {
            script_analyzer_contextual_numeric(a, expression_index, type);
        }
        if (capability && parameter >= 0 && parameter < parameter_count &&
            capability->parameters[parameter].c_string) {
            script_analyzer_c_string_argument(
                a,
                value,
                expression_index,
                capability->parameters[parameter].name
            );
        }
        if (capability && parameter >= 0 && parameter < parameter_count &&
            capability->parameters[parameter].default_owner &&
            a->out->owner_symbol >= 0) {
            const script_expression *provided = &a->out->expressions[expression_index];
            if (provided->kind == SCRIPT_EXPR_CONSTANT &&
                provided->type.storage == PIN_HANDLE && !provided->constant.u64) {
                int owner = a->out->owner_symbol;
                expression_index = script_analyzer_expr_add(
                    a,
                    (script_expression){ .kind = SCRIPT_EXPR_SYMBOL,
                                         .type = a->out->symbols[owner].type,
                                         .symbol = owner,
                                         .left = -1,
                                         .right = -1,
                                         .range = script_analyzer_range(a, value) }
                );
            }
        }
        if (parameter >= 0 && parameter < parameter_count) {
            if (seen[parameter]) {
                script_analyzer_diagnostic(a, argument, "Argument supplied more than once");
            }
            seen[parameter] = true;
            script_type actual = script_analyzer_expr_type(a, expression_index);
            if (capability && (type.flags & SCRIPT_TYPE_UNTYPED) &&
                type.container == PIN_CONTAINER_ARRAY &&
                actual.container == PIN_CONTAINER_VIEW) {
                type.container = PIN_CONTAINER_VIEW;
            }
            if (requires_ref && actual.storage == PIN_POINTER &&
                a->out->expressions[expression_index].kind == SCRIPT_EXPR_SYMBOL) {
                int referenced = a->out->expressions[expression_index].symbol;
                if (referenced >= 0) {
                    actual = a->out->symbols[referenced].type;
                }
            }
            if (requires_ref) {
                require_reference_type(a, value, type, actual);
            } else {
                script_analyzer_require(a, value, type, actual);
            }
            if ((type.flags & SCRIPT_TYPE_REFERENCE) &&
                !a->out->expressions[expression_index].writable) {
                script_analyzer_diagnostic(a, value, "Pointer argument requires a writable location");
            }
        } else if (capability && !capability->variadic) {
            script_analyzer_diagnostic(a, argument, "Too many arguments");
        }
        apush(
            result.arguments,
            ((script_argument){ expression_index,
                                parameter,
                                capability && parameter >= 0 && parameter < parameter_count
                                    ? capability->parameters[parameter].member
                                    : 0,
                                script_analyzer_range(a, argument) })
        );
        result.effects |= argument_effects(a, expression_index);
        function = function_index >= 0 ? &a->out->functions[function_index] : NULL;
    }
    bool callable_parameter = script_analyzer_callable_parameter_index(a, symbol, "") !=
                            -2;
    for (int i = 0; i < parameter_count; i++) {
        if (!seen[i] && symbol >= 0 && a->out->symbols[symbol].kind == SCRIPT_ACTION &&
            a->out->symbols[symbol].initializer >= 0) {
            const script_expression
                *configuration = &a->out->expressions[a->out->symbols[symbol].initializer];
            for (int j = 0; j < configuration->argument_count; j++) {
                if (configuration->arguments[j].parameter == i) {
                    script_argument inherited = configuration->arguments[j];
                    apush(result.arguments, inherited);
                    result.effects |= script_analyzer_expr_effects(a, inherited.value);
                    seen[i] = true;
                    break;
                }
            }
        }
        int authored_default = -1;
        if (!seen[i] && function && !callable_parameter) {
            int parameter = function->parameters[i];
            script_analyzer_resolve_initializer(a, parameter);
            function = &a->out->functions[function_index];
            authored_default = a->out->symbols[parameter].initializer;
            if (authored_default >= 0) {
                apush(
                    result.arguments,
                    ((script_argument){ authored_default, i, 0, script_analyzer_range(a, arguments) })
                );
                result.effects |= script_analyzer_expr_effects(a, authored_default);
            }
        }
        if (!seen[i] && authored_default < 0 &&
            (callable_parameter || !capability || !capability->parameters[i].has_default)) {
            script_analyzer_diagnostic(
                a,
                arguments,
                "Missing argument '%s'",
                capability ? capability->parameters[i].name
                           : a->out->symbols[function->parameters[i]].name
            );
        }
        if (!seen[i] && capability && !callable_parameter &&
            capability->parameters[i].default_owner && a->out->owner_symbol >= 0) {
            int owner = a->out->owner_symbol;
            int index = script_analyzer_expr_add(
                a,
                (script_expression){ .kind = SCRIPT_EXPR_SYMBOL,
                                     .type = a->out->symbols[owner].type,
                                     .symbol = owner,
                                     .effects = a->out->kind == SCRIPT_PROGRAM_LIBRARY
                                                  ? SCRIPT_EFFECT_INVOCATION_READ
                                                  : 0,
                                     .left = -1,
                                     .right = -1,
                                     .range = script_analyzer_range(a, arguments) }
            );
            apush(
                result.arguments,
                ((script_argument){ index, i, 0, script_analyzer_range(a, arguments) })
            );
            result.effects |= script_analyzer_expr_effects(a, index);
            seen[i] = true;
        }
        if (!seen[i] && capability && !callable_parameter &&
            capability->parameters[i].has_default) {
            pin_value value = { 0 };
            if (pin_value_capture_to_arena(
                    &a->out->constants,
                    &value,
                    &capability->parameters[i].default_value
                )) {
                script_analyzer_diagnostic(a, arguments, "Catalog default cannot be snapshotted");
            }
            int index = script_analyzer_expr_add(
                a,
                (script_expression){ .kind = SCRIPT_EXPR_CONSTANT,
                                     .type = capability->parameters[i].type,
                                     .constant = value,
                                     .constant_snapshot = true,
                                     .range = script_analyzer_range(a, arguments),
                                     .symbol = -1,
                                     .left = -1,
                                     .right = -1 }
            );
            if (capability->parameters[i].c_string) {
                script_analyzer_c_string_argument(
                    a,
                    arguments,
                    index,
                    capability->parameters[i].name
                );
            }
            apush(
                result.arguments,
                ((script_argument){ index, i, 0, script_analyzer_range(a, arguments) })
            );
        }
    }
    free(seen);
    free(resolved_arguments);
    /* An unawaited call to an authored function or library function that
     * waits starts a task. Action initializers and invocations keep their own
     * lifetimes. */
    bool spawnable = !result.instance && entry_index < 0 &&
                     !ts_node_eq(node, a->awaited) &&
                     !script_analyzer_is(unparenthesized_parent(node), "action_declaration") &&
                     !(symbol >= 0 && a->out->symbols[symbol].kind == SCRIPT_ACTION);
    if (capability && script_name_matches(capability->name, "Resize")) {
        for (int i = 0; i < acount(result.arguments); i++) {
            if (result.arguments[i].parameter != 0) {
                continue;
            }
            script_type array = script_analyzer_expr_type(a, result.arguments[i].value);
            script_type element = script_catalog_element_type(a->catalog, array);
            if (array.container == PIN_CONTAINER_ARRAY &&
                requires_pointer_value(a->catalog, element, 0)) {
                script_analyzer_diagnostic(a, node, "Resizing this array requires an explicit initialized element value");
            }
        }
    }
    bool authored_task = spawnable && function && !capability &&
                         function->kind == SCRIPT_FUNCTION_AUTHORED &&
                         script_analyzer_function_waits(a, function_index);
    bool library_task = spawnable && capability &&
                        capability->kind == SCRIPT_CAPABILITY_FUNCTION &&
                        (capability->effects & SCRIPT_EFFECT_SUSPEND);
    /* An unawaited action continues before its result exists. */
    TSNode use = unparenthesized_parent(node);
    if (!result.instance && entry_index < 0 && !ts_node_eq(node, a->awaited) &&
        capability && capability->kind == SCRIPT_CAPABILITY_ACTION &&
        (capability->effects & SCRIPT_EFFECT_SUSPEND) && capability->result_count &&
        !script_analyzer_is(use, "expression_statement") &&
        !script_analyzer_is(use, "action_declaration")) {
        script_analyzer_diagnostic(a, node, "Await this call before using its result");
    }
    if (authored_task || library_task) {
        result.task = true;
        result.effects &= ~SCRIPT_EFFECT_SUSPEND;
        int results = library_task ? capability->result_count
                                   : acount(function->results);
        script_type success = results == 1
                                ? (library_task
                                       ? capability->results[0].type
                                       : a->out->symbols[function->results[0]].type)
                            : results > 1
                                ? (script_type){ .storage = PIN_STRUCT,
                                                 .nominal = result.capability,
                                                 .flags = SCRIPT_TYPE_RESULTS }
                                : (script_type){ .storage = PIN_FLOW };
        result.type = script_analyzer_task_type(
            a,
            success,
            library_task ? capability->id : a->out->symbols[function->symbol].id
        );
        for (int i = 0; i < acount(result.arguments); i++) {
            int p = result.arguments[i].parameter;
            script_type type = p < 0 ? script_analyzer_bad_type()
                             : library_task
                                 ? capability->parameters[p].type
                                 : a->out->symbols[function->parameters[p]].type;
            int local = type.flags & SCRIPT_TYPE_REFERENCE
                          ? local_root(a, result.arguments[i].value)
                          : -1;
            if (local >= 0) {
                a->out->symbols[local].boxed = true;
            }
        }
    }
    int count = result.task ? 0
              : capability  ? capability->result_count
              : function    ? acount(function->results)
                            : 0;
    if (count == 1) {
        result.type = capability ? capability->results[0].type
                                 : a->out->symbols[function->results[0]].type;
        /* Reflected setters can return the whole component after a field write.
         * Only a generic result needs specialization from the selected type. */
        if (capability && result.type.storage == PIN_STRUCT && !result.type.nominal &&
            (capability->component_access == SCRIPT_COMPONENT_ACCESS_READ ||
             capability->component_access == SCRIPT_COMPONENT_ACCESS_WRITE)) {
            const script_catalog_type *component = component_argument(a, capability, &result);
            if (component) {
                unsigned flags = result.type.flags;
                if (capability->component_field) {
                    for (int i = 0; i < component->field_count; i++) {
                        if (component->fields[i].id == capability->component_field) {
                            result.type = component->fields[i].type;
                            break;
                        }
                    }
                } else {
                    result.type = component->type;
                    result.type.nominal = component->id;
                }
                result.type.flags |= flags & SCRIPT_TYPE_REFERENCE;
            }
        }
        if (capability && result.type.parameter) {
            uint64_t parameter = result.type.parameter;
            for (int i = 0; i < acount(result.arguments); i++) {
                script_argument argument = result.arguments[i];
                if (argument.parameter < 0 ||
                    argument.parameter >= capability->parameter_count ||
                    capability->parameters[argument.parameter].type.parameter !=
                        parameter) {
                    continue;
                }
                script_type actual = script_analyzer_expr_type(a, argument.value);
                actual.container = result.type.container;
                actual.flags &= ~(SCRIPT_TYPE_REFERENCE | SCRIPT_TYPE_GENERIC | SCRIPT_TYPE_UNTYPED);
                actual.flags |= result.type.flags &
                                (SCRIPT_TYPE_REFERENCE | SCRIPT_TYPE_OPTIONAL);
                actual.parameter = 0;
                if (!(result.type.flags & SCRIPT_TYPE_UNTYPED)) {
                    script_analyzer_require(a, node, result.type, actual);
                }
                result.type = actual;
            }
            if (result.type.flags & SCRIPT_TYPE_UNTYPED) {
                if (!(expected.flags & SCRIPT_TYPE_UNTYPED)) {
                    result.type = expected;
                } else {
                    script_analyzer_diagnostic(a, node, "Native generic result has no concrete type");
                }
            }
        }
        if (capability && capability->numeric_aggregate) {
            script_type specialized = expected;
            bool first = true;
            for (int i = 0; i < acount(result.arguments); i++) {
                script_argument argument = result.arguments[i];
                script_type actual = script_analyzer_expr_type(a, argument.value);
                actual.flags &= ~SCRIPT_TYPE_REFERENCE;
                if (!script_analyzer_numeric_aggregate(a->catalog, actual, 0)) {
                    script_analyzer_diagnostic(a, node, "Numeric capability requires numeric operands");
                    continue;
                }
                specialized = first || actual.storage == PIN_STRUCT ? actual
                            : specialized.storage == PIN_STRUCT
                                ? specialized
                                : script_analyzer_arithmetic_type(specialized, actual);
                first = false;
            }
            result.type = specialized;
        }
    } else if (count > 1) {
        result.type = (script_type){ .storage = PIN_STRUCT,
                                     .nominal = result.capability,
                                     .flags = SCRIPT_TYPE_RESULTS };
    } else if (!result.task && (capability || function)) {
        result.type = (script_type){ .storage = PIN_FLOW };
    }
    for (int i = 0; i < count; i++) {
        script_type type = capability ? capability->results[i].type
                                      : a->out->symbols[function->results[i]].type;
        if (count == 1) {
            type = result.type;
        }
        if (capability && type.parameter && (type.flags & SCRIPT_TYPE_UNTYPED)) {
            for (int j = 0; j < acount(result.arguments); j++) {
                script_argument argument = result.arguments[j];
                if (argument.parameter < 0 ||
                    argument.parameter >= capability->parameter_count ||
                    capability->parameters[argument.parameter].type.parameter !=
                        type.parameter) {
                    continue;
                }
                script_type actual = script_analyzer_expr_type(a, argument.value);
                actual.container = type.container;
                actual.flags &= ~(SCRIPT_TYPE_REFERENCE | SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_GENERIC);
                actual.flags |= type.flags &
                                (SCRIPT_TYPE_REFERENCE | SCRIPT_TYPE_OPTIONAL);
                type = actual;
                break;
            }
            if (type.flags & SCRIPT_TYPE_UNTYPED) {
                script_analyzer_diagnostic(a, node, "Native generic result has no concrete type");
            }
        }
        apush(result.results, type);
    }
    result.result_count = acount(result.results);
    if (receiver >= 0 && capability && script_analyzer_array_changer(capability)) {
        /* The change is the call's whole effect; its value means nothing. */
        if (!ts_node_eq(a->changing, node)) {
            char *method = script_analyzer_name_copy(a, script_analyzer_field(target, "member"));
            script_analyzer_diagnostic(a, target, "%s changes the array; call it as a statement on a name", method);
            free(method);
        }
        if (!a->out->expressions[receiver].writable) {
            script_analyzer_diagnostic(a, target, "Array mutation requires a writable receiver");
        }
        static const char *const methods[] = {
            "append",  "add_unique",   "extend",      "set_array_elem",
            "insert",  "remove_index", "remove_item", "clear",
            "reverse", "swap",         "resize"
        };
        for (size_t i = 0; i < sizeof methods / sizeof *methods; i++) {
            if (script_name_matches(capability->name, methods[i])) {
                result.member = i + 1;
                break;
            }
        }
        if (script_analyzer_expr_type(a, receiver).container == PIN_CONTAINER_VIEW &&
            result.member != 4 && result.member != 9 && result.member != 10) {
            script_analyzer_diagnostic(a, target, "A fixed-position view cannot change its length");
        }
        result.kind = SCRIPT_EXPR_ARRAY_MUTATE;
        result.type = script_analyzer_boolean_type();
        afree(result.results);
        result.results = NULL;
        apush(result.results, result.type);
        result.result_count = 1;
        /* Changing a local array changes only the caller's value. */
        if (script_analyzer_expr_type(a, receiver).container == PIN_CONTAINER_VIEW ||
            script_analyzer_writes_external(a->out, receiver)) {
            result.effects |= SCRIPT_EFFECT_WRITE;
        }
    }
    result.writable = (result.type.flags & SCRIPT_TYPE_REFERENCE) != 0;
    if (symbol >= 0) {
        script_analyzer_use(a, target, symbol, SCRIPT_CALL, result.capability);
    }
    if (capability && capability->pure &&
        capability->effects & (SCRIPT_EFFECT_WRITE | SCRIPT_EFFECT_STRUCTURAL |
                               SCRIPT_EFFECT_HOST | SCRIPT_EFFECT_SUSPEND)) {
        script_analyzer_diagnostic(a, target, "Catalog marks an effectful capability pure");
    }
    (void)expected;
    free(name);
    afree(selected_parameters);
    /* Ordered header bindings remain immutable across proved-pure calls. */
    if (!a->query) {
        script_analyzer_invalidate_pointers(a);
    }
    return script_analyzer_expr_add(a, result);
}
