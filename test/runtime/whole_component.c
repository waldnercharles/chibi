#include "execution/ecs_reader.h"
#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/authoring/local_edit.h"
#include "graph/model/local.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/runtime.h"
#include "test/suite_internal.h"
#include "test/whole_aggregate_fixture.h"

static pin_spec health_pin = { .name = "Health", .type = PIN_STRUCT, .type_name = "GameHealth" };
static pin_spec target_pin = { .name = "Target", .type = PIN_HANDLE, .type_name = "ecs.entity" };

/* Actual reflected nodes, including their oracle implementations and access
 * contracts. mode selects Self, explicit target, or a default-Self field Set. */
behavior_asset *whole_component_fixture(int mode)
{
    behavior_asset *a = behavior_asset_new("Component bodies");
    assert(!behavior_asset_set_owner_type(a, "ecs.entity"));
    graph *g = behavior_asset_add_function(a, "Update", 0);
    node *entry = g->nodes[0], *ret = g->nodes[1];
    assert(graph_add_pin(g, entry, 1, &target_pin) == 1);
    assert(graph_add_pin(g, entry, 1, &health_pin) == 2);
    assert(graph_add_pin(g, ret, 0, &health_pin) == 1);
    assert(graph_add_pin(g, ret, 0, &health_pin) == 2);
    const char *name = mode == 0 ? "Set_GameHealth"
                     : mode == 1 ? "SetEntity_GameHealth"
                                 : "Set_GameHealth_current";
    node *set = graph_add_node(g, registry_node_by_name(name));
    node *second = graph_add_node(g, registry_node_by_name("Set_GameHealth_current"));
    assert(!graph_set_pin_default(g, second, 0, 1, &(pin_value){ .type = PIN_I32, .i = 91 }));
    if (mode == 1) {
        assert(!graph_connect_data(g, entry, 1, set, 1));
        assert(!graph_connect_data(g, entry, 2, set, 2));
        assert(!graph_connect_data(g, entry, 1, second, 2));
    } else if (!mode) {
        assert(!graph_connect_data(g, entry, 2, set, 1));
    } else {
        node *get = graph_add_node(g, registry_node_by_name("Get_GameHealth"));
        assert(!graph_expose_output_fields(g, get, 0));
        int current = -1;
        for (int i = 1; i < get->output_count; i++) {
            if (!strcmp(get->output_pins[i].field_name, "current")) {
                current = i;
            }
        }
        assert(current > 0);
        node *add = graph_add_node(g, registry_node_by_name("Add"));
        assert(!graph_set_pin_type(g, add, 0, 0, PIN_I32, "int"));
        assert(!graph_set_pin_type(g, add, 0, 1, PIN_I32, "int"));
        assert(!graph_set_pin_type(g, add, 1, 0, PIN_I32, "int"));
        assert(!graph_connect_data(g, get, current, add, 0));
        assert(!graph_set_pin_default(g, add, 0, 1, &(pin_value){ .type = PIN_I32, .i = 7 }));
        assert(!graph_connect_data(g, add, 0, set, 1));
    }
    assert(!graph_connect_exec(g, entry, 0, set, 0, 0));
    assert(!graph_connect_exec(g, set, 0, second, 0, 0));
    assert(!graph_connect_exec(g, second, 0, ret, 0, 0));
    assert(!graph_connect_data(g, set, 1, ret, 1));
    assert(!graph_connect_data(g, second, 1, ret, 2));
    return a;
}
static void same_health(pin_value a, pin_value b)
{
    assert(a.type == b.type && a.type == PIN_STRUCT && a.ptr && b.ptr);
    assert(!strcmp(a.type_name, b.type_name));
    const GameHealth *x = a.ptr, *y = b.ptr;
    assert(x->current == y->current && x->maximum == y->maximum);
}
static void writes()
{
    for (int fixture = 0; fixture < 3; fixture++) {
        behavior_asset *a = whole_component_fixture(fixture);
        graph *g = a->graphs[1];
        for (int scenario = 0; scenario < 6; scenario++) {
            for (int budget = 1; budget <= 5; budget++) {
                int status = 0;
                flow_error expected_error = { 0 };
                unsigned long steps = 0;
                GameHealth expected_owner = { 0 }, expected_other = { 0 };
                pin_value expected[2] = { 0 };
                arena storage = { 0 };
                for (int mode = 0; mode < 3; mode++) {
                    ecs_world *world = ecs_world_new();
                    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL));
                    ecs_entity owner = ecs_world_create_entity(world),
                               other = ecs_world_create_entity(world);
                    assert(!GAME_GameHealth_add(world, owner, 0));
                    assert(!GAME_GameHealth_add(world, other, 0));
                    assert(!ecs_world_apply_commands(world));
                    *GAME_GameHealth_get(world, owner) = (GameHealth){ .current = 40,
                                                                       .maximum = 70 };
                    *GAME_GameHealth_get(world, other) = (GameHealth){ .current = 12,
                                                                       .maximum = 80 };
                    pin_value owner_value = { .type = PIN_HANDLE,
                                              .type_name = "ecs.entity",
                                              .u64 = ecs_entity_to_u64(owner) };
                    GameHealth input = { .current = 23, .maximum = 29 };
                    pin_value args[] = { { .type = PIN_HANDLE,
                                           .type_name = "ecs.entity",
                                           .u64 = ecs_entity_to_u64(other) },
                                         { .type = PIN_STRUCT,
                                           .type_name = "GameHealth",
                                           .ptr = scenario == 1 ? NULL : &input } };
                    if (scenario == 2) {
                        args[0].u64 = UINT64_C(0x12345678ffffffff);
                        owner_value.u64 = args[0].u64;
                    }
                    if (scenario == 3) {
                        assert(!GAME_GameHealth_remove(world, fixture == 1 ? other : owner, 0));
                        assert(!ecs_world_apply_commands(world));
                    }
                    if (scenario == 4) {
                        args[0].u64 += UINT64_C(1) << 32;
                        owner_value.u64 += UINT64_C(1) << 32;
                    }
                    behavior_instance *bi = behavior_instance_create(a, &owner_value, NULL, NULL);
                    behavior_instance_set_trace_enabled(bi, 0);
                    assert(!behavior_instance_set_backend(
                        bi,
                        (behavior_backend[]){ BEHAVIOR_ORACLE,
                                              BEHAVIOR_BYTECODE,
                                              behavior_default_backend() }[mode]
                    ));
                    if (scenario != 5) {
                        assert(!behavior_instance_bind_service(bi, "ecs.world", world));
                    }
                    flow_options options = { .instance = bi,
                                             .step_budget = (unsigned)budget };
                    flow_error error = { 0 };
                    flow_run *run = flow_run_start(
                        g,
                        g->nodes[0],
                        (flow_inputs){ args, 2 },
                        &options,
                        &error
                    );
                    assert(run);
                    uint64_t oracle = eval_oracle_calls();
                    int result = flow_run_resume(run);
                    const GameHealth *own = GAME_GameHealth_get_const(world, owner),
                                     *target = GAME_GameHealth_get_const(world, other);
                    if (!mode) {
                        status = result;
                        steps = flow_run_steps(run);
                        expected_error = *flow_run_error(run);
                        expected_owner = own ? *own : (GameHealth){ 0 };
                        expected_other = target ? *target : (GameHealth){ 0 };
                        if (!result) {
                            for (int i = 0; i < 2; i++) {
                                assert(!pin_value_snapshot_to_arena(
                                    &storage,
                                    &expected[i],
                                    flow_run_input_value(run, g->nodes[1], i + 1)
                                ));
                            }
                        }
                    } else {
                        assert(run->whole_function_executed && !acount(run->children));
                        assert(result == status && flow_run_steps(run) == steps);
                        const flow_error *actual = flow_run_error(run);
                        assert(
                            actual->node == expected_error.node &&
                            actual->pin == expected_error.pin
                        );
                        if (result && result != FLOW_ERR_STEP_BUDGET) {
                            assert(!strcmp(actual->message, expected_error.message));
                        }
                        if (own) {
                            assert(
                                own->current == expected_owner.current &&
                                own->maximum == expected_owner.maximum
                            );
                        }
                        if (target) {
                            assert(
                                target->current == expected_other.current &&
                                target->maximum == expected_other.maximum
                            );
                        }
                        if (!result) {
                            for (int i = 0; i < 2; i++) {
                                same_health(
                                    expected[i],
                                    *flow_run_input_value(run, g->nodes[1], i + 1)
                                );
                            }
                        }
                        assert(eval_oracle_calls() == oracle);
                    }
                    flow_run_free(run);
                    behavior_instance_destroy(bi);
                    ecs_world_free(world);
                }
                arena_free(&storage);
            }
        }
        behavior_asset_free(a);
    }
}
static behavior_asset *read_fixture(int kind)
{
    behavior_asset *a = behavior_asset_new("Component reads");
    assert(!behavior_asset_set_owner_type(a, "ecs.entity"));
    graph *g = behavior_asset_add_function(a, "Read", 0);
    node *entry = g->nodes[0], *ret = g->nodes[1];
    assert(graph_add_pin(g, entry, 1, &target_pin) == 1);
    pin_spec output = kind == 1 || kind == 3
                        ? (pin_spec){ .name = "Has", .type = PIN_BOOL, .type_name = "bool" }
                    : kind == 5 ? (pin_spec){ .name = "Maximum",
                                              .type = PIN_F64,
                                              .type_name = "double" }
                                : health_pin;
    assert(graph_add_pin(g, ret, 0, &output) == 1);
    assert(!graph_connect_exec(g, entry, 0, ret, 0, 0));
    if (kind < 4) {
        const char *names[] = { "Get_GameHealth",
                                "Has_GameHealth",
                                "GetEntity_GameHealth",
                                "HasEntity_GameHealth" };
        node *read = graph_add_node(g, registry_node_by_name(names[kind]));
        if (kind >= 2) {
            assert(!graph_connect_data(g, entry, 1, read, 0));
        }
        assert(!graph_connect_data(g, read, 0, ret, 1));
    } else {
        pin_binding binding = {
            .kind = kind == 4 ? PIN_BINDING_COMPONENT : PIN_BINDING_COMPONENT_FIELD,
            .component_key = (char *)GAME_COMPONENT_GameHealth.key,
            .field_name = kind == 5 ? "maximum" : NULL
        };
        /* Integer-to-real conversion is a separate explicit language operation.
         * This fixture binds an integer, then converts through the graph edge. */
        if (kind == 5) {
            assert(!graph_set_pin_type(g, ret, 0, 1, PIN_I32, "int"));
        }
        assert(!graph_set_pin_binding(g, ret, 0, 1, &binding));
    }
    return a;
}
static void reads()
{
    for (int kind = 0; kind < 6; kind++) {
        behavior_asset *a = read_fixture(kind);
        graph *g = a->graphs[1];
        for (int scenario = 0; scenario < 8; scenario++) {
            flow_error expected_error = { 0 };
            int status = 0;
            pin_value expected = { 0 };
            arena memory = { 0 };
            for (int mode = 0; mode < 4; mode++) {
                ecs_world *world = ecs_world_new();
                assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL));
                ecs_entity owner = ecs_world_create_entity(world);
                assert(!GAME_GameHealth_add(world, owner, 0));
                assert(!ecs_world_apply_commands(world));
                *GAME_GameHealth_get(world, owner) = (GameHealth){ .current = 16,
                                                                   .maximum = 32 };
                pin_value handle = { .type = PIN_HANDLE,
                                     .type_name = "ecs.entity",
                                     .u64 = ecs_entity_to_u64(owner) };
                if (scenario == 1) {
                    handle.u64 = 0;
                }
                if (scenario == 2) {
                    handle.u64 = UINT64_MAX;
                }
                if (scenario == 3) {
                    assert(!GAME_GameHealth_remove(world, owner, 0));
                    assert(!ecs_world_apply_commands(world));
                }
                if (scenario >= 5) {
                    assert(!ecs_world_record_destroy(world, owner, 0));
                    if (scenario >= 6) {
                        assert(!ecs_world_apply_commands(world));
                    }
                    if (scenario == 7) {
                        ecs_entity replacement = ecs_world_create_entity(world);
                        assert(
                            replacement.slot == owner.slot &&
                            replacement.generation != owner.generation
                        );
                        assert(!GAME_GameHealth_add(world, replacement, 0));
                        assert(!ecs_world_apply_commands(world));
                    }
                }
                behavior_instance *bi = behavior_instance_create(a, &handle, NULL, NULL);
                behavior_instance_set_trace_enabled(bi, mode == 3);
                assert(!behavior_instance_set_backend(
                    bi,
                    (behavior_backend[]){ BEHAVIOR_ORACLE,
                                          BEHAVIOR_BYTECODE,
                                          behavior_default_backend(),
                                          behavior_default_backend() }[mode]
                ));
                eval_component_reader reader;
                ecs_component_reader_init(&reader, world);
                if (scenario != 4) {
                    assert(!behavior_instance_bind_service(
                        bi,
                        kind >= 4 ? EVAL_COMPONENT_READER_SERVICE : "ecs.world",
                        kind >= 4 ? (void *)&reader : world
                    ));
                }
                flow_options options = { .instance = bi };
                flow_error error = { 0 };
                flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ &handle, 1 }, &options, &error);
                assert(run);
                uint64_t oracle = eval_oracle_calls();
                int result = flow_run_resume(run);
                if (!mode) {
                    status = result;
                    expected_error = *flow_run_error(run);
                    if (!result) {
                        assert(!pin_value_snapshot_to_arena(
                            &memory,
                            &expected,
                            flow_run_input_value(run, g->nodes[1], 1)
                        ));
                    }
                } else {
                    assert(run->whole_function_executed == (mode != 3) && result == status);
                    const flow_error *actual = flow_run_error(run);
                    assert(
                        actual->node == expected_error.node &&
                        actual->pin == expected_error.pin
                    );
                    if (result) {
                        assert(!strcmp(actual->message, expected_error.message));
                    } else {
                        const pin_value *value = flow_run_input_value(run, g->nodes[1], 1);
                        if (kind == 1 || kind == 3) {
                            assert(value->b == expected.b);
                        } else if (kind == 5) {
                            assert(value->i == expected.i);
                        } else {
                            same_health(expected, *value);
                        }
                    }
                    assert(oracle == eval_oracle_calls());
                }
                flow_run_free(run);
                behavior_instance_destroy(bi);
                ecs_world_free(world);
            }
            arena_free(&memory);
        }
        behavior_asset_free(a);
    }
}
static void layout_guard()
{
    ecs_world *world = ecs_world_new();
    uint64_t hash = GAME_COMPONENT_GameHealth.id * UINT64_C(11400714819323198485);
    unsigned bucket = (unsigned)(hash ^ (hash >> 32)) & 127;
    ecs_component_desc collision = GAME_COMPONENT_GameHealth;
    collision.key = collision.name = "Collision";
    for (collision.id = 1000;; collision.id++) {
        uint64_t candidate = collision.id * UINT64_C(11400714819323198485);
        if (((unsigned)(candidate ^ (candidate >> 32)) & 127) == bucket) {
            break;
        }
    }
    assert(!ecs_world_register_component(world, &collision, NULL));
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL));
    ecs_entity owner = ecs_world_create_entity(world);
    assert(!GAME_GameHealth_add(world, owner, 0));
    assert(!ecs_world_apply_commands(world));
    /* Grow and collide in the lookup after the program was published. */
    behavior_asset *a = read_fixture(0);
    behavior_program_publish(a);
    const behavior_function *f = behavior_program_function(
        behavior_program_graph(behavior_program_cached(a), a->graphs[1]),
        a->graphs[1]->nodes[0]
    );
    assert(f);
    for (int i = 0; i < 40; i++) {
        char name[32];
        snprintf(name, sizeof name, "Extra%d", i);
        ecs_component_desc c = GAME_COMPONENT_GameHealth;
        c.id = (uint64_t)i + 1;
        c.key = c.name = name;
        assert(!ecs_world_register_component(world, &c, NULL));
    }
    typedef struct
    {
        int maximum, current, extra;
    } changed_health;
    static const type_field fields[] = {
        { "current", PIN_I32, offsetof(changed_health, current), sizeof(int), "int" },
        { "maximum", PIN_I32, offsetof(changed_health, maximum), sizeof(int), "int" },
        { "extra", PIN_I32, offsetof(changed_health, extra), sizeof(int), "int" },
    };
    static const type_desc changed_type = { .name = "GameHealth",
                                            .size = sizeof(changed_health),
                                            .fields = fields,
                                            .field_count = 3 };
    for (int schema = 0; schema < 2; schema++) {
        ecs_world *target = world;
        ecs_entity target_owner = owner;
        if (schema == 1) {
            ecs_component_desc c = GAME_COMPONENT_GameHealth;
            c.type = &changed_type;
            c.element_size = changed_type.size;
            /* A stale descriptor hash cannot hide the changed layout. */
            c.layout_hash = GAME_COMPONENT_GameHealth.layout_hash;
            target = ecs_world_new();
            assert(!ecs_world_register_component(target, &c, NULL));
            target_owner = ecs_world_create_entity(target);
            assert(!ecs_world_record_add(target, target_owner, c.id, NULL, 0));
            assert(!ecs_world_apply_commands(target));
        }
        for (int mode = 0; mode < 2; mode++) {
            arena storage = { 0 };
            pin_value results[2] = { 0 };
            behavior_function_prepare_results(f, results, &storage);
            behavior_function_context ctx = {
                .results = results,
                .budget = 100,
                .world = (const struct ecs_execution_view *)target,
                .owner = ecs_entity_to_u64(target_owner)
            };
            int result = behavior_function_run(f, &ctx, mode != 0);
            assert((result != 0) == (schema == 1));
            if (result) {
                assert(result == FLOW_ERR_INVALID_GRAPH && ctx.error_pin == 0);
                assert(strstr(ctx.error_message, "layout changed"));
            }
            arena_free(&storage);
        }
        if (target != world) {
            ecs_world_free(target);
        }
    }
    behavior_asset_free(a);
    ecs_world_free(world);
}

static void authoring_bindings()
{
    behavior_asset *asset = behavior_asset_new("Nested inline vector");
    assert(!behavior_asset_set_owner_type(asset, "ecs.entity"));
    graph *g = behavior_asset_add_function(asset, "Read", false);
    node *entry = g->nodes[0], *ret = g->nodes[1];
    pin_spec vector = { .name = "Vector", .type = PIN_STRUCT, .type_name = "CF_V3" };
    assert(graph_add_pin(g, ret, false, &vector) == 1);
    node *scalar = graph_add_node(g, registry_node_by_name("Add"));
    assert(scalar);
    assert(!graph_set_pin_type(g, scalar, false, 0, PIN_F32, "float"));
    assert(!graph_set_pin_type(g, scalar, false, 1, PIN_F32, "float"));
    assert(!graph_set_pin_type(g, scalar, true, 0, PIN_F32, "float"));
    pin_binding binding = { .kind = PIN_BINDING_COMPONENT_FIELD,
                            .component_key = GAME_COMPONENT_GameAim.key,
                            .field_name = "direction.y" };
    assert(!graph_set_pin_binding(g, scalar, false, 0, &binding));
    assert(!graph_connect_data_conversion(g, scalar, 0, ret, 1, &NODE_Splat_CF_V3));
    assert(graph_create_local(g, scalar, 0, "AimY"));
    assert(!graph_connect_exec(g, entry, 0, ret, 0, 0));
    ecs_world *world = ecs_world_new();
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameAim, NULL));
    ecs_entity owner = ecs_world_create_entity(world);
    assert(!GAME_GameAim_add(world, owner, 0));
    assert(!ecs_world_apply_commands(world));
    *GAME_GameAim_get(world, owner) = (GameAim){ .direction = { 3, -4 } };
    pin_value handle = { .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(owner) };
    eval_component_reader reader;
    ecs_component_reader_init(&reader, world);
    behavior_program_publish(asset);
    const behavior_function *function = behavior_program_function(
        behavior_program_graph(behavior_program_cached(asset), g),
        entry
    );
    assert(function);
    for (int mode = 0; mode < 3; mode++) {
        for (int inspected = 0; inspected < 2; inspected++) {
            behavior_instance *bi = behavior_instance_create(asset, &handle, NULL, NULL);
            assert(bi);
            assert(!behavior_instance_set_backend(
                bi,
                (behavior_backend[]){ BEHAVIOR_ORACLE,
                                      BEHAVIOR_BYTECODE,
                                      behavior_default_backend() }[mode]
            ));
            behavior_instance_set_trace_enabled(bi, inspected);
            assert(!behavior_instance_bind_service(bi, EVAL_COMPONENT_READER_SERVICE, &reader));
            flow_options options = { .instance = bi };
            flow_error error = { 0 };
            flow_run *run = flow_run_start(g, entry, (flow_inputs){ 0 }, &options, &error);
            assert(run);
            uint64_t oracle = eval_oracle_calls();
            assert(!flow_run_resume(run));
            const pin_value *result = flow_run_input_value(run, ret, 1);
            assert(result && result->type == PIN_STRUCT && result->ptr);
            const CF_V3 *value = result->ptr;
            assert(value->x == -4 && value->y == -4 && value->z == -4);
            assert(!mode || eval_oracle_calls() == oracle);
            assert(!mode || inspected || run->whole_function_executed);
            flow_run_free(run);
            behavior_instance_destroy(bi);
        }
    }
    ecs_world_free(world);
    behavior_asset_free(asset);
}

void test_compiled_components()
{
    reads();
    authoring_bindings();
    layout_guard();
    writes();
    for (int portable = 0; portable < 2; portable++) {
        behavior_backend previous = behavior_debug_backend(
            portable ? BEHAVIOR_BYTECODE : BEHAVIOR_SLJIT
        );
        whole_component_smoke();
        behavior_debug_backend(previous);
    }
    puts("PASS: whole component writes, snapshots, stale handles, missing components, source errors and budgets");
}
