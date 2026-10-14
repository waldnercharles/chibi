#include "execution/function_builder.h"
#include "execution/module_builder.h"
#include "execution/random.h"
#include "execution/service.h"
#include "execution/state.h"
#include "execution/status.h"
#include "runtime/engine.h"
#include "runtime/math_native.h"
#include "runtime/vectors.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    engine *engine;
    source_registry *sources;
    ecs_world *world;
    double values[32][6];
    int count, noise;
} random_world;
static random_world worlds[2];
static behavior_function_builder *builder;
static behavior_function *function;
static execution_module_plan *plan;
static execution_module *module;
static execution_program *program, *candidate;
static char error[256];
static void cleanup()
{
    behavior_random_leave(NULL);
    execution_program_free(candidate);
    candidate = NULL;
    execution_program_free(program);
    program = NULL;
    execution_module_free(module);
    module = NULL;
    execution_module_plan_free(plan);
    plan = NULL;
    behavior_function_free(function);
    function = NULL;
    if (builder) {
        behavior_function_builder_free(builder);
    }
    builder = NULL;
    for (int i = 0; i < 2; i++) {
        engine_free(worlds[i].engine);
        if (worlds[i].sources) {
            source_registry_free(worlds[i].sources);
        }
        ecs_world_free(worlds[i].world);
        worlds[i] = (random_world){ 0 };
    }
}
TEST_CASE(random_generator_matches_pcg_vectors_and_validates_native_ranges)
{
    behavior_random random = { 0 };
    behavior_random_seed(&random, 42);
    const uint32_t expected[] = { 0xa15c02b7, 0x7b47f409, 0xba1d3330,
                                  0x83d2f293, 0xbfa4784b, 0xcbed606e };
    for (int i = 0; i < 6; i++) {
        REQUIRE(behavior_random_u32(&random) == expected[i]);
    }
    behavior_random_seed(&random, 0);
    behavior_random *previous = behavior_random_enter(&random);
    for (int i = 0; i < 128; i++) {
        double unit = behavior_random_unit();
        REQUIRE(unit >= 0 && unit < 1);
        REQUIRE(behavior_random_int() >= 0);
    }
    pin_value range[] = { { .type = PIN_F64, .d = 9 }, { .type = PIN_F64, .d = -7 } };
    pin_value value = native_math_RandomRangeReal(range, 2, NULL);
    REQUIRE(value.type == PIN_F64 && value.d >= -7 && value.d <= 9);
    behavior_random before = random;
    range[0].d = NAN;
    REQUIRE(native_math_RandomRangeReal(range, 2, NULL).type == PIN_FLOW);
    REQUIRE(random.state == before.state);
    behavior_random_leave(previous);
    return true;
}
static const char *fail_native(void *, const pin_value *, pin_value *)
{
    return "intentional random scope failure";
}
static const node_host_call failing_call = { .invoke = fail_native };
static bool random_function(bool failing)
{
    builder = behavior_function_builder_new();
    pin_value outputs[] = { { .type = PIN_I32 }, { .type = PIN_F64 } };
    behavior_function_builder_signature(builder, NULL, 0, outputs, 2, NULL, 0);
    int integer = behavior_function_builder_slot(builder, PIN_I32),
        min = behavior_function_builder_slot(builder, PIN_F64),
        max = behavior_function_builder_slot(builder, PIN_F64),
        real = behavior_function_builder_slot(builder, PIN_F64);
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_RANDOM, .type = PIN_I32, .output = integer }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_F64,
                                         .output = min,
                                         .value = { .type = PIN_F64, .d = -4 } }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_F64,
                                         .output = max,
                                         .value = { .type = PIN_F64, .d = 9 } }
    );
    behavior_function_builder_emit(
        builder,
        (
            behavior_function_instruction
        ){ .op = BF_RANDOM_RANGE, .type = PIN_F64, .output = real, .left = min, .right = max }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = integer }
    );
    behavior_function_builder_emit(
        builder,
        (
            behavior_function_instruction
        ){ .op = BF_RESULT, .type = PIN_F64, .left = real, .target = 1 }
    );
    if (failing) {
        static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
        int host = behavior_function_builder_host(builder, &failing_call, &access, NULL, 0, NULL, 0);
        if (!behavior_function_builder_host_identity(builder, host, 3001, 3002)) {
            return false;
        }
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_HOST_CALL,
                                             .target = host,
                                             .left = BEHAVIOR_FUNCTION_HOST_DATA }
        );
    }
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    function = behavior_function_builder_finish(builder, error, sizeof error);
    behavior_function_builder_free(builder);
    builder = NULL;
    if (!function) {
        fprintf(stderr, "random function: %s\n", error);
    }
    return function && behavior_function_compile(function);
}
TEST_CASE(random_vm_and_native_backend_share_owned_stream_and_restore_nested_errors)
{
    REQUIRE(random_function(false));
    behavior_random reference = { 0 }, interpreted = { 0 }, compiled = { 0 },
                    noise = { 0 };
    behavior_random_seed(&reference, 42);
    behavior_random_seed(&interpreted, 42);
    behavior_random_seed(&compiled, 42);
    behavior_random_seed(&noise, 77);
    service_binding *vm_services = NULL, *jit_services = NULL;
    apush(vm_services, ((service_binding){ BEHAVIOR_RANDOM_SERVICE, &interpreted }));
    apush(jit_services, ((service_binding){ BEHAVIOR_RANDOM_SERVICE, &compiled }));
    pin_value vm_out[] = { { .type = PIN_I32 }, { .type = PIN_F64 } },
              jit_out[] = { { .type = PIN_I32 }, { .type = PIN_F64 } };
    behavior_function_context vm = { .services = vm_services,
                                     .results = vm_out,
                                     .budget = 10000 },
                              jit = { .services = jit_services,
                                      .results = jit_out,
                                      .budget = 10000 };
    for (int i = 0; i < 16; i++) {
        REQUIRE(behavior_function_run(function, &vm, false) == FLOW_OK);
        for (int j = 0; j < i; j++) {
            behavior_random_u32(&noise);
        }
        REQUIRE(behavior_function_run(function, &jit, true) == FLOW_OK);
        behavior_random *previous = behavior_random_enter(&reference);
        int integer = behavior_random_int();
        double real = behavior_random_range(-4, 9);
        behavior_random_leave(previous);
        REQUIRE(vm_out[0].i == integer && jit_out[0].i == integer);
        REQUIRE(vm_out[1].d == real && jit_out[1].d == real);
    }
    behavior_function_free(function);
    function = NULL;
    REQUIRE(random_function(true));
    behavior_random outside = { 0 }, expected = { 0 };
    behavior_random_seed(&outside, 9);
    behavior_random_seed(&expected, 9);
    behavior_random *previous = behavior_random_enter(&outside);
    REQUIRE(behavior_function_run(function, &vm, false) < 0);
    REQUIRE(vm.error_message && !strcmp(vm.error_message, "intentional random scope failure"));
    REQUIRE(behavior_random_int() == (int)(behavior_random_u32(&expected) >> 1));
    REQUIRE(behavior_function_run(function, &jit, true) < 0);
    REQUIRE(jit.error_message && !strcmp(jit.error_message, "intentional random scope failure"));
    REQUIRE(behavior_random_int() == (int)(behavior_random_u32(&expected) >> 1));
    behavior_random_leave(previous);
    afree(vm_services);
    afree(jit_services);
    return true;
}
static int sample(ecs_world *, double, double, void *user, schedule_error *)
{
    random_world *world = user;
    assert(world->count < 32);
    double *out = world->values[world->count++];
    out[0] = native_math_RandomReal(NULL, 0, NULL).d;
    pin_value range[] = { { .type = PIN_F64, .d = -5 }, { .type = PIN_F64, .d = 7 } };
    out[1] = native_math_RandomRangeReal(range, 2, NULL).d;
    CF_V3 min = { 0, -2, 2 }, max = { 1, 3, 4 };
    pin_value vector[] = { { .type = PIN_STRUCT, .type_name = "CF_V3", .ptr = &min },
                           { .type = PIN_STRUCT, .type_name = "CF_V3", .ptr = &max } };
    arena memory = { 0 };
    pin_value result = native_random_range_vector(vector, 2, &memory);
    CF_V3 value = *(CF_V3 *)result.ptr;
    out[2] = value.x;
    out[3] = value.y;
    out[4] = value.z;
    arena_free(&memory);
    char mask[] = "000";
    out[5] = behavior_state_select(mask, 3, -1, BEHAVIOR_STATE_RANDOM);
    for (int i = 0; i < world->noise; i++) {
        behavior_random_int();
    }
    return FLOW_OK;
}
static bool new_world(random_world *world, unsigned seed, int noise)
{
    world->world = ecs_world_new();
    world->sources = source_registry_new(world->world);
    world->noise = noise;
    engine_modules modules = { .sources = world->sources, .input = true };
    world->engine = engine_new(world->world, &modules);
    REQUIRE(world->engine && engine_random_stream(world->engine)->initialized);
    REQUIRE(!engine_register_system(
        world->engine,
        &(schedule_desc){ .name = "sample", .phase = SYSTEM_PRE_PHYSICS, .run = sample, .user = world }
    ));
    engine_record_input(world->engine, seed);
    REQUIRE(!engine_frame(world->engine, 0));
    return true;
}
TEST_CASE(random_interleaved_engines_match_isolated_recording_sequences)
{
    double expected_a[16][6], expected_b[16][6];
    REQUIRE(new_world(&worlds[0], 42, 0));
    for (int i = 1; i <= 16; i++) {
        REQUIRE(!engine_frame(worlds[0].engine, i * ENGINE_FIXED_SECONDS));
    }
    REQUIRE(worlds[0].count == 16);
    memcpy(expected_a, worlds[0].values, sizeof expected_a);
    cleanup();
    REQUIRE(new_world(&worlds[1], 789, 3));
    for (int i = 1; i <= 16; i++) {
        REQUIRE(!engine_frame(worlds[1].engine, i * ENGINE_FIXED_SECONDS));
    }
    REQUIRE(worlds[1].count == 16);
    memcpy(expected_b, worlds[1].values, sizeof expected_b);
    cleanup();
    REQUIRE(new_world(&worlds[0], 42, 0));
    REQUIRE(new_world(&worlds[1], 789, 3));
    for (int i = 1; i <= 16; i++) {
        REQUIRE(!engine_frame(worlds[1].engine, i * ENGINE_FIXED_SECONDS));
        REQUIRE(!engine_frame(worlds[0].engine, i * ENGINE_FIXED_SECONDS));
    }
    REQUIRE(worlds[0].count == 16 && worlds[1].count == 16);
    REQUIRE(!memcmp(expected_a, worlds[0].values, sizeof expected_a));
    REQUIRE(!memcmp(expected_b, worlds[1].values, sizeof expected_b));
    return true;
}
TEST_CASE(random_hot_replacement_forks_candidate_and_preserves_live_engine_stream)
{
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, NULL, 0, NULL, 0, NULL, 0);
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    behavior_function *body = behavior_function_builder_finish(builder, error, sizeof error);
    behavior_function_builder_free(builder);
    builder = NULL;
    REQUIRE(body);
    plan = execution_module_plan_new();
    plan->valid = true;
    plan->id = 9001;
    apush(plan->events, ((execution_module_event_declaration){ .id = 9002, .abi = 9003, .name = "Tick" }));
    plan->event_count = 1;
    apush(
        plan->functions,
        ((execution_module_function){ .id = 9004,
                                      .event = 9002,
                                      .event_abi = 9003,
                                      .kind = EXECUTION_FUNCTION_HANDLER,
                                      .body = body })
    );
    plan->function_count = 1;
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module);
    behavior_random engine_random = { 0 };
    behavior_random_seed(&engine_random, 42);
    behavior_random expected = engine_random;
    service_binding *services = NULL;
    apush(services, ((service_binding){ BEHAVIOR_RANDOM_SERVICE, &engine_random }));
    program = execution_program_new(module, NULL, services);
    afree(services);
    REQUIRE(program);
    candidate = execution_program_prepare_replace(program, module, false);
    REQUIRE(candidate);
    REQUIRE(engine_random.state == expected.state);
    execution_program_free(candidate);
    candidate = NULL;
    REQUIRE(engine_random.state == expected.state);
    candidate = execution_program_prepare_replace(program, module, false);
    REQUIRE(candidate);
    REQUIRE(behavior_random_u32(&engine_random) == behavior_random_u32(&expected));
    execution_program_commit_replace(program, candidate);
    candidate = NULL;
    REQUIRE(behavior_random_u32(&engine_random) == behavior_random_u32(&expected));
    return true;
}
void test_random_isolation()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(random_generator_matches_pcg_vectors_and_validates_native_ranges);
    RUN_TEST_CASE(random_vm_and_native_backend_share_owned_stream_and_restore_nested_errors);
    RUN_TEST_CASE(random_interleaved_engines_match_isolated_recording_sequences);
    RUN_TEST_CASE(random_hot_replacement_forks_candidate_and_preserves_live_engine_stream);
    pu_setup(NULL, NULL);
}
