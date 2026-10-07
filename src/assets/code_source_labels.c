#include "assets/code_program_internal.h"
#include "assets/code_source.h"
#include "graph/model/local.h"
#include "graph/nodes/component_nodes.h"
#include "support/array.h"
#include "support/code_lex.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

const char *code_source_expression_input_label_at(
    const code_source *source,
    const behavior_asset *asset,
    const node *expression,
    int input
)
{
    assert(asset && expression && expression->desc == &NODE_BehaviorExpression);
    assert(input >= 0 && input < expression->input_count);
    const code_program *program = source ? source->program : NULL;
    const char *name = expression->input_pins[input].name;
    for (int i = 0; program && i < acount(program->expressions); i++) {
        const code_expression_source *record = &program->expressions[i];
        if (record->native->id != expression->id) {
            continue;
        }
        for (int j = 0; j < acount(record->reads); j++) {
            if (!strcmp(record->reads[j].name, name)) {
                return record->reads[j].text;
            }
        }
    }
    return code_source_expression_input_label(asset, expression, input);
}

void code_source_expression_capture_name(const char *text, char *out, size_t size)
{
    code_lexer lexer;
    code_lex_init(&lexer, text);
    code_token first = code_lex_next(&lexer);
    if (first.punctuation == '&') {
        first = code_lex_next(&lexer);
    }
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
    const char *name = code_source_expression_input_label(asset, expression, input);
    if (name && strncmp(name, "_code_input_", 12)) {
        code_source_expression_capture_name(name, out, size);
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

static void capture_name(
    const code_source *source,
    const behavior_asset *asset,
    const node *expression,
    int input,
    char *out,
    size_t size
)
{
    const char *text = code_source_expression_input_label_at(source, asset, expression, input);
    if (text && strncmp(text, "_code_input_", 12)) {
        code_source_expression_capture_name(text, out, size);
        return;
    }
    native_capture_name(asset, expression, input, out, size);
}

void code_source_expression_input_name(
    const code_source *source,
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
        capture_name(source, asset, expression, i, base, sizeof base);
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
