#include "ecs/ecs.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <assert.h>
#include <stddef.h>

static const type_field value_fields[] = {
    { .name = "value", .type = PIN_I32, .offset = 0, .size = sizeof(int), .type_name = "int" },
};
static const type_desc value_type = {
    .name = "sdk.ecs.pair_value",
    .size = sizeof(int),
    .fields = value_fields,
    .field_count = 1,
};
static const ecs_component_desc armor_desc = {
    .id = 501,
    .key = "sdk.ecs.armor",
    .name = "Armor",
    .element_size = sizeof(int),
    .type = &value_type,
};
static const ecs_component_desc weight_desc = {
    .id = 502,
    .key = "sdk.ecs.weight",
    .name = "Weight",
    .element_size = sizeof(int),
    .type = &value_type,
};
/* Derived from armor, which registers first, and from shield, which
 * registers after it. */
static const ecs_component_desc total_desc = {
    .id = 503,
    .key = "sdk.ecs.total",
    .name = "Total",
    .element_size = sizeof(int),
    .type = &value_type,
    .derived_from = 501,
};
static const ecs_component_desc shield_desc = {
    .id = 504,
    .key = "sdk.ecs.shield",
    .name = "Shield",
    .element_size = sizeof(int),
    .type = &value_type,
};
static const ecs_component_desc cover_desc = {
    .id = 505,
    .key = "sdk.ecs.cover",
    .name = "Cover",
    .element_size = sizeof(int),
    .type = &value_type,
    .derived_from = 504,
};
static const ecs_relationship_desc likes_desc = { .id = 601,
                                                  .key = "sdk.ecs.likes",
                                                  .name = "Likes" };
static const ecs_relationship_desc owner_desc = {
    .id = 602,
    .key = "sdk.ecs.owner",
    .name = "Owner",
    .traits = ECS_REL_EXCLUSIVE | ECS_REL_ACYCLIC,
};
static const ecs_relationship_desc parent_desc = {
    .id = 603,
    .key = "sdk.ecs.parent",
    .name = "Parent",
    .traits = ECS_REL_EXCLUSIVE | ECS_REL_TRAVERSABLE,
    .on_delete_target = ECS_CLEANUP_DELETE,
};
static const ecs_relationship_desc anchor_desc = {
    .id = 604,
    .key = "sdk.ecs.anchor",
    .name = "Anchor",
    .on_delete_target = ECS_CLEANUP_PANIC,
};
static const ecs_relationship_desc depends_desc = {
    .id = 605,
    .key = "sdk.ecs.depends",
    .name = "Depends",
    .on_delete_target = ECS_CLEANUP_DELETE,
};
static const ecs_relationship_desc *const relationship_descs[] = {
    &likes_desc, &owner_desc, &parent_desc, &anchor_desc, &depends_desc,
};

static ecs_world *world;

static void prepare_world()
{
    world = ecs_world_new();
    assert(ecs_world_register_component(world, &armor_desc, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &weight_desc, NULL) == ECS_OK);
    for (size_t i = 0; i < sizeof relationship_descs / sizeof relationship_descs[0];
         i++) {
        assert(ecs_world_register_relationship(world, relationship_descs[i], NULL) == ECS_OK);
    }
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

static int add_pair(ecs_entity entity, ecs_component_id relationship, ecs_entity target)
{
    int result = ecs_world_record_add_pair(world, entity, relationship, target, 0);
    return result ? result : ecs_world_apply_commands(world);
}

static bool same(ecs_entity left, ecs_entity right)
{
    return ecs_entity_to_u64(left) == ecs_entity_to_u64(right);
}

TEST_CASE(ecs_pairs_index_targets_and_sources)
{
    ecs_entity fan = spawn(), other = spawn(), first = spawn(), second = spawn();
    REQUIRE(add_pair(fan, likes_desc.id, second) == ECS_OK);
    REQUIRE(add_pair(fan, likes_desc.id, first) == ECS_OK);
    REQUIRE(add_pair(other, likes_desc.id, second) == ECS_OK);
    REQUIRE(ecs_has_pair(world, fan, likes_desc.id, first));
    REQUIRE(ecs_pair_target_count(world, fan, likes_desc.id) == 2);
    REQUIRE(same(ecs_pair_target(world, fan, likes_desc.id, 0), first));
    REQUIRE(same(ecs_pair_target(world, fan, likes_desc.id, 1), second));
    REQUIRE(ecs_pair_source_count(world, likes_desc.id, second) == 2);
    REQUIRE(same(ecs_pair_source(world, likes_desc.id, second, 0), fan));
    REQUIRE(same(ecs_pair_source(world, likes_desc.id, second, 1), other));
    REQUIRE(ecs_world_record_add_pair(world, fan, likes_desc.id, first, 0) == ECS_ERR_DUPLICATE);
    REQUIRE(ecs_world_record_remove_pair(world, other, likes_desc.id, first, 0) == ECS_ERR_COMPONENT);
    REQUIRE(ecs_world_record_add_pair(world, fan, armor_desc.id, first, 0) == ECS_ERR_ENTITY);
    REQUIRE(ecs_world_record_remove_pair(world, fan, likes_desc.id, second, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(!ecs_has_pair(world, fan, likes_desc.id, second));
    REQUIRE(ecs_pair_source_count(world, likes_desc.id, second) == 1);
    REQUIRE(same(ecs_pair_source(world, likes_desc.id, second, 0), other));
    REQUIRE(ecs_world_destroy_entity(world, first) == ECS_OK);
    REQUIRE(ecs_pair_target_count(world, fan, likes_desc.id) == 0);
    REQUIRE(ecs_entity_is_alive(world, fan));
    REQUIRE(ecs_world_record_add_pair(world, fan, likes_desc.id, first, 0) == ECS_ERR_ENTITY);
    return true;
}

TEST_CASE(ecs_registration_shares_one_id_space)
{
    ecs_relationship_desc duplicate = likes_desc;
    duplicate.id = armor_desc.id;
    REQUIRE(ecs_world_register_relationship(world, &duplicate, NULL) == ECS_ERR_DUPLICATE);
    REQUIRE(ecs_world_register_relationship(world, &likes_desc, NULL) == ECS_ERR_DUPLICATE);
    ecs_component_desc component = armor_desc;
    component.id = likes_desc.id;
    component.key = "sdk.ecs.other";
    REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_ERR_DUPLICATE);
    ecs_relationship_desc same_key = likes_desc;
    same_key.id = 699;
    REQUIRE(ecs_world_register_relationship(world, &same_key, NULL) == ECS_ERR_DUPLICATE);
    same_key.key = armor_desc.key;
    REQUIRE(ecs_world_register_relationship(world, &same_key, NULL) == ECS_ERR_DUPLICATE);
    component.id = 0;
    component.key = owner_desc.key;
    REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_ERR_DUPLICATE);
    REQUIRE(ecs_world_relationship(world, parent_desc.id)->traits & ECS_REL_ACYCLIC);
    REQUIRE(!(ecs_world_relationship(world, owner_desc.id)->traits & ECS_REL_TRAVERSABLE));
    REQUIRE(!ecs_world_relationship(world, armor_desc.id));
    return true;
}

TEST_CASE(ecs_exclusive_pairs_replace_their_target)
{
    ecs_entity item = spawn(), first = spawn(), second = spawn(), third = spawn();
    REQUIRE(add_pair(item, owner_desc.id, first) == ECS_OK);
    REQUIRE(add_pair(item, owner_desc.id, third) == ECS_OK);
    REQUIRE(ecs_world_record_remove_pair(world, item, owner_desc.id, third, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add_pair(world, item, owner_desc.id, second, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_pair_target_count(world, item, owner_desc.id) == 1);
    REQUIRE(same(ecs_pair_target(world, item, owner_desc.id, 0), second));
    REQUIRE(ecs_pair_source_count(world, owner_desc.id, first) == 0);
    REQUIRE(ecs_pair_source_count(world, owner_desc.id, second) == 1);
    return true;
}

TEST_CASE(ecs_acyclic_pairs_reject_cycles_when_recorded_and_applied)
{
    ecs_entity a = spawn(), b = spawn(), c = spawn();
    REQUIRE(add_pair(a, parent_desc.id, b) == ECS_OK);
    REQUIRE(add_pair(b, parent_desc.id, c) == ECS_OK);
    REQUIRE(ecs_world_record_add_pair(world, c, parent_desc.id, a, 0) == ECS_ERR_CYCLE);
    REQUIRE(ecs_world_record_add_pair(world, c, parent_desc.id, c, 0) == ECS_ERR_CYCLE);
    REQUIRE(ecs_world_record_add_pair(world, c, likes_desc.id, c, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    ecs_entity d = spawn(), e = spawn();
    REQUIRE(ecs_world_record_add_pair(world, d, parent_desc.id, e, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add_pair(world, e, parent_desc.id, d, 1) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_ERR_CYCLE);
    REQUIRE(ecs_has_pair(world, d, parent_desc.id, e));
    REQUIRE(ecs_pair_target_count(world, e, parent_desc.id) == 0);
    return true;
}

static ecs_entity destroyed[8];
static int destroyed_count;

static void trace_destroy(ecs_world *w, ecs_event, ecs_entity entity, ecs_component_id, void *)
{
    assert(destroyed_count < 8 && ecs_entity_is_alive(w, entity));
    destroyed[destroyed_count++] = entity;
}

TEST_CASE(ecs_delete_cleanup_destroys_sources_in_postorder)
{
    ecs_entity root = spawn(), branch = spawn(), leaf = spawn(), sibling = spawn();
    ecs_entity fan = spawn();
    REQUIRE(add_pair(branch, parent_desc.id, root) == ECS_OK);
    REQUIRE(add_pair(leaf, parent_desc.id, branch) == ECS_OK);
    REQUIRE(add_pair(sibling, parent_desc.id, root) == ECS_OK);
    REQUIRE(add_pair(fan, likes_desc.id, branch) == ECS_OK);
    REQUIRE(ecs_world_observe(world, ECS_ON_DESTROY, 0, trace_destroy, NULL) > 0);
    destroyed_count = 0;
    REQUIRE(ecs_world_record_destroy(world, root, 0) == ECS_OK);
    REQUIRE(ecs_world_record_destroy(world, leaf, 1) == ECS_OK);
    REQUIRE(ecs_world_record_add(world, branch, armor_desc.id, NULL, 2) == ECS_OK);
    REQUIRE(ecs_world_record_add_pair(world, fan, likes_desc.id, sibling, 3) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(destroyed_count == 4);
    REQUIRE(same(destroyed[0], leaf) && same(destroyed[1], branch));
    REQUIRE(same(destroyed[2], sibling) && same(destroyed[3], root));
    REQUIRE(ecs_world_live_count(world) == 1 && ecs_entity_is_alive(world, fan));
    REQUIRE(ecs_pair_target_count(world, fan, likes_desc.id) == 0);
    REQUIRE(!ecs_entity_is_retiring(world, spawn()));
    return true;
}

/* S depends on root and on T, which also depends on root. */
TEST_CASE(ecs_delete_cleanup_destroys_shared_sources_before_their_targets)
{
    ecs_entity root = spawn(), s = spawn(), t = spawn();
    REQUIRE(add_pair(s, depends_desc.id, root) == ECS_OK);
    REQUIRE(add_pair(t, depends_desc.id, root) == ECS_OK);
    REQUIRE(add_pair(s, depends_desc.id, t) == ECS_OK);
    REQUIRE(ecs_world_observe(world, ECS_ON_DESTROY, 0, trace_destroy, NULL) > 0);
    destroyed_count = 0;
    REQUIRE(ecs_world_destroy_entity(world, root) == ECS_OK);
    REQUIRE(destroyed_count == 3);
    REQUIRE(same(destroyed[0], s) && same(destroyed[1], t) && same(destroyed[2], root));
    return true;
}

TEST_CASE(ecs_panic_cleanup_fails_the_sync_point)
{
    ecs_entity root = spawn(), mount = spawn(), rider = spawn();
    REQUIRE(add_pair(mount, parent_desc.id, root) == ECS_OK);
    REQUIRE(add_pair(rider, anchor_desc.id, mount) == ECS_OK);
    REQUIRE(ecs_world_destroy_entity(world, root) == ECS_ERR_COMMAND);
    REQUIRE(ecs_entity_is_alive(world, root) && !ecs_entity_is_retiring(world, root));
    REQUIRE(ecs_entity_is_alive(world, mount) && !ecs_entity_is_retiring(world, mount));
    REQUIRE(ecs_has_pair(world, mount, parent_desc.id, root));
    REQUIRE(ecs_world_destroy_entity(world, mount) == ECS_ERR_COMMAND);
    REQUIRE(ecs_world_record_remove_pair(world, rider, anchor_desc.id, mount, 0) == ECS_OK);
    REQUIRE(ecs_world_record_destroy(world, mount, 1) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(!ecs_entity_is_alive(world, mount) && ecs_entity_is_alive(world, rider));
    return true;
}

TEST_CASE(ecs_pairs_target_entities_created_in_the_same_sync_point)
{
    ecs_entity child = spawn();
    ecs_entity parent = ecs_world_record_create(world, 1);
    REQUIRE(ecs_world_record_add_pair(world, child, parent_desc.id, parent, 0) == ECS_ERR_ENTITY);
    REQUIRE(ecs_world_record_add_pair(world, child, parent_desc.id, parent, 1) == ECS_OK);
    REQUIRE(ecs_world_record_add_pair(world, parent, parent_desc.id, child, 1) == ECS_ERR_ENTITY);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_has_pair(world, child, parent_desc.id, parent));
    return true;
}

TEST_CASE(ecs_pairs_survive_entity_growth_and_feed_the_world_hash)
{
    ecs_entity target = spawn();
    uint64_t before = ecs_world_hash(world);
    enum
    {
        COUNT = 600
    };
    ecs_entity sources[COUNT];
    for (int i = 0; i < COUNT; i++) {
        sources[i] = ecs_world_record_create(world, (uint64_t)i);
    }
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    for (int i = 0; i < COUNT; i++) {
        REQUIRE(ecs_world_record_add_pair(world, sources[i], owner_desc.id, target, 0) == ECS_OK);
    }
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_pair_source_count(world, owner_desc.id, target) == COUNT);
    REQUIRE(same(ecs_pair_source(world, owner_desc.id, target, COUNT - 1), sources[COUNT - 1]));
    REQUIRE(same(ecs_pair_target(world, sources[COUNT - 1], owner_desc.id, 0), target));
    uint64_t linked = ecs_world_hash(world);
    REQUIRE(linked != before);
    REQUIRE(ecs_world_destroy_entity(world, target) == ECS_OK);
    REQUIRE(ecs_pair_target_count(world, sources[0], owner_desc.id) == 0);
    return true;
}

static int added[4];
static int added_count;

static void trace_add(ecs_world *, ecs_event, ecs_entity, ecs_component_id component, void *)
{
    assert(added_count < 4);
    added[added_count++] = (int)component;
}

TEST_CASE(ecs_with_adds_implied_components_once)
{
    REQUIRE(ecs_world_add_with(world, armor_desc.id, weight_desc.id) == ECS_OK);
    REQUIRE(ecs_world_add_with(world, armor_desc.id, weight_desc.id) == ECS_ERR_DUPLICATE);
    REQUIRE(ecs_world_add_with(world, armor_desc.id, armor_desc.id) == ECS_ERR_COMPONENT);
    REQUIRE(ecs_world_add_with(world, weight_desc.id, armor_desc.id) == ECS_OK);
    REQUIRE(ecs_world_observe(world, ECS_ON_ADD, armor_desc.id, trace_add, NULL) > 0);
    REQUIRE(ecs_world_observe(world, ECS_ON_ADD, weight_desc.id, trace_add, NULL) > 0);
    ecs_entity bare = spawn(), weighed = spawn(), both = spawn();
    int armor = 4, weight = 9;
    REQUIRE(ecs_world_record_add(world, both, armor_desc.id, &armor, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(world, both, weight_desc.id, &weight, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(world, weighed, weight_desc.id, &weight, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_component_has(world, weighed, armor_desc.id));
    added_count = 0;
    REQUIRE(ecs_world_record_add(world, bare, armor_desc.id, &armor, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(
        added_count == 2 && added[0] == (int)weight_desc.id &&
        added[1] == (int)armor_desc.id
    );
    REQUIRE(*(const int *)ecs_component_data_const(world, bare, weight_desc.id) == 0);
    REQUIRE(*(const int *)ecs_component_data_const(world, weighed, weight_desc.id) == 9);
    REQUIRE(*(const int *)ecs_component_data_const(world, both, weight_desc.id) == 9);
    REQUIRE(*(const int *)ecs_component_data_const(world, both, armor_desc.id) == 4);
    /* A plain implied component stays when its source goes. */
    REQUIRE(ecs_world_record_remove(world, bare, armor_desc.id, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_component_has(world, bare, weight_desc.id));
    return true;
}

TEST_CASE(ecs_derived_components_follow_their_source_and_reject_writes)
{
    REQUIRE(ecs_world_register_component(world, &total_desc, NULL) == ECS_OK);
    REQUIRE(ecs_world_register_component(world, &cover_desc, NULL) == ECS_OK);
    REQUIRE(ecs_world_register_component(world, &shield_desc, NULL) == ECS_OK);
    ecs_component_desc itself = shield_desc;
    itself.id = 506, itself.key = "sdk.ecs.itself", itself.derived_from = 506;
    REQUIRE(ecs_world_register_component(world, &itself, NULL) == ECS_ERR_ARGUMENT);
    ecs_entity armored = spawn(), shielded = spawn();
    int value = 3;
    REQUIRE(ecs_world_record_add(world, armored, armor_desc.id, &value, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(world, shielded, shield_desc.id, &value, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_component_has(world, armored, total_desc.id));
    REQUIRE(!ecs_component_has(world, armored, cover_desc.id));
    REQUIRE(ecs_component_has(world, shielded, cover_desc.id));
    pin_value number = { .type = PIN_I32, .type_name = "int", .i = 7 };
    REQUIRE(!ecs_component_accepts_value(world, total_desc.id, 0, number));
    REQUIRE(ecs_component_accepts_value(world, armor_desc.id, 0, number));
    REQUIRE(
        ecs_component_write_value(world, armored, total_desc.id, 0, number, number) == ECS_ERR_ARGUMENT
    );
    REQUIRE(ecs_component_write_field(world, armored, total_desc.id, 0, number) == ECS_ERR_ARGUMENT);
    *(int *)ecs_component_data(world, armored, total_desc.id) = 7;
    REQUIRE(*(const int *)ecs_component_data_const(world, armored, total_desc.id) == 7);
    /* One registered after its source already has rows comes on them too. */
    ecs_component_desc late = total_desc;
    late.id = 507, late.key = "sdk.ecs.late", late.name = "Late";
    REQUIRE(ecs_world_register_component(world, &late, NULL) == ECS_OK);
    REQUIRE(
        ecs_component_has(world, armored, late.id) &&
        !ecs_component_has(world, shielded, late.id)
    );
    REQUIRE(ecs_world_record_remove(world, armored, armor_desc.id, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(!ecs_component_has(world, armored, total_desc.id));
    REQUIRE(!ecs_component_has(world, armored, late.id));
    /* An automatic id never takes a source still to come. */
    ecs_component_id next = 1;
    while (ecs_world_component(world, next) || ecs_world_relationship(world, next)) {
        next++;
    }
    ecs_component_desc early = total_desc, unrelated = shield_desc, source = shield_desc;
    early.id = 0, early.key = "sdk.ecs.early", early.name = "Early",
    early.derived_from = next;
    unrelated.id = 0, unrelated.key = "sdk.ecs.unrelated", unrelated.name = "Unrelated";
    source.id = next, source.key = "sdk.ecs.source", source.name = "Source";
    ecs_component_id early_id = 0, unrelated_id = 0;
    REQUIRE(ecs_world_register_component(world, &early, &early_id) == ECS_OK);
    REQUIRE(ecs_world_register_component(world, &unrelated, &unrelated_id) == ECS_OK);
    REQUIRE(early_id != next && unrelated_id != next);
    REQUIRE(ecs_world_register_component(world, &source, NULL) == ECS_OK);
    REQUIRE(ecs_world_record_add(world, shielded, next, &value, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_component_has(world, shielded, early_id));
    return true;
}

void test_tool_ecs_relationships()
{
    pu_setup(prepare_world, release_world);
    RUN_TEST_CASE(ecs_pairs_index_targets_and_sources);
    RUN_TEST_CASE(ecs_registration_shares_one_id_space);
    RUN_TEST_CASE(ecs_exclusive_pairs_replace_their_target);
    RUN_TEST_CASE(ecs_acyclic_pairs_reject_cycles_when_recorded_and_applied);
    RUN_TEST_CASE(ecs_delete_cleanup_destroys_sources_in_postorder);
    RUN_TEST_CASE(ecs_delete_cleanup_destroys_shared_sources_before_their_targets);
    RUN_TEST_CASE(ecs_panic_cleanup_fails_the_sync_point);
    RUN_TEST_CASE(ecs_pairs_target_entities_created_in_the_same_sync_point);
    RUN_TEST_CASE(ecs_pairs_survive_entity_growth_and_feed_the_world_hash);
    RUN_TEST_CASE(ecs_with_adds_implied_components_once);
    RUN_TEST_CASE(ecs_derived_components_follow_their_source_and_reject_writes);
    pu_clear_setup();
}
