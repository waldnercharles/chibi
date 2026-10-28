#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/union.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A @fallible call's trailing optional result must be acknowledged. Sibling
 * results remain valid: streaming APIs can return progress and an error.
 * Follow each produced outcome through local aliases until a real use or an
 * explicit transfer. Binding it to an otherwise unused local is not a use. */
enum
{
    RESULT_TUPLE = 1,
    RESULT_OPTIONAL = 2,
    RESULT_AWAIT = 4,
    RESULT_TASK = 8,
    RESULT_POINTER = 16,
};
typedef struct
{
    int target, condition;
    bool truth;
} result_edge;
typedef struct
{
    int statement;
    bool loop_guard;
    result_edge *edges;
} result_node;
typedef struct
{
    bool reached, pending, success;
    unsigned *aliases;
    int *targets;
    bool *associated; /* This generation may have occupied the cell on a path. */
} result_state;
typedef struct
{
    analyzer *analyzer;
    const script_function *function;
    result_node *nodes;
    int *labels;
    int source;
    uint64_t member;
    bool lost, visited;
} result_check;

static int node(result_check *check, int statement, bool guard)
{
    apush(check->nodes, ((result_node){ .statement = statement, .loop_guard = guard }));
    return acount(check->nodes) - 1;
}
static void edge(result_check *check, int from, int to)
{
    apush(check->nodes[from].edges, ((result_edge){ .target = to, .condition = -1 }));
}
static void branch(result_check *check, int from, int to, int condition, bool truth)
{
    apush(check->nodes[from].edges, ((result_edge){ to, condition, truth }));
}
static int graph(result_check *check, int index, int next, int leave, int repeat)
{
    if (index < 0) {
        return next;
    }
    const script_statement *statement = &check->analyzer->out->statements[index];
    if (statement->kind == SCRIPT_STMT_BLOCK || statement->kind == SCRIPT_STMT_SEQUENCE ||
        statement->kind == SCRIPT_STMT_CASE) {
        if (statement->body >= 0) {
            return graph(check, statement->body, next, leave, repeat);
        }
        for (int i = acount(statement->children) - 1; i >= 0; i--) {
            next = graph(check, statement->children[i], next, leave, repeat);
        }
        return next;
    }
    int current = node(check, index, false);
    if (statement->kind == SCRIPT_STMT_RETURN) {
        return current;
    }
    if (statement->kind == SCRIPT_STMT_GOTO) {
        return current;
    }
    if (statement->kind == SCRIPT_STMT_LABEL && statement->symbol >= 0) {
        check->labels[statement->symbol] = current;
    }
    if (statement->kind == SCRIPT_STMT_BREAK || statement->kind == SCRIPT_STMT_CONTINUE) {
        edge(check, current, statement->kind == SCRIPT_STMT_BREAK ? leave : repeat);
        return current;
    }
    if (statement->kind == SCRIPT_STMT_IF) {
        const script_expression *condition = &check->analyzer->out
                                                  ->expressions[statement->expression];
        bool fixed = condition->kind == SCRIPT_EXPR_CONSTANT &&
                     condition->type.storage == PIN_BOOL;
        if (!fixed || condition->constant.b) {
            branch(
                check,
                current,
                graph(check, statement->body, next, leave, repeat),
                statement->expression,
                true
            );
        }
        if (!fixed || !condition->constant.b) {
            branch(
                check,
                current,
                graph(check, statement->alternative, next, leave, repeat),
                statement->expression,
                false
            );
        }
        return current;
    }
    if (statement->kind == SCRIPT_STMT_MATCH) {
        const script_expression *subject = &check->analyzer->out
                                                ->expressions[statement->expression];
        bool exhaustive = (subject->type.flags & SCRIPT_TYPE_UNION) != 0;
        bool has_default = false;
        unsigned booleans = 0;
        int fallback = next;
        for (int i = 0; i < acount(statement->children); i++) {
            const script_statement *arm = &check->analyzer->out
                                               ->statements[statement->children[i]];
            exhaustive |= arm->expression < 0;
            if (arm->expression < 0) {
                has_default = true;
                fallback = graph(check, arm->body, next, leave, repeat);
            }
            if (subject->type.storage == PIN_BOOL && arm->expression >= 0) {
                const script_expression *pattern = &check->analyzer->out->expressions[arm->expression];
                if (pattern->kind == SCRIPT_EXPR_CONSTANT) {
                    booleans |= pattern->constant.b ? 1u : 2u;
                }
            }
        }
        exhaustive |= booleans == 3;
        int dispatch = fallback;
        for (int i = acount(statement->children) - 1; i >= 0; i--) {
            const script_statement *arm = &check->analyzer->out
                                               ->statements[statement->children[i]];
            if (arm->expression < 0) {
                continue;
            }
            int test = node(check, statement->children[i], false);
            branch(check, test, graph(check, arm->body, next, leave, repeat), arm->expression, true);
            if (dispatch != next || !exhaustive || has_default) {
                branch(check, test, dispatch, arm->expression, false);
            }
            dispatch = test;
        }
        edge(check, current, dispatch);
        return current;
    }
    if (statement->kind == SCRIPT_STMT_WHILE) {
        const script_expression *condition = &check->analyzer->out
                                                  ->expressions[statement->expression];
        bool fixed = condition->kind == SCRIPT_EXPR_CONSTANT &&
                     condition->type.storage == PIN_BOOL;
        if (!fixed || condition->constant.b) {
            branch(
                check,
                current,
                graph(check, statement->body, current, next, current),
                statement->expression,
                true
            );
        }
        if (!fixed || !condition->constant.b) {
            branch(check, current, next, statement->expression, false);
        }
        return current;
    }
    if (statement->kind == SCRIPT_STMT_FOR || statement->kind == SCRIPT_STMT_FOREACH) {
        int guard = node(check, index, true);
        edge(check, current, guard);
        edge(check, guard, next);
        edge(check, guard, graph(check, statement->body, guard, next, guard));
        return current;
    }
    edge(check, current, next);
    return current;
}
static void acknowledge(result_state *state, unsigned value)
{
    if (value) {
        state->pending = false;
    }
}
static bool merge(const result_check *, result_state *, const result_state *);
static result_state copy_state(const result_check *check, const result_state *state)
{
    int symbols = acount(check->analyzer->out->symbols);
    result_state copy = *state;
    copy.aliases = xmalloc((size_t)symbols * sizeof *copy.aliases);
    copy.targets = xmalloc((size_t)symbols * sizeof *copy.targets);
    copy.associated = xmalloc((size_t)symbols * sizeof *copy.associated);
    memcpy(copy.aliases, state->aliases, (size_t)symbols * sizeof *copy.aliases);
    memcpy(copy.targets, state->targets, (size_t)symbols * sizeof *copy.targets);
    memcpy(copy.associated, state->associated, (size_t)symbols * sizeof *copy.associated);
    return copy;
}
static void free_state(result_state *state)
{
    free(state->aliases);
    free(state->targets);
    free(state->associated);
}
static unsigned symbol_alias(const result_check *check, const result_state *state, int symbol, int depth)
{
    if (symbol < 0 || depth >= SCRIPT_MAX_DEPTH) {
        return 0;
    }
    if (state->targets[symbol] >= 0) {
        unsigned value = symbol_alias(check, state, state->targets[symbol], depth + 1);
        return value ? value + RESULT_POINTER : 0;
    }
    return state->aliases[symbol];
}
static int pointer_target(const result_check *, const result_state *, int);
static int local_target(const result_check *check, const result_state *state, int index)
{
    if (index < 0) {
        return -1;
    }
    const script_expression *value = &check->analyzer->out->expressions[index];
    if (value->kind == SCRIPT_EXPR_SYMBOL) {
        return value->symbol;
    }
    if (value->kind == SCRIPT_EXPR_DEREFERENCE) {
        return pointer_target(check, state, value->left);
    }
    if (value->kind == SCRIPT_EXPR_UNWRAP) {
        return local_target(check, state, value->left);
    }
    return -1;
}
static int pointer_target(const result_check *check, const result_state *state, int index)
{
    if (index < 0) {
        return -1;
    }
    const script_expression *value = &check->analyzer->out->expressions[index];
    if (value->kind == SCRIPT_EXPR_SYMBOL) {
        return value->symbol >= 0 ? state->targets[value->symbol] : -1;
    }
    if (value->kind == SCRIPT_EXPR_ADDRESS) {
        return local_target(check, state, value->left);
    }
    if (value->kind == SCRIPT_EXPR_DEREFERENCE) {
        int target = pointer_target(check, state, value->left);
        return target >= 0 ? state->targets[target] : -1;
    }
    if (value->kind == SCRIPT_EXPR_UNWRAP) {
        return pointer_target(check, state, value->left);
    }
    return -1;
}
static unsigned source_alias(const result_check *check, const result_state *state)
{
    const script_expression *source = &check->analyzer->out->expressions[check->source];
    if (source->task) {
        return RESULT_TASK;
    }
    unsigned value = acount(source->results) > 1 ? RESULT_TUPLE : RESULT_OPTIONAL;
    return value |
           (source->kind == SCRIPT_EXPR_JOIN && !state->success ? RESULT_AWAIT : 0);
}
static unsigned alias(const result_check *check, const result_state *state, int index)
{
    if (index < 0) {
        return 0;
    }
    const script_expression *value = &check->analyzer->out->expressions[index];
    if (index == check->source) {
        return source_alias(check, state);
    }
    if (value->kind == SCRIPT_EXPR_SYMBOL) {
        return symbol_alias(check, state, value->symbol, 0);
    }
    if (value->kind == SCRIPT_EXPR_UNWRAP || value->kind == SCRIPT_EXPR_FALLBACK) {
        return alias(check, state, value->left);
    }
    if (value->kind == SCRIPT_EXPR_MEMBER && value->member == check->member &&
        alias(check, state, value->left)) {
        return RESULT_OPTIONAL;
    }
    if (value->kind == SCRIPT_EXPR_DEREFERENCE) {
        unsigned pointer = alias(check, state, value->left);
        return pointer >= RESULT_POINTER ? pointer - RESULT_POINTER : 0;
    }
    return 0;
}
static void refine(result_check *check, result_state *state, int index, bool truth)
{
    if (index < 0 || !state->pending) {
        return;
    }
    const script_expression *value = &check->analyzer->out->expressions[index];
    if (value->kind == SCRIPT_EXPR_UNARY && value->op == SCRIPT_OP_NOT) {
        refine(check, state, value->left, !truth);
        return;
    }
    if (value->kind == SCRIPT_EXPR_BINARY &&
        (value->op == SCRIPT_OP_AND || value->op == SCRIPT_OP_OR)) {
        bool conjunction = value->op == SCRIPT_OP_AND;
        if (truth == conjunction) {
            refine(check, state, value->left, truth);
            refine(check, state, value->right, truth);
            return;
        }
        result_state other = copy_state(check, state);
        refine(check, state, value->left, truth);
        refine(check, &other, value->left, !truth);
        refine(check, &other, value->right, truth);
        state->reached = true;
        merge(check, state, &other);
        free_state(&other);
        return;
    }
    if (value->kind != SCRIPT_EXPR_IS || value->tag != PIN_UNION_CANCELLED_TAG ||
        !(alias(check, state, value->left) & RESULT_AWAIT)) {
        return;
    }
    if (truth != value->negated) {
        /* A cancelled task never produced its success outputs. */
        state->pending = false;
        return;
    }
    state->success = true;
    for (int i = 0; i < acount(check->analyzer->out->symbols); i++) {
        if (state->aliases[i] < RESULT_POINTER) {
            state->aliases[i] &= ~RESULT_AWAIT;
        }
    }
}
static void inspect(result_state *state, unsigned value)
{
    if (value < RESULT_POINTER && !(value & (RESULT_AWAIT | RESULT_TASK))) {
        acknowledge(state, value);
    }
}
static unsigned expression(result_check *check, result_state *state, int index, bool consume)
{
    if (index < 0) {
        return 0;
    }
    const script_expression *value = &check->analyzer->out->expressions[index];
    unsigned result = 0;
    if (value->kind == SCRIPT_EXPR_SYMBOL) {
        result = symbol_alias(check, state, value->symbol, 0);
    } else if (value->kind == SCRIPT_EXPR_MEMBER) {
        unsigned owner = expression(check, state, value->left, false);
        if (owner && value->member == check->member) {
            result = RESULT_OPTIONAL;
        } else if (owner & RESULT_OPTIONAL) {
            /* Reading a field of an unwrapped error is an actual use. */
            acknowledge(state, owner);
        }
    } else if (value->kind == SCRIPT_EXPR_CANCEL) {
        /* Cancellation is an explicit choice to abandon completion. This
         * acknowledges the handle, not an error from an already joined task. */
        expression(check, state, value->left, true);
        expression(check, state, value->right, true);
    } else if (value->kind == SCRIPT_EXPR_ARRAY_MUTATE) {
        /* Lowering evaluates the receiver's place once, then its items. The
         * receiver is also argument zero; do not evaluate it a second time. */
        inspect(state, expression(check, state, value->left, false));
        for (int i = 0; i < acount(value->arguments); i++) {
            if (value->arguments[i].parameter > 0) {
                expression(check, state, value->arguments[i].value, true);
            }
        }
    } else if (value->kind == SCRIPT_EXPR_CALL || value->kind == SCRIPT_EXPR_JOIN) {
        if (value->kind == SCRIPT_EXPR_JOIN) {
            expression(check, state, value->left, true);
        }
        for (int i = 0; i < acount(value->arguments); i++) {
            expression(check, state, value->arguments[i].value, true);
        }
        if (index == check->source) {
            check->visited = true;
            check->lost |= state->pending;
            memset(
                state->aliases,
                0,
                (size_t)acount(check->analyzer->out->symbols) * sizeof *state->aliases
            );
            memset(
                state->associated,
                0,
                (size_t)acount(check->analyzer->out->symbols) * sizeof *state->associated
            );
            state->pending = true;
            state->success = false;
            result = source_alias(check, state);
        }
    } else if (value->kind == SCRIPT_EXPR_UNWRAP || value->kind == SCRIPT_EXPR_FALLBACK) {
        result = expression(check, state, value->left, false);
    } else if (value->kind == SCRIPT_EXPR_BINARY && (value->op == SCRIPT_OP_AND || value->op == SCRIPT_OP_OR || value->op == SCRIPT_OP_COALESCE)) {
        expression(check, state, value->left, true);
        const script_expression *left = &check->analyzer->out->expressions[value->left];
        bool fixed = left->kind == SCRIPT_EXPR_CONSTANT && left->type.storage == PIN_BOOL;
        bool runs = value->op == SCRIPT_OP_AND ? left->constant.b
                                               : !left->constant.b;
        if (fixed && !runs) {
            return 0;
        }
        result_state skipped = copy_state(check, state);
        skipped.reached = true;
        refine(check, &skipped, value->left, value->op == SCRIPT_OP_OR);
        refine(check, state, value->left, value->op != SCRIPT_OP_OR);
        expression(check, state, value->right, true);
        if (!fixed) {
            state->reached = true;
            merge(check, state, &skipped);
        }
        free_state(&skipped);
    } else if (value->kind == SCRIPT_EXPR_SEQUENCE) {
        inspect(state, expression(check, state, value->left, false));
        inspect(state, expression(check, state, value->right, false));
        for (int i = 0; i < acount(value->arguments); i++) {
            inspect(state, expression(check, state, value->arguments[i].value, false));
        }
    } else if (
        value->kind == SCRIPT_EXPR_BINARY || value->kind == SCRIPT_EXPR_INDEX ||
        value->kind == SCRIPT_EXPR_MAP_HAS || value->kind == SCRIPT_EXPR_MAP_REMOVE
    ) {
        inspect(state, expression(check, state, value->left, false));
        inspect(state, expression(check, state, value->right, false));
    } else if (value->kind == SCRIPT_EXPR_COMPONENT || value->kind == SCRIPT_EXPR_RELATION || value->kind == SCRIPT_EXPR_QUERY_FIELD) {
        inspect(state, expression(check, state, value->left, false));
    } else if (value->kind == SCRIPT_EXPR_QUERY) {
        const script_query_description *query = &check->analyzer->out
                                                     ->descriptions[value->entry];
        for (int i = 0; i < acount(query->captures); i++) {
            /* A query retains its captured values for later row evaluation. */
            acknowledge(state, symbol_alias(check, state, query->captures[i], 0));
        }
    } else if (value->kind == SCRIPT_EXPR_ADDRESS) {
        result = expression(check, state, value->left, false);
        result = result ? result + RESULT_POINTER : 0;
    } else if (value->kind == SCRIPT_EXPR_DEREFERENCE) {
        result = expression(check, state, value->left, false);
        result = result >= RESULT_POINTER ? result - RESULT_POINTER : 0;
    } else if (value->kind == SCRIPT_EXPR_UNARY || value->kind == SCRIPT_EXPR_CAST || value->kind == SCRIPT_EXPR_IS || value->kind == SCRIPT_EXPR_AS) {
        inspect(state, expression(check, state, value->left, false));
    } else {
        /* Operators, aggregates and arguments either inspect an outcome or
         * explicitly store it. These uses acknowledge/transfer responsibility. */
        for (int i = 0; i < acount(value->arguments); i++) {
            expression(check, state, value->arguments[i].value, true);
        }
    }
    if (consume) {
        acknowledge(state, result);
    }
    return result;
}
/* Resolving a destination reads its address, not the complete tuple that
 * owns a sibling result. Reading an error payload to reach a field or element
 * still inspects that error. Index and call arguments keep their usual uses. */
static void address(result_check *check, result_state *state, int index)
{
    if (index < 0) {
        return;
    }
    const script_expression *value = &check->analyzer->out->expressions[index];
    unsigned owner = expression(check, state, value->left, false);
    if (value->kind != SCRIPT_EXPR_DEREFERENCE && (owner & RESULT_OPTIONAL)) {
        inspect(state, owner);
    }
    if (value->kind == SCRIPT_EXPR_INDEX) {
        inspect(state, expression(check, state, value->right, false));
    }
}
static void assign_alias(result_check *check, result_state *state, int symbol, unsigned value, int target)
{
    if (state->associated[symbol] && !value) {
        /* A merged pointer may reach the cell being replaced. Its cached
         * alias cannot establish that the old outcome is still there. A cell
         * may hold it on only some paths, even when its definite alias is zero.
         * Known pointers follow their target live and normalize before joins. */
        for (int i = 0; i < acount(check->analyzer->out->symbols); i++) {
            if (state->targets[i] < 0 && state->aliases[i] >= RESULT_POINTER) {
                state->aliases[i] = 0;
            }
        }
    }
    state->aliases[symbol] = value;
    state->targets[symbol] = target;
    state->associated[symbol] |= value != 0;
}
static void assign(result_check *check, result_state *state, const script_statement *statement)
{
    int target = statement->symbol;
    int tuple = -1;
    if (statement->expression >= 0) {
        const script_expression *left = &check->analyzer->out
                                             ->expressions[statement->expression];
        target = local_target(check, state, statement->expression);
        if (left->kind == SCRIPT_EXPR_MEMBER && left->member == check->member &&
            left->left >= 0) {
            int owner = local_target(check, state, left->left);
            if (owner >= 0 && (symbol_alias(check, state, owner, 0) & RESULT_TUPLE)) {
                tuple = owner;
            }
        }
    }
    address(check, state, statement->expression);
    unsigned value = expression(check, state, statement->second, false);
    if (target >= 0) {
        const script_symbol *symbol = &check->analyzer->out->symbols[target];
        if (symbol->kind == SCRIPT_LOCAL || symbol->kind == SCRIPT_RESULT) {
            assign_alias(
                check,
                state,
                target,
                value,
                pointer_target(check, state, statement->second)
            );
            return;
        }
    }
    if (tuple >= 0) {
        assign_alias(check, state, tuple, value ? RESULT_TUPLE : 0, -1);
        return;
    }
    if (statement->expression >= 0) {
        const script_expression *left = &check->analyzer->out
                                             ->expressions[statement->expression];
        if (left->kind == SCRIPT_EXPR_DEREFERENCE &&
            alias(check, state, left->left) >= RESULT_POINTER && target < 0) {
            /* A branch merged pointers with different local pointees. A write
             * through that pointer may replace any outstanding local alias.
             * Keep the obligation, rather than claim the old error survived. */
            memset(
                state->aliases,
                0,
                (size_t)acount(check->analyzer->out->symbols) * sizeof *state->aliases
            );
            return;
        }
    }
    /* A state/field/element write transfers an outcome into explicit storage. */
    acknowledge(state, value);
}
static void transfer(result_check *check, result_state *state, const result_node *current)
{
    if (current->statement < 0 || current->loop_guard) {
        return;
    }
    const script_statement *statement = &check->analyzer->out
                                             ->statements[current->statement];
    if (statement->kind == SCRIPT_STMT_DECLARE) {
        unsigned value = expression(check, state, statement->expression, false);
        assign_alias(
            check,
            state,
            statement->symbol,
            value,
            pointer_target(check, state, statement->expression)
        );
        return;
    }
    if (statement->kind == SCRIPT_STMT_ASSIGN) {
        assign(check, state, statement);
        return;
    }
    if (statement->kind == SCRIPT_STMT_RETURN) {
        for (int i = 0; i < acount(statement->children); i++) {
            assign(check, state, &check->analyzer->out->statements[statement->children[i]]);
        }
        expression(check, state, statement->expression, true);
        for (int i = 0; i < acount(check->function->results); i++) {
            acknowledge(state, symbol_alias(check, state, check->function->results[i], 0));
        }
        check->lost |= state->pending;
        return;
    }
    if (statement->kind == SCRIPT_STMT_MATCH) {
        inspect(state, expression(check, state, statement->expression, false));
        return;
    }
    if (statement->kind == SCRIPT_STMT_CASE && statement->expression >= 0 &&
        check->analyzer->out->expressions[statement->expression].kind == SCRIPT_EXPR_IS) {
        /* Lowering caches a union switch's subject; testing another arm must
         * not execute a subject call a second time. Edge refinement handles
         * cancellation without acknowledging the success payload's error. */
        return;
    }
    /* A bare call's results are dropped, not passed to another operation. */
    expression(check, state, statement->expression, statement->kind != SCRIPT_STMT_EXPRESSION);
    expression(check, state, statement->second, true);
    expression(check, state, statement->third, true);
}
static bool merge(const result_check *check, result_state *to, const result_state *from)
{
    int symbols = acount(check->analyzer->out->symbols);
    if (!to->reached) {
        to->reached = true;
        to->pending = from->pending;
        to->success = from->success;
        memcpy(to->aliases, from->aliases, (size_t)symbols * sizeof *to->aliases);
        memcpy(to->targets, from->targets, (size_t)symbols * sizeof *to->targets);
        memcpy(to->associated, from->associated, (size_t)symbols * sizeof *to->associated);
        return true;
    }
    bool changed = false;
    /* Resolve both predecessor maps before changing any target. A known
     * pointer's cached alias may predate an overwrite of its pointee. Losing
     * its target at this join must not revive that stale cached value. */
    unsigned *aliases = xmalloc((size_t)symbols * 2 * sizeof *aliases);
    unsigned *other = aliases + symbols;
    for (int i = 0; i < symbols; i++) {
        aliases[i] = symbol_alias(check, to, i, 0);
        other[i] = symbol_alias(check, from, i, 0);
    }
    for (int i = 0; i < symbols; i++) {
        int target = to->targets[i] == from->targets[i] ? to->targets[i] : -1;
        changed |= target != to->targets[i];
        to->targets[i] = target;
        changed |= aliases[i] != to->aliases[i];
        to->aliases[i] = aliases[i];
        changed |= from->associated[i] && !to->associated[i];
        to->associated[i] |= from->associated[i];
    }
    if (!from->pending) {
        free(aliases);
        return changed;
    }
    changed |= !to->pending;
    if (!to->pending) {
        to->success = from->success;
        memcpy(to->aliases, other, (size_t)symbols * sizeof *to->aliases);
    } else {
        /* An alias can acknowledge only if it names this outcome on every
         * incoming path where the outcome is still pending. */
        for (int i = 0; i < symbols; i++) {
            unsigned alias = to->aliases[i] & other[i];
            changed |= alias != to->aliases[i];
            to->aliases[i] = alias;
        }
        changed |= to->success && !from->success;
        to->success &= from->success;
    }
    free(aliases);
    to->pending = true;
    return changed;
}
static const script_catalog_capability *producer(const analyzer *a, const script_expression *call)
{
    uint64_t identity = call->capability;
    if (call->kind == SCRIPT_EXPR_JOIN && call->left >= 0) {
        identity = a->out->expressions[call->left].type.nominal;
    }
    return script_catalog_capability_by_id(a->catalog, identity);
}
static uint64_t last_member(const analyzer *a, const script_expression *call)
{
    if (call->right >= 0) {
        const script_function *function = &a->out->functions[call->right];
        return a->out->symbols[alast(function->results)].id;
    }
    const script_catalog_capability *native = producer(a, call);
    return native && native->result_count
             ? script_catalog_id(
                   "port",
                   native->results[native->result_count - 1].name
               )
             : 0;
}
static void ignored_result(analyzer *a, int source)
{
    script_diagnostic diagnostic = { .range = a->out->expressions[source].range };
    const char
        *message = a->out->expressions[source].task
                     ? "Fallible task handle is ignored on a reachable "
                       "path; await, cancel, forward, store or return it"
                     : "Trailing optional result is ignored on a reachable "
                       "path; check, report, store or return it";
    snprintf(diagnostic.message, sizeof diagnostic.message, "%s", message);
    apush(a->out->diagnostics, diagnostic);
}
static bool check_expression_result(analyzer *a, int source, int root)
{
    if (root < 0) {
        return false;
    }
    int symbols = acount(a->out->symbols);
    result_check check = { .analyzer = a,
                           .source = source,
                           .member = last_member(a, &a->out->expressions[source]) };
    result_state state = {
        .aliases = xcalloc((size_t)symbols, sizeof *state.aliases),
        .targets = xmalloc((size_t)symbols * sizeof *state.targets),
        .associated = xcalloc((size_t)symbols, sizeof *state.associated)
    };
    for (int i = 0; i < symbols; i++) {
        state.targets[i] = -1;
    }
    /* These roots either test a condition or store their resulting value.
     * Keeping the complete outcome transfers its error; projecting only the
     * success value leaves the source pending. */
    expression(&check, &state, root, true);
    if (check.lost || state.pending) {
        ignored_result(a, source);
    }
    free_state(&state);
    return check.visited;
}
enum
{
    QUERY_FALSE = 1,
    QUERY_TRUE = 2
};
static unsigned check_query_result(analyzer *a, int source, int index, bool *visited)
{
    const script_query *query = &a->out->queries[index];
    if (query->kind == SCRIPT_QUERY_VALUE) {
        *visited = check_expression_result(a, source, query->value);
        const script_expression *value = &a->out->expressions[query->value];
        return value->kind == SCRIPT_EXPR_CONSTANT && value->type.storage == PIN_BOOL
                 ? value->constant.b ? QUERY_TRUE : QUERY_FALSE
                 : QUERY_FALSE | QUERY_TRUE;
    }
    if (query->kind == SCRIPT_QUERY_NOT) {
        unsigned value = check_query_result(a, source, query->children[0], visited);
        return ((value & QUERY_TRUE) ? QUERY_FALSE : 0) |
               ((value & QUERY_FALSE) ? QUERY_TRUE : 0);
    }
    if (query->kind != SCRIPT_QUERY_ALL && query->kind != SCRIPT_QUERY_ANY) {
        return QUERY_FALSE | QUERY_TRUE;
    }
    bool any = query->kind == SCRIPT_QUERY_ANY;
    unsigned continuing = any ? QUERY_FALSE : QUERY_TRUE;
    unsigned stopped = any ? QUERY_TRUE : QUERY_FALSE;
    unsigned result = continuing;
    for (int i = 0; !*visited && (result & continuing) && i < acount(query->children);
         i++) {
        result = (result & stopped) |
                 check_query_result(a, source, query->children[i], visited);
    }
    return result;
}
static bool check_query_description_result(analyzer *a, int source, int index)
{
    const script_query_description *query = &a->out->descriptions[index];
    bool visited = false;
    for (int i = 0; !visited && i < acount(query->stages); i++) {
        const script_query_stage *stage = &query->stages[i];
        if (stage->symbol >= 0) {
            visited = check_expression_result(a, source, stage->expression);
        } else if (!(check_query_result(a, source, stage->expression, &visited) &
                     QUERY_TRUE)) {
            /* A failed guard exits the evaluator before later stages. */
            break;
        }
    }
    return visited;
}
static bool contains_expression(const script_analysis *analysis, int index, int source)
{
    if (index < 0) {
        return false;
    }
    if (index == source) {
        return true;
    }
    const script_expression *value = &analysis->expressions[index];
    if (contains_expression(analysis, value->left, source) ||
        ((value->kind != SCRIPT_EXPR_CALL && value->kind != SCRIPT_EXPR_JOIN &&
          value->kind != SCRIPT_EXPR_ARRAY_MUTATE) &&
         contains_expression(analysis, value->right, source))) {
        return true;
    }
    for (int i = 0; i < acount(value->arguments); i++) {
        if (contains_expression(analysis, value->arguments[i].value, source)) {
            return true;
        }
    }
    return false;
}
static bool contains_statement(const script_analysis *analysis, int index, int source)
{
    if (index < 0) {
        return false;
    }
    const script_statement *statement = &analysis->statements[index];
    if (contains_expression(analysis, statement->expression, source) ||
        contains_expression(analysis, statement->second, source) ||
        contains_expression(analysis, statement->third, source) ||
        contains_statement(analysis, statement->body, source) ||
        contains_statement(analysis, statement->alternative, source)) {
        return true;
    }
    for (int i = 0; i < acount(statement->children); i++) {
        if (contains_statement(analysis, statement->children[i], source)) {
            return true;
        }
    }
    return false;
}
static bool emitted_result_initializer(const script_symbol *result)
{
    return result->initializer >= 0 &&
           (result->type.storage == PIN_UNION || result->type.storage == PIN_POINTER ||
            (result->type.flags & SCRIPT_TYPE_BOXED_OPTIONAL));
}
static bool check_result(analyzer *a, const script_function *function, int source, bool *ignored)
{
    bool contains = contains_statement(a->out, function->body, source);
    for (int i = 0; !contains && i < acount(function->results); i++) {
        const script_symbol *result = &a->out->symbols[function->results[i]];
        contains = emitted_result_initializer(result) &&
                   contains_expression(a->out, result->initializer, source);
    }
    if (!contains) {
        return false;
    }
    int symbols = acount(a->out->symbols);
    result_check check = { .analyzer = a,
                           .function = function,
                           .source = source,
                           .member = last_member(a, &a->out->expressions[source]),
                           .labels = xmalloc((size_t)symbols * sizeof *check.labels) };
    for (int i = 0; i < symbols; i++) {
        check.labels[i] = -1;
    }
    int stop = node(&check, -1, false);
    int start = graph(&check, function->body, stop, stop, stop);
    for (int i = 0; i < acount(check.nodes); i++) {
        int statement = check.nodes[i].statement;
        if (statement >= 0 && a->out->statements[statement].kind == SCRIPT_STMT_GOTO) {
            int label = a->out->statements[statement].symbol;
            edge(&check, i, label >= 0 && check.labels[label] >= 0 ? check.labels[label] : stop);
        }
    }
    int count = acount(check.nodes);
    result_state *states = xcalloc((size_t)count, sizeof *states);
    for (int i = 0; i < count; i++) {
        states[i].aliases = xcalloc((size_t)symbols, sizeof *states[i].aliases);
        states[i].targets = xmalloc((size_t)symbols * sizeof *states[i].targets);
        states[i].associated = xcalloc((size_t)symbols, sizeof *states[i].associated);
        for (int j = 0; j < symbols; j++) {
            states[i].targets[j] = -1;
        }
    }
    states[start].reached = true;
    for (int i = 0; i < acount(function->results); i++) {
        int symbol = function->results[i];
        const script_symbol *result = &a->out->symbols[symbol];
        if (emitted_result_initializer(result)) {
            unsigned value = expression(&check, &states[start], result->initializer, false);
            assign_alias(
                &check,
                &states[start],
                symbol,
                value,
                pointer_target(&check, &states[start], result->initializer)
            );
        }
    }
    result_state next = copy_state(&check, &states[start]);
    int *work = NULL;
    apush(work, start);
    while (acount(work)) {
        int current = apop(work);
        next.pending = states[current].pending;
        next.success = states[current].success;
        memcpy(
            next.aliases,
            states[current].aliases,
            (size_t)symbols * sizeof *next.aliases
        );
        memcpy(
            next.targets,
            states[current].targets,
            (size_t)symbols * sizeof *next.targets
        );
        memcpy(
            next.associated,
            states[current].associated,
            (size_t)symbols * sizeof *next.associated
        );
        transfer(&check, &next, &check.nodes[current]);
        for (int i = 0; i < acount(check.nodes[current].edges); i++) {
            const result_edge *destination = &check.nodes[current].edges[i];
            result_state path = copy_state(&check, &next);
            refine(&check, &path, destination->condition, destination->truth);
            if (merge(&check, &states[destination->target], &path)) {
                apush(work, destination->target);
            }
            free_state(&path);
        }
    }
    if (check.lost || (states[stop].reached && states[stop].pending)) {
        *ignored = true;
    }
    for (int i = 0; i < count; i++) {
        free_state(&states[i]);
        afree(check.nodes[i].edges);
    }
    free(states);
    free_state(&next);
    free(check.labels);
    afree(check.nodes);
    afree(work);
    return check.visited;
}
void script_analyzer_check_results(analyzer *a)
{
    if (acount(a->out->diagnostics)) {
        return;
    }
    for (int i = 0; i < acount(a->out->functions); i++) {
        const script_function *function = &a->out->functions[i];
        if (function->fallible &&
            (!acount(function->results) ||
             !(a->out->symbols[alast(function->results)].type.flags &
               SCRIPT_TYPE_OPTIONAL))) {
            script_diagnostic diagnostic = { .range = function->range };
            snprintf(diagnostic.message, sizeof diagnostic.message, "@fallible requires a final optional error result");
            apush(a->out->diagnostics, diagnostic);
        }
    }
    if (acount(a->out->diagnostics)) {
        return;
    }
    for (int i = 0; i < acount(a->out->expressions); i++) {
        const script_expression *call = &a->out->expressions[i];
        if (call->kind != SCRIPT_EXPR_CALL && call->kind != SCRIPT_EXPR_JOIN) {
            continue;
        }
        const script_catalog_capability *native = producer(a, call);
        bool fallible = call->right >= 0 ? a->out->functions[call->right].fallible
                                         : native && native->fallible;
        if (!fallible) {
            continue;
        }
        if (call->right < 0 && (!native->result_count ||
                                !(native->results[native->result_count - 1].type.flags &
                                  SCRIPT_TYPE_OPTIONAL))) {
            script_diagnostic diagnostic = { .range = call->range };
            snprintf(diagnostic.message, sizeof diagnostic.message, "@fallible requires a final optional error result");
            apush(a->out->diagnostics, diagnostic);
            continue;
        }
        bool visited = false, ignored = false;
        for (int j = 0; j < acount(a->out->functions); j++) {
            const script_function *function = &a->out->functions[j];
            if (function->body < 0 || function->disabled) {
                continue;
            }
            /* Specializations share source ranges. Parameter defaults share
             * expression nodes across callers. Check every actual owner. */
            visited |= check_result(a, function, i, &ignored);
        }
        if (ignored) {
            ignored_result(a, i);
        }
        if (visited) {
            continue;
        }
        /* Generated query evaluators and reactive bindings execute outside
         * authored bodies. Check their semantic roots directly; lexical range
         * containment does not make them part of the surrounding body CFG. */
        for (int j = 0; !visited && j < acount(a->out->descriptions); j++) {
            visited = check_query_description_result(a, i, j);
        }
        for (int j = 0; !visited && j < acount(a->out->bindings); j++) {
            const script_binding *binding = &a->out->bindings[j];
            visited = check_expression_result(a, i, binding->target) ||
                      check_expression_result(a, i, binding->value);
        }
        for (int j = 0; !visited && j < acount(a->out->symbols); j++) {
            const script_symbol *symbol = &a->out->symbols[j];
            if (symbol->kind == SCRIPT_STATE || symbol->kind == SCRIPT_SHARED_STATE) {
                visited = check_expression_result(a, i, symbol->initializer);
            }
        }
    }
}
