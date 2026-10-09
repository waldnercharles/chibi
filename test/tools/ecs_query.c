#include "ecs/ecs.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdint.h>

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

static ecs_world *world;

static void prepare_world()
{
    world = ecs_world_new();
    assert(ecs_world_register_component(world, &position_desc, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &team_desc, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &frozen_desc, NULL) == ECS_OK);
    assert(ecs_world_register_relationship(world, &likes_desc, NULL) == ECS_OK);
    assert(ecs_world_register_relationship(world, &owner_desc, NULL) == ECS_OK);
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
    pu_clear_setup();
}
