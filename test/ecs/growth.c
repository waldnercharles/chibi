#include "ecs/execution.h"
#include "test/suite_internal.h"

void run_ecs_growth_checks()
{
    enum
    {
        COUNT = 20001
    };
    ecs_world *world = ecs_world_new();
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL));
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL));
    ecs_entity first = ecs_world_create_entity(world), last = first;
    assert(!GAME_GameHealth_add(world, first, 0));
    assert(!ecs_world_apply_commands(world));
    *GAME_GameHealth_get(world, first) = (GameHealth){ 75, 100 };
    const GameHealth *original = GAME_GameHealth_get_const(world, first);
    for (int i = 1; i < COUNT; i++) {
        last = ecs_world_record_create(world, (uint64_t)i);
        assert(last.slot == (uint32_t)i + 1 && !ecs_entity_is_alive(world, last));
    }
    assert(!ecs_world_apply_commands(world));
    test_check(
        ecs_world_live_count(world) == COUNT &&
            ecs_world_entity_capacity(world) >= COUNT &&
            original == GAME_GameHealth_get_const(world, first) &&
            original->current == 75,
        "20,001 entities grow automatically without relocating unused "
        "component rows"
    );
    ecs_component_id ids[] = { GAME_COMPONENT_GameTransform.id,
                               GAME_COMPONENT_GameHealth.id };
    for (uint32_t slot = 1; slot <= COUNT; slot++) {
        ecs_entity entity = { slot, ecs_world_generation(world, slot) };
        assert(ecs_entity_is_alive(world, entity));
        if (slot % 2) {
            GameTransform value = { (float)slot, -(float)slot };
            assert(!ecs_world_record_add(world, entity, ids[0], &value, 0));
        }
        if (slot != 1 && slot % 3 == 0) {
            GameHealth value = { (int)slot, COUNT };
            assert(!ecs_world_record_add(world, entity, ids[1], &value, 0));
        }
    }
    assert(!ecs_world_apply_commands(world));
    const ecs_execution_view *view = (const ecs_execution_view *)world;
    uint32_t high = ecs_execution_entity(view, ecs_entity_to_u64(last));
    const ecs_execution_column *column = ecs_execution_component(view, high, ids[0]);
    assert(column);
    assert(column->count == 10001);
    const ecs_execution_column *health = ecs_execution_find_column(view, ids[1]);
    assert(health && health->count == 6668);
    const GameTransform *row = (const GameTransform *)((const char *)column->data +
                                                       high * column->stride);
    test_check(
        high == COUNT && row->x == COUNT && row->y == -COUNT &&
            GAME_GameHealth_get_const(world, first)->current == 75,
        "grown compiled execution view preserves low rows and addresses high "
        "rows"
    );
    ecs_query query;
    ecs_entity entity;
    assert(!ecs_query_begin(world, &query, ids, 2));
    uint32_t previous = 0;
    int count = 0;
    while (ecs_query_next(&query, &entity)) {
        assert(entity.slot > previous && (entity.slot == 1 || entity.slot % 6 == 3));
        previous = entity.slot;
        count++;
    }
    ecs_query_end(&query);
    test_check(count == 3335, "mixed dense and bitset queries stay ordered across capacity growth");

    /* Registration after growth must create an empty index covering all slots. */
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameVelocity, NULL));
    assert(!GAME_GameVelocity_has(world, last));
    assert(!GAME_GameVelocity_add(world, last, 0));
    assert(!ecs_world_apply_commands(world));
    column = ecs_execution_find_column(view, GAME_COMPONENT_GameVelocity.id);
    assert(column && column->count == 1 && column->singleton == last.slot);
    test_check(GAME_GameVelocity_get_const(world, last)->x == 0, "late component registration initializes high rows");

    uint32_t capacity = ecs_world_entity_capacity(world);
    assert(!ecs_query_begin(world, &query, ids, 1));
    test_check(
        ecs_world_reserve_entities(world, capacity + 1) == ECS_ERR_ITERATING &&
            !ecs_world_record_create(world, 0).slot &&
            ecs_world_register_component(world, &GAME_COMPONENT_GameMovement, NULL) ==
                ECS_ERR_ITERATING,
        "active queries reject growth and registration that would invalidate "
        "their rows"
    );
    ecs_query_end(&query);
    test_check(
        ecs_world_reserve_entities(world, UINT32_MAX) == ECS_ERR_CAPACITY &&
            ecs_world_entity_capacity(world) == capacity &&
            !ecs_entity_is_alive(world, (ecs_entity){ UINT32_MAX, 1 }),
        "unrepresentable reservations and out-of-range handles leave the world "
        "intact"
    );
    assert(!ecs_world_record_destroy(world, last, 0));
    assert(ecs_entity_is_retiring(world, last));
    assert(!ecs_world_reserve_entities(world, capacity + 1));
    assert(ecs_entity_is_retiring(world, last));
    ecs_world_clear_commands(world);
    assert(!ecs_entity_is_retiring(world, last) && ecs_entity_is_alive(world, last));
    assert(!ecs_world_destroy_entity(world, last));
    assert(!ecs_world_destroy_entity(world, first));
    column = ecs_execution_find_column(view, GAME_COMPONENT_GameVelocity.id);
    assert(column && !column->count && !column->singleton);
    assert(ecs_execution_find_column(view, ids[0])->count == 9999);
    assert(ecs_execution_find_column(view, ids[1])->count == 6666);
    ecs_entity reused = ecs_world_record_create(world, 0);
    assert(reused.slot == first.slot && reused.generation != first.generation);
    ecs_world_clear_commands(world);
    ecs_entity retry = ecs_world_create_entity(world);
    test_check(
        retry.slot == first.slot && retry.generation != reused.generation &&
            !ecs_entity_is_alive(world, reused) &&
            !ecs_entity_is_alive(world, first) && !GAME_GameHealth_has(world, retry),
        "lowest free slot reuse invalidates destroyed and canceled handles and "
        "clears occupancy"
    );
    ecs_entity high_reuse = ecs_world_create_entity(world);
    test_check(
        high_reuse.slot == last.slot && high_reuse.generation != last.generation,
        "free-slot cursor skips the occupied prefix after low-slot reuse"
    );
    ecs_world_free(world);
}
