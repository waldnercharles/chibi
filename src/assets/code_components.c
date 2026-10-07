#include "assets/code_components.h"

#include "assets/code_program_internal.h"
#include "assets/code_source_intelligence.h"
#include "assets/code_source_internal.h"
#include "graph/model/local.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/component_nodes.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *code_component_spelling(const ecs_component_desc *component)
{
    const char *name = component->name;
    if (!strncmp(name, "GAME.", 5)) {
        name += 5;
    }
    return !strncmp(name, "Game", 4) && isupper((unsigned char)name[4]) ? name + 4
                                                                        : name;
}

void code_component_field_spelling(const type_field *field, char *out, size_t size)
{
    size_t at = 0;
    bool capital = true;
    for (const char *p = field->name; *p && at + 1 < size; p++) {
        if (*p == '_') {
            capital = true;
            continue;
        }
        out[at++] = capital ? (char)toupper((unsigned char)*p) : *p;
        capital = false;
    }
    if (size) {
        out[at] = 0;
    }
}

static bool same_name(const char *a, const char *b)
{
    while (*a || *b) {
        while (*a == '_') {
            a++;
        }
        while (*b == '_') {
            b++;
        }
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        if (*a) {
            a++;
        }
        if (*b) {
            b++;
        }
    }
    return true;
}

const ecs_component_desc *code_component_named(const char *name, bool *ambiguous)
{
    const ecs_component_desc *found = NULL;
    *ambiguous = false;
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *desc = registry_node(i);
        const ecs_component_desc *component = desc->component_kind == NODE_COMPONENT_GET_SELF
                                                ? desc->component
                                                : NULL;
        if (!component || component == found) {
            continue;
        }
        if (!strcmp(component->name, name) ||
            (component->key && !strcmp(component->key, name))) {
            return component;
        }
        if (!same_name(code_component_spelling(component), name)) {
            continue;
        }
        if (found && found != component) {
            *ambiguous = true;
        }
        found = component;
    }
    return *ambiguous ? NULL : found;
}

const type_field *code_component_field(const type_desc *type, const char *name, bool *ambiguous)
{
    const type_field *found = NULL;
    *ambiguous = false;
    for (int i = 0; type && i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (!strcmp(field->name, name)) {
            return field;
        }
        if (!same_name(field->name, name)) {
            continue;
        }
        if (found) {
            *ambiguous = true;
        }
        found = field;
    }
    return *ambiguous ? NULL : found;
}

static char *token_name(const char *text, code_token token)
{
    size_t length = token.end - token.start;
    char *name = xmalloc(length + 1);
    memcpy(name, text + token.start, length);
    name[length] = 0;
    return name;
}

static bool entity_port(const code_call_port *port)
{
    return port->type == PIN_HANDLE && port->container == PIN_CONTAINER_SINGLE &&
           port->type_name && !strcmp(port->type_name, "ecs.entity");
}

static bool component_get(
    parser *p,
    scope *s,
    const ecs_component_desc *component,
    code_call_argument entity,
    code_call_argument *out
)
{
    entity.name = "Entity";
    code_call_spec spec = { .name = "Get Component",
                            .native = true,
                            .reference = component->key,
                            .arguments = &entity,
                            .argument_count = 1 };
    char error[192];
    node *n = code_call_build(p->asset, s->native, &spec, error, sizeof error);
    int pin = n ? code_call_port_index(n, true, "Value", error, sizeof error) : -1;
    if (!n || pin < 0) {
        code_source_error(p->source, p->base, p->base + 1, error);
        return false;
    }
    *out = (code_call_argument){ .kind = CODE_CALL_OUTPUT, .source = { n, pin } };
    return true;
}

int code_components_value(
    parser *p,
    scope *s,
    const char *text,
    int offset,
    code_call_port *expected,
    code_call_argument *out
)
{
    code_program_complete_ecs_value(p, s, text, offset, expected);
    if (!code_program_atomic_value(text)) {
        return -1;
    }
    code_lexer lexer;
    code_lex_init(&lexer, text);
    code_token root = code_lex_next(&lexer), dot = code_lex_next(&lexer);
    bool binding_root = root.punctuation == '&' && dot.kind == CODE_TOKEN_NAME &&
                        (code_token_is(&lexer, dot, "local") ||
                         code_token_is(&lexer, dot, "state"));
    if (binding_root) {
        if (code_lex_next(&lexer).punctuation != '.') {
            return -1;
        }
        root = code_lex_next(&lexer);
        dot = code_lex_next(&lexer);
    }
    code_lexer look = lexer;
    code_token separator = dot;
    while (separator.punctuation == '.') {
        if (code_lex_next(&look).kind != CODE_TOKEN_NAME) {
            return -1;
        }
        separator = code_lex_next(&look);
    }
    if (separator.kind != CODE_TOKEN_END) {
        return -1;
    }
    if (root.kind != CODE_TOKEN_NAME &&
        (!binding_root || root.kind != CODE_TOKEN_STRING)) {
        return -1;
    }
    char *name = binding_root ? xmalloc(root.end + 1) : token_name(text, root);
    if (binding_root) {
        memcpy(name, text, root.end);
        name[root.end] = 0;
    }
    code_component_binding *binding = NULL;
    for (int i = 0; i < acount(s->components); i++) {
        if (!strcmp(s->components[i].name, name)) {
            binding = &s->components[i];
            break;
        }
    }
    if (dot.punctuation != '.' && (!binding || dot.kind != CODE_TOKEN_END)) {
        free(name);
        return -1;
    }
    code_call_argument value = { 0 };
    code_call_port port = { .kind = PIN_DATA, .type = PIN_HANDLE, .type_name = "ecs.entity" };
    bool known = !strcmp(name, "self") || binding || binding_root;
    code_token first_member = dot.kind == CODE_TOKEN_END ? dot : code_lex_next(&lexer);
    char *member = first_member.kind == CODE_TOKEN_NAME
                     ? token_name(text, first_member)
                     : NULL;
    bool ambiguous = false, projected = false;
    if (!strcmp(name, "self") && member && expected && expected->type == PIN_NAME &&
        expected->container == PIN_CONTAINER_SINGLE) {
        code_lexer tail = lexer;
        const ecs_component_desc *component = code_component_named(member, &ambiguous);
        code_token dot = code_lex_next(&tail), token = code_lex_next(&tail);
        if (component && dot.punctuation == '.' && token.kind == CODE_TOKEN_NAME &&
            code_lex_next(&tail).kind == CODE_TOKEN_END) {
            char *field_name = token_name(text, token);
            const type_field *field = code_component_field(component->type, field_name, &ambiguous);
            free(field_name);
            if (field && (field->type == PIN_STR || field->type == PIN_NAME)) {
                /* A reflected text reference reads directly into the callee's
                 * name storage, just like an authored component-field binding. */
                *out = (code_call_argument){ .kind = CODE_CALL_BINDING,
                                             .binding = {
                                                 .kind = PIN_BINDING_COMPONENT_FIELD,
                                                 .component_key = graph_own_str(
                                                     s->native,
                                                     component->key,
                                                     strlen(component->key)
                                                 ),
                                                 .field_name = graph_own_str(
                                                     s->native,
                                                     field->name,
                                                     strlen(field->name)
                                                 ) } };
                code_program_resolved_use(p, s, text, offset, out, CODE_USE_READ);
                free(name);
                free(member);
                return 1;
            }
        }
    }
    if (!strcmp(name, "self") && member && expected && expected->type == PIN_STRUCT &&
        expected->container == PIN_CONTAINER_SINGLE) {
        code_lexer tail = lexer;
        const ecs_component_desc *component = code_component_named(member, &ambiguous);
        pin_spec actual = { .type = PIN_STRUCT,
                            .type_name = component ? component->type->name : NULL };
        pin_spec wanted = { .type = expected->type,
                            .type_name = expected->type_name,
                            .container = expected->container };
        if (component && code_lex_next(&tail).kind == CODE_TOKEN_END &&
            pin_compatible(&actual, &wanted)) {
            *out = (code_call_argument){ .kind = CODE_CALL_BINDING,
                                         .binding = { .kind = PIN_BINDING_COMPONENT,
                                                      .component_key = graph_own_str(
                                                          s->native,
                                                          component->key,
                                                          strlen(component->key)
                                                      ) } };
            code_program_resolved_use(p, s, text, offset, out, CODE_USE_READ);
            free(name);
            free(member);
            return 1;
        }
    }
    declaration *declaration = !known ? code_program_declaration(s, name) : NULL;
    node *producer = declaration ? code_program_build(p, s, declaration) : NULL;
    if (declaration && declaration->expression_output.node) {
        value = (code_call_argument){ .kind = CODE_CALL_OUTPUT,
                                      .source = declaration->expression_output };
        projected = code_statement_value_type(p, s, &value, &port);
        bool result_projection = member && !strcmp(member, "Result") &&
                                 port.type != PIN_STRUCT;
        bool field_ambiguous = false;
        bool reflected_member = member && port.type == PIN_STRUCT && port.type_name &&
                                code_component_field(
                                    registry_type_by_name(port.type_name),
                                    member,
                                    &field_ambiguous
                                );
        if (projected && member &&
            ((!reflected_member &&
              !strcmp(member, producer->output_pins[value.source.pin].name)) ||
             result_projection)) {
            code_token separator = code_lex_next(&lexer);
            first_member = separator.kind == CODE_TOKEN_END ? separator
                                                            : code_lex_next(&lexer);
        }
    }
    for (int i = 0; !projected && producer && member && i < producer->output_count;
         i++) {
        const pin_spec *output = &producer->output_pins[i];
        bool tick_delta = producer->desc == &NODE_Event && producer->reference_name &&
                          !strcmp(producer->reference_name, "Tick") &&
                          !strcmp(member, "dt") &&
                          !strcmp(output->name, "DeltaSeconds");
        if (output->kind != PIN_DATA || output->field_source ||
            (!tick_delta && strcmp(output->name, member))) {
            continue;
        }
        value = (code_call_argument){ .kind = CODE_CALL_OUTPUT,
                                      .source = { producer, i } };
        if (!code_statement_value_type(p, s, &value, &port)) {
            free(name);
            free(member);
            return 0;
        }
        projected = true;
        code_token separator = code_lex_next(&lexer);
        first_member = separator.kind == CODE_TOKEN_END ? separator
                                                        : code_lex_next(&lexer);
        break;
    }
    if (!projected && producer && member) {
        int primary = -1;
        for (int i = 0; i < producer->output_count; i++) {
            const pin_spec *output = &producer->output_pins[i];
            if (output->kind != PIN_DATA || output->field_source) {
                continue;
            }
            bool selected = !acount(declaration->results);
            for (int j = 0; j < acount(declaration->results); j++) {
                selected |= !strcmp(declaration->results[j].name, output->name);
            }
            if (!selected) {
                continue;
            }
            if (primary >= 0) {
                primary = -1;
                break;
            }
            primary = i;
        }
        if (primary >= 0) {
            code_call_argument aggregate = { .kind = CODE_CALL_OUTPUT,
                                             .source = { producer, primary } };
            code_call_port aggregate_port = { 0 };
            if (code_statement_value_type(p, s, &aggregate, &aggregate_port) &&
                (aggregate_port.type == PIN_STRUCT || entity_port(&aggregate_port))) {
                value = aggregate;
                port = aggregate_port;
                projected = true;
            }
        }
    }
    if (!known && !projected &&
        (!member || !code_component_named(member, &ambiguous))) {
        free(name);
        free(member);
        return -1;
    }
    if (binding && binding->optional && !binding->narrowed) {
        code_source_error(p->source, offset, offset + (int)root.end, "Optional component must be guarded before member access");
        free(name);
        free(member);
        return 0;
    }
    bool implicit_owner = (binding && !binding->optional) || !strcmp(name, "self");
    if (implicit_owner) {
        value = (code_call_argument){
            .kind = CODE_CALL_LITERAL,
            .literal = { .type = PIN_HANDLE, .type_name = "ecs.entity" }
        };
    }
    bool valid = projected || implicit_owner ||
                 (code_program_value(p, s, binding ? "self" : name, offset, &port, &value) &&
                  code_statement_value_type(p, s, &value, &port));
    if (implicit_owner && p->asset->owner_type_name &&
        strcmp(p->asset->owner_type_name, "ecs.entity")) {
        valid = false;
    }
    free(name);
    if (!valid || (!projected && !entity_port(&port))) {
        if (known) {
            code_source_error(p->source, offset, offset + (int)root.end, "Component access requires an entity owner");
        }
        free(member);
        return known ? 0 : -1;
    }
    if (binding) {
        const ecs_component_desc *component = component_by_key(binding->key);
        valid = component && component_get(p, s, component, value, &value) &&
                code_statement_value_type(p, s, &value, &port);
    }
    for (code_token token = first_member; valid && token.kind != CODE_TOKEN_END;
         token = code_lex_next(&lexer)) {
        if (token.kind != CODE_TOKEN_NAME) {
            code_source_error(
                p->source,
                offset + (int)token.start,
                offset + (int)token.end,
                "Expected a component or field name"
            );
            valid = false;
            break;
        }
        free(member);
        member = token_name(text, token);
        char error[192] = "Cannot resolve component member";
        if (entity_port(&port)) {
            const ecs_component_desc *component = code_component_named(member, &ambiguous);
            if (!component) {
                snprintf(error, sizeof error, "%s component '%s'", ambiguous ? "Ambiguous" : "Unknown", member);
                valid = false;
            } else {
                valid = component_get(p, s, component, value, &value);
            }
        } else if (port.type == PIN_STRUCT && port.container == PIN_CONTAINER_SINGLE) {
            const type_desc *type = port.type_name
                                      ? registry_type_by_name(port.type_name)
                                      : NULL;
            const type_field *field = code_component_field(type, member, &ambiguous);
            int pin = field && value.kind == CODE_CALL_OUTPUT
                        ? graph_expose_output_field(
                              s->native,
                              value.source.node,
                              value.source.pin,
                              field->name
                          )
                        : -1;
            if (pin < 0) {
                snprintf(
                    error,
                    sizeof error,
                    "%s field '%s' on '%s'",
                    ambiguous ? "Ambiguous" : "Unknown",
                    member,
                    port.type_name ? port.type_name : "value"
                );
                valid = false;
            } else {
                value.source.pin = pin;
            }
        } else {
            snprintf(error, sizeof error, "Value has no member '%s'", member);
            valid = false;
        }
        if (!valid) {
            code_source_error(
                p->source,
                offset + (int)token.start,
                offset + (int)token.end,
                error
            );
            break;
        }
        valid = code_statement_value_type(p, s, &value, &port);
        code_token next = code_lex_next(&lexer);
        if (next.kind == CODE_TOKEN_END) {
            break;
        }
        if (next.punctuation != '.') {
            free(member);
            return -1;
        }
    }
    free(member);
    if (valid) {
        *out = value;
        code_program_resolved_use(p, s, text, offset, out, CODE_USE_READ);
    }
    return valid ? 1 : 0;
}

static code_call_argument getter_entity(graph *g, node *getter)
{
    int input = code_call_port_index(getter, false, "Entity", NULL, 0);
    node *source = NULL;
    int pin = -1;
    if (input >= 0 && graph_input_source(g, getter, input, &source, &pin)) {
        return (code_call_argument){ .kind = CODE_CALL_OUTPUT,
                                     .source = { source, pin } };
    }
    if (input >= 0 && getter->input_pins[input].binding) {
        return (code_call_argument){ .kind = CODE_CALL_BINDING,
                                     .binding = *getter->input_pins[input].binding };
    }
    return (code_call_argument){
        .kind = CODE_CALL_LITERAL,
        .literal = input >= 0 ? getter->input_pins[input].default_value
                              : (pin_value){ .type = PIN_HANDLE, .type_name = "ecs.entity" }
    };
}

int code_components_assignment(parser *p, scope *s, const char *text, int offset, code_call_argument *out)
{
    code_lexer lexer;
    code_lex_init(&lexer, text);
    code_token token = code_lex_next(&lexer);
    bool binding_root = token.punctuation == '&';
    if (binding_root) {
        code_token kind = code_lex_next(&lexer);
        if (kind.kind != CODE_TOKEN_NAME ||
            (!code_token_is(&lexer, kind, "local") &&
             !code_token_is(&lexer, kind, "state")) ||
            code_lex_next(&lexer).punctuation != '.') {
            return -1;
        }
        token = code_lex_next(&lexer);
    }
    if (token.kind != CODE_TOKEN_NAME &&
        (!binding_root || token.kind != CODE_TOKEN_STRING)) {
        return -1;
    }
    bool dotted = false;
    code_token next = code_lex_next(&lexer);
    while (next.punctuation == '.') {
        dotted = true;
        token = code_lex_next(&lexer);
        if (token.kind != CODE_TOKEN_NAME) {
            return -1;
        }
        next = code_lex_next(&lexer);
    }
    if (!dotted) {
        char *name = token_name(text, token);
        bool bound = false;
        for (int i = 0; i < acount(s->components); i++) {
            bound |= !strcmp(s->components[i].name, name);
        }
        free(name);
        if (!bound) {
            return -1;
        }
    }
    char operation = 0;
    if (next.punctuation != '=') {
        if (next.kind != CODE_TOKEN_PUNCTUATION || !strchr("+-*/%", next.punctuation)) {
            return -1;
        }
        operation = next.punctuation;
        next = code_lex_next(&lexer);
        if (next.punctuation != '=') {
            return -1;
        }
    }
    code_token rhs_token = code_lex_next(&lexer);
    if (rhs_token.punctuation == '=') {
        return -1;
    }
    if (rhs_token.kind == CODE_TOKEN_END) {
        code_source_error(
            p->source,
            offset + (int)next.start,
            offset + (int)next.end,
            "Component assignment requires a value"
        );
        return 0;
    }
    char *left = xmalloc(token.end + 1);
    memcpy(left, text, token.end);
    left[token.end] = 0;
    code_call_argument target = { 0 };
    code_call_port expected = { .kind = PIN_DATA, .type = PIN_FLOW };
    int existing_nodes = acount(s->native->nodes);
    int resolved = code_components_value(p, s, left, offset, &expected, &target);
    free(left);
    if (resolved <= 0) {
        return resolved;
    }
    node *getter = target.kind == CODE_CALL_OUTPUT ? target.source.node : NULL;
    const ecs_component_desc *component = getter ? component_node_component(getter)
                                                 : NULL;
    const type_field **fields = NULL;
    int *parents = NULL;
    for (int pin = getter ? target.source.pin : -1; getter && pin >= 0;) {
        int parent = -1;
        const type_field *field = graph_output_field(getter, pin, &parent);
        if (!field) {
            break;
        }
        apush(fields, field);
        apush(parents, parent);
        pin = parent;
    }
    const type_field *field = acount(fields) ? fields[acount(fields) - 1] : NULL;
    if (!component || (field && !component_field_settable(component, field->name))) {
        code_source_error(p->source, offset, offset + (int)token.end, "Component assignment requires a writable reflected field");
        goto failed;
    }
    if (!code_statement_value_type(p, s, &target, &expected)) {
        goto failed;
    }
    code_call_argument value = { 0 };
    code_call_port operand = expected;
    if (operation && strchr("*/%", operation) && expected.type == PIN_STRUCT) {
        operand.type = PIN_F64;
        operand.type_name = "double";
    }
    const char *rhs = text + rhs_token.start;
    bool valid = rhs_token.punctuation == '{' || rhs_token.punctuation == '[' ||
                         code_program_atomic_value(rhs)
                   ? code_program_value(p, s, rhs, offset + (int)rhs_token.start, &operand, &value)
                   : code_statement_expression(
                         p,
                         s,
                         rhs,
                         offset + (int)rhs_token.start,
                         &operand,
                         &value
                     );
    if (!valid) {
        goto failed;
    }
    char error[192];
    if (operation) {
        const char *name = operation == '+' ? "Add"
                         : operation == '-' ? "Subtract"
                         : operation == '*' ? "Multiply"
                         : operation == '/' ? "Divide"
                                            : "Modulo";
        code_call_argument operands[] = { target, value };
        operands[0].name = "A";
        operands[1].name = "B";
        code_call_port right = { 0 };
        if (!code_statement_value_type(p, s, &value, &right)) {
            code_source_error(
                p->source,
                offset + (int)rhs_token.start,
                offset + (int)strlen(text),
                "Cannot determine compound assignment operand type"
            );
            goto failed;
        }
        code_call_port ports[] = { expected, right };
        ports[0].name = "A";
        ports[1].name = "B";
        code_call_spec arithmetic = { .name = name,
                                      .native = true,
                                      .arguments = operands,
                                      .argument_count = 2,
                                      .inputs = ports,
                                      .input_count = 2 };
        node *n = code_call_build(p->asset, s->native, &arithmetic, error, sizeof error);
        int result = n ? code_call_port_index(n, true, "Result", error, sizeof error)
                       : -1;
        if (result < 0) {
            code_source_error(p->source, offset, offset + (int)strlen(text), error);
            goto failed;
        }
        value = (code_call_argument){ .kind = CODE_CALL_OUTPUT,
                                      .source = { n, result } };
    }
    for (int i = 0; i + 1 < acount(fields); i++) {
        const type_desc *type = graph_output_struct(getter, parents[i]);
        if (!type) {
            code_source_error(p->source, offset, offset + (int)token.end, "Nested component assignment requires a reflected struct");
            goto failed;
        }
        code_call_argument *members = NULL;
        for (int j = 0; j < type->field_count; j++) {
            const type_field *member = &type->fields[j];
            code_call_argument argument = value;
            if (strcmp(member->name, fields[i]->name)) {
                int output = graph_expose_output_field(
                    s->native,
                    getter,
                    parents[i],
                    member->name
                );
                if (output < 0) {
                    afree(members);
                    code_source_error(p->source, offset, offset + (int)token.end, "Cannot read a preserved struct field");
                    goto failed;
                }
                argument = (code_call_argument){ .kind = CODE_CALL_OUTPUT,
                                                 .source = { getter, output } };
            }
            argument.name = member->name;
            apush(members, argument);
        }
        code_call_spec copy = { .name = "Make Struct",
                                .native = true,
                                .reference = type->name,
                                .arguments = members,
                                .argument_count = acount(members) };
        node *aggregate = code_call_build(p->asset, s->native, &copy, error, sizeof error);
        afree(members);
        int output = -1;
        for (int j = 0; aggregate && j < aggregate->output_count; j++) {
            if (aggregate->output_pins[j].kind != PIN_DATA ||
                aggregate->output_pins[j].field_source) {
                continue;
            }
            if (output >= 0) {
                output = -1;
                break;
            }
            output = j;
        }
        if (aggregate && output < 0) {
            snprintf(error, sizeof error, "Struct constructor requires one aggregate result");
        }
        if (output < 0) {
            code_source_error(p->source, offset, offset + (int)token.end, error);
            goto failed;
        }
        value = (code_call_argument){ .kind = CODE_CALL_OUTPUT,
                                      .source = { aggregate, output } };
    }
    char field_name[192];
    if (field) {
        snprintf(field_name, sizeof field_name, "Value.%s", field->name);
    } else {
        snprintf(field_name, sizeof field_name, "Value");
    }
    code_call_argument arguments[] = { getter_entity(s->native, getter), value };
    arguments[0].name = "Entity";
    arguments[1].name = field_name;
    code_call_spec setter = { .name = "Set Component",
                              .native = true,
                              .reference = component->key,
                              .arguments = arguments,
                              .argument_count = 2 };
    node *n = code_call_build(p->asset, s->native, &setter, error, sizeof error);
    int result = n ? code_call_port_index(n, true, "Value", error, sizeof error) : -1;
    if (result < 0) {
        code_source_error(p->source, offset, offset + (int)strlen(text), error);
        goto failed;
    }
    if (field) {
        result = graph_expose_output_field(s->native, n, result, field->name);
    }
    if (result < 0) {
        code_source_error(p->source, offset, offset + (int)token.end, "Component setter cannot expose its written field");
        goto failed;
    }
    bool fresh = false, used = false;
    for (int i = existing_nodes; i < acount(s->native->nodes); i++) {
        fresh |= s->native->nodes[i] == getter;
    }
    for (int i = 0; i < acount(s->native->edges); i++) {
        used |= s->native->edges[i].from_id == getter->id;
    }
    for (int i = 0; i < acount(s->declarations); i++) {
        const declaration *d = s->declarations[i];
        used |= d->native == getter || d->expression_output.node == getter;
    }
    for (int i = 0; i < acount(s->native->locals); i++) {
        used |= s->native->locals[i].source_id == getter->id;
    }
    if (fresh && !used) {
        graph_remove_node(s->native, getter);
    }
    *out = (code_call_argument){ .kind = CODE_CALL_OUTPUT, .source = { n, result } };
    afree(fields);
    afree(parents);
    return 1;
failed:
    afree(fields);
    afree(parents);
    return 0;
}

int code_components_condition(
    parser *p,
    scope *s,
    const char *text,
    int offset,
    code_call_argument *out,
    code_component_binding **narrowed,
    bool *present_on_true
)
{
    code_lexer lexer;
    code_lex_init(&lexer, text);
    code_token name = code_lex_next(&lexer);
    bool positive = true;
    if (name.punctuation == '!') {
        positive = false;
        name = code_lex_next(&lexer);
    }
    if (name.kind != CODE_TOKEN_NAME) {
        return -1;
    }
    char *spelling = token_name(text, name);
    code_component_binding *binding = NULL;
    for (int i = 0; i < acount(s->components); i++) {
        if (s->components[i].optional && !strcmp(s->components[i].name, spelling)) {
            binding = &s->components[i];
            break;
        }
    }
    free(spelling);
    if (!binding) {
        return -1;
    }
    code_token next = code_lex_next(&lexer);
    if (next.kind != CODE_TOKEN_END) {
        if (!positive || (next.punctuation != '!' && next.punctuation != '=')) {
            return -1;
        }
        positive = next.punctuation == '!';
        if (code_lex_next(&lexer).punctuation != '=') {
            return -1;
        }
        code_token nil = code_lex_next(&lexer);
        if (nil.kind != CODE_TOKEN_NAME || nil.end - nil.start != 3 ||
            strncmp(text + nil.start, "nil", 3) ||
            code_lex_next(&lexer).kind != CODE_TOKEN_END) {
            return -1;
        }
    }
    code_call_port entity = { .kind = PIN_DATA, .type = PIN_HANDLE, .type_name = "ecs.entity" };
    code_call_argument argument = { 0 };
    if (!code_program_value(p, s, "self", offset, &entity, &argument)) {
        return 0;
    }
    argument.name = "Entity";
    code_call_spec has = { .name = "Has Component",
                           .native = true,
                           .reference = binding->key,
                           .arguments = &argument,
                           .argument_count = 1 };
    char error[192];
    node *n = code_call_build(p->asset, s->native, &has, error, sizeof error);
    int pin = n ? code_call_port_index(n, true, "Value", error, sizeof error) : -1;
    if (pin < 0) {
        code_source_error(p->source, offset, offset + (int)strlen(text), error);
        return 0;
    }
    code_call_argument value = { .kind = CODE_CALL_OUTPUT, .source = { n, pin } };
    if (!positive) {
        value.name = "Value";
        code_call_spec negate = { .name = "Not",
                                  .native = true,
                                  .arguments = &value,
                                  .argument_count = 1 };
        n = code_call_build(p->asset, s->native, &negate, error, sizeof error);
        pin = n ? code_call_port_index(n, true, "Result", error, sizeof error) : -1;
        if (pin < 0) {
            code_source_error(p->source, offset, offset + (int)strlen(text), error);
            return 0;
        }
        value = (code_call_argument){ .kind = CODE_CALL_OUTPUT, .source = { n, pin } };
    }
    *narrowed = binding;
    *present_on_true = positive;
    *out = value;
    return 1;
}
