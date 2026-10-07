#include "assets/code_program_internal.h"
#include "assets/code_source_intelligence.h"
#include "assets/code_source_internal.h"
#include "graph/authoring/pin_edit.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is(parser *p, const char *text)
{
    return code_token_is(&p->lexer, p->token, text);
}
static bool failed(parser *p)
{
    return acount(p->source->diagnostics) != 0;
}
static char *slice(parser *p, int start, int end)
{
    char *text = xmalloc((size_t)(end - start) + 1);
    memcpy(text, p->lexer.text + start, (size_t)(end - start));
    text[end - start] = 0;
    apush(p->program->strings, text);
    return text;
}
static void error(parser *p, statement *s, const char *message)
{
    code_source_error(p->source, s->start, s->end, message);
}
/* Statement expressions stop at their own grammar delimiter. Nested call,
 * array, and aggregate delimiters remain part of the expression. */
static char *capture(parser *p, const char *stop, int *offset)
{
    int start = (int)p->token.start, end = start, round = 0, square = 0, curly = 0;
    *offset = p->base + start;
    while (p->token.kind != CODE_TOKEN_END) {
        if (!round && !square && !curly &&
            (is(p, stop) || is(p, "}") ||
             (!strcmp(stop, ";") && end != start && code_program_statement_end(p)))) {
            break;
        }
        if (is(p, "(")) {
            round++;
        }
        if (is(p, ")")) {
            round--;
        }
        if (is(p, "{")) {
            curly++;
        }
        if (is(p, "}")) {
            curly--;
        }
        if (is(p, "[")) {
            square++;
        }
        if (is(p, "]")) {
            square--;
        }
        end = (int)p->token.end;
        code_program_complete_cast(p);
        code_program_next(p);
    }
    if (start == end) {
        code_source_error(p->source, *offset, *offset, "Expected a statement expression");
        return NULL;
    }
    return slice(p, start, end);
}
static bool starts_call(parser *p)
{
    parser look = *p;
    if (look.token.kind != CODE_TOKEN_NAME && look.token.kind != CODE_TOKEN_STRING) {
        return false;
    }
    code_program_next(&look);
    if (is(&look, ".")) {
        int member_start = p->base + (int)look.token.end;
        code_program_next(&look);
        bool named = look.token.kind == CODE_TOKEN_NAME ||
                     look.token.kind == CODE_TOKEN_STRING;
        if (named || look.token.kind == CODE_TOKEN_END || is(&look, "}") ||
            is(&look, ";") || is(&look, ")") || is(&look, ",")) {
            parser owner = *p;
            const char *asset_name = code_program_name(&owner);
            for (int i = 0;
                 asset_name && !code_program_declaration(p->scope, asset_name) &&
                 !behavior_asset_find_graph_by_name(p->asset, asset_name) &&
                 i < p->source->context.library_count;
                 i++) {
                if (!strcmp(p->source->context.libraries[i]->name, asset_name)) {
                    code_program_complete_qualified(
                        p,
                        asset_name,
                        named ? p->base + (int)look.token.start : member_start,
                        named ? p->base + (int)look.token.end : member_start
                    );
                    break;
                }
            }
        }
        code_program_next(&look);
    }
    if (!is(&look, "(") && !is(&look, "<") && !is(p, "engine")) {
        return false;
    }
    while (look.token.kind != CODE_TOKEN_END && !is(&look, "(")) {
        if (is(&look, ";") || is(&look, "}")) {
            return false;
        }
        code_program_next(&look);
    }
    int depth = 0;
    while (look.token.kind != CODE_TOKEN_END) {
        if (is(&look, "(")) {
            depth++;
        }
        if (is(&look, ")") && !--depth) {
            code_program_next(&look);
            /* A projected result, assignment or operator belongs to the
             * expression grammar, even when its first atom is a call. */
            return code_program_statement_end(&look);
        }
        code_program_next(&look);
    }
    return true; /* Keep incomplete invocation argument completion available. */
}
static char *call_name(parser *p, int *start, int *end)
{
    if (code_program_take(p, "engine")) {
        code_program_expect(p, "[");
        *start = p->base + (int)p->token.start;
        *end = p->base + (int)p->token.end;
        char *name = code_program_name(p);
        code_program_expect(p, "]");
        return name;
    }
    return code_program_name(p);
}
static const graph *direct_target(parser *p, const char *owner, const char *name, const char **resolved)
{
    char message[256];
    const graph *target = code_source_context_graph(
        &p->source->context,
        p->asset,
        owner,
        name,
        GRAPH_FUNCTION,
        resolved,
        message,
        sizeof message
    );
    if (!target && !strstr(message, "Ambiguous")) {
        target = code_source_context_graph(
            &p->source->context,
            p->asset,
            owner,
            name,
            GRAPH_MACRO,
            resolved,
            message,
            sizeof message
        );
    }
    if (!target && strstr(message, "Ambiguous")) {
        code_source_error(
            p->source,
            p->base + (int)p->token.start,
            p->base + (int)p->token.end,
            message
        );
    }
    return target;
}
static const graph *statement_target(
    parser *p,
    const char *name,
    const char *method,
    const char **owner,
    bool *qualified
)
{
    *qualified = false;
    *owner = NULL;
    if (method) {
        const graph *local = behavior_asset_find_graph_by_name(p->asset, name);
        if (local && (local->kind == GRAPH_FUNCTION || local->kind == GRAPH_MACRO)) {
            return local;
        }
        *qualified = true;
    }
    return direct_target(p, method ? name : NULL, method ? method : name, owner);
}
static const char *collection_method(const behavior_variable *state, const char *method)
{
    if (!state || !method) {
        return NULL;
    }
    if (state->container == PIN_CONTAINER_SET) {
        if (!strcmp(method, "add")) {
            return "Set Add";
        }
        if (!strcmp(method, "remove")) {
            return "Set Remove";
        }
    }
    if (state->container == PIN_CONTAINER_ARRAY) {
        if (!strcmp(method, "add")) {
            return "Array Add";
        }
        if (!strcmp(method, "remove")) {
            return "Remove Item";
        }
    }
    return NULL;
}
static argument *call_arguments(parser *p, const char *name, const char *method, bool native)
{
    argument *args = NULL;
    char message[256];
    const behavior_variable *collection = behavior_asset_find_variable_by_name(p->asset, name);
    const char *operation = native ? NULL : collection_method(collection, method);
    const node_desc *desc = code_call_descriptor(
        operation                  ? operation
        : !strcmp(name, "destroy") ? "Destroy Entity"
                                   : name,
        message,
        sizeof message
    );
    const char *owner = NULL;
    bool qualified;
    const graph *target = native || operation ||
                                  code_program_declaration(p->scope, name)
                            ? NULL
                            : statement_target(p, name, method, &owner, &qualified);
    const node *entry = target ? graph_node_by_id(target, target->entry_id) : NULL;
    const pin_spec *inputs = entry ? entry->output_pins
                           : desc  ? desc->inputs
                                   : NULL;
    int input_count = entry ? entry->output_count
                    : desc  ? desc->input_count
                            : 0;
    int positional = operation ? 1 : 0;
    code_program_expect(p, "(");
    code_call_spec call = { .name = desc ? desc->name : name,
                            .reference_asset = owner,
                            .target = target };
    code_call_port expected;
    if ((p->token.kind == CODE_TOKEN_END || is(p, ")")) &&
        code_program_positional_port(p, &call, 0, &expected)) {
        code_program_complete_value(
            p,
            p->scope,
            p->base + (int)p->token.start,
            p->base + (int)p->token.end,
            &expected
        );
    }
    while (!is(p, ")") && p->token.kind != CODE_TOKEN_END && !failed(p)) {
        argument arg = { .name_start = p->base + (int)p->token.start,
                         .name_end = p->base + (int)p->token.end };
        parser look = *p;
        code_program_next(&look);
        while (is(&look, ".")) {
            code_program_next(&look);
            if (look.token.kind != CODE_TOKEN_NAME &&
                look.token.kind != CODE_TOKEN_STRING) {
                break;
            }
            code_program_next(&look);
        }
        if ((p->token.kind == CODE_TOKEN_NAME || p->token.kind == CODE_TOKEN_STRING) &&
            is(&look, ":")) {
            arg.port.name = code_program_name(p);
            while (code_program_take(p, ".")) {
                arg.name_end = p->base + (int)p->token.end;
                char *field = code_program_name(p), *path = NULL;
                const char *parts[] = { arg.port.name, ".", field };
                for (int i = 0; i < 3; i++) {
                    for (const char *at = parts[i]; at && *at; at++) {
                        apush(path, *at);
                    }
                }
                apush(path, 0);
                char *owned = xstrdup(path);
                apush(p->program->strings, owned);
                afree(path);
                arg.port.name = owned;
            }
            code_program_expect(p, ":");
            if (code_program_type_token(p)) {
                const char *name = arg.port.name;
                arg.port = code_program_type(p);
                arg.port.name = name;
                arg.annotated = true;
                if (code_program_take(p, "from") || code_program_take(p, "split")) {
                    arg.port.aggregate = code_program_name(p);
                    code_program_expect(p, ".");
                    arg.port.field = code_program_name(p);
                }
                if (!code_program_take(p, "=") && arg.port.kind == PIN_DATA) {
                    code_source_error(
                        p->source,
                        p->base + (int)p->token.start,
                        p->base + (int)p->token.end,
                        "Typed argument requires a value"
                    );
                    break;
                }
            }
        } else {
            while (positional < input_count && inputs[positional].kind == PIN_EXEC) {
                positional++;
            }
            if (!inputs || positional >= input_count) {
                code_source_error(
                    p->source,
                    p->base + (int)p->token.start,
                    p->base + (int)p->token.end,
                    "Positional native argument requires a unique declared "
                    "input"
                );
                break;
            }
            arg.positional = !operation;
            for (int i = 0; i < positional; i++) {
                arg.ordinal += inputs[i].kind == PIN_DATA && !inputs[i].field_source;
            }
            arg.port.name = inputs[positional++].name;
            arg.name_end = arg.name_start;
        }
        if (operation) {
            arg.port = (code_call_port){ .name = "Item",
                                         .type = collection->type,
                                         .type_name = collection->type_name,
                                         .kind = PIN_DATA };
            arg.annotated = true;
        }
        if (entry && !arg.annotated) {
            int index = code_call_port_index(entry, true, arg.port.name, message, sizeof message);
            if (index >= 0) {
                const pin_spec *pin = &entry->output_pins[index];
                arg.port = (code_call_port){ .name = pin->name,
                                             .type = pin->type,
                                             .type_name = pin->type_name,
                                             .container = pin->container,
                                             .kind = pin->kind };
                arg.annotated = true;
            }
        }
        if (!arg.annotated || arg.port.kind == PIN_DATA) {
            if (code_program_argument_port(p, &call, arg.port.name, &expected)) {
                code_program_complete_value(
                    p,
                    p->scope,
                    p->base + (int)p->token.start,
                    p->base + (int)p->token.end,
                    &expected
                );
            }
            arg.text = code_program_capture(p, &arg.start, &arg.end);
            arg.start += p->base;
            arg.end += p->base;
        }
        apush(args, arg);
        if (!code_program_take(p, ",")) {
            break;
        }
    }
    code_program_expect(p, ")");
    return args;
}
static statement parse_statement(parser *p)
{
    statement s = { .start = p->base + (int)p->token.start };
    if (++p->depth > 128) {
        s.end = p->base + (int)p->token.end;
        error(p, &s, "Statement nesting exceeds 128 levels");
        p->depth--;
        return s;
    }
    if (code_program_take(p, "match")) {
        int start = s.start;
        s = code_statement_parse_match(p);
        s.start = start;
    } else if (code_program_take(p, "sequence")) {
        s.kind = CODE_STMT_SEQUENCE;
        code_program_expect(p, "{");
        while (!is(p, "}") && p->token.kind != CODE_TOKEN_END && !failed(p)) {
            statement branch = { .kind = CODE_STMT_BLOCK,
                                 .start = p->base + (int)p->token.start };
            code_program_expect(p, "branch");
            branch.body = code_statement_parse_block(p);
            branch.end = p->base + (int)p->token.start;
            apush(s.body, branch);
        }
        code_program_expect(p, "}");
    } else if (code_program_take(p, "if") || code_program_take(p, "while")) {
        s.kind = code_token_is(
                     &p->lexer,
                     (code_token){ .start = (size_t)(s.start - p->base),
                                   .end = (size_t)(s.start - p->base + 2) },
                     "if"
                 )
                   ? CODE_STMT_IF
                   : CODE_STMT_WHILE;
        bool parens = code_program_take(p, "(");
        s.text = capture(p, parens ? ")" : "{", &s.value_start);
        if (parens) {
            code_program_expect(p, ")");
        }
        s.body = code_statement_parse_block(p);
        if (s.kind == CODE_STMT_IF && code_program_take(p, "else")) {
            if (is(p, "if")) {
                apush(s.alternate, parse_statement(p));
            } else {
                s.alternate = code_statement_parse_block(p);
            }
        }
    } else if (code_program_take(p, "for") || code_program_take(p, "foreach")) {
        bool foreach = !strncmp(p->lexer.text + s.start - p->base, "foreach", 7);
        s.kind = foreach ? CODE_STMT_FOREACH : CODE_STMT_FOR;
        bool parens = code_program_take(p, "(");
        s.target_start = p->base + (int)p->token.start;
        s.alias = code_program_name(p);
        s.target_end = s.target_start + (s.alias ? (int)strlen(s.alias) : 0);
        code_program_expect(p, "in");
        s.text = capture(p, parens ? ")" : "{", &s.value_start);
        if (!foreach && s.text) {
            char *range = strstr(s.text, "..");
            if (range) {
                s.second = range + 2;
                s.second_start = s.value_start + (int)(range + 2 - s.text);
                *range = 0;
            } else {
                s.kind = CODE_STMT_FOREACH;
            }
        }
        if (parens) {
            code_program_expect(p, ")");
        }
        s.body = code_statement_parse_block(p);
    } else if (code_program_take(p, "return")) {
        s.kind = CODE_STMT_RETURN;
        if (code_program_take(p, ".")) {
            s.method = code_program_name(p);
        }
        if (!code_program_statement_end(p)) {
            if (is(p, "(")) {
                s.arguments = code_program_arguments(p, false);
            } else {
                s.text = capture(p, ";", &s.value_start);
            }
        }
        code_program_expect(p, ";");
    } else if (code_program_take(p, "goto")) {
        s.kind = CODE_STMT_GOTO;
        s.target_start = p->base + (int)p->token.start;
        s.target_end = p->base + (int)p->token.end;
        s.target = code_program_name(p);
        code_program_expect(p, ";");
    } else if (code_program_take(p, "break") || code_program_take(p, "continue")) {
        s.kind = !strncmp(p->lexer.text + s.start - p->base, "break", 5)
                   ? CODE_STMT_BREAK
                   : CODE_STMT_CONTINUE;
        code_program_expect(p, ";");
    } else {
        parser declaration_look = *p;
        code_program_next(&declaration_look);
        if (p->token.kind == CODE_TOKEN_NAME && is(&declaration_look, ":")) {
            code_program_next(&declaration_look);
            if (is(&declaration_look, "=")) {
                s.result_start = p->base + (int)p->token.start;
                s.result_end = p->base + (int)p->token.end;
                s.result_name = code_program_name(p);
                code_program_expect(p, ":");
                code_program_expect(p, "=");
            }
        }
        s.awaited = code_program_take(p, "await");
        parser look = *p;
        code_program_next(&look);
        if (s.result_name && !s.awaited) {
            s.kind = CODE_STMT_LOCAL;
            s.text = capture(p, ";", &s.value_start);
            code_program_expect(p, ";");
        } else if (p->token.kind == CODE_TOKEN_NAME && is(&look, ":")) {
            s.kind = CODE_STMT_LABEL;
            s.target_start = p->base + (int)p->token.start;
            s.target_end = p->base + (int)p->token.end;
            s.target = code_program_name(p);
            code_program_expect(p, ":");
        } else if (starts_call(p)) {
            s.kind = CODE_STMT_CALL;
            s.target_start = p->base + (int)p->token.start;
            s.target_end = p->base + (int)p->token.end;
            s.engine = is(p, "engine");
            s.target = call_name(p, &s.target_start, &s.target_end);
            if (code_program_take(p, ".")) {
                s.method_start = p->base + (int)p->token.start;
                s.method_end = p->base + (int)p->token.end;
                s.method = code_program_name(p);
            } else if (code_program_take(p, "[")) {
                s.method_start = p->base + (int)p->token.start;
                s.method_end = p->base + (int)p->token.end;
                s.method = code_program_name(p);
                code_program_expect(p, "]");
            }
            if (code_program_take(p, "<")) {
                /* Callable references are semantic names, not graph records. */
                s.alias = code_program_reference_name(p);
                if (code_program_take(p, ",")) {
                    s.third = code_program_name(p);
                }
                code_program_expect(p, ">");
            }
            s.arguments = call_arguments(p, s.target, s.method, s.engine);
            code_program_expect(p, ";");
        } else {
            if (s.awaited) {
                error(p, &s, "await requires a callable invocation");
            }
            s.kind = CODE_STMT_EXPRESSION;
            s.text = capture(p, ";", &s.value_start);
            code_program_expect(p, ";");
        }
    }
    s.end = p->base + (int)p->token.start;
    p->depth--;
    return s;
}
statement *code_statement_parse_body(parser *p)
{
    statement *body = NULL;
    while (!is(p, "}") && p->token.kind != CODE_TOKEN_END && !failed(p)) {
        if (code_program_system_declaration(p)) {
            continue;
        }
        apush(body, parse_statement(p));
    }
    return body;
}
statement *code_statement_parse_block(parser *p)
{
    statement *body = NULL;
    if (!code_program_expect(p, "{")) {
        return body;
    }
    while (!is(p, "}") && p->token.kind != CODE_TOKEN_END && !failed(p)) {
        apush(body, parse_statement(p));
    }
    code_program_expect(p, "}");
    return body;
}

typedef struct
{
    node *node;
    int pin;
} flow_exit;
typedef struct
{
    parser *parser;
    scope *scope;
    code_program_label *labels;
    int loops, block_end;
} lower;
static int execution(const node *n, bool output, const char *name)
{
    if (!n) {
        return -1;
    }
    if (name) {
        char message[256];
        int index = code_call_port_index(n, output, name, message, sizeof message);
        return index >= 0 && (output ? n->output_pins : n->input_pins)[index].kind == PIN_EXEC
                 ? index
                 : -1;
    }
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count, found = -1;
    if (!output) {
        for (int i = 0; i < count; i++) {
            if (pins[i].kind == PIN_EXEC && !strcmp(pins[i].name, "Exec")) {
                return i;
            }
        }
    }
    for (int i = 0; i < count; i++) {
        if (pins[i].kind != PIN_EXEC || (name && strcmp(pins[i].name, name))) {
            continue;
        }
        if (found >= 0) {
            return -1;
        }
        found = i;
    }
    return found;
}
static bool connect(lower *l, flow_exit *incoming, node *n, int input, statement *stmt)
{
    if (!n || input < 0) {
        error(l->parser, stmt, "Execution entry is missing or ambiguous");
        return false;
    }
    for (int i = 0; i < acount(incoming); i++) {
        if (graph_connect_exec(l->scope->native, incoming[i].node, incoming[i].pin, n, input, 0)) {
            error(l->parser, stmt, "Cannot connect statement execution");
            return false;
        }
    }
    return true;
}
static bool feed_value(lower *l, node *n, int input, const char *text, int start)
{
    pin_spec *pin = &n->input_pins[input];
    code_call_port expected = { .type = pin->type,
                                .type_name = pin->type_name,
                                .container = pin->container,
                                .kind = pin->kind };
    code_call_argument value = { 0 };
    code_program_complete_value(l->parser, l->scope, start, start + (int)strlen(text), &expected);
    if (!code_program_value(l->parser, l->scope, text, start, &expected, &value)) {
        return false;
    }
    bool cast_boundary = value.has_type && pin->type != PIN_FLOW &&
                         value.type != pin->type;
    if (cast_boundary) {
        char message[192];
        if (!code_call_materialize_cast(l->parser->asset, l->scope->native, &value, message, sizeof message)) {
            code_source_error(l->parser->source, start, start + (int)strlen(text), message);
            return false;
        }
    }
    if ((pin->flags & PIN_FLAG_WILDCARD) && !cast_boundary) {
        code_call_port actual = { 0 };
        if (!code_statement_value_type(l->parser, l->scope, &value, &actual) ||
            graph_set_pin_type(
                l->scope->native,
                n,
                false,
                input,
                actual.type,
                actual.type_name
            )) {
            return false;
        }
    }
    if (value.kind == CODE_CALL_LITERAL) {
        return !graph_set_pin_default(l->scope->native, n, false, input, &value.literal) &&
               !code_call_apply_fallback(l->scope->native, n, input, &value);
    }
    if (value.kind == CODE_CALL_BINDING) {
        if (value.binding.kind == PIN_BINDING_LOCAL) {
            graph *g = l->scope->native;
            if (!value.conversion) {
                return !code_call_use_local(g, value.binding.local_id, n, input) &&
                       !code_call_apply_fallback(g, n, input, &value);
            }
            const graph_local *local = graph_local_by_id(g, value.binding.local_id);
            node *producer = local ? graph_node_by_id(g, local->source_id) : NULL;
            int output = 0, index = -1;
            if (!producer ||
                !graph_pin_by_id(producer, local->source_pin_id, &output, &index) ||
                !output ||
                graph_connect_data_conversion(
                    g,
                    producer,
                    index,
                    n,
                    input,
                    value.conversion
                )) {
                return false;
            }
        }
        return !graph_set_pin_binding(l->scope->native, n, false, input, &value.binding) &&
               !code_call_apply_fallback(l->scope->native, n, input, &value);
    }
    pin_spec *source = &value.source.node->output_pins[value.source.pin];
    unsigned flags = source->flags;
    if (cast_boundary) {
        source->flags &= ~PIN_FLAG_WILDCARD;
    }
    int result = graph_connect_data_conversion(
        l->scope->native,
        value.source.node,
        value.source.pin,
        n,
        input,
        value.conversion
    );
    source->flags = flags;
    return !result && !code_call_apply_fallback(l->scope->native, n, input, &value);
}
static bool feed(lower *l, node *n, int input, const char *text, int start)
{
    bool resolved = feed_value(l, n, input, text, start);
    if (!resolved && !failed(l->parser)) {
        code_source_error(l->parser->source, start, start + (int)strlen(text), "Cannot resolve or connect statement input");
    }
    return resolved;
}
static flow_exit *one(node *n, int pin)
{
    flow_exit *result = NULL;
    if (n && pin >= 0) {
        apush(result, ((flow_exit){ n, pin }));
    }
    return result;
}
static flow_exit *lower_body(lower *l, flow_exit *incoming, statement *body, bool follows);
static void bind_loop_alias(lower *l, statement *stmt, declaration *alias, code_call_port *result)
{
    *alias = (declaration){ .name = stmt->alias,
                            .native = stmt->native,
                            .start = stmt->start,
                            .end = stmt->end };
    int pin = 1;
    const pin_spec *native = &stmt->native->output_pins[pin];
    *result = (code_call_port){ .name = native->name,
                                .type = native->type,
                                .type_name = native->type_name,
                                .container = native->container,
                                .kind = PIN_DATA };
    apush(alias->results, *result);
    alias->symbol = (uint64_t)stmt->target_start + 1;
    code_semantic_symbol symbol = {
        .id = alias->symbol,
        .kind = CODE_SYMBOL_LOCAL,
        .name = stmt->alias,
        .type = { result->type, result->container, result->type_name },
        .start = stmt->target_start,
        .end = stmt->target_end,
        .scope_start = stmt->start,
        .scope_end = stmt->end,
        .graph_id = l->scope->native->graph_id,
        .node_id = stmt->native->id,
        .pin_id = native->id,
        .renameable = true
    };
    code_intelligence_symbol(l->parser->source, &symbol);
    apush(l->scope->declarations, alias);
}

bool code_statement_probe_loop(parser *p, scope *s, statement *stmt, declaration *alias)
{
    if (!stmt->alias || !stmt->text || (stmt->kind == CODE_STMT_FOR && !stmt->second)) {
        return false;
    }
    lower probe = { .parser = p, .scope = s };
    node *n = graph_add_node(s->native, stmt->kind == CODE_STMT_FOR ? &NODE_ForLoop : &NODE_ForEach);
    stmt->native = n;
    if (!feed(&probe, n, 1, stmt->text, stmt->value_start) ||
        (stmt->kind == CODE_STMT_FOR &&
         !feed(&probe, n, 2, stmt->second, stmt->second_start))) {
        return false;
    }
    code_call_port result;
    bind_loop_alias(&probe, stmt, alias, &result);
    return true;
}

static flow_exit *lower_collection(
    lower *l,
    flow_exit *incoming,
    statement *stmt,
    const behavior_variable *variable,
    const char *operation
)
{
    if (acount(stmt->arguments) != 1) {
        error(l->parser, stmt, "Collection mutation requires one item argument");
        return NULL;
    }
    argument *item = &stmt->arguments[0];
    code_call_port ports[] = {
        { .name = variable->container == PIN_CONTAINER_SET ? "Set" : "Array",
          .type = variable->type,
          .type_name = variable->type_name,
          .container = variable->container,
          .kind = PIN_DATA },
        { .name = "Item", .type = variable->type, .type_name = variable->type_name, .kind = PIN_DATA }
    };
    code_call_argument inputs[] = { { .name = ports[0].name,
                                      .kind = CODE_CALL_BINDING,
                                      .binding = { .kind = PIN_BINDING_VARIABLE,
                                                   .variable_id = variable->id } },
                                    { .name = "Item" } };
    if (!code_program_value(l->parser, l->scope, item->text, item->start, &ports[1], &inputs[1])) {
        return NULL;
    }
    item->resolved = inputs[1];
    item->has_resolved = true;
    char message[256];
    code_call_spec call = { .name = operation,
                            .inputs = ports,
                            .input_count = 2,
                            .arguments = inputs,
                            .argument_count = 2 };
    node *producer = code_call_build(l->parser->asset, l->scope->native, &call, message, sizeof message);
    if (producer) {
        code_program_record_inputs(l->parser->program, producer, inputs, 2);
    }
    code_call_output result;
    if (!producer ||
        !code_call_resolve_output(l->scope->native, producer, ports[0].name, &result, message, sizeof message)) {
        error(l->parser, stmt, message);
        return NULL;
    }
    code_call_argument value = { .name = "Value",
                                 .kind = CODE_CALL_OUTPUT,
                                 .source = result };
    call = (code_call_spec){ .name = "Set Variable",
                             .reference = variable->name,
                             .arguments = &value,
                             .argument_count = 1 };
    node *write = code_call_build(l->parser->asset, l->scope->native, &call, message, sizeof message);
    if (write) {
        code_program_record_inputs(l->parser->program, write, &value, 1);
    }
    stmt->native = write;
    if (!write || !connect(l, incoming, write, execution(write, false, NULL), stmt)) {
        return NULL;
    }
    code_program_resolved_use(
        l->parser,
        l->scope,
        stmt->target,
        stmt->target_start,
        &inputs[0],
        CODE_USE_READ | CODE_USE_WRITE
    );
    return one(write, execution(write, true, NULL));
}
static bool default_return(statement *body)
{
    for (int i = 0; i < acount(body); i++) {
        statement *s = &body[i];
        if ((s->kind == CODE_STMT_RETURN && !s->text && !acount(s->arguments)) ||
            default_return(s->body) || default_return(s->alternate)) {
            return true;
        }
    }
    return false;
}
static node *statement_call(parser *p, scope *s, statement *stmt, const char **entry_name)
{
    node *n = NULL;
    declaration *d = stmt->engine ? NULL : code_program_declaration(s, stmt->target);
    if (d) {
        if (stmt->result_name) {
            error(p, stmt, "Capturing a shared action result requires a fresh callable invocation");
            return NULL;
        }
        if (d->result_local) {
            error(p, stmt, "A captured local result is a value, not a callable");
            return NULL;
        }
        if (acount(stmt->arguments)) {
            error(p, stmt, "Shared action invocation arguments belong to its declaration");
            return NULL;
        }
        n = code_program_build(p, s, d);
        code_intelligence_use(
            p->source,
            (code_semantic_use){ d->symbol, stmt->target_start, stmt->target_end, CODE_USE_CALL }
        );
    } else {
        code_call_spec call = { .name = !strcmp(stmt->target, "destroy")
                                          ? "Destroy Entity"
                                          : stmt->target,
                                .reference = stmt->alias,
                                .reference_asset = stmt->third,
                                .native = stmt->engine };
        const char *owner = NULL;
        bool qualified;
        const graph *target = stmt->engine
                                ? NULL
                                : statement_target(p, stmt->target, stmt->method, &owner, &qualified);
        if (target && (target->kind == GRAPH_FUNCTION || target->kind == GRAPH_MACRO)) {
            const node *entry = graph_node_by_id(target, target->entry_id);
            bool pure = target->kind == GRAPH_FUNCTION;
            for (int i = 0; entry && i < entry->output_count; i++) {
                pure &= entry->output_pins[i].kind != PIN_EXEC;
            }
            call.name = target->kind == GRAPH_MACRO ? "call_macro"
                      : pure                        ? "call_pure_function"
                                                    : "call_function";
            call.reference = target->name;
            call.reference_asset = owner;
            call.target = target;
            *entry_name = qualified ? NULL : stmt->method;
            if (qualified && stmt->method_end > stmt->method_start) {
                stmt->target_start = stmt->method_start;
                stmt->target_end = stmt->method_end;
            }
        }
        n = code_program_call(p, s, call, stmt->arguments, stmt->start, stmt->end);
    }
    return n;
}
node *code_statement_probe_call(parser *p, scope *s, statement *stmt)
{
    const char *entry = stmt->method;
    return statement_call(p, s, stmt, &entry);
}
static bool result_name_available(parser *p, scope *s, const char *name)
{
    if (code_program_reserved(name) || !strcmp(name, "delta") ||
        !strcmp(name, "component") ||
        behavior_asset_find_variable_by_name(p->asset, name) ||
        code_program_declaration(s, name)) {
        return false;
    }
    for (int i = 0; i < acount(s->locals); i++) {
        if (!strcmp(s->locals[i].name, name)) {
            return false;
        }
    }
    for (int i = 0; i < acount(s->native->locals); i++) {
        if (!strcmp(s->native->locals[i].name, name)) {
            return false;
        }
    }
    const node *entry = graph_node_by_id(s->native, s->native->entry_id);
    for (int i = 0; entry && i < entry->output_count; i++) {
        if (entry->output_pins[i].kind == PIN_DATA &&
            !strcmp(entry->output_pins[i].name, name)) {
            return false;
        }
    }
    return true;
}
static flow_exit *lower_one(lower *l, flow_exit *incoming, statement *stmt, bool follows)
{
    parser *p = l->parser;
    graph *g = l->scope->native;
    node *n = NULL;
    const char *entry_name = stmt->method;
    switch (stmt->kind) {
        case CODE_STMT_BLOCK:
            return lower_body(l, incoming, stmt->body, follows);
        case CODE_STMT_MATCH: {
            int *outputs = NULL, default_output = -1;
            n = code_statement_match_node(p, l->scope, stmt, &outputs, &default_output);
            if (!n || !connect(l, incoming, n, 0, stmt)) {
                afree(outputs);
                return NULL;
            }
            flow_exit *joined = NULL;
            bool has_default = false;
            for (int i = 0; i < acount(stmt->body); i++) {
                flow_exit *entry = one(n, outputs[i]);
                if (!stmt->body[i].text) {
                    has_default = true;
                    for (int pin = 0; pin < default_output; pin++) {
                        bool selected = false;
                        for (int arm = 0; arm < acount(outputs); arm++) {
                            selected |= outputs[arm] == pin;
                        }
                        if (!selected) {
                            apush(entry, ((flow_exit){ n, pin }));
                        }
                    }
                }
                flow_exit *tail = lower_body(l, entry, stmt->body[i].body, follows);
                for (int j = 0; j < acount(tail); j++) {
                    apush(joined, tail[j]);
                }
                afree(tail);
            }
            if (!has_default) {
                for (int pin = 0; pin <= default_output; pin++) {
                    bool selected = false;
                    for (int arm = 0; arm < acount(outputs); arm++) {
                        selected |= outputs[arm] == pin;
                    }
                    if (!selected) {
                        apush(joined, ((flow_exit){ n, pin }));
                    }
                }
            }
            afree(outputs);
            return joined;
        }
        case CODE_STMT_SEQUENCE: {
            if (!acount(stmt->body)) {
                error(p, stmt, "Sequence requires at least one branch");
                return NULL;
            }
            n = graph_add_node(g, &NODE_Sequence);
            stmt->native = n;
            int count = acount(stmt->body) + follows;
            while (n->output_count > count) {
                graph_remove_pin(g, n, true, n->output_count - 1);
            }
            while (n->output_count < count) {
                char name[32];
                snprintf(name, sizeof name, "Then%d", n->output_count);
                pin_spec pin = { .name = name, .type = PIN_FLOW, .kind = PIN_EXEC };
                graph_append_pin(g, n, true, &pin);
            }
            if (!connect(l, incoming, n, 0, stmt)) {
                return NULL;
            }
            for (int i = 0; i < acount(stmt->body); i++) {
                flow_exit *tail = lower_body(l, one(n, i), stmt->body[i].body, false);
                afree(tail);
            }
            return follows ? one(n, count - 1) : NULL;
        }
        case CODE_STMT_IF:
        case CODE_STMT_WHILE: {
            code_component_binding *narrowed = NULL;
            bool present = false;
            code_call_argument presence = { 0 };
            int recognized = code_components_condition(
                p,
                l->scope,
                stmt->text,
                stmt->value_start,
                &presence,
                &narrowed,
                &present
            );
            if (!recognized) {
                return NULL;
            }
            if (recognized > 0) {
                argument condition = { .port = { .name = "Condition",
                                                 .kind = PIN_DATA,
                                                 .type = PIN_BOOL },
                                       .has_resolved = true,
                                       .resolved = presence,
                                       .start = stmt->value_start,
                                       .end = stmt->end };
                code_call_spec call = { .name = stmt->kind == CODE_STMT_IF
                                                  ? "Branch"
                                                  : "WhileLoop",
                                        .native = true };
                argument *inputs = NULL;
                apush(inputs, condition);
                n = code_program_call(p, l->scope, call, inputs, stmt->start, stmt->end);
                afree(inputs);
                stmt->native = n;
                if (!n || !connect(l, incoming, n, 0, stmt)) {
                    return NULL;
                }
            } else if (stmt->kind == CODE_STMT_IF && !code_program_atomic_value(stmt->text)) {
                code_call_port expected = { .type = PIN_BOOL, .kind = PIN_DATA };
                code_call_argument condition = { 0 };
                graph_expression_mode previous = p->expression_mode;
                p->expression_mode = GRAPH_EXPRESSION_BRANCH;
                bool built = code_statement_expression(
                    p,
                    l->scope,
                    stmt->text,
                    stmt->value_start,
                    &expected,
                    &condition
                );
                p->expression_mode = previous;
                if (!built) {
                    if (!failed(p)) {
                        error(p, stmt, "Cannot lower branch condition");
                    }
                    return NULL;
                }
                n = condition.source.node;
                stmt->native = n;
                if (!connect(l, incoming, n, execution(n, false, NULL), stmt)) {
                    return NULL;
                }
            } else {
                n = graph_add_node(g, stmt->kind == CODE_STMT_IF ? &NODE_Branch : &NODE_WhileLoop);
                stmt->native = n;
                if (!feed(l, n, 1, stmt->text, stmt->value_start) ||
                    !connect(l, incoming, n, 0, stmt)) {
                    return NULL;
                }
            }
            if (stmt->kind == CODE_STMT_IF) {
                bool previous = narrowed && narrowed->narrowed;
                unsigned revision = l->scope->narrowing_revision;
                bool *baseline = NULL;
                for (int i = 0; i < acount(l->scope->components); i++) {
                    apush(baseline, l->scope->components[i].narrowed);
                }
                if (narrowed) {
                    narrowed->narrowed = previous || present;
                }
                flow_exit *yes = lower_body(l, one(n, 0), stmt->body, follows);
                for (int i = 0; i < acount(baseline); i++) {
                    l->scope->components[i].narrowed = baseline[i];
                }
                if (narrowed) {
                    narrowed->narrowed = previous || !present;
                }
                unsigned true_revision = l->scope->narrowing_revision;
                l->scope->narrowing_revision = revision;
                flow_exit *no = lower_body(l, one(n, 1), stmt->alternate, follows);
                bool invalidated = true_revision != revision ||
                                   l->scope->narrowing_revision != revision;
                l->scope->narrowing_revision = revision + invalidated;
                for (int i = 0; i < acount(baseline); i++) {
                    l->scope->components[i].narrowed = baseline[i] && !invalidated;
                }
                afree(baseline);
                for (int i = 0; i < acount(no); i++) {
                    apush(yes, no[i]);
                }
                afree(no);
                return yes;
            }
            l->loops++;
            bool previous = narrowed && narrowed->narrowed;
            unsigned revision = l->scope->narrowing_revision;
            if (narrowed) {
                narrowed->narrowed = previous || present;
            }
            flow_exit *tail = lower_body(l, one(n, 0), stmt->body, false);
            if (narrowed) {
                narrowed->narrowed = previous && revision == l->scope->narrowing_revision;
            }
            afree(tail);
            l->loops--;
            return one(n, 1);
        }
        case CODE_STMT_FOR:
        case CODE_STMT_FOREACH: {
            n = graph_add_node(g, stmt->kind == CODE_STMT_FOR ? &NODE_ForLoop : &NODE_ForEach);
            stmt->native = n;
            if (!feed(l, n, 1, stmt->text, stmt->value_start) ||
                (stmt->kind == CODE_STMT_FOR &&
                 !feed(l, n, 2, stmt->second, stmt->second_start)) ||
                !connect(l, incoming, n, 0, stmt)) {
                return NULL;
            }
            declaration alias;
            code_call_port result;
            bind_loop_alias(l, stmt, &alias, &result);
            l->loops++;
            flow_exit *tail = lower_body(l, one(n, 0), stmt->body, false);
            afree(tail);
            l->loops--;
            asetlen(l->scope->declarations, acount(l->scope->declarations) - 1);
            afree(alias.results);
            return one(n, stmt->kind == CODE_STMT_FOR ? 2 : 3);
        }
        case CODE_STMT_BREAK:
            if (!l->loops) {
                error(p, stmt, "Break requires a loop");
                return NULL;
            }
            n = graph_add_node(g, &NODE_Break);
            stmt->native = n;
            connect(l, incoming, n, 0, stmt);
            return NULL;
        case CODE_STMT_CONTINUE:
            if (!l->loops) {
                error(p, stmt, "Continue requires a loop");
            }
            return NULL;
        case CODE_STMT_GOTO:
            for (int i = 0; i < acount(l->labels); i++) {
                if (!strcmp(l->labels[i].name, stmt->target)) {
                    stmt->native = l->labels[i].node;
                    connect(l, incoming, stmt->native, 0, stmt);
                    return NULL;
                }
            }
            error(p, stmt, "Unknown goto label");
            return NULL;
        case CODE_STMT_LABEL:
            connect(l, incoming, stmt->native, 0, stmt);
            return one(stmt->native, 0);
        case CODE_STMT_RETURN: {
            node *signature = graph_node_by_id(g, g->return_id);
            if (!signature) {
                error(p, stmt, "Return requires a callable scope");
                return NULL;
            }
            bool used = false;
            for (int i = 0; i < acount(g->edges); i++) {
                used |= g->edges[i].to_id == signature->id &&
                        g->edges[i].kind == GRAPH_EDGE_EXEC;
            }
            bool overrides = stmt->text != NULL || acount(stmt->arguments) > 0;
            for (int i = 0; overrides && i < acount(l->scope->continuations); i++) {
                used |= default_return(l->scope->continuations[i].body);
            }
            used &= overrides;
            n = used ? graph_add_node(g, &NODE_Return) : signature;
            if (used) {
                graph_remove_pin(g, n, false, 0);
                for (int i = 0; i < signature->input_count; i++) {
                    pin_spec pin = signature->input_pins[i];
                    pin.id = 0;
                    pin.declaration_id = signature->input_pins[i].id;
                    pin.binding = NULL;
                    graph_add_pin(g, n, false, &pin);
                }
                for (int i = 0; i < signature->input_count; i++) {
                    if (signature->input_pins[i].kind != PIN_DATA) {
                        continue;
                    }
                    node *driver = NULL;
                    int output = -1;
                    if (graph_input_source(g, signature, i, &driver, &output) &&
                        graph_connect_data_conversion(
                            g,
                            driver,
                            output,
                            n,
                            i,
                            graph_input_conversion(g, signature, i)
                        )) {
                        error(p, stmt, "Cannot retain the callable's default return source");
                        return NULL;
                    }
                    if (signature->input_pins[i].binding &&
                        graph_set_pin_binding(
                            g,
                            n,
                            false,
                            i,
                            signature->input_pins[i].binding
                        )) {
                        error(p, stmt, "Cannot retain the callable's default return binding");
                        return NULL;
                    }
                }
            }
            stmt->native = n;
            if (stmt->text) {
                int result = -1;
                for (int i = 0; i < n->input_count; i++) {
                    if (n->input_pins[i].kind != PIN_DATA) {
                        continue;
                    }
                    if (result >= 0) {
                        error(p, stmt, "Multiple return values require named arguments");
                        return NULL;
                    }
                    result = i;
                }
                if (result < 0 || !feed(l, n, result, stmt->text, stmt->value_start)) {
                    return NULL;
                }
            }
            for (int i = 0; i < acount(stmt->arguments); i++) {
                argument *a = &stmt->arguments[i];
                int pin = -1;
                for (int j = 0; j < n->input_count; j++) {
                    if (!strcmp(n->input_pins[j].name, a->port.name)) {
                        pin = j;
                    }
                }
                if (pin < 0 || !feed(l, n, pin, a->text, a->start)) {
                    error(p, stmt, "Unknown callable return value");
                    return NULL;
                }
            }
            connect(l, incoming, n, execution(n, false, stmt->method), stmt);
            return NULL;
        }
        case CODE_STMT_LOCAL: {
            argument input = { .port = { .name = "Value", .kind = PIN_DATA },
                               .text = stmt->text,
                               .start = stmt->value_start,
                               .end = stmt->value_start + (int)strlen(stmt->text) };
            code_call_spec call = { .name = "Snapshot", .native = true };
            argument *inputs = NULL;
            apush(inputs, input);
            n = code_program_call(p, l->scope, call, inputs, stmt->start, stmt->end);
            afree(inputs);
            break;
        }
        case CODE_STMT_EXPRESSION: {
            code_call_argument value = { 0 };
            code_call_port expected = { .type = PIN_FLOW, .kind = PIN_DATA };
            if (!code_statement_expression(p, l->scope, stmt->text, stmt->value_start, &expected, &value) ||
                value.kind != CODE_CALL_OUTPUT) {
                if (!failed(p)) {
                    error(p, stmt, "Cannot lower statement expression");
                }
                return NULL;
            }
            n = value.source.node;
            break;
        }
        case CODE_STMT_CALL: {
            const behavior_variable
                *variable = behavior_asset_find_variable_by_name(p->asset, stmt->target);
            const char *operation = stmt->engine
                                      ? NULL
                                      : collection_method(variable, stmt->method);
            if (operation) {
                return lower_collection(l, incoming, stmt, variable, operation);
            }
            n = statement_call(p, l->scope, stmt, &entry_name);
            break;
        }
    }
    stmt->native = n;
    int entry = execution(n, false, entry_name);
    if (stmt->kind == CODE_STMT_CALL && !stmt->awaited && follows) {
        node *sequence = graph_add_node(g, &NODE_Sequence);
        stmt->sequence = sequence;
        flow_exit *invoke = one(sequence, 0);
        bool connected = connect(l, incoming, sequence, 0, stmt) &&
                         connect(l, invoke, n, entry, stmt);
        afree(invoke);
        if (!connected) {
            return NULL;
        }
        return one(sequence, 1);
    }
    if (!n || !connect(l, incoming, n, entry, stmt)) {
        return NULL;
    }
    int completion = execution(n, true, NULL);
    if (stmt->kind == CODE_STMT_CALL && stmt->awaited) {
        completion = -1;
        for (int i = 0; i < n->output_count; i++) {
            const pin_spec *pin = &n->output_pins[i];
            if (pin->kind != PIN_EXEC ||
                (strcmp(pin->name, "Then") && strcmp(pin->name, "Completed"))) {
                continue;
            }
            if (completion >= 0) {
                error(p, stmt, "await requires one conventional Then or Completed outcome");
                return NULL;
            }
            completion = i;
        }
        if (completion < 0) {
            error(p, stmt, "await requires one conventional Then or Completed outcome");
            return NULL;
        }
    }
    if (stmt->awaited) {
        l->scope->narrowing_revision++;
        for (int i = 0; i < acount(l->scope->components); i++) {
            l->scope->components[i].narrowed = false;
        }
    }
    if (stmt->result_name) {
        int result = -1;
        for (int i = 0; i < n->output_count; i++) {
            if (n->output_pins[i].kind != PIN_DATA || n->output_pins[i].field_source) {
                continue;
            }
            if (result >= 0) {
                error(p, stmt, "An inferred declaration requires one callable result");
                return NULL;
            }
            result = i;
        }
        if (result < 0) {
            error(p, stmt, "The awaited callable has no result");
            return NULL;
        }
        if (!result_name_available(p, l->scope, stmt->result_name)) {
            error(p, stmt, "Duplicate or shadowed local declaration");
            return NULL;
        }
        declaration *alias = xcalloc(1, sizeof *alias);
        stmt->result_alias = alias;
        *alias = (declaration){ .name = stmt->result_name,
                                .result_local = true,
                                .native = n,
                                .start = stmt->result_start,
                                .end = stmt->result_end,
                                .symbol = (uint64_t)stmt->result_start + 1 };
        const pin_spec *pin = &n->output_pins[result];
        if (stmt->kind == CODE_STMT_LOCAL &&
            !graph_declare_local(g, n, result, stmt->result_name)) {
            error(p, stmt, "Cannot retain the inferred local name");
            return NULL;
        }
        apush(
            alias->results,
            ((code_call_port){ .name = pin->name,
                               .kind = PIN_DATA,
                               .type = pin->type,
                               .type_name = pin->type_name,
                               .container = pin->container })
        );
        code_semantic_symbol symbol = { .id = alias->symbol,
                                        .kind = CODE_SYMBOL_LOCAL,
                                        .name = alias->name,
                                        .type = { pin->type, pin->container, pin->type_name },
                                        .start = stmt->result_start,
                                        .end = stmt->result_end,
                                        .scope_start = stmt->end,
                                        .scope_end = l->block_end,
                                        .graph_id = g->graph_id,
                                        .node_id = n->id,
                                        .pin_id = pin->id,
                                        .renameable = true };
        code_intelligence_symbol(p->source, &symbol);
        apush(l->scope->declarations, alias);
    }
    return one(n, completion);
}
static flow_exit *lower_body(lower *l, flow_exit *incoming, statement *body, bool follows)
{
    int declarations = acount(l->scope->declarations);
    int previous_end = l->block_end;
    l->block_end = acount(body) ? body[acount(body) - 1].end : l->scope->end;
    for (int i = 0; i < acount(body) && !failed(l->parser); i++) {
        flow_exit *next = lower_one(l, incoming, &body[i], i + 1 < acount(body) || follows);
        afree(incoming);
        incoming = next;
    }
    asetlen(l->scope->declarations, declarations);
    l->block_end = previous_end;
    return incoming;
}
static statement *block_capture(statement *body)
{
    for (int i = 0; i < acount(body); i++) {
        if (body[i].result_name) {
            return &body[i];
        }
        statement *capture = block_capture(body[i].body);
        if (!capture) {
            capture = block_capture(body[i].alternate);
        }
        if (capture) {
            return capture;
        }
    }
    return NULL;
}
void code_statement_lower_block(parser *p, scope *s, node *from, int output, statement *body)
{
    if (acount(s->labels)) {
        for (int i = 0; i < acount(s->continuations); i++) {
            statement *capture = block_capture(s->continuations[i].body);
            if (capture) {
                error(p, capture, "Awaited result declarations require structured control flow; labels and goto can bypass initialization");
                return;
            }
        }
    }
    lower l = { .parser = p, .scope = s, .labels = s->labels };
    flow_exit *tail = lower_body(&l, one(from, output), body, false);
    afree(tail);
}
node *code_program_statement_node(scope *s, statement *statement)
{
    if (statement->sequence) {
        return statement->sequence;
    }
    if (statement->kind == CODE_STMT_GOTO || statement->label_alias) {
        return NULL;
    }
    for (int i = 0; i < acount(s->declarations); i++) {
        if (s->declarations[i]->native == statement->native) {
            return NULL;
        }
    }
    return statement->native;
}
void code_statement_free(statement *body)
{
    for (int i = 0; i < acount(body); i++) {
        afree(body[i].arguments);
        if (body[i].result_alias) {
            afree(body[i].result_alias->results);
            free(body[i].result_alias);
        }
        code_statement_free(body[i].body);
        code_statement_free(body[i].alternate);
    }
    afree(body);
}

void code_statement_parse_local(parser *p)
{
    local_declaration local = { .start = p->base + (int)p->token.start };
    local.name = code_program_name(p);
    local.end = p->base + (int)p->token.start;
    code_program_expect(p, "=");
    int end;
    local.text = code_program_capture_statement(p, &local.value_start, &end);
    local.value_start += p->base;
    code_program_expect(p, ";");
    apush(p->scope->locals, local);
}
bool code_statement_lower_local(parser *p, scope *s, local_declaration *local)
{
    if (local->has_resolved) {
        return true;
    }
    if (local->building) {
        code_source_error(p->source, local->start, local->end, "Cyclic local producer dependency");
        return false;
    }
    local->building = true;
    code_call_port expected = { .type = PIN_FLOW, .kind = PIN_DATA };
    if (!code_program_value(
            p,
            s,
            local->text,
            local->value_start,
            &expected,
            &local->resolved
        )) {
        goto done;
    }
    if (local->resolved.kind != CODE_CALL_OUTPUT) {
        code_source_error(
            p->source,
            local->value_start,
            local->value_start + (int)strlen(local->text),
            "A local declaration names a native producer result"
        );
        goto done;
    }
    local->id = graph_declare_local(
        s->native,
        local->resolved.source.node,
        local->resolved.source.pin,
        local->name
    );
    if (!local->id) {
        code_source_error(p->source, local->start, local->end, "Invalid or duplicate local declaration");
        goto done;
    }
    const pin_spec *pin = &local->resolved.source.node
                               ->output_pins[local->resolved.source.pin];
    code_semantic_symbol symbol = { .id = (uint64_t)local->start + 1,
                                    .kind = CODE_SYMBOL_LOCAL,
                                    .name = local->name,
                                    .type = { pin->type, pin->container, pin->type_name },
                                    .start = local->start,
                                    .end = local->start + (int)strlen(local->name),
                                    .scope_start = s->start,
                                    .scope_end = s->end,
                                    .renameable = true,
                                    .graph_id = s->native->graph_id,
                                    .node_id = local->resolved.source.node->id,
                                    .pin_id = pin->id };
    code_intelligence_symbol(p->source, &symbol);
    local->has_resolved = true;
done:
    local->building = false;
    return local->has_resolved;
}
void code_statement_lower_locals(parser *p, scope *s)
{
    for (int i = 0; i < acount(s->locals) && !failed(p); i++) {
        if (!code_statement_lower_local(p, s, &s->locals[i])) {
            break;
        }
    }
}
void code_statement_order_locals(scope *s)
{
    /* Resolving a producer can recursively declare a later local. Preserve
     * the authored declaration order after dependency resolution completes. */
    int ordered = 0;
    for (int i = 0; i < acount(s->locals); i++) {
        for (int j = ordered; j < acount(s->native->locals); j++) {
            if (s->native->locals[j].id == s->locals[i].id) {
                graph_local local = s->native->locals[j];
                memmove(
                    &s->native->locals[ordered + 1],
                    &s->native->locals[ordered],
                    (size_t)(j - ordered) * sizeof(graph_local)
                );
                s->native->locals[ordered++] = local;
                break;
            }
        }
    }
}

static const code_expression_source *expression_record(const code_program *program, const node *native)
{
    for (int i = 0; i < acount(program->expressions); i++) {
        if (program->expressions[i].native->id == native->id) {
            return &program->expressions[i];
        }
    }
    return NULL;
}
static int atom_end(const char *text, int start, const char *atom)
{
    code_lexer source, expected;
    code_lex_init(&source, text + start);
    code_lex_init(&expected, atom);
    int end = start;
    for (;;) {
        code_token pattern = code_lex_next(&expected);
        if (pattern.kind == CODE_TOKEN_END) {
            return end;
        }
        code_token token = code_lex_next(&source);
        size_t length = pattern.end - pattern.start;
        if (pattern.kind == CODE_TOKEN_ERROR || token.kind != pattern.kind ||
            token.end - token.start != length ||
            memcmp(atom + pattern.start, text + start + token.start, length)) {
            return -1;
        }
        end = start + (int)token.end;
    }
}
static char *rewrite_expression(const code_expression_source *record, const char *text, bool inverse)
{
    code_lexer lexer;
    code_lex_init(&lexer, text);
    char *out = NULL;
    int copied = 0;
    bool member = false;
    for (;;) {
        code_token token = code_lex_next(&lexer);
        if (token.kind == CODE_TOKEN_END || token.kind == CODE_TOKEN_ERROR) {
            break;
        }
        bool suffix = member;
        member = token.punctuation == '.';
        if (suffix) {
            continue;
        }
        const code_expression_read *selected = NULL;
        int end = -1;
        for (int i = 0; i < acount(record->reads); i++) {
            const code_expression_read *read = &record->reads[i];
            int matched = atom_end(text, (int)token.start, inverse ? read->text : read->name);
            if (matched > end) {
                selected = read;
                end = matched;
            }
        }
        if (!selected) {
            continue;
        }
        for (int i = copied; i < (int)token.start; i++) {
            apush(out, text[i]);
        }
        const char *replacement = inverse ? selected->name : selected->text;
        for (int i = 0; replacement[i]; i++) {
            apush(out, replacement[i]);
        }
        copied = end;
        lexer.at = (size_t)end;
    }
    for (int i = copied; text[i]; i++) {
        apush(out, text[i]);
    }
    apush(out, 0);
    char *result = xstrdup(out);
    afree(out);
    return result;
}
char *code_statement_display_expression(
    const code_program *program,
    const behavior_asset *asset,
    const node *expression
)
{
    const code_expression_source *record = expression_record(program, expression);
    const char *text = behavior_expression_source(asset, expression);
    return record && text ? rewrite_expression(record, text, false) : NULL;
}
char *code_statement_resolve_expression(
    const code_program *program,
    const node *expression,
    const char *text
)
{
    const code_expression_source *record = expression_record(program, expression);
    return record ? rewrite_expression(record, text, true) : NULL;
}
