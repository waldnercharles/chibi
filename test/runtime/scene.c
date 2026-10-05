#include "test/asset_fixture.h"
#include "test/suite_internal.h"

static const char *
    g_prototype_base_json = "name \"BaseEnemy\"; components { GameHealth { "
                            "current 50; maximum 50; }; GameTransform { x 1; y "
                            "2; }; }; (array)behaviors { - \"idle.graph\"; }; ";
static int g_prototype_attach_count;
static const char
    *g_cycle_a = "name \"CycleA\"; (array)includes { - \"CycleB\"; }; ";
static const char
    *g_cycle_b = "name \"CycleB\"; (array)includes { - \"CycleA\"; }; ";

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
        *derived_json = "name \"Enemy\"; identity \"enemy-id\"; "
                        "(array)includes { - \"BaseEnemy\"; }; components { "
                        "GameHealth { current 100; }; }; (array)behaviors { - "
                        "\"attack.graph\"; }; ";
    static const char *
        reordered_json = "(array)behaviors { - \"attack.graph\"; - "
                         "\"idle.graph\"; }; identity \"enemy-id\"; components "
                         "{ GameTransform { y 2; x 1; }; GameHealth { maximum "
                         "50; current 100; }; }; name \"Enemy\"; ";
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
    game_prototype *plain = game_prototype_load("name \"Plain\"; ", &registry, error, sizeof error);
    game_prototype *other = game_prototype_load("name \"Other\"; ", &registry, error, sizeof error);
    char *saved_plain = plain ? game_prototype_save(plain) : NULL;
    test_check(
        plain && other &&
            strcmp(game_prototype_identity(plain), game_prototype_identity(other)) != 0 &&
            saved_plain && !strstr(saved_plain, "identity"),
        "identity derives from the name and is not written for a named "
        "prototype"
    );
    free(saved_plain);
    game_prototype_free(plain);
    game_prototype_free(other);
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
    av_value *overrides = game_prototype_capture_overrides_value(base, world, entity);
    test_check(
        base && overrides &&
            av_get_int(av_obj_get(av_obj_get(overrides, "GAME.GameHealth"), "current")) == 7 &&
            !av_obj_get(av_obj_get(overrides, "GAME.GameHealth"), "maximum"),
        "instance capture stores sparse field overrides"
    );
    av_value *sparse = test_read_asset_value("GameHealth { current 7; }; ");
    char **owned_strings = NULL;
    ecs_entity override_entity = { 0 };
    test_check(
        base &&
            game_prototype_spawn(base, world, &hooks, &override_entity, error, sizeof error) ==
                ECS_OK &&
            game_prototype_apply_components_value(
                world,
                override_entity,
                sparse,
                &registry,
                &owned_strings,
                error,
                sizeof error
            ) == 0 &&
            GAME_GameHealth_get(world, override_entity)->current == 7 &&
            GAME_GameHealth_get(world, override_entity)->maximum == 50,
        "sparse field overrides apply without zeroing sibling fields"
    );
    av_value_free(sparse);
    GAME_GameHealth_get(world, entity)->current = 100;
    av_value_free(overrides);
    game_prototype_free(base);
    game_prototype *captured = game_prototype_capture_entity("CapturedEnemy", world, entity, &registry);
    av_value *captured_components = game_prototype_components_value(captured);
    test_check(
        captured && captured_components &&
            av_obj_get(captured_components, "GAME.GameHealth"),
        "a live entity can be captured through the generic prototype registry"
    );
    av_value_free(captured_components);
    game_prototype_free(captured);
    game_prototype_free(prototype);
    GameHealth *spawned_health = GAME_GameHealth_get(world, entity);
    test_check(
        spawned_health && spawned_health->current == 100 && spawned_health->maximum == 50,
        "editing/freeing a prototype does not change spawned entity values"
    );
    ecs_world_free(world);
    game_prototype_owned_strings_free(owned_strings);
    test_check(health != NULL, "descriptor lookup is independent of component C types");
}

behavior_asset *make_behavior_behavior_asset(const char *name, int latent)
{
    behavior_asset *asset = behavior_asset_new(name);
    if (!asset) {
        return NULL;
    }
    graph *graph = asset->graph;
    node *event = graph_add_node(graph, registry_node_by_name("Event"));
    if (!event || graph_set_event_name(graph, event, "Tick") != 0) {
        behavior_asset_free(asset);
        return NULL;
    }
    node *begin = NULL;
    if (!latent) {
        begin = graph_add_node(graph, registry_node_by_name("Event"));
        if (!begin || graph_set_event_name(graph, begin, "Begin") != 0) {
            behavior_asset_free(asset);
            return NULL;
        }
    }
    if (latent) {
        node *delay = graph_add_node(graph, registry_node_by_name("Delay"));
        pin_value duration = { .type = PIN_F64, .type_name = "double", .d = 1.0 };
        if (!delay || graph_set_pin_default(graph, delay, 0, 1, &duration) != 0 ||
            graph_connect_exec(graph, event, 0, delay, 0, 0) != 0) {
            behavior_asset_free(asset);
            return NULL;
        }
        return asset;
    }
    behavior_variable *counter = behavior_asset_add_variable(
        asset,
        "Counter",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_I32, .i = 0 },
        "Behavior"
    );
    node *set = counter ? behavior_asset_add_set_node(asset, counter->id) : NULL;
    pin_value one = { .type = PIN_I32, .type_name = "int", .i = 1 };
    if (!counter || !set || graph_set_pin_default(graph, set, 0, 1, &one) != 0 ||
        graph_connect_exec(graph, event, 0, set, 0, 0) != 0 ||
        graph_connect_exec(graph, begin, 0, set, 0, 0) != 0) {
        behavior_asset_free(asset);
        return NULL;
    }
    return asset;
}

void run_scene_document_checks(void)
{
    {
        const char
            *source = "version 1; name \"Readable\"; (array)entities { - { "
                      "name \"Fluid\"; prototype \"Base\"; components { "
                      "GameFluidVolume kind=1 color=966781183 "
                      "absorption=0.035000000000000003; "
                      "GameTransform x=0.15000000596046448 y=-0.0; "
                      "GameCollider category=16; }; }; };";
        char error[256];
        game_scene_document *scene = game_scene_document_load(source, error, sizeof error);
        assert(scene);
        char *saved = game_scene_document_save(scene);
        assert(
            saved && strstr(saved, "kind=\"Liquid\"") &&
            strstr(saved, "color=0x399fe8ff") &&
            strstr(saved, "absorption=0.035") && strstr(saved, "x=0.15") &&
            strstr(saved, "y=-0.0") && strstr(saved, "category=16")
        );
        game_scene_document *loaded = game_scene_document_load(saved, error, sizeof error);
        assert(loaded);
        char *again = game_scene_document_save(loaded);
        assert(again && !strcmp(saved, again));
        av_value *before = av_obj_get(scene->entities[0].components, "GameTransform");
        av_value *after = av_obj_get(loaded->entities[0].components, "GameTransform");
        float a = (float)av_get_num(av_obj_get(before, "x"));
        float b = (float)av_get_num(av_obj_get(after, "x"));
        assert(!memcmp(&a, &b, sizeof a));
        assert(signbit(av_get_num(av_obj_get(after, "y"))));
        free(again);
        free(saved);
        game_scene_document_free(loaded);
        game_scene_document_free(scene);
    }
    const char *behaviors[] = { "SceneBehavior", "Damage" };
    game_scene_document *document = game_scene_document_new("CombatScene");
    test_check(
        document &&
            game_scene_document_add_entity(document, "Player", "PlayerPrototype", behaviors, 2) ==
                0,
        "a scene entity stores its prototype and behavior assets"
    );
    av_value *component_values = test_read_asset_value("GameHealth { current 7; maximum 9; }; ");
    test_check(
        game_scene_document_set_entity_components(document, 0, component_values) == 0 &&
            av_obj_get(document->entities[0].components, "GameHealth"),
        "a scene entity stores generic component overrides"
    );
    av_value_free(component_values);
    component_values = test_read_asset_value("GameAppearance shape=0; ");
    assert(game_scene_document_set_entity_components(document, 0, component_values) == 0);
    char *enum_text = game_scene_document_save(document);
    char enum_error[256];
    av_doc *enum_saved = av_doc_read(enum_text, enum_error, sizeof enum_error);
    av_value *enum_entity = enum_saved
                              ? av_arr_get(av_obj_get(av_doc_root(enum_saved), "entities"), 0)
                              : NULL;
    av_value *enum_component = av_obj_get(av_obj_get(enum_entity, "components"), "GameAppearance");
    test_check(
        enum_saved && av_is_str(av_obj_get(enum_component, "shape")) &&
            av_get_int(av_obj_get(av_obj_get(component_values, "GameAppearance"), "shape")) ==
                0,
        "scene enum overrides save as named choices without changing the "
        "caller's numeric value"
    );
    av_doc_free(enum_saved);
    free(enum_text);
    av_value_free(component_values);
    component_values = test_read_asset_value("GameHealth { current 7; maximum 9; }; GameAppearance { shape 4294967296; }; ");
    test_check(
        game_scene_document_set_entity_components(document, 0, component_values) == 0,
        "scene overrides accept numeric component data for serialization"
    );
    av_value_free(component_values);
    document->physics = (game_physics_settings){ .workers = 8,
                                                 .substeps = 2,
                                                 .sleep = false,
                                                 .continuous = true,
                                                 .force_drive = true,
                                                 .contact_hertz = 15,
                                                 .contact_damping = 1 };
    char *saved = game_scene_document_save(document);
    char error[256];
    game_scene_document *loaded = game_scene_document_load(saved, error, sizeof error);
    test_check(saved && strstr(saved, "4294967296"), "scene save does not truncate an out-of-range enum while labeling it");
    test_check(
        loaded && game_physics_settings_equal(document->physics, loaded->physics),
        "scene physics settings round-trip"
    );
    game_scene_document *minimal = game_scene_document_load(
        "version 1; name \"Minimal\"; (array)entities { }; ",
        error,
        sizeof error
    );
    test_check(
        minimal &&
            game_physics_settings_equal(minimal->physics, game_physics_defaults()),
        "an omitted physics object uses defaults"
    );
    game_scene_document_free(minimal);
    const char *invalid_physics[] = { "substeps 0; ", "substeps 1.5; ",
                                      "workers 33; ", "contact_damping -1; ",
                                      "sleep 0; ",    "unknown #true; " };
    for (int i = 0; i < 6; i++) {
        char json[256];
        snprintf(
            json,
            sizeof json,
            "version 1; name \"Invalid\"; (array)entities; physics { %s }; ",
            invalid_physics[i]
        );
        game_scene_document *bad = game_scene_document_load(json, error, sizeof error);
        test_check(!bad && strstr(error, "physics"), "malformed physics settings are rejected");
        game_scene_document_free(bad);
    }
    test_check(
        loaded && acount(loaded->entities) == 1 &&
            strcmp(loaded->entities[0].name, "Player") == 0 &&
            strcmp(loaded->entities[0].prototype, "PlayerPrototype") == 0 &&
            av_obj_get(loaded->entities[0].components, "GameHealth") &&
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
        "version 2; name \"Bad\"; (array)entities { }; ",
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
    behavior_asset *asset = behavior_asset_new("LogBehavior");
    node *event = asset
                    ? graph_add_node(asset->graph, registry_node_by_name("Event"))
                    : NULL;
    node *print = asset
                    ? graph_add_node(asset->graph, registry_node_by_name("Print String"))
                    : NULL;
    node *text = asset ? make_string(asset->graph, "entity log") : NULL;
    if (!asset || !event || !print || !text ||
        behavior_asset_set_owner_type(asset, "ecs.entity") != 0 ||
        graph_set_event_name(asset->graph, event, "Log") != 0 ||
        graph_connect_exec(asset->graph, event, 0, print, 0, 0) != 0 ||
        graph_connect_data(asset->graph, text, 0, print, 1) != 0) {
        behavior_asset_free(asset);
        return NULL;
    }
    return asset;
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
    test_check(game_behavior_set_debug_hold(world, e1, "windup", 1) == 0, "activation retention can be scoped to one entity asset");
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
