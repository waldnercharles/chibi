#include "authoring/graph_expression.h"

#include "graph/model/local.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "support/array.h"
#include "support/code_lex.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void append_text(char **out, const char *text)
{
    for (int i = 0; text[i]; i++) {
        apush(*out, text[i]);
    }
}
static const char *expression_label(const behavior_asset *asset, const node *expression, int index)
{
    const pin_spec *pin = &expression->input_pins[index];
    if (pin->binding && pin->binding->kind == PIN_BINDING_VARIABLE) {
        const behavior_variable
            *variable = behavior_asset_find_variable_by_id(asset, pin->binding->variable_id);
        if (variable) {
            return variable->name;
        }
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        if (graph_node_by_id(g, expression->id) != expression) {
            continue;
        }
        node *event = NULL;
        int output = -1;
        if (graph_input_source(g, expression, index, &event, &output) &&
            event->desc == &NODE_Event && event->reference_name &&
            !strcmp(event->reference_name, "Tick") &&
            !strcmp(event->output_pins[output].name, "dt")) {
            return "event.dt";
        }
    }
    return pin->display_name && pin->display_name[0] ? pin->display_name : pin->name;
}
const char *graph_expression_input_label(const behavior_asset *asset, const node *expression, int input)
{
    assert(asset && expression && expression->desc == &NODE_BehaviorExpression);
    assert(input >= 0 && input < expression->input_count);
    return expression_label(asset, expression, input);
}
char *graph_expression_display(const behavior_asset *asset, const node *expression)
{
    const char *text = behavior_expression_source(asset, expression);
    if (!text) {
        return NULL;
    }
    code_lexer lexer;
    code_lex_init(&lexer, text);
    char *out = NULL;
    int copied = 0;
    for (;;) {
        code_token token = code_lex_next(&lexer);
        if (token.kind == CODE_TOKEN_END || token.kind == CODE_TOKEN_ERROR) {
            break;
        }
        if (token.kind != CODE_TOKEN_NAME) {
            continue;
        }
        for (int i = 0; i < expression->input_count; i++) {
            const pin_spec *pin = &expression->input_pins[i];
            if (strlen(pin->name) != token.end - token.start ||
                memcmp(pin->name, text + token.start, token.end - token.start)) {
                continue;
            }
            for (int at = copied; at < (int)token.start; at++) {
                apush(out, text[at]);
            }
            char label[80];
            graph_expression_input_name(asset, expression, i, label, sizeof label);
            append_text(&out, label);
            copied = (int)token.end;
            break;
        }
    }
    append_text(&out, text + copied);
    apush(out, 0);
    char *result = xstrdup(out);
    afree(out);
    return result;
}
char *graph_expression_resolve(
    const behavior_asset *asset,
    const node *expression,
    const char *text,
    char *error,
    size_t size
)
{
    if (size) {
        error[0] = 0;
    }
    code_lexer lexer;
    code_lex_init(&lexer, text);
    char *out = NULL;
    int copied = 0;
    for (;;) {
        code_token token = code_lex_next(&lexer);
        if (token.kind == CODE_TOKEN_END || token.kind == CODE_TOKEN_ERROR) {
            break;
        }
        if (token.kind != CODE_TOKEN_NAME) {
            continue;
        }
        code_lexer next = lexer;
        bool qualified = false;
        int end = (int)token.end;
        for (;;) {
            code_lexer look = next;
            code_token dot = code_lex_next(&look);
            if (dot.punctuation != '.') {
                break;
            }
            code_token member = code_lex_next(&look);
            if (member.kind != CODE_TOKEN_NAME) {
                break;
            }
            qualified = true;
            end = (int)member.end;
            next = look;
        }
        bool matched = false;
        for (int i = 0; i < expression->input_count; i++) {
            const pin_spec *pin = &expression->input_pins[i];
            if (qualified && strlen(pin->name) == token.end - token.start &&
                !memcmp(pin->name, text + token.start, token.end - token.start)) {
                /* Canonical input names retain their aggregate member access. */
                lexer = next;
                matched = true;
                break;
            }
            char label[80];
            graph_expression_input_name(asset, expression, i, label, sizeof label);
            if (strlen(label) != (size_t)(end - (int)token.start) ||
                memcmp(label, text + token.start, (size_t)(end - (int)token.start))) {
                continue;
            }
            for (int at = copied; at < (int)token.start; at++) {
                apush(out, text[at]);
            }
            append_text(&out, pin->name);
            copied = end;
            if (qualified) {
                lexer = next;
            }
            matched = true;
            break;
        }
        if (!matched && qualified) {
            if (size) {
                snprintf(error, size, "Unknown reflected expression input; add its binding or edit the behavior source");
            }
            afree(out);
            return NULL;
        }
    }
    append_text(&out, text + copied);
    apush(out, 0);
    char *result = xstrdup(out);
    afree(out);
    return result;
}
void graph_expression_capture_name(const char *text, char *out, size_t size)
{
    code_lexer lexer;
    code_lex_init(&lexer, text);
    code_token first = code_lex_next(&lexer);
    if (code_token_is(&lexer, first, "component")) {
        code_token open = code_lex_next(&lexer);
        code_token component = code_lex_next(&lexer);
        if (open.punctuation == '(' && component.kind == CODE_TOKEN_STRING) {
            const char *start = text + component.start + 1;
            int length = (int)(component.end - component.start) - 2;
            for (int i = 0; i < length; i++) {
                if (start[i] == '.') {
                    start += i + 1;
                    length -= i + 1;
                    i = -1;
                }
            }
            snprintf(out, size, "%.*s", length, start);
            return;
        }
        code_lex_init(&lexer, text);
        code_lex_next(&lexer);
    }
    code_token last = first;
    for (;;) {
        code_lexer next = lexer;
        code_token dot = code_lex_next(&next);
        if (dot.punctuation != '.') {
            break;
        }
        code_token member = code_lex_next(&next);
        if (member.kind != CODE_TOKEN_NAME && member.kind != CODE_TOKEN_STRING) {
            break;
        }
        last = member;
        lexer = next;
    }
    const char *start = text + first.start;
    int length = (int)(last.end - first.start);
    if (code_token_is(&lexer, first, "state") || code_token_is(&lexer, first, "local")) {
        start = text + last.start;
        length = (int)(last.end - last.start);
    }
    if (last.kind == CODE_TOKEN_STRING) {
        start = text + last.start + 1;
        length = (int)(last.end - last.start) - 2;
    }
    if (first.kind != CODE_TOKEN_NAME && first.kind != CODE_TOKEN_STRING) {
        snprintf(out, size, "value");
        return;
    }
    snprintf(out, size, "%.*s", length, start);
}

static const graph *expression_graph(const behavior_asset *asset, const node *expression)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        if (graph_node_by_id(asset->graphs[i], expression->id) == expression) {
            return asset->graphs[i];
        }
    }
    return NULL;
}

static void native_capture_name(
    const behavior_asset *asset,
    const node *expression,
    int input,
    char *out,
    size_t size
)
{
    const pin_spec *pin = &expression->input_pins[input];
    const graph *g = expression_graph(asset, expression);
    const char *name = graph_expression_input_label(asset, expression, input);
    if (name && strncmp(name, "_code_input_", 12)) {
        graph_expression_capture_name(name, out, size);
        return;
    }
    if (g && pin->binding && pin->binding->kind == PIN_BINDING_LOCAL) {
        const graph_local *local = graph_local_by_id(g, pin->binding->local_id);
        if (local) {
            snprintf(out, size, "%s", local->name);
            return;
        }
    }
    node *producer = NULL;
    int output = -1;
    if (g && graph_input_source(g, expression, input, &producer, &output)) {
        if (producer->desc == &NODE_Event) {
            snprintf(out, size, "event.%s", producer->output_pins[output].name);
            return;
        }
        name = producer->desc->name;
    } else {
        name = "value";
    }
    int copied = 0;
    for (int i = 0; name[i] && copied + 1 < (int)size; i++) {
        if (isalnum((unsigned char)name[i])) {
            out[copied++] = (char)tolower((unsigned char)name[i]);
        } else if (copied && out[copied - 1] != '_') {
            out[copied++] = '_';
        }
    }
    if (size) {
        out[copied] = 0;
    }
}

void graph_expression_input_name(
    const behavior_asset *asset,
    const node *expression,
    int input,
    char *out,
    size_t size
)
{
    assert(asset && expression && expression->desc == &NODE_BehaviorExpression);
    assert(input >= 0 && input < expression->input_count);
    if (expression->input_pins[input].kind != PIN_DATA) {
        snprintf(out, size, "%s", expression->input_pins[input].name);
        return;
    }
    typedef struct
    {
        char text[80];
    } label;
    label *names = NULL;
    for (int i = 0; i <= input; i++) {
        if (expression->input_pins[i].kind != PIN_DATA) {
            continue;
        }
        char base[64];
        native_capture_name(asset, expression, i, base, sizeof base);
        label name;
        snprintf(name.text, sizeof name.text, "%s", base);
        int ordinal = 1;
        for (int j = 0; j < acount(names); j++) {
            if (!strcmp(name.text, names[j].text)) {
                snprintf(name.text, sizeof name.text, "%s_%d", base, ++ordinal);
                j = -1;
            }
        }
        apush(names, name);
        if (i == input) {
            snprintf(out, size, "%s", name.text);
        }
    }
    afree(names);
}
