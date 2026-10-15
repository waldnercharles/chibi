#include "authoring/engine_preview.h"
#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/events.h"
#include "test/suite_internal.h"

behavior_asset *make_live_behavior_asset(void)
{
    behavior_asset *asset = behavior_asset_new("LiveWorking");
    if (!asset || behavior_asset_set_owner_type(asset, "ecs.entity") != 0) {
        behavior_asset_free(asset);
        return NULL;
    }
    behavior_variable *counter = behavior_asset_add_variable(
        asset,
        "Counter",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_I32, .i = 0 },
        "Live"
    );
    node *begin = graph_add_node(asset->graph, registry_node_by_name("Event"));
    node *tick = graph_add_node(asset->graph, registry_node_by_name("Event"));
    node *input = graph_add_node(asset->graph, registry_node_by_name("Event"));
    node *set = counter ? behavior_asset_add_set_node(asset, counter->id) : NULL;
    pin_value one = { .type = PIN_I32, .type_name = "int", .i = 1 };
    if (!counter || !begin || !tick || !input || !set ||
        graph_set_event_name(asset->graph, begin, "Begin") != 0 ||
        graph_set_event_name(asset->graph, tick, "Tick") != 0 ||
        graph_set_event_name(asset->graph, input, "Input") != 0 ||
        graph_host_event_apply(asset->graph, begin) != 0 ||
        graph_host_event_apply(asset->graph, tick) != 0 ||
        graph_host_event_apply(asset->graph, input) != 0 ||
        graph_set_pin_default(asset->graph, set, 0, 1, &one) != 0 ||
        graph_connect_exec(asset->graph, begin, 0, set, 0, 0) != 0 ||
        graph_connect_exec(asset->graph, tick, 0, set, 0, 0) != 0) {
        behavior_asset_free(asset);
        return NULL;
    }
    return asset;
}

static behavior_asset *authoring_snapshot(const behavior_asset *asset)
{
    av_doc *doc = av_doc_new();
    behavior_asset *copy = behavior_asset_from_value(behavior_asset_to_value(doc, asset));
    av_doc_free(doc);
    assert(copy);
    return copy;
}

/* Automatic arrangement changes layout alone; moving a placed node is an edit.
   Use authoring snapshots because executable source does not preserve node IDs. */
static void automatic_layout_checks()
{
    behavior_asset *source = make_live_behavior_asset();
    uint64_t automatic = source->graph->nodes[0]->id,
             placed = source->graph->nodes[1]->id;
    source->graph->nodes[0]->placed = false;
    preview_definition *definition = preview_definition_new("layout", source);
    behavior_asset *open = authoring_snapshot(definition->working);
    graph_node_by_id(open->graph, automatic)->x += 240;
    test_check(
        preview_sync_working(definition, open) == 0 && !definition->dirty &&
            !definition->unsaved &&
            graph_node_by_id(definition->working->graph, automatic)->x ==
                graph_node_by_id(open->graph, automatic)->x,
        "arranging automatic nodes leaves a definition saved and applied"
    );
    graph_node_by_id(open->graph, placed)->x += 40;
    test_check(
        preview_sync_working(definition, open) == 0 && definition->dirty &&
            definition->unsaved,
        "moving a placed node is an unsaved edit"
    );
    behavior_asset_free(open);
    preview_definition_free(definition);
}

/* Component access nodes write one-line file entries. Wherever arrangement
   puts them, the entries read the same, so the definition stays saved. */
static void arranged_entries_checks()
{
    static const char *const kinds[] = {
        "Get_Collider",
        "GetEntity_Collider",
        "Has_Collider",
        "HasEntity_Collider",
        "Set_Collider",
        "SetEntity_Collider",
        "Add_Collider",
        "Remove_Collider",
        "Make_Collider",
        "Break_Collider",
        "Set_Collider_ignore_entity",
    };
    static const float spots[][2] = { { 12, 0 }, { 1576, 261 }, { -1234567, -7654321 } };
    behavior_asset *source = behavior_asset_new("Arranged");
    for (size_t i = 0; i < sizeof kinds / sizeof *kinds; i++) {
        node *n = graph_add_node(source->graph, registry_node_by_name(kinds[i]));
        assert(n);
        n->placed = false;
    }
    preview_definition *definition = preview_definition_new("arranged", source);
    bool saved = true;
    for (size_t s = 0; s < sizeof spots / sizeof *spots; s++) {
        behavior_asset *open = authoring_snapshot(definition->working);
        for (int i = 0; i < acount(open->graph->nodes); i++) {
            open->graph->nodes[i]->x = spots[s][0];
            open->graph->nodes[i]->y = spots[s][1];
        }
        saved &= preview_sync_working(definition, open) == 0 && !definition->unsaved;
        behavior_asset_free(open);
    }
    test_check(saved, "arranged component access nodes keep their file entries");
    preview_definition_free(definition);
}

void run_live_preview_checks(void)
{
    automatic_layout_checks();
    arranged_entries_checks();
    ecs_world *ecs = ecs_world_new();
    ecs_entity first = ecs_world_create_entity(ecs);
    ecs_entity second = ecs_world_create_entity(ecs);
    preview_world *world = preview_world_new(ecs);
    behavior_asset *source = make_live_behavior_asset();
    preview_definition *definition = preview_definition_new("preview", source);
    test_check(
        preview_add_definition(world, definition) == 0 &&
            behavior_attach(world->runtime->behaviors, first, "preview") == 0 &&
            behavior_attach(world->runtime->behaviors, second, "preview") == 0,
        "preview fixture attaches two independent runtime instances"
    );
    behavior_instance *one = behavior_instance(world->runtime->behaviors, first, "preview");
    behavior_instance *two = behavior_instance(world->runtime->behaviors, second, "preview");
    uint64_t counter = definition->working->variables[0].id;
    pin_value value = { .type = PIN_I32, .type_name = "int", .i = 37 };
    behavior_instance_set_variable(one, counter, &value);
    node *set = definition->working->graph->nodes[3];
    value.i = 9;
    graph_set_pin_default(definition->working->graph, set, 0, 1, &value);
    preview_mark_dirty(definition);
    behavior_asset *before = definition->applied;
    flow_run *pending = behavior_instance_enqueue(
        two,
        two->asset->graph->nodes[2],
        (flow_inputs){ 0 },
        NULL
    );
    char message[256];
    test_check(
        pending && preview_update(world, definition, message, sizeof message) == INSTANCE_UPDATE_BUSY &&
            one->asset == before && two->asset == before,
        "one queued activation defers preview for every affected instance "
        "atomically"
    );
    behavior_pump(world->runtime->behaviors, 1.0);
    test_check(
        preview_update(world, definition, message, sizeof message) == INSTANCE_UPDATE_READY &&
            one == behavior_instance(world->runtime->behaviors, first, "preview") &&
            two == behavior_instance(world->runtime->behaviors, second, "preview") &&
            one->asset == definition->applied && two->asset == definition->applied &&
            behavior_instance_get_variable(one, counter, &value) == 0 && value.i == 37,
        "preview updates both definitions without recreating instances or "
        "resetting variables"
    );
    test_check(
        behavior_dispatch(world->runtime->behaviors, first, "Tick", (flow_inputs){ 0 }, NULL) ==
                1 &&
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
    preview_mark_dirty(definition);
    before = definition->applied;
    test_check(
        added && preview_update(world, definition, message, sizeof message) == INSTANCE_UPDATE_RESTART &&
            definition->applied == before && one->asset == before,
        "state schema changes require explicit restart and leave the running "
        "version intact"
    );
    behavior_asset_delete_variable(definition->working, added->id);
    behavior_asset_delete_variable(definition->working, counter);
    preview_mark_dirty(definition);
    test_check(
        preview_update(world, definition, message, sizeof message) < 0 &&
            definition->diagnostics.error_count > 0 && definition->applied == before,
        "invalid preview retains the running snapshot and reports the graph "
        "error"
    );
    test_check(
        behavior_dispatch(world->runtime->behaviors, first, "Tick", (flow_inputs){ 0 }, NULL) ==
                1 &&
            behavior_instance_get_variable(one, counter, &value) == 0 && value.i == 9,
        "last valid behavior continues executing while the working graph is "
        "broken"
    );
    preview_sync_working(definition, definition->applied);
    test_check(
        preview_update(world, definition, message, sizeof message) == INSTANCE_UPDATE_READY &&
            !definition->dirty && definition->unsaved &&
            !definition->diagnostics.error_count &&
            behavior_instance_get_variable(one, counter, &value) == 0 && value.i == 9,
        "fixing the working graph recovers preview without losing runtime state"
    );
    preview_world_free(world);
    ecs_world_free(ecs);
}

void run_prototype_live_checks()
{
    prototype_registry registry = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT };
    prototype_preview *preview = prototype_preview_new(&registry);
    behavior_asset *working = make_live_behavior_asset();
    char message[256];
    prototype *prototype = prototype_load(
        "name \"LivePreview\"\n"
        "(array)behaviors \"live\"\n",
        &registry,
        message,
        sizeof message
    );
    test_check(
        prototype &&
            prototype_preview_update_behavior(preview, "live", working, false, message, sizeof message) ==
                0 &&
            prototype_preview_launch(preview, prototype, message, sizeof message) == 0,
        "prototype preview can launch a newly authored behavior"
    );
    behavior_instance *instance = prototype_preview_instance(preview, "live");
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
    behavior_instance_enqueue(instance, instance->asset->graph->nodes[2], (flow_inputs){ 0 }, &error);
    test_check(
        prototype_preview_update_behavior(preview, "live", working, false, message, sizeof message) ==
            INSTANCE_UPDATE_BUSY,
        "prototype preview defers edits while an execution is queued"
    );
    behavior_pump(prototype_preview_behaviors(preview), 1.0);
    test_check(
        prototype_preview_update_behavior(preview, "live", working, false, message, sizeof message) ==
                INSTANCE_UPDATE_READY &&
            instance == prototype_preview_instance(preview, "live") &&
            behavior_instance_get_variable(instance, counter, &value) == 0 &&
            value.i == 61,
        "prototype preview updates without resetting its instance state"
    );
    test_check(
        prototype_preview_tick(preview, 2.0) >= 0 &&
            prototype_preview_tick(preview, 2.0 + ENGINE_FIXED_SECONDS) >= 0 &&
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
        prototype_preview_update_behavior(preview, "live", working, false, message, sizeof message) ==
                INSTANCE_UPDATE_RESTART &&
            behavior_instance_get_variable(instance, counter, &value) == 0 &&
            value.i == 7,
        "prototype schema changes wait for an explicit restart"
    );
    test_check(
        prototype_preview_update_behavior(preview, "live", working, true, message, sizeof message) ==
                INSTANCE_UPDATE_READY &&
            behavior_instance_get_variable(prototype_preview_instance(preview, "live"), added_id, &value) ==
                0,
        "explicit prototype restart installs the new state schema"
    );
    instance = prototype_preview_instance(preview, "live");
    behavior_asset_delete_variable(working, counter);
    test_check(
        prototype_preview_update_behavior(preview, "live", working, false, message, sizeof message) <
                0 &&
            prototype_preview_tick(preview, 2.0 + 2 * ENGINE_FIXED_SECONDS) >= 0 &&
            behavior_instance_get_variable(instance, counter, &value) == 0 &&
            value.i == 7,
        "invalid prototype edits leave the last valid graph running"
    );
    behavior_asset_free(working);
    prototype_free(prototype);
    prototype_preview_free(preview);
}

static void run_optional_module_checks()
{
    ecs_world *ecs = ecs_world_new();
    live_world *world = live_world_new_modules(ecs, 0);
    test_check(
        world->behaviors && world->entities && !world->physics &&
            !world->particles && !world->materials && !world->visibility &&
            !behavior_audio(world->behaviors) && !behavior_ui(world->behaviors),
        "a headless runtime creates behavior and entity services without "
        "optional modules"
    );
    engine *runtime = authoring_engine_new(ecs, world);
    engine_set_playing(runtime, false);
    test_check(
        engine_step(runtime) == 0 && engine_frame(runtime, 0) == 0 &&
            engine_last_timing(runtime)->simulation_steps == 1,
        "a headless runtime advances a fixed step without input, physics or "
        "rendering"
    );
    test_check(engine_sample_movement(runtime, 1, 0) == FLOW_ERR_STATE, "an uninstalled input service rejects host input");
    engine_reset(runtime);
    engine_free(runtime);
    live_world_free(world);
    ecs_world_free(ecs);
}

void run_live_checks(void)
{
    run_optional_module_checks();
    ecs_world *ecs = ecs_world_new();
    ecs_world_register_component(ecs, &COMPONENT_Health, NULL);
    ecs_entity e1 = ecs_world_create_entity(ecs);
    ecs_entity e2 = ecs_world_create_entity(ecs);
    Health_add(ecs, e1, 1);
    Health_add(ecs, e2, 2);
    ecs_world_apply_commands(ecs);
    Health_get(ecs, e1)->current = 7;

    preview_world *world = preview_world_new(ecs);
    preview_definition *definition = preview_definition_new("live", make_live_behavior_asset());
    preview_definition *other_definition = preview_definition_new(
        "other",
        make_behavior_behavior_asset("Other", 0)
    );
    test_check(
        world && definition && other_definition &&
            preview_add_definition(world, definition) == 0 &&
            preview_add_definition(world, other_definition) == 0,
        "a live definition owns a mutable working copy and an applied "
        "snapshot"
    );
    behavior_world *behaviors = world->runtime->behaviors;
    test_check(
        behavior_attach(behaviors, e1, "live") == 0 &&
            behavior_attach(behaviors, e1, "other") == 0,
        "runtime instances attach only to the applied snapshot"
    );
    behavior_instance *live_before = behavior_instance(behaviors, e1, "live");
    test_check(
        behavior_instance_service(live_before, "ecs.world") == ecs,
        "attached instances receive the owning ECS world service"
    );
    const behavior_asset *applied_before = definition->applied;
    behavior_instance *other_before = behavior_instance(behaviors, e1, "other");
    uint64_t node_id = definition->working->graph->nodes[0]->id;
    test_check(
        behavior_asset_mutable(definition->working) &&
            behavior_asset_set_name(definition->working, "LiveEdited") == 0 &&
            behavior_asset_set_name((behavior_asset *)definition->applied, "Rejected") != 0,
        "working definitions are mutable while attached applied "
        "snapshots are immutable"
    );
    test_check(
        preview_mark_dirty(definition) == 0 && definition->dirty &&
            strcmp(definition->applied->name, "LiveWorking") == 0,
        "Compile/apply is explicit and unapplied edits are visible"
    );
    behavior_asset *working = definition->working;
    behavior_asset_set_description(working, "edited working copy");
    preview_mark_dirty(definition);
    char message[256];
    test_check(
        preview_apply(world, definition, message, sizeof message) == 0 &&
            !definition->dirty && strstr(message, "destroyed and recreated"),
        "one replacement choke point states that only this definition "
        "is recreated"
    );
    behavior_instance *live_after = behavior_instance(behaviors, e1, "live");
    behavior_instance *other_after = behavior_instance(behaviors, e1, "other");
    test_check(
        live_after && live_after->asset == definition->applied &&
            live_after->asset != applied_before,
        "applying replaces the changed definition's instances as one "
        "operation"
    );
    test_check(
        other_after == other_before && Health_get(ecs, e1)->current == 7,
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
        behavior_instance(behaviors, e2, "other") == NULL &&
            behavior_attachment_count(behaviors) == 2,
        "replacing one definition does not create instances for "
        "unrelated entities"
    );
    test_check(
        preview_apply(world, definition, message, sizeof message) == 0 &&
            strstr(message, "no unapplied edits"),
        "a clean applied snapshot is stable until another explicit "
        "apply"
    );
    behavior_variable *invalid_counter = behavior_asset_find_variable_by_name(working, "Counter");
    test_check(
        invalid_counter &&
            behavior_asset_delete_variable(working, invalid_counter->id) == 0 &&
            preview_mark_dirty(definition) == 0 &&
            preview_apply(world, definition, message, sizeof message) != 0 &&
            definition->diagnostics.error_count > 0,
        "blocked apply retains structured graph diagnostics"
    );
    test_check(
        preview_mark_dirty(definition) == 0 &&
            behavior_asset_mutable(definition->working),
        "authoring remains available on the working copy after apply"
    );
    test_check(
        definition->dirty && definition->applied->graph->nodes[0]->id == node_id,
        "runtime state stays out of working-copy history and applied "
        "identity"
    );
    preview_world_free(world);
    ecs_world_free(ecs);
}
