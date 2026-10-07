#include "assets/code_source.h"
#include "graph/compiler/graph_jit_sljit.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "runtime/generated.h"
#include "runtime/systems/system.h"
#include "support/arena.h"
#include "support/array.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static node *add(graph *graph, const char *name)
{
    const node_desc *desc = registry_node_by_name(name);
    assert(desc);
    return graph_add_node(graph, desc);
}

static void typed_pin(graph *graph, node *node, int output, int index, pin_type type, const char *name)
{
    assert(graph_set_pin_type(graph, node, output, index, type, name) == 0);
}

static void value(graph *graph, node *node, int index, pin_value value)
{
    assert(graph_set_pin_default(graph, node, 0, index, &value) == 0);
}

static game_system *finish(ecs_world *world, behavior_asset *asset, node *result, const char *component)
{
    node *tick = add(asset->graph, "Event");
    assert(graph_set_event_name(asset->graph, tick, "Tick") == 0);
    node *set = add(asset->graph, component);
    assert(graph_connect_exec(asset->graph, tick, 0, set, 0, 0) == 0);
    assert(graph_connect_data(asset->graph, result, 0, set, 1) == 0);
    game_system *system = game_system_new(world, asset);
    assert(system);
    const compile_report *report = game_system_compile(system);
    for (int i = 0; i < acount(report->items); i++) {
        if (report->items[i].severity == CMPL_ERROR) {
            fprintf(stderr, "System arithmetic: %s\n", report->items[i].message);
        }
    }
    assert(!compile_report_blocked(report));
    assert(game_system_uses_bytecode(system));
    return system;
}

static behavior_asset *asset()
{
    behavior_asset *result = behavior_asset_new("System arithmetic");
    assert(behavior_asset_set_owner_type(result, "ecs.entity") == 0);
    return result;
}

static bool unavailable(game_system *system, int mode)
{

    return (mode == GAME_SYSTEM_EXEC_TYPED_BYTECODE &&
            !game_system_uses_typed_bytecode(system)) ||
           (mode == GAME_SYSTEM_EXEC_SJLIT && !game_system_uses_sljit(system));
}

static void check_run(game_system *system, int mode, const node *operation, bool invalid)
{
    flow_error error = { 0 };
    int result = game_system_tick_mode(system, 0, 1.0 / 60.0, &error, (game_system_exec_mode)mode);
    if (unavailable(system, mode)) {
        assert(result == FLOW_ERR_STATE && error.code == FLOW_ERR_STATE);
        assert(
            strcmp(
                error.message,
                mode == GAME_SYSTEM_EXEC_TYPED_BYTECODE
                    ? "typed System bytecode is unavailable"
                    : "SLJIT System code is unavailable"
            ) == 0
        );
        return;
    }
    if (invalid) {
        assert(result == FLOW_ERR_NODE && error.code == FLOW_ERR_NODE);
        assert(error.node == operation);
        assert(strcmp(error.message, "data function produced an invalid result") == 0);
        return;
    }
    if (result != FLOW_OK) {
        fprintf(stderr, "System arithmetic mode %d: %s\n", mode, error.message);
    }
    assert(result == FLOW_OK);
}

static int field(const node *node, const char *name)
{
    for (int i = 0; i < node->output_count; i++) {
        if (node->output_pins[i].field_name &&
            strcmp(node->output_pins[i].field_name, name) == 0) {
            return i;
        }
    }
    assert(false);
    return -1;
}

static void health_division(ecs_world *world, ecs_entity entity, int numerator, int divisor, int expected, bool invalid)
{
    behavior_asset *definition = asset();
    graph *graph = definition->graph;
    node *read = add(graph, "Get_GameHealth");
    assert(graph_expose_output_fields(graph, read, 0) == 0);
    node *divide = add(graph, "Divide");
    typed_pin(graph, divide, 0, 0, PIN_I32, "int");
    typed_pin(graph, divide, 0, 1, PIN_I32, "int");
    typed_pin(graph, divide, 1, 0, PIN_I32, "int");
    value(graph, divide, 1, (pin_value){ .type = PIN_I32, .i = divisor });
    node *make = add(graph, "Make_GameHealth");
    assert(graph_connect_data(graph, read, field(read, "current"), divide, 0) == 0);
    assert(graph_connect_data(graph, divide, 0, make, 0) == 0);
    assert(graph_connect_data(graph, read, field(read, "maximum"), make, 1) == 0);
    game_system *system = finish(world, definition, make, "Set_GameHealth");
    assert(!game_system_uses_typed_bytecode(system) && !game_system_uses_sljit(system));
    for (int mode = GAME_SYSTEM_EXEC_AUTO; mode <= GAME_SYSTEM_EXEC_SJLIT; mode++) {
        *GAME_GameHealth_get(world, entity) = (GameHealth){ numerator, 777 };
        check_run(system, mode, divide, invalid);
        const GameHealth *health = GAME_GameHealth_get_const(world, entity);
        assert(health->current == (invalid || unavailable(system, mode) ? numerator : expected));
        assert(health->maximum == 777);
    }
    game_system_free(system);
}

/* Preserve all result bits in a real component without inserting a numeric
 * conversion, which would itself disable the backend and hide this bug. */
static GameHealth bits(pin_value value)
{
    GameHealth result = { 0, 777 };
    switch (value.type) {
        case PIN_I32: memcpy(&result.current, &value.i, sizeof value.i); break;
        case PIN_I64: memcpy(&result, &value.i64, sizeof value.i64); break;
        case PIN_F32: memcpy(&result.current, &value.f, sizeof value.f); break;
        case PIN_F64: memcpy(&result, &value.d, sizeof value.d); break;
        default: assert(false);
    }
    return result;
}

static pin_value capture(const pin_value *inputs, int count, arena *memory)
{
    assert(count == 1);
    GameHealth *result = arena_alloc(memory, sizeof *result);
    *result = bits(inputs[0]);
    return (pin_value){ .type = PIN_STRUCT, .type_name = "GameHealth", .ptr = result };
}

static int capture32(void *output, const void *input)
{
    GameHealth *result = output;
    memcpy(&result->current, input, sizeof result->current);
    result->maximum = 777;
    return 0;
}

static int capture64(void *output, const void *input)
{
    memcpy(output, input, sizeof(GameHealth));
    return 0;
}

static int capture32_fast(void *output, const void *const *inputs)
{
    return capture32(output, inputs[0]);
}

static int capture64_fast(void *output, const void *const *inputs)
{
    return capture64(output, inputs[0]);
}

static void scalar(
    ecs_world *world,
    ecs_entity entity,
    const char *name,
    pin_value left,
    pin_value right,
    pin_value expected,
    bool invalid
)
{
    behavior_asset *definition = asset();
    graph *graph = definition->graph;
    node *operation = add(graph, name);
    typed_pin(graph, operation, 0, 0, left.type, pin_type_name(left.type));
    typed_pin(graph, operation, 0, 1, right.type, pin_type_name(right.type));
    typed_pin(graph, operation, 1, 0, expected.type, pin_type_name(expected.type));
    value(graph, operation, 0, left);
    value(graph, operation, 1, right);
    bool wide = expected.type == PIN_I64 || expected.type == PIN_F64;
    pin_spec input = { .name = "Value",
                       .type = expected.type,
                       .type_name = pin_type_name(expected.type) };
    pin_spec output = { .name = "Result", .type = PIN_STRUCT, .type_name = "GameHealth" };
    const node_access access = { .flags = NODE_EFFECT_PURE };
    node_desc desc = { .name = "Capture arithmetic bits",
                       .input_count = 1,
                       .inputs = &input,
                       .output_count = 1,
                       .outputs = &output,
                       .pure_kind = NODE_PURE_CALL,
                       .fn = capture,
                       .fast = wide ? capture64_fast : capture32_fast,
                       .direct = { .one = wide ? capture64 : capture32 },
                       .direct_count = 2,
                       .access = &access };
    node *result = graph_add_node(graph, &desc);
    assert(graph_connect_data(graph, operation, 0, result, 0) == 0);
    game_system *system = finish(world, definition, result, "Set_GameHealth");
    for (int mode = GAME_SYSTEM_EXEC_AUTO; mode <= GAME_SYSTEM_EXEC_SJLIT; mode++) {
        GameHealth before = { 123, 777 };
        *GAME_GameHealth_get(world, entity) = before;
        check_run(system, mode, operation, invalid);
        GameHealth wanted = invalid || unavailable(system, mode) ? before
                                                                 : bits(expected);
        const GameHealth *actual = GAME_GameHealth_get_const(world, entity);
        if (memcmp(actual, &wanted, sizeof wanted) != 0) {
            fprintf(
                stderr,
                "System %s %s mode %d: got bits {%d, %d}, expected {%d, %d}\n",
                name,
                pin_type_name(expected.type),
                mode,
                actual->current,
                actual->maximum,
                wanted.current,
                wanted.maximum
            );
        }
        assert(memcmp(actual, &wanted, sizeof wanted) == 0);
    }
    game_system_free(system);
}

static void vectors(
    ecs_world *world,
    ecs_entity entity,
    const char *name,
    bool scalar_right,
    GameTransform expected,
    bool optimized
)
{
    behavior_asset *definition = asset();
    graph *graph = definition->graph;
    node *read = add(graph, "Get_GameTransform");
    node *operation = add(graph, name);
    typed_pin(graph, operation, 0, 0, PIN_STRUCT, "GameTransform");
    typed_pin(graph, operation, 1, 0, PIN_STRUCT, "GameTransform");
    assert(graph_connect_data(graph, read, 0, operation, 0) == 0);
    if (scalar_right) {
        typed_pin(graph, operation, 0, 1, PIN_F32, "float");
        value(graph, operation, 1, (pin_value){ .type = PIN_F32, .f = 8 });
    } else {
        node *right = add(graph, "Make_CF_V2");
        value(graph, right, 0, (pin_value){ .type = PIN_F32, .f = 8 });
        value(graph, right, 1, (pin_value){ .type = PIN_F32, .f = 4 });
        typed_pin(graph, operation, 0, 1, PIN_STRUCT, "CF_V2");
        assert(graph_connect_data(graph, right, 0, operation, 1) == 0);
    }
    game_system *system = finish(world, definition, operation, "Set_GameTransform");
    assert(game_system_uses_typed_bytecode(system) == optimized);
    assert(game_system_uses_sljit(system) == (optimized && graph_sljit_available()));
    for (int mode = GAME_SYSTEM_EXEC_AUTO; mode <= GAME_SYSTEM_EXEC_SJLIT; mode++) {
        GameTransform before = { 17, -9 };
        *GAME_GameTransform_get(world, entity) = before;
        check_run(system, mode, operation, false);
        GameTransform wanted = unavailable(system, mode) ? before : expected;
        const GameTransform *actual = GAME_GameTransform_get_const(world, entity);
        if (actual->x != wanted.x || actual->y != wanted.y) {
            fprintf(
                stderr,
                "%s vector mode %d: got (%g, %g), expected (%g, %g)\n",
                name,
                mode,
                actual->x,
                actual->y,
                wanted.x,
                wanted.y
            );
        }
        assert(actual->x == wanted.x && actual->y == wanted.y);
    }
    game_system_free(system);
}

static int shift_direct(void *out, const void *input)
{
    const GameTransform *v = input;
    *(GameTransform *)out = (GameTransform){ v->x + 10, v->y - 20 };
    return 0;
}
static int shift_fast(void *out, const void *const *inputs)
{
    return shift_direct(out, inputs[0]);
}
static pin_value shift_value(const pin_value *args, int, arena *storage)
{
    pin_value out = { .type = PIN_STRUCT,
                      .type_name = "CF_V2",
                      .ptr = arena_alloc(storage, sizeof(GameTransform)) };
    shift_direct(out.ptr, args[0].ptr);
    return out;
}
static void nonidentity_conversion(ecs_world *world, ecs_entity entity)
{
    behavior_asset *definition = asset();
    node_desc desc = *registry_node_by_name("game_transform_to_cf");
    desc.jit_kind = NODE_JIT_NONE;
    desc.fn = shift_value;
    desc.fast = shift_fast;
    desc.direct.one = shift_direct;
    node *read = add(definition->graph, "Get_GameTransform");
    node *operation = graph_add_node(definition->graph, &desc);
    assert(!graph_connect_data(definition->graph, read, 0, operation, 0));
    game_system *system = finish(world, definition, operation, "Set_GameTransform");
    assert(game_system_uses_sljit(system) == graph_sljit_available());
    for (int mode = GAME_SYSTEM_EXEC_AUTO; mode <= GAME_SYSTEM_EXEC_SJLIT; mode++) {
        *GAME_GameTransform_get(world, entity) = (GameTransform){ 3, 4 };
        check_run(system, mode, operation, false);
        const GameTransform *actual = GAME_GameTransform_get_const(world, entity);
        GameTransform expected = unavailable(system, mode)
                                   ? (GameTransform){ 3, 4 }
                                   : (GameTransform){ 13, -16 };
        assert(actual->x == expected.x && actual->y == expected.y);
    }
    game_system_free(system);
}

/* Independent ECS oracle for the reflected getter/unwrap ABI migration. */
static void movement_source_oracle()
{
    const char
        *text = "system \"Movement oracle\" { owner \"ecs.entity\"; dataflow; "
                "on Tick as tick(DeltaSeconds: float = 0.0) { "
                "set_game_transform(Value: add(A: struct<\"CF_V2\"> = "
                "get_game_transform(), "
                "B: struct<\"CF_V2\"> = multiply(A: struct<\"CF_V2\"> = "
                "multiply(A: struct<\"CF_V2\"> = get_game_input(), B: "
                "get_game_movement()), "
                "B: tick.DeltaSeconds))); } }";
    const double deltas[] = { 0, 1.0 / 60.0, 0.125, 1.0 / 1000.0, 0.75, 0.03125 };
    const CF_V2 directions[] = { { 1, 0 }, { -0.75f, 0.5f }, { 0, -1 }, { 0.125f, -0.25f } };
    const float speeds[] = { 0, 180, -12.5f, 0.03125f };
    code_source *source = code_source_parse(text);
    assert(source);
    behavior_asset *definition = code_source_build_behavior(source);
    assert(definition);
    code_source_free(source);
    for (int mode = GAME_SYSTEM_EXEC_GENERIC_BYTECODE; mode <= GAME_SYSTEM_EXEC_SJLIT;
         mode++) {
        ecs_world *world = ecs_world_new();
        assert(ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
        assert(ecs_world_register_component(world, &GAME_COMPONENT_GameMovement, NULL) == ECS_OK);
        assert(ecs_world_register_component(world, &GAME_COMPONENT_GameInput, NULL) == ECS_OK);
        game_system *system = game_system_new(world, behavior_asset_clone(definition));
        assert(system && !compile_report_blocked(game_system_compile(system)));
        assert(game_system_program(system) && game_system_uses_bytecode(system));
        assert(game_system_uses_typed_bytecode(system));
        assert(!graph_sljit_available() || game_system_uses_sljit(system));
        if (unavailable(system, mode)) {
            printf("Movement source oracle: backend %d unavailable\n", mode);
            game_system_free(system);
            ecs_world_free(world);
            continue;
        }
        flow_error error = { 0 };
        assert(game_system_tick_mode(system, 0, 0.125, &error, mode) == FLOW_OK);
        assert(game_system_match_count(system) == 0);
        ecs_entity entities[7];
        GameTransform expected[7];
        for (int i = 0; i < 7; i++) {
            entities[i] = ecs_world_create_entity(world);
            /* Four complete rows, followed by one row missing each component. */
            if (i != 4) {
                assert(GAME_GameTransform_add(world, entities[i], 0) == ECS_OK);
            }
            if (i != 5) {
                assert(GAME_GameMovement_add(world, entities[i], 0) == ECS_OK);
            }
            if (i != 6) {
                assert(GAME_GameInput_add(world, entities[i], 0) == ECS_OK);
            }
        }
        assert(ecs_world_apply_commands(world) == ECS_OK);
        for (int i = 0; i < 7; i++) {
            expected[i] = (GameTransform){ (float)i * 7.25f, (float)i * -2.5f };
            if (i != 4) {
                *GAME_GameTransform_get(world, entities[i]) = expected[i];
            }
        }
        double time = 0;
        for (int tick = 0; tick < 24; tick++) {
            double delta = deltas[tick % 6];
            for (int i = 0; i < 7; i++) {
                CF_V2 direction = directions[(i + tick) % 4];
                float speed = speeds[(i + tick / 3) % 4];
                if (i != 6) {
                    GAME_GameInput_get(world, entities[i])->move = direction;
                }
                if (i != 5) {
                    GAME_GameMovement_get(world, entities[i])->speed = speed;
                }
                if (i < 4) {
                    expected[i].x += direction.x * speed * (float)delta;
                    expected[i].y += direction.y * speed * (float)delta;
                }
            }
            assert(game_system_tick_mode(system, time, delta, &error, mode) == FLOW_OK);
            assert(game_system_match_count(system) == 4);
            for (int i = 0; i < 7; i++) {
                if (i != 4) {
                    const GameTransform *actual = GAME_GameTransform_get_const(world, entities[i]);
                    assert(!memcmp(actual, &expected[i], sizeof *actual));
                }
                if (i != 6) {
                    CF_V2 direction = directions[(i + tick) % 4];
                    assert(
                        !memcmp(&GAME_GameInput_get_const(world, entities[i])->move, &direction, sizeof direction)
                    );
                }
                if (i != 5) {
                    assert(
                        GAME_GameMovement_get_const(world, entities[i])->speed ==
                        speeds[(i + tick / 3) % 4]
                    );
                }
            }
            time += delta;
        }
        printf("PASS: Movement source oracle backend %d, varied input and delta, missing components\n", mode);
        game_system_free(system);
        ecs_world_free(world);
    }
    behavior_asset_free(definition);
}

void test_system_arithmetic()
{
    movement_source_oracle();
    _Static_assert(sizeof(GameHealth) == sizeof(int64_t), "capture preserves 64-bit results");
    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(world);
    assert(GAME_GameHealth_add(world, entity, 0) == ECS_OK);
    assert(GAME_GameTransform_add(world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    nonidentity_conversion(world, entity);

    health_division(world, entity, 17, 8, 2, false);
    health_division(world, entity, -17, 8, -2, false);
    health_division(world, entity, 17, -8, -2, false);
    health_division(world, entity, -17, -8, 2, false);
    health_division(world, entity, 17, 0, 0, true);
    health_division(world, entity, INT_MIN, -1, 0, true);

    const struct
    {
        const char *name;
        int integer;
        double real;
    } operations[] = {
        { "Add", 25, 25 },      { "Subtract", 9, 9 }, { "Multiply", 136, 136 },
        { "Divide", 2, 2.125 }, { "Min", 8, 8 },      { "Max", 17, 17 },
        { "Modulo", 1, 1 },
    };
    for (int i = 0; i < (int)(sizeof operations / sizeof *operations); i++) {
        scalar(
            world,
            entity,
            operations[i].name,
            (pin_value){ .type = PIN_I32, .i = 17 },
            (pin_value){ .type = PIN_I32, .i = 8 },
            (pin_value){ .type = PIN_I32, .i = operations[i].integer },
            false
        );
        scalar(
            world,
            entity,
            operations[i].name,
            (pin_value){ .type = PIN_I64, .i64 = 17 },
            (pin_value){ .type = PIN_I64, .i64 = 8 },
            (pin_value){ .type = PIN_I64, .i64 = operations[i].integer },
            false
        );
        scalar(
            world,
            entity,
            operations[i].name,
            (pin_value){ .type = PIN_F32, .f = 17 },
            (pin_value){ .type = PIN_F32, .f = 8 },
            (pin_value){ .type = PIN_F32, .f = (float)operations[i].real },
            false
        );
        scalar(
            world,
            entity,
            operations[i].name,
            (pin_value){ .type = PIN_F64, .d = 17 },
            (pin_value){ .type = PIN_F64, .d = 8 },
            (pin_value){ .type = PIN_F64, .d = operations[i].real },
            false
        );
    }
    const struct
    {
        int64_t left, right, expected;
        bool invalid;
    } wide[] = {
        { INT64_C(8589934593), 3, INT64_C(2863311531), false },
        { -INT64_C(8589934593), 8, -INT64_C(1073741824), false },
        { INT64_C(8589934593), -8, -INT64_C(1073741824), false },
        { -INT64_C(8589934593), -3, INT64_C(2863311531), false },
        { 17, 0, 0, true },
        { INT64_MIN, -1, 0, true },
    };
    for (int i = 0; i < (int)(sizeof wide / sizeof *wide); i++) {
        scalar(
            world,
            entity,
            "Divide",
            (pin_value){ .type = PIN_I64, .i64 = wide[i].left },
            (pin_value){ .type = PIN_I64, .i64 = wide[i].right },
            (pin_value){ .type = PIN_I64, .i64 = wide[i].expected },
            wide[i].invalid
        );
    }

    vectors(world, entity, "Add", false, (GameTransform){ 25, -5 }, true);
    vectors(world, entity, "Subtract", false, (GameTransform){ 9, -13 }, true);
    vectors(world, entity, "Multiply", true, (GameTransform){ 136, -72 }, true);
    vectors(world, entity, "Divide", true, (GameTransform){ 2.125f, -1.125f }, true);
    vectors(world, entity, "Min", false, (GameTransform){ 8, -9 }, true);
    vectors(world, entity, "Max", false, (GameTransform){ 17, 4 }, true);
    vectors(world, entity, "Modulo", false, (GameTransform){ 1, -1 }, true);
    vectors(world, entity, "Add", true, (GameTransform){ 25, -1 }, false);
    vectors(world, entity, "Multiply", false, (GameTransform){ 136, -36 }, false);
    vectors(world, entity, "Divide", false, (GameTransform){ 2.125f, -2.25f }, false);
    ecs_world_free(world);
}
