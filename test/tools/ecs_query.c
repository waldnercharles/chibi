#include "ecs/ecs.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static const type_field value_fields[] = {
    { .name = "value", .type = PIN_I32, .offset = 0, .size = sizeof(int), .type_name = "int" },
};
static const type_desc value_type = {
    .name = "sdk.ecs.query_value",
    .size = sizeof(int),
    .fields = value_fields,
    .field_count = 1,
};
static const ecs_component_desc position_desc = {
    .id = 901,
    .key = "sdk.query.position",
    .name = "Position",
    .element_size = sizeof(int),
    .type = &value_type,
    .index = ECS_INDEX_DENSE,
};
static const ecs_component_desc team_desc = {
    .id = 902,
    .key = "sdk.query.team",
    .name = "Team",
    .element_size = sizeof(int),
    .type = &value_type,
};
static const ecs_component_desc frozen_desc = {
    .id = 903,
    .key = "sdk.query.frozen",
    .name = "Frozen",
    .element_size = sizeof(int),
    .type = &value_type,
};
static const ecs_relationship_desc likes_desc = { .id = 951,
                                                  .key = "sdk.query.likes",
                                                  .name = "Likes" };
static const ecs_relationship_desc owner_desc = {
    .id = 952,
    .key = "sdk.query.owner",
    .name = "Owner",
    .traits = ECS_REL_EXCLUSIVE,
};
static const ecs_relationship_desc contained_in_desc = {
    .id = 953,
    .key = "sdk.query.contained_in",
    .name = "ContainedIn",
    .traits = ECS_REL_EXCLUSIVE | ECS_REL_TRAVERSABLE,
};

static ecs_world *world;

static void prepare_world()
{
    world = ecs_world_new();
    assert(ecs_world_register_component(world, &position_desc, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &team_desc, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &frozen_desc, NULL) == ECS_OK);
    assert(ecs_world_register_relationship(world, &likes_desc, NULL) == ECS_OK);
    assert(ecs_world_register_relationship(world, &owner_desc, NULL) == ECS_OK);
    assert(ecs_world_register_relationship(world, &contained_in_desc, NULL) == ECS_OK);
}

static void release_world()
{
    ecs_world_free(world);
    world = NULL;
}

static ecs_entity spawn()
{
    ecs_entity entity = ecs_world_create_entity(world);
    assert(entity.slot);
    return entity;
}

static void add(ecs_entity entity, const ecs_component_desc *component)
{
    int result = ecs_world_record_add(world, entity, component->id, NULL, 0);
    assert(result == ECS_OK);
    (void)result;
}

static void pair(ecs_entity entity, ecs_component_id relationship, ecs_entity target)
{
    int result = ecs_world_record_add_pair(world, entity, relationship, target, 0);
    assert(result == ECS_OK);
    (void)result;
}

static void apply()
{
    int result = ecs_world_apply_commands(world);
    assert(result == ECS_OK);
    (void)result;
}

static bool same(ecs_entity left, ecs_entity right)
{
    return ecs_entity_to_u64(left) == ecs_entity_to_u64(right);
}

/* Collects up to capacity results as slots and returns the result count. */
static int collect(const ecs_term *terms, int count, uint32_t *slots, int capacity)
{
    ecs_query query;
    int result = ecs_query_begin_terms(world, &query, terms, count);
    assert(result == ECS_OK);
    (void)result;
    int found = 0;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        if (found < capacity) {
            slots[found] = entity.slot;
        }
        found++;
    }
    ecs_query_end(&query);
    return found;
}

static int collect_plan(ecs_query_plan *plan, uint32_t *slots, int capacity)
{
    ecs_query query;
    int result = ecs_query_begin_plan(plan, &query);
    assert(result == ECS_OK);
    (void)result;
    int found = 0;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        if (found < capacity) {
            slots[found] = entity.slot;
        }
        found++;
    }
    ecs_query_end(&query);
    return found;
}

TEST_CASE(ecs_pair_terms_match_targets_wildcards_and_negation)
{
    ecs_entity alice = spawn(), bob = spawn(), carol = spawn(), dave = spawn();
    pair(alice, likes_desc.id, bob);
    pair(carol, likes_desc.id, bob);
    pair(carol, likes_desc.id, dave);
    pair(dave, likes_desc.id, alice);
    add(carol, &position_desc);
    apply();
    uint32_t slots[4];
    ecs_term likes_bob[] = { { .id = likes_desc.id, .target = bob } };
    REQUIRE(collect(likes_bob, 1, slots, 4) == 2);
    REQUIRE(slots[0] == alice.slot && slots[1] == carol.slot);
    ecs_term likes_anyone[] = { { .id = likes_desc.id, .target_var = ECS_WILDCARD } };
    REQUIRE(collect(likes_anyone, 1, slots, 4) == 3);
    ecs_term positioned_fans[] = {
        { .id = position_desc.id },
        { .id = likes_desc.id, .target = bob },
        { .oper = ECS_NOT, .id = likes_desc.id, .target = alice },
    };
    REQUIRE(collect(positioned_fans, 3, slots, 4) == 1 && slots[0] == carol.slot);
    ecs_term likes_nobody[] = {
        { .oper = ECS_NOT, .id = likes_desc.id, .target_var = ECS_WILDCARD }
    };
    REQUIRE(collect(likes_nobody, 1, slots, 4) == 1 && slots[0] == bob.slot);
    return true;
}

TEST_CASE(ecs_variables_backtrack_over_each_target)
{
    ecs_entity alice = spawn(), bob = spawn(), carol = spawn(), dave = spawn();
    pair(alice, likes_desc.id, bob);
    pair(alice, likes_desc.id, carol);
    pair(alice, likes_desc.id, dave);
    pair(dave, likes_desc.id, carol);
    add(bob, &team_desc);
    add(carol, &team_desc);
    pair(bob, owner_desc.id, carol);
    pair(carol, owner_desc.id, carol);
    apply();
    /* $this likes $x, $x has Team, and $x is owned by carol. */
    ecs_term terms[] = {
        { .id = likes_desc.id, .target_var = 1 },
        { .id = team_desc.id, .src_var = 1 },
        { .id = owner_desc.id, .src_var = 1, .target = carol },
    };
    ecs_query query;
    REQUIRE(ecs_query_begin_terms(world, &query, terms, 3) == ECS_OK);
    ecs_entity found;
    REQUIRE(ecs_query_next(&query, &found) && same(found, alice));
    REQUIRE(same(ecs_query_var(&query, 1), bob) && same(ecs_query_src(&query, 1), bob));
    REQUIRE(same(ecs_query_src(&query, 0), alice));
    REQUIRE(ecs_query_next(&query, &found) && same(found, alice));
    REQUIRE(same(ecs_query_var(&query, 1), carol));
    REQUIRE(ecs_query_next(&query, &found) && same(found, dave));
    REQUIRE(same(ecs_query_var(&query, 1), carol));
    REQUIRE(!ecs_query_next(&query, &found));
    ecs_query_end(&query);
    /* A second variable bound through the first. */
    ecs_term chain[] = {
        { .id = likes_desc.id, .target_var = 1 },
        { .id = owner_desc.id, .src_var = 1, .target_var = 2 },
        { .id = likes_desc.id, .src_var = 2, .target_var = ECS_WILDCARD, .oper = ECS_NOT },
    };
    uint32_t slots[4];
    REQUIRE(collect(chain, 3, slots, 4) == 3);
    return true;
}

TEST_CASE(ecs_up_terms_find_the_nearest_ancestor)
{
    ecs_entity root = spawn(), middle = spawn(), leaf = spawn(), other = spawn();
    pair(middle, ECS_CHILD_OF, root);
    pair(leaf, ECS_CHILD_OF, middle);
    add(root, &team_desc);
    add(middle, &team_desc);
    add(root, &position_desc);
    add(leaf, &position_desc);
    add(other, &position_desc);
    apply();
    ecs_term inherited[] = {
        { .id = position_desc.id },
        { .id = team_desc.id, .trav = ECS_UP },
    };
    ecs_query query;
    REQUIRE(ecs_query_begin_terms(world, &query, inherited, 2) == ECS_OK);
    ecs_entity found;
    REQUIRE(ecs_query_next(&query, &found) && same(found, leaf));
    REQUIRE(same(ecs_query_src(&query, 1), middle));
    REQUIRE(!ecs_query_next(&query, &found));
    ecs_query_end(&query);
    uint32_t slots[4];
    ecs_term self_or_up[] = {
        { .id = position_desc.id },
        { .id = team_desc.id, .trav = ECS_SELF_UP },
    };
    REQUIRE(collect(self_or_up, 2, slots, 4) == 2);
    REQUIRE(slots[0] == root.slot && slots[1] == leaf.slot);
    ecs_term orphans[] = {
        { .id = position_desc.id },
        { .oper = ECS_NOT, .id = team_desc.id, .trav = ECS_SELF_UP },
    };
    REQUIRE(collect(orphans, 2, slots, 4) == 1 && slots[0] == other.slot);
    ecs_term bad_traversal[] = {
        { .id = team_desc.id, .trav = ECS_UP, .trav_rel = likes_desc.id }
    };
    REQUIRE(ecs_query_begin_terms(world, &query, bad_traversal, 1) == ECS_ERR_ARGUMENT);
    return true;
}

TEST_CASE(ecs_cascade_visits_parents_before_children)
{
    ecs_entity grandchild = spawn(), child = spawn(), root = spawn(),
               sibling = spawn();
    pair(child, ECS_CHILD_OF, root);
    pair(grandchild, ECS_CHILD_OF, child);
    pair(sibling, ECS_CHILD_OF, root);
    ecs_entity all[] = { grandchild, child, root, sibling };
    for (int i = 0; i < 4; i++) {
        add(all[i], &position_desc);
    }
    apply();
    ecs_term terms[] = {
        { .id = position_desc.id },
        { .oper = ECS_OPTIONAL, .id = position_desc.id, .trav = ECS_UP, .cascade = true },
    };
    ecs_query query;
    REQUIRE(ecs_query_begin_terms(world, &query, terms, 2) == ECS_OK);
    ecs_entity expected[] = { root, child, sibling, grandchild };
    ecs_entity found;
    for (int i = 0; i < 4; i++) {
        REQUIRE(ecs_query_next(&query, &found) && same(found, expected[i]));
    }
    REQUIRE(!ecs_query_next(&query, &found));
    ecs_query_end(&query);
    ecs_term mixed[] = {
        { .id = position_desc.id },
        { .oper = ECS_OPTIONAL, .id = position_desc.id, .trav = ECS_UP, .cascade = true },
        { .oper = ECS_OPTIONAL,
          .id = position_desc.id,
          .trav = ECS_UP,
          .trav_rel = contained_in_desc.id,
          .cascade = true },
    };
    REQUIRE(ecs_query_begin_terms(world, &query, mixed, 3) == ECS_ERR_ARGUMENT);
    return true;
}

TEST_CASE(ecs_scopes_nest_any_boolean_expression)
{
    ecs_entity entities[4];
    for (int i = 0; i < 4; i++) {
        entities[i] = spawn();
        add(entities[i], &position_desc);
    }
    add(entities[1], &team_desc);
    add(entities[2], &frozen_desc);
    add(entities[3], &team_desc);
    add(entities[3], &frozen_desc);
    apply();
    uint32_t slots[4];
    /* Position, !(Team || Frozen) */
    ecs_term neither[] = {
        { .id = position_desc.id },
        { .kind = ECS_TERM_OPEN, .oper = ECS_NOT },
        { .oper = ECS_OR, .id = team_desc.id },
        { .oper = ECS_OR, .id = frozen_desc.id },
        { .kind = ECS_TERM_CLOSE },
    };
    REQUIRE(collect(neither, 5, slots, 4) == 1 && slots[0] == entities[0].slot);
    /* {Team, Frozen} || {!Team, !Frozen} */
    ecs_term same_flags[] = {
        { .kind = ECS_TERM_OPEN, .oper = ECS_OR },
        { .id = team_desc.id },
        { .id = frozen_desc.id },
        { .kind = ECS_TERM_CLOSE },
        { .kind = ECS_TERM_OPEN, .oper = ECS_OR },
        { .oper = ECS_NOT, .id = team_desc.id },
        { .oper = ECS_NOT, .id = frozen_desc.id },
        { .kind = ECS_TERM_CLOSE },
    };
    REQUIRE(collect(same_flags, 8, slots, 4) == 2);
    REQUIRE(slots[0] == entities[0].slot && slots[1] == entities[3].slot);
    ecs_query query;
    REQUIRE(ecs_query_begin_terms(world, &query, neither, 4) == ECS_ERR_ARGUMENT);
    REQUIRE(ecs_query_begin_terms(world, &query, &neither[4], 1) == ECS_ERR_ARGUMENT);
    return true;
}

TEST_CASE(ecs_fixed_sources_gate_every_result)
{
    ecs_entity settings = spawn(), a = spawn(), b = spawn();
    add(a, &position_desc);
    add(b, &position_desc);
    apply();
    ecs_term terms[] = {
        { .id = position_desc.id },
        { .id = frozen_desc.id, .src = settings },
    };
    uint32_t slots[2];
    REQUIRE(collect(terms, 2, slots, 2) == 0);
    add(settings, &frozen_desc);
    apply();
    REQUIRE(collect(terms, 2, slots, 2) == 2);
    return true;
}

TEST_CASE(ecs_invalid_terms_are_rejected)
{
    ecs_entity gone = spawn();
    REQUIRE(ecs_world_destroy_entity(world, gone) == ECS_OK);
    ecs_query query;
    const ecs_term unknown[] = { { .id = 999 } };
    REQUIRE(ecs_query_begin_terms(world, &query, unknown, 1) == ECS_ERR_COMPONENT);
    const ecs_term unbound[] = { { .id = position_desc.id, .src_var = 1 } };
    REQUIRE(ecs_query_begin_terms(world, &query, unbound, 1) == ECS_ERR_ARGUMENT);
    ecs_entity found;
    REQUIRE(ecs_query_next(&query, &found) == ECS_ERR_ARGUMENT);
    ecs_query_end(&query);
    ecs_component_id missing = 999;
    REQUIRE(ecs_query_begin(world, &query, &missing, 1) == ECS_ERR_COMPONENT);
    REQUIRE(ecs_query_next(&query, &found) == ECS_ERR_ARGUMENT);
    ecs_query_end(&query);
    const ecs_term bound_in_or[] = {
        { .oper = ECS_OR, .id = likes_desc.id, .target_var = 1 },
        { .oper = ECS_OR, .id = position_desc.id },
    };
    REQUIRE(ecs_query_begin_terms(world, &query, bound_in_or, 2) == ECS_ERR_ARGUMENT);
    const ecs_term component_target[] = { { .id = position_desc.id,
                                            .target_var = ECS_WILDCARD } };
    REQUIRE(ecs_query_begin_terms(world, &query, component_target, 1) == ECS_ERR_ARGUMENT);
    const ecs_term dead_target[] = { { .id = likes_desc.id, .target = gone } };
    REQUIRE(ecs_query_begin_terms(world, &query, dead_target, 1) == ECS_ERR_ENTITY);
    const ecs_term dead_source[] = { { .id = position_desc.id, .src = gone } };
    REQUIRE(ecs_query_begin_terms(world, &query, dead_source, 1) == ECS_ERR_ENTITY);
    return true;
}

TEST_CASE(ecs_reusable_plans_refresh_sources_membership_and_storage)
{
    ecs_entity root = spawn(), parent = spawn(), child = spawn();
    add(root, &team_desc);
    add(parent, &team_desc);
    add(child, &position_desc);
    pair(parent, ECS_CHILD_OF, root);
    pair(child, ECS_CHILD_OF, parent);
    pair(child, owner_desc.id, parent);
    apply();
    ecs_term terms[] = { { .id = position_desc.id },
                         { .id = team_desc.id, .trav = ECS_UP },
                         { .id = team_desc.id, .src_rel = owner_desc.id } };
    ecs_query_plan *plan = NULL;
    REQUIRE(ecs_query_plan_new(world, terms, 3, &plan) == ECS_OK);
    ecs_query query;
    ecs_entity found;
    REQUIRE(ecs_query_begin_plan(plan, &query) == ECS_OK);
    REQUIRE(ecs_query_next(&query, &found) && same(found, child));
    REQUIRE(same(ecs_query_src(&query, 1), parent));
    REQUIRE(same(ecs_query_src(&query, 2), parent));
    REQUIRE(!ecs_query_next(&query, &found));
    ecs_query_end(&query);
    REQUIRE(ecs_query_plan_stats(plan).cached_rows == 1);
    REQUIRE(ecs_world_record_remove(world, parent, team_desc.id, 0) == ECS_OK);
    pair(child, owner_desc.id, root);
    apply();
    REQUIRE(ecs_world_reserve_entities(world, 2048) == ECS_OK);
    /* Reallocation of both registration tables must not stale a plan. */
    for (int i = 0; i < 24; i++) {
        char key[64];
        snprintf(key, sizeof key, "sdk.query.extra.%d", i);
        ecs_component_desc component = { .id = 3000 + i,
                                         .key = key,
                                         .name = key,
                                         .element_size = sizeof(int),
                                         .type = &value_type };
        REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_OK);
        ecs_relationship_desc relationship = { .id = 4000 + i, .key = key, .name = key };
        snprintf(key, sizeof key, "sdk.query.relation.%d", i);
        REQUIRE(ecs_world_register_relationship(world, &relationship, NULL) == ECS_OK);
    }
    REQUIRE(ecs_query_begin_plan(plan, &query) == ECS_OK);
    REQUIRE(ecs_query_next(&query, &found) && same(found, child));
    REQUIRE(same(ecs_query_src(&query, 1), root));
    REQUIRE(same(ecs_query_src(&query, 2), root));
    REQUIRE(!ecs_query_next(&query, &found));
    ecs_query_end(&query);
    REQUIRE(ecs_query_plan_stats(plan).cache_builds == 2);
    REQUIRE(ecs_world_record_remove(world, root, team_desc.id, 0) == ECS_OK);
    apply();
    REQUIRE(ecs_query_begin_plan(plan, &query) == ECS_OK);
    REQUIRE(!ecs_query_next(&query, &found));
    ecs_query_end(&query);
    ecs_query_plan_free(plan);
    return true;
}

TEST_CASE(ecs_scalar_sources_are_optional_and_exclusive)
{
    ecs_entity root = spawn(), child = spawn(), missing = spawn();
    add(root, &team_desc);
    add(child, &position_desc);
    add(missing, &position_desc);
    pair(child, owner_desc.id, root);
    pair(child, ECS_CHILD_OF, root);
    apply();
    ecs_term terms[] = {
        { .id = position_desc.id },
        { .id = team_desc.id, .src_rel = owner_desc.id, .oper = ECS_OPTIONAL }
    };
    ecs_query query;
    REQUIRE(ecs_query_begin_terms(world, &query, terms, 2) == ECS_OK);
    ecs_entity found;
    REQUIRE(ecs_query_next(&query, &found) && same(found, child));
    REQUIRE(same(ecs_query_src(&query, 1), root));
    REQUIRE(ecs_query_next(&query, &found) && same(found, missing));
    REQUIRE(!ecs_query_src(&query, 1).slot);
    REQUIRE(!ecs_query_next(&query, &found));
    ecs_query_end(&query);
    REQUIRE(same(ecs_query_source(world, child, team_desc.id, ECS_CHILD_OF, ECS_UP), root));
    REQUIRE(!ecs_query_source(world, root, team_desc.id, ECS_CHILD_OF, ECS_UP).slot);
    REQUIRE(same(ecs_query_source(world, root, team_desc.id, ECS_CHILD_OF, ECS_SELF_UP), root));
    terms[1].src_rel = likes_desc.id;
    REQUIRE(ecs_query_begin_terms(world, &query, terms, 2) == ECS_ERR_ARGUMENT);
    return true;
}

TEST_CASE(ecs_reusable_boolean_plan_avoids_iteration_allocations)
{
    ecs_entity a = spawn(), b = spawn();
    add(a, &position_desc);
    add(b, &position_desc);
    add(b, &frozen_desc);
    apply();
    const ecs_term terms[] = { { .id = position_desc.id },
                               { .id = frozen_desc.id, .oper = ECS_NOT } };
    xalloc_stats before, fresh, reused;
    xalloc_stats_get(&before);
    for (int i = 0; i < 8; i++) {
        uint32_t slots[2];
        REQUIRE(collect(terms, 2, slots, 2) == 1 && slots[0] == a.slot);
    }
    xalloc_stats_get(&fresh);
    REQUIRE(fresh.calls > before.calls);
    ecs_query_plan *plan = NULL;
    REQUIRE(ecs_query_plan_new(world, terms, 2, &plan) == ECS_OK);
    xalloc_stats_get(&before);
    for (int i = 0; i < 8; i++) {
        ecs_query query;
        ecs_entity found;
        REQUIRE(ecs_query_begin_plan(plan, &query) == ECS_OK);
        REQUIRE(ecs_query_next(&query, &found) && same(found, a));
        REQUIRE(!ecs_query_next(&query, &found));
        ecs_query_end(&query);
    }
    xalloc_stats_get(&reused);
    REQUIRE(reused.calls == before.calls);
    ecs_query_plan_free(plan);
    return true;
}

TEST_CASE(ecs_reusable_fixed_source_does_not_follow_recycled_entities)
{
    ecs_entity settings = spawn(), entity = spawn();
    add(settings, &team_desc);
    add(entity, &position_desc);
    apply();
    const ecs_term terms[] = { { .id = position_desc.id },
                               { .id = team_desc.id, .src = settings } };
    ecs_query_plan *plan = NULL;
    REQUIRE(ecs_query_plan_new(world, terms, 2, &plan) == ECS_OK);
    ecs_query query;
    ecs_entity found;
    REQUIRE(ecs_query_begin_plan(plan, &query) == ECS_OK);
    REQUIRE(ecs_query_next(&query, &found) && same(found, entity));
    REQUIRE(!ecs_query_next(&query, &found));
    ecs_query_end(&query);
    REQUIRE(ecs_world_destroy_entity(world, settings) == ECS_OK);
    ecs_entity replacement = spawn();
    REQUIRE(replacement.slot == settings.slot && !same(replacement, settings));
    add(replacement, &team_desc);
    apply();
    REQUIRE(ecs_query_begin_plan(plan, &query) == ECS_OK);
    REQUIRE(!ecs_query_next(&query, &found));
    ecs_query_end(&query);
    ecs_query_plan_free(plan);
    return true;
}

TEST_CASE(ecs_reusable_pair_candidates_recheck_target_generations)
{
    ecs_entity target = spawn(), source = spawn();
    pair(source, likes_desc.id, target);
    apply();
    const ecs_term term = { .id = likes_desc.id, .target = target };
    ecs_query_plan *plan = NULL;
    REQUIRE(ecs_query_plan_new(world, &term, 1, &plan) == ECS_OK);
    ecs_query query;
    ecs_entity found;
    REQUIRE(ecs_query_begin_plan(plan, &query) == ECS_OK);
    REQUIRE(ecs_query_next(&query, &found) && same(found, source));
    REQUIRE(!ecs_query_next(&query, &found));
    ecs_query_end(&query);
    REQUIRE(ecs_world_destroy_entity(world, target) == ECS_OK);
    ecs_entity replacement = spawn();
    REQUIRE(replacement.slot == target.slot && !same(replacement, target));
    pair(source, likes_desc.id, replacement);
    apply();
    REQUIRE(ecs_query_begin_plan(plan, &query) == ECS_OK);
    REQUIRE(!ecs_query_next(&query, &found));
    ecs_query_end(&query);
    ecs_query_plan_free(plan);
    return true;
}

TEST_CASE(ecs_query_cache_reuses_membership_but_reads_current_values)
{
    ecs_entity a = spawn(), b = spawn();
    add(a, &position_desc);
    add(b, &position_desc);
    add(b, &frozen_desc);
    apply();
    const ecs_term terms[] = { { .id = position_desc.id },
                               { .id = frozen_desc.id, .oper = ECS_NOT } };
    ecs_query_plan *plan = NULL;
    REQUIRE(ecs_query_plan_new(world, terms, 2, &plan) == ECS_OK);
    uint32_t slots[2];
    REQUIRE(collect_plan(plan, slots, 2) == 1 && slots[0] == a.slot);
    ecs_query_stats built = ecs_query_plan_stats(plan);
    REQUIRE(built.cache_builds == 1 && built.cached_rows == 1);
    REQUIRE(built.candidates == 2 && built.node_tests > 0);
    *(int *)ecs_component_data(world, a, position_desc.id) = 42;
    REQUIRE(ecs_component_modified(world, a, position_desc.id) == ECS_OK);
    /* Unrelated membership, pair and entity changes do not invalidate a plan
     * anchored in Position. Native row writes never become stale snapshots. */
    add(a, &team_desc);
    pair(a, owner_desc.id, b);
    apply();
    ecs_entity unrelated = spawn();
    REQUIRE(ecs_world_destroy_entity(world, unrelated) == ECS_OK);
    REQUIRE(collect_plan(plan, slots, 2) == 1 && slots[0] == a.slot);
    ecs_query_stats hit = ecs_query_plan_stats(plan);
    REQUIRE(hit.cache_hits == 1 && hit.cache_builds == built.cache_builds);
    REQUIRE(hit.candidates == built.candidates && hit.node_tests == built.node_tests);
    REQUIRE(*(int *)ecs_component_data(world, a, position_desc.id) == 42);
    REQUIRE(ecs_world_record_add(world, a, ECS_DISABLED, NULL, 0) == ECS_OK);
    apply();
    REQUIRE(collect_plan(plan, slots, 2) == 0);
    REQUIRE(ecs_query_plan_stats(plan).cache_builds == 2);
    REQUIRE(ecs_world_record_remove(world, a, ECS_DISABLED, 0) == ECS_OK);
    REQUIRE(ecs_world_record_remove(world, b, frozen_desc.id, 0) == ECS_OK);
    apply();
    REQUIRE(collect_plan(plan, slots, 2) == 2);
    REQUIRE(ecs_query_plan_stats(plan).cache_builds == 3);
    ecs_query_plan_free(plan);
    return true;
}

TEST_CASE(ecs_query_cache_partial_iterations_never_publish)
{
    ecs_entity a = spawn(), b = spawn();
    add(a, &position_desc);
    add(b, &position_desc);
    apply();
    const ecs_term terms[] = { { .id = position_desc.id },
                               { .id = frozen_desc.id, .oper = ECS_NOT } };
    ecs_query_plan *plan = NULL;
    REQUIRE(ecs_query_plan_new(world, terms, 2, &plan) == ECS_OK);
    REQUIRE(ecs_world_record_remove(world, b, position_desc.id, 0) == ECS_OK);
    ecs_query query;
    ecs_entity found;
    REQUIRE(ecs_query_begin_plan(plan, &query) == ECS_OK);
    REQUIRE(ecs_query_next(&query, &found) && same(found, a));
    REQUIRE(ecs_world_record_remove(world, b, position_desc.id, 0) == ECS_ERR_ENTITY);
    REQUIRE(ecs_world_apply_commands(world) == ECS_ERR_ITERATING);
    ecs_query_end(&query);
    REQUIRE(ecs_query_plan_stats(plan).cached_rows == 0);
    uint32_t slots[2];
    REQUIRE(collect_plan(plan, slots, 2) == 2);
    REQUIRE(ecs_query_plan_stats(plan).cache_hits == 0);
    REQUIRE(collect_plan(plan, slots, 2) == 2);
    REQUIRE(ecs_query_plan_stats(plan).cache_hits == 1);
    apply();
    REQUIRE(collect_plan(plan, slots, 2) == 1 && slots[0] == a.slot);
    ecs_query_plan_free(plan);
    return true;
}

TEST_CASE(ecs_query_cache_preserves_sources_and_optional_absence)
{
    ecs_entity root = spawn(), parent = spawn(), child = spawn();
    add(root, &team_desc);
    add(child, &position_desc);
    pair(parent, ECS_CHILD_OF, root);
    pair(child, ECS_CHILD_OF, parent);
    pair(child, owner_desc.id, parent);
    apply();
    const ecs_term terms[] = {
        { .id = position_desc.id },
        { .id = team_desc.id, .trav = ECS_UP, .cascade = true },
        { .id = team_desc.id, .src_rel = owner_desc.id, .oper = ECS_OPTIONAL },
    };
    ecs_query_plan *plan = NULL;
    REQUIRE(ecs_query_plan_new(world, terms, 3, &plan) == ECS_OK);
    for (int pass = 0; pass < 4; pass++) {
        if (pass == 1) {
            /* Storage growth and unrelated registration must be safe on a hit. */
            REQUIRE(ecs_world_reserve_entities(world, 2048) == ECS_OK);
            for (int i = 0; i < 24; i++) {
                char key[64];
                snprintf(key, sizeof key, "sdk.query.cache.extra.%d", i);
                ecs_component_desc desc = position_desc;
                desc.id = 6000 + i;
                desc.key = key;
                REQUIRE(ecs_world_register_component(world, &desc, NULL) == ECS_OK);
            }
        }
        if (pass == 2) {
            add(parent, &team_desc);
            pair(child, owner_desc.id, root);
            apply();
        }
        ecs_query query;
        ecs_entity found;
        REQUIRE(ecs_query_begin_plan(plan, &query) == ECS_OK);
        REQUIRE(ecs_query_next(&query, &found) && same(found, child));
        REQUIRE(same(ecs_query_src(&query, 1), pass < 2 ? root : parent));
        REQUIRE(
            pass < 2 ? !ecs_query_src(&query, 2).slot
                     : same(ecs_query_src(&query, 2), root)
        );
        REQUIRE(!ecs_query_next(&query, &found));
        ecs_query_end(&query);
    }
    ecs_query_stats stats = ecs_query_plan_stats(plan);
    REQUIRE(stats.cache_hits == 2 && stats.cache_builds == 2);
    REQUIRE(ecs_world_record_remove_pair(world, child, ECS_CHILD_OF, parent, 0) == ECS_OK);
    apply();
    REQUIRE(collect_plan(plan, NULL, 0) == 0);
    ecs_query_plan_free(plan);
    return true;
}

TEST_CASE(ecs_query_cache_preserves_multi_target_bindings_and_source_order)
{
    ecs_entity root = spawn(), a = spawn(), b = spawn();
    pair(a, ECS_CHILD_OF, root);
    pair(b, ECS_CHILD_OF, root);
    pair(a, likes_desc.id, a);
    pair(a, likes_desc.id, b);
    pair(b, likes_desc.id, a);
    apply();
    const ecs_term terms[] = { { .id = ECS_CHILD_OF, .target = root },
                               { .id = likes_desc.id, .target_var = 1 } };
    ecs_query_plan *plan = NULL;
    REQUIRE(ecs_query_plan_new(world, terms, 2, &plan) == ECS_OK);
    for (int pass = 0; pass < 4; pass++) {
        if (pass == 2) {
            REQUIRE(ecs_world_record_child_index(world, b, 0, 0) == ECS_OK);
            apply();
        }
        ecs_entity owners[] = { a, a, b }, targets[] = { a, b, a };
        if (pass >= 2) {
            owners[0] = b;
            owners[1] = a;
            owners[2] = a;
            targets[0] = a;
            targets[1] = a;
            targets[2] = b;
        }
        ecs_query query;
        ecs_entity found;
        REQUIRE(ecs_query_begin_plan(plan, &query) == ECS_OK);
        for (int i = 0; i < 3; i++) {
            REQUIRE(ecs_query_next(&query, &found) && same(found, owners[i]));
            REQUIRE(same(ecs_query_var(&query, 1), targets[i]));
            REQUIRE(same(ecs_query_src(&query, 1), owners[i]));
        }
        REQUIRE(!ecs_query_next(&query, &found));
        ecs_query_end(&query);
    }
    ecs_query_stats stats = ecs_query_plan_stats(plan);
    REQUIRE(stats.cache_hits == 2 && stats.cache_builds == 2);
    REQUIRE(ecs_world_record_remove_pair(world, a, likes_desc.id, b, 0) == ECS_OK);
    apply();
    REQUIRE(collect_plan(plan, NULL, 0) == 2);
    ecs_query_plan_free(plan);
    return true;
}

TEST_CASE(ecs_query_cache_bounds_cartesian_result_storage)
{
    ecs_entity source = spawn(), targets[36];
    for (int i = 0; i < 36; i++) {
        targets[i] = spawn();
        pair(source, likes_desc.id, targets[i]);
    }
    apply();
    const ecs_term terms[] = { { .id = likes_desc.id, .target_var = 1 },
                               { .id = likes_desc.id, .target_var = 2 },
                               { .id = likes_desc.id, .target_var = 3 } };
    ecs_query_plan *plan = NULL;
    REQUIRE(ecs_query_plan_new(world, terms, 3, &plan) == ECS_OK);
    REQUIRE(collect_plan(plan, NULL, 0) == 36 * 36 * 36);
    ecs_query_stats stats = ecs_query_plan_stats(plan);
    REQUIRE(stats.cache_overflows == 1 && stats.cached_rows == 0);
    REQUIRE(stats.retained_bytes == 0);
    REQUIRE(collect_plan(plan, NULL, 0) == 36 * 36 * 36);
    REQUIRE(ecs_query_plan_stats(plan).cache_builds == stats.cache_builds);
    for (int i = 2; i < 36; i++) {
        REQUIRE(
            ecs_world_record_remove_pair(world, source, likes_desc.id, targets[i], 0) == ECS_OK
        );
    }
    apply();
    REQUIRE(collect_plan(plan, NULL, 0) == 8);
    REQUIRE(collect_plan(plan, NULL, 0) == 8);
    stats = ecs_query_plan_stats(plan);
    REQUIRE(stats.cache_hits == 1 && stats.cached_rows == 8);
    REQUIRE(stats.retained_bytes <= 1024 * 1024);
    ecs_query_plan_free(plan);
    return true;
}

TEST_CASE(ecs_query_cache_reserves_multi_target_rows_outside_iteration)
{
    ecs_entity source = spawn();
    for (int i = 0; i < 3; i++) {
        pair(source, likes_desc.id, spawn());
    }
    apply();
    const ecs_term terms[] = { { .id = likes_desc.id, .target_var = 1 },
                               { .id = likes_desc.id, .target_var = 2 } };
    ecs_query_plan *plan = NULL;
    REQUIRE(ecs_query_plan_new(world, terms, 2, &plan) == ECS_OK);
    xalloc_stats before, after;
    xalloc_stats_get(&before);
    REQUIRE(collect_plan(plan, NULL, 0) == 9);
    xalloc_stats_get(&after);
    REQUIRE(after.calls == before.calls);
    REQUIRE(ecs_query_plan_stats(plan).cached_rows == 0);
    ecs_query query;
    REQUIRE(ecs_query_begin_plan(plan, &query) == ECS_OK);
    xalloc_stats_get(&before);
    int rows = 0;
    ecs_entity found;
    while (ecs_query_next(&query, &found)) {
        REQUIRE(same(found, source));
        rows++;
    }
    ecs_query_end(&query);
    xalloc_stats_get(&after);
    REQUIRE(rows == 9 && after.calls == before.calls);
    REQUIRE(ecs_query_plan_stats(plan).cached_rows == 9);
    REQUIRE(collect_plan(plan, NULL, 0) == 9);
    REQUIRE(ecs_query_plan_stats(plan).cache_hits == 1);
    ecs_query_plan_free(plan);
    return true;
}

TEST_CASE(ecs_query_cache_all_entity_plans_track_lifetimes)
{
    ecs_query_plan *plan = NULL;
    REQUIRE(ecs_query_plan_new(world, NULL, 0, &plan) == ECS_OK);
    REQUIRE(collect_plan(plan, NULL, 0) == 0);
    REQUIRE(collect_plan(plan, NULL, 0) == 0);
    REQUIRE(ecs_query_plan_stats(plan).cache_hits == 1);
    ecs_entity entity = spawn();
    REQUIRE(collect_plan(plan, NULL, 0) == 1);
    REQUIRE(ecs_world_destroy_entity(world, entity) == ECS_OK);
    REQUIRE(collect_plan(plan, NULL, 0) == 0);
    REQUIRE(ecs_query_plan_stats(plan).cache_builds == 3);
    ecs_query_plan_free(plan);
    return true;
}

TEST_CASE(ecs_query_candidates_use_actual_sparse_and_dense_costs)
{
    ecs_entity last = { 0 };
    for (int i = 0; i < 256; i++) {
        last = spawn();
        add(last, &position_desc);
    }
    add(last, &team_desc);
    apply();
    const ecs_term terms[] = { { .id = position_desc.id }, { .id = team_desc.id } };
    ecs_query_plan *plan = NULL;
    REQUIRE(ecs_query_plan_new(world, terms, 2, &plan) == ECS_OK);
    uint32_t slots[1];
    REQUIRE(collect_plan(plan, slots, 1) == 1 && slots[0] == last.slot);
    ecs_query_stats stats = ecs_query_plan_stats(plan);
    REQUIRE(stats.candidates == 1 && stats.node_tests == 1);
    REQUIRE(collect_plan(plan, slots, 1) == 1 && slots[0] == last.slot);
    REQUIRE(ecs_query_plan_stats(plan).candidates == stats.candidates);
    ecs_query_plan_free(plan);
    REQUIRE(ecs_query_plan_new(world, terms, 1, &plan) == ECS_OK);
    REQUIRE(collect_plan(plan, NULL, 0) == 256);
    REQUIRE(ecs_query_plan_stats(plan).retained_bytes == 0);
    REQUIRE(ecs_query_plan_stats(plan).cache_builds == 0);
    ecs_query_plan_free(plan);
    return true;
}

TEST_CASE(ecs_query_filters_prune_before_later_variable_generators)
{
    ecs_entity source = spawn(), targets[8];
    add(source, &position_desc);
    for (int i = 0; i < 8; i++) {
        targets[i] = spawn();
        pair(source, likes_desc.id, targets[i]);
    }
    add(targets[0], &team_desc);
    add(targets[7], &frozen_desc);
    apply();
    const ecs_term terms[] = {
        { .id = position_desc.id },
        { .id = likes_desc.id, .target_var = 1 },
        { .id = likes_desc.id, .target_var = 2 },
        { .id = team_desc.id, .src_var = 1 },
        { .id = frozen_desc.id, .src_var = 2, .oper = ECS_NOT },
        { .id = team_desc.id, .src_var = 2, .oper = ECS_OPTIONAL },
    };
    ecs_query_plan *plan = NULL;
    REQUIRE(ecs_query_plan_new(world, terms, 6, &plan) == ECS_OK);
    for (int pass = 0; pass < 2; pass++) {
        ecs_query query;
        ecs_entity found;
        REQUIRE(ecs_query_begin_plan(plan, &query) == ECS_OK);
        for (int i = 0; i < 7; i++) {
            REQUIRE(ecs_query_next(&query, &found) && same(found, source));
            REQUIRE(same(ecs_query_var(&query, 1), targets[0]));
            REQUIRE(same(ecs_query_var(&query, 2), targets[i]));
            REQUIRE(
                i ? !ecs_query_src(&query, 5).slot
                  : same(ecs_query_src(&query, 5), targets[0])
            );
        }
        REQUIRE(!ecs_query_next(&query, &found));
        ecs_query_end(&query);
    }
    ecs_query_stats stats = ecs_query_plan_stats(plan);
    /* Each generator advances across eight targets plus exhaustion once.
     * Deferring Team($1) until both generators would require 9 + 8*9 steps. */
    REQUIRE(stats.binding_steps == 18);
    REQUIRE(stats.cache_hits == 1 && stats.cached_rows == 7);
    ecs_query_plan_free(plan);
    return true;
}

TEST_CASE(ecs_component_versions_track_publications_without_invalidating_membership)
{
    ecs_entity entity = spawn();
    add(entity, &position_desc);
    add(entity, &team_desc);
    apply();
    REQUIRE(ecs_component_track_changes(world, position_desc.id) == ECS_OK);
    REQUIRE(ecs_world_iteration_begin(world) == ECS_OK);
    REQUIRE(ecs_component_track_changes(world, position_desc.id) == ECS_ERR_ITERATING);
    REQUIRE(ecs_world_iteration_end(world) == ECS_OK);
    const ecs_term terms[] = { { .id = position_desc.id }, { .id = team_desc.id } };
    ecs_query_plan *plan = NULL;
    REQUIRE(ecs_query_plan_new(world, terms, 2, &plan) == ECS_OK);
    REQUIRE(collect_plan(plan, NULL, 0) == 1);
    uint64_t before = ecs_component_version(world, entity, position_desc.id);
    REQUIRE(
        ecs_component_write_field(world, entity, position_desc.id, 0, (pin_value){ .type = PIN_I32, .i = 0 }) ==
        ECS_OK
    );
    uint64_t written = ecs_component_version(world, entity, position_desc.id);
    REQUIRE(written > before);
    apply();
    REQUIRE(ecs_component_version(world, entity, position_desc.id) == written);
    REQUIRE(collect_plan(plan, NULL, 0) == 1);
    REQUIRE(ecs_query_plan_stats(plan).cache_hits == 1);
    *(int *)ecs_component_data(world, entity, position_desc.id) = 7;
    REQUIRE(ecs_component_version(world, entity, position_desc.id) == written);
    REQUIRE(ecs_component_modified(world, entity, position_desc.id) == ECS_OK);
    REQUIRE(ecs_component_version(world, entity, position_desc.id) > written);
    uint64_t marked = ecs_component_version(world, entity, position_desc.id);
    REQUIRE(ecs_world_reserve_entities(world, 2048) == ECS_OK);
    REQUIRE(ecs_component_version(world, entity, position_desc.id) == marked);
    REQUIRE(ecs_world_record_remove(world, entity, position_desc.id, 0) == ECS_OK);
    apply();
    REQUIRE(ecs_component_version(world, entity, position_desc.id) == 0);
    add(entity, &position_desc);
    apply();
    REQUIRE(ecs_component_version(world, entity, position_desc.id) > marked);
    REQUIRE(ecs_world_destroy_entity(world, entity) == ECS_OK);
    ecs_entity replacement = spawn();
    add(replacement, &position_desc);
    apply();
    REQUIRE(ecs_component_version(world, entity, position_desc.id) == 0);
    REQUIRE(ecs_component_version(world, replacement, position_desc.id) > 0);
    ecs_query_plan_free(plan);
    return true;
}

void test_tool_ecs_query()
{
    pu_setup(prepare_world, release_world);
    RUN_TEST_CASE(ecs_pair_terms_match_targets_wildcards_and_negation);
    RUN_TEST_CASE(ecs_variables_backtrack_over_each_target);
    RUN_TEST_CASE(ecs_up_terms_find_the_nearest_ancestor);
    RUN_TEST_CASE(ecs_cascade_visits_parents_before_children);
    RUN_TEST_CASE(ecs_scopes_nest_any_boolean_expression);
    RUN_TEST_CASE(ecs_fixed_sources_gate_every_result);
    RUN_TEST_CASE(ecs_invalid_terms_are_rejected);
    RUN_TEST_CASE(ecs_reusable_plans_refresh_sources_membership_and_storage);
    RUN_TEST_CASE(ecs_scalar_sources_are_optional_and_exclusive);
    RUN_TEST_CASE(ecs_reusable_boolean_plan_avoids_iteration_allocations);
    RUN_TEST_CASE(ecs_reusable_fixed_source_does_not_follow_recycled_entities);
    RUN_TEST_CASE(ecs_reusable_pair_candidates_recheck_target_generations);
    RUN_TEST_CASE(ecs_query_cache_reuses_membership_but_reads_current_values);
    RUN_TEST_CASE(ecs_query_cache_partial_iterations_never_publish);
    RUN_TEST_CASE(ecs_query_cache_preserves_sources_and_optional_absence);
    RUN_TEST_CASE(ecs_query_cache_preserves_multi_target_bindings_and_source_order);
    RUN_TEST_CASE(ecs_query_cache_bounds_cartesian_result_storage);
    RUN_TEST_CASE(ecs_query_cache_reserves_multi_target_rows_outside_iteration);
    RUN_TEST_CASE(ecs_query_cache_all_entity_plans_track_lifetimes);
    RUN_TEST_CASE(ecs_query_candidates_use_actual_sparse_and_dense_costs);
    RUN_TEST_CASE(ecs_query_filters_prune_before_later_variable_generators);
    RUN_TEST_CASE(ecs_component_versions_track_publications_without_invalidating_membership);
    pu_clear_setup();
}
