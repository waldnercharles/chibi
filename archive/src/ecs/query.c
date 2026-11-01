#include "ecs/ecs_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Terms compile to a tree of nodes stored in preorder. A group's children
 * follow it; each node's end is the index after its subtree. */
typedef enum
{
    NODE_HAS, /* A component on $this itself, the common case. */
    NODE_TERM,
    NODE_ALL,
    NODE_ANY,
} node_kind;

typedef struct
{
    node_kind kind;
    bool negate, optional;
    const ecs_component *component;
    int term;
    int end;
} plan_node;

enum
{
    UP_UNKNOWN,
    UP_NONE = -1,
};

typedef struct
{
    ecs_term term;
    ecs_component *component;
    const ecs_relationship *pair;
    const ecs_relationship *trav;
    const ecs_relationship *source;
    int *up; /* Memo of each slot's nearest provider, or UP_NONE. */
    uint32_t up_count;
    bool binds; /* Binds target_var to each of the source's targets. */
    int choice;
} plan_term;

typedef struct
{
    plan_term *terms;
    plan_node *nodes;    /* nodes[0] is the root group. */
    int *top;            /* The root's operands, with plain scopes inlined. */
    plan_term **binders; /* Per top operand: its generator, or NULL. */
    int *compiled;       /* Per caller term: its plan term, or -1. */
    int skip;        /* Top operand the candidates already satisfy, or -1. */
    bool binding;    /* Some top operand is a generator. */
    uint32_t *slots; /* Cascade candidates, owned. */
    const ecs_relationship *cascade; /* What cascade orders by, or NULL. */
    ecs_entity vars[ECS_QUERY_VARS + 1];
    uint32_t this_slot;
    bool resume;
    ecs_query_stats *stats;
} query_plan;

typedef struct
{
    ecs_component_id id;
    uint64_t revision;
} query_dependency;

enum
{
    QUERY_CACHE_BYTES = 1024 * 1024
};

struct ecs_query_plan
{
    ecs_world *world;
    query_plan *plan;
    ecs_component_id components[ECS_QUERY_INLINE];
    int component_count;
    bool active;
    query_dependency *dependencies;
    uint64_t revision;
    uint32_t *rows;
    int stride, variables, row_count, row_capacity, row_cursor, needed;
    bool valid, cached, collecting, blocked, all_entities;
    ecs_query_stats stats;
};

static ecs_entity slot_entity(const ecs_world *world, uint32_t slot)
{
    return slot ? (ecs_entity){ slot, world->generations[slot] }
                : (ecs_entity){ 0, 0 };
}

static void plan_free(query_plan *plan)
{
    for (int i = 0; i < acount(plan->terms); i++) {
        free(plan->terms[i].up);
    }
    afree(plan->terms);
    afree(plan->nodes);
    afree(plan->top);
    afree(plan->binders);
    afree(plan->compiled);
    afree(plan->slots);
    free(plan);
}

/* Parsing --------------------------------------------------------------- */

typedef struct
{
    ecs_world *world;
    query_plan *plan;
    const ecs_term *terms;
    int count;
    int at;
    bool known[ECS_QUERY_VARS + 1];
} parser;

static int push_node(query_plan *plan, node_kind kind, ecs_oper oper)
{
    plan_node node = { .kind = kind,
                       .negate = oper == ECS_NOT,
                       .optional = oper == ECS_OPTIONAL,
                       .term = -1 };
    apush(plan->nodes, node);
    return acount(plan->nodes) - 1;
}

static bool var_valid(int var)
{
    return var >= 0 && var <= ECS_QUERY_VARS;
}

static const ecs_relationship *traversal(const ecs_world *world, const ecs_term *term)
{
    const ecs_relationship *relationship = world_relationship(
        world,
        term->trav_rel ? term->trav_rel : ECS_CHILD_OF
    );
    unsigned required = ECS_REL_EXCLUSIVE | ECS_REL_TRAVERSABLE;
    return relationship && (relationship->desc.traits & required) == required
             ? relationship
             : NULL;
}

static bool this_self(const ecs_term *term)
{
    return !term->src_var && !term->src.slot && !term->src_rel && term->trav == ECS_SELF;
}

/* Checks a term and records the variable it binds. top is a root AND term. */
static int compile_term(parser *p, const ecs_term *term, bool top)
{
    plan_term compiled = { .term = *term, .choice = -1 };
    compiled.component = find_component(p->world, term->id);
    compiled.pair = compiled.component ? NULL
                                       : world_relationship(p->world, term->id);
    if (!compiled.component && !compiled.pair) {
        return ECS_ERR_COMPONENT;
    }
    bool wildcard = term->target_var == ECS_WILDCARD;
    if (term->trav > ECS_SELF_UP || !var_valid(term->src_var) ||
        (!wildcard && !var_valid(term->target_var)) ||
        (compiled.component && (term->target_var || term->target.slot)) ||
        (term->cascade && (term->src_var || term->src.slot)) ||
        !p->known[term->src_var]) {
        return ECS_ERR_ARGUMENT;
    }
    if ((!term->src_var && term->src.slot && !entity_matches(p->world, term->src)) ||
        (compiled.pair && !term->target_var && !entity_matches(p->world, term->target))) {
        return ECS_ERR_ENTITY;
    }
    if (term->trav != ECS_SELF || term->cascade) {
        compiled.trav = traversal(p->world, term);
        if (!compiled.trav) {
            return ECS_ERR_ARGUMENT;
        }
    }
    if (term->src_rel) {
        compiled.source = world_relationship(p->world, term->src_rel);
        if (!compiled.source || !(compiled.source->desc.traits & ECS_REL_EXCLUSIVE)) {
            return ECS_ERR_ARGUMENT;
        }
    }
    if (term->cascade) {
        /* One depth order per query: cascades must share a relationship. */
        if (p->plan->cascade && p->plan->cascade != compiled.trav) {
            return ECS_ERR_ARGUMENT;
        }
        p->plan->cascade = compiled.trav;
    }
    if (compiled.pair && term->target_var > 0 && !p->known[term->target_var]) {
        if (!top || term->trav != ECS_SELF) {
            return ECS_ERR_ARGUMENT;
        }
        compiled.binds = true;
        p->known[term->target_var] = true;
    }
    p->plan->compiled[p->at - 1] = acount(p->plan->terms);
    apush(p->plan->terms, compiled);
    return ECS_OK;
}

static int parse_group(parser *p, int node);

/* One term, or a scope with its contents. Only root AND terms bind. */
static int parse_operand(parser *p, bool top)
{
    const ecs_term *term = &p->terms[p->at++];
    ecs_oper oper = term->oper == ECS_OR ? ECS_AND : term->oper;
    if (oper > ECS_OPTIONAL || term->kind > ECS_TERM_OPEN) {
        return ECS_ERR_ARGUMENT;
    }
    if (term->kind == ECS_TERM_OPEN) {
        return parse_group(p, push_node(p->plan, NODE_ALL, oper));
    }
    int node = push_node(p->plan, NODE_TERM, oper);
    p->plan->nodes[node].end = node + 1;
    p->plan->nodes[node].term = acount(p->plan->terms);
    int result = compile_term(p, term, top && oper == ECS_AND);
    const plan_term *compiled = result ? NULL
                                       : &p->plan->terms[p->plan->nodes[node].term];
    if (compiled && compiled->component && this_self(term)) {
        p->plan->nodes[node].kind = NODE_HAS;
        p->plan->nodes[node].component = compiled->component;
    }
    return result;
}

static bool at_or(const parser *p)
{
    return p->at < p->count && p->terms[p->at].kind != ECS_TERM_CLOSE &&
           p->terms[p->at].oper == ECS_OR;
}

/* Operands up to the scope's close, or the end for the root. Runs of OR
 * operands become one ANY node. */
static int parse_group(parser *p, int node)
{
    bool root = node == 0;
    int result = ECS_OK;
    while (!result && p->at < p->count && p->terms[p->at].kind != ECS_TERM_CLOSE) {
        if (!at_or(p)) {
            result = parse_operand(p, root);
            continue;
        }
        int chain = push_node(p->plan, NODE_ANY, ECS_AND);
        while (!result && at_or(p)) {
            result = parse_operand(p, false);
        }
        p->plan->nodes[chain].end = acount(p->plan->nodes);
    }
    if (result || root == (p->at < p->count)) {
        return result ? result : ECS_ERR_ARGUMENT;
    }
    p->at += !root;
    p->plan->nodes[node].end = acount(p->plan->nodes);
    return ECS_OK;
}

/* Adds a group's children to the top operands, inlining plain AND scopes. */
static void collect_top(query_plan *plan, int group)
{
    for (int i = group + 1; i < plan->nodes[group].end; i = plan->nodes[i].end) {
        const plan_node *node = &plan->nodes[i];
        if (node->kind == NODE_ALL && !node->negate && !node->optional) {
            collect_top(plan, i);
            continue;
        }
        plan_term *t = node->kind == NODE_TERM ? &plan->terms[node->term] : NULL;
        apush(plan->top, i);
        apush(plan->binders, t && t->binds ? t : NULL);
        plan->binding |= t && t->binds;
    }
}

/* Matching -------------------------------------------------------------- */

static uint32_t source_slot(const ecs_world *world, const query_plan *plan, const ecs_term *term)
{
    if (term->src_var) {
        return plan->vars[term->src_var].slot;
    }
    return term->src.slot ? (entity_matches(world, term->src) ? term->src.slot : 0)
                          : plan->this_slot;
}

static bool pair_holds(const plan_term *t, uint32_t slot, ecs_entity target)
{
    for (int i = 0; i < relationship_target_count(t->pair, slot); i++) {
        if (ecs_entity_to_u64(relationship_target(t->pair, slot, i)) ==
            ecs_entity_to_u64(target)) {
            return true;
        }
    }
    return false;
}

static bool id_holds(const query_plan *plan, const plan_term *t, uint32_t slot)
{
    if (t->component) {
        return component_matches(t->component, slot);
    }
    if (t->term.target_var == ECS_WILDCARD) {
        return relationship_target_count(t->pair, slot) > 0;
    }
    return pair_holds(
        t,
        slot,
        t->term.target_var ? plan->vars[t->term.target_var] : t->term.target
    );
}

/* The nearest of slot and its ancestors with the id, or zero. Memoized unless
 * the id depends on a variable. */
static uint32_t ancestor(const ecs_world *world, const query_plan *plan, plan_term *t, uint32_t slot)
{
    if (t->term.target_var > 0) {
        while (slot && !id_holds(plan, t, slot)) {
            slot = t->trav->target[slot].slot;
        }
        return slot;
    }
    if (!t->up) {
        t->up = xcalloc(world->execution.slot_count, sizeof *t->up);
        t->up_count = world->execution.slot_count;
    }
    uint32_t start = slot;
    while (slot && t->up[slot] == UP_UNKNOWN && !id_holds(plan, t, slot)) {
        slot = t->trav->target[slot].slot;
    }
    int found = !slot                     ? UP_NONE
              : t->up[slot] == UP_UNKNOWN ? (int)slot
                                          : t->up[slot];
    for (uint32_t walk = start; walk != slot; walk = t->trav->target[walk].slot) {
        t->up[walk] = found;
    }
    if (slot) {
        t->up[slot] = found;
    }
    return found == UP_NONE ? 0 : (uint32_t)found;
}

/* The slot that supplies the term's id under the current bindings, or zero. */
static uint32_t provider(const ecs_world *world, const query_plan *plan, plan_term *t)
{
    if (plan->stats) {
        plan->stats->source_resolutions++;
    }
    uint32_t slot = source_slot(world, plan, &t->term);
    if (t->source) {
        slot = t->source->target[slot].slot;
    }
    if (!slot) {
        return 0;
    }
    if (t->term.trav != ECS_UP && id_holds(plan, t, slot)) {
        return slot;
    }
    return t->term.trav == ECS_SELF
             ? 0
             : ancestor(world, plan, t, t->trav->target[slot].slot);
}

static bool node_holds(const ecs_world *, query_plan *, int index);

static bool group_holds(const ecs_world *world, query_plan *plan, int index, bool all)
{
    for (int i = index + 1; i < plan->nodes[index].end; i = plan->nodes[i].end) {
        if (node_holds(world, plan, i) != all) {
            return !all;
        }
    }
    return all;
}

static bool node_holds(const ecs_world *world, query_plan *plan, int index)
{
    if (plan->stats) {
        plan->stats->node_tests++;
    }
    const plan_node *node = &plan->nodes[index];
    bool held = false;
    switch (node->kind) {
        case NODE_HAS:
            held = component_matches(node->component, plan->this_slot);
            break;
        case NODE_TERM:
            held = provider(world, plan, &plan->terms[node->term]) != 0;
            break;
        case NODE_ALL: held = group_holds(world, plan, index, true); break;
        case NODE_ANY: held = group_holds(world, plan, index, false); break;
    }
    return node->optional || node->negate != held;
}

/* Binds the generator's variable to the source's next target. */
static bool advance(const ecs_world *world, query_plan *plan, plan_term *t)
{
    if (plan->stats) {
        plan->stats->binding_steps++;
    }
    uint32_t slot = source_slot(world, plan, &t->term);
    if (t->source) {
        slot = t->source->target[slot].slot;
    }
    bool more = ++t->choice < relationship_target_count(t->pair, slot);
    plan->vars[t->term.target_var] = more ? relationship_target(t->pair, slot, t->choice)
                                          : (ecs_entity){ 0, 0 };
    return more;
}

static bool filters_hold(const ecs_world *world, query_plan *plan)
{
    for (int k = 0; k < acount(plan->top); k++) {
        if (k != plan->skip && !node_holds(world, plan, plan->top[k])) {
            return false;
        }
    }
    return true;
}

/* Finds the next binding for this_slot, backtracking over generators. A
 * resumed search continues after the previous result. */
static bool solve(const ecs_world *world, query_plan *plan, bool resume)
{
    if (!plan->binding) {
        return !resume && filters_hold(world, plan);
    }
    int count = acount(plan->top);
    int k = resume ? count : 0;
    bool forward = !resume;
    for (;;) {
        if (forward && k == count) {
            return true;
        }
        if (forward) {
            plan_term *t = plan->binders[k];
            if (t) {
                t->choice = -1;
            }
            forward = t ? advance(world, plan, t)
                        : k == plan->skip || node_holds(world, plan, plan->top[k]);
            k += forward;
            continue;
        }
        do {
            k--;
        } while (k >= 0 && !plan->binders[k]);
        if (k < 0) {
            return false;
        }
        forward = advance(world, plan, plan->binders[k]);
        k += forward;
    }
}

/* Candidates ------------------------------------------------------------ */

static void use_component(ecs_query *query, ecs_component *component)
{
    if (component->desc.index == ECS_INDEX_DENSE) {
        compact_dense(component);
        query->candidates = component->dense;
        query->candidate_count = acount(component->dense);
        return;
    }
    query->scan = component->occupancy;
    query->scan_bits = component->occupancy[0];
    query->limit = query->world->high_water / 64u;
}

/* Sparse iteration pays for occupied rows plus scanned words. Dense lists pay
 * only for rows; a selective sparse set can therefore beat a large dense one. */
static uint64_t component_cost(const ecs_world *world, const ecs_component *component)
{
    return (uint64_t)component->count +
           (component->desc.index == ECS_INDEX_DENSE ? 0 : world->high_water / 64u + 1);
}

/* Returns -1 when the operand cannot supply candidates. */
static int64_t offered(const ecs_world *world, const query_plan *plan, int top)
{
    const plan_node *node = &plan->nodes[plan->top[top]];
    if (node->negate || node->optional || node->kind == NODE_ALL ||
        node->kind == NODE_ANY) {
        return -1;
    }
    const plan_term *t = &plan->terms[node->term];
    const ecs_component *component = find_component((ecs_world *)world, t->term.id);
    if (!this_self(&t->term) || (!component && t->term.target_var)) {
        return -1;
    }
    if (!component) {
        if (!entity_matches(world, t->term.target)) {
            return -1;
        }
        return acount(
            world_relationship(world, t->term.id)->sources[t->term.target.slot]
        );
    }
    return (int64_t)component_cost(world, component);
}

static void choose_candidates(ecs_query *query, query_plan *plan)
{
    int64_t best_count = 0;
    plan->skip = -1;
    for (int k = 0; k < acount(plan->top); k++) {
        int64_t count = offered(query->world, plan, k);
        if (count >= 0 && (plan->skip < 0 || count < best_count)) {
            plan->skip = k;
            best_count = count;
        }
    }
    if (plan->skip < 0) {
        return;
    }
    const plan_term *t = &plan->terms[plan->nodes[plan->top[plan->skip]].term];
    if (t->component) {
        use_component(query, t->component);
        return;
    }
    query->candidates = t->pair->sources[t->term.target.slot];
    query->candidate_count = acount(query->candidates);
}

/* Membership upper bounds do not assume independent component sets. A
 * negated group's bound cannot be obtained by complementing its upper bound. */
static uint64_t filter_matches(const ecs_world *world, const query_plan *plan, int index)
{
    const plan_node *node = &plan->nodes[index];
    uint64_t live = world->live_count;
    if (node->optional) {
        return live;
    }
    if (node->kind == NODE_HAS) {
        uint64_t matches = node->component->count;
        return node->negate ? live - matches : matches;
    }
    if (node->kind == NODE_TERM || node->negate) {
        return live;
    }
    uint64_t matches = node->kind == NODE_ALL ? live : 0;
    for (int i = index + 1; i < node->end; i = plan->nodes[i].end) {
        uint64_t child = filter_matches(world, plan, i);
        if (node->kind == NODE_ALL) {
            if (child < matches) {
                matches = child;
            }
        } else {
            matches += child < live - matches ? child : live - matches;
        }
    }
    return matches;
}

/* Pure tests may move freely when there are no variable generators. Prefer
 * selective component expressions before unknown source/traversal tests. */
static uint64_t filter_cost(const ecs_world *world, const query_plan *plan, int index)
{
    const plan_node *node = &plan->nodes[index];
    if (node->optional) {
        return UINT64_MAX;
    }
    if (node->kind == NODE_TERM) {
        return (uint64_t)world->live_count + 1;
    }
    return filter_matches(world, plan, index);
}

static unsigned node_variables(const query_plan *plan, int index)
{
    const plan_node *node = &plan->nodes[index];
    if (node->kind == NODE_TERM || node->kind == NODE_HAS) {
        const ecs_term *term = &plan->terms[node->term].term;
        return (term->src_var > 0 ? 1u << term->src_var : 0) |
               (term->target_var > 0 ? 1u << term->target_var : 0);
    }
    unsigned variables = 0;
    for (int i = index + 1; i < node->end; i = plan->nodes[i].end) {
        variables |= node_variables(plan, i);
    }
    return variables;
}

/* Preserve generator order (and therefore result order), but reject a partial
 * binding as soon as every variable its pure predicate needs is available. */
static void push_filters(query_plan *plan)
{
    for (int i = 0; i < acount(plan->top); i++) {
        if (plan->binders[i]) {
            continue;
        }
        int node = plan->top[i], at = 0;
        unsigned variables = node_variables(plan, node);
        for (int j = 0; j < i; j++) {
            const plan_term *binder = plan->binders[j];
            if (binder && (variables & (1u << binder->term.target_var))) {
                at = j + 1;
            }
        }
        while (at < i && !plan->binders[at]) {
            at++;
        }
        memmove(
            plan->top + at + 1,
            plan->top + at,
            (size_t)(i - at) * sizeof *plan->top
        );
        memmove(
            plan->binders + at + 1,
            plan->binders + at,
            (size_t)(i - at) * sizeof *plan->binders
        );
        plan->top[at] = node;
        plan->binders[at] = NULL;
    }
}

static void order_filters(const ecs_world *world, query_plan *plan)
{
    if (plan->binding) {
        push_filters(plan);
        return;
    }
    for (int i = 1; i < acount(plan->top); i++) {
        int node = plan->top[i], at = i;
        uint64_t cost = filter_cost(world, plan, node);
        while (at && filter_cost(world, plan, plan->top[at - 1]) > cost) {
            plan->top[at] = plan->top[at - 1];
            at--;
        }
        plan->top[at] = node;
    }
}

static inline bool next_candidate(ecs_query *query, uint32_t *out)
{
    for (;;) {
        uint32_t slot;
        if (query->candidate_count >= 0) {
            if (++query->candidate_cursor >= query->candidate_count) {
                return false;
            }
            slot = query->candidates[query->candidate_cursor];
        } else if (query->scan) {
            while (!query->scan_bits) {
                if (++query->cursor > query->limit) {
                    return false;
                }
                query->scan_bits = query->scan[query->cursor];
            }
            slot = query->cursor * 64u + (uint32_t)__builtin_ctzll(query->scan_bits);
            query->scan_bits &= query->scan_bits - 1;
        } else {
            if (++query->cursor > query->limit) {
                return false;
            }
            slot = query->cursor;
            if (!query->world->occupied[slot]) {
                continue;
            }
        }
        ecs_query_stats *stats = query->prepared ? &query->prepared->stats
                               : query->plan ? ((query_plan *)query->plan)->stats
                                             : NULL;
        if (stats) {
            stats->candidates++;
        }
        if (!query->disabled || !component_matches(query->disabled, slot)) {
            *out = slot;
            return true;
        }
    }
}

static int depth_along(const ecs_world *world, const ecs_relationship *trav, uint32_t slot)
{
    if (trav == &world->relationships[CHILD_OF_RELATIONSHIP]) {
        return world->depth[slot];
    }
    int depth = 0;
    for (slot = trav->target[slot].slot; slot; slot = trav->target[slot].slot) {
        depth++;
    }
    return depth;
}

/* Materializes the candidates ordered by depth, keeping their order within
 * each depth. */
static void cascade(ecs_query *query, query_plan *plan, const ecs_relationship *trav)
{
    uint32_t *found = NULL;
    int *depths = NULL;
    int deepest = 0;
    uint32_t slot;
    while (next_candidate(query, &slot)) {
        int depth = depth_along(query->world, trav, slot);
        apush(found, slot);
        apush(depths, depth);
        deepest = depth > deepest ? depth : deepest;
    }
    int *starts = xcalloc((size_t)deepest + 2, sizeof *starts);
    for (int i = 0; i < acount(found); i++) {
        starts[depths[i] + 1]++;
    }
    for (int depth = 1; depth <= deepest; depth++) {
        starts[depth] += starts[depth - 1];
    }
    asetlen(plan->slots, acount(found));
    for (int i = 0; i < acount(found); i++) {
        plan->slots[starts[depths[i]]++] = found[i];
    }
    free(starts);
    afree(depths);
    afree(found);
    query->candidates = plan->slots;
    query->candidate_count = acount(plan->slots);
    query->candidate_cursor = -1;
    query->scan = NULL;
    query->disabled = NULL;
}

/* Begin ----------------------------------------------------------------- */

static void reset(ecs_query *query, ecs_world *world)
{
    memset(query, 0, sizeof *query);
    query->world = world;
    query->candidate_count = -1;
    query->candidate_cursor = -1;
    query->candidate_ref = -1;
    query->limit = world->high_water;
}

/* Components beyond the inline limit go through the planner. */
static int begin_planned(ecs_world *world, ecs_query *query, const ecs_component_id *components, int count)
{
    ecs_term *terms = xcalloc((size_t)count, sizeof *terms);
    for (int i = 0; i < count; i++) {
        terms[i].id = components[i];
    }
    int result = ecs_query_begin_terms(world, query, terms, count);
    free(terms);
    return result;
}

int ecs_query_begin(ecs_world *world, ecs_query *query, const ecs_component_id *components, int component_count)
{
    if (!world || !query || component_count < 0 ||
        (component_count && !components) || world->iteration_depth) {
        return ECS_ERR_ARGUMENT;
    }
    if (component_count > ECS_QUERY_INLINE) {
        return begin_planned(world, query, components, component_count);
    }
    reset(query, world);
    bool named_disabled = false;
    ecs_component *best = NULL;
    for (int i = 0; i < component_count; i++) {
        ecs_component *component = find_component(world, components[i]);
        if (!component) {
            memset(query, 0, sizeof *query);
            return ECS_ERR_COMPONENT;
        }
        query->refs[i] = component;
        named_disabled |= components[i] == ECS_DISABLED;
        if (!best || component_cost(world, component) < component_cost(world, best)) {
            best = component;
            query->candidate_ref = i;
        }
    }
    query->ref_count = component_count;
    if (best) {
        use_component(query, best);
    }
    query->disabled = skipped_disabled(world, named_disabled);
    world->iteration_depth++;
    return ECS_OK;
}

/* Plain AND components on $this take the inline path. */
static bool inline_terms(const ecs_term *terms, int count, ecs_component_id *components)
{
    if (count > ECS_QUERY_INLINE) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        const ecs_term *term = &terms[i];
        if (term->kind != ECS_TERM_ID || term->oper != ECS_AND || term->target_var ||
            term->target.slot || !this_self(term) || term->cascade) {
            return false;
        }
        components[i] = term->id;
    }
    return true;
}

static void finish_plan(ecs_world *world, ecs_query *query, query_plan *plan)
{
    reset(query, world);
    query->plan = plan;
    order_filters(world, plan);
    choose_candidates(query, plan);
    bool named_disabled = false;
    for (int i = 0; i < acount(plan->terms); i++) {
        named_disabled |= plan->terms[i].term.id == ECS_DISABLED;
    }
    query->disabled = skipped_disabled(world, named_disabled);
    if (plan->cascade) {
        cascade(query, plan, plan->cascade);
    }
}

int ecs_query_begin_terms(ecs_world *world, ecs_query *query, const ecs_term *terms, int term_count)
{
    if (!world || !query || term_count < 0 || (term_count && !terms) ||
        world->iteration_depth) {
        return ECS_ERR_ARGUMENT;
    }
    ecs_component_id components[ECS_QUERY_INLINE];
    if (inline_terms(terms, term_count, components)) {
        return ecs_query_begin(world, query, components, term_count);
    }
    query_plan *plan = xcalloc(1, sizeof *plan);
    asetlen(plan->compiled, term_count);
    memset(plan->compiled, 0xff, (size_t)term_count * sizeof *plan->compiled);
    parser p = { .world = world, .plan = plan, .terms = terms, .count = term_count };
    p.known[0] = true;
    push_node(plan, NODE_ALL, ECS_AND);
    int result = parse_group(&p, 0);
    if (result) {
        plan_free(plan);
        memset(query, 0, sizeof *query);
        return result;
    }
    collect_top(plan, 0);
    finish_plan(world, query, plan);
    world->iteration_depth++;
    return ECS_OK;
}

static uint64_t dependency_revision(ecs_world *world, ecs_component_id id)
{
    const ecs_component *component = find_component(world, id);
    if (component) {
        return component->query_revision;
    }
    const ecs_relationship *relationship = world_relationship(world, id);
    assert(relationship);
    return relationship->query_revision;
}

static void cache_dependency(ecs_query_plan *prepared, ecs_component_id id)
{
    for (int i = 0; i < acount(prepared->dependencies); i++) {
        if (prepared->dependencies[i].id == id) {
            return;
        }
    }
    apush(prepared->dependencies, ((query_dependency){ id, 0 }));
}

static bool cache_shape_changed(ecs_query_plan *prepared)
{
    uint64_t revision = prepared->world->query_revision;
    bool changed = prepared->all_entities &&
                   (prepared->revision != revision || revision == UINT64_MAX);
    prepared->revision = revision;
    for (int i = 0; i < acount(prepared->dependencies); i++) {
        query_dependency *dependency = &prepared->dependencies[i];
        revision = dependency_revision(prepared->world, dependency->id);
        changed |= dependency->revision != revision || revision == UINT64_MAX;
        dependency->revision = revision;
    }
    return changed;
}

static bool cache_worthwhile(const ecs_query_plan *prepared)
{
    return prepared->plan || prepared->component_count != 1 ||
           (prepared->components[0] != ECS_DISABLED &&
            prepared->world->components[DISABLED_COMPONENT].count);
}

/* Capacity is prepared outside iteration. Unknown multi-target cardinality
 * may need one counting pass, then a larger reservation at the next begin. */
static void cache_reserve(ecs_query_plan *prepared)
{
    size_t limit = QUERY_CACHE_BYTES / sizeof *prepared->rows / (size_t)prepared->stride;
    size_t count = prepared->world->live_count;
    query_plan *plan = prepared->plan;
    for (int i = 0; i < prepared->component_count; i++) {
        size_t matches = (size_t)find_component(prepared->world, prepared->components[i])
                             ->count;
        count = matches < count ? matches : count;
    }
    for (int i = 0; plan && !plan->binding && i < acount(plan->top); i++) {
        if (offered(prepared->world, plan, i) < 0) {
            continue;
        }
        const plan_term *term = &plan->terms[plan->nodes[plan->top[i]].term];
        const ecs_component
            *component = find_component(prepared->world, term->term.id);
        size_t matches = component
                           ? (size_t)component->count
                           : (size_t)acount(
                                 world_relationship(prepared->world, term->term.id)
                                     ->sources[term->term.target.slot]
                             );
        count = matches < count ? matches : count;
    }
    if (count < (size_t)prepared->needed) {
        count = prepared->needed;
    }
    count = count < limit ? count : limit;
    if (count <= (size_t)prepared->row_capacity) {
        return;
    }
    prepared->rows = xrealloc(
        prepared->rows,
        count * (size_t)prepared->stride * sizeof *prepared->rows
    );
    prepared->row_capacity = (int)count;
}

static void cache_setup(ecs_query_plan *prepared)
{
    cache_dependency(prepared, ECS_DISABLED);
    query_plan *plan = prepared->plan;
    prepared->all_entities = plan || !prepared->component_count;
    for (int i = 0; plan && i < acount(plan->top); i++) {
        const plan_node *node = &plan->nodes[plan->top[i]];
        if (!node->negate && !node->optional &&
            (node->kind == NODE_HAS || node->kind == NODE_TERM) &&
            this_self(&plan->terms[node->term].term)) {
            /* An unrelated entity without this required component/pair cannot
             * enter. Removing a participating entity changes that dependency. */
            prepared->all_entities = false;
            break;
        }
    }
    for (int i = 0; plan && i < acount(plan->terms); i++) {
        const ecs_term *term = &plan->terms[i].term;
        cache_dependency(prepared, term->id);
        if (term->src_rel) {
            cache_dependency(prepared, term->src_rel);
        }
        if (term->trav != ECS_SELF || term->cascade) {
            cache_dependency(prepared, term->trav_rel ? term->trav_rel : ECS_CHILD_OF);
        }
        if (term->target_var > prepared->variables) {
            prepared->variables = term->target_var;
        }
    }
    for (int i = 0; i < prepared->component_count; i++) {
        cache_dependency(prepared, prepared->components[i]);
    }
    prepared->stride = 1 + prepared->variables + (plan ? acount(plan->terms) : 0);
    if (plan) {
        plan->stats = &prepared->stats;
    }
    cache_shape_changed(prepared);
    if (cache_worthwhile(prepared)) {
        cache_reserve(prepared);
    }
}

static void cache_begin(ecs_query_plan *prepared)
{
    if (cache_shape_changed(prepared)) {
        prepared->valid = prepared->blocked = false;
        prepared->needed = 0;
    }
    prepared->cached = prepared->valid;
    prepared->collecting = false;
    prepared->row_cursor = -1;
    if (prepared->cached) {
        prepared->stats.cache_hits++;
        return;
    }
    if (prepared->blocked || !cache_worthwhile(prepared)) {
        return;
    }
    cache_reserve(prepared);
    prepared->needed = prepared->row_count = 0;
    prepared->collecting = true;
    prepared->stats.cache_builds++;
}

static void cache_row(ecs_query *query)
{
    ecs_query_plan *prepared = query->prepared;
    if (!prepared || prepared->blocked || !cache_worthwhile(prepared)) {
        return;
    }
    size_t limit = QUERY_CACHE_BYTES / sizeof *prepared->rows / (size_t)prepared->stride;
    if ((size_t)prepared->needed <= limit) {
        prepared->needed++;
    }
    if (prepared->needed > prepared->row_capacity) {
        prepared->collecting = false;
        return;
    }
    if (!prepared->collecting) {
        return;
    }
    uint32_t *row = prepared->rows + (size_t)prepared->row_count++ * prepared->stride;
    *row++ = query->current.slot;
    query_plan *plan = prepared->plan;
    for (int var = 1; var <= prepared->variables; var++) {
        *row++ = plan->vars[var].slot;
    }
    for (int i = 0; plan && i < acount(plan->terms); i++) {
        *row++ = provider(query->world, plan, &plan->terms[i]);
    }
}

static void cache_complete(ecs_query_plan *prepared)
{
    if (!prepared || prepared->blocked || prepared->valid) {
        return;
    }
    prepared->valid = prepared->collecting;
    size_t limit = QUERY_CACHE_BYTES / sizeof *prepared->rows / (size_t)prepared->stride;
    if ((size_t)prepared->needed > limit) {
        prepared->blocked = true;
        prepared->stats.cache_overflows++;
        free(prepared->rows);
        prepared->rows = NULL;
        prepared->row_capacity = prepared->row_count = 0;
    }
}

int ecs_query_plan_new(ecs_world *world, const ecs_term *terms, int count, ecs_query_plan **out)
{
    if (!world || !out || count < 0 || (count && !terms) || world->iteration_depth) {
        return ECS_ERR_ARGUMENT;
    }
    *out = NULL;
    ecs_component_id components[ECS_QUERY_INLINE];
    if (inline_terms(terms, count, components)) {
        for (int i = 0; i < count; i++) {
            if (!find_component(world, components[i])) {
                return ECS_ERR_COMPONENT;
            }
        }
        ecs_query_plan *prepared = xcalloc(1, sizeof *prepared);
        prepared->world = world;
        prepared->component_count = count;
        memcpy(prepared->components, components, (size_t)count * sizeof *components);
        cache_setup(prepared);
        *out = prepared;
        return ECS_OK;
    }
    query_plan *plan = xcalloc(1, sizeof *plan);
    asetlen(plan->compiled, count);
    memset(plan->compiled, 0xff, (size_t)count * sizeof *plan->compiled);
    parser p = { .world = world, .plan = plan, .terms = terms, .count = count };
    p.known[0] = true;
    push_node(plan, NODE_ALL, ECS_AND);
    int result = parse_group(&p, 0);
    if (result) {
        plan_free(plan);
        return result;
    }
    collect_top(plan, 0);
    ecs_query_plan *prepared = xcalloc(1, sizeof *prepared);
    prepared->world = world;
    prepared->plan = plan;
    cache_setup(prepared);
    *out = prepared;
    return ECS_OK;
}

void ecs_query_plan_free(ecs_query_plan *prepared)
{
    if (!prepared) {
        return;
    }
    assert(!prepared->active);
    if (prepared->plan) {
        plan_free(prepared->plan);
    }
    afree(prepared->dependencies);
    free(prepared->rows);
    free(prepared);
}

ecs_query_stats ecs_query_plan_stats(const ecs_query_plan *prepared)
{
    assert(prepared);
    ecs_query_stats stats = prepared->stats;
    stats.retained_bytes = (size_t)prepared->row_capacity * prepared->stride *
                           sizeof *prepared->rows;
    stats.cached_rows = prepared->valid ? prepared->row_count : 0;
    return stats;
}

int ecs_query_begin_plan(ecs_query_plan *prepared, ecs_query *query)
{
    if (!prepared || !query || prepared->active || prepared->world->iteration_depth) {
        return ECS_ERR_ARGUMENT;
    }
    ecs_world *world = prepared->world;
    query_plan *plan = prepared->plan;
    cache_begin(prepared);
    if (prepared->cached) {
        reset(query, world);
        query->plan = plan;
        query->ref_count = prepared->component_count;
        query->prepared = prepared;
        prepared->active = true;
        world->iteration_depth++;
        return ECS_OK;
    }
    if (!plan) {
        int status = ecs_query_begin(
            world,
            query,
            prepared->components,
            prepared->component_count
        );
        if (!status) {
            query->prepared = prepared;
            prepared->active = true;
        }
        return status;
    }
    /* Registration can relocate component/relationship tables. The plan owns
     * identities, never persistent pointers into those tables. */
    plan->cascade = NULL;
    for (int i = 0; i < acount(plan->terms); i++) {
        plan_term *t = &plan->terms[i];
        t->component = find_component(world, t->term.id);
        t->pair = t->component ? NULL : world_relationship(world, t->term.id);
        t->source = t->term.src_rel ? world_relationship(world, t->term.src_rel) : NULL;
        t->trav = t->term.trav != ECS_SELF || t->term.cascade
                    ? traversal(world, &t->term)
                    : NULL;
        if (t->term.cascade) {
            plan->cascade = t->trav;
        }
        if (t->up) {
            if (t->up_count != world->execution.slot_count) {
                t->up_count = world->execution.slot_count;
                t->up = xrealloc(t->up, (size_t)t->up_count * sizeof *t->up);
            }
            memset(t->up, 0, (size_t)t->up_count * sizeof *t->up);
        }
        t->choice = -1;
    }
    for (int i = 0; i < acount(plan->nodes); i++) {
        plan_node *node = &plan->nodes[i];
        if (node->kind == NODE_HAS) {
            node->component = plan->terms[node->term].component;
        }
    }
    memset(plan->vars, 0, sizeof plan->vars);
    plan->resume = false;
    finish_plan(world, query, plan);
    query->prepared = prepared;
    prepared->active = true;
    world->iteration_depth++;
    return ECS_OK;
}

/* Iteration ------------------------------------------------------------- */

static bool inline_holds(const ecs_query *query, uint32_t slot)
{
    for (int i = 0; i < query->ref_count; i++) {
        if (query->prepared && i != query->candidate_ref) {
            query->prepared->stats.node_tests++;
        }
        if (i != query->candidate_ref && !component_matches(query->refs[i], slot)) {
            return false;
        }
    }
    return true;
}

/* Kept out of line so the inline path stays small. */
__attribute__((noinline)) static bool plan_next(ecs_query *query, query_plan *plan, uint32_t *slot)
{
    if (plan->resume && solve(query->world, plan, true)) {
        *slot = plan->this_slot;
        return true;
    }
    while (next_candidate(query, slot)) {
        plan->this_slot = *slot;
        if (solve(query->world, plan, false)) {
            return true;
        }
    }
    return false;
}

int ecs_query_next(ecs_query *query, ecs_entity *out_entity)
{
    if (!query || !query->world || !out_entity) {
        return ECS_ERR_ARGUMENT;
    }
    ecs_query_plan *prepared = query->prepared;
    if (prepared && prepared->cached) {
        if (prepared->row_cursor + 1 >= prepared->row_count) {
            query->current = (ecs_entity){ 0, 0 };
            return 0;
        }
        const uint32_t *row = prepared->rows +
                              (size_t)++prepared->row_cursor * prepared->stride;
        query->current = slot_entity(query->world, row[0]);
        *out_entity = query->current;
        return 1;
    }
    query_plan *plan = query->plan;
    uint32_t slot;
    bool found = false;
    if (plan) {
        found = plan->resume = plan_next(query, plan, &slot);
    } else {
        while (!found && next_candidate(query, &slot)) {
            found = inline_holds(query, slot);
        }
    }
    if (!found) {
        cache_complete(prepared);
        query->current = (ecs_entity){ 0, 0 };
        return 0;
    }
    ecs_entity entity = { slot, query->world->generations[slot] };
    query->current = entity;
    cache_row(query);
    *out_entity = entity;
    return 1;
}

ecs_entity ecs_query_source(
    const ecs_world *world,
    ecs_entity entity,
    ecs_component_id component,
    ecs_component_id relationship,
    ecs_trav trav
)
{
    if (!world || !entity_matches(world, entity) || trav > ECS_SELF_UP) {
        return (ecs_entity){ 0, 0 };
    }
    const ecs_component *column = find_component((ecs_world *)world, component);
    const ecs_relationship *rel = relationship
                                    ? world_relationship(world, relationship)
                                    : NULL;
    unsigned required = ECS_REL_EXCLUSIVE |
                        (trav == ECS_SELF ? 0 : ECS_REL_TRAVERSABLE);
    if (!column ||
        (relationship && (!rel || (rel->desc.traits & required) != required)) ||
        (trav != ECS_SELF && !relationship)) {
        return (ecs_entity){ 0, 0 };
    }
    uint32_t slot = entity.slot;
    if (trav == ECS_UP || (trav == ECS_SELF && rel)) {
        slot = rel->target[slot].slot;
    }
    while (slot) {
        if (component_matches(column, slot)) {
            return slot_entity(world, slot);
        }
        if (trav == ECS_SELF) {
            break;
        }
        slot = rel->target[slot].slot;
    }
    return (ecs_entity){ 0, 0 };
}

ecs_entity ecs_query_var(const ecs_query *query, int var)
{
    assert(query && var > 0 && var <= ECS_QUERY_VARS);
    const ecs_query_plan *prepared = query->prepared;
    if (prepared && prepared->cached) {
        if (!query->current.slot || var > prepared->variables) {
            return (ecs_entity){ 0, 0 };
        }
        const uint32_t *row = prepared->rows +
                              (size_t)prepared->row_cursor * prepared->stride;
        return slot_entity(query->world, row[var]);
    }
    const query_plan *plan = query->plan;
    return plan ? plan->vars[var] : (ecs_entity){ 0, 0 };
}

ecs_entity ecs_query_src(const ecs_query *query, int term)
{
    assert(query && term >= 0 && query->current.slot);
    query_plan *plan = query->plan;
    if (!plan) {
        assert(term < query->ref_count);
        return query->current;
    }
    assert(term < acount(plan->compiled) && plan->compiled[term] >= 0);
    const ecs_query_plan *prepared = query->prepared;
    if (prepared && prepared->cached) {
        const uint32_t *row = prepared->rows +
                              (size_t)prepared->row_cursor * prepared->stride;
        return slot_entity(query->world, row[1 + prepared->variables + plan->compiled[term]]);
    }
    return slot_entity(
        query->world,
        provider(query->world, plan, &plan->terms[plan->compiled[term]])
    );
}

void ecs_query_end(ecs_query *query)
{
    if (!query || !query->world) {
        return;
    }
    assert(query->world->iteration_depth > 0);
    query->world->iteration_depth--;
    if (query->prepared) {
        query->prepared->active = false;
    } else if (query->plan) {
        plan_free(query->plan);
    }
    memset(query, 0, sizeof *query);
}
