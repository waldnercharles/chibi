#include "assets/code_program_internal.h"
#include "assets/code_source_internal.h"
#include "support/array.h"

#include <ctype.h>
#include <string.h>

static void identity(code_source *source, uint64_t current, uint64_t previous)
{
    if (!current || !previous) {
        return;
    }
    for (int i = 0; i < acount(source->identity_matches); i++) {
        if (source->identity_matches[i].current == current) {
            return;
        }
    }
    apush(source->identity_matches, ((code_source_identity){ current, previous }));
}
static bool same(const char *a, const char *b)
{
    return a && b ? !strcmp(a, b) : a == b;
}
static size_t assignment(const char *text)
{
    if (!text) {
        return 0;
    }
    const char *end = strpbrk(text, "=+-*/%");
    size_t length = end ? (size_t)(end - text) : strlen(text);
    while (length && isspace((unsigned char)text[length - 1])) {
        length--;
    }
    return length;
}
static bool anchor(const statement *a, const statement *b)
{
    if (a->kind != b->kind) {
        return false;
    }
    if (a->kind == CODE_STMT_CALL) {
        return a->engine == b->engine && same(a->target, b->target) &&
               same(a->method, b->method);
    }
    if (a->kind == CODE_STMT_LOCAL) {
        return same(a->result_name, b->result_name);
    }
    if (a->kind == CODE_STMT_EXPRESSION) {
        size_t an = assignment(a->text), bn = assignment(b->text);
        return an == bn && a->text && b->text && !strncmp(a->text, b->text, an);
    }
    if (a->kind == CODE_STMT_LABEL || a->kind == CODE_STMT_GOTO) {
        return same(a->target, b->target);
    }
    return true;
}
static bool exact_statement(const statement *a, const statement *b)
{
    return anchor(a, b) && same(a->text, b->text) &&
           same(a->second, b->second) && same(a->third, b->third);
}
static bool retained_elsewhere(
    statement *current,
    statement *previous,
    const statement *a,
    const statement *b
)
{
    if (exact_statement(a, b)) {
        return false;
    }
    int count = 0;
    for (int i = 0; i < acount(current); i++) {
        if (exact_statement(&current[i], b)) {
            count++;
        }
    }
    if (count != 1) {
        return false;
    }
    count = 0;
    for (int i = 0; i < acount(previous); i++) {
        if (exact_statement(&previous[i], b)) {
            count++;
        }
    }
    return count == 1;
}
static bool same_body(statement *current, statement *previous)
{
    if (acount(current) != acount(previous)) {
        return false;
    }
    for (int i = 0; i < acount(current); i++) {
        if (!exact_statement(&current[i], &previous[i]) ||
            !same_body(current[i].body, previous[i].body) ||
            !same_body(current[i].alternate, previous[i].alternate)) {
            return false;
        }
    }
    return true;
}

static void argument_producers(code_source *source, argument *current, argument *previous)
{
    for (int i = 0; i < acount(current); i++) {
        const argument *a = &current[i];
        if (!a->has_resolved || a->resolved.kind != CODE_CALL_OUTPUT) {
            continue;
        }
        for (int j = 0; j < acount(previous); j++) {
            const argument *b = &previous[j];
            if (a->positional != b->positional ||
                (a->positional ? a->ordinal != b->ordinal
                               : !same(a->port.name, b->port.name)) ||
                !same(a->text, b->text) || !b->has_resolved ||
                b->resolved.kind != CODE_CALL_OUTPUT) {
                continue;
            }
            const node *left = a->resolved.source.node,
                       *right = b->resolved.source.node;
            if (left && right && left->desc == right->desc) {
                identity(source, left->id, right->id);
            }
            break;
        }
    }
}

static void statements(
    code_source *source,
    scope *current_scope,
    scope *previous_scope,
    statement *current,
    statement *previous
)
{
    bool unchanged = same_body(current, previous);
    for (int i = 0; i < acount(current); i++) {
        statement *a = &current[i], *match = NULL;
        if (unchanged) {
            argument_producers(source, a->arguments, previous[i].arguments);
            node *current_node = code_program_statement_node(current_scope, a);
            node *previous_node = code_program_statement_node(previous_scope, &previous[i]);
            if (current_node && previous_node) {
                identity(source, current_node->id, previous_node->id);
                if (a->native && previous[i].native && current_node != a->native &&
                    a->native->desc == previous[i].native->desc) {
                    identity(source, a->native->id, previous[i].native->id);
                }
            }
            statements(
                source,
                current_scope,
                previous_scope,
                a->body,
                previous[i].body
            );
            statements(
                source,
                current_scope,
                previous_scope,
                a->alternate,
                previous[i].alternate
            );
            continue;
        }
        int exact = 0, candidates = 0;
        for (int j = 0; j < acount(previous); j++) {
            statement *b = &previous[j];
            if (!anchor(a, b) || retained_elsewhere(current, previous, a, b)) {
                continue;
            }
            candidates++;
            match = b;
            if (same(a->text, b->text) && same(a->second, b->second) &&
                same(a->third, b->third)) {
                exact++;
            }
        }
        if (exact == 1) {
            for (int j = 0; j < acount(previous); j++) {
                statement *b = &previous[j];
                if (anchor(a, b) && same(a->text, b->text) &&
                    same(a->second, b->second) && same(a->third, b->third)) {
                    match = b;
                }
            }
        } else if (candidates > 1) {
            continue;
        }
        if (!match || !candidates) {
            continue;
        }
        int current_count = 0;
        for (int j = 0; j < acount(current); j++) {
            if (anchor(a, &current[j])) {
                current_count++;
            }
        }
        if (current_count > 1 && exact != 1) {
            continue;
        }
        argument_producers(source, a->arguments, match->arguments);
        node *current_node = code_program_statement_node(current_scope, a);
        node *previous_node = code_program_statement_node(previous_scope, match);
        if (current_node && previous_node) {
            identity(source, current_node->id, previous_node->id);
            if (a->native && match->native && current_node != a->native &&
                a->native->desc == match->native->desc) {
                identity(source, a->native->id, match->native->id);
            }
        }
        statements(source, current_scope, previous_scope, a->body, match->body);
        statements(source, current_scope, previous_scope, a->alternate, match->alternate);
    }
}
void code_program_match_previous(code_source *source, const code_source *previous)
{
    code_program *current = source->program, *old = previous->program;
    for (int i = 0; i < acount(current->scopes); i++) {
        scope *a = current->scopes[i], *b = NULL;
        for (int j = 0; j < acount(old->scopes); j++) {
            scope *candidate = old->scopes[j];
            bool roots = a->native == source->asset->graph &&
                         candidate->native == previous->asset->graph;
            if (a->native->kind == candidate->native->kind &&
                (roots || same(a->native->name, candidate->native->name))) {
                b = candidate;
                break;
            }
        }
        if (!b) {
            continue;
        }
        identity(source, a->native->graph_id, b->native->graph_id);
        for (int j = 0; j < acount(a->locals); j++) {
            for (int k = 0; k < acount(b->locals); k++) {
                if (same(a->locals[j].name, b->locals[k].name)) {
                    identity(source, a->locals[j].id, b->locals[k].id);
                    break;
                }
            }
        }
        for (int j = 0; j < acount(a->declarations); j++) {
            declaration *d = a->declarations[j];
            for (int k = 0; k < acount(b->declarations); k++) {
                declaration *old_d = b->declarations[k];
                if (same(d->name, old_d->name) && d->native && old_d->native &&
                    d->native->desc == old_d->native->desc) {
                    identity(source, d->native->id, old_d->native->id);
                    break;
                }
            }
        }
        for (int j = 0; j < acount(a->continuations); j++) {
            continuation *c = &a->continuations[j];
            continuation *match = NULL;
            int count = 0;
            for (int k = 0; k < acount(b->continuations); k++) {
                continuation *old_c = &b->continuations[k];
                if (same(c->source, old_c->source) && same(c->outcome, old_c->outcome)) {
                    match = old_c;
                    count++;
                }
            }
            if (count == 1) {
                for (int k = 0; k < acount(c->aliases); k++) {
                    const declaration *alias = c->aliases[k];
                    for (int n = 0; n < acount(match->aliases); n++) {
                        const declaration *old_alias = match->aliases[n];
                        if (alias->native && old_alias->native &&
                            same(alias->name, old_alias->name) &&
                            same(alias->call.reference, old_alias->call.reference)) {
                            identity(
                                source,
                                alias->native->id,
                                old_alias->native->id
                            );
                            break;
                        }
                    }
                }
                statements(source, a, b, c->body, match->body);
            }
        }
    }
}
