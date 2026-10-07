#include "assets/code_program_internal.h"
#include "assets/code_source_intelligence.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* This runs only on the parser's disposable, unpublished candidate. The normal
 * resolver supplies facts; syntax diagnostics still prevent asset publication. */
static void discard_probe_errors(code_source *source)
{
    for (int i = 0; i < acount(source->diagnostics); i++) {
        free(source->diagnostics[i].asset_name);
        free(source->diagnostics[i].graph_name);
    }
    afree(source->diagnostics);
}
static bool arguments_ready(const argument *arguments)
{
    for (int i = 0; i < acount(arguments); i++) {
        if (!arguments[i].port.name || !arguments[i].text) {
            return false;
        }
    }
    return true;
}
static bool declaration_ready(const declaration *decl)
{
    return decl->name && (decl->native || decl->expression ||
                          (decl->call.name && arguments_ready(decl->arguments)));
}
static void expression(parser *p, scope *s, const char *text, int start, pin_type type)
{
    if (!text) {
        return;
    }
    int end = start + (int)strlen(text);
    while (p->source->text[end] && isspace((unsigned char)p->source->text[end])) {
        end++;
    }
    char *probe = xmalloc((size_t)(end - start) + 1);
    memcpy(probe, p->source->text + start, (size_t)(end - start));
    probe[end - start] = 0;
    code_call_port expected = { .kind = PIN_DATA, .type = type };
    code_call_argument result = { 0 };
    code_program_complete_ecs_path(p, s, probe, start);
    code_statement_expression(p, s, probe, start, &expected, &result);
    free(probe);
    discard_probe_errors(p->source);
}
static declaration *probe_result(parser *p, scope *s, statement *stmt, node *native, int end)
{
    if (!stmt->result_name || !native ||
        code_program_declaration(s, stmt->result_name)) {
        return NULL;
    }
    int result = -1;
    for (int i = 0; i < native->output_count; i++) {
        if (native->output_pins[i].kind != PIN_DATA ||
            native->output_pins[i].field_source) {
            continue;
        }
        if (result >= 0) {
            return NULL;
        }
        result = i;
    }
    if (result < 0) {
        return NULL;
    }
    const pin_spec *pin = &native->output_pins[result];
    declaration *alias = xcalloc(1, sizeof *alias);
    *alias = (declaration){ .name = stmt->result_name,
                            .native = native,
                            .result_local = true,
                            .start = stmt->result_start,
                            .end = stmt->result_end,
                            .symbol = (uint64_t)stmt->result_start + 1 };
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
                                    .start = alias->start,
                                    .end = alias->end,
                                    .scope_start = stmt->end,
                                    .scope_end = end,
                                    .graph_id = s->native->graph_id,
                                    .node_id = native->id,
                                    .pin_id = pin->id,
                                    .renameable = true };
    code_intelligence_symbol(p->source, &symbol);
    apush(s->declarations, alias);
    return alias;
}
static void statements(parser *p, scope *s, statement *body)
{
    int initial = acount(s->declarations);
    int end = acount(body) ? body[acount(body) - 1].end : s->end;
    declaration **results = NULL;
    for (int i = 0; i < acount(body); i++) {
        statement *stmt = &body[i];
        if (stmt->kind == CODE_STMT_FOR || stmt->kind == CODE_STMT_FOREACH) {
            declaration alias;
            bool bound = code_statement_probe_loop(p, s, stmt, &alias);
            discard_probe_errors(p->source);
            statements(p, s, stmt->body);
            if (bound) {
                asetlen(s->declarations, acount(s->declarations) - 1);
                afree(alias.results);
            }
            continue;
        }
        if (stmt->kind == CODE_STMT_CALL && stmt->target) {
            if (arguments_ready(stmt->arguments)) {
                stmt->native = code_statement_probe_call(p, s, stmt);
            }
            discard_probe_errors(p->source);
            declaration *result = probe_result(p, s, stmt, stmt->native, end);
            if (result) {
                apush(results, result);
            }
        }
        if (stmt->kind == CODE_STMT_LOCAL && stmt->text) {
            code_call_port expected = { .kind = PIN_DATA, .type = PIN_FLOW };
            code_call_argument value = { 0 };
            if (code_statement_expression(p, s, stmt->text, stmt->value_start, &expected, &value) &&
                value.kind == CODE_CALL_OUTPUT) {
                declaration *result = probe_result(p, s, stmt, value.source.node, end);
                if (result) {
                    apush(results, result);
                }
            }
            discard_probe_errors(p->source);
        }
        expression(
            p,
            s,
            stmt->text,
            stmt->value_start,
            stmt->kind == CODE_STMT_IF || stmt->kind == CODE_STMT_WHILE ? PIN_BOOL : PIN_FLOW
        );
        expression(p, s, stmt->second, stmt->second_start, PIN_FLOW);
        expression(p, s, stmt->third, stmt->third_start, PIN_FLOW);
        statements(p, s, stmt->body);
        statements(p, s, stmt->alternate);
    }
    asetlen(s->declarations, initial);
    for (int i = 0; i < acount(results); i++) {
        afree(results[i]->results);
        free(results[i]);
    }
    afree(results);
}
void code_program_partial_intelligence(parser *p)
{
    code_source_diagnostic *syntax = p->source->diagnostics;
    p->source->diagnostics = NULL;
    for (int i = 0; i < acount(p->program->scopes); i++) {
        scope *s = p->program->scopes[i];
        declaration **original = s->declarations;
        s->declarations = NULL;
        for (int j = 0; j < acount(original); j++) {
            if (declaration_ready(original[j])) {
                apush(s->declarations, original[j]);
            }
        }
        for (int j = 0; j < acount(s->declarations); j++) {
            code_program_build(p, s, s->declarations[j]);
            discard_probe_errors(p->source);
        }
        local_declaration *original_locals = s->locals;
        s->locals = NULL;
        for (int j = 0; j < acount(original_locals); j++) {
            if (original_locals[j].name && original_locals[j].text) {
                apush(s->locals, original_locals[j]);
            }
        }
        code_statement_lower_locals(p, s);
        discard_probe_errors(p->source);
        for (int j = 0; j < acount(s->returns); j++) {
            argument *arg = &s->returns[j];
            if (arg->text) {
                arg->has_resolved = code_program_value(p, s, arg->text, arg->start, &arg->port, &arg->resolved);
                discard_probe_errors(p->source);
            }
        }
        for (int j = 0; j < acount(s->continuations); j++) {
            continuation *handler = &s->continuations[j];
            declaration *handled = handler->source
                                     ? code_program_declaration(s, handler->source)
                                     : NULL;
            node *event = handled ? code_program_build(p, s, handled) : NULL;
            discard_probe_errors(p->source);
            int count = acount(s->declarations);
            for (int k = 0; k < acount(handler->aliases); k++) {
                declaration *alias = handler->aliases[k];
                if (!declaration_ready(alias)) {
                    continue;
                }
                apush(s->declarations, alias);
                if (!strcmp(alias->name, "event")) {
                    alias->native = event;
                } else {
                    code_program_build(p, s, alias);
                    discard_probe_errors(p->source);
                }
            }
            statements(p, s, handler->body);
            asetlen(s->declarations, count);
        }
        for (int j = 0; j < acount(s->locals); j++) {
            for (int k = 0; k < acount(original_locals); k++) {
                if (original_locals[k].start == s->locals[j].start) {
                    original_locals[k] = s->locals[j];
                    break;
                }
            }
        }
        afree(s->locals);
        s->locals = original_locals;
        afree(s->declarations);
        s->declarations = original;
    }
    discard_probe_errors(p->source);
    p->source->diagnostics = syntax;
}
