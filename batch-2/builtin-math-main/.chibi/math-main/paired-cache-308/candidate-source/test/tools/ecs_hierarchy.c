#include "ecs/ecs.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <string.h>

static const type_field value_fields[] = {
    { .name = "value", .type = PIN_I32, .offset = 0, .size = sizeof(int), .type_name = "int" },
};
static const type_desc value_type = {
    .name = "sdk.ecs.hierarchy_value",
    .size = sizeof(int),
    .fields = value_fields,
    .field_count = 1,
};
static const ecs_component_desc health_desc = {
    .id = 701,
    .key = "sdk.ecs.health",
    .name = "Health",
    .element_size = sizeof(int),
    .type = &value_type,
    .index = ECS_INDEX_DENSE,
};

static ecs_world *world;

static void prepare_world()
{
    world = ecs_world_new();
    assert(ecs_world_register_component(world, &health_desc, NULL) == ECS_OK);
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

static ecs_entity spawn_child(ecs_entity parent, const char *name)
{
    ecs_entity entity = spawn();
    if (parent.slot) {
        assert(ecs_world_record_add_pair(world, entity, ECS_CHILD_OF, parent, 0) == ECS_OK);
    }
    if (name) {
        assert(ecs_world_record_name(world, entity, name, 1) == ECS_OK);
    }
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return entity;
}

static bool same(ecs_entity left, ecs_entity right)
{
    return ecs_entity_to_u64(left) == ecs_entity_to_u64(right);
}

static ecs_entity child_at(ecs_entity parent, int index)
{
    return ecs_pair_source(world, ECS_CHILD_OF, parent, index);
}

TEST_CASE(ecs_child_of_keeps_order_and_depth_across_reparenting)
{
    ecs_entity root = spawn_child((ecs_entity){ 0 }, NULL);
    ecs_entity a = spawn_child(root, NULL), b = spawn_child(root, NULL);
    ecs_entity c = spawn_child(a, NULL);
    REQUIRE(ecs_entity_depth(world, root) == 0 && ecs_entity_depth(world, c) == 2);
    REQUIRE(same(ecs_parent(world, c), a) && !ecs_parent(world, root).slot);
    REQUIRE(ecs_world_relationship(world, ECS_CHILD_OF)->traits & ECS_REL_EXCLUSIVE);
    REQUIRE(ecs_world_record_add_pair(world, a, ECS_CHILD_OF, b, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_entity_depth(world, a) == 2 && ecs_entity_depth(world, c) == 3);
    REQUIRE(ecs_pair_source_count(world, ECS_CHILD_OF, root) == 1);
    REQUIRE(same(child_at(b, 0), a));
    REQUIRE(ecs_world_record_add_pair(world, b, ECS_CHILD_OF, c, 0) == ECS_ERR_CYCLE);
    REQUIRE(ecs_world_record_remove_pair(world, a, ECS_CHILD_OF, b, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_entity_depth(world, a) == 0 && ecs_entity_depth(world, c) == 1);
    ecs_entity x = spawn_child(root, NULL), y = spawn_child(root, NULL),
               z = spawn_child(root, NULL);
    REQUIRE(ecs_world_record_child_index(world, z, 0, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(same(child_at(root, 0), z) && same(child_at(root, 1), b));
    REQUIRE(same(child_at(root, 2), x) && same(child_at(root, 3), y));
    REQUIRE(ecs_world_record_child_index(world, b, 99, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(same(child_at(root, 1), x) && same(child_at(root, 3), b));
    REQUIRE(ecs_world_record_child_index(world, root, 0, 0) == ECS_ERR_COMPONENT);
    REQUIRE(ecs_world_record_child_index(world, x, -1, 0) == ECS_ERR_ARGUMENT);
    return true;
}

TEST_CASE(ecs_renames_may_take_names_freed_earlier_in_the_batch)
{
    ecs_entity alpha = spawn_child((ecs_entity){ 0 }, "Alpha");
    ecs_entity beta = spawn_child((ecs_entity){ 0 }, "Beta");
    REQUIRE(ecs_world_record_name(world, alpha, "Gamma", 0) == ECS_OK);
    REQUIRE(ecs_world_record_name(world, beta, "Alpha", 1) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(same(ecs_lookup(world, (ecs_entity){ 0 }, "Gamma"), alpha));
    REQUIRE(same(ecs_lookup(world, (ecs_entity){ 0 }, "Alpha"), beta));
    return true;
}

TEST_CASE(ecs_names_resolve_paths_and_stay_unique_among_siblings)
{
    ecs_entity level = spawn_child((ecs_entity){ 0 }, "Level");
    ecs_entity player = spawn_child(level, "Player");
    ecs_entity gun = spawn_child(player, "Gun");
    ecs_entity other = spawn_child((ecs_entity){ 0 }, "Other");
    ecs_entity other_player = spawn_child(other, "Player");
    REQUIRE(same(ecs_lookup(world, (ecs_entity){ 0 }, "Level.Player.Gun"), gun));
    REQUIRE(same(ecs_lookup(world, level, "Player"), player));
    REQUIRE(same(ecs_lookup(world, (ecs_entity){ 0 }, "Other.Player"), other_player));
    REQUIRE(!ecs_lookup(world, (ecs_entity){ 0 }, "Player").slot);
    REQUIRE(!ecs_lookup(world, (ecs_entity){ 0 }, "Level..Gun").slot);
    REQUIRE(!ecs_lookup(world, (ecs_entity){ 0 }, "Level.Gun").slot);
    REQUIRE(!strcmp(ecs_entity_name(world, gun), "Gun"));
    REQUIRE(ecs_world_record_name(world, other, "Level", 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_ERR_DUPLICATE);
    REQUIRE(ecs_world_record_name(world, other, "A.B", 0) == ECS_ERR_ARGUMENT);
    REQUIRE(ecs_world_record_name(world, other, "", 0) == ECS_ERR_ARGUMENT);
    REQUIRE(ecs_world_record_add(world, other, ECS_NAME, NULL, 0) == ECS_ERR_ARGUMENT);
    pin_value text = { .type = PIN_NAME, .str = "Forged" };
    REQUIRE(ecs_component_write_field(world, other, ECS_NAME, 0, text) == ECS_ERR_ARGUMENT);
    REQUIRE(ecs_component_write_value(world, other, ECS_NAME, 0, text, text) == ECS_ERR_ARGUMENT);
    REQUIRE(!ecs_component_data(world, gun, ECS_NAME));
    REQUIRE(ecs_component_data_const(world, gun, ECS_NAME));
    REQUIRE(!ecs_component_accepts_value(world, ECS_NAME, 0, text));
    char buffer[] = "Renamed";
    REQUIRE(ecs_world_record_name(world, other, buffer, 0) == ECS_OK);
    buffer[0] = 'X';
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(same(ecs_lookup(world, (ecs_entity){ 0 }, "Renamed.Player"), other_player));
    REQUIRE(ecs_world_record_add_pair(world, other_player, ECS_CHILD_OF, level, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_ERR_DUPLICATE);
    REQUIRE(ecs_world_record_remove_pair(world, gun, ECS_CHILD_OF, player, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(same(ecs_lookup(world, (ecs_entity){ 0 }, "Gun"), gun));
    REQUIRE(ecs_world_record_remove(world, gun, ECS_NAME, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(
        !ecs_entity_name(world, gun) &&
        !ecs_lookup(world, (ecs_entity){ 0 }, "Gun").slot
    );
    return true;
}

TEST_CASE(ecs_destroying_a_parent_destroys_its_named_subtree)
{
    ecs_entity level = spawn_child((ecs_entity){ 0 }, "Level");
    ecs_entity player = spawn_child(level, "Player");
    spawn_child(player, "Gun");
    ecs_entity survivor = spawn_child((ecs_entity){ 0 }, "Survivor");
    REQUIRE(ecs_world_destroy_entity(world, level) == ECS_OK);
    REQUIRE(!ecs_entity_is_alive(world, player));
    REQUIRE(ecs_world_live_count(world) == 1);
    REQUIRE(!ecs_lookup(world, (ecs_entity){ 0 }, "Level").slot);
    REQUIRE(same(ecs_lookup(world, (ecs_entity){ 0 }, "Survivor"), survivor));
    ecs_entity reused = spawn_child((ecs_entity){ 0 }, "Level");
    REQUIRE(
        ecs_entity_depth(world, reused) == 0 &&
        !ecs_pair_source_count(world, ECS_CHILD_OF, reused)
    );
    return true;
}

static int visit(const ecs_component_id *components, int count)
{
    ecs_query query;
    assert(ecs_query_begin(world, &query, components, count) == ECS_OK);
    int visited = 0;
    ecs_entity found;
    while (ecs_query_next(&query, &found)) {
        visited++;
    }
    ecs_query_end(&query);
    return visited;
}

static int visit_terms(const ecs_term *terms, int count)
{
    ecs_query query;
    assert(ecs_query_begin_terms(world, &query, terms, count) == ECS_OK);
    int visited = 0;
    ecs_entity found;
    while (ecs_query_next(&query, &found)) {
        visited++;
    }
    ecs_query_end(&query);
    return visited;
}

TEST_CASE(ecs_disabled_entities_leave_queries_that_do_not_name_them)
{
    ecs_entity entities[3];
    for (int i = 0; i < 3; i++) {
        entities[i] = spawn();
        REQUIRE(ecs_world_record_add(world, entities[i], health_desc.id, NULL, 0) == ECS_OK);
    }
    REQUIRE(ecs_world_record_add(world, entities[1], ECS_DISABLED, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    ecs_component_id with_disabled[] = { health_desc.id, ECS_DISABLED };
    REQUIRE(visit(&health_desc.id, 1) == 2);
    REQUIRE(visit(with_disabled, 2) == 1);
    REQUIRE(visit(NULL, 0) == 2);
    ecs_term any[] = {
        { .oper = ECS_OR, .id = health_desc.id },
        { .oper = ECS_OR, .id = ECS_DISABLED },
    };
    REQUIRE(visit_terms(any, 1) == 2);
    REQUIRE(visit_terms(any, 2) == 3);
    ecs_term not_disabled[] = { { .id = health_desc.id },
                                { .oper = ECS_NOT, .id = ECS_DISABLED } };
    REQUIRE(visit_terms(not_disabled, 2) == 2);
    REQUIRE(ecs_component_has_entities(world, health_desc.id, &entities[0], 1));
    ecs_entity enabled[] = { entities[0], entities[2] };
    REQUIRE(!ecs_component_has_entities(world, health_desc.id, enabled, 2));
    REQUIRE(ecs_component_has_entities(world, ECS_DISABLED, NULL, 0));
    REQUIRE(ecs_world_record_remove(world, entities[1], ECS_DISABLED, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(visit(&health_desc.id, 1) == 3);
    return true;
}

static void record_added_name(ecs_world *observed, ecs_event, ecs_entity entity, ecs_component_id, void *user)
{
    const char *name = ecs_entity_name(observed, entity);
    *(bool *)user = name && !strcmp(name, "Spawned");
}

/* Builds Level with children A and B, then reorders them when reorder is set. */
static uint64_t named_tree_hash(bool reorder)
{
    ecs_world *saved = world;
    world = ecs_world_new();
    ecs_entity level = spawn_child((ecs_entity){ 0 }, "Level");
    char name[] = "A";
    spawn_child(level, name);
    name[0] = 'B';
    ecs_entity b = spawn_child(level, name);
    if (reorder) {
        assert(ecs_world_record_child_index(world, b, 0, 0) == ECS_OK);
        assert(ecs_world_apply_commands(world) == ECS_OK);
    }
    uint64_t hash = ecs_world_hash(world);
    ecs_world_free(world);
    world = saved;
    return hash;
}

TEST_CASE(ecs_names_apply_with_creates_and_hash_by_content)
{
    bool seen = false;
    REQUIRE(ecs_world_observe(world, ECS_ON_ADD, ECS_NAME, record_added_name, &seen) > 0);
    ecs_entity parent = spawn_child((ecs_entity){ 0 }, "Parent");
    ecs_entity child = spawn();
    REQUIRE(ecs_world_record_add_pair(world, child, ECS_CHILD_OF, parent, 0) == ECS_OK);
    REQUIRE(ecs_world_record_name(world, child, "Spawned", 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(seen && same(ecs_lookup(world, (ecs_entity){ 0 }, "Parent.Spawned"), child));
    REQUIRE(ecs_world_record_child_index(world, child, 0, 1) == ECS_OK);
    REQUIRE(ecs_world_record_remove_pair(world, child, ECS_CHILD_OF, parent, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(same(ecs_lookup(world, (ecs_entity){ 0 }, "Spawned"), child));
    REQUIRE(ecs_world_add_with(world, health_desc.id, ECS_NAME) == ECS_ERR_ARGUMENT);
    REQUIRE(named_tree_hash(false) == named_tree_hash(false));
    REQUIRE(named_tree_hash(false) != named_tree_hash(true));
    return true;
}

void test_tool_ecs_hierarchy()
{
    pu_setup(prepare_world, release_world);
    RUN_TEST_CASE(ecs_child_of_keeps_order_and_depth_across_reparenting);
    RUN_TEST_CASE(ecs_renames_may_take_names_freed_earlier_in_the_batch);
    RUN_TEST_CASE(ecs_names_resolve_paths_and_stay_unique_among_siblings);
    RUN_TEST_CASE(ecs_destroying_a_parent_destroys_its_named_subtree);
    RUN_TEST_CASE(ecs_disabled_entities_leave_queries_that_do_not_name_them);
    RUN_TEST_CASE(ecs_names_apply_with_creates_and_hash_by_content);
    pu_clear_setup();
}
