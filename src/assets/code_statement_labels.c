#include "assets/code_program_internal.h"
#include "assets/code_source_internal.h"
#include "graph/model/registry.h"
#include "support/array.h"

#include <assert.h>
#include <string.h>

static void collect(parser *p, scope *s, statement *body)
{
    for (int i = 0; i < acount(body); i++) {
        statement *stmt = &body[i];
        if (stmt->kind == CODE_STMT_LABEL) {
            for (int j = 0; j < acount(s->labels); j++) {
                if (!strcmp(s->labels[j].name, stmt->target)) {
                    code_source_error(p->source, stmt->target_start, stmt->target_end, "Duplicate label in native scope");
                    return;
                }
            }
            stmt->native = graph_add_node(s->native, &NODE_Sequence);
            assert(stmt->native);
            while (stmt->native->output_count > 1) {
                int result = graph_remove_pin(
                    s->native,
                    stmt->native,
                    true,
                    stmt->native->output_count - 1
                );
                assert(result == 0);
                (void)result;
            }
            apush(
                s->labels,
                ((code_program_label){ .name = stmt->target,
                                       .node = stmt->native,
                                       .statement = stmt })
            );
        }
        collect(p, s, stmt->body);
        collect(p, s, stmt->alternate);
        if (acount(p->source->diagnostics)) {
            return;
        }
    }
}

void code_statement_prepare_labels(parser *p, scope *s)
{
    assert(!s->labels);
    for (int i = 0; i < acount(s->continuations); i++) {
        collect(p, s, s->continuations[i].body);
        if (acount(p->source->diagnostics)) {
            return;
        }
    }
}

static bool placeholder(const scope *s, const node *n)
{
    for (int i = 0; i < acount(s->labels); i++) {
        if (s->labels[i].node == n) {
            return true;
        }
    }
    return false;
}

static const edge *successor(const graph *g, const node *n)
{
    const edge *next = NULL;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind != GRAPH_EDGE_EXEC || e->from_id != n->id) {
            continue;
        }
        if (next || e->from_pin_id != n->output_pins[0].id) {
            return NULL;
        }
        next = e;
    }
    return next;
}

typedef struct
{
    node *source, *target;
    uint64_t input;
} label_alias;

/* A label chain can contain at most every scope label once. A path that never
 * reaches an executable node retains its placeholders and cycle semantics. */
static label_alias resolve(const scope *s, node *start)
{
    label_alias alias = { .source = start };
    node *current = start;
    for (int hops = 0; hops < acount(s->labels); hops++) {
        const edge *next = successor(s->native, current);
        if (!next) {
            return alias;
        }
        node *target = graph_node_by_id(s->native, next->to_id);
        int output, index;
        const pin_spec *pin = graph_pin_by_id(target, next->to_pin_id, &output, &index);
        assert(pin && !output && pin->kind == PIN_EXEC);
        if (!placeholder(s, target)) {
            alias.target = target;
            alias.input = pin->id;
            return alias;
        }
        current = target;
    }
    return alias;
}

static void replace(statement *body, const label_alias *aliases)
{
    for (int i = 0; i < acount(body); i++) {
        statement *stmt = &body[i];
        for (int j = 0; j < acount(aliases); j++) {
            if (aliases[j].target && stmt->native == aliases[j].source) {
                stmt->native = aliases[j].target;
                if (stmt->kind == CODE_STMT_LABEL) {
                    stmt->label_alias = true;
                }
                break;
            }
        }
        replace(stmt->body, aliases);
        replace(stmt->alternate, aliases);
    }
}

void code_statement_finish_labels(parser *p, scope *s)
{
    (void)p;
    label_alias *aliases = NULL;
    for (int i = 0; i < acount(s->labels); i++) {
        apush(aliases, resolve(s, s->labels[i].node));
    }
    /* Resolve all targets against the original graph before changing edges.
     * Only destinations change: source pin identity and execution order stay. */
    for (int i = 0; i < acount(s->native->edges); i++) {
        edge *e = &s->native->edges[i];
        if (e->kind != GRAPH_EDGE_EXEC) {
            continue;
        }
        for (int j = 0; j < acount(aliases); j++) {
            const label_alias *alias = &aliases[j];
            if (alias->target && e->to_id == alias->source->id) {
                e->to_id = alias->target->id;
                e->to_pin_id = alias->input;
                break;
            }
        }
    }
    for (int i = 0; i < acount(s->continuations); i++) {
        replace(s->continuations[i].body, aliases);
    }
    for (int i = 0; i < acount(s->labels); i++) {
        if (aliases[i].target) {
            s->labels[i].node = aliases[i].target;
            /* Removal invalidates the graph's cached edge index/revision after
               the destination rewrites and deletes the alias's own output. */
            int result = graph_remove_node(s->native, aliases[i].source);
            assert(result == 0);
            (void)result;
        }
    }
    afree(aliases);
}
