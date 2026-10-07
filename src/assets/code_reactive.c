#include "assets/code_reactive.h"

#include "assets/code_source_internal.h"
#include "assets/code_value.h"
#include "graph/document/events.h"
#include "graph/document/serialize.h"
#include "graph/model/reactive.h"
#include "graph/model/registry.h"
#include "graph/nodes/component_nodes.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *keep(parser *p, const char *text, int length)
{
    char *copy = xmalloc((size_t)length + 1);
    memcpy(copy, text, (size_t)length);
    copy[length] = 0;
    apush(p->program->strings, copy);
    return copy;
}

static char *capture(parser *p, const char *stop, int *start)
{
    int first = (int)p->token.start, end = first, depth = 0, line = p->token.line;
    bool previous_operator = false;
    *start = first + p->base;
    while (p->token.kind != CODE_TOKEN_END) {
        if (!depth && (code_token_is(&p->lexer, p->token, stop) ||
                       code_token_is(&p->lexer, p->token, "}"))) {
            break;
        }
        bool operator = p->token.kind == CODE_TOKEN_PUNCTUATION &&
                        strchr("+-*/%&|^!<>=.,", p->token.punctuation);
        if (!strcmp(stop, ";") && !depth && end != first &&
            p->token.line > line && !previous_operator && !operator) {
            break;
        }
        depth += code_token_is(&p->lexer, p->token, "(") ||
                 code_token_is(&p->lexer, p->token, "[");
        depth -= code_token_is(&p->lexer, p->token, ")") ||
                 code_token_is(&p->lexer, p->token, "]");
        end = (int)p->token.end;
        line = p->token.line;
        previous_operator = operator;
        code_program_next(p);
    }
    return keep(p, p->lexer.text + first, end - first);
}

static const char *fallback_suffix(const char *expression)
{
    code_lexer lexer;
    code_lex_init(&lexer, expression);
    code_token token = code_lex_next(&lexer);
    if (!code_token_is(&lexer, token, "fallback")) {
        return NULL;
    }
    int depth = 0;
    while ((token = code_lex_next(&lexer)).kind != CODE_TOKEN_END) {
        if (code_token_is(&lexer, token, "(") || code_token_is(&lexer, token, "[")) {
            depth++;
        } else if (code_token_is(&lexer, token, ")") || code_token_is(&lexer, token, "]")) {
            depth--;
        } else if (depth == 1 && code_token_is(&lexer, token, ",")) {
            return expression + token.start;
        }
    }
    return NULL;
}

static declaration *event(parser *p, behavior_reactive_kind kind, int ordinal, int start)
{
    char name[64];
    snprintf(name, sizeof name, kind == BEHAVIOR_REACTIVE_BIND ? "__Bind_%d" : "__PressedChanged_%d", ordinal);
    declaration *d = xcalloc(1, sizeof *d);
    d->name = keep(p, name, (int)strlen(name));
    d->call = (code_call_spec){ .name = "Event", .reference = d->name };
    d->start = d->end = start;
    if (kind == BEHAVIOR_REACTIVE_PRESSED_CHANGED) {
        apush(
            d->results,
            ((code_call_port){ .name = "pressed",
                               .type = PIN_BOOL,
                               .type_name = "bool",
                               .kind = PIN_DATA })
        );
        d->call.results = d->results;
        d->call.result_count = 1;
    }
    apush(p->scope->declarations, d);
    return d;
}

static bool path(parser *p, const char *text, int start, pin_type type, code_call_argument *value)
{
    code_call_port expected = { .type = type,
                                .type_name = type == PIN_BOOL ? "bool" : NULL };
    if (!code_program_value(p, p->scope, text, start, &expected, value) ||
        value->kind != CODE_CALL_OUTPUT) {
        code_source_error(p->source, start, start + (int)strlen(text), "A reactive source must be an ECS component field path");
        return false;
    }
    const node_desc *desc = value->source.node->desc;
    if (!desc || (component_node_kind_of(desc) != COMPONENT_NODE_GET &&
                  desc->component_kind != NODE_COMPONENT_GET_SELF &&
                  desc->component_kind != NODE_COMPONENT_GET_ENTITY)) {
        code_source_error(p->source, start, start + (int)strlen(text), "A reactive source must resolve directly to a component field");
        return false;
    }
    return true;
}

static node *different(parser *p, code_call_argument a, code_call_argument b, code_call_port type, int depth)
{
    if (depth > 16 || type.container != PIN_CONTAINER_SINGLE) {
        code_source_error(p->source, 0, 0, "A binding target must have an inline comparable value");
        return NULL;
    }
    if (type.type == PIN_STRUCT) {
        const type_desc *aggregate = registry_type_by_name(type.type_name);
        if (!aggregate || !type_is_plain(aggregate) ||
            a.kind != CODE_CALL_OUTPUT || b.kind != CODE_CALL_OUTPUT) {
            code_source_error(p->source, 0, 0, "A binding aggregate must have reflected inline fields");
            return NULL;
        }
        node *result = NULL;
        for (int i = 0; i < aggregate->field_count; i++) {
            const type_field *field = &aggregate->fields[i];
            code_call_argument left = a, right = b;
            char names[2][256], message[256];
            snprintf(
                names[0],
                sizeof names[0],
                "%s.%s",
                a.source.node->output_pins[a.source.pin].name,
                field->name
            );
            snprintf(
                names[1],
                sizeof names[1],
                "%s.%s",
                b.source.node->output_pins[b.source.pin].name,
                field->name
            );
            if (!code_call_resolve_output(p->scope->native, a.source.node, names[0], &left.source, message, sizeof message) ||
                !code_call_resolve_output(
                    p->scope->native,
                    b.source.node,
                    names[1],
                    &right.source,
                    message,
                    sizeof message
                )) {
                code_source_error(p->source, 0, 0, message);
                return NULL;
            }
            node *next = different(
                p,
                left,
                right,
                (code_call_port){ .type = field->type, .type_name = field->type_name },
                depth + 1
            );
            if (!next) {
                return NULL;
            }
            if (!result) {
                result = next;
                continue;
            }
            node *either = graph_add_node(p->scope->native, &NODE_Or);
            graph_connect_data(p->scope->native, result, 0, either, 0);
            graph_connect_data(p->scope->native, next, 0, either, 1);
            result = either;
        }
        return result;
    }
    code_call_argument args[2] = { a, b };
    args[0].name = "A";
    args[1].name = "B";
    args[0].positional = args[1].positional = false;
    args[0].ordinal = args[1].ordinal = 0;
    code_call_port ports[2] = { type, type };
    ports[0].name = "A";
    ports[1].name = "B";
    code_call_spec call = { .name = "Not Equal",
                            .native = true,
                            .arguments = args,
                            .argument_count = 2,
                            .inputs = ports,
                            .input_count = 2 };
    char message[256];
    node *result = code_call_build(p->asset, p->scope->native, &call, message, sizeof message);
    if (!result) {
        code_source_error(p->source, 0, 0, message);
    }
    return result;
}

bool code_reactive_parse_bind(parser *p)
{
    int start;
    char *target = capture(p, "=", &start);
    code_program_complete_ecs_path(p, p->scope, target, start);
    if (!code_program_expect(p, "=")) {
        return true;
    }
    int expression_start;
    char *expression = capture(p, ";", &expression_start);
    code_program_take(p, ";");
    code_call_argument destination = { 0 };
    if (!path(p, target, start, PIN_FLOW, &destination)) {
        return true;
    }
    code_call_port expected = { 0 };
    if (!code_statement_value_type(p, p->scope, &destination, &expected)) {
        return true;
    }
    code_program_complete_ecs_value(p, p->scope, expression, expression_start, &expected);
    code_lexer lexer;
    code_lex_init(&lexer, expression);
    for (code_token token = code_lex_next(&lexer); token.kind != CODE_TOKEN_END;
         token = code_lex_next(&lexer)) {
        bool assignment = code_token_is(&lexer, token, "=") &&
                          expression[token.end] != '=' &&
                          (!token.start ||
                           !strchr("=!<>", expression[token.start - 1]));
        if (assignment || code_token_is(&lexer, token, "await") ||
            code_token_is(&lexer, token, ":=") || code_token_is(&lexer, token, "++") ||
            code_token_is(&lexer, token, "--")) {
            code_source_error(
                p->source,
                expression_start,
                expression_start + (int)strlen(expression),
                "bind requires a pure value expression"
            );
            return true;
        }
    }
    code_call_argument value = { 0 };
    graph_expression_mode previous_mode = p->expression_mode;
    p->expression_mode = GRAPH_EXPRESSION_VALUE;
    char literal_error[128];
    av_doc *literal = expected.type != PIN_STRUCT
                        ? code_value_read(expression, literal_error, sizeof literal_error)
                        : NULL;
    bool atomic = code_program_atomic_value(expression);
    code_call_port inferred = expected;
    if (atomic && expected.type == PIN_STRUCT) {
        /* A maintained value is copied; a whole component must be a value
         * producer rather than the argument binder's by-reference shortcut. */
        inferred.type = PIN_FLOW;
        inferred.type_name = NULL;
    }
    bool resolved = literal || atomic
                      ? code_program_value(p, p->scope, expression, expression_start, &inferred, &value)
                      : code_statement_expression(p, p->scope, expression, expression_start, &expected, &value);
    av_doc_free(literal);
    p->expression_mode = previous_mode;
    if (!resolved) {
        return true;
    }
    int ordinal = acount(p->asset->reactives);
    declaration *d = event(p, BEHAVIOR_REACTIVE_BIND, ordinal, start);
    const char *rhs = expression;
    if (value.kind == CODE_CALL_OUTPUT) {
        declaration *snapshot = xcalloc(1, sizeof *snapshot);
        char name[64];
        snprintf(name, sizeof name, "__BindValue_%d", ordinal);
        snapshot->name = keep(p, name, (int)strlen(name));
        snapshot->native = value.source.node;
        snapshot->lazy = true;
        const pin_spec *pin = &value.source.node->output_pins[value.source.pin];
        apush(
            snapshot->results,
            ((code_call_port){ .name = pin->name,
                               .type = pin->type,
                               .type_name = pin->type_name,
                               .container = pin->container,
                               .kind = PIN_DATA })
        );
        apush(p->scope->declarations, snapshot);
        rhs = snapshot->name;
        const char *suffix = (value.has_fallback_default || value.has_fallback_binding)
                               ? fallback_suffix(expression)
                               : NULL;
        if (suffix) {
            size_t length = strlen(rhs) + strlen(suffix) + 16;
            char *captured = xmalloc(length);
            snprintf(captured, length, "fallback(%s%s", rhs, suffix);
            apush(p->program->strings, captured);
            rhs = captured;
        } else if (value.has_fallback_default) {
            av_doc *document = av_doc_new();
            char *fallback = code_value_write(
                serialize_value(document, &value.fallback_default)
            );
            size_t length = strlen(rhs) + strlen(fallback) + 32;
            char *captured = xmalloc(length);
            snprintf(captured, length, "fallback(%s, default: %s)", rhs, fallback);
            apush(p->program->strings, captured);
            rhs = captured;
            free(fallback);
            av_doc_free(document);
        }
    }
    node *comparison = different(p, destination, value, expected, 0);
    if (!comparison) {
        return true;
    }
    declaration *guard = xcalloc(1, sizeof *guard);
    char guard_name[64];
    snprintf(guard_name, sizeof guard_name, "__BindChanged_%d", ordinal);
    guard->name = keep(p, guard_name, (int)strlen(guard_name));
    guard->native = comparison;
    guard->lazy = true;
    apush(guard->results, ((code_call_port){ .name = "Result", .type = PIN_BOOL, .kind = PIN_DATA }));
    apush(p->scope->declarations, guard);
    size_t size = strlen(target) + strlen(rhs) + 16;
    char *assignment = xmalloc(size);
    snprintf(assignment, size, "%s = %s", target, rhs);
    char *condition = guard->name;
    apush(p->program->strings, assignment);
    statement assign = { .kind = CODE_STMT_EXPRESSION,
                         .text = assignment,
                         .value_start = start,
                         .start = start,
                         .end = (int)p->token.start + p->base };
    statement branch = { .kind = CODE_STMT_IF,
                         .text = condition,
                         .value_start = start,
                         .start = start,
                         .end = assign.end };
    apush(branch.body, assign);
    continuation c = { .source = d->name, .start = start, .end = assign.end };
    apush(c.body, branch);
    apush(p->scope->continuations, c);
    apush(
        p->asset->reactives,
        ((behavior_reactive){
            .kind = BEHAVIOR_REACTIVE_BIND,
            .graph_id = p->scope->native->graph_id,
            .source_node_id = destination.source.node->id,
            .source_pin_id = destination.source.node
                                 ->output_pins[destination.source.pin]
                                 .id,
            .value_node_id = value.kind == CODE_CALL_OUTPUT ? value.source.node->id : 0,
            .value_pin_id = value.kind == CODE_CALL_OUTPUT
                              ? value.source.node->output_pins[value.source.pin].id
                              : 0 })
    );
    return true;
}

bool code_reactive_parse_event(parser *p)
{
    int start = (int)p->token.start + p->base;
    code_lexer look = p->lexer;
    code_token token = p->token;
    int end = (int)token.end;
    while (token.kind == CODE_TOKEN_NAME || code_token_is(&look, token, ".")) {
        end = (int)token.end;
        token = code_lex_next(&look);
    }
    char *reference = keep(p, p->lexer.text + start - p->base, end - start + p->base);
    code_program_complete_ecs_event(p, p->scope, reference, start);
    const char *suffix = reference ? strrchr(reference, '.') : NULL;
    if (!suffix || strcmp(suffix, ".PressedChanged")) {
        return false;
    }
    p->lexer = look;
    p->token = token;
    size_t length = (size_t)(suffix - reference);
    char *source = xmalloc(length + 9);
    memcpy(source, reference, length);
    memcpy(source + length, ".pressed", 9);
    apush(p->program->strings, source);
    code_call_argument watched = { 0 };
    if (!path(p, source, start, PIN_BOOL, &watched)) {
        return true;
    }
    int ordinal = acount(p->asset->reactives);
    declaration *d = event(p, BEHAVIOR_REACTIVE_PRESSED_CHANGED, ordinal, start);
    code_program_expect(p, "(");
    int parameter_start = (int)p->token.start + p->base;
    char *name = code_program_name(p);
    code_program_expect(p, ")");
    continuation c = { .source = d->name, .start = start };
    declaration *alias = xcalloc(1, sizeof *alias);
    alias->name = name;
    alias->call = d->call;
    alias->start = parameter_start;
    alias->end = parameter_start + (name ? (int)strlen(name) : 0);
    apush(alias->results, d->results[0]);
    apush(c.aliases, alias);
    c.body = code_statement_parse_block(p);
    c.end = (int)p->token.start + p->base;
    apush(p->scope->continuations, c);
    apush(
        p->asset->reactives,
        ((
            behavior_reactive
        ){ .kind = BEHAVIOR_REACTIVE_PRESSED_CHANGED,
           .graph_id = p->scope->native->graph_id,
           .source_node_id = watched.source.node->id,
           .source_pin_id = watched.source.node->output_pins[watched.source.pin].id })
    );
    return true;
}

void code_reactive_finish(parser *p)
{
    for (int i = 0; i < acount(p->asset->reactives); i++) {
        behavior_reactive *r = &p->asset->reactives[i];
        if (p->asset->domain != BEHAVIOR_DOMAIN_EVENT ||
            r->graph_id != p->asset->graph->graph_id) {
            code_source_error(p->source, 0, 0, "Reactive relationships belong to a behavior or system root");
            continue;
        }
        graph *g = behavior_asset_find_graph(p->asset, r->graph_id);
        char name[64];
        snprintf(name, sizeof name, r->kind == BEHAVIOR_REACTIVE_BIND ? "__Bind_%d" : "__PressedChanged_%d", i);
        for (int j = 0; g && j < acount(g->nodes); j++) {
            node *n = g->nodes[j];
            if (n->desc && n->desc->flow_kind == NODE_FLOW_EVENT &&
                n->reference_name && !strcmp(n->reference_name, name)) {
                r->event_id = n->id;
            }
        }
        for (int j = 0; j < acount(p->program->scopes); j++) {
            scope *s = p->program->scopes[j];
            for (int k = 0; k < acount(s->continuations); k++) {
                continuation *c = &s->continuations[k];
                if (!strcmp(c->source, name) && r->kind == BEHAVIOR_REACTIVE_BIND &&
                    acount(c->body) && acount(c->body[0].body)) {
                    node *n = c->body[0].body[0].native;
                    r->statement_node_id = n ? n->id : 0;
                }
            }
        }
        if (!r->event_id ||
            (r->kind == BEHAVIOR_REACTIVE_BIND && !r->statement_node_id)) {
            code_source_error(p->source, 0, 0, "Cannot lower executable reactive relationship");
        }
    }
    char message[256];
    if (!acount(p->source->diagnostics) &&
        !behavior_reactive_validate(p->asset, message, sizeof message)) {
        code_source_error(p->source, 0, 0, message);
    }
}
