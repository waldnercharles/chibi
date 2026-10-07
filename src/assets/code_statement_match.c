#include "assets/code_program_internal.h"
#include "assets/code_source_internal.h"
#include "assets/code_value.h"
#include "graph/authoring/pin_edit.h"
#include "graph/model/registry.h"
#include "support/array.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static bool is(parser *p, const char *word)
{
    return code_token_is(&p->lexer, p->token, word);
}

static bool failed(parser *p)
{
    return acount(p->source->diagnostics) != 0;
}

static bool error(parser *p, int start, int end, const char *message)
{
    code_source_error(p->source, start, end, message);
    return false;
}

static char *literal_text(parser *p, int *start)
{
    int first = (int)p->token.start;
    *start = p->base + first;
    code_program_take(p, "-");
    if (p->token.kind != CODE_TOKEN_STRING && p->token.kind != CODE_TOKEN_NUMBER) {
        error(p, *start, p->base + (int)p->token.end, "Match cases require an integer ordinal or string literal");
        return NULL;
    }
    int end = (int)p->token.end;
    char *text = xmalloc((size_t)(end - first) + 1);
    memcpy(text, p->lexer.text + first, (size_t)(end - first));
    text[end - first] = 0;
    apush(p->program->strings, text);
    code_program_next(p);
    return text;
}

statement code_statement_parse_match(parser *p)
{
    statement match = { .kind = CODE_STMT_MATCH,
                        .start = p->base + (int)p->token.start };
    if (code_program_take(p, "enum")) {
        match.alias = xstrdup("enum");
        apush(p->program->strings, match.alias);
    } else if (code_program_take(p, "name")) {
        match.alias = xstrdup("name");
        apush(p->program->strings, match.alias);
    }
    if (!code_program_expect(p, "(")) {
        return match;
    }
    int end;
    match.text = code_program_capture(p, &match.value_start, &end);
    match.value_start += p->base;
    if (!code_program_expect(p, ")") || !code_program_expect(p, "{")) {
        return match;
    }
    bool has_default = false;
    while (!is(p, "}") && p->token.kind != CODE_TOKEN_END && !failed(p)) {
        statement arm = { .kind = CODE_STMT_BLOCK,
                          .start = p->base + (int)p->token.start };
        if (code_program_take(p, "case")) {
            arm.text = literal_text(p, &arm.value_start);
        } else if (code_program_take(p, "default")) {
            if (has_default) {
                error(p, arm.start, p->base + (int)p->token.end, "Match has multiple default arms");
            }
            has_default = true;
        } else {
            error(p, arm.start, p->base + (int)p->token.end, "Expected case or default in match");
        }
        if (failed(p)) {
            break;
        }
        arm.body = code_statement_parse_block(p);
        arm.end = p->base + (int)p->token.start;
        apush(match.body, arm);
    }
    code_program_expect(p, "}");
    match.end = p->base + (int)p->token.start;
    return match;
}

/* Native integer switches address outputs by ordinal. Text switches carry
 * their semantic label in the output name, with Default always last. */
static bool case_label(parser *p, statement *arm, bool text, int *ordinal, const char **label)
{
    char message[192];
    av_doc *doc = code_value_read(arm->text, message, sizeof message);
    const av_value *value = doc ? av_doc_root(doc) : NULL;
    bool valid = false;
    if (text && av_is_str(value)) {
        const char *decoded = av_get_str(value);
        if (av_get_len(value) == strlen(decoded)) {
            char *owned = xstrdup(decoded);
            apush(p->program->strings, owned);
            *label = owned;
            valid = true;
        }
    } else if (!text && av_is_int(value)) {
        uint64_t number = av_get_uint(value);
        if (arm->text[0] != '-' && number < INT_MAX) {
            *ordinal = (int)number;
            valid = true;
        }
    }
    av_doc_free(doc);
    if (!valid) {
        error(p, arm->value_start, arm->value_start + (int)strlen(arm->text), text ? "Text match cases require string literals without embedded NUL bytes" : "Integer and enum match cases require nonnegative int ordinals");
    }
    return valid;
}

node *code_statement_match_node(parser *p, scope *s, statement *match, int **outputs, int *default_output)
{
    *outputs = NULL;
    *default_output = -1;
    code_call_port expected = { .type = PIN_I32, .kind = PIN_DATA };
    for (int i = 0; i < acount(match->body); i++) {
        const statement *arm = &match->body[i];
        if (!arm->text) {
            continue;
        }
        code_lexer lexer;
        code_lex_init(&lexer, arm->text);
        if (code_lex_next(&lexer).kind == CODE_TOKEN_STRING) {
            expected.type = PIN_STR;
        }
        break;
    }
    if (match->text) {
        code_lexer lexer;
        code_lex_init(&lexer, match->text);
        if (code_lex_next(&lexer).kind == CODE_TOKEN_STRING) {
            expected.type = PIN_STR;
        }
    }
    bool enum_kind = match->alias && !strcmp(match->alias, "enum");
    bool name_kind = match->alias && !strcmp(match->alias, "name");
    if (enum_kind) {
        expected.type = PIN_I32;
    } else if (name_kind) {
        expected.type = PIN_NAME;
    }
    code_call_argument selection = { 0 };
    if (!match->text ||
        !code_program_value(p, s, match->text, match->value_start, &expected, &selection) ||
        !code_statement_value_type(p, s, &selection, &expected)) {
        return NULL;
    }
    if (expected.type == PIN_ENUM) {
        error(p, match->value_start, match->value_start + (int)strlen(match->text), "Native enum switches require int ordinals; typed enum selectors are unsupported");
        return NULL;
    }
    const node_desc *desc = enum_kind && expected.type == PIN_I32 ? &NODE_SwitchEnum
                          : expected.type == PIN_I32  ? &NODE_SwitchInt
                          : expected.type == PIN_STR  ? &NODE_SwitchString
                          : expected.type == PIN_NAME ? &NODE_SwitchName
                                                      : NULL;
    if (!desc || expected.container != PIN_CONTAINER_SINGLE ||
        (enum_kind && expected.type != PIN_I32) ||
        (name_kind && expected.type != PIN_NAME)) {
        error(p, match->value_start, match->value_start + (int)strlen(match->text), "Match requires a scalar int, string or name selector");
        return NULL;
    }
    bool text = expected.type == PIN_STR || expected.type == PIN_NAME;
    const char **labels = NULL;
    int *ordinals = NULL;
    int cases = 0;
    bool has_default = false;
    for (int i = 0; i < acount(match->body); i++) {
        statement *arm = &match->body[i];
        int ordinal = -1;
        const char *label = NULL;
        if (!arm->text) {
            if (has_default) {
                error(p, arm->start, arm->end, "Match has multiple default arms");
                goto refused;
            }
            has_default = true;
        } else {
            if (!case_label(p, arm, text, &ordinal, &label)) {
                goto refused;
            }
            for (int j = 0; j < acount(ordinals); j++) {
                bool duplicate = text ? labels[j] && !strcmp(labels[j], label)
                                      : ordinals[j] == ordinal;
                if (duplicate) {
                    error(p, arm->value_start, arm->value_start + (int)strlen(arm->text), "Duplicate match case");
                    goto refused;
                }
            }
            cases = text ? cases + 1 : ordinal >= cases ? ordinal + 1 : cases;
        }
        apush(ordinals, ordinal);
        apush(labels, label);
    }
    node *n = graph_add_node(s->native, desc);
    match->native = n;
    if (!n || graph_set_pin_type(s->native, n, false, 1, expected.type, expected.type_name)) {
        goto refused;
    }
    /* Configure the complete native case run before attaching selector data. */
    while (n->output_count > cases + 1) {
        if (graph_remove_pin(s->native, n, true, 0)) {
            goto refused;
        }
    }
    while (n->output_count < cases + 1) {
        if (graph_add_run_pin(s->native, n) < 0) {
            /* A zero-case switch has no run template instance. */
            pin_spec pin = { .name = "Case0", .type = PIN_FLOW, .kind = PIN_EXEC };
            if (graph_append_pin(s->native, n, true, &pin) < 0) {
                goto refused;
            }
        }
    }
    if (!text) {
        for (int output = 0; output < cases; output++) {
            char name[48];
            snprintf(name, sizeof name, "Case%d", output);
            if (graph_set_pin_name(s->native, n, true, output, name)) {
                goto refused;
            }
        }
    }
    int next_text = 0;
    for (int i = 0; i < acount(match->body); i++) {
        int output = cases;
        if (match->body[i].text) {
            output = text ? next_text++ : ordinals[i];
            if (text) {
                size_t size = strlen(labels[i]) + 6;
                char *name = xmalloc(size);
                snprintf(name, size, "Case:%s", labels[i]);
                int result = graph_set_pin_name(s->native, n, true, output, name);
                free(name);
                if (result) {
                    goto refused;
                }
            }
        }
        apush(*outputs, output);
    }
    /* Holes remain ordinal outputs and are routed to the default by lowering. */
    if (graph_set_pin_name(s->native, n, true, cases, "Default")) {
        goto refused;
    }
    int result = selection.kind == CODE_CALL_LITERAL
                   ? graph_set_pin_default(s->native, n, false, 1, &selection.literal)
               : selection.kind == CODE_CALL_BINDING
                   ? graph_set_pin_binding(s->native, n, false, 1, &selection.binding)
                   : graph_connect_data_conversion(
                         s->native,
                         selection.source.node,
                         selection.source.pin,
                         n,
                         1,
                         selection.conversion
                     );
    if (result) {
        error(p, match->value_start, match->value_start + (int)strlen(match->text), "Match selector cannot feed its native switch");
        goto refused;
    }
    *default_output = cases;
    afree(labels);
    afree(ordinals);
    return n;
refused:
    if (!failed(p)) {
        error(p, match->start, match->end, "Cannot construct native match cases");
    }
    afree(labels);
    afree(ordinals);
    afree(*outputs);
    *outputs = NULL;
    return NULL;
}
