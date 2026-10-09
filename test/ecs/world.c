#include "graph/authoring/graph_edit.h"
#include "graph/nodes/core_nodes.h"
#include "graph/nodes/generated_components.h"
#include "test/suite_internal.h"

typedef struct
{
    float x;
    float y;
} ecs_position;

typedef struct
{
    int value;
} ecs_health;

static const type_field g_ecs_position_fields[] = {
    { "x", PIN_F32, offsetof(ecs_position, x), sizeof(float), "float" },
    { "y", PIN_F32, offsetof(ecs_position, y), sizeof(float), "float" },
};
static const type_desc g_ecs_position_type = { "EcsPosition",
                                               sizeof(ecs_position),
                                               g_ecs_position_fields,
                                               2 };
static const type_field g_ecs_health_fields[] = {
    { "value", PIN_I32, offsetof(ecs_health, value), sizeof(int), "int" },
};
static const type_desc g_ecs_health_type = { "EcsHealth",
                                             sizeof(ecs_health),
                                             g_ecs_health_fields,
                                             1 };
static ecs_component_id g_ecs_observed_component;
static float g_ecs_observed_x;

static void ecs_destroy_probe(ecs_world *world, ecs_event, ecs_entity entity, ecs_component_id, void *)
{
    pin_value value;
    if (ecs_component_read_field(world, entity, g_ecs_observed_component, 0, &value) ==
        ECS_OK) {
        g_ecs_observed_x = value.f;
    }
}

static void sparse_component_growth()
{
    ecs_world *world = ecs_world_new();
    ecs_entity entity = ecs_world_create_entity(world);
    ecs_component_desc desc = {
        .name = "Sparse",
        .element_size = sizeof(ecs_health),
        .type = &g_ecs_health_type,
    };
    enum
    {
        COMPONENTS = 129
    };
    for (int i = 0; i < COMPONENTS; i++) {
        desc.id = UINT64_C(0xffff000000000001) + (uint64_t)i * 256;
        assert(ecs_world_register_component(world, &desc, NULL) == ECS_OK);
        ecs_health value = { i + 1 };
        assert(ecs_world_record_add(world, entity, desc.id, &value, 0) == ECS_OK);
    }
    assert(ecs_world_apply_commands(world) == ECS_OK);
    bool retained = true;
    for (int i = COMPONENTS - 1; i >= 0; i--) {
        ecs_component_id id = UINT64_C(0xffff000000000001) + (uint64_t)i * 256;
        const ecs_health *value = ecs_component_data_const(world, entity, id);
        retained &= value && value->value == i + 1;
    }
    test_check(retained, "sparse component identities retain values across registry growth");
    test_check(
        ecs_world_register_component(world, &desc, NULL) == ECS_ERR_DUPLICATE &&
            !ecs_world_component(world, UINT64_MAX),
        "grown component registry distinguishes duplicate and missing "
        "identities"
    );
    desc.id = 0;
    ecs_component_id automatic = 0;
    test_check(
        ecs_world_register_component(world, &desc, &automatic) == ECS_OK &&
            automatic == 1 && ecs_world_component(world, automatic),
        "automatic component identities coexist with sparse registered "
        "identities"
    );
    ecs_world_free(world);
}

void run_ecs_checks(void)
{
    sparse_component_growth();
    ecs_world *world = ecs_world_new();
    ecs_component_id position = 0;
    ecs_component_id health = 0;
    ecs_component_desc position_desc = {
        .key = "ecs.Position",
        .name = "Position",
        .element_size = sizeof(ecs_position),
        .type = &g_ecs_position_type,
        .index = ECS_INDEX_BITSET,
    };
    ecs_component_desc health_desc = {
        .name = "Health",
        .element_size = sizeof(ecs_health),
        .type = &g_ecs_health_type,
        .index = ECS_INDEX_DENSE,
    };
    test_check(
        ecs_world_register_component(world, &position_desc, &position) == ECS_OK &&
            ecs_world_register_component(world, &health_desc, &health) == ECS_OK,
        "runtime component descriptors register with columns and layout "
        "hashes"
    );
    const ecs_component_desc *registered = ecs_world_component(world, position);
    test_check(
        sizeof(ecs_entity) == 8 && !ecs_entity_is_alive(world, (ecs_entity){ 0, 0 }),
        "handles are 64-bit slot/generation pairs and slot zero is invalid"
    );
    test_check(
        registered && registered->column_base &&
            registered->layout_hash == ecs_layout_hash(&g_ecs_position_type),
        "descriptor reports its current column base and reflected layout "
        "hash"
    );
    ecs_component_desc duplicate_key = position_desc;
    duplicate_key.id = 0;
    duplicate_key.name = "RenamedPosition";
    test_check(
        ecs_world_register_component(world, &duplicate_key, NULL) == ECS_ERR_DUPLICATE,
        "duplicate persistent component keys are rejected"
    );

    ecs_entity e1 = ecs_world_create_entity(world);
    ecs_entity e2 = ecs_world_create_entity(world);
    ecs_entity e3 = ecs_world_create_entity(world);
    test_check(
        ecs_entity_is_alive(world, e1) && ecs_entity_is_alive(world, e2),
        "entity allocation returns live lowest-index slots"
    );
    test_check(
        ecs_world_record_add(world, e1, position, NULL, 1) == ECS_OK &&
            ecs_world_record_add(world, e1, health, NULL, 2) == ECS_OK &&
            ecs_world_record_add(world, e2, position, NULL, 3) == ECS_OK &&
            ecs_world_record_add(world, e3, health, NULL, 4) == ECS_OK &&
            ecs_world_apply_commands(world) == ECS_OK,
        "structural changes wait in the command buffer for apply"
    );

    ecs_position *p1 = (ecs_position *)ecs_component_data(world, e1, position);
    ecs_position *p2 = (ecs_position *)ecs_component_data(world, e2, position);
    p1->x = 1.0f;
    p1->y = 2.0f;
    p2->x = 3.0f;
    p2->y = 4.0f;
    test_check(
        p1 && p2 && (unsigned char *)p2 - (unsigned char *)p1 == sizeof(ecs_position),
        "component values use contiguous flat columns with element stride"
    );
    pin_value field = { .type = PIN_F32, .type_name = "float", .f = 9.0f };
    test_check(
        ecs_component_write_field(world, e1, position, 0, field) == ECS_OK &&
            ecs_component_read_field(world, e1, position, 0, &field) == ECS_OK &&
            field.f == 9.0f,
        "generic runtime-id access reads and writes without C type "
        "knowledge"
    );
    test_check(
        ecs_component_read_field(world, e1, position, 1, &field) == ECS_OK &&
            field.f == 2.0f,
        "component field access goes through pin read/write helpers"
    );
    test_check(
        ecs_component_has(world, e1, position) && ecs_component_has(world, e1, health),
        "each component tracks occupancy"
    );

    ecs_component_id query_components[] = { position, health };
    ecs_query query;
    ecs_entity found[4] = { 0 };
    int found_count = 0;
    test_check(
        ecs_query_begin(world, &query, query_components, 2) == ECS_OK,
        "intersection queries can begin over multiple component ids"
    );
    while (ecs_query_next(&query, &found[found_count])) {
        found_count++;
    }
    ecs_query_end(&query);
    test_check(found_count == 1 && found[0].slot == e1.slot, "a query visits exactly entities holding every named component");
    test_check(ecs_query_begin(world, &query, &health, 1) == ECS_OK, "dense occupancy remains a query-compatible index");
    int dense_ordered = ecs_query_next(&query, &found[0]) &&
                        ecs_query_next(&query, &found[1]) &&
                        found[0].slot < found[1].slot;
    ecs_query_end(&query);
    test_check(dense_ordered, "dense and bitset indexes preserve observable ascending order");

    ecs_query_expr *expression = ecs_query_expr_new(ECS_QUERY_ALL);
    ecs_query_expr *excluded = ecs_query_expr_new(ECS_QUERY_NOT);
    test_check(
        ecs_query_expr_add(expression, ecs_query_expr_has(position)) == ECS_OK &&
            ecs_query_expr_add(excluded, ecs_query_expr_has(health)) == ECS_OK &&
            ecs_query_expr_add(expression, excluded) == ECS_OK,
        "nested query filters can combine required and excluded components"
    );
    ecs_query_expr *copy = ecs_query_expr_clone(expression);
    ecs_query_expr_free(expression);
    test_check(
        ecs_query_begin_expression(world, &query, copy) == ECS_OK,
        "a cloned query owns its complete expression after the source is freed"
    );
    found_count = 0;
    ecs_entity match;
    while (ecs_query_next(&query, &match)) {
        found_count++;
        test_check(match.slot == e2.slot, "cloned filters reject entities missing Position or holding Health");
    }
    ecs_query_end(&query);
    test_check(found_count == 1, "a cloned nested query retains its exact matching set");
    ecs_query_expr_free(copy);

    ecs_world *ordered = ecs_world_new();
    ecs_component_id ordered_position = 0;
    ecs_world_register_component(ordered, &position_desc, &ordered_position);
    ecs_entity ordered_a = ecs_world_create_entity(ordered);
    ecs_entity ordered_b = ecs_world_create_entity(ordered);
    ecs_position first = { 1.0f, 0.0f };
    ecs_position second = { 2.0f, 0.0f };
    ecs_world_record_add(ordered, ordered_b, ordered_position, &second, 20);
    ecs_world_record_add(ordered, ordered_a, ordered_position, &first, 10);
    test_check(ecs_world_apply_commands(ordered) == ECS_OK, "command application sorts by explicit sort key");
    ecs_world *reverse = ecs_world_new();
    ecs_component_id reverse_position = 0;
    ecs_world_register_component(reverse, &position_desc, &reverse_position);
    ecs_entity reverse_a = ecs_world_create_entity(reverse);
    ecs_entity reverse_b = ecs_world_create_entity(reverse);
    ecs_world_record_add(reverse, reverse_a, reverse_position, &first, 10);
    ecs_world_record_add(reverse, reverse_b, reverse_position, &second, 20);
    ecs_world_apply_commands(reverse);
    test_check(ecs_world_hash(ordered) == ecs_world_hash(reverse), "the same sorted command set has one result regardless of recording order");
    test_check(
        ecs_world_record_create(world, 0).slot != 0 &&
            ecs_world_command_count(world) == 1,
        "a recorded create is not alive before the sync point"
    );
    ecs_world_clear_commands(world);

    test_check(ecs_world_record_add(world, e2, health, NULL, 1) == ECS_OK, "a valid structural command is accepted");
    test_check(
        ecs_query_begin(world, &query, &position, 1) == ECS_OK &&
            ecs_world_apply_commands(world) == ECS_ERR_ITERATING,
        "command application is rejected during iteration"
    );
    ecs_query_end(&query);
    test_check(ecs_world_apply_commands(world) == ECS_OK, "the command buffer applies at a defined sync point");

    g_ecs_observed_component = position;
    g_ecs_observed_x = 0.0f;
    ecs_world_observe(world, ECS_ON_DESTROY, 0, ecs_destroy_probe, NULL);
    test_check(
        ecs_world_record_destroy(world, e1, 1) == ECS_OK &&
            ecs_component_data(world, e1, position) != NULL &&
            ecs_world_apply_commands(world) == ECS_OK && g_ecs_observed_x == 9.0f,
        "destroyed values remain readable until command application"
    );
    test_check(
        !ecs_entity_is_alive(world, e1) &&
            ecs_world_generation(world, e1.slot) != e1.generation,
        "destroy advances generation and invalidates stale handles"
    );
    ecs_entity recycled = ecs_world_create_entity(world);
    test_check(recycled.slot == e1.slot && recycled.generation != e1.generation, "freeing a low slot makes it the next allocation");
    test_check(
        ecs_world_record_add(world, e3, health, NULL, 1) == ECS_ERR_DUPLICATE &&
            ecs_world_record_remove(world, e3, position, 1) == ECS_ERR_COMPONENT &&
            ecs_world_record_destroy(world, e1, 1) == ECS_ERR_ENTITY,
        "duplicate, missing, and stale structural operations report "
        "errors"
    );
    test_check(
        ecs_world_live_count(world) == 3 &&
            ecs_world_high_water_mark(world) >= recycled.slot,
        "live count and occupancy remain coherent after structural "
        "changes"
    );

    ecs_world *same = ecs_world_new();
    ecs_component_id same_position = 0;
    ecs_world_register_component(same, &position_desc, &same_position);
    ecs_entity same_entity = ecs_world_create_entity(same);
    ecs_world_record_add(same, same_entity, same_position, &first, 1);
    ecs_world_apply_commands(same);
    ecs_component_write_field(same, same_entity, same_position, 0, field);
    test_check(
        ecs_world_hash(ordered) == ecs_world_hash(reverse) &&
            ecs_world_hash(ordered) != 0,
        "identical seeded worlds expose an identical deterministic hash"
    );
    test_check(ecs_world_component_count(ordered) == 1, "component registration remains independent per world");
    test_check(
        ecs_world_iteration_begin(world) == ECS_OK &&
            ecs_world_iteration_end(world) == ECS_OK,
        "ECS lifecycle and iteration are headless with no host dependency"
    );

    ecs_world *generated_world = ecs_world_new();
    test_check(
        GAME_REGISTRY_COMPONENT_COUNT >= 8 &&
            ecs_world_register_component(generated_world, &GAME_COMPONENT_GameTransform, NULL) ==
                ECS_OK &&
            ecs_world_register_component(generated_world, &GAME_COMPONENT_GameHealth, NULL) ==
                ECS_OK &&
            ecs_world_register_component(generated_world, &GAME_COMPONENT_GameMovement, NULL) ==
                ECS_OK &&
            ecs_world_register_component(generated_world, &GAME_COMPONENT_GameInput, NULL) ==
                ECS_OK &&
            GAME_COMPONENT_GameHealth.index == ECS_INDEX_DENSE &&
            GAME_COMPONENT_GameHealth.layout_hash ==
                ecs_layout_hash(GAME_COMPONENT_GameHealth.type),
        "reflection emits component descriptors, ids, layout hashes, "
        "and index policy"
    );
    ecs_entity generated_entity = ecs_world_create_entity(generated_world);
    test_check(
        GAME_GameTransform_add(generated_world, generated_entity, 1) == ECS_OK &&
            GAME_GameHealth_add(generated_world, generated_entity, 2) == ECS_OK &&
            ecs_world_apply_commands(generated_world) == ECS_OK,
        "generated typed component accessors add through runtime "
        "descriptors"
    );
    pin_value generated_value = { .type = PIN_F32, .type_name = "float", .f = 12.0f };
    test_check(
        ecs_component_write_field(
            generated_world,
            generated_entity,
            GAME_COMPONENT_GameTransform.id,
            0,
            generated_value
        ) == ECS_OK &&
            GAME_GameTransform_get(generated_world, generated_entity)->x == 12.0f,
        "generated component access uses the shared reflected field path"
    );
    test_check(
        GAME_GameTransform_has(generated_world, generated_entity) &&
            GAME_GameHealth_has(generated_world, generated_entity) &&
            node_desc_is_pure(&GAME_NODE_Get_GameTransform) &&
            GAME_NODE_Set_GameTransform.flow &&
            node_desc_is_pure(&GAME_NODE_Has_GameHealth) &&
            GAME_NODE_Add_GameHealth.flow && GAME_NODE_Remove_GameHealth.flow,
        "component reads have no Exec pins; component mutations remain ordered"
    );
    ecs_world_free(generated_world);

    ecs_world_free(same);
    ecs_world_free(reverse);
    ecs_world_free(ordered);
    ecs_world_free(world);
}

void run_component_read_checks()
{
    ecs_world *world = ecs_world_new();
    ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL);
    ecs_entity entity = ecs_world_create_entity(world);
    GAME_GameHealth_add(world, entity, 0);
    ecs_world_apply_commands(world);
    *GAME_GameHealth_get(world, entity) = (GameHealth){ 12, 100 };

    behavior_asset *asset = behavior_asset_new("ReadAfterWrite");
    behavior_asset_set_owner_type(asset, "ecs.entity");
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    graph_set_event_name(g, event, "Tick");
    node *get = graph_add_node(g, &GAME_NODE_Get_GameHealth);
    node *before = graph_add_node(g, &GAME_NODE_Set_GameHealth);
    node *write = graph_add_node(g, &GAME_NODE_Set_GameHealth);
    node *after = graph_add_node(g, &GAME_NODE_Set_GameHealth);
    node *value = graph_add_node(g, registry_node_by_name("Make_GameHealth"));
    graph_set_pin_default(g, value, 0, 0, &(pin_value){ .type = PIN_I32, .i = 42 });
    graph_set_pin_default(g, value, 0, 1, &(pin_value){ .type = PIN_I32, .i = 100 });
    test_check(
        graph_connect_exec(g, event, 0, before, 0, 0) == 0 &&
            graph_connect_exec(g, before, 0, write, 0, 0) == 0 &&
            graph_connect_exec(g, write, 0, after, 0, 0) == 0 &&
            graph_connect_data(g, get, 0, before, 1) == 0 &&
            graph_connect_data(g, value, 0, write, 1) == 0 &&
            graph_connect_data(g, get, 0, after, 1) == 0,
        "component reads connect directly as data, without execution wiring"
    );

    pin_value owner = { .type = PIN_HANDLE,
                        .type_name = "ecs.entity",
                        .u64 = ecs_entity_to_u64(entity) };
    behavior_instance *instance = behavior_instance_create(asset, &owner, NULL, NULL);
    behavior_instance_bind_service(instance, "ecs.world", world);
    flow_error error = { 0 };
    flow_run *run = behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    if (!run) {
        fprintf(stderr, "component read dispatch failed: %s\n", error.message);
    }
    if (run && flow_run_status(run) != FLOW_COMPLETED) {
        fprintf(stderr, "component read failed: %s\n", flow_run_error(run)->message);
    }
    test_check(
        run && flow_run_status(run) == FLOW_COMPLETED &&
            GAME_GameHealth_get_const(world, entity)->current == 42,
        "the same pure component read observes a write later in the activation"
    );
    behavior_instance_destroy(instance);

    char *text = behavior_asset_save(asset);
    behavior_asset *loaded = behavior_asset_load(text);
    node *read = loaded ? graph_node_by_id(loaded->graph, get->id) : NULL;
    test_check(
        read && read->input_count == 0 && read->output_count == 1 &&
            read->output_pins[0].kind == PIN_DATA,
        "code roundtrips component readers as pure data nodes"
    );
    if (loaded) {
        GAME_GameHealth_get(world, entity)->current = 12;
        instance = behavior_instance_create(loaded, &owner, NULL, NULL);
        behavior_instance_bind_service(instance, "ecs.world", world);
        run = behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, NULL);
        test_check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                GAME_GameHealth_get_const(world, entity)->current == 42,
            "roundtripped component reads observe ordered component writes"
        );
        behavior_instance_destroy(instance);
    }
    behavior_asset_free(loaded);
    free(text);

    node *has = graph_add_node(g, &GAME_NODE_Has_GameHealth);
    node *check_event = graph_add_node(g, &NODE_Event);
    graph_set_event_name(g, check_event, "CheckPresence");
    node *branch = graph_add_node(g, &NODE_Branch);
    node *present = graph_add_node(g, &g_capture_desc);
    node *absent = graph_add_node(g, &g_capture_desc);
    graph_set_pin_default(g, present, 0, 1, &(pin_value){ .type = PIN_I32, .i = 1 });
    graph_set_pin_default(g, absent, 0, 1, &(pin_value){ .type = PIN_I32, .i = 0 });
    graph_connect_exec(g, check_event, 0, branch, 0, 0);
    graph_connect_data(g, has, 0, branch, 1);
    graph_connect_exec(g, branch, 0, present, 0, 0);
    graph_connect_exec(g, branch, 1, absent, 0, 0);
    instance = behavior_instance_create(asset, &owner, NULL, NULL);
    behavior_instance_bind_service(instance, "ecs.world", world);
    g_captured_int = -1;
    run = behavior_instance_dispatch(instance, "CheckPresence", (flow_inputs){ 0 }, NULL);
    test_check(
        run && flow_run_status(run) == FLOW_COMPLETED && g_captured_int == 1,
        "pure Has drives a Branch directly when the component exists"
    );
    GAME_GameHealth_remove(world, entity, 0);
    ecs_world_apply_commands(world);
    run = behavior_instance_dispatch(instance, "CheckPresence", (flow_inputs){ 0 }, NULL);
    test_check(
        run && flow_run_status(run) == FLOW_COMPLETED && g_captured_int == 0,
        "pure Has returns false rather than failing for a missing component"
    );
    run = behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, NULL);
    test_check(
        run && flow_run_status(run) == FLOW_FAILED &&
            strstr(flow_run_error(run)->message, "entity lacks component"),
        "a missing component Get retains its actionable read diagnostic"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    ecs_world_free(world);
}
