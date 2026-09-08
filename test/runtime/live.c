#include "test/suite_internal.h"

behavior_asset *make_live_behavior_asset(void)
{
    behavior_asset *bp = behavior_asset_new("LiveWorking");
    if (!bp || behavior_asset_set_owner_type(bp, "ecs.entity") != 0) {
        behavior_asset_free(bp);
        return NULL;
    }
    behavior_variable *counter = behavior_asset_add_variable(
        bp,
        "Counter",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_I32, .i = 0 },
        "Live"
    );
    node *begin = graph_add_node(bp->graph, registry_node_by_name("Event"));
    node *tick = graph_add_node(bp->graph, registry_node_by_name("Event"));
    node *input = graph_add_node(bp->graph, registry_node_by_name("Event"));
    node *set = counter ? behavior_asset_add_set_node(bp, counter->id) : NULL;
    pin_value one = { .type = PIN_I32, .type_name = "int", .i = 1 };
    if (!counter || !begin || !tick || !input || !set ||
        graph_set_event_name(bp->graph, begin, "Begin") != 0 ||
        graph_set_event_name(bp->graph, tick, "Tick") != 0 ||
        graph_set_event_name(bp->graph, input, "Input") != 0 ||
        graph_set_pin_default(bp->graph, set, 0, 1, &one) != 0 ||
        graph_connect_exec(bp->graph, begin, 0, set, 0, 0) != 0 ||
        graph_connect_exec(bp->graph, tick, 0, set, 0, 1) != 0) {
        behavior_asset_free(bp);
        return NULL;
    }
    return bp;
}

void run_live_preview_checks(void)
{
    ecs_world *ecs = ecs_world_new();
    ecs_entity first = ecs_world_create_entity(ecs);
    ecs_entity second = ecs_world_create_entity(ecs);
    game_live_world *world = game_live_world_new(ecs);
    behavior_asset *source = make_live_behavior_asset();
    game_live_definition *definition = game_live_definition_new("preview", source);
    test_check(
        game_live_add_definition(world, definition) == 0 &&
            game_behavior_attach(world->behaviors, first, "preview") == 0 &&
            game_behavior_attach(world->behaviors, second, "preview") == 0,
        "preview fixture attaches two independent runtime instances"
    );
    behavior_instance *one = game_behavior_instance(world->behaviors, first, "preview");
    behavior_instance *two = game_behavior_instance(world->behaviors, second, "preview");
    uint64_t counter = definition->working->variables[0].id;
    pin_value value = { .type = PIN_I32, .type_name = "int", .i = 37 };
    behavior_instance_set_variable(one, counter, &value);
    node *set = definition->working->graph->nodes[3];
    value.i = 9;
    graph_set_pin_default(definition->working->graph, set, 0, 1, &value);
    game_live_mark_dirty(definition);
    behavior_asset *before = definition->applied;
    flow_run *pending = behavior_instance_enqueue(two, two->bp->graph->nodes[2], (flow_inputs){ 0 }, NULL);
    char message[256];
    test_check(
        pending && game_live_preview(world, definition, message, sizeof message) == INSTANCE_UPDATE_BUSY &&
            one->bp == before && two->bp == before,
        "one queued activation defers preview for every affected instance "
        "atomically"
    );
    game_behavior_pump(world->behaviors, 1.0);
    test_check(
        game_live_preview(world, definition, message, sizeof message) == INSTANCE_UPDATE_READY &&
            one == game_behavior_instance(world->behaviors, first, "preview") &&
            two == game_behavior_instance(world->behaviors, second, "preview") &&
            one->bp == definition->applied && two->bp == definition->applied &&
            behavior_instance_get_variable(one, counter, &value) == 0 && value.i == 37,
        "preview updates both definitions without recreating instances or "
        "resetting variables"
    );
    test_check(
        game_behavior_dispatch(world->behaviors, first, "Tick", (flow_inputs){ 0 }, NULL) == 1 &&
            behavior_instance_get_variable(one, counter, &value) == 0 && value.i == 9 &&
            behavior_instance_get_variable(two, counter, &value) == 0 && value.i == 1,
        "next dispatch uses edited logic while unrelated instance state stays "
        "intact"
    );
    behavior_variable *added = behavior_asset_add_variable(
        definition->working,
        "NewState",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        NULL,
        ""
    );
    game_live_mark_dirty(definition);
    before = definition->applied;
    test_check(
        added && game_live_preview(world, definition, message, sizeof message) == INSTANCE_UPDATE_RESTART &&
            definition->applied == before && one->bp == before,
        "state schema changes require explicit restart and leave the running "
        "version intact"
    );
    behavior_asset_delete_variable(definition->working, added->id);
    behavior_asset_delete_variable(definition->working, counter);
    game_live_mark_dirty(definition);
    test_check(
        game_live_preview(world, definition, message, sizeof message) < 0 &&
            definition->diagnostics.error_count > 0 && definition->applied == before,
        "invalid preview retains the running snapshot and reports the graph "
        "error"
    );
    test_check(
        game_behavior_dispatch(world->behaviors, first, "Tick", (flow_inputs){ 0 }, NULL) == 1 &&
            behavior_instance_get_variable(one, counter, &value) == 0 && value.i == 9,
        "last valid behavior continues executing while the working graph is "
        "broken"
    );
    game_live_sync_working(definition, definition->applied);
    test_check(
        game_live_preview(world, definition, message, sizeof message) == INSTANCE_UPDATE_READY &&
            !definition->dirty && definition->unsaved &&
            !definition->diagnostics.error_count &&
            behavior_instance_get_variable(one, counter, &value) == 0 && value.i == 9,
        "fixing the working graph recovers preview without losing runtime state"
    );
    game_live_world_free(world);
    ecs_world_free(ecs);
}

void run_prototype_live_checks()
{
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    game_prototype_preview *preview = game_prototype_preview_new(&registry);
    behavior_asset *working = make_live_behavior_asset();
    char message[256];
    game_prototype *prototype = game_prototype_load(
        "{\"name\":\"LivePreview\",\"behaviors\":[\"live\"]}",
        &registry,
        message,
        sizeof message
    );
    test_check(
        prototype &&
            game_prototype_preview_update_behavior(preview, "live", working, false, message, sizeof message) ==
                0 &&
            game_prototype_preview_launch(preview, prototype, message, sizeof message) == 0,
        "prototype preview can launch a newly authored behavior"
    );
    behavior_instance *instance = game_prototype_preview_instance(preview, "live");
    uint64_t counter = working->variables[0].id;
    behavior_instance_set_variable(instance, counter, &(pin_value){ .type = PIN_I32, .i = 61 });
    graph_set_pin_default(
        working->graph,
        working->graph->nodes[3],
        0,
        1,
        &(pin_value){ .type = PIN_I32, .i = 7 }
    );
    pin_value value;
    flow_error error = { 0 };
    behavior_instance_enqueue(instance, instance->bp->graph->nodes[2], (flow_inputs){ 0 }, &error);
    test_check(
        game_prototype_preview_update_behavior(preview, "live", working, false, message, sizeof message) ==
            INSTANCE_UPDATE_BUSY,
        "prototype preview defers edits while an execution is queued"
    );
    game_behavior_pump(game_prototype_preview_behaviors(preview), 1.0);
    test_check(
        game_prototype_preview_update_behavior(preview, "live", working, false, message, sizeof message) ==
                INSTANCE_UPDATE_READY &&
            instance == game_prototype_preview_instance(preview, "live") &&
            behavior_instance_get_variable(instance, counter, &value) == 0 &&
            value.i == 61,
        "prototype preview updates without resetting its instance state"
    );
    test_check(
        game_prototype_preview_tick(preview, 2.0) >= 0 &&
            behavior_instance_get_variable(instance, counter, &value) == 0 &&
            value.i == 7,
        "prototype preview executes the edited graph on its next tick"
    );
    behavior_variable *added = behavior_asset_add_variable(
        working,
        "NewState",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        NULL,
        ""
    );
    uint64_t added_id = added->id;
    test_check(
        game_prototype_preview_update_behavior(preview, "live", working, false, message, sizeof message) ==
                INSTANCE_UPDATE_RESTART &&
            behavior_instance_get_variable(instance, counter, &value) == 0 &&
            value.i == 7,
        "prototype schema changes wait for an explicit restart"
    );
    test_check(
        game_prototype_preview_update_behavior(preview, "live", working, true, message, sizeof message) ==
                INSTANCE_UPDATE_READY &&
            behavior_instance_get_variable(
                game_prototype_preview_instance(preview, "live"),
                added_id,
                &value
            ) == 0,
        "explicit prototype restart installs the new state schema"
    );
    instance = game_prototype_preview_instance(preview, "live");
    behavior_asset_delete_variable(working, counter);
    test_check(
        game_prototype_preview_update_behavior(preview, "live", working, false, message, sizeof message) <
                0 &&
            game_prototype_preview_tick(preview, 3.0) >= 0 &&
            behavior_instance_get_variable(instance, counter, &value) == 0 &&
            value.i == 7,
        "invalid prototype edits leave the last valid graph running"
    );
    behavior_asset_free(working);
    game_prototype_free(prototype);
    game_prototype_preview_free(preview);
}

void run_live_checks(void)
{
    ecs_world *ecs = ecs_world_new();
    ecs_world_register_component(ecs, &GAME_COMPONENT_GameHealth, NULL);
    ecs_entity e1 = ecs_world_create_entity(ecs);
    ecs_entity e2 = ecs_world_create_entity(ecs);
    GAME_GameHealth_add(ecs, e1, 1);
    GAME_GameHealth_add(ecs, e2, 2);
    ecs_world_apply_commands(ecs);
    GAME_GameHealth_get(ecs, e1)->current = 7;

    game_live_world *world = game_live_world_new(ecs);
    game_live_definition *definition = game_live_definition_new("live", make_live_behavior_asset());
    game_live_definition *other_definition = game_live_definition_new(
        "other",
        make_behavior_behavior_asset("Other", 0)
    );
    test_check(
        world && definition && other_definition &&
            game_live_add_definition(world, definition) == 0 &&
            game_live_add_definition(world, other_definition) == 0,
        "a live definition owns a mutable working copy and an applied "
        "snapshot"
    );
    game_behavior_world *behaviors = world->behaviors;
    test_check(
        game_behavior_attach(behaviors, e1, "live") == 0 &&
            game_behavior_attach(behaviors, e1, "other") == 0,
        "runtime instances attach only to the applied snapshot"
    );
    behavior_instance *live_before = game_behavior_instance(behaviors, e1, "live");
    test_check(
        behavior_instance_service(live_before, "ecs.world") == ecs,
        "attached instances receive the owning ECS world service"
    );
    const behavior_asset *applied_before = definition->applied;
    behavior_instance *other_before = game_behavior_instance(behaviors, e1, "other");
    uint64_t node_id = definition->working->graph->nodes[0]->id;
    test_check(
        behavior_asset_mutable(definition->working) &&
            behavior_asset_set_name(definition->working, "LiveEdited") == 0 &&
            behavior_asset_set_name((behavior_asset *)definition->applied, "Rejected") != 0,
        "working definitions are mutable while attached applied "
        "snapshots are immutable"
    );
    test_check(
        game_live_mark_dirty(definition) == 0 && definition->dirty &&
            strcmp(definition->applied->name, "LiveWorking") == 0,
        "Compile/apply is explicit and unapplied edits are visible"
    );
    behavior_asset *working = definition->working;
    behavior_asset_set_description(working, "edited working copy");
    game_live_mark_dirty(definition);
    char message[256];
    test_check(
        game_live_apply(world, definition, message, sizeof message) == 0 &&
            !definition->dirty && strstr(message, "destroyed and recreated"),
        "one replacement choke point states that only this definition "
        "is recreated"
    );
    behavior_instance *live_after = game_behavior_instance(behaviors, e1, "live");
    behavior_instance *other_after = game_behavior_instance(behaviors, e1, "other");
    test_check(
        live_after && live_after->bp == definition->applied &&
            live_after->bp != applied_before,
        "applying replaces the changed definition's instances as one "
        "operation"
    );
    test_check(
        other_after == other_before && GAME_GameHealth_get(ecs, e1)->current == 7,
        "applying is scoped and leaves other behaviors and components intact"
    );
    test_check(definition->applied->graph->nodes[0]->id == node_id, "snapshot replacement preserves node identity");
    behavior_variable *counter = behavior_asset_find_variable_by_name(definition->applied, "Counter");
    pin_value begin_value = { 0 };
    test_check(
        counter &&
            behavior_instance_get_variable(live_after, counter->id, &begin_value) == 0 &&
            begin_value.i == 1,
        "recreated instances run their Begin event"
    );
    test_check(begin_value.i == 1, "apply keeps observable behavior defined when transient state is recreated");
    test_check(
        game_behavior_instance(behaviors, e2, "other") == NULL &&
            game_behavior_attachment_count(behaviors) == 2,
        "replacing one definition does not create instances for "
        "unrelated entities"
    );
    test_check(
        game_live_apply(world, definition, message, sizeof message) == 0 &&
            strstr(message, "no unapplied edits"),
        "a clean applied snapshot is stable until another explicit "
        "apply"
    );
    behavior_variable *invalid_counter = behavior_asset_find_variable_by_name(working, "Counter");
    test_check(
        invalid_counter &&
            behavior_asset_delete_variable(working, invalid_counter->id) == 0 &&
            game_live_mark_dirty(definition) == 0 &&
            game_live_apply(world, definition, message, sizeof message) != 0 &&
            definition->diagnostics.error_count > 0,
        "blocked apply retains structured graph diagnostics"
    );
    test_check(
        game_live_mark_dirty(definition) == 0 &&
            behavior_asset_mutable(definition->working),
        "authoring remains available on the working copy after apply"
    );
    test_check(
        definition->dirty && definition->applied->graph->nodes[0]->id == node_id,
        "runtime state stays out of working-copy history and applied "
        "identity"
    );
    game_live_world_free(world);
    ecs_world_free(ecs);
}
