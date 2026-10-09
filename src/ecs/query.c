#include "ecs/ecs_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
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
    int *up;    /* Memo of each slot's nearest provider, or UP_NONE. */
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
    ecs_entity vars[ECS_QUERY_VARS + 1];
    uint32_t this_slot;
    bool resume;
} query_plan;

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
    return !term->src_var && !term->src.slot && term->trav == ECS_SELF;
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

static uint32_t source_slot(const query_plan *plan, const ecs_term *term)
{
    if (term->src_var) {
        return plan->vars[term->src_var].slot;
    }
    return term->src.slot ? term->src.slot : plan->this_slot;
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
    uint32_t slot = source_slot(plan, &t->term);
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
static bool advance(query_plan *plan, plan_term *t)
{
    uint32_t slot = source_slot(plan, &t->term);
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
            forward = t ? advance(plan, t)
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
        forward = advance(plan, plan->binders[k]);
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

/* Ranks the candidates a top operand offers the way the inline path does:
 * lists (dense components and pair sources) before bitsets, then fewer
 * first. Returns -1 when the operand cannot supply candidates. */
static int64_t offered(const query_plan *plan, int top)
{
    const plan_node *node = &plan->nodes[plan->top[top]];
    if (node->negate || node->optional || node->kind == NODE_ALL ||
        node->kind == NODE_ANY) {
        return -1;
    }
    const plan_term *t = &plan->terms[node->term];
    if (!this_self(&t->term) || (t->pair && t->term.target_var)) {
        return -1;
    }
    if (t->pair) {
        return acount(t->pair->sources[t->term.target.slot]);
    }
    int count = t->component->count;
    return t->component->desc.index == ECS_INDEX_DENSE ? count
                                                       : (int64_t)INT_MAX + count;
}

static void choose_candidates(ecs_query *query, query_plan *plan)
{
    int64_t best_count = 0;
    plan->skip = -1;
    for (int k = 0; k < acount(plan->top); k++) {
        int64_t count = offered(plan, k);
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
        bool dense = component->desc.index == ECS_INDEX_DENSE;
        bool best_dense = best && best->desc.index == ECS_INDEX_DENSE;
        if (!best || dense > best_dense ||
            (dense == best_dense && component->count < best->count)) {
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
    collect_top(plan, 0);
    reset(query, world);
    query->plan = plan;
    choose_candidates(query, plan);
    bool named_disabled = false;
    const ecs_relationship *ordered = NULL;
    for (int i = 0; i < acount(plan->terms); i++) {
        named_disabled |= plan->terms[i].term.id == ECS_DISABLED;
        ordered = plan->terms[i].term.cascade ? plan->terms[i].trav : ordered;
    }
    query->disabled = skipped_disabled(world, named_disabled);
    if (ordered) {
        cascade(query, plan, ordered);
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
    finish_plan(world, query, plan);
    world->iteration_depth++;
    return ECS_OK;
}

/* Iteration ------------------------------------------------------------- */

static bool inline_holds(const ecs_query *query, uint32_t slot)
{
    for (int i = 0; i < query->ref_count; i++) {
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
        query->current = (ecs_entity){ 0, 0 };
        return 0;
    }
    ecs_entity entity = { slot, query->world->generations[slot] };
    query->current = entity;
    *out_entity = entity;
    return 1;
}

ecs_entity ecs_query_var(const ecs_query *query, int var)
{
    assert(query && var > 0 && var <= ECS_QUERY_VARS);
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
    if (query->plan) {
        plan_free(query->plan);
    }
    memset(query, 0, sizeof *query);
}
