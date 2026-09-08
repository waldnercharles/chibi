#include "test/suite_internal.h"

static const char
    *g_prototype_base_json = "{\"name\":\"BaseEnemy\",\"components\":{"
                             "\"GameHealth\":{\"current\":50,\"maximum\":50},"
                             "\"GameTransform\":{\"x\":1,\"y\":2}},"
                             "\"behaviors\":[\"idle.graph\"]}";
static int g_prototype_attach_count;
static const char
    *g_cycle_a = "{\"name\":\"CycleA\",\"includes\":[\"CycleB\"]}";
static const char
    *g_cycle_b = "{\"name\":\"CycleB\",\"includes\":[\"CycleA\"]}";

static const char *prototype_include(const char *name, void *user)
{
    (void)user;
    return strcmp(name, "BaseEnemy") == 0 ? g_prototype_base_json : NULL;
}

static const char *prototype_cycle_include(const char *name, void *user)
{
    (void)user;
    if (strcmp(name, "CycleA") == 0) {
        return g_cycle_a;
    }
    if (strcmp(name, "CycleB") == 0) {
        return g_cycle_b;
    }
    return NULL;
}

static int prototype_attach(ecs_world *world, ecs_entity entity, const char *graph_name, void *user)
{
    (void)world;
    (void)entity;
    (void)user;
    g_prototype_attach_count += graph_name && graph_name[0] != '\0';
    return 0;
}

void run_prototype_checks(void)
{
    static const char
        *derived_json = "{\"name\":\"Enemy\",\"identity\":\"enemy-id\","
                        "\"includes\":[\"BaseEnemy\"],"
                        "\"components\":{\"GameHealth\":{\"current\":100}},"
                        "\"behaviors\":[\"attack.graph\"]}";
    static const char
        *reordered_json = "{\"behaviors\":[\"attack.graph\",\"idle.graph\"],"
                          "\"identity\":\"enemy-id\","
                          "\"components\":{\"GameTransform\":{\"y\":2,\"x\":1},"
                          "\"GameHealth\":{\"maximum\":50,\"current\":100}},"
                          "\"name\":\"Enemy\"}";
    const game_prototype_registry registry = {
        GAME_REGISTRY_COMPONENTS,
        GAME_REGISTRY_COMPONENT_COUNT,
        prototype_include,
        NULL,
    };
    char error[256];
    game_prototype *prototype = game_prototype_load(derived_json, &registry, error, sizeof error);
    test_check(prototype && strcmp(prototype->name, "Enemy") == 0, "a prototype names components and attached graph assets");
    test_check(prototype && acount(prototype->components) == 2, "a prototype is data and does not allocate an entity slot");
    const ecs_component_desc *health = NULL;
    const GameHealth *health_value = NULL;
    for (int i = 0; prototype && i < acount(prototype->components); i++) {
        const ecs_component_desc *desc = prototype->components[i].desc;
        if (strcmp(desc->name, "GameHealth") == 0) {
            health = desc;
            health_value = prototype->components[i].data;
        }
    }
    test_check(
        health && health_value && health_value->current == 100 &&
            health_value->maximum == 50,
        "include values load and local fields override by name"
    );
    char *saved = prototype ? game_prototype_save(prototype) : NULL;
    game_prototype *reordered = game_prototype_load(reordered_json, &registry, error, sizeof error);
    char *saved_reordered = reordered ? game_prototype_save(reordered) : NULL;
    test_check(
        saved && saved_reordered && strcmp(saved, saved_reordered) == 0,
        "generic descriptor-driven load/save is deterministic"
    );
    test_check(
        saved && strstr(saved, "GameTransform") && strstr(saved, "GameHealth"),
        "prototype output contains generic component values"
    );
    free(saved);
    free(saved_reordered);
    game_prototype_free(reordered);

    game_prototype_registry missing = { GAME_REGISTRY_COMPONENTS, 1, prototype_include, NULL };
    game_prototype *missing_component = game_prototype_load(derived_json, &missing, error, sizeof error);
    test_check(!missing_component && strstr(error, "GameHealth"), "an unavailable component fails by name without partial data");
    const game_prototype_registry cycles = {
        GAME_REGISTRY_COMPONENTS,
        GAME_REGISTRY_COMPONENT_COUNT,
        prototype_cycle_include,
        NULL,
    };
    game_prototype *cycle = game_prototype_load(g_cycle_a, &cycles, error, sizeof error);
    test_check(!cycle && strstr(error, "cycle"), "include cycles are rejected");

    ecs_world *world = ecs_world_new();
    ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL);
    ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL);
    g_prototype_attach_count = 0;
    const game_prototype_spawn_hooks hooks = { prototype_attach, NULL };
    ecs_entity no_hook_entity = { 0 };
    test_check(
        prototype &&
            game_prototype_spawn(prototype, world, NULL, &no_hook_entity, error, sizeof error) ==
                ECS_ERR_ARGUMENT &&
            ecs_world_live_count(world) == 0 && strstr(error, "attach hook"),
        "behavior-bearing prototypes cannot silently skip attachment"
    );
    ecs_entity entity = { 0 };
    test_check(
        prototype &&
            game_prototype_spawn(prototype, world, &hooks, &entity, error, sizeof error) ==
                ECS_OK &&
            g_prototype_attach_count == 2 && ecs_entity_is_alive(world, entity),
        "spawning writes generic component fields and attaches each "
        "graph asset"
    );
    pin_value health_field = { 0 };
    test_check(
        prototype &&
            game_prototype_read_field(prototype, "GameHealth", "current", &health_field) == 0 &&
            health_field.i == 100 &&
            game_prototype_write_field(
                prototype,
                "GameHealth",
                "current",
                (pin_value){
                    .type = PIN_I32,
                    .i = 101,
                }
            ) == 0 &&
            game_prototype_read_field(prototype, "GameHealth", "current", &health_field) == 0 &&
            health_field.i == 101,
        "prototype component fields can be inspected and edited"
    );
    game_prototype_write_field(
        prototype,
        "GameHealth",
        "current",
        (pin_value){ .type = PIN_I32, .i = 100 }
    );
    test_check(
        prototype && game_prototype_add_graph(prototype, "debug.graph") == 0 &&
            game_prototype_has_graph(prototype, "debug.graph") &&
            game_prototype_remove_graph(prototype, "debug.graph") == 0 &&
            !game_prototype_has_graph(prototype, "debug.graph"),
        "prototype behavior attachments are editable"
    );
    test_check(
        prototype && game_prototype_remove_component(prototype, "GameTransform") == 0 &&
            acount(prototype->components) == 1 &&
            game_prototype_add_component(prototype, &GAME_COMPONENT_GameTransform) == 0 &&
            acount(prototype->components) == 2,
        "prototype component structure is editable"
    );
    game_prototype *base = game_prototype_clone(prototype, error, sizeof error);
    GAME_GameHealth_get(world, entity)->current = 7;
    char *overrides = game_prototype_capture_overrides_json(base, world, entity);
    test_check(
        base && overrides && strstr(overrides, "\"current\": 7") &&
            !strstr(overrides, "\"maximum\""),
        "instance capture stores sparse field overrides"
    );
    ecs_entity override_entity = { 0 };
    test_check(
        base &&
            game_prototype_spawn(base, world, &hooks, &override_entity, error, sizeof error) ==
                ECS_OK &&
            game_prototype_apply_components_json(
                world,
                override_entity,
                "{\"GameHealth\":{\"current\":7}}",
                &registry,
                error,
                sizeof error
            ) == 0 &&
            GAME_GameHealth_get(world, override_entity)->current == 7 &&
            GAME_GameHealth_get(world, override_entity)->maximum == 50,
        "sparse field overrides apply without zeroing sibling fields"
    );
    GAME_GameHealth_get(world, entity)->current = 100;
    free(overrides);
    game_prototype_free(base);
    game_prototype *captured = game_prototype_capture_entity("CapturedEnemy", world, entity, &registry);
    char *captured_components = game_prototype_components_json(captured);
    test_check(
        captured && captured_components && strstr(captured_components, "GameHealth"),
        "a live entity can be captured through the generic prototype registry"
    );
    free(captured_components);
    game_prototype_free(captured);
    game_prototype_free(prototype);
    GameHealth *spawned_health = GAME_GameHealth_get(world, entity);
    test_check(
        spawned_health && spawned_health->current == 100 && spawned_health->maximum == 50,
        "editing/freeing a prototype does not change spawned entity values"
    );
    ecs_world_free(world);
    test_check(health != NULL, "descriptor lookup is independent of component C types");
}

behavior_asset *make_behavior_behavior_asset(const char *name, int latent)
{
    behavior_asset *bp = behavior_asset_new(name);
    if (!bp) {
        return NULL;
    }
    graph *graph = bp->graph;
    node *event = graph_add_node(graph, registry_node_by_name("Event"));
    if (!event || graph_set_event_name(graph, event, "Tick") != 0) {
        behavior_asset_free(bp);
        return NULL;
    }
    node *begin = NULL;
    if (!latent) {
        begin = graph_add_node(graph, registry_node_by_name("Event"));
        if (!begin || graph_set_event_name(graph, begin, "Begin") != 0) {
            behavior_asset_free(bp);
            return NULL;
        }
    }
    if (latent) {
        node *delay = graph_add_node(graph, registry_node_by_name("Delay"));
        pin_value duration = { .type = PIN_F64, .type_name = "double", .d = 1.0 };
        if (!delay || graph_set_pin_default(graph, delay, 0, 1, &duration) != 0 ||
            graph_connect_exec(graph, event, 0, delay, 0, 0) != 0) {
            behavior_asset_free(bp);
            return NULL;
        }
        return bp;
    }
    behavior_variable *counter = behavior_asset_add_variable(
        bp,
        "Counter",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_I32, .i = 0 },
        "Behavior"
    );
    node *set = counter ? behavior_asset_add_set_node(bp, counter->id) : NULL;
    pin_value one = { .type = PIN_I32, .type_name = "int", .i = 1 };
    if (!counter || !set || graph_set_pin_default(graph, set, 0, 1, &one) != 0 ||
        graph_connect_exec(graph, event, 0, set, 0, 0) != 0 ||
        graph_connect_exec(graph, begin, 0, set, 0, 0) != 0) {
        behavior_asset_free(bp);
        return NULL;
    }
    return bp;
}

static int g_promote_probe;
static int behavior_promote_probe(
    behavior_asset *definition,
    uint64_t variable_id,
    ecs_component_id component_id,
    void *user
)
{
    (void)definition;
    (void)user;
    g_promote_probe = variable_id != 0 && component_id != 0;
    return 0;
}

void run_scene_document_checks(void)
{
    const char *behaviors[] = { "SceneBehavior", "Damage" };
    game_scene_document *document = game_scene_document_new("CombatScene");
    test_check(
        document &&
            game_scene_document_add_entity(document, "Player", "PlayerPrototype", behaviors, 2) ==
                0,
        "a scene entity stores its prototype and behavior assets"
    );
    test_check(
        game_scene_document_set_entity_components(document, 0, "{\"GameHealth\":{\"current\":7,\"maximum\":9}}") ==
                0 &&
            strstr(document->entities[0].components_json, "GameHealth"),
        "a scene entity stores generic component overrides"
    );
    char *saved = game_scene_document_save(document);
    char error[256];
    game_scene_document *loaded = game_scene_document_load(saved, error, sizeof error);
    test_check(
        loaded && acount(loaded->entities) == 1 &&
            strcmp(loaded->entities[0].name, "Player") == 0 &&
            strcmp(loaded->entities[0].prototype, "PlayerPrototype") == 0 &&
            strstr(loaded->entities[0].components_json, "GameHealth") &&
            acount(loaded->entities[0].behaviors) == 2 &&
            strcmp(loaded->entities[0].behaviors[1], "Damage") == 0,
        "scene documents round-trip behavior attachment names and "
        "component overrides"
    );
    char *resaved = game_scene_document_save(loaded);
    test_check(saved && resaved && strcmp(saved, resaved) == 0, "scene document save output is deterministic");
    game_scene_document *duplicate = game_scene_document_new("Duplicate");
    test_check(
        duplicate && game_scene_document_add_entity(duplicate, "A", "P", NULL, 0) == 0 &&
            game_scene_document_add_entity(duplicate, "A", "P", NULL, 0) != 0,
        "duplicate scene entity names are rejected"
    );
    game_scene_document *invalid = game_scene_document_load(
        "{\"version\":2,\"name\":\"Bad\",\"entities\":[]}",
        error,
        sizeof error
    );
    test_check(!invalid && strstr(error, "version 1"), "unsupported scene versions are rejected");
    free(saved);
    free(resaved);
    game_scene_document_free(invalid);
    game_scene_document_free(duplicate);
    game_scene_document_free(loaded);
    game_scene_document_free(document);
}

static behavior_asset *make_log_behavior(void)
{
    behavior_asset *bp = behavior_asset_new("LogBehavior");
    node *event = bp ? graph_add_node(bp->graph, registry_node_by_name("Event")) : NULL;
    node *print = bp ? graph_add_node(bp->graph, registry_node_by_name("Print String"))
                     : NULL;
    node *text = bp ? make_string(bp->graph, "entity log") : NULL;
    if (!bp || !event || !print || !text ||
        behavior_asset_set_owner_type(bp, "ecs.entity") != 0 ||
        graph_set_event_name(bp->graph, event, "Log") != 0 ||
        graph_connect_exec(bp->graph, event, 0, print, 0, 0) != 0 ||
        graph_connect_data(bp->graph, text, 0, print, 1) != 0) {
        behavior_asset_free(bp);
        return NULL;
    }
    return bp;
}

void run_behavior_checks(void)
{
    ecs_world *ecs = ecs_world_new();
    ecs_world_register_component(ecs, &GAME_COMPONENT_GameHealth, NULL);
    ecs_entity e1 = ecs_world_create_entity(ecs);
    ecs_entity e2 = ecs_world_create_entity(ecs);
    GAME_GameHealth_add(ecs, e1, 1);
    GAME_GameHealth_add(ecs, e2, 2);
    ecs_world_apply_commands(ecs);

    behavior_asset *behavior = make_behavior_behavior_asset("Behavior", 0);
    behavior_asset *latent = make_behavior_behavior_asset("Windup", 1);
    behavior_asset *logging = make_log_behavior();
    game_behavior_world *world = game_behavior_world_new(ecs);
    test_check(
        behavior && latent && logging && world &&
            game_behavior_register_asset(world, "move", behavior) == 0 &&
            game_behavior_register_asset(world, "move_again", behavior) == 0 &&
            game_behavior_register_asset(world, "windup", latent) == 0 &&
            game_behavior_register_asset(world, "logging", logging) == 0,
        "behavior assets can be composed as several instances per entity"
    );
    test_check(
        game_behavior_attach(world, e1, "move") == 0 &&
            game_behavior_attach(world, e1, "move_again") == 0 &&
            game_behavior_attach(world, e2, "move") == 0,
        "each attached graph asset creates one per-entity instance"
    );
    test_check(
        game_behavior_attach(world, e2, "move_again") == 0 &&
            game_behavior_set_debug_hold(world, e2, "move_again", 1) == 0 &&
            game_behavior_pump(world, 0.0) == FLOW_OK,
        "an inspection hold can retain a completed Begin activation"
    );
    behavior_instance *e2_begin_hold = game_behavior_instance(world, e2, "move_again");
    test_check(
        e2_begin_hold && acount(e2_begin_hold->activations) == 1 &&
            flow_run_status(e2_begin_hold->activations[0]) == FLOW_COMPLETED,
        "retained Begin activation is available for inspection"
    );
    game_behavior_set_debug_hold(world, e2, "move_again", 0);
    game_behavior_pump(world, 0.0);
    game_behavior_detach_asset(world, e2, "move_again");
    behavior_instance *e1_move = game_behavior_instance(world, e1, "move");
    behavior_instance *e1_again = game_behavior_instance(world, e1, "move_again");
    game_behavior_attachment_info e1_first;
    game_behavior_attachment_info e1_second;
    test_check(
        game_behavior_attachment_count_for_entity(world, e1) == 2 &&
            game_behavior_attachment_count_for_entity(world, e2) == 1 &&
            game_behavior_attachment_info_at(world, e1, 0, &e1_first) == 0 &&
            game_behavior_attachment_info_at(world, e1, 1, &e1_second) == 0 &&
            strcmp(e1_first.asset_name, "move") == 0 &&
            strcmp(e1_second.asset_name, "move_again") == 0 &&
            e1_first.definition == behavior && e1_second.definition == behavior &&
            e1_first.instance == e1_move && e1_second.instance == e1_again &&
            e1_first.sequence < e1_second.sequence &&
            game_behavior_attachment_info_at(world, e1, 2, &e1_first) != 0,
        "entity attachment inspection lists assets and instances in "
        "attach order"
    );
    test_check(
        game_behavior_detach_asset(world, e2, "move") == 1 &&
            game_behavior_attachment_count_for_entity(world, e2) == 0 &&
            game_behavior_attach(world, e2, "move") == 0,
        "one named behavior asset can be detached without removing "
        "others"
    );
    test_check(
        e1_move && e1_again && e1_move != e1_again &&
            behavior_instance_owner(e1_move).u64 == ecs_entity_to_u64(e1) &&
            behavior_instance_owner(e1_move).type == PIN_HANDLE,
        "attached instances are independent and carry the owning entity "
        "handle"
    );
    behavior_variable *counter = behavior_asset_find_variable_by_name(behavior, "Counter");
    pin_value begin_value;
    test_check(
        counter &&
            behavior_instance_get_variable(e1_move, counter->id, &begin_value) == 0 &&
            begin_value.i == 1,
        "attaching an asset runs its Begin event once"
    );
    test_check(
        game_behavior_dispatch(world, e1, "Tick", (flow_inputs){ 0 }, NULL) == 2,
        "named dispatch reaches every matching attachment in attach order"
    );
    pin_value first;
    pin_value second;
    behavior_instance_get_variable(e1_move, counter->id, &first);
    behavior_instance_get_variable(e1_again, counter->id, &second);
    test_check(first.i == 1 && second.i == 1, "each instance owns private variable state");
    behavior_instance_set_variable(e1_move, counter->id, &(pin_value){ .type = PIN_I32, .i = 9 });
    behavior_instance_get_variable(e1_again, counter->id, &second);
    test_check(
        second.i == 1 && GAME_GameHealth_get(ecs, e1)->current == 0,
        "shared entity state is a component, not another instance variable"
    );
    test_check(
        game_behavior_dispatch(world, e1, "Missing", (flow_inputs){ 0 }, NULL) == 0,
        "an attachment without a pumped event costs nothing"
    );

    behavior_instance *pooled = e1_move;
    game_behavior_detach_entity(world, e1);
    test_check(
        game_behavior_attachment_count_for_entity(world, e1) == 0 &&
            game_behavior_attachment_info_at(world, e1, 0, &e1_first) != 0,
        "detaching an entity removes its inspectable attachments"
    );
    test_check(game_behavior_pool_size(world, "move") == 1, "detached instance storage enters the definition pool");
    game_behavior_attach(world, e1, "move");
    test_check(
        game_behavior_instance(world, e1, "move") == pooled &&
            game_behavior_pool_size(world, "move") == 0,
        "steady-state attach/detach reuses the same instance storage"
    );
    g_promote_probe = 0;
    test_check(
        game_behavior_promote_variable(
            world,
            "move",
            counter->id,
            GAME_COMPONENT_GameHealth.id,
            behavior_promote_probe,
            NULL
        ) == 0 &&
            g_promote_probe,
        "the editor promotion operation has a component-bound callback"
    );
    test_check(
        game_behavior_attach(world, e1, "logging") == 0 &&
            game_behavior_dispatch(world, e1, "Log", (flow_inputs){ 0 }, NULL) == 1 &&
            game_behavior_log_count(world) == 1,
        "entity behavior messages are retained in the runtime log"
    );
    game_behavior_log_info log;
    test_check(
        game_behavior_log_info_at(world, 0, &log) == 0 &&
            log.entity.slot == e1.slot && strcmp(log.asset_name, "logging") == 0 &&
            strcmp(log.message, "entity log") == 0,
        "runtime log entries identify their entity and behavior asset"
    );
    game_behavior_clear_logs(world);

    test_check(
        game_behavior_attach(world, e1, "windup") == 0 &&
            game_behavior_attach(world, e2, "windup") == 0 &&
            game_behavior_dispatch(world, e1, "Tick", (flow_inputs){ 0 }, NULL) == 2 &&
            game_behavior_dispatch(world, e2, "Tick", (flow_inputs){ 0 }, NULL) == 2,
        "latent behavior runs as a continuation on each owning entity"
    );
    game_behavior_entity_state entity_state;
    test_check(
        game_behavior_entity_state_at(world, e1, "windup", &entity_state) == 0 &&
            entity_state.active && entity_state.suspended &&
            entity_state.entity.slot == e1.slot,
        "continuation state is explicit and scoped to its entity"
    );
    behavior_instance *e1_windup = game_behavior_instance(world, e1, "windup");
    behavior_instance *e2_windup = game_behavior_instance(world, e2, "windup");
    test_check(
        e1_windup != e2_windup && acount(e1_windup->activations) == 1 &&
            acount(e2_windup->activations) == 1,
        "two entities of one graph share no activation state"
    );
    test_check(
        game_behavior_set_debug_hold(world, e1, "windup", 1) == 0 &&
            game_behavior_set_breakpoints_enabled_for_asset(world, e1, "windup", 0) == 0 &&
            game_behavior_breakpoints_enabled_for_asset(world, e1, "windup") == 0,
        "breakpoint and activation retention can be scoped to one "
        "entity asset"
    );
    game_behavior_pump(world, 2.0);
    test_check(
        game_behavior_entity_state_at(world, e1, "windup", &entity_state) == 0 &&
            !entity_state.active && !entity_state.suspended,
        "entity continuation state becomes inspectable when resumed"
    );
    test_check(
        acount(e1_windup->activations) == 1 &&
            flow_run_status(e1_windup->activations[0]) == FLOW_COMPLETED &&
            acount(e2_windup->activations) == 0,
        "a selected entity retains its completed activation for "
        "inspection"
    );
    game_behavior_set_debug_hold(world, e1, "windup", 0);
    game_behavior_pump(world, 2.0);
    test_check(
        acount(e1_windup->activations) == 0 && acount(e2_windup->activations) == 0,
        "finished activations are reaped after inspection hold is released"
    );
    for (int frame = 0; frame < 600; frame++) {
        game_behavior_dispatch(world, e1, "Tick", (flow_inputs){ 0 }, NULL);
        game_behavior_pump(world, (double)frame + 3.0);
    }
    test_check(
        acount(game_behavior_instance(world, e1, "move")->activations) == 0,
        "per-frame dispatch leaves every completed activation queue empty"
    );
    test_check(
        game_behavior_destroy_entity(world, e2) == ECS_OK &&
            game_behavior_instance(world, e2, "move") == NULL,
        "entity destruction tears down every attached behavior first"
    );
    game_behavior_world_free(world);
    behavior_asset_free(behavior);
    behavior_asset_free(latent);
    behavior_asset_free(logging);
    ecs_world_free(ecs);
}
