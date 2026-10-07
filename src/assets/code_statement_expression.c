#include "assets/code_components.h"
#include "assets/code_program_internal.h"
#include "assets/code_source_context.h"
#include "assets/code_source_intelligence.h"
#include "assets/code_source_internal.h"
#include "assets/code_value.h"
#include "ecs/ecs.h"
#include "graph/model/local.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    char name[64];
    code_call_argument value;
    code_call_port port;
    int start, end;
    bool repeated, contextual_number;
    unsigned original_flags;
} expression_input;

static bool same_text(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}

static bool same_input(const expression_input *a, const expression_input *b)
{
    if (a->value.has_fallback_binding || a->value.has_fallback_default ||
        b->value.has_fallback_binding || b->value.has_fallback_default ||
        a->value.has_type != b->value.has_type ||
        (a->value.has_type && a->value.type != b->value.type) ||
        a->value.kind != b->value.kind || a->value.conversion != b->value.conversion ||
        a->port.type != b->port.type || a->port.container != b->port.container ||
        !same_text(a->port.type_name, b->port.type_name)) {
        return false;
    }
    if (a->value.kind == CODE_CALL_OUTPUT) {
        return a->value.source.node == b->value.source.node &&
               a->value.source.pin == b->value.source.pin;
    }
    if (a->value.kind != CODE_CALL_BINDING) {
        return false;
    }
    const pin_binding *left = &a->value.binding, *right = &b->value.binding;
    return left->kind == right->kind && left->variable_id == right->variable_id &&
           left->local_id == right->local_id &&
           left->event_node_id == right->event_node_id &&
           left->event_pin_id == right->event_pin_id &&
           same_text(left->source_asset, right->source_asset) &&
           same_text(left->component_key, right->component_key) &&
           same_text(left->field_name, right->field_name);
}

static char *slice(const char *text, int start, int end)
{
    char *copy = xmalloc((size_t)(end - start) + 1);
    memcpy(copy, text + start, (size_t)(end - start));
    copy[end - start] = 0;
    return copy;
}

static char *keep_slice(parser *p, const char *text, int start, int end)
{
    char *owned = slice(text, start, end);
    apush(p->program->strings, owned);
    return owned;
}

static int closing(code_token *tokens, int first, char open, char close)
{
    int depth = 0;
    for (int i = first; i < acount(tokens); i++) {
        if (tokens[i].kind != CODE_TOKEN_PUNCTUATION) {
            continue;
        }
        if (tokens[i].punctuation == open) {
            depth++;
        }
        if (tokens[i].punctuation == close && --depth == 0) {
            return i;
        }
    }
    return -1;
}

static int projection_end(code_token *tokens, int last)
{
    while (last + 1 < acount(tokens)) {
        if (tokens[last + 1].punctuation == '.' && last + 2 < acount(tokens) &&
            (tokens[last + 2].kind == CODE_TOKEN_NAME ||
             tokens[last + 2].kind == CODE_TOKEN_STRING)) {
            last += 2;
            continue;
        }
        if (tokens[last + 1].punctuation == '[') {
            int end = closing(tokens, last + 1, '[', ']');
            if (end >= 0) {
                last = end;
                continue;
            }
        }
        break;
    }
    return last;
}

static bool binding_type(parser *p, scope *s, const pin_binding *binding, code_call_port *port)
{
    switch (binding->kind) {
        case PIN_BINDING_DELTA_TIME: port->type = PIN_F32; return true;
        case PIN_BINDING_VARIABLE: {
            const behavior_variable
                *v = behavior_asset_find_variable_by_id(p->asset, binding->variable_id);
            if (!v) {
                return false;
            }
            port->type = v->type;
            port->type_name = v->type_name;
            port->container = v->container;
            return true;
        }
        case PIN_BINDING_LOCAL:
            for (int i = 0; i < acount(s->native->locals); i++) {
                const graph_local *local = &s->native->locals[i];
                if (local->id == binding->local_id) {
                    node *source = graph_node_by_id(s->native, local->source_id);
                    int output, index;
                    const pin_spec *pin = source
                                            ? graph_pin_by_id(source, local->source_pin_id, &output, &index)
                                            : NULL;
                    if (!pin || !output || pin->kind != PIN_DATA) {
                        return false;
                    }
                    port->type = pin->type;
                    port->type_name = pin->type_name;
                    port->container = pin->container;
                    return true;
                }
            }
            return false;
        case PIN_BINDING_EVENT_OUTPUT: {
            node *event = graph_node_by_id(s->native, binding->event_node_id);
            int output, index;
            const pin_spec *pin = event
                                    ? graph_pin_by_id(event, binding->event_pin_id, &output, &index)
                                    : NULL;
            if (!pin || !output || pin->kind != PIN_DATA) {
                return false;
            }
            port->type = pin->type;
            port->type_name = pin->type_name;
            port->container = pin->container;
            return true;
        }
        case PIN_BINDING_COMPONENT:
        case PIN_BINDING_COMPONENT_FIELD:
            for (int i = 0; i < registry_node_count(); i++) {
                const ecs_component_desc *component = registry_node(i)->component;
                if (!component || !component->key || !component->type ||
                    strcmp(component->key, binding->component_key)) {
                    continue;
                }
                if (binding->kind == PIN_BINDING_COMPONENT) {
                    port->type = PIN_STRUCT;
                    port->type_name = component->type->name;
                    return true;
                }
                const type_field
                    *field = pin_type_field_path(component->type, binding->field_name);
                if (!field) {
                    return false;
                }
                port->type = field->type;
                port->type_name = field->type_name;
                return true;
            }
            return false;
        default: return false;
    }
}

bool code_statement_value_type(parser *p, scope *s, const code_call_argument *value, code_call_port *port)
{
    port->kind = PIN_DATA;
    port->type_name = NULL;
    port->container = PIN_CONTAINER_SINGLE;
    if (value->has_type) {
        if (value->type < PIN_BOOL || value->type > PIN_F64) {
            return false;
        }
        port->type = value->type;
        port->type_name = pin_type_name(value->type);
        return true;
    }
    if (value->conversion) {
        const node_desc *conversion = value->conversion;
        if (conversion->output_count != 1 || conversion->outputs[0].kind != PIN_DATA) {
            return false;
        }
        const pin_spec *output = &conversion->outputs[0];
        port->type = output->type;
        port->type_name = output->type_name;
        port->container = output->container;
        return true;
    }
    if (value->kind == CODE_CALL_BINDING) {
        return binding_type(p, s, &value->binding, port);
    }
    if (value->kind == CODE_CALL_OUTPUT) {
        const node *n = value->source.node;
        int output = value->source.pin;
        if (!n || output < 0 || output >= n->output_count ||
            n->output_pins[output].kind != PIN_DATA) {
            return false;
        }
        const pin_spec *pin = &n->output_pins[output];
        port->type = pin->type;
        port->type_name = pin->type_name;
        port->container = pin->container;
    } else {
        port->type = value->literal.type;
        port->type_name = value->literal.type_name;
        port->container = value->literal.container;
    }
    port->kind = PIN_DATA;
    return true;
}

static bool parameter(scope *s, const char *name)
{
    node *entry = graph_node_by_id(s->native, s->native->entry_id);
    for (int i = 0; entry && i < entry->output_count; i++) {
        if (entry->output_pins[i].kind == PIN_DATA &&
            !strcmp(entry->output_pins[i].name, name)) {
            return true;
        }
    }
    return false;
}

static const graph_local *named_local(scope *s, const char *name)
{
    for (int i = 0; i < acount(s->native->locals); i++) {
        if (!strcmp(s->native->locals[i].name, name)) {
            return &s->native->locals[i];
        }
    }
    return NULL;
}

static bool bare_value(parser *p, scope *s, const char *name, code_call_argument *value)
{
    declaration *d = code_program_declaration(s, name);
    if (!d) {
        const graph_local *local = named_local(s, name);
        if (local) {
            value->kind = CODE_CALL_BINDING;
            value->binding.kind = PIN_BINDING_LOCAL;
            value->binding.local_id = local->id;
            return true;
        }
        const behavior_variable *state = behavior_asset_find_variable_by_name(p->asset, name);
        if (!state) {
            return false;
        }
        value->kind = CODE_CALL_BINDING;
        value->binding.kind = PIN_BINDING_VARIABLE;
        value->binding.variable_id = state->id;
        return true;
    }
    node *n = code_program_build(p, s, d);
    if (!n) {
        return false;
    }
    if (d->expression_output.node) {
        value->kind = CODE_CALL_OUTPUT;
        value->source = d->expression_output;
        return true;
    }
    if (d->results) {
        const char *result = NULL;
        for (int i = 0; i < acount(d->results); i++) {
            if (d->results[i].kind != PIN_DATA) {
                continue;
            }
            if (result) {
                return false;
            }
            result = d->results[i].name;
        }
        char error[192];
        if (!result ||
            !code_call_resolve_output(s->native, n, result, &value->source, error, sizeof error)) {
            return false;
        }
        value->kind = CODE_CALL_OUTPUT;
        return true;
    }
    int result = -1;
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].kind != PIN_DATA || n->output_pins[i].field_source) {
            continue;
        }
        if (result >= 0) {
            return false;
        }
        result = i;
    }
    if (result < 0) {
        return false;
    }
    value->kind = CODE_CALL_OUTPUT;
    value->source = (code_call_output){ n, result };
    return true;
}

static bool input_value(parser *p, scope *s, const char *text, int offset, expression_input *input)
{
    input->port = (code_call_port){ .kind = PIN_DATA, .type = PIN_F32 };
    int component = code_components_value(
        p,
        s,
        text,
        offset,
        &input->port,
        &input->value
    );
    if (component >= 0) {
        return component &&
               code_statement_value_type(p, s, &input->value, &input->port);
    }
    int method = code_statement_method(p, s, text, offset, &input->value);
    if (method >= 0) {
        return method && code_statement_value_type(p, s, &input->value, &input->port);
    }
    code_lexer lexer;
    code_lex_init(&lexer, text);
    code_token first = code_lex_next(&lexer), next = code_lex_next(&lexer);
    bool valid;
    char *root = first.kind == CODE_TOKEN_NAME
                   ? slice(text, (int)first.start, (int)first.end)
                   : NULL;
    if (root && parameter(s, root) && !code_program_declaration(s, root)) {
        node *entry = graph_node_by_id(s->native, s->native->entry_id);
        char error[192];
        valid = code_call_resolve_output(s->native, entry, text, &input->value.source, error, sizeof error);
        input->value.kind = CODE_CALL_OUTPUT;
        if (!valid) {
            code_source_error(p->source, offset, offset + (int)strlen(text), error);
        }
    } else {
        valid = first.kind == CODE_TOKEN_NAME && next.kind == CODE_TOKEN_END
                  ? bare_value(p, s, text, &input->value)
                  : code_program_value(p, s, text, offset, &input->port, &input->value);
    }
    free(root);
    if (!valid) {
        return false;
    }
    return code_statement_value_type(p, s, &input->value, &input->port);
}

static int method_suffix(const char *text, code_token *tokens, int end)
{
    while (end >= 1 && tokens[end].kind == CODE_TOKEN_NAME &&
           tokens[end - 1].punctuation == '.') {
        end -= 2;
    }
    if (end < 4 || tokens[end].punctuation != ')' ||
        tokens[end - 1].punctuation != '(' || tokens[end - 2].kind != CODE_TOKEN_NAME ||
        tokens[end - 2].end - tokens[end - 2].start != 4 ||
        strncmp(text + tokens[end - 2].start, "norm", 4) ||
        tokens[end - 3].punctuation != '.') {
        return -1;
    }
    return end;
}

int code_statement_method(parser *p, scope *s, const char *text, int offset, code_call_argument *out)
{
    if (!code_program_atomic_value(text)) {
        return -1;
    }
    code_lexer lexer;
    code_lex_init(&lexer, text);
    code_token *tokens = NULL;
    for (code_token token = code_lex_next(&lexer); token.kind != CODE_TOKEN_END;
         token = code_lex_next(&lexer)) {
        apush(tokens, token);
    }
    int end = method_suffix(text, tokens, acount(tokens) - 1);
    if (end < 0) {
        afree(tokens);
        return -1;
    }
    int receiver_end = (int)tokens[end - 3].start;
    char *receiver = slice(text, 0, receiver_end);
    expression_input input = { 0 };
    code_call_port inferred = { .kind = PIN_DATA, .type = PIN_FLOW };
    bool valid;
    if (code_statement_math_call(p, receiver)) {
        valid = code_statement_expression(
                    p,
                    s,
                    receiver,
                    offset,
                    &inferred,
                    &input.value
                ) &&
                code_statement_value_type(p, s, &input.value, &input.port);
    } else {
        valid = code_program_value(p, s, receiver, offset, &inferred, &input.value) &&
                code_statement_value_type(p, s, &input.value, &input.port);
    }
    free(receiver);
    if (!valid) {
        afree(tokens);
        return 0;
    }
    pin_spec receiver_type = { .type = input.port.type,
                               .type_name = input.port.type_name,
                               .container = input.port.container };
    pin_spec vector_type = { .type = PIN_STRUCT, .type_name = "CF_V2" };
    if (!pin_compatible(&receiver_type, &vector_type)) {
        code_source_error(
            p->source,
            offset + (int)tokens[end - 2].start,
            offset + (int)tokens[end].end,
            "norm() requires a vec2 receiver"
        );
        afree(tokens);
        return 0;
    }
    code_program_complete_vector_method(
        p,
        offset + (int)tokens[end - 2].start,
        offset + (int)tokens[end - 2].end
    );
    code_program_complete_value(p, s, offset, offset + receiver_end, &input.port);
    code_program_resolved_use(p, s, text, offset, &input.value, CODE_USE_READ);
    input.value.name = "Value";
    code_call_spec call = { .name = "Normalize Vector 2",
                            .native = true,
                            .arguments = &input.value,
                            .argument_count = 1 };
    char error[256];
    node *native = code_call_build(p->asset, s->native, &call, error, sizeof error);
    if (!native) {
        code_source_error(p->source, offset, offset + (int)strlen(text), error);
        afree(tokens);
        return 0;
    }
    out->kind = CODE_CALL_OUTPUT;
    out->source = (code_call_output){ native, 0 };
    if (end + 1 < acount(tokens)) {
        const char *member = text + tokens[end + 2].start;
        size_t length = strlen(member) + sizeof "Result.";
        char *path = xmalloc(length);
        snprintf(path, length, "Result.%s", member);
        valid = code_call_resolve_output(s->native, native, path, &out->source, error, sizeof error);
        free(path);
        if (!valid) {
            code_source_error(
                p->source,
                offset + (int)tokens[end + 2].start,
                offset + (int)strlen(text),
                error
            );
        }
    }
    afree(tokens);
    return valid;
}

static int method_end(const char *text, code_token *tokens, int last)
{
    while (last >= 1 && last + 2 < acount(tokens) &&
           tokens[last - 1].punctuation == '.' && tokens[last].kind == CODE_TOKEN_NAME &&
           tokens[last].end - tokens[last].start == 4 &&
           !strncmp(text + tokens[last].start, "norm", 4) &&
           tokens[last + 1].punctuation == '(' && tokens[last + 2].punctuation == ')') {
        last = projection_end(tokens, last + 2);
    }
    return last;
}

/* Only native calls are lifted. Positional math functions remain in the
 * expression parser so their ordinary arithmetic overloads stay intact. */
static bool math_alias(const char *name)
{
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *desc = registry_node(i);
        for (int j = 0;; j++) {
            const char *alias = behavior_expression_alias(desc, j);
            if (!alias) {
                break;
            }
            if (!strcmp(alias, name)) {
                return true;
            }
        }
    }
    return false;
}
bool code_statement_math_call(parser *p, const char *text)
{
    code_lexer lexer;
    code_lex_init(&lexer, text);
    code_token root = code_lex_next(&lexer);
    if (root.kind != CODE_TOKEN_NAME || code_lex_next(&lexer).punctuation != '(') {
        return false;
    }
    code_token argument = code_lex_next(&lexer);
    if ((argument.kind == CODE_TOKEN_NAME || argument.kind == CODE_TOKEN_STRING) &&
        code_lex_next(&lexer).punctuation == ':') {
        return false;
    }
    char *name = slice(text, (int)root.start, (int)root.end);
    char error[192];
    bool math = math_alias(name) &&
                !code_source_context_graph(
                    &p->source->context,
                    p->asset,
                    NULL,
                    name,
                    GRAPH_FUNCTION,
                    NULL,
                    error,
                    sizeof error
                ) &&
                !code_source_context_graph(
                    &p->source->context,
                    p->asset,
                    NULL,
                    name,
                    GRAPH_MACRO,
                    NULL,
                    error,
                    sizeof error
                );
    free(name);
    return math;
}

static int native_call_end(parser *p, scope *s, const char *text, code_token *tokens, int first)
{
    (void)s;
    int at = first + 1;
    char *name = slice(text, (int)tokens[first].start, (int)tokens[first].end);
    char error[192];
    if (tokens[first].kind == CODE_TOKEN_STRING) {
        av_doc *doc = code_value_read(name, error, sizeof error);
        const char *decoded = doc ? av_get_str(av_doc_root(doc)) : NULL;
        if (decoded) {
            free(name);
            name = xstrdup(decoded);
        }
        av_doc_free(doc);
    }
    bool engine = !strcmp(name, "engine");
    bool qualified = !engine && at + 1 < acount(tokens) &&
                     tokens[at].punctuation == '.' &&
                     (tokens[at + 1].kind == CODE_TOKEN_NAME ||
                      tokens[at + 1].kind == CODE_TOKEN_STRING);
    if (qualified) {
        at += 2;
    }
    bool authored = code_source_context_graph(
                        &p->source->context,
                        p->asset,
                        NULL,
                        name,
                        GRAPH_FUNCTION,
                        NULL,
                        error,
                        sizeof error
                    ) ||
                    code_source_context_graph(
                        &p->source->context,
                        p->asset,
                        NULL,
                        name,
                        GRAPH_MACRO,
                        NULL,
                        error,
                        sizeof error
                    );
    bool builtin = math_alias(name);
    free(name);
    if (engine && at < acount(tokens) && tokens[at].punctuation == '[') {
        at = closing(tokens, at, '[', ']') + 1;
        if (at <= 0) {
            return -1;
        }
    }
    bool referenced = at < acount(tokens) && tokens[at].punctuation == '<';
    if (referenced) {
        at = closing(tokens, at, '<', '>') + 1;
        if (at <= 0) {
            return -1;
        }
    }
    if (at >= acount(tokens) || tokens[at].punctuation != '(') {
        return -1;
    }
    int end = closing(tokens, at, '(', ')');
    if (end < 0) {
        return -1;
    }
    bool named = at + 2 < end && tokens[at + 1].kind == CODE_TOKEN_NAME &&
                 (tokens[at + 2].punctuation == '=' ||
                  tokens[at + 2].punctuation == ':');
    if (!engine && !qualified && !referenced && !named && !authored && builtin) {
        return -1;
    }
    return projection_end(tokens, end);
}

/* Keep aggregate member reads in the expression graph. A separately declared
 * scalar value remains a scalar capture, so authors can choose that boundary. */
static int aggregate_prefix(
    parser *p,
    scope *s,
    const char *text,
    int offset,
    code_token *tokens,
    int first,
    int last
)
{
    int root = first;
    if (tokens[first].punctuation == '&') {
        if (first + 3 >= last || tokens[first + 2].punctuation != '.') {
            return last;
        }
        root = first + 3;
        char *atom = slice(text, (int)tokens[first].start, (int)tokens[root].end);
        expression_input input = { 0 };
        bool aggregate = input_value(p, s, atom, offset + (int)tokens[first].start, &input) &&
                         input.port.type == PIN_STRUCT &&
                         input.port.container == PIN_CONTAINER_SINGLE;
        free(atom);
        return aggregate ? root : last;
    }
    char *name = slice(text, (int)tokens[first].start, (int)tokens[first].end);
    bool call = first + 1 < acount(tokens) &&
                (tokens[first + 1].punctuation == '(' ||
                 tokens[first + 1].punctuation == '<');
    declaration *d = tokens[first].kind == CODE_TOKEN_NAME && !call
                       ? code_program_declaration(s, name)
                       : NULL;
    const pin_spec *output = NULL;
    pin_spec state_output = { 0 };
    const behavior_variable *state = behavior_asset_find_variable_by_name(p->asset, name);
    bool inferred_call = false;
    if (!strcmp(name, "entry") && first + 2 < last &&
        tokens[first + 1].punctuation == '.') {
        char *parameter_name = slice(
            text,
            (int)tokens[first + 2].start,
            (int)tokens[first + 2].end
        );
        node *entry = graph_node_by_id(s->native, s->native->entry_id);
        for (int i = 0; entry && i < entry->output_count; i++) {
            if (entry->output_pins[i].kind == PIN_DATA &&
                !entry->output_pins[i].field_source &&
                !strcmp(entry->output_pins[i].name, parameter_name)) {
                output = &entry->output_pins[i];
                root = first + 2;
                break;
            }
        }
        free(parameter_name);
    } else if (d) {
        node *n = code_program_build(p, s, d);
        if (n && d->results) {
            for (int i = 0; i < acount(d->results); i++) {
                if (d->results[i].kind != PIN_DATA) {
                    continue;
                }
                if (output) {
                    output = NULL;
                    break;
                }
                char error[192];
                int index = code_call_port_index(n, true, d->results[i].name, error, sizeof error);
                if (index >= 0) {
                    output = &n->output_pins[index];
                }
            }
        } else if (n) {
            for (int i = 0; i < n->output_count; i++) {
                if (n->output_pins[i].kind != PIN_DATA || n->output_pins[i].field_source) {
                    continue;
                }
                if (output) {
                    output = NULL;
                    break;
                }
                output = &n->output_pins[i];
            }
        }
    } else if (parameter(s, name)) {
        node *entry = graph_node_by_id(s->native, s->native->entry_id);
        for (int i = 0; entry && i < entry->output_count; i++) {
            if (entry->output_pins[i].kind == PIN_DATA &&
                !strcmp(entry->output_pins[i].name, name)) {
                output = &entry->output_pins[i];
                break;
            }
        }
    } else if (state) {
        state_output = (pin_spec){ .name = state->name,
                                   .type = state->type,
                                   .type_name = state->type_name,
                                   .container = state->container,
                                   .kind = PIN_DATA };
        output = &state_output;
    } else {
        char error[192];
        bool authored = code_source_context_graph(
                            &p->source->context,
                            p->asset,
                            NULL,
                            name,
                            GRAPH_FUNCTION,
                            NULL,
                            error,
                            sizeof error
                        ) ||
                        code_source_context_graph(
                            &p->source->context,
                            p->asset,
                            NULL,
                            name,
                            GRAPH_MACRO,
                            NULL,
                            error,
                            sizeof error
                        );
        const node_desc *desc = authored
                                  ? NULL
                                  : code_call_descriptor(name, error, sizeof error);
        if (desc) {
            inferred_call = true;
            int open = first + 1;
            if (open < last && tokens[open].punctuation == '<') {
                open = closing(tokens, open, '<', '>') + 1;
            }
            if (open > first && open < last && tokens[open].punctuation == '(') {
                root = closing(tokens, open, '(', ')');
                for (int i = 0; root >= 0 && i < desc->output_count; i++) {
                    if (desc->outputs[i].kind != PIN_DATA ||
                        desc->outputs[i].field_source) {
                        continue;
                    }
                    if (output) {
                        output = NULL;
                        break;
                    }
                    output = &desc->outputs[i];
                }
            }
        }
    }
    free(name);
    if (!output || root < first || root >= last ||
        (!inferred_call && output->type != PIN_STRUCT && output->type != PIN_FLOW) ||
        output->container != PIN_CONTAINER_SINGLE) {
        return last;
    }
    if (root + 2 <= last && tokens[root + 1].punctuation == '.') {
        char *member = slice(
            text,
            (int)tokens[root + 2].start,
            (int)tokens[root + 2].end
        );
        bool ambiguous = false;
        bool reflected_member = d && d->expression_output.node &&
                                output->type == PIN_STRUCT && output->type_name &&
                                code_component_field(
                                    registry_type_by_name(output->type_name),
                                    member,
                                    &ambiguous
                                );
        if (!reflected_member && !strcmp(member, output->name)) {
            root += 2;
        }
        free(member);
    }
    return root < last ? root : last;
}

static bool identifier_used(const char *text, const code_token *tokens, const char *name)
{
    size_t length = strlen(name);
    for (int i = 0; i < acount(tokens); i++) {
        if (tokens[i].kind == CODE_TOKEN_NAME &&
            tokens[i].end - tokens[i].start == length &&
            !strncmp(text + tokens[i].start, name, length)) {
            return true;
        }
    }
    return false;
}

static void append(char **result, int **origins, const char *text, int start, int end)
{
    for (int i = start; i < end; i++) {
        apush(*result, text[i]);
        apush(*origins, i);
    }
}

static int append_members(
    char **source,
    int **origins,
    const char *text,
    const code_token *tokens,
    int last,
    code_call_port port
)
{
    while (last + 2 < acount(tokens) && tokens[last + 1].punctuation == '.' &&
           tokens[last + 2].kind == CODE_TOKEN_NAME &&
           port.type == PIN_STRUCT && port.container == PIN_CONTAINER_SINGLE) {
        code_token token = tokens[last + 2];
        char *name = slice(text, (int)token.start, (int)token.end);
        bool ambiguous;
        const type_desc *type = port.type_name ? registry_type_by_name(port.type_name)
                                               : NULL;
        const type_field *field = code_component_field(type, name, &ambiguous);
        free(name);
        if (!field) {
            break;
        }
        apush(*source, '.');
        apush(*origins, (int)tokens[last + 1].start);
        for (int i = 0; field->name[i]; i++) {
            apush(*source, field->name[i]);
            apush(*origins, (int)token.start);
        }
        port.type = field->type;
        port.type_name = field->type_name;
        last += 2;
    }
    return last;
}

static void compiler_error(
    parser *p,
    scope *s,
    const char *text,
    int offset,
    const int *origins,
    const behavior_expression_error *error
)
{
    int length = (int)strlen(text);
    int start = error->offset >= 0 && error->offset < acount(origins)
                  ? origins[error->offset]
                  : length;
    int end = start;
    if (error->offset >= 0 && error->offset < acount(origins) && error->length > 0) {
        int last = error->offset + error->length - 1;
        if (last >= acount(origins)) {
            last = acount(origins) - 1;
        }
        end = origins[last] + 1;
    }
    code_program_complete_value(p, s, offset + start, offset + end, NULL);
    code_source_error(p->source, offset + start, offset + end, error->message);
}

/* Contextual literals choose float operands, including independent wildcard
 * groups on the generated operation. Typed reads and authored casts retain
 * their own widths. */
static void contextual_operations(
    parser *p,
    node *call,
    const expression_input *inputs,
    const code_call_port *expected
)
{
    if (!expected || expected->type != PIN_F32 ||
        expected->container != PIN_CONTAINER_SINGLE || !acount(inputs)) {
        return;
    }
    for (int i = 0; i < acount(inputs); i++) {
        if (!inputs[i].contextual_number) {
            return;
        }
    }
    graph *body = behavior_asset_find_graph(p->asset, call->reference_id);
    for (int i = 0; i < acount(body->nodes); i++) {
        node *operation = body->nodes[i];
        if (operation->desc == &NODE_Return || operation->output_count != 1 ||
            operation->output_pins[0].type != PIN_F32 ||
            operation->output_pins[0].container != PIN_CONTAINER_SINGLE) {
            continue;
        }
        for (int pin = 0; pin < operation->input_count; pin++) {
            pin_spec *input = &operation->input_pins[pin];
            node *driver;
            int output;
            if (input->type != PIN_F64 || input->container != PIN_CONTAINER_SINGLE ||
                !graph_input_source(body, operation, pin, &driver, &output) ||
                driver->output_pins[output].type != PIN_F32 ||
                driver->output_pins[output].container != PIN_CONTAINER_SINGLE) {
                continue;
            }
            int result = graph_set_pin_type(body, operation, false, pin, PIN_F32, "float");
            assert(result == 0);
            (void)result;
        }
    }
}

/* The math frontend consumes expressions without code comments. Keep byte
 * positions stable so its diagnostics still map to the authored source. */
static void strip_comments(char *text)
{
    code_lexer lexer;
    code_lex_init(&lexer, text);
    size_t end = 0;
    for (;;) {
        code_token token = code_lex_next(&lexer);
        for (size_t i = end; i < token.start; i++) {
            if (text[i] != '\n' && text[i] != '\r') {
                text[i] = ' ';
            }
        }
        if (token.kind == CODE_TOKEN_END) {
            return;
        }
        end = token.end;
    }
}

bool code_statement_expression(
    parser *p,
    scope *s,
    const char *text,
    int offset,
    code_call_port *expected,
    code_call_argument *out
)
{
    int assignment = code_components_assignment(p, s, text, offset, out);
    if (assignment >= 0) {
        return assignment != 0;
    }
    code_program_complete_value(p, s, offset, offset + (int)strlen(text), expected);
    code_lexer lexer;
    code_lex_init(&lexer, text);
    code_token *tokens = NULL;
    for (code_token t = code_lex_next(&lexer); t.kind != CODE_TOKEN_END;
         t = code_lex_next(&lexer)) {
        apush(tokens, t);
        if (t.kind == CODE_TOKEN_ERROR) {
            code_source_error(p->source, offset + (int)t.start, offset + (int)t.end, "Invalid expression token");
            afree(tokens);
            return false;
        }
    }
    char *source = NULL;
    int *origins = NULL;
    expression_input *inputs = NULL;
    int copied = 0, suffix = 0;
    bool valid = false;
    for (int i = 0; i < acount(tokens); i++) {
        if (i && tokens[i - 1].punctuation == '.') {
            continue;
        }
        int last = -1;
        bool invocation = false;
        bool method_invocation = false;
        int preceding_ampersands = 0;
        for (int j = i - 1; j >= 0 && tokens[j].punctuation == '&'; j--) {
            preceding_ampersands++;
        }
        if (tokens[i].punctuation == '&' && !(preceding_ampersands % 2) &&
            i + 1 < acount(tokens) && tokens[i + 1].kind == CODE_TOKEN_NAME) {
            last = projection_end(tokens, i + 1);
            if (last + 1 < acount(tokens) && tokens[last + 1].punctuation == '(') {
                last = closing(tokens, last + 1, '(', ')');
                if (last >= 0) {
                    int projected = projection_end(tokens, last);
                    int method = method_end(text, tokens, projected);
                    if (method > projected) {
                        last = method;
                        method_invocation = true;
                    }
                }
            }
        } else if (tokens[i].kind == CODE_TOKEN_NUMBER && expected && expected->type == PIN_F32 && expected->container == PIN_CONTAINER_SINGLE) {
            last = i;
        } else if (tokens[i].kind == CODE_TOKEN_NAME || tokens[i].kind == CODE_TOKEN_STRING) {
            last = native_call_end(p, s, text, tokens, i);
            invocation = last >= i;
            if (last < 0 && i + 1 < acount(tokens) && tokens[i + 1].punctuation == '(') {
                int end = closing(tokens, i + 1, '(', ')');
                if (end >= 0) {
                    int projected = projection_end(tokens, end);
                    int method = method_end(text, tokens, projected);
                    if (method > projected) {
                        last = method;
                        invocation = true;
                        method_invocation = true;
                    }
                }
            }
            if (last < 0 && tokens[i].kind == CODE_TOKEN_NAME) {
                char *name = slice(text, (int)tokens[i].start, (int)tokens[i].end);
                bool component_alias = false;
                for (int j = 0; j < acount(s->components); j++) {
                    component_alias |= !strcmp(s->components[j].name, name);
                }
                if (!strcmp(name, "self") || component_alias ||
                    code_program_declaration(s, name) || parameter(s, name) ||
                    named_local(s, name) ||
                    behavior_asset_find_variable_by_name(p->asset, name)) {
                    last = projection_end(tokens, i);
                }
                free(name);
            }
        } else if (tokens[i].punctuation == '(') {
            int end = closing(tokens, i, '(', ')');
            if (end >= 0) {
                int projected = projection_end(tokens, end);
                int method = method_end(text, tokens, projected);
                if (method > projected) {
                    last = method;
                    invocation = true;
                    method_invocation = true;
                }
            }
        }
        if (last < i) {
            continue;
        }
        int extended = method_end(text, tokens, last);
        bool method = method_invocation || extended > last ||
                      method_suffix(text, tokens, extended) >= 0;
        last = extended;
        if (!method) {
            last = aggregate_prefix(p, s, text, offset, tokens, i, last);
        }
        expression_input input = { .start = (int)tokens[i].start,
                                   .end = (int)tokens[last].end };
        bool contextual_number = tokens[i].kind == CODE_TOKEN_NUMBER;
        input.contextual_number = contextual_number;
        if (contextual_number && strchr("+-", text[input.start])) {
            input.start++;
        }
        char *atom = slice(text, input.start, input.end);
        int previous_nodes = acount(s->native->nodes);
        bool resolved;
        if (contextual_number) {
            input.port = *expected;
            resolved = code_program_value(
                           p,
                           s,
                           atom,
                           offset + input.start,
                           &input.port,
                           &input.value
                       ) &&
                       code_statement_value_type(p, s, &input.value, &input.port);
        } else {
            resolved = input_value(p, s, atom, offset + input.start, &input);
        }
        if (resolved) {
            code_program_complete_value(
                p,
                s,
                offset + input.start,
                offset + input.end,
                &input.port
            );
            unsigned role = CODE_USE_READ;
            if (last + 1 < acount(tokens) && tokens[last + 1].punctuation == '=' &&
                (last + 2 >= acount(tokens) || tokens[last + 2].punctuation != '=')) {
                role = CODE_USE_WRITE;
            } else if (
                last + 2 < acount(tokens) && tokens[last + 2].punctuation == '=' &&
                strchr("+-*/%", tokens[last + 1].punctuation) &&
                tokens[last + 1].kind == CODE_TOKEN_PUNCTUATION
            ) {
                role |= CODE_USE_WRITE;
            }
            code_program_resolved_use(p, s, atom, offset + input.start, &input.value, role);
        }
        free(atom);
        if (!resolved) {
            if (!acount(p->source->diagnostics)) {
                code_source_error(
                    p->source,
                    offset + input.start,
                    offset + input.end,
                    "Expression value requires one typed data result"
                );
            }
            goto done;
        }
        if (invocation && input.value.kind == CODE_CALL_OUTPUT) {
            const node *n = input.value.source.node;
            bool converted_reference = false;
            for (int j = 0; input.value.conversion && j < previous_nodes; j++) {
                converted_reference |= s->native->nodes[j] == n;
            }
            for (int pin = 0; pin < n->input_count; pin++) {
                if (!converted_reference && n->input_pins[pin].kind == PIN_EXEC) {
                    code_source_error(
                        p->source,
                        offset + input.start,
                        offset + input.end,
                        "A nested value call must be pure"
                    );
                    goto done;
                }
            }
        }
        for (int j = 0; j < acount(inputs); j++) {
            if (!same_input(&inputs[j], &input)) {
                continue;
            }
            strcpy(input.name, inputs[j].name);
            input.repeated = true;
            break;
        }
        if (!input.repeated) {
            do {
                snprintf(input.name, sizeof input.name, "_code_input_%d", suffix++);
            } while (identifier_used(text, tokens, input.name));
        }
        append(&source, &origins, text, copied, input.start);
        for (int j = 0; input.name[j]; j++) {
            apush(source, input.name[j]);
            apush(origins, input.start);
        }
        apush(inputs, input);
        last = append_members(&source, &origins, text, tokens, last, input.port);
        copied = (int)tokens[last].end;
        i = last;
    }
    bool known_double = false;
    for (int i = 0; i < acount(inputs); i++) {
        known_double |= !inputs[i].contextual_number &&
                        inputs[i].port.type == PIN_F64 &&
                        inputs[i].port.container == PIN_CONTAINER_SINGLE;
    }
    if (known_double) {
        for (int i = 0; i < acount(inputs); i++) {
            expression_input *input = &inputs[i];
            if (!input->contextual_number) {
                continue;
            }
            char *atom = slice(text, input->start, input->end);
            input->port = (code_call_port){ .kind = PIN_DATA,
                                            .type = PIN_F64,
                                            .type_name = "double" };
            input->value = (code_call_argument){ 0 };
            bool
                resolved = code_program_value(
                               p,
                               s,
                               atom,
                               offset + input->start,
                               &input->port,
                               &input->value
                           ) &&
                           code_statement_value_type(p, s, &input->value, &input->port);
            free(atom);
            if (!resolved) {
                goto done;
            }
        }
    }
    append(&source, &origins, text, copied, (int)strlen(text));
    apush(source, '\0');
    strip_comments(source);
    behavior_expression_error error;
    node *call = behavior_expression_add_mode(p->asset, s->native, source, p->expression_mode, &error);
    if (!call) {
        compiler_error(p, s, text, offset, origins, &error);
        goto done;
    }
    for (int i = 0; i < acount(inputs); i++) {
        expression_input *input = &inputs[i];
        if (input->repeated) {
            continue;
        }
        int pin = -1;
        for (int j = 0; j < call->input_count; j++) {
            if (!strcmp(call->input_pins[j].name, input->name)) {
                pin = j;
                break;
            }
        }
        if (pin < 0 ||
            graph_set_pin_type(
                s->native,
                call,
                false,
                pin,
                input->port.type,
                input->port.type_name
            ) ||
            graph_set_pin_container(s->native, call, false, pin, input->port.container)) {
            code_source_error(p->source, offset + input->start, offset + input->end, "Expression input has an unsupported type");
            goto done;
        }
        if (input->value.has_type || input->value.conversion) {
            input->original_flags = call->input_pins[pin].flags;
            call->input_pins[pin].flags &= ~PIN_FLAG_WILDCARD;
        }
        int result = 0;
        switch (input->value.kind) {
            case CODE_CALL_OUTPUT: {
                pin_spec *source_pin = &input->value.source.node
                                            ->output_pins[input->value.source.pin];
                unsigned source_flags = source_pin->flags;
                if (source_pin->type != PIN_FLOW &&
                    call->input_pins[pin].type != PIN_FLOW &&
                    pin_compatible(source_pin, &call->input_pins[pin])) {
                    source_pin->flags &= ~PIN_FLAG_WILDCARD;
                }
                result = graph_connect_data_conversion(
                    s->native,
                    input->value.source.node,
                    input->value.source.pin,
                    call,
                    pin,
                    input->value.conversion
                );
                source_pin->flags = source_flags;
                break;
            }
            case CODE_CALL_BINDING:
                if (p->program->system &&
                    input->value.binding.kind == PIN_BINDING_VARIABLE) {
                    node *driver = behavior_asset_add_get_node_to_graph(
                        p->asset,
                        s->native,
                        input->value.binding.variable_id
                    );
                    result = graph_connect_data_conversion(
                        s->native,
                        driver,
                        0,
                        call,
                        pin,
                        input->value.conversion
                    );
                    break;
                }
                if (input->value.binding.kind == PIN_BINDING_LOCAL) {
                    if (!input->value.conversion) {
                        result = code_call_use_local(
                            s->native,
                            input->value.binding.local_id,
                            call,
                            pin
                        );
                        break;
                    }
                    const graph_local *local = graph_local_by_id(
                        s->native,
                        input->value.binding.local_id
                    );
                    node *driver = local ? graph_node_by_id(s->native, local->source_id)
                                         : NULL;
                    int output = 0, driver_pin = -1;
                    if (!driver ||
                        !graph_pin_by_id(driver, local->source_pin_id, &output, &driver_pin) ||
                        !output) {
                        result = -1;
                        break;
                    }
                    result = graph_connect_data_conversion(
                        s->native,
                        driver,
                        driver_pin,
                        call,
                        pin,
                        input->value.conversion
                    );
                    if (result) {
                        break;
                    }
                }
                result = graph_set_pin_binding(
                    s->native,
                    call,
                    false,
                    pin,
                    &input->value.binding
                );
                break;
            case CODE_CALL_LITERAL:
                result = graph_set_pin_default(
                    s->native,
                    call,
                    false,
                    pin,
                    &input->value.literal
                );
                break;
        }
        if (!result) {
            result = code_call_apply_fallback(s->native, call, pin, &input->value);
        }
        if (result) {
            code_source_error(p->source, offset + input->start, offset + input->end, "Expression input could not be connected");
            goto done;
        }
    }
    for (int pin = 0; pin < call->input_count; pin++) {
        node *driver;
        int driver_pin;
        bool literal_input = false;
        for (int i = 0; i < acount(inputs); i++) {
            literal_input |= inputs[i].value.kind == CODE_CALL_LITERAL &&
                             !strcmp(inputs[i].name, call->input_pins[pin].name);
        }
        if (call->input_pins[pin].kind == PIN_DATA &&
            !call->input_pins[pin].binding && !literal_input &&
            !graph_input_source(s->native, call, pin, &driver, &driver_pin)) {
            char message[192];
            snprintf(
                message,
                sizeof message,
                "Unresolved source name '%s'",
                call->input_pins[pin].name
            );
            int start = 0, end = (int)strlen(text);
            for (int i = 0; i < acount(tokens); i++) {
                size_t length = tokens[i].end - tokens[i].start;
                if (tokens[i].kind == CODE_TOKEN_NAME &&
                    length == strlen(call->input_pins[pin].name) &&
                    !strncmp(text + tokens[i].start, call->input_pins[pin].name, length)) {
                    start = (int)tokens[i].start;
                    end = (int)tokens[i].end;
                    break;
                }
            }
            const pin_spec *native = &call->input_pins[pin];
            code_call_port required = { .type = native->type,
                                        .type_name = native->type_name,
                                        .container = native->container,
                                        .kind = native->kind,
                                        .flags = native->flags };
            code_program_complete_value(p, s, offset + start, offset + end, &required);
            code_source_error(p->source, offset + start, offset + end, message);
            goto done;
        }
    }
    for (int i = 0; i < acount(inputs); i++) {
        const expression_input *input = &inputs[i];
        if (input->repeated || (!input->value.has_type && !input->value.conversion)) {
            continue;
        }
        for (int j = 0; j < call->input_count; j++) {
            if (!strcmp(call->input_pins[j].name, input->name)) {
                call->input_pins[j].flags = input->original_flags;
                break;
            }
        }
    }
    bool explicit_number = false;
    for (int i = 0; i < acount(inputs); i++) {
        explicit_number |= !inputs[i].contextual_number &&
                           (inputs[i].value.has_type || inputs[i].port.type == PIN_F64);
    }
    if (acount(inputs) == 1 && inputs[0].value.has_type &&
        inputs[0].port.container == PIN_CONTAINER_SINGLE &&
        !strcmp(source, inputs[0].name)) {
        for (int i = 0; i < call->output_count; i++) {
            if (call->output_pins[i].kind == PIN_DATA &&
                !strcmp(call->output_pins[i].name, "Result") &&
                graph_set_pin_type(
                    s->native,
                    call,
                    true,
                    i,
                    inputs[0].port.type,
                    inputs[0].port.type_name
                )) {
                code_source_error(p->source, offset, offset + (int)strlen(text), "Cast result type could not be configured");
                goto done;
            }
        }
    }
    if (expected && expected->type >= PIN_I8 && expected->type <= PIN_F64 &&
        expected->container == PIN_CONTAINER_SINGLE && !explicit_number &&
        p->expression_mode != GRAPH_EXPRESSION_BRANCH) {
        for (int i = 0; i < call->output_count; i++) {
            pin_spec *output = &call->output_pins[i];
            if (output->kind != PIN_DATA || strcmp(output->name, "Result") ||
                output->type < PIN_I8 || output->type > PIN_F64 ||
                output->container != PIN_CONTAINER_SINGLE) {
                continue;
            }
            if (graph_set_pin_type(
                    s->native,
                    call,
                    true,
                    i,
                    expected->type,
                    expected->type_name
                )) {
                code_source_error(p->source, offset, offset + (int)strlen(text), "Expression result type could not be configured");
                goto done;
            }
        }
    }
    if (behavior_expression_infer_assignment_result(p->asset, call, &error) ||
        behavior_expression_refresh_mode(p->asset, call, p->expression_mode, &error)) {
        compiler_error(p, s, text, offset, origins, &error);
        goto done;
    }
    contextual_operations(p, call, inputs, expected);
    for (int i = 0; i < acount(inputs); i++) {
        const expression_input *input = &inputs[i];
        if (input->repeated) {
            continue;
        }
        char *label = slice(text, input->start, input->end);
        char display[80], base[64];
        code_source_expression_capture_name(label, base, sizeof base);
        snprintf(display, sizeof display, "%s", base);
        int ordinal = 1;
        for (int previous = 0; previous < i; previous++) {
            if (inputs[previous].repeated) {
                continue;
            }
            for (int pin = 0; pin < call->input_count; pin++) {
                if (!strcmp(call->input_pins[pin].name, inputs[previous].name) &&
                    call->input_pins[pin].display_name &&
                    !strcmp(display, call->input_pins[pin].display_name)) {
                    snprintf(display, sizeof display, "%s_%d", base, ++ordinal);
                    previous = -1;
                    break;
                }
            }
        }
        if (input->value.kind == CODE_CALL_OUTPUT) {
            const node *source_node = input->value.source.node;
            const pin_spec *source_pin = &source_node
                                              ->output_pins[input->value.source.pin];
            if (source_node->desc == &NODE_Event && source_node->reference_name &&
                !strcmp(source_node->reference_name, "Tick") &&
                !strcmp(source_pin->name, "DeltaSeconds")) {
                snprintf(display, sizeof display, "event.dt");
            }
        }
        for (int j = 0; j < call->input_count; j++) {
            if (!strcmp(call->input_pins[j].name, input->name)) {
                graph_set_pin_display_name(s->native, call, false, j, display);
                break;
            }
        }
        free(label);
    }
    int result = -1;
    for (int i = 0; i < call->output_count; i++) {
        if (call->output_pins[i].kind == PIN_DATA &&
            !strcmp(call->output_pins[i].name, "Result")) {
            result = i;
        }
    }
    if (p->expression_mode == GRAPH_EXPRESSION_BRANCH) {
        graph *generated = behavior_asset_find_graph(p->asset, call->reference_id);
        int true_output = -1, false_output = -1;
        for (int i = 0; i < call->output_count; i++) {
            if (call->output_pins[i].kind != PIN_EXEC) {
                continue;
            }
            if (!strcmp(call->output_pins[i].name, "True")) {
                true_output = i;
            }
            if (!strcmp(call->output_pins[i].name, "False")) {
                false_output = i;
            }
        }
        if (!generated || generated->expression_mode != GRAPH_EXPRESSION_BRANCH ||
            true_output < 0 || false_output < 0) {
            code_source_error(p->source, offset, offset + (int)strlen(text), "Expression does not produce True and False execution outcomes");
            goto done;
        }
        result = true_output;
    }
    if (result < 0) {
        code_source_error(p->source, offset, offset + (int)strlen(text), "Expression does not produce a value");
        goto done;
    }
    code_expression_source record = {
        .native = call,
        .text = keep_slice(p, text, 0, (int)strlen(text)),
        .start = offset,
        .end = offset + (int)strlen(text),
    };
    for (int i = 0; i < acount(inputs); i++) {
        const expression_input *input = &inputs[i];
        code_expression_read read = {
            .name = keep_slice(p, input->name, 0, (int)strlen(input->name)),
            .text = keep_slice(p, text, input->start, input->end),
            .start = offset + input->start,
            .end = offset + input->end,
            .resolved = input->value,
            .type = input->port.type,
            .contextual_number = input->contextual_number,
        };
        read.resolved.name = read.name;
        apush(record.reads, read);
    }
    apush(p->program->expressions, record);
    out->kind = CODE_CALL_OUTPUT;
    out->source = (code_call_output){ call, result };
    valid = true;
done:
    afree(tokens);
    afree(source);
    afree(origins);
    afree(inputs);
    return valid;
}
