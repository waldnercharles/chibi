#include "assets/code_program_reconcile.h"

#include "assets/code_print.h"
#include "assets/code_program_internal.h"
#include "assets/code_query.h"
#include "assets/code_source_internal.h"
#include "assets/code_value.h"
#include "graph/document/serialize_internal.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "support/text_edit.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool same_string(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}
static bool generated(const behavior_asset *asset, const graph *g)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        for (int j = 0; j < acount(asset->graphs[i]->nodes); j++) {
            const node *n = asset->graphs[i]->nodes[j];
            if (n->desc == &NODE_BehaviorExpression && n->reference_id == g->graph_id) {
                return true;
            }
        }
    }
    return false;
}
static const graph *containing(const behavior_asset *asset, const node *n)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        if (graph_node_by_id(asset->graphs[i], n->id) == n) {
            return asset->graphs[i];
        }
    }
    return NULL;
}
static bool binding_equal(const pin_binding *a, const pin_binding *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->kind == b->kind && a->variable_id == b->variable_id &&
           a->local_id == b->local_id && a->event_node_id == b->event_node_id &&
           a->event_pin_id == b->event_pin_id &&
           same_string(a->source_asset, b->source_asset) &&
           same_string(a->component_key, b->component_key) &&
           same_string(a->field_name, b->field_name);
}
static bool pin_equal(const pin_spec *a, const pin_spec *b)
{
    unsigned semantic = PIN_FLAG_REFERENCE | PIN_FLAG_SERVICE | PIN_FLAG_SETTING |
                        PIN_FLAG_DEFAULT_SELF | PIN_FLAG_WILDCARD;
    return a->id == b->id && same_string(a->name, b->name) && a->type == b->type &&
           same_string(a->type_name, b->type_name) && a->container == b->container &&
           a->kind == b->kind && (a->flags & semantic) == (b->flags & semantic) &&
           a->group == b->group && a->field_source == b->field_source &&
           a->split_parent == b->split_parent &&
           same_string(a->field_name, b->field_name) &&
           binding_equal(a->binding, b->binding) &&
           serialize_same_value(&a->default_value, &b->default_value);
}
/* Generated expression bodies are rebuilt from the patched expression text.
 * Authoring annotations never affect this executable comparison. */
static bool editable_shape(const behavior_asset *a, const behavior_asset *b)
{
    if (a->domain != b->domain || !same_string(a->name, b->name) ||
        !same_string(a->owner_type_name, b->owner_type_name) ||
        acount(a->variables) != acount(b->variables)) {
        return false;
    }
    for (int i = 0; i < acount(a->variables); i++) {
        const behavior_variable *x = &a->variables[i],
                                *y = behavior_asset_find_variable_by_id(b, x->id);
        if (!y || x->type != y->type || x->container != y->container ||
            !same_string(x->name, y->name) ||
            !same_string(x->type_name, y->type_name)) {
            return false;
        }
    }
    int left = 0, right = 0;
    for (int i = 0; i < acount(a->graphs); i++) {
        left += !generated(a, a->graphs[i]);
    }
    for (int i = 0; i < acount(b->graphs); i++) {
        right += !generated(b, b->graphs[i]);
    }
    if (left != right) {
        return false;
    }
    for (int i = 0; i < acount(a->graphs); i++) {
        const graph *x = a->graphs[i], *y = behavior_asset_find_graph(b, x->graph_id);
        if (generated(a, x)) {
            continue;
        }
        if (!y || x->kind != y->kind || x->dataflow != y->dataflow ||
            x->call_depth_limit != y->call_depth_limit ||
            (x != a->graph && !same_string(x->name, y->name)) ||
            acount(x->nodes) != acount(y->nodes) ||
            acount(x->edges) != acount(y->edges) ||
            acount(x->locals) != acount(y->locals)) {
            return false;
        }
        for (int j = 0; j < acount(x->locals); j++) {
            const graph_local *local = &x->locals[j],
                              *next = graph_local_by_id(y, local->id);
            if (!next || !same_string(local->name, next->name) ||
                local->source_id != next->source_id ||
                local->source_pin_id != next->source_pin_id) {
                return false;
            }
        }
        for (int j = 0; j < acount(x->edges); j++) {
            const edge *e = &x->edges[j];
            bool found = false;
            for (int k = 0; k < acount(y->edges); k++) {
                const edge *f = &y->edges[k];
                found |= e->kind == f->kind && e->from_id == f->from_id &&
                         e->to_id == f->to_id && e->from_pin_id == f->from_pin_id &&
                         e->to_pin_id == f->to_pin_id &&
                         e->conversion == f->conversion &&
                         e->exec_order == f->exec_order;
            }
            if (!found) {
                return false;
            }
        }
        for (int j = 0; j < acount(x->nodes); j++) {
            const node *n = x->nodes[j], *m = graph_node_by_id(y, n->id);
            if (!m || n->desc != m->desc || n->disabled != m->disabled ||
                n->reference_id != m->reference_id ||
                !same_string(n->reference_asset, m->reference_asset) ||
                n->input_count != m->input_count || n->output_count != m->output_count ||
                (n->desc != &NODE_BehaviorExpression &&
                 !same_string(n->reference_name, m->reference_name))) {
                return false;
            }
            for (int k = 0; k < n->input_count; k++) {
                if (!pin_equal(&n->input_pins[k], &m->input_pins[k])) {
                    return false;
                }
            }
            for (int k = 0; k < n->output_count; k++) {
                if (!pin_equal(&n->output_pins[k], &m->output_pins[k])) {
                    return false;
                }
            }
        }
    }
    return true;
}
static char *value_text(const pin_value *value)
{
    av_doc *doc = av_doc_new();
    char *text = code_value_write(serialize_value(doc, value));
    av_doc_free(doc);
    return text;
}
static bool state_range(const char *text, int after, int *start, int *end)
{
    code_lexer lex;
    code_lex_init(&lex, text);
    lex.at = (size_t)after;
    code_token token;
    do {
        token = code_lex_next(&lex);
    } while (token.kind != CODE_TOKEN_END && token.punctuation != '=');
    if (token.kind == CODE_TOKEN_END) {
        return false;
    }
    token = code_lex_next(&lex);
    *start = (int)token.start;
    *end = *start;
    while (token.kind != CODE_TOKEN_END && token.punctuation != ';') {
        *end = (int)token.end;
        token = code_lex_next(&lex);
    }
    return *end > *start;
}
static char *readable_expression(const code_expression_source *record, const char *text)
{
    code_lexer lex;
    code_lex_init(&lex, text);
    text_edit *edits = NULL;
    for (;;) {
        code_token token = code_lex_next(&lex);
        if (token.kind == CODE_TOKEN_END || token.kind == CODE_TOKEN_ERROR) {
            break;
        }
        if (token.kind != CODE_TOKEN_NAME) {
            continue;
        }
        for (int i = 0; i < acount(record->reads); i++) {
            const code_expression_read *read = &record->reads[i];
            if (strlen(read->name) == token.end - token.start &&
                !strncmp(text + token.start, read->name, token.end - token.start)) {
                apush(edits, ((text_edit){ (int)token.start, (int)token.end, read->text }));
                break;
            }
        }
    }
    char *result = text_edits_apply(text, edits, acount(edits));
    afree(edits);
    return result;
}
/* Comments are collected lexically, never from strings or generated identity
 * annotations. The exact authored comment bytes survive a topology rewrite. */
static char *comments(const char *text, const char *existing)
{
    char *out = NULL;
    for (int i = 0; text[i];) {
        if (text[i] == '"' || text[i] == '\'') {
            char quote = text[i++];
            while (text[i] && text[i] != quote) {
                if (text[i] == '\\' && text[i + 1]) {
                    i++;
                }
                i++;
            }
            if (text[i]) {
                i++;
            }
            continue;
        }
        if (text[i] == '/' && (text[i + 1] == '/' || text[i + 1] == '*')) {
            int start = i;
            bool block = text[i + 1] == '*';
            i += 2;
            if (block) {
                while (text[i] && !(text[i] == '*' && text[i + 1] == '/')) {
                    i++;
                }
                if (text[i]) {
                    i += 2;
                }
            } else {
                while (text[i] && text[i] != '\n') {
                    i++;
                }
            }
            int marker = start + 2;
            while (isspace((unsigned char)text[marker])) {
                marker++;
            }
            if (!strncmp(text + marker, "@node", 5) ||
                !strncmp(text + marker, "@authoring", 10)) {
                continue;
            }
            if (existing) {
                size_t length = (size_t)(i - start);
                char *comment = xmalloc(length + 1);
                memcpy(comment, text + start, length);
                comment[length] = 0;
                bool present = strstr(existing, comment) != NULL;
                free(comment);
                if (present) {
                    continue;
                }
            }
            for (int j = start; j < i; j++) {
                apush(out, text[j]);
            }
            apush(out, '\n');
            continue;
        }
        i++;
    }
    apush(out, 0);
    char *result = xstrdup(out);
    afree(out);
    return result;
}
static char *fallback(const code_source *source, const behavior_asset *asset, char *error, size_t size)
{
    char *formatted = code_print_behavior(asset, error, size);
    if (!formatted) {
        return NULL;
    }
    code_program *program = source->program;
    code_lexer lex;
    code_lex_init(&lex, source->text);
    code_token first = code_lex_next(&lex);
    code_token authored_name = code_lex_next(&lex);
    bool system = code_token_is(&lex, first, "system"),
         library = code_token_is(&lex, first, "library");
    text_edit *edits = NULL;
    char *name = NULL;
    if (same_string(source->asset->name, asset->name) &&
        (authored_name.kind == CODE_TOKEN_NAME ||
         authored_name.kind == CODE_TOKEN_STRING)) {
        size_t count = authored_name.end - authored_name.start;
        name = xmalloc(count + 1);
        memcpy(name, source->text + authored_name.start, count);
        name[count] = 0;
        code_lexer header;
        code_lex_init(&header, formatted);
        code_lex_next(&header);
        code_token token = code_lex_next(&header);
        apush(edits, ((text_edit){ (int)token.start, (int)token.end, name }));
    }
    if (system || library) {
        code_lexer header;
        code_lex_init(&header, formatted);
        code_token token = code_lex_next(&header);
        apush(edits, ((text_edit){ (int)token.start, (int)token.end, system ? "system" : "library" }));
    }
    char *query = program && program->query ? code_query_write(program->query) : NULL;
    char *directive = NULL;
    if (query) {
        code_lexer scan;
        code_lex_init(&scan, formatted);
        code_token token;
        do {
            token = code_lex_next(&scan);
        } while (token.kind != CODE_TOKEN_END && token.punctuation != '{');
        size_t length = strlen(query) + 20;
        directive = xmalloc(length);
        snprintf(directive, length, "\n    query %s;", query);
        apush(edits, ((text_edit){ (int)token.end, (int)token.end, directive }));
    }
    char *rewritten = text_edits_apply(formatted, edits, acount(edits));
    free(formatted);
    free(name);
    free(query);
    free(directive);
    afree(edits);
    char *retained = comments(source->text, NULL);
    size_t length = strlen(retained) + strlen(rewritten) + 1;
    char *result = xmalloc(length);
    snprintf(result, length, "%s%s", retained, rewritten);
    free(retained);
    free(rewritten);
    return result;
}
char *code_program_reconcile(const code_source *source, const behavior_asset *asset, char *error, size_t size)
{
    if (size) {
        error[0] = 0;
    }
    if (!source || !source->asset || !source->program) {
        return NULL;
    }
    if (!editable_shape(source->asset, asset)) {
        return fallback(source, asset, error, size);
    }
    code_program *program = source->program;
    text_edit *edits = NULL;
    bool valid = true;
    for (int i = 0; i < acount(program->states); i++) {
        const state_declaration *state = &program->states[i];
        const behavior_variable
            *old = behavior_asset_find_variable_by_id(source->asset, state->native_id),
            *next = behavior_asset_find_variable_by_id(asset, state->native_id);
        if (!old || !next) {
            valid = false;
            break;
        }
        if (serialize_same_value(&old->default_value, &next->default_value)) {
            continue;
        }
        int start, end;
        char *text = value_text(&next->default_value);
        if (!text || !state_range(source->text, state->end, &start, &end)) {
            free(text);
            valid = false;
            break;
        }
        apush(edits, ((text_edit){ start, end, text }));
    }
    for (int i = 0; valid && i < acount(program->expressions); i++) {
        const code_expression_source *record = &program->expressions[i];
        const graph *owner = containing(source->asset, record->native);
        graph *g = owner ? behavior_asset_find_graph(asset, owner->graph_id) : NULL;
        node *n = g ? graph_node_by_id(g, record->native->id) : NULL;
        if (!n) {
            valid = false;
            break;
        }
        const char *before = behavior_expression_source(source->asset, record->native),
                   *after = behavior_expression_source(asset, n);
        if (same_string(before, after)) {
            continue;
        }
        if (!after) {
            valid = false;
            break;
        }
        char *text = readable_expression(record, after);
        apush(edits, ((text_edit){ record->start, record->end, text }));
    }
    for (int i = 0; i < acount(edits); i++) {
        text_edit *edit = &edits[i];
        size_t length = (size_t)(edit->end - edit->start);
        char *old = xmalloc(length + 1);
        memcpy(old, source->text + edit->start, length);
        old[length] = 0;
        char *trivia = comments(old, edit->text);
        free(old);
        if (trivia[0]) {
            size_t size = strlen(trivia) + strlen(edit->text) + 1;
            char *replacement = xmalloc(size);
            snprintf(replacement, size, "%s%s", trivia, edit->text);
            free((char *)edit->text);
            edit->text = replacement;
        }
        free(trivia);
    }
    int edit_count = acount(edits);
    int changed_expressions = 0;
    for (int i = 0; i < acount(source->asset->graphs); i++) {
        const graph *g = source->asset->graphs[i];
        graph *next = behavior_asset_find_graph(asset, g->graph_id);
        if (!next || generated(source->asset, g)) {
            continue;
        }
        for (int j = 0; j < acount(g->nodes); j++) {
            const node *n = g->nodes[j], *m = graph_node_by_id(next, n->id);
            if (n->desc == &NODE_BehaviorExpression && m &&
                !same_string(
                    behavior_expression_source(source->asset, n),
                    behavior_expression_source(asset, m)
                )) {
                changed_expressions++;
            }
        }
    }
    int patched_expressions = 0;
    for (int i = 0; i < acount(edits); i++) {
        for (int j = 0; j < acount(program->expressions); j++) {
            patched_expressions += edits[i].start == program->expressions[j].start &&
                                   edits[i].end == program->expressions[j].end;
        }
    }
    valid &= changed_expressions == patched_expressions;
    char *text = valid ? text_edits_apply(source->text, edits, edit_count) : NULL;
    for (int i = 0; i < acount(edits); i++) {
        free((char *)edits[i].text);
    }
    afree(edits);
    if (!text) {
        return fallback(source, asset, error, size);
    }
    if (!edit_count) {
        return text;
    }
    code_source *candidate = code_source_parse_context(text, source, &source->context);
    valid = candidate->asset != NULL;
    code_source_free(candidate);
    if (valid) {
        return text;
    }
    free(text);
    return fallback(source, asset, error, size);
}
