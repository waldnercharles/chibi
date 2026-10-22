#include "assets/input_map.h"
#include "assets/project.h"
#include "authoring/native_bindings.h"
#include "authoring/source_catalog.h"
#include "authoring/source_minimize.h"
#include "backend.h"
#include "ecs/ecs.h"
#include "execution/delay.h"
#include "execution/inspection.h"
#include "execution/module.h"
#include "execution/prepared.h"
#include "execution/program_internal.h"
#include "execution/status.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "language/identity.h"
#include "language/lower.h"
#include "runtime/arithmetic_definition.h"
#include "runtime/arrays_definition.h"
#include "runtime/core_definition.h"
#include "runtime/native_definitions.h"
#include "runtime/state_definition.h"
#include "runtime/text_definition.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/asset_document.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/map.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* SDK fixture catalog: Position{x,y:double}, Velocity{x:double},
 * Impulse(amount:double,enabled:bool), Tick(dt:double), Delay(Duration:double),
 * self:entity and dt:double. These cases use this embedding catalog.
 * Run: make language-build && make test-language-program (selector --program).
 * Semantic type IDs intentionally differ from authoritative ECS component IDs. */
#define PROGRAM_SOURCE_CAPACITY 1024
#define MEMORY_TEXT_MIN 512
#define MEMORY_TEXT_STEP 32
#define MEMORY_TEXT_MAX 4096
#define MEMORY_PLATEAU_SLACK 4096

typedef struct
{
    double x, y;
} program_position;
typedef struct
{
    double x;
} program_velocity;
static const type_field position_fields[] = {
    { .name = "x", .type = PIN_F64, .size = sizeof(double), .type_name = "double" },
    { .name = "y",
      .type = PIN_F64,
      .offset = offsetof(program_position, y),
      .size = sizeof(double),
      .type_name = "double" },
};
static const type_field velocity_field = {
    .name = "x",
    .type = PIN_F64,
    .size = sizeof(double),
    .type_name = "double",
};
static const type_desc position_type = {
    .name = "Position",
    .size = sizeof(program_position),
    .fields = position_fields,
    .field_count = 2,
};
static const type_desc velocity_type = {
    .name = "Velocity",
    .size = sizeof(program_velocity),
    .fields = &velocity_field,
    .field_count = 1,
};
static const type_desc *const reflected_types[] = { &position_type, &velocity_type };
/* A few controls, as the input map names them. */
static const enum_value_desc control_values[] = {
    { 3, "CONTROL_SPACE", "Space" },
    { 30, "CONTROL_X", "X" },
    { 32, "CONTROL_Z", "Z" },
};
static const enum_desc control_type = { "Control", control_values, 3 };
static const enum_value_desc pad_values[] = {
    { 1, "PAD_A", "A" },
    { 12, "PAD_DPAD_LEFT", "Dpad Left" },
};
static const enum_desc pad_type = { "PadControl", pad_values, 2 };
static const enum_desc *const program_enums[] = { &control_type, &pad_type };
type_catalog_pack test_program_types()
{
    return (type_catalog_pack){ .types = reflected_types,
                                .type_count = 2,
                                .enums = program_enums,
                                .enum_count = 2 };
}
static const ecs_component_desc position_component = {
    .id = 501,
    .key = "program.position",
    .name = "Position",
    .element_size = sizeof(program_position),
    .type = &position_type,
};
static const ecs_component_desc velocity_component = {
    .id = 502,
    .key = "program.velocity",
    .name = "Velocity",
    .element_size = sizeof(program_velocity),
    .type = &velocity_type,
};
static const script_catalog_field script_position_fields[] = {
    {
        .id = 1011,
        .name = "x",
        .type = { .storage = PIN_F64 },
        .writable = true,
    },
    { .id = 1012,
      .name = "y",
      .type = { .storage = PIN_F64 },
      .writable = true,
      .offset = offsetof(program_position, y) },
};
static const script_catalog_field script_velocity_field = {
    .id = 1021,
    .name = "x",
    .type = { .storage = PIN_F64 },
    .writable = true,
};
static script_catalog_type script_types[] = {
    { .id = 101,
      .name = "Position",
      .type = { .storage = PIN_STRUCT, .nominal = 101 },
      .fields = script_position_fields,
      .field_count = 2,
      .size = sizeof(program_position),
      .component = true,
      .component_id = 501 },
    { .id = 102,
      .name = "Velocity",
      .type = { .storage = PIN_STRUCT, .nominal = 102 },
      .fields = &script_velocity_field,
      .field_count = 1,
      .size = sizeof(program_velocity),
      .component = true,
      .component_id = 502 },
};
static const script_catalog_parameter tick_parameter = {
    .name = "dt",
    .type = { .storage = PIN_F64 },
};
static const script_catalog_parameter impulse_parameters[] = {
    { .name = "amount", .type = { .storage = PIN_F64 } },
    { .name = "enabled", .type = { .storage = PIN_BOOL } },
};
static const script_catalog_parameter delay_parameter = {
    .name = "Duration",
    .type = { .storage = PIN_F64 },
};
static const char *const completed[] = { "Completed" };
static const script_catalog_parameter library_results[] = {
    { .name = "local_count", .type = { .storage = PIN_F64 } },
    { .name = "position_x", .type = { .storage = PIN_F64 } },
};
static const script_catalog_parameter bump_parameter = {
    .name = "x",
    .type = { .storage = PIN_F64, .flags = SCRIPT_TYPE_REFERENCE },
};
static const script_catalog_parameter failed_result = { .name = "Result",
                                                        .type = { .storage = PIN_F64 } };
static const script_catalog_parameter managed_result = { .name = "Result",
                                                         .type = { .storage = PIN_STR } };
static const script_catalog_capability capabilities[] = {
    { .id = 10,
      .name = "Tick",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = &tick_parameter,
      .parameter_count = 1 },
    { .id = 11,
      .name = "Impulse",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = impulse_parameters,
      .parameter_count = 2 },
    { .id = 20,
      .name = "Delay",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .parameters = &delay_parameter,
      .parameter_count = 1,
      .outcomes = completed,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_SUSPEND },
    { .id = 40,
      .name = "library_work",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .results = library_results,
      .result_count = 2,
      .outcomes = completed,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_READ | SCRIPT_EFFECT_WRITE | SCRIPT_EFFECT_SUSPEND },
    { .id = 41,
      .name = "library_bump",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = &bump_parameter,
      .parameter_count = 1,
      .outcomes = completed,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_READ | SCRIPT_EFFECT_WRITE | SCRIPT_EFFECT_SUSPEND },
    { .id = 21,
      .name = "timer",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .parameters = &delay_parameter,
      .parameter_count = 1,
      .outcomes = completed,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_SUSPEND },
    { .id = 22,
      .name = "make_thing",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .results = &failed_result,
      .result_count = 1,
      .outcomes = completed,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_SUSPEND },
    { .id = 50,
      .name = "fail_native",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .results = &failed_result,
      .result_count = 1,
      .effects = SCRIPT_EFFECT_HOST },
    { .id = 60,
      .name = "managed_text",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .results = &managed_result,
      .result_count = 1,
      .effects = SCRIPT_EFFECT_HOST },
    { .id = 63,
      .name = "capture_text",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .results = &managed_result,
      .result_count = 1,
      .effects = SCRIPT_EFFECT_HOST },
};
static const script_catalog_global globals[] = {
    { .id = 30, .name = "self", .type = { .storage = PIN_HANDLE, .nominal = 700 } },
    { .id = 31, .name = "dt", .type = { .storage = PIN_F64 } },
};
static const script_catalog catalog = {
    .types = script_types,
    .type_count = 2,
    .capabilities = capabilities,
    .capability_count = sizeof capabilities / sizeof *capabilities,
    .globals = globals,
    .global_count = 2,
    .entity_type = 700,
    .tick_event = 10,
};
static ecs_world *world;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module *module;
static execution_module_plan *owned_plan;
static execution_program *program;
static execution_module *imported;
static execution_module *prior_module;
static execution_program *other_program;
static bool program_prepared_type(void *, uint64_t, execution_prepared_type *);
static native_capabilities *state_native;
static execution_inspector *debugger;
static behavior_function_context parser_context;
static behavior_function_activation parser_activation;
static arena parser_storage;

static void cleanup()
{
    behavior_function_release_host(&parser_context);
    behavior_function_activation_free(&parser_activation);
    parser_activation = (behavior_function_activation){ 0 };
    parser_context = (behavior_function_context){ 0 };
    arena_free(&parser_storage);
    execution_program_free(other_program);
    execution_program_free(program);
    execution_inspector_free(debugger);
    debugger = NULL;
    execution_module_free(module);
    execution_module_free(prior_module);
    execution_module_plan_free(owned_plan);
    execution_module_free(imported);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    ecs_world_free(world);
    native_capabilities_free(state_native);
    state_native = NULL;
    program = NULL;
    module = NULL;
    owned_plan = NULL;
    analysis = NULL;
    syntax = NULL;
    world = NULL;
    imported = NULL;
    prior_module = NULL;
    other_program = NULL;
}
static void prepare()
{
    cleanup();
    world = ecs_world_new();
    assert(ecs_world_register_component(world, &position_component, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &velocity_component, NULL) == ECS_OK);
    script_types[0].layout = type_layout_hash(&position_type);
    script_types[1].layout = type_layout_hash(&velocity_type);
}
static bool global(void *, uint64_t id, behavior_function_instruction *out)
{
    if (id == 30) {
        *out = (behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE };
        return true;
    }
    if (id == 31) {
        *out = (behavior_function_instruction){
            .op = BF_CONTEXT,
            .type = PIN_F64,
            .offset = offsetof(behavior_function_context, delta_seconds),
        };
        return true;
    }
    return false;
}
static char failure_message[64];
static const char *fail_native(arena *, const pin_value *, int, pin_value *)
{
    return failure_message;
}
static const node_host_call failing_call = { .value = fail_native };
static int managed_text_calls;
static const char *managed_text(arena *storage, const pin_value *, int count, pin_value *out)
{
    assert(count == 0);
    static char text[MEMORY_TEXT_MAX + 1];
    int index = managed_text_calls++;
    int size = MEMORY_TEXT_MIN + index * MEMORY_TEXT_STEP;
    assert(size <= MEMORY_TEXT_MAX);
    memset(text, 'x', (size_t)size);
    text[0] = (char)('A' + index % 26);
    text[size] = 0;
    const pin_value value = { .type = PIN_STR, .str = text };
    return pin_value_snapshot_to_arena(storage, out, &value)
             ? "managed text snapshot failed"
             : NULL;
}
static const node_host_call managed_call = { .value = managed_text };
#define CAPTURE_TEXT_BYTES 512
#define CAPTURE_WRITES 32
#define CAPTURE_KEEP_UPDATES 40
#define CAPTURE_STORAGE_LIMIT ((size_t)256 * 1024)
static int capture_text_calls;
static const char *capture_text(arena *storage, const pin_value *, int count, pin_value *out)
{
    assert(count == 0 && capture_text_calls < CAPTURE_WRITES + 2);
    char text[CAPTURE_TEXT_BYTES + 1];
    memset(text, 'x', CAPTURE_TEXT_BYTES);
    text[0] = (char)('A' + capture_text_calls / 26);
    text[1] = (char)('a' + capture_text_calls % 26);
    text[CAPTURE_TEXT_BYTES] = 0;
    capture_text_calls++;
    const pin_value value = { .type = PIN_STR, .str = text };
    return pin_value_snapshot_to_arena(storage, out, &value)
             ? "capture text snapshot failed"
             : NULL;
}
static const node_host_call capture_call = { .value = capture_text };
static const char *instance_clock(
    const behavior_function_context *context,
    void *,
    const pin_value *,
    pin_value *out
)
{
    assert(context->instance_time);
    *out = (pin_value){ .type = PIN_F64, .d = *context->instance_time };
    return NULL;
}
static const node_host_call clock_call = { .context = instance_clock };
static bool jump_held, jump_pressed;
static const char *action_held(arena *, const pin_value *in, int count, pin_value *out)
{
    assert(count == 1 && !strcmp(in[0].str, "Jump"));
    *out = (pin_value){ .type = PIN_BOOL, .b = jump_held };
    return NULL;
}
static const char *action_pressed(arena *, const pin_value *in, int count, pin_value *out)
{
    assert(count == 1 && !strcmp(in[0].str, "Jump"));
    *out = (pin_value){ .type = PIN_BOOL, .b = jump_pressed };
    return NULL;
}
static const node_host_call held_call = { .value = action_held };
static const node_host_call pressed_call = { .value = action_pressed };
static ecs_entity bound_level;
static bool binding_available;
static const char *scene_binding(arena *, const pin_value *in, int count, pin_value *out)
{
    assert(count == 1 && !strcmp(in[0].str, "Level"));
    if (!binding_available) {
        return "Get Global Variable references an unknown value";
    }
    out->u64 = ecs_entity_to_u64(bound_level);
    return NULL;
}
static const node_host_call scene_binding_call = { .value = scene_binding };
static bool host(void *, uint64_t id, script_lower_host *out)
{
    if (id == 61) {
        *out = (script_lower_host){ .call = &clock_call };
        return true;
    }
    if (id == script_catalog_id("capability", "Get Global Variable")) {
        *out = (script_lower_host){ .call = &scene_binding_call };
        return true;
    }
    if (id == script_catalog_id("capability", INPUT_ACTION_HELD) ||
        id == script_catalog_id("capability", INPUT_ACTION_PRESSED)) {
        bool held = id == script_catalog_id("capability", INPUT_ACTION_HELD);
        *out = (script_lower_host){ .call = held ? &held_call : &pressed_call };
        return true;
    }
    if (id == 60) {
        *out = (script_lower_host){ .call = &managed_call };
        return true;
    }
    if (id == 21) {
        *out = (script_lower_host){ .command = &EXECUTION_Delay };
        return true;
    }
    if (id == 63) {
        *out = (script_lower_host){ .call = &capture_call };
        return true;
    }
    if (id == 50) {
        *out = (script_lower_host){ .call = &failing_call };
        return true;
    }
    if ((id == 40 || id == 41) && imported) {
        for (int i = 0; i < execution_module_function_count(imported); i++) {
            execution_module_function_info function = execution_module_function_at(imported, i);
            if (function.kind == EXECUTION_FUNCTION_AUTHORED) {
                *out = (script_lower_host){ .function = function.body,
                                            .module = imported };
                return true;
            }
        }
        return false;
    }
    if (id != 20) {
        return false;
    }
    *out = (script_lower_host){ .intrinsic = BF_DELAY };
    return true;
}
/* A pack's own natives, then the fixture's hosts. */
static bool pack_host(void *user, uint64_t id, script_lower_host *out)
{
    script_lower_bindings natives = authoring_native_bindings(state_native);
    return natives.host(natives.user, id, out) || host(user, id, out);
}
static bool compile_catalog(const char *source, behavior_backend backend, const script_catalog *active)
{
    syntax = script_parse(source);
    analysis = script_analyze(syntax, active);
    if (!analysis->valid) {
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            fprintf(stderr, "program semantic: %s\n", analysis->diagnostics[i].message);
        }
        return false;
    }
    const script_lower_bindings bindings = { .global = global,
                                             .host = state_native ? pack_host : host };
    execution_module_plan *plan = script_lower(analysis, active, &bindings);
    if (!plan || !plan->valid) {
        for (int i = 0; plan && i < plan->diagnostic_count; i++) {
            fprintf(
                stderr,
                "program lower %d:%d: %s\n",
                plan->diagnostics[i].range.line,
                plan->diagnostics[i].range.column,
                plan->diagnostics[i].message
            );
        }
        execution_module_plan_free(plan);
        return false;
    }
    char error[256];
    module = execution_module_finish(&plan, backend, error, sizeof error);
    execution_module_plan_free(plan);
    int status = module ? FLOW_OK : FLOW_ERR_COMPILE;
    if (status != FLOW_OK) {
        fprintf(stderr, "program publish: %s\n", error);
    }
    if (status != FLOW_OK) {
        return false;
    }
    return true;
}
static bool compile_module(const char *source, behavior_backend backend)
{
    return compile_catalog(source, backend, &catalog);
}
static bool compile_pack(const char *source, behavior_backend backend, const native_definition_pack *pack)
{
    const native_capability_pack base = {
        .catalog = &catalog,
        .layouts = reflected_types,
        .layout_count = 2,
    };
    char error[256];
    const native_definition_pack packs[] = { *pack, CORE_DEFINITION_PACK };
    state_native = native_capabilities_from_definition_packs(&base, 1, packs, 2, error, sizeof error);
    if (!state_native) {
        fprintf(stderr, "state catalog: %s\n", error);
        return false;
    }
    if (!compile_catalog(source, backend, native_capabilities_catalog(state_native))) {
        return false;
    }
    program = execution_program_new(module, world, NULL);
    return program != NULL;
}
/* Whether lowering source against a definition pack reports message. */
static bool lowering_reports(const char *source, const native_definition_pack *pack, const char *message)
{
    const native_capability_pack base = {
        .catalog = &catalog,
        .layouts = reflected_types,
        .layout_count = 2,
    };
    char error[256];
    const native_definition_pack packs[] = { *pack, CORE_DEFINITION_PACK };
    state_native = native_capabilities_from_definition_packs(&base, 1, packs, 2, error, sizeof error);
    syntax = script_parse(source);
    analysis = script_analyze(syntax, native_capabilities_catalog(state_native));
    const script_lower_bindings bindings = { .global = global, .host = pack_host };
    execution_module_plan
        *plan = script_lower(analysis, native_capabilities_catalog(state_native), &bindings);
    bool found = false;
    for (int i = 0; i < plan->diagnostic_count; i++) {
        found |= !strcmp(plan->diagnostics[i].message, message);
    }
    execution_module_plan_free(plan);
    return found;
}
static bool compile(const char *source, behavior_backend backend)
{
    if (!compile_module(source, backend)) {
        return false;
    }
    program = execution_program_new(module, world, NULL);
    return program != NULL;
}
static uint64_t state_id(const char *name)
{
    for (int i = 0; i < analysis->symbol_count; i++) {
        if ((analysis->symbols[i].kind == SCRIPT_STATE ||
             analysis->symbols[i].kind == SCRIPT_SHARED_STATE) &&
            !strcmp(analysis->symbols[i].name, name)) {
            return analysis->symbols[i].id;
        }
    }
    return 0;
}
static bool number_state(execution_instance *instance, const char *name, double expected)
{
    const pin_value *value = execution_instance_state(instance, state_id(name));
    return value && value->type == PIN_F64 && value->d == expected;
}
static ecs_entity positioned(double x, bool moving)
{
    ecs_entity entity = ecs_world_create_entity(world);
    program_position position = { .x = x };
    assert(ecs_world_record_add(world, entity, 501, &position, 0) == ECS_OK);
    if (moving) {
        program_velocity velocity = { .x = 4 };
        assert(ecs_world_record_add(world, entity, 502, &velocity, 0) == ECS_OK);
    }
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return entity;
}

TEST_CASE(program_nested_typed_records_preserve_explicit_labels)
{
    const char
        *source = "Parser :: library { Package :: struct { directory: string; "
                  "name: string; "
                  "member_start: int; member_count: int; }\n"
                  "Main :: (enabled: bool) -> int {\n"
                  " result: int = 0;\n"
                  " if enabled {\n"
                  "  item: Package = {directory: \"app\", name: \"sample\",\n"
                  "                   member_start: 3, member_count: 4};\n"
                  "  result = item.member_start + item.member_count;\n"
                  " }\n"
                  " if true {\n"
                  " ordinary:\n"
                  " result = result + 1;\n"
                  " compound: result += 1;\n"
                  " }\n"
                  " return result;\n"
                  "} }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_module(source, backends[b]));
        REQUIRE(execution_module_function_count(module) == 1);
        const behavior_function *function = execution_module_function_at(module, 0)
                                                .body;
        REQUIRE(function);
        for (int enabled = 0; enabled < 2; enabled++) {
            behavior_function_release_host(&parser_context);
            behavior_function_activation_free(&parser_activation);
            parser_activation = (behavior_function_activation){ 0 };
            arena_free(&parser_storage);
            if (behavior_function_resumable(function)) {
                behavior_function_activation_init(function, &parser_activation);
            }
            pin_value argument = { .type = PIN_BOOL, .b = enabled },
                      result = { .type = PIN_I32 };
            parser_context = (behavior_function_context){
                .activation = behavior_function_resumable(function) ? &parser_activation
                                                                    : NULL,
                .arguments = &argument,
                .argument_count = 1,
                .results = &result,
                .budget = 1000
            };
            behavior_function_bind_storage(&parser_context, &parser_storage);
            REQUIRE(behavior_function_bind(function, &parser_context, NULL, 0));
            behavior_run_scope runs = { 0 };
            behavior_run_scope_begin(&runs);
            int status = behavior_function_run(function, &parser_context, b != 0);
            behavior_run_scope_end(&runs);
            REQUIRE(status == FLOW_OK && parser_context.returned);
            REQUIRE(result.type == PIN_I32 && result.i == (enabled ? 9 : 2));
            REQUIRE(!runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks);
            REQUIRE(b ? !runs.counts.bytecode_entries : runs.counts.bytecode_entries > 0);
            REQUIRE(backends[b] != BEHAVIOR_SLJIT || runs.counts.sljit_entries > 0);
            REQUIRE(backends[b] != BEHAVIOR_WASM || runs.counts.wasm_entries > 0);
        }
    }
    return true;
}

/* `on X :: (a: T)` names payload fields in any order and any subset; the
 * fields it leaves out keep their slots, so each name reads its own field. */
TEST_CASE(program_handlers_name_payload_fields)
{
    const char *sources[] = {
        "A :: behavior { total: double = 0;\n"
        "    on Impulse :: (enabled: bool, amount: double) { if (enabled) { "
        "total += amount; } } }",
        "A :: behavior { total: double = 0;\n"
        "    on Impulse :: (enabled: bool) { if (enabled) { total += 7; } } }",
        "A :: behavior { total: double = 0;\n"
        "    on Impulse :: (Amount: double) { total += Amount; } }",
    };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int s = 0; s < 3; s++) {
        for (int i = 0; i < 2; i++) {
            prepare();
            REQUIRE(compile(sources[s], backends[i]));
            ecs_entity owner = ecs_world_create_entity(world);
            uint64_t handle = ecs_entity_to_u64(owner);
            execution_instance *instance = execution_program_attach(program, handle);
            REQUIRE(instance && state_id("total"));
            pin_value payload[] = { { .type = PIN_F64, .d = 7 },
                                    { .type = PIN_BOOL, .b = true } };
            REQUIRE(execution_instance_dispatch(instance, 11, payload, 2, 1, 0, 1000) == FLOW_OK);
            REQUIRE(execution_instance_state(instance, state_id("total"))->d == 7);
        }
    }
    const struct
    {
        const char *source, *message;
    } refused[] = {
        { "A :: behavior { on Impulse :: (force: double) { } }",
          "Impulse has no payload field 'force'" },
        { "A :: behavior { on Impulse :: (amount: double, Amount: double) { } "
          "}",
          "listed twice" },
        { "A :: behavior { on Impulse :: (amount) { } }",
          "Write the type of payload field 'amount'" },
        { "A :: behavior { on Impulse :: (amount: bool) { } }", "" },
        { "A :: behavior { on Impulse :: (amount: double = 1) { } }", "takes no default" },
        { "A :: behavior { x: double = 0; on Impulse :: () { x = amount; } }", "" },
        { "A :: behavior { x: double = 0; on Impulse :: () { x = event.amount; "
          "} }",
          "list the field instead" },
        { "M :: system(p: Position) { on Impulse :: (enabled: bool) { p.x = "
          "event.dt; } }",
          "list the field instead" },
    };
    for (int i = 0; i < 8; i++) {
        prepare();
        syntax = script_parse(refused[i].source);
        REQUIRE(!syntax->diagnostic_count);
        analysis = script_analyze(syntax, &catalog);
        bool found = false;
        for (int d = 0; d < analysis->diagnostic_count; d++) {
            found |= strstr(analysis->diagnostics[d].message, refused[i].message) != NULL;
        }
        REQUIRE(!analysis->valid && found);
    }
    return true;
}
TEST_CASE(program_typed_event_changes_state_on_vm_and_sljit)
{
    const char *source = "Accumulator :: behavior {\n"
                         "    total: double = 0;\n"
                         "    seen: bool = false;\n"
                         "\n"
                         "    on Impulse :: (amount: double, enabled: bool) {\n"
                         "        if (enabled) {\n"
                         "            total += amount;\n"
                         "            seen = true;\n"
                         "        }\n"
                         "    }\n"
                         "}\n";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int i = 0; i < 2; i++) {
        prepare();
        REQUIRE(compile(source, backends[i]));
        ecs_entity owner = ecs_world_create_entity(world);
        uint64_t handle = ecs_entity_to_u64(owner);
        execution_instance *instance = execution_program_attach(program, handle);
        REQUIRE(instance != NULL && state_id("total") != 0);
        pin_value payload[] = { { .type = PIN_F64, .d = 7 },
                                { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_program_emit(program, 11, handle, payload, 2) == FLOW_OK);
        payload[0].d = 99;
        REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
        const pin_value *total = execution_instance_state(instance, state_id("total"));
        const pin_value *seen = execution_instance_state(instance, state_id("seen"));
        REQUIRE(total && total->type == PIN_F64 && total->d == 7);
        REQUIRE(seen && seen->type == PIN_BOOL && seen->b);
        payload[0].d = 3;
        payload[1].b = false;
        REQUIRE(execution_instance_dispatch(instance, 11, payload, 2, 1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_instance_state(instance, state_id("total"))->d == 7);
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}

/* A system names the fields of its step's payload in its header, as a
 * handler does; only its body reads them, and it has no `event`. */
TEST_CASE(program_systems_name_their_step)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        ecs_entity moving = positioned(1, true);
        REQUIRE(compile(
            "Motion :: system(position: Position, velocity: Velocity) on Tick "
            ":: (dt: double) { position.x += velocity.x * dt; }",
            backends[backend]
        ));
        REQUIRE(execution_program_step(program, 0, .5, 1000) == FLOW_OK);
        const program_position *position = ecs_component_data_const(world, moving, 501);
        REQUIRE(position && position->x == 3);
    }
    const struct
    {
        const char *source, *message;
    } refused[] = {
        { "Motion :: system(p: Position) on Impulse :: (amount: double) { p.x "
          "+= amount; }",
          "A system runs on Tick" },
        { "Motion :: system(p: Position) on Tick :: (dt: double) { p.x += "
          "event.dt; }",
          "Unknown value" },
        { "Motion :: system(p: Position) on Tick :: (dt: double) { Step :: () "
          "-> double { return dt; } p.x += Step(); }",
          "Read the step in the system's body" },
        { "Motion :: system(p: Position) on Tick :: (step: double) { p.x += 1; "
          "}",
          "Tick has no payload field 'step'" },
        { "Motion :: system(p: Position) on Tick :: (dt: double) {\n"
          "Step :: () { return dt; }\np.x += Step(); }",
          "Read the step in the system's body" },
        { "Motion :: system(p: Position) on Tick :: (dt: double) {\n"
          "on Impulse :: (amount: double) { p.x += dt; } }",
          "Read the step in the system's body" },
        { "Motion :: system(p: Position) on Tick :: () { p.x += 1; }",
          "List the step's fields" },
        { "Motion :: system(p: Position) on Tick :: (dt: double) {\n"
          "bind self.Position.x = dt;\np.x += 1; }",
          "Read the step in the system's body" },
        { "Motion :: system(p: Position) { p.x += event.dt; }", "Unknown value 'event'" },
        { "Motion :: system(p: Position) where dt > 0 on Tick :: (dt: double) "
          "{ p.x += dt; }",
          "Query predicate cannot read" },
    };
    for (int i = 0; i < 10; i++) {
        prepare();
        syntax = script_parse(refused[i].source);
        REQUIRE(!syntax->diagnostic_count);
        analysis = script_analyze(syntax, &catalog);
        bool found = false, inner = false;
        for (int d = 0; d < analysis->diagnostic_count; d++) {
            found |= strstr(analysis->diagnostics[d].message, refused[i].message) != NULL;
            inner |= strstr(analysis->diagnostics[d].message, "Read the step") != NULL;
        }
        REQUIRE(!analysis->valid && found);
        /* A query reads no callable's step; it says so once. */
        REQUIRE(i != 9 || !inner);
    }
    /* Lowering reads Tick's one field as the step, so a Tick with more
     * fields has no system step. */
    script_catalog_capability wider[64];
    REQUIRE(catalog.capability_count <= 64);
    memcpy(wider, catalog.capabilities, (size_t)catalog.capability_count * sizeof *wider);
    for (int i = 0; i < catalog.capability_count; i++) {
        if (wider[i].id == catalog.tick_event) {
            wider[i].parameters = impulse_parameters;
            wider[i].parameter_count = 2;
        }
    }
    script_catalog two = catalog;
    two.capabilities = wider;
    prepare();
    syntax = script_parse("Motion :: system(p: Position) on Tick :: (amount: double) { p.x += amount; }");
    analysis = script_analyze(syntax, &two);
    bool step = false;
    for (int d = 0; d < analysis->diagnostic_count; d++) {
        step |= strstr(analysis->diagnostics[d].message, "reads only the step") != NULL;
    }
    REQUIRE(!analysis->valid && step);
    return true;
}
TEST_CASE(program_query_owner_snapshots_preserve_parallel_effects)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        ecs_entity moving = positioned(1, true);
        REQUIRE(compile(
            "Motion :: system( position: Position, velocity: Velocity) on Tick "
            ":: (dt: float s) { position.x += velocity.x * dt; position.y = "
            "position.x + velocity.x; }",
            backends[backend]
        ));
        execution_effect_summary effects = execution_module_effects(module);
        REQUIRE(effects.declared && effects.parallel_safe && effects.deterministic);
        REQUIRE(!effects.structural && !effects.host_sink && !effects.world_read);
        bool reads_position = false, reads_velocity = false;
        for (int i = 0; i < effects.read_count; i++) {
            REQUIRE(effects.reads[i] == 501 || effects.reads[i] == 502);
            reads_position |= effects.reads[i] == 501;
            reads_velocity |= effects.reads[i] == 502;
        }
        REQUIRE(reads_position && reads_velocity && effects.write_count > 0);
        for (int i = 0; i < effects.write_count; i++) {
            REQUIRE(effects.writes[i] == 501);
        }
        REQUIRE(execution_program_step(program, 0, .5, 1000) == FLOW_OK);
        const program_position *position = ecs_component_data_const(world, moving, 501);
        REQUIRE(position && position->x == 3 && position->y == 7);
    }
    return true;
}

TEST_CASE(program_system_membership_exit_cancels_pending_work)
{
    const char *source = "Motion :: system(position: Position, velocity: "
                         "Velocity) on Tick :: (dt: float s) {\n"
                         "    position.x += velocity.x * dt;\n"
                         "    await Delay(10);\n"
                         "    position.y = 99;\n"
                         "}\n";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int i = 0; i < 2; i++) {
        prepare();
        ecs_entity moving = positioned(1, true), stationary = positioned(10, false);
        REQUIRE(compile(source, backends[i]));
        REQUIRE(execution_program_step(program, 0, 0.5, 1000) == FLOW_OK);
        const program_position *position = ecs_component_data_const(world, moving, 501);
        REQUIRE(position && position->x == 3 && position->y == 0);
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, stationary, 501))
                ->x == 10
        );
        execution_instance *instance = execution_program_instance(program, ecs_entity_to_u64(moving));
        REQUIRE(instance && execution_instance_pending(instance) == 1);
        REQUIRE(!execution_program_instance(program, ecs_entity_to_u64(stationary)));
        REQUIRE(ecs_world_record_remove(world, moving, 502, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 20, 0, 1000) == FLOW_OK);
        REQUIRE(!execution_program_instance(program, ecs_entity_to_u64(moving)));
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, moving, 501))
                ->y == 0
        );
    }
    return true;
}

TEST_CASE(program_query_sources_resolve_current_targets_and_nearest_ancestors)
{
    REQUIRE(SCRIPT_CHILD_OF == ECS_CHILD_OF);
    const script_catalog_relationship relationships[] = {
        { .id = 601, .name = "Targets", .exclusive = true },
    };
    const ecs_relationship_desc targets = { .id = 601,
                                            .key = "program.targets",
                                            .name = "Targets",
                                            .traits = ECS_REL_EXCLUSIVE };
    script_catalog active = catalog;
    active.relationships = relationships;
    active.relationship_count = 1;
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        const native_capability_pack pack = { .catalog = &active,
                                              .layouts = reflected_types,
                                              .layout_count = 2 };
        char error[256];
        state_native = native_capabilities_new(&pack, 1, error, sizeof error);
        REQUIRE(state_native);
        REQUIRE(native_capabilities_catalog(state_native)->relationship_count == 1);
        REQUIRE(ecs_world_register_relationship(world, &targets, NULL) == ECS_OK);
        ecs_entity root = positioned(10, true), middle = positioned(20, true),
                   child = positioned(1, false), orphan = positioned(2, false);
        REQUIRE(ecs_world_record_add_pair(world, middle, ECS_CHILD_OF, root, 0) == ECS_OK);
        REQUIRE(ecs_world_record_add_pair(world, child, ECS_CHILD_OF, middle, 0) == ECS_OK);
        REQUIRE(ecs_world_record_add_pair(world, child, targets.id, root, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(compile_catalog(
            "Follow :: system( p: Position, aim: Targets.Position?, team: "
            "Position from ancestor(ChildOf)) where not Velocity { p.x = "
            "team.x; if aim { p.y = aim.x; } }",
            backends[backend],
            native_capabilities_catalog(state_native)
        ));
        REQUIRE(!execution_module_effects(module).parallel_safe);
        unsigned char *bytes = NULL;
        size_t length = 0;
        REQUIRE(execution_prepared_write(module, &bytes, &length, error, sizeof error));
        const execution_prepared_linker linker = { .type = program_prepared_type };
        execution_module *loaded = execution_prepared_read(
            bytes,
            length,
            &linker,
            backends[backend],
            error,
            sizeof error
        );
        free(bytes);
        if (!loaded) {
            fprintf(stderr, "prepared query: %s\n", error);
        }
        REQUIRE(loaded);
        execution_module_free(module);
        module = loaded;
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
        const program_position *row = ecs_component_data_const(world, child, 501);
        REQUIRE(row && row->x == 20 && row->y == 10);
        REQUIRE(!execution_program_instance(program, ecs_entity_to_u64(orphan)));
        REQUIRE(!execution_program_instance(program, ecs_entity_to_u64(middle)));
        REQUIRE(ecs_world_record_add_pair(world, child, targets.id, middle, 0) == ECS_OK);
        REQUIRE(ecs_world_record_remove(world, middle, 501, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 1, 0, 1000) == FLOW_OK);
        row = ecs_component_data_const(world, child, 501);
        REQUIRE(row && row->x == 10 && row->y == 10);
        REQUIRE(ecs_world_record_remove(world, root, 501, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(!execution_program_instance(program, ecs_entity_to_u64(child)));
    }
    return true;
}

TEST_CASE(program_query_required_path_keeps_one_instance_per_root)
{
    const script_catalog_relationship relationships[] = {
        { .id = 601, .name = "Targets", .exclusive = true },
    };
    const ecs_relationship_desc targets = { .id = 601,
                                            .key = "program.targets",
                                            .name = "Targets",
                                            .traits = ECS_REL_EXCLUSIVE };
    script_catalog active = catalog;
    active.relationships = relationships;
    active.relationship_count = 1;
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        REQUIRE(ecs_world_register_relationship(world, &targets, NULL) == ECS_OK);
        ecs_entity target = positioned(10, true), first = positioned(1, false),
                   second = positioned(2, false), missing = positioned(3, false);
        REQUIRE(ecs_world_record_add_pair(world, first, targets.id, target, 0) == ECS_OK);
        REQUIRE(ecs_world_record_add_pair(world, second, targets.id, target, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(compile_catalog(
            "Follow :: system( p: Position, aim: Targets.Position) where not "
            "self.Velocity { p.x += 1; aim.y += 1; }",
            backends[backend],
            &active
        ));
        REQUIRE(!execution_module_effects(module).parallel_safe);
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, target, 501))
                ->y == 2
        );
        REQUIRE(!execution_program_instance(program, ecs_entity_to_u64(missing)));
        REQUIRE(execution_program_step(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, first, 501))
                ->x == 3
        );
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, second, 501))
                ->x == 4
        );
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, target, 501))
                ->y == 4
        );
    }
    return true;
}

TEST_CASE(program_query_joins_filter_targets_and_refresh_across_suspension)
{
    const script_catalog_relationship relationships[] = {
        { .id = 601, .name = "Targets", .exclusive = true },
        { .id = 602, .name = "Likes" },
    };
    const ecs_relationship_desc targets = { .id = 601,
                                            .key = "program.targets",
                                            .name = "Targets",
                                            .traits = ECS_REL_EXCLUSIVE };
    const ecs_relationship_desc likes = { .id = 602, .key = "program.likes", .name = "Likes" };
    script_catalog active = catalog;
    active.relationships = relationships;
    active.relationship_count = 2;
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        REQUIRE(ecs_world_register_relationship(world, &targets, NULL) == ECS_OK);
        ecs_entity first = positioned(10, true), second = positioned(20, true),
                   root = positioned(1, false), missing = positioned(2, false);
        REQUIRE(ecs_world_record_add_pair(world, root, targets.id, first, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(compile_catalog(
            "Follow :: system( p: Position from self) join aim: "
            "Targets.Position where not self.Velocity and Targets.Velocity and "
            "has(Likes) { count: retain double = 0; count += 1; p.x = count; "
            "await Delay(1); p.y = aim.x; }",
            backends[backend],
            &active
        ));
        REQUIRE(!execution_module_effects(module).parallel_safe);
        unsigned char *bytes = NULL;
        size_t length = 0;
        char error[256];
        REQUIRE(execution_prepared_write(module, &bytes, &length, error, sizeof error));
        const execution_prepared_linker linker = { .type = program_prepared_type };
        execution_module *loaded = execution_prepared_read(
            bytes,
            length,
            &linker,
            backends[backend],
            error,
            sizeof error
        );
        free(bytes);
        REQUIRE(loaded);
        execution_module_free(module);
        module = loaded;
        REQUIRE(!execution_program_new(module, world, NULL));
        REQUIRE(ecs_world_register_relationship(world, &likes, NULL) == ECS_OK);
        REQUIRE(ecs_world_record_add_pair(world, root, likes.id, first, 0) == ECS_OK);
        REQUIRE(ecs_world_record_add_pair(world, root, likes.id, second, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
        const program_position *row = ecs_component_data_const(world, root, 501);
        REQUIRE(row && row->x == 1 && row->y == 0);
        REQUIRE(!execution_program_instance(program, ecs_entity_to_u64(missing)));
        REQUIRE(ecs_world_record_add_pair(world, root, targets.id, second, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 1, 0, 1000) == FLOW_OK);
        row = ecs_component_data_const(world, root, 501);
        REQUIRE(row && row->x == 2 && row->y == 20);
        REQUIRE(ecs_world_record_remove(world, second, 502, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(!execution_program_instance(program, ecs_entity_to_u64(root)));
        row = ecs_component_data_const(world, root, 501);
        REQUIRE(row && row->x == 2 && row->y == 20);
        REQUIRE(ecs_world_record_add_pair(world, root, targets.id, first, 0) == ECS_OK);
        REQUIRE(ecs_world_record_remove_pair(world, root, likes.id, first, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 3, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_instance(program, ecs_entity_to_u64(root)));
        row = ecs_component_data_const(world, root, 501);
        /* The query row was removed, but the registration's retained counter
         * survives the empty query and advances on re-entry. */
        REQUIRE(row && row->x == 3);
        REQUIRE(
            number_state(execution_program_instance(program, ecs_entity_to_u64(root)), "count", 3)
        );
        REQUIRE(ecs_world_record_remove_pair(world, root, likes.id, second, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 4, 0, 1000) == FLOW_OK);
        REQUIRE(!execution_program_instance(program, ecs_entity_to_u64(root)));
    }
    return true;
}

TEST_CASE(program_query_optional_joins_preserve_missing_target_exclusions)
{
    const script_catalog_relationship relationships[] = {
        { .id = 601, .name = "Targets", .exclusive = true },
    };
    const ecs_relationship_desc targets = { .id = 601,
                                            .key = "program.targets",
                                            .name = "Targets",
                                            .traits = ECS_REL_EXCLUSIVE };
    script_catalog active = catalog;
    active.relationships = relationships;
    active.relationship_count = 1;
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        REQUIRE(ecs_world_register_relationship(world, &targets, NULL) == ECS_OK);
        ecs_entity target = positioned(10, true), filtered = positioned(1, false),
                   missing = positioned(2, false);
        REQUIRE(ecs_world_record_add_pair(world, filtered, targets.id, target, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(compile_catalog(
            "Follow :: system( p: Position) join aim: Targets.Position? where "
            "not self.Velocity and not Targets.Velocity { p.y += 1; if aim { "
            "p.x = aim.x; } }",
            backends[backend],
            &active
        ));
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
        REQUIRE(!execution_program_instance(program, ecs_entity_to_u64(filtered)));
        const program_position *row = ecs_component_data_const(world, missing, 501);
        REQUIRE(row && row->x == 2 && row->y == 1);
        REQUIRE(ecs_world_record_remove(world, target, 502, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 1, 0, 1000) == FLOW_OK);
        row = ecs_component_data_const(world, filtered, 501);
        REQUIRE(row && row->x == 10 && row->y == 1);
        REQUIRE(ecs_world_record_remove(world, target, 501, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 2, 0, 1000) == FLOW_OK);
        row = ecs_component_data_const(world, filtered, 501);
        REQUIRE(row && row->x == 10 && row->y == 2);
    }
    return true;
}

TEST_CASE(program_relationship_loops_stream_targets_explicitly)
{
    const script_catalog_relationship relationships[] = { { .id = 601, .name = "Targets" } };
    const ecs_relationship_desc targets = { .id = 601,
                                            .key = "program.targets",
                                            .name = "Targets" };
    script_catalog active = catalog;
    active.relationships = relationships;
    active.relationship_count = 1;
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        REQUIRE(ecs_world_register_relationship(world, &targets, NULL) == ECS_OK);
        ecs_entity source = positioned(0, false), a = positioned(2, true),
                   b = positioned(3, true), empty = ecs_world_create_entity(world);
        REQUIRE(ecs_world_record_add_pair(world, source, targets.id, a, 0) == ECS_OK);
        REQUIRE(ecs_world_record_add_pair(world, source, targets.id, b, 0) == ECS_OK);
        REQUIRE(ecs_world_record_add_pair(world, source, targets.id, empty, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(compile_catalog(
            "Sum :: system( p: Position) where not Velocity { ticks: retain "
            "double = 0; ticks += 1; p.y = ticks; p.x = 0; for target in "
            "self.Targets { if not target.Position { continue; } p.x += "
            "target.Position.x; } }",
            backends[backend],
            &active
        ));
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
        const program_position *row = ecs_component_data_const(world, source, 501);
        REQUIRE(row && row->x == 5 && row->y == 1);
        REQUIRE(execution_program_step(program, 1, 0, 1000) == FLOW_OK);
        row = ecs_component_data_const(world, source, 501);
        REQUIRE(row && row->x == 5 && row->y == 2);
    }
    /* Unlike the system above, this handler has no component column access to
     * incidentally bind its world. Streaming alone must require that service. */
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        REQUIRE(ecs_world_register_relationship(world, &targets, NULL) == ECS_OK);
        ecs_entity source = ecs_world_create_entity(world);
        ecs_entity target = ecs_world_create_entity(world);
        REQUIRE(ecs_world_record_add_pair(world, source, targets.id, target, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(compile_catalog(
            "Count :: behavior { count:retain int=0; on "
            "Impulse :: () { "
            "for target in self.Targets { count+=1; } } }",
            backends[backend],
            &active
        ));
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(source));
        REQUIRE(instance);
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        const pin_value *count = execution_instance_state(instance, state_id("count"));
        REQUIRE(count && count->type == PIN_I32 && count->i == 1);
    }
    prepare();
    syntax = script_parse("Bad :: system( p: Position) { for target in self.Targets { await Delay(1); } }");
    analysis = script_analyze(syntax, &active);
    REQUIRE(!analysis->valid);
    bool rejected = false;
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        rejected |= strstr(analysis->diagnostics[i].message, "Streaming relationship iteration cannot suspend") !=
                    NULL;
    }
    REQUIRE(rejected);
    return true;
}

TEST_CASE(program_component_binding_runs_and_source_cycle_is_rejected)
{
    const char *source = "Binding :: behavior {\n"
                         "    bind self.Position.y = self.Position.x * 2;\n"
                         "}\n";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int i = 0; i < 2; i++) {
        prepare();
        ecs_entity entity = positioned(6, false);
        REQUIRE(compile(source, backends[i]));
        REQUIRE(execution_program_attach(program, ecs_entity_to_u64(entity)) != NULL);
        REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
        const program_position *position = ecs_component_data_const(world, entity, 501);
        REQUIRE(position && position->x == 6 && position->y == 12);
        REQUIRE(
            ecs_component_write_field(world, entity, 501, 0, (pin_value){ .type = PIN_F64, .d = 9 }) == ECS_OK
        );
        REQUIRE(execution_program_step(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, entity, 501))
                ->y == 18
        );
        prepare();
        entity = positioned(0, false);
        REQUIRE(compile(
            "Binding :: behavior { saved: double=2; Read :: ()->double { "
            "return saved } "
            "bind self.Position.y=Read(); on Impulse :: (amount: double) { "
            "saved=amount; } }",
            backends[i]
        ));
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(entity));
        REQUIRE(instance);
        REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, entity, 501))
                ->y == 2
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 7 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_step(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, entity, 501))
                ->y == 7
        );
    }
    prepare();
    syntax = script_parse("Cycle :: behavior { bind self.Position.x = self.Position.y; bind self.Position.y = self.Position.x; }");
    analysis = script_analyze(syntax, &catalog);
    REQUIRE(!analysis->valid && analysis->diagnostic_count > 0);
    REQUIRE(module == NULL && program == NULL);
    return true;
}
static const script_catalog_relationship row_relationship = { .id = 601, .name = "Targets" };
static const ecs_relationship_desc row_targets = { .id = 601,
                                                   .key = "program.row-targets",
                                                   .name = "Targets" };
static execution_instance *program_query_tuple(ecs_entity owner, ecs_entity target)
{
    uint64_t variable = ecs_entity_to_u64(target);
    return execution_program_row(program, ecs_entity_to_u64(owner), &variable, 1);
}
TEST_CASE(program_query_chained_variables_filter_values_and_keep_event_tuples)
{
    const script_catalog_relationship relationships[] = {
        { .id = 601, .name = "Targets" },
        { .id = 602, .name = "Leaves" },
    };
    const ecs_relationship_desc leaves = { .id = 602,
                                           .key = "program.row-leaves",
                                           .name = "Leaves" };
    script_catalog active = catalog;
    active.relationships = relationships;
    active.relationship_count = 2;
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        for (int inspected = 0; inspected < 2; inspected++) {
            prepare();
            REQUIRE(ecs_world_register_relationship(world, &row_targets, NULL) == ECS_OK);
            REQUIRE(ecs_world_register_relationship(world, &leaves, NULL) == ECS_OK);
            ecs_entity owner = positioned(0, false);
            ecs_entity middle[] = { positioned(10, false), positioned(20, false) };
            ecs_entity leaf[] = { positioned(5, false),
                                  positioned(15, false),
                                  positioned(25, false) };
            for (int i = 0; i < 2; i++) {
                REQUIRE(
                    ecs_world_record_add_pair(world, owner, row_targets.id, middle[i], 0) == ECS_OK
                );
                REQUIRE(
                    ecs_world_record_add_pair(world, middle[i], leaves.id, leaf[i], 0) == ECS_OK
                );
                REQUIRE(
                    ecs_world_record_add_pair(world, middle[i], leaves.id, leaf[i + 1], 0) == ECS_OK
                );
            }
            REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
            REQUIRE(compile_catalog(
                "Chained :: system( p:Position) join middle in self.Targets "
                "join leaf in middle.Leaves join mid:Position from middle join "
                "aim:Position from leaf where aim.x > mid.x { ticks:retain "
                "double=0; seen:retain double=0; on Impulse :: (amount: "
                "double, enabled: bool) { "
                "if enabled { seen=amount; p.x+=amount+mid.x+aim.x; } } "
                "ticks+=1; p.y+=mid.x+aim.x; aim.y+=mid.x; }",
                backends[backend],
                &active
            ));
            REQUIRE(!execution_module_effects(module).parallel_safe);
            program = execution_program_new(module, world, NULL);
            REQUIRE(program);
            if (inspected) {
                debugger = execution_program_inspector(program);
                REQUIRE(debugger);
                execution_inspector_recording(debugger, true, ecs_entity_to_u64(owner));
            }
            REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
            REQUIRE(execution_program_memory_usage(program).query_rows == 4);
            REQUIRE(execution_program_memory_usage(program).instance_count == 2);
            execution_instance *accepted[2];
            uint64_t tuples[2][2];
            for (int i = 0; i < 2; i++) {
                tuples[i][0] = ecs_entity_to_u64(middle[i]);
                tuples[i][1] = ecs_entity_to_u64(leaf[i + 1]);
                accepted[i] = execution_program_row(program, ecs_entity_to_u64(owner), tuples[i], 2);
                REQUIRE(accepted[i] && number_state(accepted[i], "ticks", 2));
                REQUIRE(number_state(accepted[i], "seen", 0));
                const uint64_t rejected[] = { tuples[i][0],
                                              ecs_entity_to_u64(leaf[i]) };
                REQUIRE(!execution_program_row(program, ecs_entity_to_u64(owner), rejected, 2));
            }
            REQUIRE(accepted[0] != accepted[1]);
            const program_position *position = ecs_component_data_const(world, owner, 501);
            REQUIRE(position && position->x == 0 && position->y == 70);
            for (int i = 0; i < 3; i++) {
                position = ecs_component_data_const(world, leaf[i], 501);
                REQUIRE(position && position->y == i * 10);
            }
            pin_value payload[] = { { .type = PIN_F64, .d = 3 },
                                    { .type = PIN_BOOL, .b = true } };
            REQUIRE(
                execution_program_emit(program, 11, ecs_entity_to_u64(owner), payload, 2) == FLOW_OK
            );
            payload[0].d = 99;
            payload[1].b = false;
            REQUIRE(execution_program_pump(program, .5, 0, 1000) == FLOW_OK);
            position = ecs_component_data_const(world, owner, 501);
            REQUIRE(position && position->x == 76 && position->y == 70);
            for (int i = 0; i < 2; i++) {
                REQUIRE(number_state(accepted[i], "ticks", 2));
                REQUIRE(number_state(accepted[i], "seen", 3));
                REQUIRE(execution_instance_pending(accepted[i]) == 0);
            }
            if (inspected) {
                bool seen[2] = { false, false };
                REQUIRE(execution_inspector_watch_count(debugger) > 0);
                for (int i = 0; i < execution_inspector_activity_count(debugger); i++) {
                    execution_debug_activity activity = execution_inspector_activity_at(debugger, i);
                    if (activity.owner != ecs_entity_to_u64(owner)) {
                        continue;
                    }
                    bool first = activity.query_variables[0] == tuples[0][0] &&
                                 activity.query_variables[1] == tuples[0][1];
                    bool second = activity.query_variables[0] == tuples[1][0] &&
                                  activity.query_variables[1] == tuples[1][1];
                    REQUIRE(first || second);
                    seen[0] |= first;
                    seen[1] |= second;
                }
                REQUIRE(seen[0] && seen[1]);
            }
            REQUIRE(!execution_program_error(program));
        }
    }
    return true;
}
TEST_CASE(program_query_rows_keep_locals_and_cancel_removed_tuple)
{
    script_catalog active = catalog;
    active.relationships = &row_relationship;
    active.relationship_count = 1;
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        REQUIRE(ecs_world_register_relationship(world, &row_targets, NULL) == ECS_OK);
        ecs_entity owner = positioned(0, false), first = positioned(10, true),
                   second = positioned(20, true);
        REQUIRE(ecs_world_record_add_pair(world, owner, row_targets.id, first, 0) == ECS_OK);
        REQUIRE(ecs_world_record_add_pair(world, owner, row_targets.id, second, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(compile_catalog(
            "Rows :: system( p:Position) join target in self.Targets join "
            "aim:Position from target { starts:retain double=0; starts+=1; "
            "saved:=aim.x; await Delay(1); p.y+=saved; }",
            backends[backend],
            &active
        ));
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
        execution_instance *one = program_query_tuple(owner, first);
        execution_instance *two = program_query_tuple(owner, second);
        REQUIRE(one && two && one != two);
        REQUIRE(number_state(one, "starts", 2) && number_state(two, "starts", 2));
        /* Each activation captured its own value. Later component writes must
         * not change either saved local, and removing a row cancels its work. */
        ((program_position *)ecs_component_data(world, first, 501))->x = 100;
        ((program_position *)ecs_component_data(world, second, 501))->x = 200;
        REQUIRE(ecs_world_record_remove_pair(world, owner, row_targets.id, first, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, .25, 0, 1000) == FLOW_OK);
        REQUIRE(!program_query_tuple(owner, first));
        REQUIRE(execution_instance_pending(two) == 1);
        REQUIRE(execution_program_step(program, 1.25, 0, 1000) == FLOW_OK);
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, owner, 501))
                ->y == 20
        );
        REQUIRE(number_state(two, "starts", 3));
        REQUIRE(ecs_world_record_add_pair(world, owner, row_targets.id, first, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 1.5, 0, 1000) == FLOW_OK);
        one = program_query_tuple(owner, first);
        REQUIRE(one && number_state(one, "starts", 4) && number_state(two, "starts", 4));
        ((program_position *)ecs_component_data(world, first, 501))->x = 1000;
        REQUIRE(execution_program_step(program, 2.6, 0, 1000) == FLOW_OK);
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, owner, 501))
                ->y == 320
        );
    }
    return true;
}
TEST_CASE(program_query_changed_preserves_busy_published_writes)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        ecs_entity owner = positioned(0, false);
        REQUIRE(compile_module(
            "Changed :: system( p:Position) where changed(p) and Accept(p.x) { "
            "Accept :: (value:double)->bool { return value>=0; } calls:retain "
            "double=0; calls+=1; await Delay(1); }",
            backends[backend]
        ));
        unsigned char *bytes = NULL;
        size_t length = 0;
        char error[256];
        REQUIRE(execution_prepared_write(module, &bytes, &length, error, sizeof error));
        const execution_prepared_linker linker = { .type = program_prepared_type };
        execution_module *loaded = execution_prepared_read(
            bytes,
            length,
            &linker,
            backends[backend],
            error,
            sizeof error
        );
        free(bytes);
        REQUIRE(loaded);
        execution_module_free(module);
        module = loaded;
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
        execution_instance *instance = execution_program_instance(program, ecs_entity_to_u64(owner));
        REQUIRE(instance && number_state(instance, "calls", 1));
        REQUIRE(ecs_component_modified(world, owner, 501) == ECS_OK);
        REQUIRE(execution_program_step(program, .2, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "calls", 1));
        REQUIRE(execution_program_step(program, 1.2, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "calls", 2));
        REQUIRE(execution_program_step(program, 2.3, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "calls", 2) && execution_instance_pending(instance) == 0);
        ((program_position *)ecs_component_data(world, owner, 501))->x = 2;
        REQUIRE(execution_program_step(program, 2.4, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "calls", 2));
        REQUIRE(ecs_component_modified(world, owner, 501) == ECS_OK);
        REQUIRE(execution_program_step(program, 2.5, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "calls", 3));
    }
    return true;
}
TEST_CASE(program_query_admission_is_frozen_before_row_writes)
{
    script_catalog active = catalog;
    active.relationships = &row_relationship;
    active.relationship_count = 1;
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        REQUIRE(ecs_world_register_relationship(world, &row_targets, NULL) == ECS_OK);
        ecs_entity owner = positioned(0, false);
        for (int target = 0; target < 3; target++) {
            ecs_entity entity = ecs_world_create_entity(world);
            REQUIRE(ecs_world_record_add_pair(world, owner, row_targets.id, entity, 0) == ECS_OK);
        }
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(compile_catalog(
            "Snapshot :: system( p:Position) join target in self.Targets where "
            "p.x==0 { p.x+=1; }",
            backends[backend],
            &active
        ));
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        REQUIRE(execution_program_prepare_system(program, 0, 0, 1000) == FLOW_OK);
        execution_memory_usage memory = execution_program_memory_usage(program);
        double before = ((const program_position *)ecs_component_data_const(world, owner, 501))
                            ->x;
        int run = execution_program_run_system(program);
        int finish = execution_program_finish_system(program);
        REQUIRE(memory.instance_count == 0 && memory.activation_bytes == 0);
        REQUIRE(memory.query_rows == 3 && memory.query_history_bytes == sizeof(uint64_t));
        REQUIRE(before == 0);
        REQUIRE(run == FLOW_OK && finish == FLOW_OK);
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, owner, 501))
                ->x == 3
        );
        memory = execution_program_memory_usage(program);
        REQUIRE(memory.instance_count == 0 && memory.query_history_bytes == 0);
        REQUIRE(execution_program_step(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, owner, 501))
                ->x == 3
        );
    }
    return true;
}
TEST_CASE(program_query_value_filters_preserve_short_circuit_failure_order)
{
    const char *sources[] = {
        "Ordered :: system() where Risky(int(self.Position.x)) and "
        "has(Velocity) { Risky :: (index:int)->bool { values:=[10,20]; return "
        "values[index]>0; } }",
        "Guarded :: system() where has(Velocity) and "
        "Risky(int(self.Position.x)) { Risky :: (index:int)->bool { "
        "values:=[10,20]; return values[index]>0; } }",
    };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        for (int order = 0; order < 2; order++) {
            prepare();
            ecs_entity owner = positioned(2, false);
            REQUIRE(compile(sources[order], backends[backend]));
            if (!order) {
                program_position *position = ecs_component_data(world, owner, 501);
                position->x = 0;
                REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
                REQUIRE(execution_program_memory_usage(program).query_cache_bytes > 0);
                position->x = 2;
                REQUIRE(ecs_component_modified(world, owner, 501) == ECS_OK);
            }
            int status = execution_program_step(program, 0, 0, 1000);
            if (!order) {
                REQUIRE(status < 0);
                REQUIRE(execution_program_memory_usage(program).query_cache_bytes == 0);
                const execution_program_failure *failure = execution_program_error(program);
                REQUIRE(
                    failure && failure->status == status &&
                    failure->owner == ecs_entity_to_u64(owner)
                );
            } else {
                REQUIRE(status == FLOW_OK && !execution_program_error(program));
                REQUIRE(!execution_program_instance(program, ecs_entity_to_u64(owner)));
            }
        }
    }
    return true;
}

TEST_CASE(program_query_predicates_reuse_bounded_scratch_and_fresh_values)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        ecs_entity owners[4];
        for (int i = 0; i < 4; i++) {
            owners[i] = positioned(2 * i - 1, false);
        }
        REQUIRE(compile(
            "Filter :: system( p:Position) where Above(p.x) and Below(p.x) and "
            "p.x<100 { Above :: (value:double)->bool { return Positive(value); "
            "} Positive :: (value:double)->bool { return value>0; } Below :: "
            "(value:double)->bool { return value<4; } p.y+=1; }",
            backends[backend]
        ));
        REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
        execution_memory_usage memory = execution_program_memory_usage(program);
        REQUIRE(memory.query_cache_bytes > 0 && memory.query_cache_bytes <= 64 * 1024);
        uint64_t allocations = behavior_function_data_allocations();
        uint64_t vm_allocations = behavior_function_vm_allocations();
        for (int step = 1; step <= 3; step++) {
            for (int i = 0; i < 4; i++) {
                program_position *position = ecs_component_data(world, owners[i], 501);
                double expected = i == 1 || i == 2 ? 1 : 0;
                if (step > 1) {
                    expected += i == 0 || i == 3 ? step - 1 : 0;
                }
                REQUIRE(position->y == expected);
                position->x = i == 0 || i == 3 ? 2 : -2;
                REQUIRE(ecs_component_modified(world, owners[i], 501) == ECS_OK);
            }
            REQUIRE(execution_program_step(program, step, 0, 1000) == FLOW_OK);
            REQUIRE(behavior_function_data_allocations() == allocations);
            REQUIRE(behavior_function_vm_allocations() == vm_allocations);
            execution_memory_usage next = execution_program_memory_usage(program);
            REQUIRE(next.query_cache_bytes == memory.query_cache_bytes);
        }
        prior_module = module;
        module = NULL;
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        REQUIRE(compile_module(
            "Filter :: system( p:Position) where Above(p.x) and Below(p.x) { "
            "Above :: (value:double)->bool { return Negative(value); } "
            "Negative :: (value:double)->bool { return value<0; } Below :: "
            "(value:double)->bool { return value<4; } p.y+=1; }",
            backends[backend]
        ));
        REQUIRE(execution_program_replace(program, module, false) == FLOW_OK);
        REQUIRE(execution_program_memory_usage(program).query_cache_bytes == 0);
        execution_module_free(prior_module);
        prior_module = NULL;
        REQUIRE(execution_program_step(program, 4, 0, 1000) == FLOW_OK);
        for (int i = 0; i < 4; i++) {
            const program_position *position = ecs_component_data_const(world, owners[i], 501);
            REQUIRE(position->y == (i == 0 || i == 3 ? 3 : 2));
        }
        memory = execution_program_memory_usage(program);
        REQUIRE(memory.query_cache_bytes > 0 && memory.query_cache_bytes <= 64 * 1024);
    }
    return true;
}

TEST_CASE(program_imported_library_preserves_owner_namespace_and_reader_after_replace)
{
    const char *library_source = "Store :: library { count: double = 10;Work "
                                 ":: () -> (local_count: double, position_x: "
                                 "double) {count += 1; await Delay(1); "
                                 "return(count, self.Position.x); } }";
    const char
        *owner_source = "Owner :: behavior { count: double = 0; local_count: "
                        "double = 0; position_x: double = 0;on Impulse :: "
                        "(amount: "
                        "double) { count += amount; values := await "
                        "library_work();local_count = values.local_count; "
                        "position_x = values.position_x; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_module(library_source, backends[b]));
        imported = module;
        module = NULL;
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        REQUIRE(compile(owner_source, backends[b]));
        REQUIRE(execution_module_import_count(module) == 1);
        ecs_entity owners[] = { positioned(6, false), positioned(9, false) };
        execution_instance *instances[] = {
            execution_program_attach(program, ecs_entity_to_u64(owners[0])),
            execution_program_attach(program, ecs_entity_to_u64(owners[1])),
        };
        REQUIRE(instances[0] && instances[1]);
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        for (int i = 0; i < 2; i++) {
            REQUIRE(execution_instance_dispatch(instances[i], 11, args, 2, i, 0, 1000) == FLOW_OK);
            REQUIRE(execution_instance_pending(instances[i]) == 1);
            REQUIRE(number_state(instances[i], "count", 1));
            REQUIRE(number_state(instances[i], "local_count", 0));
            REQUIRE(execution_program_update(program, i + 1, 0, 1000) == FLOW_OK);
            REQUIRE(execution_instance_pending(instances[i]) == 0);
            REQUIRE(number_state(instances[i], "local_count", 11));
            REQUIRE(number_state(instances[i], "position_x", i ? 9 : 6));
        }
        prior_module = module;
        module = NULL;
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        REQUIRE(compile_module(owner_source, backends[b]));
        REQUIRE(execution_program_replace(program, module, false) == FLOW_OK);
        execution_module_free(prior_module);
        prior_module = NULL;
        instances[0] = execution_program_instance(program, ecs_entity_to_u64(owners[0]));
        instances[1] = execution_program_instance(program, ecs_entity_to_u64(owners[1]));
        REQUIRE(instances[0] && instances[1]);
        REQUIRE(
            ecs_component_write_field(world, owners[0], 501, 0, (pin_value){ .type = PIN_F64, .d = 13 }) ==
            ECS_OK
        );
        REQUIRE(execution_instance_dispatch(instances[0], 11, args, 2, 2, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 3, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instances[0], "count", 2));
        REQUIRE(number_state(instances[0], "local_count", 12));
        REQUIRE(number_state(instances[0], "position_x", 13));
        REQUIRE(number_state(instances[1], "count", 1));
        REQUIRE(execution_instance_dispatch(instances[0], 11, args, 2, 3, 0, 1000) == FLOW_OK);
        REQUIRE(execution_instance_pending(instances[0]) == 1);
        execution_program_detach(program, ecs_entity_to_u64(owners[0]));
        REQUIRE(execution_program_instance(program, ecs_entity_to_u64(owners[0])) == NULL);
        REQUIRE(execution_program_update(program, 4, 0, 1000) == FLOW_OK);
        instances[0] = execution_program_attach(program, ecs_entity_to_u64(owners[0]));
        REQUIRE(instances[0] != NULL && number_state(instances[0], "count", 0));
        REQUIRE(execution_instance_dispatch(instances[0], 11, args, 2, 4, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 5, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instances[0], "local_count", 11));
        REQUIRE(number_state(instances[0], "position_x", 13));
        other_program = execution_program_new(module, world, NULL);
        REQUIRE(other_program != NULL);
        execution_instance *other = execution_program_attach(
            other_program,
            ecs_entity_to_u64(owners[0])
        );
        REQUIRE(other != NULL);
        REQUIRE(execution_instance_dispatch(other, 11, args, 2, 10, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(other_program, 11, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(other, "count", 1));
        REQUIRE(number_state(other, "local_count", 11));
        REQUIRE(number_state(other, "position_x", 13));
        REQUIRE(number_state(instances[1], "local_count", 11));
    }
    return true;
}
TEST_CASE(program_system_shared_state_survives_membership_and_is_registration_owned)
{
    const char *source = "Shared :: system(position: Position) { total: "
                         "retain double = 0; position.x += 1; total += 1; "
                         "position.y = total; }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owners[] = { positioned(1, false), positioned(2, false) };
        REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
        execution_instance *
            first = execution_program_instance(program, ecs_entity_to_u64(owners[0]));
        execution_instance *
            second = execution_program_instance(program, ecs_entity_to_u64(owners[1]));
        REQUIRE(first && second);
        REQUIRE(number_state(first, "total", 2) && number_state(second, "total", 2));
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, owners[0], 501))
                ->x == 2
        );
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, owners[1], 501))
                ->x == 3
        );
        REQUIRE(ecs_world_record_remove(world, owners[0], 501, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_instance(program, ecs_entity_to_u64(owners[0])) == NULL);
        REQUIRE(number_state(second, "total", 3));
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, owners[1], 501))
                ->x == 4
        );
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, owners[1], 501))
                ->y == 3
        );
        program_position row = { .x = 1 };
        REQUIRE(ecs_world_record_add(world, owners[0], 501, &row, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 2, 0, 1000) == FLOW_OK);
        first = execution_program_instance(program, ecs_entity_to_u64(owners[0]));
        REQUIRE(first && number_state(first, "total", 5));
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, owners[0], 501))
                ->x == 2
        );
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, owners[1], 501))
                ->x == 5
        );
        other_program = execution_program_new(module, world, NULL);
        REQUIRE(other_program != NULL);
        REQUIRE(execution_program_step(other_program, 3, 0, 1000) == FLOW_OK);
        execution_instance *other = execution_program_instance(
            other_program,
            ecs_entity_to_u64(owners[0])
        );
        REQUIRE(other && number_state(other, "total", 2));
        REQUIRE(number_state(first, "total", 5));
    }
    return true;
}
static bool string_state(execution_instance *instance, const char *name, const char *expected)
{
    const pin_value *value = execution_instance_state(instance, state_id(name));
    return value && value->type == PIN_STR && !strcmp(value->str, expected);
}
TEST_CASE(program_managed_state_survives_overwrite_and_multiple_resumes)
{
    const char *
        source = "Managed :: behavior { count: double = 0; text: string = "
                 "\"initial\"; observed: string = \"\";on Impulse :: (amount: "
                 "double) { count += amount; saved := text;text = "
                 "\"changed\"; await Delay(1); observed = saved; saved = "
                 "text;text = \"final\"; await Delay(1); observed = saved; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        for (int round = 0; round < 2; round++) {
            double now = round * 3;
            char initial[] = "before";
            REQUIRE(
                execution_instance_set_state(
                    instance,
                    state_id("text"),
                    (pin_value){ .type = PIN_STR, .str = initial }
                ) == FLOW_OK
            );
            initial[0] = 'X';
            REQUIRE(string_state(instance, "text", "before"));
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, now, 0, 1000) == FLOW_OK);
            REQUIRE(execution_instance_pending(instance) == 1);
            REQUIRE(string_state(instance, "text", "changed"));
            REQUIRE(execution_instance_state(instance, state_id("count"))->d == round + 1);
            char replacement[] = "between";
            REQUIRE(
                execution_instance_set_state(
                    instance,
                    state_id("text"),
                    (pin_value){ .type = PIN_STR, .str = replacement }
                ) == FLOW_OK
            );
            replacement[0] = 'X';
            REQUIRE(execution_program_update(program, now + 1, 0, 1000) == FLOW_OK);
            REQUIRE(string_state(instance, "observed", "before"));
            REQUIRE(string_state(instance, "text", "final"));
            REQUIRE(execution_instance_pending(instance) == 1);
            REQUIRE(execution_instance_state(instance, state_id("count"))->d == round + 1);
            REQUIRE(execution_program_update(program, now + 2, 0, 1000) == FLOW_OK);
            REQUIRE(string_state(instance, "observed", "between"));
            REQUIRE(execution_instance_pending(instance) == 0);
            REQUIRE(
                execution_instance_set_state(
                    instance,
                    state_id("text"),
                    (pin_value){ .type = PIN_STR, .str = "after" }
                ) == FLOW_OK
            );
            REQUIRE(string_state(instance, "observed", "between"));
        }
        execution_program_detach(program, ecs_entity_to_u64(owner));
        REQUIRE(execution_program_instance(program, ecs_entity_to_u64(owner)) == NULL);
    }
    return true;
}
TEST_CASE(program_result_tuple_copies_survive_mutation_loop_and_resume)
{
    const char *source =
        "TupleCopies :: behavior { first: double = 0; second: double = "
        "0;before_text: string = \"\"; after_text: string = \"\";Pair :: (n: "
        "double, t: string) -> (total: double, message: string) {return(n, t); "
        "}on Impulse :: () { i: double = 0; while i < 2 {current := "
        "Pair(i, \"before\"); saved := current;current.total = 99; "
        "current.message = \"mutated\";current = Pair(100, \"replacement\"); "
        "await Delay(1);first += saved.total; before_text = "
        "saved.message;saved = current; await Delay(1);second += saved.total; "
        "after_text = saved.message; i += 1; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        for (int round = 0; round < 2; round++) {
            REQUIRE(execution_instance_pending(instance) == 1);
            REQUIRE(execution_program_update(program, round * 2 + 1, 0, 1000) == FLOW_OK);
            REQUIRE(number_state(instance, "first", round));
            REQUIRE(string_state(instance, "before_text", "before"));
            REQUIRE(execution_instance_pending(instance) == 1);
            REQUIRE(execution_program_update(program, round * 2 + 2, 0, 1000) == FLOW_OK);
            REQUIRE(number_state(instance, "second", (round + 1) * 100));
            REQUIRE(string_state(instance, "after_text", "replacement"));
        }
        REQUIRE(execution_instance_pending(instance) == 0);
        execution_program_detach(program, ecs_entity_to_u64(owner));
        REQUIRE(execution_program_instance(program, ecs_entity_to_u64(owner)) == NULL);
    }
    return true;
}
TEST_CASE(program_array_literals_are_fresh_copies)
{
    const char *source =
        "Literals :: behavior { first: double = 0; second: double = 0; "
        "mixed_total: double = 0; text: string = \"\"; kept: double[] = []; "
        "loop_total: double = 0; Tiles :: () -> (tiles: double[]) { return [3, "
        "-1.5, 7]; } on Impulse :: (amount: double) { changed := Tiles(); "
        "changed[0] = 99; fresh := Tiles(); first = fresh[0] + fresh[1]; "
        "second = changed[0]; mixed: double[] = [amount, 2]; mixed_total = "
        "mixed[0] + mixed[1]; names: string[] = [\"a\", \"b\"]; text = "
        "names[1]; kept = [1, 2]; kept[0] = kept[0] + 10; loop_total = 0; i: "
        "double = 0; while i < 2 { row: double[] = [3, 4]; row[0] = row[0] + "
        "1; loop_total += row[0]; i += 1; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        const pin_value args[] = { { .type = PIN_F64, .d = 5 },
                                   { .type = PIN_BOOL, .b = true } };
        for (int round = 0; round < 2; round++) {
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
            REQUIRE(number_state(instance, "first", 1.5));
            REQUIRE(number_state(instance, "second", 99));
            REQUIRE(number_state(instance, "mixed_total", 7));
            REQUIRE(string_state(instance, "text", "b"));
            REQUIRE(number_state(instance, "loop_total", 8));
            const pin_value *kept = execution_instance_state(instance, state_id("kept"));
            REQUIRE(kept && kept->container == PIN_CONTAINER_ARRAY);
            const pin_array *items = kept->ptr;
            REQUIRE(
                items->count == 2 && pin_array_get(items, 0).d == 11 &&
                pin_array_get(items, 1).d == 2
            );
        }
        execution_program_detach(program, ecs_entity_to_u64(owner));
    }
    return true;
}
static bool memory_text_state(execution_instance *instance, const char *name, int produced)
{
    const pin_value *value = execution_instance_state(instance, state_id(name));
    int size = MEMORY_TEXT_MIN + produced * MEMORY_TEXT_STEP;
    return value && value->type == PIN_STR && value->str[0] == 'A' + produced % 26 &&
           strlen(value->str) == (size_t)size && value->str[size - 1] == 'x';
}
/* This audit deliberately inspects private box storage: public payload metrics
 * cover activation arenas, not the detached box's ordinary arena. It measures
 * retained bytes without declaring arena usage to be semantically live data. */
static execution_box *capture_box(execution_instance *instance, int *holders)
{
    execution_box *box = NULL;
    *holders = 0;
    for (int i = 0; i < acount(instance->activations); i++) {
        execution_activation *activation = instance->activations[i];
        for (int j = 0; j < acount(activation->held); j++) {
            assert(!box || box == activation->held[j]);
            box = activation->held[j];
            (*holders)++;
        }
    }
    return box;
}
static bool capture_text_is(const pin_value *value, int produced)
{
    if (!value || value->type != PIN_STR || value->container != PIN_CONTAINER_SINGLE ||
        strlen(value->str) != CAPTURE_TEXT_BYTES ||
        value->str[0] != 'A' + produced / 26 || value->str[1] != 'a' + produced % 26) {
        return false;
    }
    for (int i = 2; i < CAPTURE_TEXT_BYTES; i++) {
        if (value->str[i] != 'x') {
            return false;
        }
    }
    return true;
}
static bool capture_state_is(execution_instance *instance, const char *name, int produced, bool array)
{
    const pin_value *value = execution_instance_state(instance, state_id(name));
    if (!array) {
        return capture_text_is(value, produced);
    }
    if (!value || value->type != PIN_STR ||
        value->container != PIN_CONTAINER_ARRAY || !value->ptr) {
        return false;
    }
    const pin_array *items = value->ptr;
    if (items->count != 2) {
        return false;
    }
    pin_value first = pin_array_get(items, 0), second = pin_array_get(items, 1);
    return capture_text_is(&first, produced) && capture_text_is(&second, 1);
}
static bool capture_measure(
    execution_instance *instance,
    const char *label,
    behavior_backend backend,
    bool array,
    bool attached,
    int expected_holders,
    int writes
)
{
    int holders;
    execution_box *box = capture_box(instance, &holders);
    REQUIRE(holders == expected_holders);
    if (!expected_holders) {
        REQUIRE(!box);
        printf(
            "capture backend=%d shape=%s phase=%s writes=%d holders=0 "
            "boxes=0\n",
            backend,
            array ? "array-element" : "string",
            label,
            writes
        );
        return true;
    }
    REQUIRE(box && box->holders == holders + attached);
    REQUIRE((box->owner == &box->detached) == !attached);
    REQUIRE(box->value);
    REQUIRE(attached || box->value == &box->kept);
    size_t used, capacity;
    arena_usage(&box->storage, &used, &capacity);
    size_t backing = arena_storage_bytes(&box->storage);
    REQUIRE(used <= capacity && capacity <= backing && backing < CAPTURE_STORAGE_LIMIT);
    /* A fresh owning snapshot is a bounded reference size for the current
     * value, including nested array headers and strings, not the old versions. */
    arena current = { 0 };
    pin_value snapshot;
    REQUIRE(!pin_value_snapshot_to_arena(&current, &snapshot, box->value));
    size_t live, live_capacity;
    arena_usage(&current, &live, &live_capacity);
    REQUIRE(live >= CAPTURE_TEXT_BYTES && live < 4 * CAPTURE_TEXT_BYTES);
    pin_value captured = array ? pin_array_get(snapshot.ptr, 0) : snapshot;
    REQUIRE(capture_text_is(&captured, array ? writes ? writes + 1 : 0 : writes));
    arena_free(&current);
    if (!attached) {
        REQUIRE(used == live);
    }
    execution_memory_usage memory = execution_program_memory_usage(program);
    printf(
        "capture backend=%d shape=%s phase=%s writes=%d attached=%d holders=%d "
        "box_used=%zu box_capacity=%zu box_backing=%zu current_snapshot=%zu "
        "activation_payload=%zu activation_capacity=%zu\n",
        backend,
        array ? "array-element" : "string",
        label,
        writes,
        attached,
        box->holders,
        used,
        capacity,
        backing,
        live,
        memory.payload_bytes,
        memory.payload_capacity
    );
    return true;
}
static bool capture_retention_case(bool array, bool attached, int ending)
{
    char source[2048];
    int length = snprintf(
        source,
        sizeof source,
        "Capture :: behavior { writer: task; keeper: task; count: double = 0;"
        "seen: string = \"\"; latest: string = \"\"; tail: string = \"\";"
        "initial: %s; saved: %s;"
        "Write :: (value: ref %s, alias: ref %s) { yield; i := 0;"
        "while i < %d { %s = capture_text(); seen = %s; %s"
        "if i == 7 { saved = alias; } count += 1; i += 1; yield; } }"
        "Keep :: (value: ref %s) { yield; i := 0; while i < %d {"
        "latest = %s; i += 1; yield; } }"
        "on Impulse :: (amount: double, enabled: bool) { if amount == 0 { v := "
        "%s; initial = v;"
        "writer = Write(v, v); keeper = Keep(v); if enabled { await writer; } }"
        "else if amount == 1 { cancel writer; } else { cancel keeper; } } }",
        array ? "string[]" : "string = \"\"",
        array ? "string[]" : "string = \"\"",
        array ? "string[]" : "string",
        array ? "string[]" : "string",
        CAPTURE_WRITES,
        array ? "value[0]" : "value",
        array ? "alias[0]" : "alias",
        array ? "tail = alias[1];" : "",
        array ? "string[]" : "string",
        CAPTURE_KEEP_UPDATES,
        array ? "value[0]" : "value",
        array ? "[capture_text(), capture_text()]" : "capture_text()"
    );
    REQUIRE(length > 0 && (size_t)length < sizeof source);
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        capture_text_calls = 0;
        REQUIRE(compile(source, backends[b]));
        REQUIRE(execution_module_backend(module) == backends[b]);
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL, .b = attached } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 10000) == FLOW_OK);
        REQUIRE(capture_measure(instance, "start", backends[b], array, attached, 3, 0));
        int last = ending ? CAPTURE_WRITES / 2 : CAPTURE_WRITES;
        for (int step = 1; step <= last; step++) {
            REQUIRE(execution_program_update(program, step, 0, 10000) == FLOW_OK);
            REQUIRE(number_state(instance, "count", step));
            REQUIRE(capture_state_is(instance, "seen", step + array, false));
            REQUIRE(capture_state_is(instance, "initial", 0, array));
            if (step >= 8) {
                REQUIRE(capture_state_is(instance, "saved", 8 + array, array));
            }
            if (array) {
                REQUIRE(capture_state_is(instance, "tail", 1, false));
            }
            if (step == 8 || step == 16 || step == CAPTURE_WRITES) {
                REQUIRE(
                    capture_measure(instance, "overwrite", backends[b], array, attached, 3, step)
                );
            }
        }
        REQUIRE(capture_text_calls == last + 1 + array);
        if (ending == 2) {
            execution_program_detach(program, ecs_entity_to_u64(owner));
            execution_memory_usage detached = execution_program_memory_usage(program);
            REQUIRE(
                !detached.instance_count && !detached.activation_bytes &&
                !detached.payload_capacity
            );
            printf(
                "capture backend=%d shape=%s phase=owner-detach instances=0 "
                "activations=0 payload=0\n",
                backends[b],
                array ? "array-element" : "string"
            );
            continue;
        }
        if (ending == 1) {
            args[0].d = 1;
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, last, 0, 10000) == FLOW_OK);
        }
        REQUIRE(execution_program_update(program, last + 1, 0, 10000) == FLOW_OK);
        REQUIRE(number_state(instance, "count", last));
        REQUIRE(capture_state_is(instance, "latest", last + array, false));
        REQUIRE(capture_measure(
            instance,
            ending ? "writer-cancelled" : "writer-complete",
            backends[b],
            array,
            false,
            1,
            last
        ));
        if (ending == 1) {
            args[0].d = 2;
            REQUIRE(
                execution_instance_dispatch(instance, 11, args, 2, last + 1, 0, 10000) == FLOW_OK
            );
            REQUIRE(execution_instance_pending(instance) == 0);
            REQUIRE(
                capture_measure(instance, "cancelled-before-update", backends[b], array, false, 1, last)
            );
            /* Cancellation stops execution immediately. Finished activations
             * release their held boxes at the next program safe point. */
            REQUIRE(execution_program_update(program, last + 2, 0, 10000) == FLOW_OK);
        } else {
            for (int step = last + 2; step <= CAPTURE_KEEP_UPDATES + 1; step++) {
                REQUIRE(execution_program_update(program, step, 0, 10000) == FLOW_OK);
            }
        }
        REQUIRE(capture_measure(
            instance,
            ending ? "last-holder-cancelled" : "last-holder-complete",
            backends[b],
            array,
            false,
            0,
            last
        ));
        REQUIRE(execution_instance_pending(instance) == 0);
        REQUIRE(capture_state_is(instance, "initial", 0, array));
        REQUIRE(capture_state_is(instance, "saved", 8 + array, array));
        REQUIRE(capture_state_is(instance, "seen", last + array, false));
        execution_memory_usage complete = execution_program_memory_usage(program);
        REQUIRE(!complete.payload_bytes && !complete.payload_capacity);
        execution_program_detach(program, ecs_entity_to_u64(owner));
        REQUIRE(!execution_program_memory_usage(program).instance_count);
    }
    return true;
}
TEST_CASE(program_detached_string_capture_retention_audit)
{
    for (int ending = 0; ending < 3; ending++) {
        REQUIRE(capture_retention_case(false, false, ending));
    }
    return true;
}
TEST_CASE(program_detached_array_capture_retention_audit)
{
    for (int ending = 0; ending < 3; ending++) {
        REQUIRE(capture_retention_case(true, false, ending));
    }
    return true;
}
TEST_CASE(program_attached_string_capture_retention_control)
{
    REQUIRE(capture_retention_case(false, true, 0));
    return true;
}
TEST_CASE(program_descendant_capture_keeps_inspector_snapshots_across_shrink)
{
    const char
        *source = "Descendant :: behavior { before: string = \"\"; observed: "
                  "string = \"\";done: double = 0;Child :: (value: ref string) "
                  "{ yield; value = value; before = value;yield; value = "
                  "\"small\"; observed = value;yield; value = \"\"; done = 1; "
                  "yield; }Relay :: (value: ref string) { yield; Child(value); "
                  "}on Impulse :: () { v := capture_text(); "
                  "Relay(v); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        capture_text_calls = 0;
        REQUIRE(compile(source, backends[b]));
        REQUIRE(execution_module_backend(module) == backends[b]);
        uint64_t owner = ecs_entity_to_u64(ecs_world_create_entity(world));
        execution_instance *instance = execution_program_attach(program, owner);
        REQUIRE(instance);
        debugger = execution_program_inspector(program);
        REQUIRE(debugger);
        const pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 10000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 1, 0, 10000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 2, 0, 10000) == FLOW_OK);
        REQUIRE(capture_text_calls == 1 && capture_state_is(instance, "before", 0, false));
        int holders;
        execution_box *box = capture_box(instance, &holders);
        REQUIRE(box && holders == 1 && box->holders == 1 && box->owner == &box->detached);
        int saved = -1;
        for (int i = 0; i < execution_inspector_watch_count(debugger); i++) {
            const execution_debug_watch *watch = execution_inspector_watch_at(debugger, i);
            if (watch->availability == EXECUTION_WATCH_AVAILABLE &&
                capture_text_is(&watch->value, 0)) {
                saved = i;
            }
        }
        REQUIRE(saved >= 0);
        execution_inspector_recording(debugger, false, owner);
        REQUIRE(execution_inspector_pause(debugger, owner));
        REQUIRE(execution_program_update(program, 3, 0, 10000) >= FLOW_OK);
        REQUIRE(execution_program_paused(program));
        REQUIRE(capture_text_is(box->value, 0));
        REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_RESUME) == FLOW_SUSPENDED_RESULT);
        REQUIRE(!strcmp(box->value->str, "small") && arena_storage_bytes(&box->storage) < 512);
        REQUIRE(execution_program_update(program, 4, 0, 10000) == FLOW_OK);
        REQUIRE(!strcmp(box->value->str, "") && arena_storage_bytes(&box->storage) < 512);
        REQUIRE(number_state(instance, "done", 1));
        REQUIRE(capture_text_is(&execution_inspector_watch_at(debugger, saved)->value, 0));
        REQUIRE(execution_program_update(program, 5, 0, 10000) == FLOW_OK);
        REQUIRE(!capture_box(instance, &holders) && !holders);
        REQUIRE(!execution_instance_pending(instance));
        execution_program_detach(program, owner);
        REQUIRE(capture_text_is(&execution_inspector_watch_at(debugger, saved)->value, 0));
    }
    return true;
}
TEST_CASE(program_detached_capture_preserves_task_handles_until_replaced)
{
    const char
        *source = "Handles :: behavior { seen: double = 0;Result :: () -> "
                  "(value: double) { yield; return 7; }Use :: (jobs: ref "
                  "task<double>[]) { yield; jobs = jobs; yield;answer := await "
                  "jobs[0]; if answer is double n { seen = n; }jobs = []; "
                  "yield; }on Impulse :: () { jobs: task<double>[] "
                  "= [Result()]; Use(jobs); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        REQUIRE(execution_module_backend(module) == backends[b]);
        uint64_t owner = ecs_entity_to_u64(ecs_world_create_entity(world));
        execution_instance *instance = execution_program_attach(program, owner);
        REQUIRE(instance);
        const pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 10000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 1, 0, 10000) == FLOW_OK);
        REQUIRE(execution_program_memory_usage(program).task_result_bytes > 0);
        int holders;
        execution_box *box = capture_box(instance, &holders);
        REQUIRE(box && holders == 1 && ((pin_array *)box->value->ptr)->count == 1);
        REQUIRE(execution_program_update(program, 2, 0, 10000) == FLOW_OK);
        REQUIRE(number_state(instance, "seen", 7));
        REQUIRE(((pin_array *)box->value->ptr)->count == 0);
        REQUIRE(arena_storage_bytes(&box->storage) < 512);
        REQUIRE(execution_program_update(program, 3, 0, 10000) == FLOW_OK);
        REQUIRE(!capture_box(instance, &holders) && !holders);
        REQUIRE(!execution_instance_pending(instance));
        REQUIRE(!execution_program_memory_usage(program).task_result_bytes);
        execution_program_detach(program, owner);
    }
    return true;
}
TEST_CASE(program_managed_payload_memory_is_bounded_across_root_and_outlined_resumes)
{
    const char *body = "saved := managed_text(); i: double = 0; while i < 64 {"
                       "current := Pair(i, managed_text()); copied := current;"
                       "current.message = \"discarded\"; text = "
                       "copied.message; observed = saved;"
                       "if i == 3 { saved = text; } await Delay(1); count += "
                       "1; i += 1; }";
    const char *starts[] = { "on Impulse :: (amount: double, enabled: bool) {",
                             "Run :: () -> () {" };
    const char *ends[] = { "}", "} on Impulse :: (amount: double, enabled: bool) { await Run(); }" };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int outlined = 0; outlined < 2; outlined++) {
        char source[2048];
        int size = snprintf(
            source,
            sizeof source,
            "Retained :: behavior { count: double = 0; text: string = \"\";"
            "observed: string = \"\";"
            "Pair :: (n: double, t: string) -> (total: double, message: "
            "string) {"
            "return(n, t); } %s %s %s }",
            starts[outlined],
            body,
            ends[outlined]
        );
        REQUIRE(size > 0 && (size_t)size < sizeof source);
        for (int b = 0; b < 2; b++) {
            prepare();
            managed_text_calls = 0;
            REQUIRE(compile(source, backends[b]));
            ecs_entity owner = ecs_world_create_entity(world);
            execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
            REQUIRE(instance != NULL);
            const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                       { .type = PIN_BOOL, .b = true } };
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
            REQUIRE(execution_instance_pending(instance) == 1);
            REQUIRE(memory_text_state(instance, "observed", 0));
            execution_memory_usage warm = { 0 };
            for (int step = 1; step <= 64; step++) {
                REQUIRE(execution_program_update(program, step, 0, 1000) == FLOW_OK);
                REQUIRE(number_state(instance, "count", step));
                REQUIRE(memory_text_state(instance, "observed", step <= 3 ? 0 : 4));
                REQUIRE(execution_instance_pending(instance) == (step < 64));
                if (step == 8) {
                    warm = execution_program_memory_usage(program);
                    REQUIRE(warm.payload_bytes >= MEMORY_TEXT_MIN);
                    REQUIRE(warm.payload_capacity >= warm.payload_bytes);
                    REQUIRE(warm.payload_capacity <= warm.payload_bytes * 2 + MEMORY_PLATEAU_SLACK);
                }
                if (step == 40) {
                    execution_memory_usage steady = execution_program_memory_usage(program);
                    /* Payloads grow by 32 bytes per native call. Live bytes can
                     * grow with them; capacity must stay within one live wave
                     * plus 4KiB, rather than retaining every smaller dead slot. */
                    REQUIRE(steady.payload_bytes >= warm.payload_bytes);
                    REQUIRE(steady.payload_bytes <= warm.payload_bytes * 3 + MEMORY_PLATEAU_SLACK);
                    REQUIRE(steady.payload_capacity <= steady.payload_bytes * 2 + MEMORY_PLATEAU_SLACK);
                }
            }
            REQUIRE(managed_text_calls == 65);
            REQUIRE(memory_text_state(instance, "text", 64));
            execution_memory_usage complete = execution_program_memory_usage(program);
            REQUIRE(complete.payload_bytes == 0 && complete.payload_capacity == 0);
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 100, 0, 1000) == FLOW_OK);
            REQUIRE(execution_instance_pending(instance) == 1);
            REQUIRE(execution_program_memory_usage(program).payload_bytes > 0);
            execution_program_detach(program, ecs_entity_to_u64(owner));
            execution_memory_usage detached = execution_program_memory_usage(program);
            REQUIRE(detached.payload_bytes == 0 && detached.payload_capacity == 0);
        }
    }
    return true;
}
TEST_CASE(program_completed_payloads_are_released_without_marking)
{
    const char
        *source = "Completed :: behavior { seen: double = 0;Pair :: (n: "
                  "double) -> (first: double, second: double) {return(n, n + "
                  "1); }on Impulse :: () {i := 0; while i < 512 { "
                  "current := Pair(i);values: double[] = [current.first, "
                  "current.second];seen = values[0] + values[1]; i += 1; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        uint64_t marks = arena_collect_marks();
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000000) == FLOW_OK);
        REQUIRE(number_state(instance, "seen", 1023));
        REQUIRE(execution_instance_pending(instance) == 0);
        execution_memory_usage complete = execution_program_memory_usage(program);
        REQUIRE(complete.payload_bytes == 0 && complete.payload_capacity == 0);
        /* The completed call owns no suspended work. Free its payloads
         * directly instead of tracing values that will all be destroyed. */
        REQUIRE(arena_collect_marks() == marks);
    }
    return true;
}
TEST_CASE(program_temporary_payloads_preserve_roots_across_collection)
{
    const char
        *source = "Payloads :: behavior { seen: double = 0;Pair :: (n: double) "
                  "-> (first: double, second: double) {return(n, n + 1); }on "
                  "Impulse :: () {saved := Pair(40); kept: double[] "
                  "= [2, 3, 5];i := 0; while i < 512 {current := Pair(i); "
                  "values: double[] = [current.first, "
                  "current.second];self.Position.x = values[0]; "
                  "self.Position.y = values[1]; i += 1; }await Delay(1); seen "
                  "= saved.first + saved.second + kept[0] + kept[2];} }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = positioned(0, false);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000000) == FLOW_OK);
        REQUIRE(execution_instance_pending(instance) == 1);
        const program_position *updated = ecs_component_data_const(world, owner, 501);
        REQUIRE(updated && updated->x == 511 && updated->y == 512);
        REQUIRE(execution_program_update(program, 1, 0, 1000000) == FLOW_OK);
        REQUIRE(number_state(instance, "seen", 88));
        REQUIRE(execution_instance_pending(instance) == 0);
        execution_memory_usage complete = execution_program_memory_usage(program);
        REQUIRE(complete.payload_bytes == 0 && complete.payload_capacity == 0);
    }
    return true;
}
TEST_CASE(program_sibling_timer_actions_complete_independently_and_cancel_on_detach)
{
    const char
        *source = "Timers :: behavior { early_count: double = 0; late_count: "
                  "double = 0;action early = timer(1); action late = "
                  "timer(3);on Impulse :: () { early(); late(); "
                  "}after early.Completed { early_count += 1; }after "
                  "late.Completed { late_count += 1; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(
            number_state(instance, "early_count", 0) &&
            number_state(instance, "late_count", 0)
        );
        REQUIRE(execution_program_update(program, 0.5, 0, 1000) == FLOW_OK);
        REQUIRE(
            number_state(instance, "early_count", 0) &&
            number_state(instance, "late_count", 0)
        );
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(
            number_state(instance, "early_count", 1) &&
            number_state(instance, "late_count", 0)
        );
        REQUIRE(execution_program_update(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(
            number_state(instance, "early_count", 1) &&
            number_state(instance, "late_count", 0)
        );
        REQUIRE(execution_program_update(program, 3, 0, 1000) == FLOW_OK);
        REQUIRE(
            number_state(instance, "early_count", 1) &&
            number_state(instance, "late_count", 1)
        );
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 4, 0, 1000) == FLOW_OK);
        execution_program_detach(program, ecs_entity_to_u64(owner));
        REQUIRE(execution_program_instance(program, ecs_entity_to_u64(owner)) == NULL);
        instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        REQUIRE(execution_program_update(program, 10, 0, 1000) == FLOW_OK);
        REQUIRE(
            number_state(instance, "early_count", 0) &&
            number_state(instance, "late_count", 0)
        );
    }
    return true;
}
TEST_CASE(program_failure_snapshot_owns_outlined_source_diagnostics)
{
    const char *source = "Failure :: behavior {\n"
                         "  Outline :: () -> (Result: double) {\n"
                         "    return fail_native();\n"
                         "  }\n"
                         "  on Impulse :: () { output := Outline(); }\n"
                         "}";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_program_set_name(program, "owned diagnostic fixture");
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        strcpy(failure_message, "first native failure");
        int status = execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000);
        REQUIRE(status < 0);
        const execution_program_failure *failure = execution_program_error(program);
        REQUIRE(failure != NULL);
        execution_program_failure copy = *failure;
        REQUIRE(copy.status == status && copy.kind == BEHAVIOR_FUNCTION_HOST_DATA);
        REQUIRE(copy.module_id == execution_module_id(module));
        REQUIRE(copy.owner == ecs_entity_to_u64(owner) && copy.node_id != 0);
        uint64_t outlined = 0;
        for (int i = 0; i < execution_module_function_count(module); i++) {
            execution_module_function_info f = execution_module_function_at(module, i);
            if (f.kind == EXECUTION_FUNCTION_AUTHORED) {
                outlined = f.id;
            }
        }
        REQUIRE(outlined != 0 && copy.function_id == outlined);
        int start = (int)(strstr(source, "fail_native()") - source);
        REQUIRE(copy.start == start && copy.end == start + (int)strlen("fail_native()"));
        REQUIRE(copy.line == 3 && copy.column == 12);
        REQUIRE(
            !strcmp(copy.message, "first native failure") &&
            !strcmp(copy.asset_name, "Failure")
        );
        strcpy(failure_message, "second native failure");
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 1, 0, 1000) < 0);
        REQUIRE(!strcmp(execution_program_error(program)->message, "first native failure"));
        execution_program_clear_error(program);
        REQUIRE(execution_program_error(program) == NULL);
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 2, 0, 1000) < 0);
        REQUIRE(!strcmp(execution_program_error(program)->message, "second native failure"));
        execution_program_free(program);
        program = NULL;
        REQUIRE(
            !strcmp(copy.message, "first native failure") && copy.line == 3 &&
            copy.start == start
        );
    }
    return true;
}
/* assert passes a true condition and fails the call at its own position
 * otherwise, with its message or else its condition's text, on each backend.
 * It is pure, and a helper that only asserts still runs. */
TEST_CASE(program_assert_fails_with_its_message_or_condition)
{
    const char
        *source = "Checks :: behavior {\n"
                  "  Positive :: (value: double) { assert(value > 0.0); }\n"
                  "  on Impulse :: (amount: double, enabled: bool) {\n"
                  "    assert(enabled, \"enabled is set\");\n"
                  "    Positive(amount);\n"
                  "    assert(amount > 2.0, \"amount is large\");\n"
                  "  }\n"
                  "}";
    static const struct
    {
        double amount;
        const char *message;
        int line, column;
    } cases[] = {
        { 3, NULL, 0, 0 },
        { 1, "Assertion failed: amount is large", 6, 5 },
        { -1, "Assertion failed: value > 0.0", 2, 33 },
    };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        for (int c = 0; c < 3; c++) {
            prepare();
            REQUIRE(compile_pack(source, backends[b], &NATIVE_TEXT_DEFINITION_PACK));
            ecs_entity owner = ecs_world_create_entity(world);
            execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
            REQUIRE(instance != NULL);
            pin_value args[] = { { .type = PIN_F64, .d = cases[c].amount },
                                 { .type = PIN_BOOL, .b = true } };
            int status = execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000);
            const execution_program_failure *failure = execution_program_error(program);
            if (!cases[c].message) {
                REQUIRE(status >= 0 && failure == NULL);
            } else {
                REQUIRE(status < 0 && failure && failure->kind == BEHAVIOR_FUNCTION_DATA);
                REQUIRE(!strcmp(failure->message, cases[c].message));
                REQUIRE(
                    failure->line == cases[c].line &&
                    failure->column == cases[c].column
                );
            }
            execution_program_free(program);
            program = NULL;
        }
    }
    /* The message is fixed when the call is compiled. */
    prepare();
    REQUIRE(lowering_reports(
        "Checks :: behavior { on Impulse :: (amount: double, enabled: bool) { "
        "assert(enabled, format_text(\"{}\", amount)); } }",
        &NATIVE_TEXT_DEFINITION_PACK,
        "assert's message is a string constant"
    ));
    return true;
}
/* Named arguments bind as for any call, and an asserting helper is pure, so
 * a narrowed optional stays narrowed across it. */
TEST_CASE(program_assert_binds_names_and_keeps_helpers_pure)
{
    const char *
        source = "Named :: behavior {\n"
                 "  total: double = 0;\n"
                 "  Check :: (n: double) { assert(n >= 0.0); }\n"
                 "  on Impulse :: (amount: double) {\n"
                 "    local: double? = amount;\n"
                 "    if local != nil { Check(amount); total = local + 1.0; }\n"
                 "    assert(message: \"too small\", condition: amount > "
                 "2.0);\n"
                 "  }\n"
                 "}";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_pack(source, backends[b], &NATIVE_TEXT_DEFINITION_PACK));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        pin_value args[] = { { .type = PIN_F64, .d = 1 },
                             { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) < 0);
        const execution_program_failure *failure = execution_program_error(program);
        REQUIRE(failure && !strcmp(failure->message, "Assertion failed: too small"));
        REQUIRE(failure->line == 7 && failure->column == 5);
        REQUIRE(number_state(instance, "total", 2));
        execution_program_free(program);
        program = NULL;
    }
    return true;
}
/* A failed call stack unwinds at once, so neither payload collection nor a
 * service rebind reaches the host banks its frames released. */
TEST_CASE(program_failed_call_stacks_unwind_at_once)
{
    const char *source = "Failure :: behavior { n: double = 0;Inner :: () -> "
                         "(Result: double) { Delay(1); return fail_native() "
                         "}Middle :: () { n += Inner() }Outer :: () { Middle() "
                         "}on Impulse :: () { Outer() } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        strcpy(failure_message, "nested failure");
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) < 0);
        execution_program_clear_error(program);
        execution_program_set_name(program, "rebound after failure");
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 0));
    }
    return true;
}
TEST_CASE(program_module_finish_preserves_failure_and_consumes_success)
{
    char initial[] = "producer text";
    owned_plan = execution_module_plan_new();
    owned_plan->id = 123;
    owned_plan->valid = true;
    apush(
        owned_plan->states,
        ((execution_module_state){
            .id = 456,
            .initial = { .type = PIN_STR, .str = initial },
        })
    );
    execution_module_plan *before = owned_plan;
    char error[192];
    module = execution_module_finish(&owned_plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(!module);
    REQUIRE(owned_plan == before && error[0]);
    owned_plan->state_count = 1;
    module = execution_module_finish(&owned_plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module && !owned_plan);
    REQUIRE(execution_module_id(module) == 123);
    initial[0] = 'X';
    program = execution_program_new(module, NULL, NULL);
    REQUIRE(program);
    execution_instance *instance = execution_program_attach(program, 1);
    REQUIRE(instance);
    const pin_value *value = execution_instance_state(instance, 456);
    REQUIRE(value && value->type == PIN_STR && !strcmp(value->str, "producer text"));
    return true;
}
static bool int_state(execution_instance *instance, const char *name, pin_type type, int64_t expected)
{
    const pin_value *value = execution_instance_state(instance, state_id(name));
    return value && value->type == type &&
           (int64_t)behavior_scalar_bits(*value) == expected;
}
/* T(value) fails when an integer does not fit; wrap and clamp choose the bits. */
TEST_CASE(program_integer_conversions_check_wrap_and_clamp)
{
    const char
        *source = "Convert :: behavior {\n"
                  "  wide: int = -300; wrapped: int = 0; clamped: int = 0;\n"
                  "  mixed: int64 = 0; narrow: byte = 0;\n"
                  "  same: bool = true; below: bool = false; small: int = 0;\n"
                  "  on Impulse :: (enabled: bool) {\n"
                  "    wrapped = byte(wrap: wide); clamped = byte(clamp: "
                  "-wide);\n"
                  "    mixed = wide + uint(7);\n"
                  "    same = wide == uint(0); below = wide < uint(7);\n"
                  "    small = byte(200) + int8(-100);\n"
                  "    if (enabled) { narrow = byte(wide); }\n"
                  "  }\n"
                  "}";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        /* The engine catalog's Byte literal must not shadow byte(value). */
        REQUIRE(compile_pack(source, backends[b], &NATIVE_ARITHMETIC_DEFINITION_PACK));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        pin_value args[] = { { .type = PIN_F64, .d = 1 },
                             { .type = PIN_BOOL, .b = false } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) >= 0);
        REQUIRE(int_state(instance, "wrapped", PIN_I32, 212));
        REQUIRE(int_state(instance, "clamped", PIN_I32, 255));
        REQUIRE(int_state(instance, "mixed", PIN_I64, -293));
        REQUIRE(
            int_state(instance, "same", PIN_BOOL, 0) &&
            int_state(instance, "below", PIN_BOOL, 1)
        );
        REQUIRE(int_state(instance, "small", PIN_I32, 100));
        args[1].b = true;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 1, 0, 1000) < 0);
        const execution_program_failure *failure = execution_program_error(program);
        int start = (int)(strstr(source, "byte(wide)") - source);
        REQUIRE(failure && failure->kind == BEHAVIOR_FUNCTION_DATA);
        REQUIRE(!strcmp(failure->message, "Value does not fit in byte"));
        REQUIRE(failure->start == start && failure->end == start + (int)strlen("byte(wide)"));
        REQUIRE(int_state(instance, "narrow", PIN_U8, 0));
        execution_program_free(program);
        program = NULL;
    }
    return true;
}
/* char holds one Unicode scalar: char(n) and char arithmetic fail outside one. */
TEST_CASE(program_char_conversions_and_arithmetic_stay_unicode_scalars)
{
    const char *source = "Letters :: behavior {\n"
                         "  code: int = 0; next: int = 0; digit: int = 0;\n"
                         "  step: int = 0; letter: char = 'a';\n"
                         "  on Impulse :: (enabled: bool) {\n"
                         "    letter = char(0x1F408); code = letter;\n"
                         "    next = letter + 1; digit = '7' - '0';\n"
                         "    if (enabled) { letter = char(step + 0xD800); }\n"
                         "    if (!enabled) { letter = char(0x10FFFF) + 1; }\n"
                         "  }\n"
                         "}";
    const char *const failing[] = { "char(step + 0xD800)", "char(0x10FFFF) + 1" };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        for (int c = 0; c < 2; c++) {
            prepare();
            REQUIRE(compile_pack(source, backends[b], &NATIVE_ARITHMETIC_DEFINITION_PACK));
            ecs_entity owner = ecs_world_create_entity(world);
            execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
            REQUIRE(instance != NULL);
            pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                 { .type = PIN_BOOL, .b = c == 0 } };
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) < 0);
            const execution_program_failure *failure = execution_program_error(program);
            int start = (int)(strstr(source, failing[c]) - source);
            REQUIRE(failure && failure->kind == BEHAVIOR_FUNCTION_DATA);
            REQUIRE(!strcmp(failure->message, "Value does not fit in char"));
            REQUIRE(failure->start == start);
            REQUIRE(int_state(instance, "code", PIN_I32, 0x1F408));
            REQUIRE(int_state(instance, "next", PIN_I32, 0x1F409));
            REQUIRE(int_state(instance, "digit", PIN_I32, 7));
            REQUIRE(int_state(instance, "letter", PIN_I32, 0x1F408));
            execution_program_free(program);
            program = NULL;
        }
    }
    return true;
}
/* A char beside an int compares as an int, and ASCII steps pass the scalar
 * check, whose surrogate test wraps below 0xD800; -c is no scalar. */
TEST_CASE(program_char_compares_with_ints_and_steps_below_surrogates)
{
    const char
        *source = "Steps :: behavior {\n"
                  "  below: bool = false; same: bool = true; code: int = 0;\n"
                  "  minus: int = -1; letter: char = 'a';\n"
                  "  on Impulse :: (enabled: bool) {\n"
                  "    below = minus < letter; same = minus == letter;\n"
                  "    letter += 1; letter = letter + 1; code = letter;\n"
                  "    if (enabled) { letter = -letter; }\n"
                  "  }\n"
                  "}";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        for (int failing = 0; failing < 2; failing++) {
            prepare();
            REQUIRE(compile_pack(source, backends[b], &NATIVE_ARITHMETIC_DEFINITION_PACK));
            ecs_entity owner = ecs_world_create_entity(world);
            execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
            REQUIRE(instance != NULL);
            pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                 { .type = PIN_BOOL, .b = failing } };
            int status = execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000);
            REQUIRE(failing ? status < 0 : status >= 0);
            REQUIRE(int_state(instance, "below", PIN_BOOL, 1));
            REQUIRE(int_state(instance, "same", PIN_BOOL, 0));
            REQUIRE(int_state(instance, "code", PIN_I32, 'c'));
            if (failing) {
                const execution_program_failure *failure = execution_program_error(program);
                REQUIRE(failure && !strcmp(failure->message, "Value does not fit in char"));
                REQUIRE(failure->start == (int)(strstr(source, "-letter") - source));
            }
            execution_program_free(program);
            program = NULL;
        }
    }
    return true;
}
/* `..<` stops before its last bound and `for <` counts down, over ranges and
 * arrays alike; neither wraps at the int bounds or runs an empty range. */
TEST_CASE(program_loops_count_half_open_and_in_reverse)
{
    const char *
        source = "Loops :: behavior {\n"
                 "  up: int = 0; half: int = 0; down: int = 0; back: int = 0;\n"
                 "  empty: int = 0; skips: int = 0; items: int = 0; order: int "
                 "= 0;\n"
                 "  edges: int = 0;\n"
                 "  on Impulse :: () {\n"
                 "    for i in 1..4 { up = up * 10 + i; }\n"
                 "    for i in 1..<4 { half = half * 10 + i; }\n"
                 "    for < i in 1..4 { down = down * 10 + i; }\n"
                 "    for < i in 1..<4 { back = back * 10 + i; }\n"
                 "    n: int = 0;\n"
                 "    for i in 0..<n { empty += 1; }\n"
                 "    for < i in 0..<n { empty += 1; }\n"
                 "    for < i in 1..4 { if i == 3 { continue; } skips = skips "
                 "* 10 + i; }\n"
                 "    xs: int[] = [1, 2, 3];\n"
                 "    for < x, i in xs { items = items * 10 + x; order = order "
                 "* 10 + i; }\n"
                 "    for < i in -2147483648..-2147483647 { edges += 1; }\n"
                 "    for i in 2147483646..<2147483647 { edges += 1; }\n"
                 "  }\n"
                 "}";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_pack(source, backends[b], &NATIVE_ARITHMETIC_DEFINITION_PACK));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        pin_value args[] = { { .type = PIN_F64, .d = 1 },
                             { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) >= 0);
        REQUIRE(int_state(instance, "up", PIN_I32, 1234));
        REQUIRE(int_state(instance, "half", PIN_I32, 123));
        REQUIRE(int_state(instance, "down", PIN_I32, 4321));
        REQUIRE(int_state(instance, "back", PIN_I32, 321));
        REQUIRE(int_state(instance, "empty", PIN_I32, 0));
        REQUIRE(int_state(instance, "skips", PIN_I32, 421));
        REQUIRE(int_state(instance, "items", PIN_I32, 321));
        REQUIRE(int_state(instance, "order", PIN_I32, 210));
        REQUIRE(int_state(instance, "edges", PIN_I32, 3));
        execution_program_free(program);
        program = NULL;
    }
    return true;
}
/* A write outside a local's array fails at the assignment, as other writes do. */
/* Suspending mid-build collects the payload while the local keeps its grown
 * items: the collection keeps the current buffer and reclaims the outgrown
 * ones, and the appends after the resume continue in place. */
TEST_CASE(program_local_array_survives_collection_mid_build)
{
    const char *source = "Grow :: behavior {\n"
                         "  total: double = 0;\n"
                         "  on Impulse :: () {\n"
                         "    xs: double[] = [];\n"
                         "    i: double = 0;\n"
                         "    while (i < 200) { xs = append(xs, i); i += 1; }\n"
                         "    await Delay(1);\n"
                         "    while (i < 400) { xs = append(xs, i); i += 1; }\n"
                         "    xs[0] = 1000;\n"
                         "    total = xs[0] + xs[199] + xs[399] + length(xs);\n"
                         "  }\n"
                         "}";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_pack(source, backends[b], &NATIVE_ARRAY_DEFINITION_PACK));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        uint64_t marks = arena_collect_marks();
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000000) == FLOW_OK);
        REQUIRE(execution_instance_pending(instance) == 1);
        execution_memory_usage suspended = execution_program_memory_usage(program);
        /* 200 doubles fill a packed 256-item buffer; the outgrown 8- to
         * 128-item buffers would hold 248 more. */
        REQUIRE(arena_collect_marks() > marks);
        REQUIRE(suspended.payload_bytes >= 256 * sizeof(double));
        REQUIRE(suspended.payload_bytes < (256 + 248) * sizeof(double));
        REQUIRE(execution_program_update(program, 1, 0, 1000000) == FLOW_OK);
        REQUIRE(number_state(instance, "total", 1000 + 199 + 399 + 400));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}
TEST_CASE(program_local_array_store_checks_its_range)
{
    const char *source = "Store :: behavior {\n"
                         "  first: int = 0;\n"
                         "  on Impulse :: (enabled: bool) {\n"
                         "    xs: int[] = [];\n"
                         "    xs = append(xs, int(1));\n"
                         "    i: int = 0;\n"
                         "    if (enabled) { i = 3; }\n"
                         "    xs[i] = 5;\n"
                         "    first = xs[0];\n"
                         "  }\n"
                         "}";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_pack(source, backends[b], &NATIVE_ARRAY_DEFINITION_PACK));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        pin_value args[] = { { .type = PIN_F64, .d = 1 },
                             { .type = PIN_BOOL, .b = false } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) >= 0);
        REQUIRE(int_state(instance, "first", PIN_I32, 5));
        args[1].b = true;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 1, 0, 1000) < 0);
        const execution_program_failure *failure = execution_program_error(program);
        int start = (int)(strstr(source, "xs[i] = 5") - source);
        REQUIRE(failure && !strcmp(failure->message, "Array index out of range"));
        REQUIRE(failure->start == start && failure->end == start + (int)strlen("xs[i] = 5;"));
        execution_program_free(program);
        program = NULL;
    }
    return true;
}
/* Every array function is a method on its first argument. `append` adds an
 * item and `extend` an array's items, and a method that changes its array
 * does so in place as a statement on a name. */
TEST_CASE(program_array_methods_change_their_array)
{
    const char *source = "Methods :: behavior {\n"
                         "  total: double = 0;\n"
                         "  count: int = 0;\n"
                         "  on Impulse :: () {\n"
                         "    xs: double[] = [];\n"
                         "    i: double = 0;\n"
                         "    while (i < 300) { xs.append(i); i += 1; }\n"
                         "    ys: double[] = [1000, 2000];\n"
                         "    xs.extend(ys);\n"
                         "    xs.reverse();\n"
                         "    count = xs.length();\n"
                         "    total = xs[0] + xs[1] + xs[301];\n"
                         "  }\n"
                         "}";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_pack(source, backends[b], &NATIVE_ARRAY_DEFINITION_PACK));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000000) == FLOW_OK);
        REQUIRE(int_state(instance, "count", PIN_I32, 302));
        REQUIRE(number_state(instance, "total", 2000 + 1000 + 0));
    }
    const char *changed = "Changed :: behavior {\n"
                          "  on Impulse :: () {\n"
                          "    xs: double[] = [];\n"
                          "    ys := xs.append(1);\n"
                          "    xs = xs.append(1);\n"
                          "    xs.array_add(1);\n"
                          "    xs.ArrayAdd(1);\n"
                          "  }\n"
                          "}";
    prepare();
    REQUIRE(!compile_pack(changed, BEHAVIOR_BYTECODE, &NATIVE_ARRAY_DEFINITION_PACK));
    int statements = 0, unknown = 0;
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        statements += strstr(analysis->diagnostics[i].message, "append changes the array") !=
                      NULL;
        unknown += strstr(analysis->diagnostics[i].message, "Unknown callable 'xs.") !=
                   NULL;
    }
    REQUIRE(statements == 2 && unknown == 2);
    return true;
}
TEST_CASE(program_integer_and_nominal_enum_literals_preserve_exact_bits)
{
    const char
        *source = "Exact :: behavior { low:int64=0; high:int64=0; "
                  "unsigned_max:uint64=0; precise:uint64=0; mode:Mode=1; on "
                  "Tick :: () { low=-9223372036854775808; "
                  "high=9223372036854775807; "
                  "unsigned_max=18446744073709551615; "
                  "precise=9007199254740993; mode=2147483647; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    const char *const names[] = { "low", "high", "unsigned_max", "precise", "mode" };
    script_catalog_type types[3];
    script_catalog exact_catalog = catalog;
    exact_catalog.types = types;
    exact_catalog.type_count = 3;
    for (int b = 0; b < 2; b++) {
        prepare();
        memcpy(types, script_types, sizeof script_types);
        types[2] = (script_catalog_type){ .id = 103,
                                          .name = "Mode",
                                          .size = sizeof(int32_t),
                                          .type = { .storage = PIN_ENUM,
                                                    .nominal = 103 } };
        syntax = script_parse(source);
        analysis = script_analyze(syntax, &exact_catalog);
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            fprintf(
                stderr,
                "exact literal semantic: %s\n",
                analysis->diagnostics[i].message
            );
        }
        REQUIRE(analysis->valid && !syntax->diagnostic_count);
        uint64_t ids[5];
        for (int i = 0; i < 5; i++) {
            ids[i] = state_id(names[i]);
            REQUIRE(ids[i] != 0);
        }
        const script_lower_bindings bindings = { .global = global, .host = host };
        owned_plan = script_lower(analysis, &exact_catalog, &bindings);
        REQUIRE(owned_plan && owned_plan->valid);
        char error[256];
        module = execution_module_finish(&owned_plan, backends[b], error, sizeof error);
        if (!module) {
            fprintf(stderr, "exact literal publication: %s\n", error);
        }
        REQUIRE(module && !owned_plan);
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        ecs_entity entity = ecs_world_create_entity(world);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(entity));
        REQUIRE(instance);
        const pin_value *mode = execution_instance_state(instance, ids[4]);
        REQUIRE(
            mode && mode->type == PIN_ENUM && mode->e == 1 && mode->type_name &&
            !strcmp(mode->type_name, "Mode")
        );
        const pin_value delta = { .type = PIN_F64 };
        REQUIRE(execution_instance_dispatch(instance, 10, &delta, 1, 0, 0, 1000) == FLOW_OK);
        const pin_value *low = execution_instance_state(instance, ids[0]),
                        *high = execution_instance_state(instance, ids[1]),
                        *unsigned_max = execution_instance_state(instance, ids[2]),
                        *precise = execution_instance_state(instance, ids[3]);
        mode = execution_instance_state(instance, ids[4]);
        REQUIRE(low && low->type == PIN_I64 && low->i64 == INT64_MIN);
        REQUIRE(high && high->type == PIN_I64 && high->i64 == INT64_MAX);
        REQUIRE(unsigned_max && unsigned_max->type == PIN_U64 && unsigned_max->u64 == UINT64_MAX);
        REQUIRE(precise && precise->type == PIN_U64 && precise->u64 == UINT64_C(9007199254740993));
        REQUIRE(
            mode && mode->type == PIN_ENUM && mode->e == INT32_MAX &&
            mode->type_name && !strcmp(mode->type_name, "Mode")
        );
    }
    const char *const invalid[] = {
        "Bad :: behavior { amount:int64=9223372036854775808; }",
        "Bad :: behavior { amount:int64=-9223372036854775809; }",
        "Bad :: behavior { amount:uint64=18446744073709551616; }",
        "Bad :: behavior { amount:uint64=-1; }",
        "Bad :: behavior { amount:Mode=2147483648; }",
    };
    for (int i = 0; i < 5; i++) {
        prepare();
        syntax = script_parse(invalid[i]);
        analysis = script_analyze(syntax, &exact_catalog);
        REQUIRE(!syntax->diagnostic_count);
        REQUIRE(!analysis->valid && analysis->diagnostic_count > 0);
        REQUIRE(!module && !program);
    }
    return true;
}

TEST_CASE(program_concrete_assignments_preserve_name_byte_and_handle_storage)
{
    const char
        *source = "Copies :: behavior { source_byte:byte=255; "
                  "copied_byte:byte=0; source_name:name=\"token\"; "
                  "copied_name:name=\"\"; source_handle:Entity=0; "
                  "copied_handle:Entity=0; source_big:uint64=9007199254740993; "
                  "copied_big:uint64=0; count:int=0; on Tick :: () { "
                  "copied_byte=source_byte; copied_name=source_name; "
                  "copied_handle=source_handle; copied_big=source_big; "
                  "count+=1; count+=1; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    const char *names[] = { "copied_byte", "copied_name", "copied_handle", "copied_big", "count" };
    for (int b = 0; b < 2; b++) {
        prepare();
        script_catalog_type types[3];
        memcpy(types, script_types, sizeof script_types);
        types[2] = (script_catalog_type){ .id = 700,
                                          .name = "ecs.entity",
                                          .source_name = "Entity",
                                          .size = sizeof(uint64_t),
                                          .type = { .storage = PIN_HANDLE,
                                                    .nominal = 700 } };
        script_catalog concrete = catalog;
        concrete.types = types;
        concrete.type_count = 3;
        syntax = script_parse(source);
        analysis = script_analyze(syntax, &concrete);
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            fprintf(
                stderr,
                "concrete copy semantic: %s\n",
                analysis->diagnostics[i].message
            );
        }
        REQUIRE(analysis->valid && !syntax->diagnostic_count);
        uint64_t ids[5];
        uint64_t source_handle = state_id("source_handle");
        REQUIRE(source_handle);
        for (int i = 0; i < 5; i++) {
            ids[i] = state_id(names[i]);
            REQUIRE(ids[i]);
        }
        const script_lower_bindings bindings = { .global = global, .host = host };
        owned_plan = script_lower(analysis, &concrete, &bindings);
        REQUIRE(owned_plan && owned_plan->valid);
        char error[192];
        module = execution_module_finish(&owned_plan, backends[b], error, sizeof error);
        if (!module) {
            fprintf(stderr, "concrete copy publication: %s\n", error);
        }
        REQUIRE(module && !owned_plan);
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        ecs_entity owner = ecs_world_create_entity(world);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        uint64_t handle = ecs_entity_to_u64(owner);
        REQUIRE(handle != 0);
        execution_instance *instance = execution_program_attach(program, handle);
        REQUIRE(instance);
        REQUIRE(
            execution_instance_set_state(
                instance,
                source_handle,
                (pin_value){ .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = UINT64_MAX }
            ) == FLOW_OK
        );
        const pin_value dt = { .type = PIN_F64 };
        REQUIRE(execution_instance_dispatch(instance, 10, &dt, 1, 0, 0, 1000) == FLOW_OK);
        const pin_value *byte = execution_instance_state(instance, ids[0]),
                        *name = execution_instance_state(instance, ids[1]),
                        *entity = execution_instance_state(instance, ids[2]),
                        *big = execution_instance_state(instance, ids[3]);
        REQUIRE(byte && byte->type == PIN_U8 && byte->u8 == UINT8_MAX);
        REQUIRE(name && name->type == PIN_NAME && name->str && !strcmp(name->str, "token"));
        REQUIRE(
            entity && entity->type == PIN_HANDLE && entity->u64 == UINT64_MAX &&
            entity->type_name && !strcmp(entity->type_name, "ecs.entity")
        );
        REQUIRE(big && big->type == PIN_U64 && big->u64 == UINT64_C(9007199254740993));
        const pin_value *count = execution_instance_state(instance, ids[4]);
        REQUIRE(count && count->type == PIN_I32 && count->i == 2);
    }
    return true;
}

static const char *native_float(arena *, const pin_value *, int count, pin_value *out)
{
    assert(count == 0);
    *out = (pin_value){ .type = PIN_F32, .f = 3.25f };
    return NULL;
}
static bool float_host(void *user, uint64_t id, script_lower_host *out)
{
    static const node_host_call call = { .value = native_float };
    if (id != 70) {
        return host(user, id, out);
    }
    *out = (script_lower_host){ .call = &call };
    return true;
}
TEST_CASE(program_source_float_width_conversion_preserves_native_input)
{
    const char
        *source = "Widths :: behavior { widened:double=0; narrowed:float=0; on "
                  "Tick :: () { widened=double(NativeFloat()); "
                  "narrowed=float(widened+0.25); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    static const script_catalog_parameter result = { .name = "amount",
                                                     .type = { .storage = PIN_F32 } };
    enum
    {
        COUNT = sizeof capabilities / sizeof *capabilities
    };
    script_catalog_capability caps[COUNT + 1];
    memcpy(caps, capabilities, sizeof capabilities);
    caps[COUNT] = (script_catalog_capability){ .id = 70,
                                               .name = "NativeFloat",
                                               .kind = SCRIPT_CAPABILITY_FUNCTION,
                                               .results = &result,
                                               .result_count = 1,
                                               .completion_outcome = -1,
                                               .effects = SCRIPT_EFFECT_HOST };
    script_catalog floats = catalog;
    floats.capabilities = caps;
    floats.capability_count = COUNT + 1;
    for (int b = 0; b < 2; b++) {
        prepare();
        syntax = script_parse(source);
        analysis = script_analyze(syntax, &floats);
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            fprintf(
                stderr,
                "float conversion semantic: %s\n",
                analysis->diagnostics[i].message
            );
        }
        REQUIRE(analysis->valid && !syntax->diagnostic_count);
        uint64_t widened_id = state_id("widened"), narrowed_id = state_id("narrowed");
        REQUIRE(widened_id && narrowed_id);
        const script_lower_bindings bindings = { .global = global, .host = float_host };
        owned_plan = script_lower(analysis, &floats, &bindings);
        REQUIRE(owned_plan && owned_plan->valid);
        char error[192];
        module = execution_module_finish(&owned_plan, backends[b], error, sizeof error);
        if (!module) {
            fprintf(stderr, "float conversion publication: %s\n", error);
        }
        REQUIRE(module && !owned_plan);
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        ecs_entity owner = ecs_world_create_entity(world);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        const pin_value dt = { .type = PIN_F64 };
        REQUIRE(execution_instance_dispatch(instance, 10, &dt, 1, 0, 0, 1000) == FLOW_OK);
        const pin_value *widened = execution_instance_state(instance, widened_id),
                        *narrowed = execution_instance_state(instance, narrowed_id);
        REQUIRE(widened && widened->type == PIN_F64 && widened->d == 3.25);
        REQUIRE(narrowed && narrowed->type == PIN_F32 && narrowed->f == 3.5f);
    }
    return true;
}

TEST_CASE(program_int_to_float_compare_and_cast_run_on_vm_and_sljit)
{
    const char
        *source = "Mixed :: behavior { count:int=3; level:float=2.5; "
                  "below:bool=false; scaled:float=0; on Tick :: () { below = "
                  "level < count; scaled = float(count); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        syntax = script_parse(source);
        analysis = script_analyze(syntax, &catalog);
        REQUIRE(analysis->valid && !syntax->diagnostic_count);
        uint64_t below_id = state_id("below"), scaled_id = state_id("scaled");
        REQUIRE(below_id && scaled_id);
        const script_lower_bindings bindings = { .global = global };
        owned_plan = script_lower(analysis, &catalog, &bindings);
        REQUIRE(owned_plan && owned_plan->valid);
        char error[192];
        module = execution_module_finish(&owned_plan, backends[b], error, sizeof error);
        if (!module) {
            fprintf(stderr, "mixed numeric publication: %s\n", error);
        }
        REQUIRE(module && !owned_plan);
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        ecs_entity owner = ecs_world_create_entity(world);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        const pin_value dt = { .type = PIN_F64 };
        REQUIRE(execution_instance_dispatch(instance, 10, &dt, 1, 0, 0, 1000) == FLOW_OK);
        const pin_value *below = execution_instance_state(instance, below_id),
                        *scaled = execution_instance_state(instance, scaled_id);
        REQUIRE(below && below->type == PIN_BOOL && below->b);
        REQUIRE(scaled && scaled->type == PIN_F32 && scaled->f == 3.0f);
    }
    return true;
}
TEST_CASE(program_state_actions_demand_inputs_route_and_preserve_per_owner_state)
{
    const char
        *source = "StateMachines :: behavior { evaluated:double=0; "
                  "order:double=0; once_hits:double=0; count_hits:double=0; "
                  "flips_a:double=0; flips_b:double=0; gate_hits:double=0; "
                  "multi_a:double=0; multi_b:double=0; counter:double=-1; "
                  "Closed :: ()->(Value:bool){evaluated+=1;return(false);} "
                  "Limit :: ()->(Value:int){evaluated+=10;return(2);} Start :: "
                  "()->(Value:int){order=order*10+3;return(0);} Random :: "
                  "()->(Value:bool){order=order*10+2;return(false);} Loop :: "
                  "()->(Value:bool){order=order*10+1;return(false);} action "
                  "once=do_once(Start_Closed:Closed()); action "
                  "count=do_n(N:Limit()); action flip=flip_flop(); action "
                  "door=gate(Start_Closed:Closed()); action "
                  "multi=multi_gate(Loop:Loop(),Random:Random(),Start_Index:"
                  "Start()); on Impulse :: (enabled: bool) {  if enabled "
                  "{once();count();counter=count.Counter;flip();door();order=0;"
                  "multi();}  else "
                  "{once.Reset();count.Reset();door.Close();multi.Reset();} } "
                  "after once.Completed{once_hits+=1;} after "
                  "count.Exit{count_hits+=1;} after flip.A{flips_a+=1;} after "
                  "flip.B{flips_b+=1;} after door.Exit{gate_hits+=1;} after "
                  "multi.Out0{multi_a+=1;} after multi.Out1{multi_b+=1;}}";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_pack(source, backends[b], &NATIVE_STATE_DEFINITION_PACK));
        ecs_entity owners[] = { ecs_world_create_entity(world),
                                ecs_world_create_entity(world) };
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        execution_instance
            *first = execution_program_attach(program, ecs_entity_to_u64(owners[0]));
        execution_instance *
            second = execution_program_attach(program, ecs_entity_to_u64(owners[1]));
        REQUIRE(first && second);
        pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL, .b = true } };
        for (int i = 0; i < 3; i++) {
            REQUIRE(execution_instance_dispatch(first, 11, args, 2, i, 0, 1000) == FLOW_OK);
            REQUIRE(number_state(first, "evaluated", 12 + i * 10));
            REQUIRE(number_state(first, "order", 321));
        }
        REQUIRE(number_state(first, "once_hits", 1));
        REQUIRE(number_state(first, "count_hits", 2) && number_state(first, "counter", 2));
        REQUIRE(number_state(first, "flips_a", 2) && number_state(first, "flips_b", 1));
        REQUIRE(number_state(first, "gate_hits", 3));
        REQUIRE(number_state(first, "multi_a", 1) && number_state(first, "multi_b", 1));
        REQUIRE(execution_instance_dispatch(second, 11, args, 2, 3, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(second, "evaluated", 12) && number_state(second, "multi_a", 1));
        REQUIRE(execution_program_replace(program, module, false) == FLOW_OK);
        first = execution_program_instance(program, ecs_entity_to_u64(owners[0]));
        REQUIRE(first);
        REQUIRE(execution_instance_dispatch(first, 11, args, 2, 4, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(first, "evaluated", 42) && number_state(first, "once_hits", 1));
        REQUIRE(number_state(first, "count_hits", 2) && number_state(first, "flips_b", 2));
        REQUIRE(number_state(first, "multi_a", 1) && number_state(first, "multi_b", 1));
        args[1].b = false;
        REQUIRE(execution_instance_dispatch(first, 11, args, 2, 5, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(first, "evaluated", 42) && number_state(first, "order", 321));
        args[1].b = true;
        REQUIRE(execution_instance_dispatch(first, 11, args, 2, 6, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(first, "evaluated", 52) && number_state(first, "once_hits", 2));
        REQUIRE(number_state(first, "count_hits", 3) && number_state(first, "counter", 0));
        REQUIRE(number_state(first, "gate_hits", 4));
        REQUIRE(number_state(first, "multi_a", 2) && number_state(first, "multi_b", 1));
        REQUIRE(execution_program_replace(program, module, true) == FLOW_OK);
        first = execution_program_instance(program, ecs_entity_to_u64(owners[0]));
        REQUIRE(first);
        REQUIRE(execution_instance_dispatch(first, 11, args, 2, 7, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(first, "evaluated", 12) && number_state(first, "once_hits", 1));
        REQUIRE(number_state(first, "count_hits", 1) && number_state(first, "flips_a", 1));
        REQUIRE(number_state(first, "multi_a", 1) && number_state(first, "multi_b", 0));
    }
    return true;
}
TEST_CASE(program_retrigger_evaluates_each_duration_once)
{
    const char
        *source = "Retriggers :: behavior { evaluations:double=0; "
                  "completions:double=0; Duration :: "
                  "()->(seconds:double){evaluations+=1;return(3);} action "
                  "pending=retriggerable_delay(Duration:Duration()); on "
                  "Impulse :: (){pending();} after "
                  "pending.Completed{completions+=1;} }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_pack(source, backends[b], &NATIVE_STATE_DEFINITION_PACK));
        ecs_entity owner = ecs_world_create_entity(world);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        pin_value args[] = { { .type = PIN_F64, .d = 1 }, { .type = PIN_BOOL } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        args[0].d = 2;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 1, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "evaluations", 2));
        REQUIRE(execution_program_update(program, 3, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "completions", 0));
        REQUIRE(execution_program_update(program, 3.5, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "completions", 0));
        REQUIRE(execution_program_update(program, 4, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "completions", 1));
        args[0].d = 7;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 5, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "evaluations", 3));
        execution_program_detach(program, ecs_entity_to_u64(owner));
        instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        REQUIRE(execution_program_update(program, 9, 0, 1000) == FLOW_OK);
        REQUIRE(
            number_state(instance, "completions", 0) &&
            number_state(instance, "evaluations", 0)
        );
    }
    return true;
}

/* How many bytes of text before `offset` are not whitespace. */
static int solid_count(const char *text, int offset)
{
    int count = 0;
    for (int i = 0; i < offset; i++) {
        count += !strchr(" \t\n\r", text[i]);
    }
    return count;
}
/* Each record's occurrence is the same after whitespace-only formatting, and
 * script_occurrence gives it from the source alone: its key, the record's
 * kind and its range counted in bytes that are not whitespace. */
TEST_CASE(program_occurrences_survive_whitespace_and_follow_the_source)
{
    static const char tight[] = "Debugger :: behavior { calls:double=0; "
                                "observed:double=0; Work :: "
                                "()->(Value:double,Text:string){calls+=1;"
                                "return(calls,\"a b\");} "
                                "on Tick :: () { values:=Work(); "
                                "observed=values.Value; observed+=10; } }";
    static const char loose
        [] = "Debugger :: behavior {\n    calls: double = 0;\n    observed: "
             "double = 0;\n"
             "    Work :: () -> (Value: double, Text: string) {\n        calls "
             "+= 1;\n"
             "        return (calls, \"a b\");\n    }\n\n    on Tick :: () {\n"
             "        values := Work();\n        observed = values.Value;\n"
             "        observed += 10;\n    }\n}\n";
    script_syntax *syntaxes[2] = { script_parse(tight), script_parse(loose) };
    script_analysis *analyses[2] = { script_analyze(syntaxes[0], &catalog),
                                     script_analyze(syntaxes[1], &catalog) };
    script_occurrences *occurrences[2] = { script_occurrences_new(analyses[0]),
                                           script_occurrences_new(analyses[1]) };
    const script_analysis *a = analyses[0], *b = analyses[1];
    bool alike = a->valid && b->valid && a->statement_count == b->statement_count &&
                 a->expression_count == b->expression_count &&
                 a->symbol_count == b->symbol_count &&
                 a->function_count == b->function_count;
    const struct
    {
        script_record_kind kind;
        int count;
    } kinds[] = { { SCRIPT_RECORD_STATEMENT, a->statement_count },
                  { SCRIPT_RECORD_EXPRESSION, a->expression_count },
                  { SCRIPT_RECORD_SYMBOL, a->symbol_count },
                  { SCRIPT_RECORD_FUNCTION, a->function_count } };
    for (int k = 0; alike && k < 4; k++) {
        for (int i = 0; alike && i < kinds[k].count; i++) {
            uint64_t id = script_record_id(occurrences[0], kinds[k].kind, i);
            alike = id && id == script_record_id(occurrences[1], kinds[k].kind, i);
        }
    }
    for (int i = 0; alike && i < a->statement_count; i++) {
        const script_range r = a->statements[i].range;
        uint64_t id = script_record_id(occurrences[0], SCRIPT_RECORD_STATEMENT, i);
        alike = id == script_occurrence(
                          a->identity,
                          SCRIPT_RECORD_STATEMENT,
                          solid_count(tight, r.start),
                          solid_count(tight, r.end)
                      );
        for (int j = 0; alike && j < i; j++) {
            const script_range other = a->statements[j].range;
            alike = (other.start == r.start && other.end == r.end) ||
                    id != script_record_id(occurrences[0], SCRIPT_RECORD_STATEMENT, j);
        }
    }
    for (int i = 0; i < 2; i++) {
        script_occurrences_free(occurrences[i]);
        script_analysis_free(analyses[i]);
        script_syntax_free(syntaxes[i]);
    }
    REQUIRE(alike);
    return true;
}

TEST_CASE(program_debugger_steps_without_replaying_and_retains_owned_watches)
{
    const char *source = "Debugger :: behavior { calls:double=0; "
                         "observed:double=0; text:string=\"unset\"; Work :: "
                         "()->(Value:double,Text:string){calls+=1;return(calls,"
                         "\"snapshot\");} on Tick :: () { values:=Work(); "
                         "observed=values.Value; text=values.Text; await "
                         "Delay(1); observed+=10; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(compile_module(source, backends[b]));
        world = ecs_world_new();
        REQUIRE(world);
        ecs_entity entity = ecs_world_create_entity(world);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        uint64_t owner = ecs_entity_to_u64(entity);
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        execution_instance *instance = execution_program_attach(program, owner);
        REQUIRE(instance);
        debugger = execution_program_inspector(program);
        REQUIRE(debugger);
        REQUIRE(execution_inspector_select_owner(debugger, owner));
        REQUIRE(execution_inspector_owner_count(debugger) == 1);
        REQUIRE(execution_inspector_owner_at(debugger, 0) == owner);
        uint64_t handler = 0, call = 0;
        for (int n = 0; n < execution_module_function_count(module); n++) {
            execution_module_function_info f = execution_module_function_at(module, n);
            if (f.event == 10) {
                handler = f.id;
            }
        }
        for (int n = 0; n < analysis->expression_count; n++) {
            const script_expression *e = &analysis->expressions[n];
            if (e->kind == SCRIPT_EXPR_CALL && e->right >= 0) {
                script_occurrences *occurrences = script_occurrences_new(analysis);
                call = script_record_id(occurrences, SCRIPT_RECORD_EXPRESSION, n);
                script_occurrences_free(occurrences);
            }
        }
        REQUIRE(handler && call);
        REQUIRE(
            execution_inspector_breakpoint(debugger, execution_module_id(module), handler, call, true)
        );
        pin_value tick = { .type = PIN_F64 };
        REQUIRE(execution_instance_dispatch(instance, 10, &tick, 1, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_paused(program));
        execution_debug_cursor first = execution_inspector_cursor(debugger);
        REQUIRE(first.status == FLOW_PAUSED_RESULT);
        REQUIRE(first.owner == owner && first.point.occurrence == call && first.depth == 0);
        REQUIRE(number_state(instance, "calls", 0));
        REQUIRE(execution_program_update(program, 0.5, 0, 1000) >= FLOW_OK);
        REQUIRE(number_state(instance, "calls", 0));
        REQUIRE(
            execution_inspector_breakpoint(debugger, execution_module_id(module), handler, call, false)
        );
        REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_INTO) == FLOW_PAUSED_RESULT);
        execution_debug_cursor child = execution_inspector_cursor(debugger);
        REQUIRE(child.run == first.run && child.step > first.step && child.depth == 1);
        REQUIRE(execution_inspector_frame_count(debugger) == 2);
        REQUIRE(number_state(instance, "calls", 0));
        REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_OUT) == FLOW_PAUSED_RESULT);
        REQUIRE(execution_inspector_cursor(debugger).depth == 0);
        REQUIRE(number_state(instance, "calls", 1));
        REQUIRE(number_state(instance, "observed", 0));
        for (int n = 0; n < 16 && !number_state(instance, "observed", 1); n++) {
            REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_OVER) == FLOW_PAUSED_RESULT);
            REQUIRE(number_state(instance, "calls", 1));
        }
        REQUIRE(number_state(instance, "observed", 1));
        REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_RESUME) == FLOW_SUSPENDED_RESULT);
        REQUIRE(number_state(instance, "calls", 1));
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "observed", 11));
        REQUIRE(execution_inspector_pause(debugger, owner));
        REQUIRE(execution_instance_dispatch(instance, 10, &tick, 1, 2, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "calls", 1));
        REQUIRE(execution_program_paused(program));
        REQUIRE(execution_inspector_cursor(debugger).status == FLOW_PAUSED_RESULT);
        REQUIRE(execution_inspector_cursor(debugger).run > first.run);
        REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_RESUME) == FLOW_SUSPENDED_RESULT);
        REQUIRE(number_state(instance, "calls", 2));
        REQUIRE(execution_program_update(program, 3, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "observed", 12));
        int saved = -1;
        for (int n = 0; n < execution_inspector_watch_count(debugger); n++) {
            const execution_debug_watch *w = execution_inspector_watch_at(debugger, n);
            if (w->availability == EXECUTION_WATCH_AVAILABLE && w->value.type == PIN_STR &&
                w->value.str && !strcmp(w->value.str, "snapshot")) {
                saved = n;
            }
        }
        REQUIRE(saved >= 0);
        REQUIRE(execution_inspector_activity_count(debugger) > 0);
        REQUIRE(execution_inspector_activity_dropped(debugger) == 0);
        execution_debug_cursor selected;
        REQUIRE(execution_inspector_owner_cursor(debugger, owner, &selected));
        int stack_count = execution_inspector_owner_frame_count(debugger, owner);
        REQUIRE(stack_count > 0);
        execution_debug_frame frame = execution_inspector_owner_frame_at(debugger, owner, 0);
        execution_program_detach(program, owner);
        REQUIRE(execution_inspector_owner_cursor(debugger, owner, &selected));
        REQUIRE(selected.retired);
        uint64_t selected_step = selected.step;
        ecs_entity sibling = ecs_world_create_entity(world);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        uint64_t sibling_owner = ecs_entity_to_u64(sibling);
        execution_instance *other = execution_program_attach(program, sibling_owner);
        REQUIRE(other);
        REQUIRE(execution_instance_dispatch(other, 10, &tick, 1, 4, 0, 1000) == FLOW_OK);
        REQUIRE(execution_instance_pending(other) > 0);
        REQUIRE(execution_program_update(program, 5, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(other, "observed", 11));
        REQUIRE(execution_inspector_cursor(debugger).owner == sibling_owner);
        REQUIRE(execution_inspector_owner_cursor(debugger, owner, &selected));
        REQUIRE(selected.retired && selected.step == selected_step);
        REQUIRE(execution_inspector_owner_frame_count(debugger, owner) == stack_count);
        REQUIRE(
            execution_inspector_owner_frame_at(debugger, owner, 0).function ==
            frame.function
        );
        execution_program_free(program);
        program = NULL;
        execution_module_free(module);
        module = NULL;
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        REQUIRE(execution_inspector_cursor(debugger).retired);
        REQUIRE(!strcmp(execution_inspector_watch_at(debugger, saved)->value.str, "snapshot"));
        REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_RESUME) == FLOW_ERR_STATE);
        REQUIRE(!execution_inspector_pause(debugger, owner));
    }
    return true;
}

static const char *const task_source =
    "Tasks :: behavior { steps: double = 0; out: double = 0; seen: double = "
    "0;Count :: (n: double) -> (doubled: double) { steps += 1; yield; steps += "
    "1;await Delay(1); steps += 1; return(n * 2); }on Impulse :: (amount: "
    "double) "
    "{ t := Count(amount); if t { seen += 1; }answer := await t; if answer is "
    "double value { out = value; } if !t { seen += 10; } } }";
TEST_CASE(program_task_runs_to_first_suspension_then_beside_caller)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(task_source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        const pin_value args[] = { { .type = PIN_F64, .d = 4 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "steps", 1) && number_state(instance, "seen", 1));
        REQUIRE(number_state(instance, "out", 0));
        REQUIRE(execution_program_update(program, 0.25, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "steps", 2) && number_state(instance, "out", 0));
        REQUIRE(execution_program_update(program, 0.5, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "steps", 2));
        REQUIRE(execution_program_update(program, 1.25, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "steps", 3) && number_state(instance, "out", 8));
        REQUIRE(number_state(instance, "seen", 11));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}
TEST_CASE(program_task_cancel_cascades_and_wakes_awaiters)
{
    const char *
        source = "Cancel :: behavior { inner: double = 0; outer: double = 0; "
                 "woke: double = 0;worker: task;Leaf :: () -> () { await "
                 "Delay(1); inner += 1; }Inner :: () -> () { Leaf(); Leaf(); "
                 "await Delay(1); inner += 1; }Outer :: () -> () { Inner(); "
                 "Inner(); await Delay(1); outer += 1; }Watch :: () -> () { "
                 "await worker; woke += 1; }on Impulse :: (enabled: bool) { if "
                 "enabled { worker = Outer(); Watch(); } else { cancel "
                 "worker; cancel worker; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        pin_value args[] = { { .type = PIN_F64, .d = 0 },
                             { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_instance_pending(instance) > 0);
        args[1].b = false;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0.5, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "woke", 1));
        REQUIRE(number_state(instance, "inner", 0) && number_state(instance, "outer", 0));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}
TEST_CASE(program_task_cancelling_itself_stops_at_once)
{
    /* C cancels the task that started it: P, its sibling A and C itself stop
     * at the cancel. Q cancels itself, and R's C cancels R while R is still
     * running the call that started C. */
    const char
        *source = "K :: behavior { kept: double = 0; worker: task; self_task: "
                  "task;inline: task; got: double = 0;A :: () -> () { await "
                  "Delay(1); kept += 1; }C :: () -> () { yield; cancel worker; "
                  "kept += 10 }P :: () -> () { C(); A(); await Delay(5); kept "
                  "+= 100 }Q :: () -> (v: double) { yield; cancel self_task; "
                  "kept += 1000; return 7 }D :: () -> () { cancel inline; kept "
                  "+= 10000 }R :: () -> () { yield; D(); kept += 100000 }Wait "
                  ":: () -> () { answer := await self_task; if answer is "
                  "Cancelled { got = 1 } }on Impulse :: () { worker "
                  "= P(); self_task = Q(); Wait(); inline = R() } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 0.1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 6, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "kept", 0));
        /* Cancellation is observable and the waiter continues. */
        REQUIRE(number_state(instance, "got", 1));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}
TEST_CASE(program_task_owns_tasks_started_by_its_continuations)
{
    /* P's outcome continuation resumes on its own and starts Spin, which
     * still belongs to P, so cancelling P stops it. */
    const char
        *source = "C :: behavior { ticks: double = 0; worker: task;Spin :: () "
                  "-> () { while true { ticks += 1; yield } }Count :: () -> "
                  "(shown: double) { return 1 }action act = Count();P :: () -> "
                  "() { act(); await Delay(10) }after act { yield; Spin() }on "
                  "Impulse :: (enabled: bool) { if enabled { worker = P() } "
                  "else "
                  "{ cancel worker } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        pin_value args[] = { { .type = PIN_F64, .d = 0 },
                             { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "ticks", 2));
        args[1].b = false;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 2, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 3, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 4, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "ticks", 2));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}
TEST_CASE(program_task_continuation_cancels_but_cannot_await_its_task)
{
    /* The continuation resumes on its own, so P is suspended while it runs.
     * Cancelling P stops P and the continuation, even while the continuation
     * first runs; awaiting P never ends. */
    const char *bodies[] = { "yield; cancel worker; hit = 1",
                             "cancel worker; hit = 1",
                             "yield; await worker" };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int c = 0; c < 3; c++) {
        char source[PROGRAM_SOURCE_CAPACITY];
        int length = snprintf(
            source,
            sizeof source,
            "S :: behavior { worker: task; hit: double = 0;"
            "Count :: () -> (shown: double) { return 1 }"
            "action act = Count();"
            "P :: () -> () { yield; act(); await Delay(10) }"
            "after act { %s }"
            "on Impulse :: (amount: double, enabled: bool) { worker = P() } }",
            bodies[c]
        );
        REQUIRE(length > 0 && (size_t)length < sizeof source);
        for (int b = 0; b < 2; b++) {
            prepare();
            REQUIRE(compile(source, backends[b]));
            execution_instance *instance = execution_program_attach(
                program,
                ecs_entity_to_u64(ecs_world_create_entity(world))
            );
            const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                       { .type = PIN_BOOL, .b = true } };
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
            if (c == 2) {
                REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
                REQUIRE(execution_program_update(program, 1, 0, 1000) != FLOW_OK);
                continue;
            }
            for (int u = 0; u < 2; u++) {
                REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
            }
            REQUIRE(execution_instance_pending(instance) == 0);
            REQUIRE(number_state(instance, "hit", 0));
        }
    }
    return true;
}
TEST_CASE(program_task_slots_are_reused_without_reviving_old_handles)
{
    const char *source =
        "Many :: behavior { done: double = 0; kept: double = 0; "
        "stale: double = 0; got: double = -1; first: task;Short :: "
        "() -> (v: double) { yield; done += 1; return 7 }Long :: () "
        "-> () { await Delay(1); kept += 1; }on Impulse :: (amount: double, "
        "enabled: bool) { if !enabled { cancel first; if first { stale += "
        "1; } } else if amount == 0 { first = Short(); } else if "
        "amount == 1 { Short(); } else if amount == 3 { answer := "
        "await first; if answer is double value { got = value } } "
        "else { Long(); } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        pin_value args[] = { { .type = PIN_F64, .d = 0 },
                             { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        args[0].d = 1;
        for (int i = 0; i < 31; i++) {
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        }
        REQUIRE(execution_program_update(program, 0.1, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "done", 32));
        /* Starting a task expires nothing: the first result is still kept. */
        args[0].d = 2;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0.15, 0, 1000) == FLOW_OK);
        args[0].d = 3;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0.15, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "got", 7));
        /* More completions cannot expire a result held in state. */
        args[0].d = 1;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0.2, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 0.3, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "done", 33));
        args[0].d = 3;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0.32, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "got", 7));
        args[0].d = 2;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0.35, 0, 1000) == FLOW_OK);
        args[1].b = false;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0.4, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "kept", 2) && number_state(instance, "stale", 0));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}
TEST_CASE(program_task_deadlines_use_start_time_and_do_not_cancel_waiters)
{
    const char *source =
        "Deadlines :: behavior { worker: task; began: double = 0; "
        "done: double = 0;scheduled: double = 0; cancelled: double = "
        "0; continued: double = 0;Work :: () -> (value: double) { "
        "began += 1; await Delay(100); done += 1; return 7 }Watch :: "
        "() -> () { result := await worker; if result is Cancelled { "
        "cancelled += 1; } continued += 1; }on Impulse :: (amount: double, "
        "enabled: bool) { if enabled { worker = Work(); Watch(); Watch(); "
        "} else { cancel worker after amount; scheduled += 1; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        pin_value args[] = { { .type = PIN_F64, .d = 5 },
                             { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 10, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "began", 1));
        args[1].b = false;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 13, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "scheduled", 1));
        /* Repeated cancellation cannot extend the original deadline. */
        args[0].d = 20;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 14, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 14.9, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "cancelled", 0));
        REQUIRE(execution_program_update(program, 15, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "cancelled", 2));
        REQUIRE(number_state(instance, "continued", 2));
        REQUIRE(number_state(instance, "done", 0));
        REQUIRE(execution_instance_pending(instance) == 0);
        /* An overdue deadline stops at the scheduling point, after initial work. */
        args[1].b = true;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 20, 0, 1000) == FLOW_OK);
        args[0].d = 0;
        args[1].b = false;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 21, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 21, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "began", 2));
        REQUIRE(number_state(instance, "cancelled", 4));
        REQUIRE(number_state(instance, "continued", 4));
        REQUIRE(number_state(instance, "scheduled", 3));
        const double immediate[] = { NAN, -10 };
        for (int i = 0; i < 2; i++) {
            args[1].b = true;
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 30 + i, 0, 1000) == FLOW_OK);
            args[0].d = immediate[i];
            args[1].b = false;
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 30 + i, 0, 1000) == FLOW_OK);
            REQUIRE(execution_program_update(program, 30 + i, 0, 1000) == FLOW_OK);
            REQUIRE(number_state(instance, "cancelled", 6 + i * 2));
            REQUIRE(number_state(instance, "continued", 6 + i * 2));
        }
    }
    return true;
}
TEST_CASE(program_task_copies_retain_results_and_unobserved_results_are_reclaimed)
{
    const char *
        source = "Results :: behavior { first: task; copy: task; got: double = "
                 "0; done: double = 0;Work :: () -> (value: double) { yield; "
                 "done += 1; return 7 }on Impulse :: (amount: double) { if "
                 "amount "
                 "== 0 { first = Work(); copy = first; } else if amount == 1 { "
                 "Work(); } else { cancel copy after 0; result := await copy; "
                 "if result is double value { got = value; } } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(
            execution_instance_set_state(instance, state_id("first"), (pin_value){ .type = PIN_TASK }) ==
            FLOW_OK
        );
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        args[0].d = 1;
        for (int i = 0; i < 128; i++) {
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, i + 2, 0, 1000) == FLOW_OK);
            REQUIRE(execution_program_update(program, i + 2, 0, 1000) == FLOW_OK);
            /* The fire-and-forget task runs to completion without retaining its result. */
            REQUIRE(execution_program_memory_usage(program).task_slots <= 3);
        }
        args[0].d = 2;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 131, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "got", 7));
        REQUIRE(number_state(instance, "done", 129));
        REQUIRE(
            execution_instance_set_state(instance, state_id("copy"), (pin_value){ .type = PIN_TASK }) == FLOW_OK
        );
        REQUIRE(execution_program_update(program, 132, 0, 1000) == FLOW_OK);
        execution_memory_usage memory = execution_program_memory_usage(program);
        REQUIRE(memory.retained_tasks == 0);
        REQUIRE(memory.task_result_bytes == 0);
    }
    return true;
}
TEST_CASE(program_task_handle_survives_in_a_detached_capture)
{
    const char *source = "Capture :: behavior { got: double = 0;Work :: () -> "
                         "(value: double) { yield; return 7 }Start :: () { "
                         "held := Work(); Observe :: (held: ref task<double>) "
                         "{ await Delay(10); result := await held; if result "
                         "is double value { got = value; } } Observe(held); "
                         "}on Impulse :: () { Start(); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "got", 0));
        REQUIRE(execution_program_update(program, 10, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "got", 7));
        REQUIRE(execution_program_memory_usage(program).retained_tasks == 0);
    }
    return true;
}

TEST_CASE(program_task_handles_survive_in_suspended_array_values)
{
    const char
        *source = "Arrays :: behavior { got: double = 0;Work :: () -> double { "
                  "yield; return 7; }Pack :: () -> task<double>[] { held: "
                  "task<double>[] = [Work()]; return held; }on Impulse :: "
                  "(amount: double, "
                  "enabled: bool) { first: task<double>[] = []; i := 0;while i "
                  "< 2 { "
                  "row := Pack(); if i == 0 { first = row; } i += 1; }copy := "
                  "first; first = [];yield; await Delay(10); result := await "
                  "copy[0];if result is double value { got = value; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 4; b++) {
        prepare();
        REQUIRE(compile(source, backends[b % 2]));
        if (b >= 2) {
            unsigned char *bytes = NULL;
            size_t length = 0;
            char error[256];
            REQUIRE(execution_prepared_write(module, &bytes, &length, error, sizeof error));
            const execution_prepared_linker linker = { .type = program_prepared_type };
            execution_module *loaded = execution_prepared_read(
                bytes,
                length,
                &linker,
                backends[b % 2],
                error,
                sizeof error
            );
            free(bytes);
            if (!loaded) {
                fprintf(stderr, "prepared task array roots: %s\n", error);
            }
            REQUIRE(loaded != NULL);
            execution_program_free(program);
            execution_module_free(module);
            module = loaded;
            program = execution_program_new(module, world, NULL);
            REQUIRE(program != NULL);
        }
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        execution_memory_usage memory = execution_program_memory_usage(program);
        if (memory.retained_tasks != 1) {
            fprintf(
                stderr,
                "task array roots: retained=%d slots=%d pending=%d\n",
                memory.retained_tasks,
                memory.task_slots,
                execution_instance_pending(instance)
            );
        }
        REQUIRE(execution_program_memory_usage(program).retained_tasks == 1);
        REQUIRE(execution_program_update(program, 11, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "got", 7));
        REQUIRE(execution_program_memory_usage(program).retained_tasks == 0);
    }
    return true;
}

TEST_CASE(program_native_nested_collections_retain_task_handles)
{
    const char *source = "Nested :: behavior { pulse: task<double>; held: "
                         "uint64[] = []; "
                         "Work :: () -> double { yield; return 7; } "
                         "on Impulse :: () { pulse = Work(); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int variant = 0; variant < 4; variant++) {
        prepare();
        REQUIRE(compile(source, backends[variant % 2]));
        if (variant >= 2) {
            unsigned char *bytes = NULL;
            size_t length = 0;
            char error[256];
            REQUIRE(execution_prepared_write(module, &bytes, &length, error, sizeof error));
            const execution_prepared_linker linker = { .type = program_prepared_type };
            execution_module *loaded = execution_prepared_read(
                bytes,
                length,
                &linker,
                backends[variant % 2],
                error,
                sizeof error
            );
            free(bytes);
            if (!loaded) {
                fprintf(stderr, "prepared nested collection roots: %s\n", error);
            }
            REQUIRE(loaded);
            execution_program_free(program);
            execution_module_free(module);
            module = loaded;
            program = execution_program_new(module, world, NULL);
            REQUIRE(program);
        }
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        REQUIRE(instance);
        const pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        pin_value pulse = *execution_instance_state(instance, state_id("pulse"));
        REQUIRE(pulse.type == PIN_TASK && pulse.u64);
        pin_map_entry entry = { { .type = PIN_STR, .str = "job" }, pulse };
        arena producer = { 0 };
        pin_map *map = pin_map_create(&producer, (pin_value){ .type = PIN_STR }, pulse, &entry, 1);
        REQUIRE(map);
        /* The public native value codec permits nested boxed containers.
         * Their scalar outer storage does not describe the nested payload. */
        pin_value child = { .type = PIN_U64, .container = PIN_CONTAINER_MAP, .ptr = map };
        pin_array array = { .element_type = PIN_U64,
                            .count = 1,
                            .capacity = 1,
                            .items = &child };
        pin_value held = *execution_instance_state(instance, state_id("held"));
        array.type_name = held.type_name;
        held.ptr = &array;
        REQUIRE(execution_instance_set_state(instance, state_id("held"), held) == FLOW_OK);
        arena_free(&producer);
        pulse.u64 = 0;
        REQUIRE(execution_instance_set_state(instance, state_id("pulse"), pulse) == FLOW_OK);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_memory_usage(program).retained_tasks == 1);
        held.ptr = NULL;
        REQUIRE(execution_instance_set_state(instance, state_id("held"), held) == FLOW_OK);
        REQUIRE(execution_program_update(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_memory_usage(program).retained_tasks == 0);
    }
    return true;
}

TEST_CASE(program_task_scope_exits_drop_only_unobserved_handles)
{
    const char *source =
        "Scope :: behavior { got: double = 0;Work :: () -> double { yield; "
        "return 7; }Pack :: () -> (number: double, discarded: task<double>) "
        "{return(number: 1, discarded: Work()); }Observe :: (held: ref "
        "task<double>) { await Delay(10);result := await held; if result is "
        "double value { got = value; } }on Impulse :: () {Pack(); "
        "unused := Pack().number;if true { held := Work(); Observe(held); }i "
        ":= 0; while i < 3 { dropped := Work(); i += 1; continue; }while true "
        "{ dropped := Work(); break; }if true { dropped := Work(); goto "
        "finished; } finished:yield; await Delay(20); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_memory_usage(program).retained_tasks == 1);
        REQUIRE(execution_program_update(program, 11, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "got", 7));
        REQUIRE(execution_program_memory_usage(program).retained_tasks == 0);
    }
    return true;
}

TEST_CASE(program_task_cancellation_is_distinct_from_zero_false_and_empty_text)
{
    const char *source =
        "Values :: behavior { zero: double = 1; false_seen: bool = false; "
        "empty_seen: bool = false; nil_seen: bool = false; optional_zero_seen: "
        "bool = false;cancelled: double = 0;Zero :: () -> (value: double) { "
        "yield; return 0; }False :: () -> (value: bool) { yield; return false; "
        "}Empty :: () -> (value: string) { yield; return \"\"; }None :: () -> "
        "double? { yield; return nil; }Some :: () -> double? { yield; return "
        "0; }on Impulse :: () { z := Zero(); f := False(); s := "
        "Empty(); k := Zero();n := None(); o := Some();cancel k; rz := await "
        "z; rf := await f; rs := await s; rk := await k;if rz is double value "
        "{ zero = value; }if rf is bool value { if !value { false_seen = true; "
        "} }if rs is string value { if value == \"\" { empty_seen = true; } "
        "}rn := await n; ro := await o;if rn is not Cancelled { value := rn; "
        "if !value { nil_seen = true; } }if ro is not Cancelled { value := ro; "
        "if value { if value == 0 { optional_zero_seen = true; } } }if rk is "
        "Cancelled { cancelled = 1; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        /* The update visits each activation once; a swapped-in waiter can
         * precede the next target it awaits. Pump its now-ready result. */
        REQUIRE(execution_program_pump(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "zero", 0));
        REQUIRE(number_state(instance, "cancelled", 1));
        REQUIRE(execution_instance_state(instance, state_id("false_seen"))->b);
        REQUIRE(execution_instance_state(instance, state_id("empty_seen"))->b);
        REQUIRE(execution_instance_state(instance, state_id("nil_seen"))->b);
        REQUIRE(
            execution_instance_state(instance, state_id("optional_zero_seen"))->b
        );
    }
    return true;
}
TEST_CASE(program_task_waiters_receive_independent_success_snapshots)
{
    const char
        *source = "Waiters :: behavior { worker: task<double[]>; total: double "
                  "= 0;Work :: () -> double[] { yield; return [7]; }Observe :: "
                  "(offset: double) { result := await worker; if result is not "
                  "Cancelled {values := result; values[0] += offset; total += "
                  "values[0]; } }on Impulse :: () { worker = "
                  "Work(); Observe(1); Observe(2); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_pump(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "total", 17));
        REQUIRE(execution_program_memory_usage(program).retained_tasks == 1);
        REQUIRE(
            execution_instance_set_state(instance, state_id("worker"), (pin_value){ .type = PIN_TASK }) ==
            FLOW_OK
        );
        REQUIRE(execution_program_memory_usage(program).retained_tasks == 0);
    }
    return true;
}

TEST_CASE(program_task_result_snapshot_outlives_the_last_handle)
{
    const char
        *source = "Snapshot :: behavior { worker: task; text: string = "
                  "\"\";Work :: (input: string) -> (value: string) { yield; "
                  "return input; }on Impulse :: () { worker = "
                  "Work(\"retained\"); result := await worker;if result is "
                  "string value { worker = nil; yield; text = value; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_memory_usage(program).retained_tasks == 0);
        REQUIRE(execution_program_memory_usage(program).task_result_bytes == 0);
        REQUIRE(execution_program_update(program, 2, 0, 1000) == FLOW_OK);
        const pin_value *text = execution_instance_state(instance, state_id("text"));
        REQUIRE(text && text->str && !strcmp(text->str, "retained"));
    }
    return true;
}

TEST_CASE(program_task_await_copies_large_payload_once)
{
    enum
    {
        ITEMS = 8192
    };
    const char
        *source = "Delivery :: behavior { input: double[] = []; worker: "
                  "task<double[]>;Work :: () -> double[] { yield; return "
                  "input; }on Impulse :: (enabled: bool) { if enabled { worker "
                  "= "
                  "Work(); }else { await worker; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        uint64_t requested[2], physical[2];
        for (int sample = 0; sample < 2; sample++) {
            prepare();
            REQUIRE(compile(source, backends[b]));
            execution_instance *instance = execution_program_attach(
                program,
                ecs_entity_to_u64(ecs_world_create_entity(world))
            );
            arena storage = { 0 };
            pin_array *input = pin_array_allocate(&storage, PIN_F64, NULL, sample ? ITEMS : 1);
            input->items[0].d = 7;
            REQUIRE(
                execution_instance_set_state(
                    instance,
                    state_id("input"),
                    (pin_value){ .type = PIN_F64, .container = PIN_CONTAINER_ARRAY, .ptr = input }
                ) == FLOW_OK
            );
            arena_free(&storage);
            const pin_value *retained = execution_instance_state(instance, state_id("input"));
            REQUIRE(
                retained && retained->container == PIN_CONTAINER_ARRAY &&
                retained->ptr
            );
            const pin_array *owned = retained->ptr;
            REQUIRE(owned->count == (sample ? ITEMS : 1));
            REQUIRE(owned->stride == sizeof(double) && !owned->items);
            physical[sample] = pin_array_storage_bytes(owned);
            pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL, .b = true } };
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
            REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
            REQUIRE(execution_program_memory_usage(program).retained_tasks == 1);
            args[1].b = false;
            xalloc_stats before, after;
            xalloc_stats_get(&before);
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 1, 0, 1000) == FLOW_OK);
            xalloc_stats_get(&after);
            requested[sample] = after.requested_bytes - before.requested_bytes;
            REQUIRE(execution_program_memory_usage(program).retained_tasks == 1);
            REQUIRE(
                execution_instance_set_state(instance, state_id("worker"), (pin_value){ .type = PIN_TASK }) ==
                FLOW_OK
            );
            REQUIRE(execution_program_memory_usage(program).task_result_bytes == 0);
        }
        /* Compare the same completed-task delivery with two payload sizes.
         * Control storage is size independent. One destination snapshot grows
         * by one physical item buffer, not boxed value headers. A temporary
         * owning union would double that work. */
        uint64_t payload = physical[1] - physical[0];
        REQUIRE(payload == (ITEMS - 1) * sizeof(double));
        REQUIRE(requested[1] >= requested[0] + payload);
        REQUIRE(requested[1] - requested[0] < payload + payload / 2);
    }
    return true;
}

TEST_CASE(program_task_collection_indexes_handles_across_many_owners)
{
    enum
    {
        OWNERS = 256,
        LOOKUPS_PER_OWNER = 16
    };
    const char *source = "ManyOwners :: behavior { held: task;Work :: () -> "
                         "(value: double) { yield; return 7; }on "
                         "Impulse :: () { held = Work(); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instances[OWNERS];
        const pin_value args[] = { { .type = PIN_F64 },
                                   { .type = PIN_BOOL, .b = true } };
        for (int i = 0; i < OWNERS; i++) {
            instances[i] = execution_program_attach(
                program,
                ecs_entity_to_u64(ecs_world_create_entity(world))
            );
            REQUIRE(execution_instance_dispatch(instances[i], 11, args, 2, 0, 0, 1000) == FLOW_OK);
        }
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        execution_memory_usage memory = execution_program_memory_usage(program);
        uint64_t steps = memory.task_lookup_steps;
        REQUIRE(steps >= OWNERS);
        REQUIRE(steps <= OWNERS * LOOKUPS_PER_OWNER);
        REQUIRE(memory.task_slots == OWNERS);
        REQUIRE(memory.retained_tasks == OWNERS);
        REQUIRE(memory.task_result_bytes > 0);
    }
    return true;
}

TEST_CASE(program_task_results_keep_nested_task_handles_alive)
{
    const char
        *source = "Nested :: behavior { held: task; observed: double = 0;Inner "
                  ":: () -> (value: double) { yield; return 7; }Outer :: () -> "
                  "(child: task) { nested := Inner(); yield; return nested; "
                  "}on Impulse :: (enabled: bool) { if enabled { held = "
                  "Outer(); "
                  "} else { result := await held; if result is task child { "
                  "await child; observed = 1; } } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_memory_usage(program).retained_tasks == 2);
        args[1].b = false;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 1, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "observed", 1));
        REQUIRE(
            execution_instance_set_state(instance, state_id("held"), (pin_value){ .type = PIN_TASK }) == FLOW_OK
        );
        REQUIRE(execution_program_update(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_memory_usage(program).retained_tasks == 0);
        REQUIRE(execution_program_memory_usage(program).task_result_bytes == 0);
    }
    return true;
}

static bool program_prepared_type(void *, uint64_t id, execution_prepared_type *out)
{
    const script_catalog_type *type = script_catalog_type_by_id(&catalog, id);
    if (type) {
        *out = (execution_prepared_type){ .name = type->name,
                                          .type = type_catalog_find(type->name),
                                          .storage = type->type.storage,
                                          .size = type->size,
                                          .layout = type->layout };
        return true;
    }
    if (id == catalog.entity_type) {
        *out = (execution_prepared_type){ .name = "ecs.entity",
                                          .storage = PIN_HANDLE,
                                          .size = sizeof(uint64_t) };
        return true;
    }
    return false;
}
TEST_CASE(program_prepared_tasks_preserve_cancellation_union_and_deadlines)
{
    const char
        *source = "Prepared :: behavior { worker: task; stored: double | "
                  "Cancelled = Cancelled;got: double = 0; initial: double = "
                  "0;Work :: () -> (value: double) { yield; return 7; }on "
                  "Impulse :: (amount: double) { if stored is Cancelled { "
                  "initial "
                  "+= 1; }worker = Work(); cancel worker after amount; answer "
                  ":= await worker; stored = answer;if answer is double value "
                  "{ got = value; } else { got = -1; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        uint64_t got_id = state_id("got"), initial_id = state_id("initial");
        unsigned char *bytes = NULL;
        size_t length = 0;
        char error[256];
        REQUIRE(execution_prepared_write(module, &bytes, &length, error, sizeof error));
        const execution_prepared_linker linker = { .type = program_prepared_type };
        execution_module *
            loaded = execution_prepared_read(bytes, length, &linker, backends[b], error, sizeof error);
        free(bytes);
        if (!loaded) {
            fprintf(stderr, "prepared tasks: %s\n", error);
        }
        REQUIRE(loaded != NULL);
        execution_program_free(program);
        execution_module_free(module);
        script_analysis_free(analysis);
        script_syntax_free(syntax);
        analysis = NULL;
        syntax = NULL;
        module = loaded;
        program = execution_program_new(module, world, NULL);
        REQUIRE(program != NULL);
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        pin_value args[] = { { .type = PIN_F64, .d = 1 },
                             { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 0.5, 0, 1000) == FLOW_OK);
        REQUIRE(execution_instance_state(instance, got_id)->d == 7);
        REQUIRE(execution_instance_state(instance, initial_id)->d == 1);
        args[0].d = 0;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 2, 0, 1000) == FLOW_OK);
        REQUIRE(execution_instance_state(instance, got_id)->d == -1);
    }
    return true;
}

TEST_CASE(program_task_stops_with_owner_and_does_not_block_events)
{
    const char *source = "Loop :: behavior { ticks: double = 0; hits: double = "
                         "0;Spin :: () -> () { while true { ticks += 1; yield; "
                         "} }on Impulse :: (enabled: bool) { if enabled { "
                         "Spin(); } hits += 1; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        uint64_t handle = ecs_entity_to_u64(owner);
        execution_instance *instance = execution_program_attach(program, handle);
        REQUIRE(instance != NULL);
        pin_value args[] = { { .type = PIN_F64, .d = 0 },
                             { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        for (int i = 1; i <= 3; i++) {
            REQUIRE(execution_program_update(program, i, 0, 1000) == FLOW_OK);
        }
        REQUIRE(number_state(instance, "ticks", 4) && number_state(instance, "hits", 1));
        args[1].b = false;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 3, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "hits", 2));
        execution_program_detach(program, handle);
        instance = execution_program_attach(program, handle);
        REQUIRE(execution_program_update(program, 5, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "ticks", 0) && execution_instance_pending(instance) == 0);
    }
    return true;
}
/* Collecting after every run traced every live task each time, so starting
 * and resuming n tasks took about 13 * n * n marks. The allocation threshold
 * keeps mark calls proportional to n; threshold scans, sorting and sweeping
 * are additional collector work that this counter does not measure. */
#define MANY_TASKS 400
#define MARKS_PER_TASK 64
TEST_CASE(program_many_tasks_collect_in_linear_mark_work)
{
    const char
        *source = "Many :: behavior { done: double = 0;Wait :: (n: double) -> "
                  "() { row: double[] = [n, n];await Delay(1); done += row[0] "
                  "- n + 1; }on Impulse :: (amount: double) { for (i in "
                  "1..int(amount)) { Wait(i) } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        const pin_value args[] = { { .type = PIN_F64, .d = MANY_TASKS },
                                   { .type = PIN_BOOL, .b = true } };
        uint64_t marks = arena_collect_marks();
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 100000) == FLOW_OK);
        REQUIRE(execution_instance_pending(instance) == MANY_TASKS);
        REQUIRE(execution_program_update(program, 2, 0, 100000) == FLOW_OK);
        /* Each task read its own row back after resuming. */
        REQUIRE(number_state(instance, "done", MANY_TASKS));
        REQUIRE(execution_instance_pending(instance) == 0);
        REQUIRE(arena_collect_marks() - marks < MANY_TASKS * MARKS_PER_TASK);
    }
    return true;
}
TEST_CASE(program_task_semantics_reject_invalid_uses)
{
    static const struct
    {
        const char *source, *message;
    } cases[] = {
        { "B :: behavior { n: double = 0; on Impulse :: () { cancel "
          "n; } }",
          "Cancel requires a task" },
        { "B :: behavior { t: task; n: double = 0; on Impulse :: () "
          "{ n = t; } }",
          "" },
        /* System-retained bindings belong to the registration, not a row's
         * activation, and cannot own tasks. */
        { "S :: system(p: Position) { worker: retain task }", "System-owned retained bindings cannot contain tasks; keep task handles in coroutine locals" },
        /* An unawaited action continues before its result exists. */
        { "B :: behavior { n: double = 0; on Impulse :: () { x := "
          "make_thing(); n = x } }",
          "Await this call before using its result" },
        { "B :: behavior { pulse: task; power: double = 0;Charge :: (time: "
          "double) -> (power: double) { yield; return time }on Impulse :: "
          "(amount: double, "
          "enabled: bool) { power = await pulse;if enabled { pulse: task = "
          "Charge(1); pulse = Charge(amount) } } }",
          "" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        prepare();
        syntax = script_parse(cases[i].source);
        analysis = script_analyze(syntax, &catalog);
        REQUIRE(!analysis->valid);
        bool found = false;
        for (int d = 0; d < analysis->diagnostic_count; d++) {
            found |= strstr(analysis->diagnostics[d].message, cases[i].message) != NULL;
        }
        REQUIRE(found);
    }
    return true;
}

/* A handler without a parameter list binds its payload's names. A bare read
 * of one that is also a parameter of the function around it is refused, and
 * naming the payload's parameters lets the read reach the function's own. */
TEST_CASE(program_handler_payloads_do_not_hide_parameters)
{
    const char *sources[] = {
        "L :: library { Twice :: (Value: int) -> (Value: int) { return (Value: "
        "Value * 2) } "
        "Run :: (Value: int) -> int { action doubled = Twice(Value: Value); "
        "doubled(); after doubled { return Value; } } }",
        "L :: library { Twice :: (Value: int) -> (Value: int) { return (Value: "
        "Value * 2) } "
        "Run :: (Value: int) -> int { action doubled = Twice(Value: Value); "
        "doubled(); after doubled(twice) { return Value; } } }",
    };
    for (int i = 0; i < 2; i++) {
        prepare();
        syntax = script_parse(sources[i]);
        analysis = script_analyze(syntax, &catalog);
        bool hidden = false;
        for (int d = 0; d < analysis->diagnostic_count; d++) {
            hidden |= strstr(analysis->diagnostics[d].message, "names both this handler's payload") !=
                      NULL;
        }
        REQUIRE(hidden == !i && analysis->valid == (i == 1));
    }
    /* Only a handler that returns lets the function's body end without one. */
    prepare();
    syntax = script_parse(
        "L :: library { Twice :: (Value: int) -> (Value: int) { return (Value: "
        "Value * 2) } "
        "Run :: (Value: int) -> int { action doubled = Twice(Value: Value); "
        "after entry { doubled(); } after doubled(twice) { } } }"
    );
    analysis = script_analyze(syntax, &catalog);
    bool missing = false;
    for (int d = 0; d < analysis->diagnostic_count; d++) {
        missing |= !strcmp(analysis->diagnostics[d].message, "Missing return");
    }
    REQUIRE(!analysis->valid && missing);
    return true;
}
TEST_CASE(program_nested_functions_wait_for_themselves)
{
    /* Declaring a waiting function does not make its parent wait. */
    const char *plain = "P :: behavior { n: double = 0;"
                        "Outer :: () -> (v: double) { "
                        "Inner :: () { yield } return 3 }"
                        "on Impulse :: () { x := Outer(); n = x } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(plain, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 3));
    }
    return true;
}

static const script_catalog_parameter action_parameter = { .name = "Action",
                                                           .type = { .storage = PIN_STR } };
static const script_catalog_parameter held_result = { .name = "Held",
                                                      .type = { .storage = PIN_BOOL } };
static const script_catalog_parameter pressed_result = { .name = "Pressed",
                                                         .type = { .storage = PIN_BOOL } };

/* The fixture root is a content directory; map is an ordinary compiled-source
 * module supplied by the test, so no project scaffold or source rewrite is needed. */
static bool input_project(char root[], const char *map)
{
    REQUIRE(mkdtemp(root));
    char path[256];
    snprintf(path, sizeof path, "%s/" INPUT_MAP_FILE, root);
    FILE *file = fopen(path, "wb");
    REQUIRE(file);
    bool written = fputs(map, file) >= 0;
    written = fclose(file) == 0 && written;
    REQUIRE(written);
    return true;
}

static void remove_input_project(const char *root)
{
    char path[256];
    snprintf(path, sizeof path, "%s/" INPUT_MAP_FILE, root);
    unlink(path);
    rmdir(root);
}

static bool bool_state(execution_instance *instance, const char *name, bool expected)
{
    const pin_value *value = execution_instance_state(instance, state_id(name));
    return value && value->type == PIN_BOOL && value->b == expected;
}
TEST_CASE(program_input_map_validates_actions)
{
    const char *canonical =
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Jump\", data.Object([\n"
        "                data.Field.Named(\"sources\", data.List([\n"
        "                    data.Value.Text(\"Space\"),\n"
        "                    data.Value.Text(\"Z\")\n"
        "                ]))\n"
        "            ])),\n"
        "            data.Field.Named(\"Fire\", data.Object([\n"
        "                data.Field.Named(\"sources\", data.List([\n"
        "                    data.Value.Text(\"X\")\n"
        "                ])),\n"
        "                data.Field.Named(\"firing\", data.Value.Bool(true))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n";
    input_action *map;
    char error[256];
    REQUIRE(input_map_read(canonical, &map, error, sizeof error));
    REQUIRE(acount(map) == 2 && !strcmp(map[0].name, "Jump") && !map[0].firing);
    REQUIRE(map[0].controls == ((UINT64_C(1) << 3) | (UINT64_C(1) << 32)));
    REQUIRE(!strcmp(map[1].name, "Fire") && map[1].firing);
    char *text = input_map_write(map);
    REQUIRE(!strcmp(text, canonical));
    free(text);
    input_map_add(&map, "Jump", UINT64_C(1) << 30, false);
    REQUIRE(acount(map) == 2 && map[0].controls & (UINT64_C(1) << 30));
    input_map_free(map);
    static const struct
    {
        const char *text, *message;
    } invalid[] = {
        { "package asset\n"
          "\n"
          "import data \"game:assets\"\n"
          "\n"
          "pub Build :: () -> byte[] {\n"
          "    return data.Encode(\n"
          "        data.Object([\n"
          "            data.Field.Named(\"Jump\", data.Object([\n"
          "                data.Field.Named(\"sources\", data.List([\n"
          "                    data.Value.Text(\"Nope\")\n"
          "                ]))\n"
          "            ]))\n"
          "        ])\n"
          "    )\n"
          "}\n",
          "Jump: sources must list" },
        { "package asset\n"
          "\n"
          "import data \"game:assets\"\n"
          "\n"
          "pub Build :: () -> byte[] {\n"
          "    return data.Encode(\n"
          "        data.Object([\n"
          "            data.Field.Named(\"Jump\", data.Object([\n"
          "                data.Field.Named(\"sources\", data.List([]))\n"
          "            ]))\n"
          "        ])\n"
          "    )\n"
          "}\n",
          "Jump: sources must list" },
        { "package asset\nimport data \"game:assets\"\n"
          "pub Build :: () -> byte[] { return data.Encode(data.Object(["
          "data.Field.Named(\"Jump\", data.Object([])),"
          "data.Field.Named(\"Jump\", data.Object([]))])) }\n",
          "Duplicate" },
        { "package asset\n"
          "\n"
          "import data \"game:assets\"\n"
          "\n"
          "pub Build :: () -> byte[] {\n"
          "    return data.Encode(\n"
          "        data.Object([\n"
          "            data.Field.Named(\"\", data.Object([\n"
          "                data.Field.Named(\"sources\", data.List([\n"
          "                    data.Value.Text(\"Z\")\n"
          "                ]))\n"
          "            ]))\n"
          "        ])\n"
          "    )\n"
          "}\n",
          "an action needs a name" },
        { "package asset\n"
          "\n"
          "import data \"game:assets\"\n"
          "\n"
          "pub Build :: () -> byte[] {\n"
          "    return data.Encode(\n"
          "        data.Object([\n"
          "            data.Field.Named(\"Jump\", data.Object([\n"
          "                data.Field.Named(\"sources\", data.List([\n"
          "                    data.Value.Text(\"Z\")\n"
          "                ])),\n"
          "                data.Field.Named(\"firing\", data.Value.UInt(1))\n"
          "            ]))\n"
          "        ])\n"
          "    )\n"
          "}\n",
          "optional firing flag" },
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        REQUIRE(!input_map_read(invalid[i].text, &map, error, sizeof error) && !map);
        if (!strstr(error, invalid[i].message)) {
            fprintf(stderr, "input map: %s\n", error);
        }
        REQUIRE(
            strstr(error, INPUT_MAP_FILE ": ") == error &&
            strstr(error, invalid[i].message)
        );
    }
    const char *cooperative =
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"P1Left\", data.Object([\n"
        "                data.Field.Named(\"sources\", data.List([\n"
        "                    data.Value.Text(\"Z\")\n"
        "                ])),\n"
        "                data.Field.Named(\"controller\", "
        "data.Value.UInt(0)),\n"
        "                data.Field.Named(\"buttons\", data.List([\n"
        "                    data.Value.Text(\"Dpad Left\")\n"
        "                ]))\n"
        "            ])),\n"
        "            data.Field.Named(\"P2Confirm\", data.Object([\n"
        "                data.Field.Named(\"controller\", "
        "data.Value.UInt(1)),\n"
        "                data.Field.Named(\"buttons\", data.List([\n"
        "                    data.Value.Text(\"A\")\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n";
    REQUIRE(input_map_read(cooperative, &map, error, sizeof error));
    REQUIRE(acount(map) == 2 && map[0].controls && map[0].buttons);
    REQUIRE(map[1].controller == 1 && !map[1].controls && map[1].buttons);
    text = input_map_write(map);
    REQUIRE(!strcmp(text, cooperative));
    input_map_free(map);
    REQUIRE(input_map_read(text, &map, error, sizeof error));
    REQUIRE(map[1].controller == 1 && map[1].buttons);
    input_map_free(map);
    free(text);
    const char *bad_controllers[] = {
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"P\", data.Object([\n"
        "                data.Field.Named(\"controller\", "
        "data.Value.UInt(4)),\n"
        "                data.Field.Named(\"buttons\", data.List([\n"
        "                    data.Value.Text(\"A\")\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"P\", data.Object([\n"
        "                data.Field.Named(\"controller\", "
        "data.Value.Int(-1)),\n"
        "                data.Field.Named(\"buttons\", data.List([\n"
        "                    data.Value.Text(\"A\")\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"P\", data.Object([\n"
        "                data.Field.Named(\"controller\", data.Value.UInt(0))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"P\", data.Object([\n"
        "                data.Field.Named(\"buttons\", data.List([\n"
        "                    data.Value.Text(\"A\")\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"P\", data.Object([\n"
        "                data.Field.Named(\"controller\", "
        "data.Value.UInt(0)),\n"
        "                data.Field.Named(\"buttons\", data.List([\n"
        "                    data.Value.Text(\"Unknown\")\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
    };
    for (size_t i = 0; i < sizeof bad_controllers / sizeof *bad_controllers; i++) {
        REQUIRE(!input_map_read(bad_controllers[i], &map, error, sizeof error) && !map);
    }
    /* A map holds at most INPUT_ACTION_MAX actions. */
    av_doc *values = av_doc_new();
    av_value *actions = av_obj(values);
    av_doc_set_root(values, actions);
    char name[32];
    for (int i = 0; i <= INPUT_ACTION_MAX; i++) {
        snprintf(name, sizeof name, "A%d", i);
        av_value *action = av_obj_add_obj(values, actions, name);
        av_arr_add_str(values, av_obj_add_arr(values, action, "sources"), "Z");
    }
    char *many = asset_document_write(actions);
    REQUIRE(many && !input_map_read(many, &map, error, sizeof error) && !map);
    REQUIRE(strstr(error, "at most"));
    free(many);
    av_obj_remove_key(actions, name);
    many = asset_document_write(actions);
    REQUIRE(many && input_map_read(many, &map, error, sizeof error));
    REQUIRE(acount(map) == INPUT_ACTION_MAX);
    input_map_free(map);
    free(many);
    av_doc_free(values);
    return true;
}
/* The catalog finds the references in a scene's or prototype's nested
 * children as well as in its roots, and reports the ones that name nothing. */
TEST_CASE(program_scene_children_are_referenced)
{
    char root[] = "/tmp/chibi-children-XXXXXX", path[256], ship[256];
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/scenes/Start.chibi", root);
    snprintf(ship, sizeof ship, "%s/prototypes/Ship.chibi", root);
    source_catalog_edit edits[] = {
        {
            .path = path,
            .text = "package asset\n"
                    "\n"
                    "import data \"game:assets\"\n"
                    "\n"
                    "pub Build :: () -> byte[] {\n"
                    "    return data.Encode(\n"
                    "        data.Object([\n"
                    "            data.Field.Named(\"version\", "
                    "data.Value.UInt(1)),\n"
                    "            data.Field.Named(\"name\", "
                    "data.Value.Text(\"Start\")),\n"
                    "            data.Field.Named(\"entities\", data.List([\n"
                    "                data.Object([\n"
                    "                    data.Field.Named(\"name\", "
                    "data.Value.Text(\"Ship\")),\n"
                    "                    data.Field.Named(\"prototype\", "
                    "data.Value.Text(\"Hull\")),\n"
                    "                    data.Field.Named(\"children\", "
                    "data.List([\n"
                    "                        data.Object([\n"
                    "                            data.Field.Named(\"name\", "
                    "data.Value.Text(\"Turret\")),\n"
                    "                            "
                    "data.Field.Named(\"children\", data.List([\n"
                    "                                data.Object([\n"
                    "                                    "
                    "data.Field.Named(\"name\", data.Value.Text(\"Barrel\")),\n"
                    "                                    "
                    "data.Field.Named(\"prototype\", "
                    "data.Value.Text(\"Gun\"))\n"
                    "                                ])\n"
                    "                            ]))\n"
                    "                        ])\n"
                    "                    ]))\n"
                    "                ])\n"
                    "            ]))\n"
                    "        ])\n"
                    "    )\n"
                    "}\n",
        },
        {
            .path = ship,
            .text = "package asset\n"
                    "\n"
                    "import data \"game:assets\"\n"
                    "\n"
                    "pub Build :: () -> byte[] {\n"
                    "    return data.Encode(\n"
                    "        data.Object([\n"
                    "            data.Field.Named(\"name\", "
                    "data.Value.Text(\"Ship\")),\n"
                    "            data.Field.Named(\"children\", data.List([\n"
                    "                data.Object([\n"
                    "                    data.Field.Named(\"name\", "
                    "data.Value.Text(\"Mast\")),\n"
                    "                    data.Field.Named(\"prototype\", "
                    "data.Value.Text(\"Pole\"))\n"
                    "                ])\n"
                    "            ]))\n"
                    "        ])\n"
                    "    )\n"
                    "}\n",
        }
    };
    source_catalog *project = source_catalog_edited(root, &catalog, edits, 2);
    const char *expected[] = { "Hull", "Gun", "Pole" };
    REQUIRE(source_catalog_reference_count(project) == 3);
    REQUIRE(source_catalog_diagnostic_count(project) == 3);
    for (int i = 0; i < 3; i++) {
        const source_catalog_reference *reference = &source_catalog_references(project)[i];
        REQUIRE(reference->kind == SOURCE_REFERENCE_PROTOTYPE);
        REQUIRE(!strcmp(reference->name, expected[i]));
        REQUIRE(strstr(source_catalog_diagnostic_at(project, i)->message, expected[i]));
    }
    source_catalog_free(project);
    rmdir(root);
    return true;
}
TEST_CASE(program_input_actions_read_through_typed_names)
{
    script_catalog_capability capabilities[] = {
        { .id = 10,
          .name = "Tick",
          .kind = SCRIPT_CAPABILITY_EVENT,
          .parameters = &tick_parameter,
          .parameter_count = 1 },
        { .id = script_catalog_id("capability", INPUT_ACTION_HELD),
          .name = INPUT_ACTION_HELD,
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .parameters = &action_parameter,
          .parameter_count = 1,
          .results = &held_result,
          .result_count = 1,
          .effects = SCRIPT_EFFECT_READ },
        { .id = script_catalog_id("capability", INPUT_ACTION_PRESSED),
          .name = INPUT_ACTION_PRESSED,
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .parameters = &action_parameter,
          .parameter_count = 1,
          .results = &pressed_result,
          .result_count = 1,
          .effects = SCRIPT_EFFECT_READ },
    };
    script_catalog native = catalog;
    native.capabilities = capabilities;
    native.capability_count = 3;
    char root[] = "/tmp/chibi-input-XXXXXX";
    REQUIRE(input_project(
        root,
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Jump\", data.Object([\n"
        "                data.Field.Named(\"sources\", data.List([\n"
        "                    data.Value.Text(\"Space\")\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    ));
    source_catalog *project = source_catalog_new(root, &native);
    /* An unsaved edit of the map replaces the file, and one without text
     * removes it. */
    char map[256];
    snprintf(map, sizeof map, "%s/" INPUT_MAP_FILE, root);
    source_catalog_edit edits[] = {
        { .path = map,
          .text = "package asset\n"
                  "\n"
                  "import data \"game:assets\"\n"
                  "\n"
                  "pub Build :: () -> byte[] {\n"
                  "    return data.Encode(\n"
                  "        data.Object([\n"
                  "            data.Field.Named(\"Dash\", data.Object([\n"
                  "                data.Field.Named(\"sources\", data.List([\n"
                  "                    data.Value.Text(\"X\")\n"
                  "                ]))\n"
                  "            ]))\n"
                  "        ])\n"
                  "    )\n"
                  "}\n" },
        { .path = map },
    };
    for (int i = 0; i < 2; i++) {
        source_catalog *edited = source_catalog_edited(root, &native, &edits[i], 1);
        const script_catalog *edited_view = source_catalog_view(edited);
        REQUIRE(!source_catalog_diagnostic_count(edited));
        REQUIRE(script_catalog_find_capability(edited_view, "Jump", SCRIPT_CAPABILITY_EVENT) < 0);
        REQUIRE(
            (script_catalog_find_capability(edited_view, "Dash", SCRIPT_CAPABILITY_EVENT) >=
             0) == !i
        );
        REQUIRE(acount(source_catalog_input(edited)) == !i);
        source_catalog_free(edited);
    }
    /* A project type that takes an input type's name is reported. */
    const script_catalog_type taken = { .id = script_catalog_id("type", "input_action"),
                                        .name = "input_action",
                                        .type = { .storage = PIN_F64 } };
    script_catalog clash = native;
    clash.types = &taken;
    clash.type_count = 1;
    source_catalog *clashing = source_catalog_new(root, &clash);
    REQUIRE(source_catalog_diagnostic_count(clashing) == 1);
    REQUIRE(strstr(source_catalog_diagnostic_at(clashing, 0)->message, "rename the type"));
    source_catalog_free(clashing);
    /* So is a project global that takes the name input. */
    const script_catalog_global global = { .id = script_catalog_id("global", "input"),
                                           .name = "input",
                                           .type = { .storage = PIN_F64 } };
    clash = native;
    clash.globals = &global;
    clash.global_count = 1;
    clashing = source_catalog_new(root, &clash);
    REQUIRE(source_catalog_diagnostic_count(clashing) == 1);
    REQUIRE(strstr(source_catalog_diagnostic_at(clashing, 0)->message, "rename the global"));
    source_catalog_free(clashing);
    /* An action named like an existing event would dispatch that event, so
     * each such action is reported. */
    source_catalog_edit lifecycle = {
        .path = map,
        .text = "package asset\n"
                "\n"
                "import data \"game:assets\"\n"
                "\n"
                "pub Build :: () -> byte[] {\n"
                "    return data.Encode(\n"
                "        data.Object([\n"
                "            data.Field.Named(\"Tick\", data.Object([\n"
                "                data.Field.Named(\"sources\", data.List([\n"
                "                    data.Value.Text(\"X\")\n"
                "                ]))\n"
                "            ])),\n"
                "            data.Field.Named(\"Jump\", data.Object([\n"
                "                data.Field.Named(\"sources\", data.List([\n"
                "                    data.Value.Text(\"Space\")\n"
                "                ]))\n"
                "            ]))\n"
                "        ])\n"
                "    )\n"
                "}\n",
    };
    clashing = source_catalog_edited(root, &native, &lifecycle, 1);
    REQUIRE(source_catalog_diagnostic_count(clashing) == 1);
    REQUIRE(
        strstr(source_catalog_diagnostic_at(clashing, 0)->message, "Input action Tick takes the name of the event Tick; rename the action")
    );
    REQUIRE(
        script_catalog_find_capability(source_catalog_view(clashing), "Jump", SCRIPT_CAPABILITY_EVENT) < 0
    );
    source_catalog_free(clashing);
    /* A project event may share an action's name: both raise that event. */
    char events[256], event[256];
    snprintf(events, sizeof events, "%s/events", root);
    snprintf(event, sizeof event, "%s/Jump.chibi", events);
    REQUIRE(!mkdir(events, 0700));
    FILE *file = fopen(event, "wb");
    REQUIRE(file);
    fputs(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(3)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Jump\")),\n"
        "            data.Field.Named(\"domain\", data.Value.Text(\"event\"))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        file
    );
    fclose(file);
    source_catalog *shared = source_catalog_new(root, &native);
    REQUIRE(!source_catalog_diagnostic_count(shared));
    REQUIRE(
        script_catalog_find_capability(source_catalog_view(shared), "Jump", SCRIPT_CAPABILITY_EVENT) >= 0
    );
    source_catalog_free(shared);
    unlink(event);
    rmdir(events);
    remove_input_project(root);
    REQUIRE(!source_catalog_diagnostic_count(project));
    const script_catalog *view = source_catalog_view(project);
    REQUIRE(script_catalog_find_capability(view, "Jump", SCRIPT_CAPABILITY_EVENT) >= 0);
    const char *source = "Jumper :: behavior {\n"
                         "    held: bool = false;\n"
                         "    pressed: bool = false;\n"
                         "    on Tick :: () {\n"
                         "        held = input.Jump.held;\n"
                         "        pressed = input.jump.pressed;\n"
                         "    }\n"
                         "}\n";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int i = 0; i < 2; i++) {
        prepare();
        REQUIRE(compile_catalog(source, backends[i], view));
        program = execution_program_new(module, world, NULL);
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value dt = { .type = PIN_F64, .d = 1 };
        jump_held = true;
        jump_pressed = false;
        REQUIRE(execution_instance_dispatch(instance, 10, &dt, 1, 0, 0, 1000) == FLOW_OK);
        REQUIRE(bool_state(instance, "held", true) && bool_state(instance, "pressed", false));
        jump_held = false;
        jump_pressed = true;
        REQUIRE(execution_instance_dispatch(instance, 10, &dt, 1, 0, 0, 1000) == FLOW_OK);
        REQUIRE(bool_state(instance, "held", false) && bool_state(instance, "pressed", true));
    }
    /* A misspelled action and the retired string reads do not compile. */
    const char *invalid[] = { "B :: behavior { x: bool = false; on Tick :: () "
                              "{ x = input.Jmp.held; } "
                              "}",
                              "B :: behavior { x: bool = false; on Tick :: () "
                              "{ x = held(\"Jump\"); } }" };
    for (int i = 0; i < 2; i++) {
        script_syntax *wrong = script_parse(invalid[i]);
        script_analysis *checked = script_analyze(wrong, view);
        REQUIRE(!checked->valid);
        REQUIRE(i || strstr(checked->diagnostics[0].message, "Unknown reflected member 'Jmp'"));
        script_analysis_free(checked);
        script_syntax_free(wrong);
    }
    source_catalog_free(project);
    return true;
}
TEST_CASE(program_scene_bindings_are_typed_names_before_play)
{
    script_catalog_capability *native_capabilities = NULL;
    for (int i = 0; i < catalog.capability_count; i++) {
        apush(native_capabilities, catalog.capabilities[i]);
    }
    const script_catalog_parameter key = { .name = "Name",
                                           .type = { .storage = PIN_NAME } };
    const script_catalog_parameter value = {
        .name = "Value",
        .type = { .storage = PIN_FLOW, .flags = SCRIPT_TYPE_UNTYPED }
    };
    apush(
        native_capabilities,
        ((script_catalog_capability){
            .id = script_catalog_id("capability", "Get Global Variable"),
            .name = "Get Global Variable",
            .kind = SCRIPT_CAPABILITY_FUNCTION,
            .parameters = &key,
            .parameter_count = 1,
            .results = &value,
            .result_count = 1,
            .pure = true,
            .effects = SCRIPT_EFFECT_READ,
        })
    );
    script_catalog native = catalog;
    native.capabilities = native_capabilities;
    native.capability_count = acount(native_capabilities);
    char root[] = "/tmp/chibi-globals-XXXXXX", prototype[256], scene[256];
    REQUIRE(mkdtemp(root));
    snprintf(prototype, sizeof prototype, "%s/prototypes/Room.chibi", root);
    snprintf(scene, sizeof scene, "%s/scenes/Start.chibi", root);
    const source_catalog_edit edits[] = {
        { .path = prototype,
          .text = "package asset\n"
                  "\n"
                  "import data \"game:assets\"\n"
                  "\n"
                  "pub Build :: () -> byte[] {\n"
                  "    return data.Encode(\n"
                  "        data.Object([\n"
                  "            data.Field.Named(\"name\", "
                  "data.Value.Text(\"Room\")),\n"
                  "            data.Field.Named(\"components\", data.Object([\n"
                  "                data.Field.Named(\"GlobalBinding\", "
                  "data.Object([\n"
                  "                    data.Field.Named(\"name\", "
                  "data.Value.Text(\"Level\"))\n"
                  "                ]))\n"
                  "            ])),\n"
                  "            data.Field.Named(\"children\", data.List([\n"
                  "                data.Object([\n"
                  "                    data.Field.Named(\"name\", "
                  "data.Value.Text(\"Player\")),\n"
                  "                    data.Field.Named(\"components\", "
                  "data.Object([\n"
                  "                        data.Field.Named(\"GlobalBinding\", "
                  "data.Object([\n"
                  "                            data.Field.Named(\"name\", "
                  "data.Value.Text(\"Player\"))\n"
                  "                        ]))\n"
                  "                    ]))\n"
                  "                ])\n"
                  "            ]))\n"
                  "        ])\n"
                  "    )\n"
                  "}\n" },
        { .path = scene,
          .text = "package asset\n"
                  "\n"
                  "import data \"game:assets\"\n"
                  "\n"
                  "pub Build :: () -> byte[] {\n"
                  "    return data.Encode(\n"
                  "        data.Object([\n"
                  "            data.Field.Named(\"name\", "
                  "data.Value.Text(\"Start\")),\n"
                  "            data.Field.Named(\"entities\", data.List([\n"
                  "                data.Object([\n"
                  "                    data.Field.Named(\"name\", "
                  "data.Value.Text(\"Room\")),\n"
                  "                    data.Field.Named(\"components\", "
                  "data.Object([\n"
                  "                        data.Field.Named(\"GlobalBinding\", "
                  "data.Object([\n"
                  "                            data.Field.Named(\"name\", "
                  "data.Value.Text(\"Level\"))\n"
                  "                        ]))\n"
                  "                    ])),\n"
                  "                    data.Field.Named(\"children\", "
                  "data.List([\n"
                  "                        data.Object([\n"
                  "                            data.Field.Named(\"name\", "
                  "data.Value.Text(\"Camera\")),\n"
                  "                            "
                  "data.Field.Named(\"components\", data.Object([\n"
                  "                                "
                  "data.Field.Named(\"GlobalBinding\", data.Object([\n"
                  "                                    "
                  "data.Field.Named(\"name\", data.Value.Text(\"Camera\"))\n"
                  "                                ]))\n"
                  "                            ]))\n"
                  "                        ])\n"
                  "                    ]))\n"
                  "                ])\n"
                  "            ]))\n"
                  "        ])\n"
                  "    )\n"
                  "}\n" },
    };
    source_catalog *project = source_catalog_edited(root, &native, edits, 2);
    REQUIRE(!source_catalog_diagnostic_count(project));
    const script_catalog *view = source_catalog_view(project);
    int scope = script_catalog_find_type(view, "SceneGlobals");
    REQUIRE(scope >= 0 && view->types[scope].field_count == 3);
    int field = script_catalog_find_field(&view->types[scope], "Level");
    REQUIRE(field >= 0);
    const script_catalog_field *level = &view->types[scope].fields[field];
    REQUIRE(level->type.storage == PIN_HANDLE && level->type.nominal == catalog.entity_type);
    REQUIRE(!level->writable && !strcmp(level->read_key, "Level"));
    int definitions = 0;
    for (int i = 0; i < source_catalog_definition_count(project); i++) {
        const source_catalog_definition *definition = &source_catalog_definitions(
            project
        )[i];
        if (definition->field) {
            REQUIRE(definition->owner == view->types[scope].id);
            REQUIRE(
                definition->range.end - definition->range.start ==
                (int)strlen(definition->name) + 2
            );
            definitions++;
        }
    }
    REQUIRE(definitions == 4);
    const char
        *source = "Reader :: behavior { x: double = 0; on Tick :: () { x "
                  "= globals.Level.Position.x; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int i = 0; i < 2; i++) {
        prepare();
        REQUIRE(compile_catalog(source, backends[i], view));
        bool reads = false;
        for (int j = 0; j < analysis->expression_count; j++) {
            const script_expression *expression = &analysis->expressions[j];
            if (expression->member == level->id) {
                reads = expression->effects & SCRIPT_EFFECT_READ;
            }
        }
        REQUIRE(reads);
        bound_level = positioned(12, false);
        binding_available = true;
        program = execution_program_new(module, world, NULL);
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value dt = { .type = PIN_F64, .d = 1 };
        REQUIRE(execution_instance_dispatch(instance, 10, &dt, 1, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "x", 12));
        binding_available = false;
        REQUIRE(execution_instance_dispatch(instance, 10, &dt, 1, 0, 0, 1000) == FLOW_ERR_INVALID_GRAPH);
        const execution_program_failure *failure = execution_program_error(program);
        REQUIRE(failure && failure->kind == BEHAVIOR_FUNCTION_HOST_DATA);
        REQUIRE(failure->node_id == level->id);
        REQUIRE(!strcmp(failure->message, "Get Global Variable references an unknown value"));
    }
    const char *invalid[] = {
        "B :: behavior { on Tick :: () { x := globals.Leve; } }",
        "B :: behavior { on Tick :: () { globals.Level = self; } }",
    };
    for (int i = 0; i < 2; i++) {
        script_syntax *wrong = script_parse(invalid[i]);
        script_analysis *checked = script_analyze(wrong, view);
        REQUIRE(!checked->valid);
        script_analysis_free(checked);
        script_syntax_free(wrong);
    }
    script_syntax *wrong = script_parse("B :: behavior { on Tick :: () { x := globals; } }");
    script_analysis *checked = script_analyze(wrong, view);
    REQUIRE(checked->valid);
    const script_lower_bindings bindings = { .global = global, .host = host };
    execution_module_plan *plan = script_lower(checked, view, &bindings);
    REQUIRE(plan && !plan->valid && plan->diagnostic_count);
    REQUIRE(strstr(plan->diagnostics[0].message, "namespace has no value"));
    execution_module_plan_free(plan);
    script_analysis_free(checked);
    script_syntax_free(wrong);
    /* Invalid namespace snapshots expose no field definitions with a missing
     * owner. Navigation can still inspect the surviving project declarations. */
    const script_catalog_global taken = { .id = 99,
                                          .name = "globals",
                                          .type = { .storage = PIN_F64 } };
    for (int i = 0; i < 2; i++) {
        script_catalog invalid = native;
        if (!i) {
            invalid.globals = &taken;
            invalid.global_count = 1;
        } else {
            invalid.capability_count--;
        }
        source_catalog *broken = source_catalog_edited(root, &invalid, edits, 2);
        REQUIRE(source_catalog_diagnostic_count(broken) == 1);
        for (int j = 0; j < source_catalog_definition_count(broken); j++) {
            REQUIRE(!source_catalog_definitions(broken)[j].field);
        }
        source_catalog_free(broken);
    }
    afree(native_capabilities);
    source_catalog_free(project);
    rmdir(root);
    return true;
}

/* Removes obsolete motion from a version 17 project while preserving an
 * unrelated SdfMotion scene name. */

TEST_CASE(program_constant_definitions_execute)
{
    const char *source =
        "Constants :: behavior {\n"
        "Values :: [2, 3]; Base: int : Values[0]; Multiplier :: Base + "
        "1;\n"
        "Bonus :: union { None | Amount(value: int) }; Extra :: "
        "Bonus.Amount(Base);\n"
        "answer: number = 0; fallback: number = 0;\n"
        "DamageFor :: (level: int) -> int { return level * Multiplier }\n"
        "OtherDamage :: (level: int) -> int { return level + Base }\n"
        "on Impulse :: () {\n"
        "Local :: 4; answer = DamageFor(Local) + OtherDamage(Local);\n"
        "switch answer { case 18: answer += 1; case: answer += 100; }\n"
        "switch answer { case 20: fallback = 1; case: { fallback = 2; } }\n"
        "switch Extra { case .None: fallback = 0; case .Amount(value): "
        "fallback += value; }\n"
        "}\n"
        "}";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        int states = 0;
        for (int i = 0; i < analysis->symbol_count; i++) {
            states += analysis->symbols[i].kind == SCRIPT_STATE;
        }
        REQUIRE(states == 2);
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        pin_value args[] = { { .type = PIN_F64, .d = 1 },
                             { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "answer", 19));
        REQUIRE(number_state(instance, "fallback", 4));
    }
    return true;
}

TEST_CASE(program_single_result_call_fields_read_the_result)
{
    const char
        *source = "B :: behavior { n: double = 0;P :: () -> (v: double) { "
                  "return(n + 1); }W :: () -> (v: double) { await Delay(1); "
                  "return(n + 10); }on Impulse :: () { x := P().v; "
                  "n += x; y := await W(); n += y.v;n += (await W()).v; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        for (int i = 1; i <= 3; i++) {
            REQUIRE(execution_program_update(program, i, 0, 1000) == FLOW_OK);
        }
        REQUIRE(number_state(instance, "n", 34));
    }
    return true;
}

TEST_CASE(program_results_are_inferred_from_returns)
{
    const char *
        source = "B :: behavior { n: double = 0;Faster :: (a: double, b: "
                 "double) { if a > b { return true } return false }Pick :: (a: "
                 "double, b: double) { if Faster(a, b) { return a } return b "
                 "}Split :: (x: double) { return(Low: x, High: x * 10) }Later "
                 ":: (x: double) { yield; return x + 1 }on Impulse :: (amount: "
                 "double) { n += Pick(2, 5); r := Split(amount); n += r.Low + "
                 "r.High; n += await Later(1); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        for (int i = 1; i <= 3; i++) {
            REQUIRE(execution_program_update(program, i, 0, 1000) == FLOW_OK);
        }
        REQUIRE(number_state(instance, "n", 18));
    }
    /* B's nested F calls A; A's own nested F is a different function. */
    const char *
        nested = "B :: behavior { n: double = 0;Outer :: () { F :: () { return "
                 "Inner() } return F() }Inner :: () { F :: () { return 2 } "
                 "return F() }on Impulse :: () { n = Outer() } }";
    prepare();
    REQUIRE(compile(nested, BEHAVIOR_BYTECODE));
    execution_instance *instance = execution_program_attach(
        program,
        ecs_entity_to_u64(ecs_world_create_entity(world))
    );
    const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                               { .type = PIN_BOOL, .b = true } };
    REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
    REQUIRE(number_state(instance, "n", 2));
    static const struct
    {
        const char *source, *message;
    } invalid[] = {
        { "B :: behavior { Count :: (n: double) { if n < 1 { return 0.0 } "
          "return Count(n - 1) + 1 } }",
          "Recursive functions need declared results" },
        { "B :: behavior { Pair :: (x: double) { return(x, x) } }", "Name each result" },
        { "B :: behavior { Mixed :: (x: double) { if x > 0 { return true } "
          "return x } }",
          "" },
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        prepare();
        syntax = script_parse(invalid[i].source);
        analysis = script_analyze(syntax, &catalog);
        REQUIRE(!analysis->valid);
        bool found = false;
        for (int d = 0; d < analysis->diagnostic_count; d++) {
            found |= strstr(analysis->diagnostics[d].message, invalid[i].message) != NULL;
        }
        REQUIRE(found);
    }
    return true;
}

TEST_CASE(program_task_with_several_results_awaits_a_tuple)
{
    const char
        *source = "B :: behavior { n: double = 0;P :: (x: double) -> (a: "
                  "double, b: double) { yield; return(x, x * 10); }on "
                  "Impulse :: (amount: double) { t := P(amount); r := await t; "
                  "if "
                  "r is not Cancelled { n += r.a + r.b; }s := await t; if s is "
                  "not Cancelled { n += s.b; }u := P(2); v := await u; if v is "
                  "not Cancelled { n += v.a; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        for (int i = 1; i <= 3; i++) {
            REQUIRE(execution_program_update(program, i, 0, 1000) == FLOW_OK);
        }
        REQUIRE(number_state(instance, "n", 23));
    }
    /* A task has no result fields until it is awaited. */
    prepare();
    REQUIRE(
        !compile("B :: behavior { n: double = 0;P :: (x: double) -> (a: double, b: double) { yield; return(x, x * 10); }on Impulse :: (amount: double) { t := P(amount); n = t.a } }", BEHAVIOR_BYTECODE)
    );
    return true;
}

TEST_CASE(program_library_call_starts_a_task)
{
    const char *library_source = "Store :: library { count: double = 10;Work "
                                 ":: () -> (local_count: double, position_x: "
                                 "double) {count += 1; await Delay(1); count "
                                 "+= 1; return(count, self.Position.x); } }";
    const char
        *owner_source = "Owner :: behavior { n: double = 0; local_count: "
                        "double = 0;on Impulse :: () { t := "
                        "library_work(); n += 1;if t { n += 10; } result := "
                        "await t; if result is not Cancelled { local_count = "
                        "result.local_count; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_module(library_source, backends[b]));
        imported = module;
        module = NULL;
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        REQUIRE(compile(owner_source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(positioned(3, false))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 11) && number_state(instance, "local_count", 0));
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(
            number_state(instance, "local_count", 12) &&
            execution_instance_pending(instance) == 0
        );
    }
    return true;
}

TEST_CASE(program_library_task_writes_the_callers_state)
{
    const char
        *library_source = "Lib :: library { count: double = 10;Bump :: (x: ref "
                          "double) { yield; x += 1; count += 1 } }";
    const char *owner_source = "Owner :: behavior { n: double = 5;on "
                               "Impulse :: () { library_bump(n) } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_module(library_source, backends[b]));
        imported = module;
        module = NULL;
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        REQUIRE(compile(owner_source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 6) && execution_instance_pending(instance) == 0);
    }
    return true;
}
TEST_CASE(program_task_keeps_state_and_component_references)
{
    const char
        *source = "R :: behavior { n: double = 0;Bump :: (x: ref double) -> () "
                  "{ yield; x += 1; yield; x += 1; }on Impulse :: (amount: "
                  "double, "
                  "enabled: bool) { Bump(n); Bump(self.Position.x); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = positioned(5, false);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 0));
        for (int i = 1; i <= 3; i++) {
            REQUIRE(execution_program_update(program, i, 0, 1000) == FLOW_OK);
        }
        REQUIRE(number_state(instance, "n", 2) && execution_instance_pending(instance) == 0);
        pin_value x;
        REQUIRE(ecs_component_read_field(world, owner, 501, 0, &x) == ECS_OK && x.d == 7);
    }
    return true;
}
/* Found porting Celeste's player: a zero-argument function that only calls
 * others, number array indexes and range bounds, and clamp over an int bound. */
TEST_CASE(program_nested_calls_number_indexes_and_mixed_clamp)
{
    const char
        *source = "N :: behavior { n: double = 0; w: int = 4; a: double[] = "
                  "[1, 2, 3];Leaf :: () -> (v: double) { j := 1.5; "
                  "return(a[int(j)]) }Inner :: () -> (v: double) { "
                  "return(Leaf() + "
                  "1) }Start :: () -> (v: double) { return(Inner()) }Bump :: "
                  "() { n += 1 } Relay :: () { Bump() }Kick :: () { Relay() "
                  "}on Impulse :: (amount: double) { i := 2; a[i] = 10;n = "
                  "Start() + a[2] + clamp(amount, 0, w); Kick();j := 1.5; for "
                  "(k in 1..int(j)) { n += k } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_pack(source, backends[b], &NATIVE_ARITHMETIC_DEFINITION_PACK));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 2.5 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 17.5));
    }
    return true;
}
/* Found animating Celeste's sprite: a result takes the type of its first
 * return, a literal settles by its spelling, and other returns convert to
 * it. */
TEST_CASE(program_returns_convert_to_their_result_type)
{
    const char
        *source = "M :: behavior { n: double = 0; k: int = 3;Half :: (v: int) "
                  "{ return v % 2 }First :: () { if k == 2 { return 8.5 } "
                  "return Half(k) }Literal :: () { if k == 2 { return 8.0 } "
                  "return Half(k) }Declared :: () -> (r: double) { return "
                  "Half(k) }on Impulse :: () {n = First() * 0.5 + "
                  "Literal() * 0.5 + Declared() } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 2));
    }
    return true;
}
TEST_CASE(program_yield_resumes_on_the_next_update_without_time)
{
    const char
        *source = "Y :: behavior { done: double = 0;Short :: () { yield; done "
                  "+= 1 }on Impulse :: () { Short() } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_pump(program, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "done", 0));
        REQUIRE(execution_program_update(program, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "done", 1));
    }
    return true;
}
TEST_CASE(program_task_state_awaits_its_function_results)
{
    const char
        *source = "P :: behavior { pulse: task; power: double = 0;Charge :: "
                  "(time: double) -> (power: double) { yield; return(time * "
                  "2); }on Impulse :: (amount: double) { pulse = "
                  "Charge(amount); "
                  "answer := await pulse; if answer is double value { power = "
                  "value } } }";
    /* The await is analyzed before the assignment that types the state. */
    const char
        *reordered = "P :: behavior { pulse: task; power: double = 0;on "
                     "Impulse :: (amount: double) { Start(amount); answer := "
                     "await pulse; if answer is double value { power = value } "
                     "}Start :: (time: double) { pulse = Charge(time) }Charge "
                     ":: (time: double) -> (power: double) { yield; "
                     "return(time * 2); }Other :: () { pulse := 1 } }";
    const char
        *local = "P :: behavior { power: double = 0;Charge :: (time: double) "
                 "-> (power: double) { yield; return(time * 2); }on "
                 "Impulse :: (amount: double) { t: task = Charge(amount); "
                 "answer "
                 ":= await t; if answer is double value { power = value } } }";
    const char
        *wrapped = "P :: behavior { pulse: task; power: double = 0;on "
                   "Impulse :: (amount: double) { Start(amount); answer := "
                   "await pulse; if answer is double value { power = "
                   "value } }Start :: (time: double) { pulse = "
                   "(Charge(time)) }Charge :: (time: double) -> (power: "
                   "double) { yield; return(time * 2); } }";
    const char
        *qualified = "P :: behavior { pulse: task; power: double = 0;on "
                     "Impulse :: (amount: double) { Start(amount); answer "
                     ":= await pulse; if answer is double value { power "
                     "= value } }Start :: (time: double) { state.pulse "
                     "= Charge(time) }Charge :: (time: double) -> "
                     "(power: double) { yield; return(time * 2); } }";
    /* A nested callable's own `pulse` does not shadow the outer one. */
    const char *
        nested = "P :: behavior { pulse: task; power: double = 0;on "
                 "Impulse :: (amount: double) { Start(amount); answer := await "
                 "pulse; if answer is double value { power = value } }Start "
                 ":: (time: double) { Other :: (pulse: double) { } pulse = "
                 "Charge(time) }Charge :: (time: double) -> (power: double) { "
                 "yield; return(time * 2); } }";
    /* A sibling block's local `pulse` does not shadow the state. */
    const char
        *sibling = "P :: behavior { pulse: task; power: double = 0;on "
                   "Impulse :: (amount: double) { Start(amount); answer := "
                   "await "
                   "pulse; if answer is double value { power = value } }Start "
                   ":: (time: double) { if (time > 99) { pulse := 1 } pulse = "
                   "Charge(time) }Charge :: (time: double) -> (power: double) "
                   "{ yield; return(time * 2); } }";
    /* A nested callee types the state for awaits analyzed after it. */
    const char
        *inner = "P :: behavior { pulse: task; power: double = 0;on "
                 "Impulse :: (amount: double) { Start(amount) }Start :: (time: "
                 "double) { Charge :: (time: double) -> (power: double) { "
                 "yield; return(time * 2); } pulse = Charge(time); answer := "
                 "await pulse; if answer is double value { power = value } } }";
    const char *sources[] = { source,    reordered, local,   wrapped,
                              qualified, nested,    sibling, inner };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 16; b++) {
        prepare();
        REQUIRE(compile(sources[b / 2], backends[b % 2]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        const pin_value args[] = { { .type = PIN_F64, .d = 3 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "power", 6));
    }
    /* A task of an unknown function leaves the state without results. */
    prepare();
    syntax = script_parse("P :: behavior { pulse: task; power: double = 0;Charge :: (time: double) -> (power: double) { yield; return(time) }Keep :: (t: task) { pulse = t }on Impulse :: (amount: double) { pulse = Charge(amount); power = await pulse } }");
    analysis = script_analyze(syntax, &catalog);
    REQUIRE(!analysis->valid);
    /* So does a state write of another function's task, before or after
     * the await. */
    const char *writes[] = {
        "P :: behavior { pulse: task; power: double = 0;Charge :: (time: "
        "double) -> (power: double) { yield; return(time) }Other :: () -> "
        "(name: string) { yield; return(\"x\") }on Impulse :: (amount: double) "
        "{ "
        "pulse = Charge(amount); power = await pulse }Swap :: () { "
        "set_variable<pulse>(Other()) } }",
        "P :: behavior { pulse: task; power: double = 0;Charge :: (time: "
        "double) -> (power: double) { yield; return(time) }Other :: () -> "
        "(name: string) { yield; return(\"x\") }Swap :: () { "
        "set_variable<pulse>(Other()) }on Impulse :: (amount: double) { pulse "
        "= "
        "Charge(amount); power = await pulse } }"
    };
    for (int i = 0; i < 4; i++) {
        prepare();
        REQUIRE(
            !compile_pack(writes[i / 2], i % 2 ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE, &NATIVE_STATE_DEFINITION_PACK)
        );
    }
    return true;
}
TEST_CASE(program_task_handles_belong_to_their_instance)
{
    const char *source = "P :: behavior { pulse: task; alive: double = "
                         "0;Charge :: (time: double) -> (power: double) { "
                         "yield; return(time) }on Impulse :: (amount: double, "
                         "enabled: bool) { "
                         "if enabled { pulse = Charge(amount) } else { alive = "
                         "0; if pulse { alive = 1 } } } }";
    prepare();
    REQUIRE(compile(source, BEHAVIOR_BYTECODE));
    execution_instance *a = execution_program_attach(
        program,
        ecs_entity_to_u64(ecs_world_create_entity(world))
    );
    execution_instance *b = execution_program_attach(
        program,
        ecs_entity_to_u64(ecs_world_create_entity(world))
    );
    const pin_value start[] = { { .type = PIN_F64, .d = 1 },
                                { .type = PIN_BOOL, .b = true } };
    const pin_value check[] = { { .type = PIN_F64, .d = 0 },
                                { .type = PIN_BOOL, .b = false } };
    REQUIRE(execution_instance_dispatch(a, 11, start, 2, 0, 0, 1000) == FLOW_OK);
    REQUIRE(execution_instance_dispatch(b, 11, start, 2, 0, 0, 1000) == FLOW_OK);
    pin_value handle = *execution_instance_state(a, state_id("pulse"));
    REQUIRE(handle.u64 != execution_instance_state(b, state_id("pulse"))->u64);
    /* B running its own task does not make A's handle live in B. */
    REQUIRE(execution_instance_set_state(b, state_id("pulse"), handle) == 0);
    REQUIRE(execution_instance_dispatch(b, 11, check, 2, 0, 0, 1000) == FLOW_OK);
    REQUIRE(number_state(b, "alive", 0));
    REQUIRE(execution_instance_dispatch(a, 11, check, 2, 0, 0, 1000) == FLOW_OK);
    REQUIRE(number_state(a, "alive", 1));
    return true;
}
TEST_CASE(program_component_paths_test_presence)
{
    const char *
        source = "H :: behavior { n: double = 0;on Impulse :: (amount: double, "
                 "enabled: bool) {if self.Velocity { n += 1 }if "
                 "!self.Velocity { n += 10 }if self.Velocity != nil && "
                 "self.Position { n += 100 }if self.Velocity == nil || "
                 "self.Velocity.x > 3 { n += 1000 } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 4; b++) {
        prepare();
        REQUIRE(compile(source, backends[b % 2]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(positioned(1, b / 2))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", b / 2 ? 1101 : 1010));
    }
    return true;
}
TEST_CASE(program_branch_completes_inside_action_continuation)
{
    /* The inner action reads the outer action's result when the outer
     * continuation starts it, before and after resuming. */
    const char *bodies[] = { "n += 1\n inner()", "n += 1\n await Wait()\n inner()" };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int c = 0; c < 2; c++) {
        char source[PROGRAM_SOURCE_CAPACITY];
        int length = snprintf(
            source,
            sizeof source,
            "B :: behavior { n: double = 0\n"
            "Count :: () -> (shown: double) { return 2 }\n"
            "Wait :: () { yield }\n"
            "action outer = Count()\n"
            "action inner = branch_if(Condition: outer.shown > 0)\n"
            "on Impulse :: (amount: double, enabled: bool) { outer() }\n"
            "after outer { %s }\n"
            "after inner.True { n += 10 }\n"
            "after inner.False { n += 100 } }",
            bodies[c]
        );
        REQUIRE(length > 0 && (size_t)length < sizeof source);
        for (int b = 0; b < 2; b++) {
            prepare();
            REQUIRE(compile_pack(source, backends[b], &NATIVE_STATE_DEFINITION_PACK));
            execution_instance *instance = execution_program_attach(
                program,
                ecs_entity_to_u64(positioned(1, 1))
            );
            const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                       { .type = PIN_BOOL, .b = true } };
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
            REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
            REQUIRE(number_state(instance, "n", 11));
        }
    }
    return true;
}
TEST_CASE(program_parameters_defaults_and_contextual_words)
{
    const char *source = "V :: behavior { n: double = 0\n"
                         "m: double = 2\n"
                         "k: double = 0\n"
                         "who: entity\n"
                         "tiles: int[]\n"
                         "Approach :: (value: double, from: double) -> (r: "
                         "double) { return value - from }\n"
                         "Step :: (left: double) -> (r: double) { while left > "
                         "0 { left -= 1 } return left }\n"
                         "Drain :: (left: double) -> (r: double) { while left "
                         "> 0 { left -= 1\n"
                         " yield } return left + 7 }\n"
                         "on Impulse :: (amount: double) { split := 1.0\n"
                         "value := amount\n"
                         "n = Approach(value, split) + Step(m)\n"
                         "k = await Drain(m)\n"
                         "who = self } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 5 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        for (int i = 1; i <= 3; i++) {
            REQUIRE(execution_program_update(program, i, 0, 1000) == FLOW_OK);
        }
        REQUIRE(number_state(instance, "n", 4) && number_state(instance, "m", 2));
        REQUIRE(number_state(instance, "k", 7));
    }
    return true;
}
TEST_CASE(program_task_shares_boxed_locals)
{
    const char
        *source = "B :: behavior { seen: double = 0; kept: double = 0;Bump :: "
                  "(x: ref double) -> () { yield; x += 1; yield; x += 10; "
                  "}Keep :: (x: ref double) -> () { yield; x += 1; yield; kept "
                  "+= x; }Relay :: (x: ref double) -> () { Keep(x); }Pair :: "
                  "(x: double) -> (a: double, b: double) { return(a: x * 1000 "
                  "+ 5, b: 0) }on Impulse :: (amount: double) {if amount == 0 "
                  "{ v "
                  ":= 1.0; t := Bump(v); v += 100; await t; seen = v; }else if "
                  "amount == 1 { w := 5.0; Keep(w); }else if amount == 2 { i "
                  ":= 0.0; while i < 2 { u := i * 10; Keep(u); i += 1; } }else "
                  "if amount == 3 { j := 0.0; while j < 2 { z := j * 100 + 2; "
                  "Relay(z); j += 1; } }else { k := 0.0; while k < 2 { p := "
                  "Pair(k); Keep(p.a); k += 1; } } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        for (int i = 0; i < 5; i++) {
            const pin_value args[] = { { .type = PIN_F64, .d = i },
                                       { .type = PIN_BOOL, .b = true } };
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        }
        for (int i = 1; i <= 3; i++) {
            REQUIRE(execution_program_update(program, i, 0, 1000) == FLOW_OK);
        }
        /* The caller and its task share v; each declaration of u, of z
         * through Relay, and of the tuple p is its own; w and z outlive
         * their callers. */
        REQUIRE(number_state(instance, "seen", 112));
        REQUIRE(number_state(instance, "kept", 6 + 1 + 11 + 3 + 103 + 6 + 1006));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}

TEST_CASE(program_assignments_and_reference_writes_do_not_allocate)
{
    /* Plain state and local writes store in place, and a reference write of
     * a number needs no scratch storage. */
    const char *source = "R :: behavior { n: double = 0; m: double = 0;Add :: "
                         "(x: ref double) -> () { i := 0.0; while i < 1000 { x "
                         "+= 1; i += 1; } }on Impulse :: () { i := "
                         "0.0; while i < 1000 { n += 1; i += 1; } Add(m); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        uint64_t blocks = arena_block_allocations();
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000000) == FLOW_OK);
        REQUIRE(arena_block_allocations() - blocks < 16);
        REQUIRE(number_state(instance, "n", 1000) && number_state(instance, "m", 1000));
    }
    return true;
}

TEST_CASE(program_repeated_helpers_have_bounded_allocations)
{
    const char
        *source = "B :: behavior { n: double = 0;Twice :: (x: double) -> (v: "
                  "double) { return x * 2 }on Impulse :: () { i := "
                  "0.0; while i < 256 {n += Twice(i); i += 1 } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        xalloc_stats before, after;
        xalloc_stats_get(&before);
        uint64_t blocks = arena_block_allocations();
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000000) == FLOW_OK);
        xalloc_stats_get(&after);
        REQUIRE(number_state(instance, "n", 65280));
        /* A single activation can repeat an ordinary helper without allocating
         * a fresh invocation and arena on each iteration. */
        REQUIRE(after.calls - before.calls < 64);
        REQUIRE(arena_block_allocations() - blocks < 16);
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}

TEST_CASE(program_reused_helpers_reset_locals_and_preserve_nested_calls)
{
    const char
        *source = "B :: behavior { n: double = 0; m: double = 0;Add :: (x: ref "
                  "double, v: double) -> (r: double) {scratch: double = 0; if "
                  "v > 0 { scratch = v }x += scratch; return x }Other :: (v: "
                  "double) -> (r: double) { return v * 10 }Count :: (v: "
                  "double) -> (r: double) {if v <= 0 { return 0 } return "
                  "Count(v - 1) + 1 }Outer :: (v: double) -> (r: double) { "
                  "return Count(v) + Other(v) }on Impulse :: () {n "
                  "+= Add(m, 2); n += Add(m, -1);n += Other(3); n += Add(m, "
                  "4);n += Outer(3); n += Outer(2); n += Count(4); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 10000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 99));
        REQUIRE(number_state(instance, "m", 6));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}

TEST_CASE(program_reused_helpers_keep_managed_results_and_resume_fresh_locals)
{
    const char
        *source = "B :: behavior { n: double = 0; text: string = \"\";Text :: "
                  "() -> (r: string) { return managed_text() }Tiles :: (v: "
                  "double) -> (r: double[]) { return [v, v + 1] }Work :: "
                  "(pause: bool) -> (r: double) {local: double = 0; if !pause "
                  "{ local = 7; return local }await Delay(1); return local }on "
                  "Impulse :: () {saved := Text(); replacement := "
                  "Text(); text = saved;first := Tiles(3); second := Tiles(9); "
                  "second[0] = 100;n = first[0] + first[1] + second[0];n += "
                  "await Work(false); n += await Work(true); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        managed_text_calls = 0;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 10000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 114));
        REQUIRE(memory_text_state(instance, "text", 0));
        REQUIRE(execution_instance_pending(instance) == 1);
        REQUIRE(execution_program_update(program, 1, 0, 10000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 114));
        REQUIRE(memory_text_state(instance, "text", 0));
        REQUIRE(managed_text_calls == 2);
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}

static void count_component_write(ecs_world *, ecs_event, ecs_entity, ecs_component_id, void *user)
{
    int *count = user;
    (*count)++;
}

TEST_CASE(program_component_scalar_reference_writes_do_not_copy_rows)
{
    const char
        *source = "R :: behavior { Add :: (x: ref double, y: ref double) -> () "
                  "{ i := 0.0; while i < 1000 { x += 1; y += 1; i += 1; } }on "
                  "Impulse :: () { Add(self.Position.x, "
                  "self.Position.x); self.Position.y = 7; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = positioned(5, false);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        int writes = 0;
        REQUIRE(ecs_world_observe(world, ECS_ON_SET, 501, count_component_write, &writes) > 0);
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        uint64_t blocks = arena_block_allocations();
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000000) == FLOW_OK);
        REQUIRE(arena_block_allocations() - blocks < 32);
        const program_position *position = ecs_component_data_const(world, owner, 501);
        REQUIRE(position && position->x == 2005 && position->y == 7);
        REQUIRE(writes == 0);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(writes == 1);
    }
    return true;
}

TEST_CASE(program_pattern_bindings_share_and_spellings)
{
    const char *operators[] = { "&&", "and" };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int op = 0; op < 2; op++) {
        char source[PROGRAM_SOURCE_CAPACITY];
        snprintf(
            source,
            sizeof source,
            "Patterns :: behavior { Choice :: union { Empty | Payload(value: "
            "number) }; "
            "total: number = 0; "
            "Check :: (value: number | Cancelled) -> number { score: number = "
            "0; "
            "if value is number left %s left > 0 { score += value; } "
            "if true %s value is number right { score += right; } "
            "if false %s value is number skipped { score += 100; } return "
            "score; } "
            "on Impulse :: (amount: double, enabled: bool) { total = Check(2) "
            "+ "
            "Check(Cancelled); "
            "tagged := Choice.Payload(3); "
            "if tagged is .Payload(left_tag) %s left_tag > 0 { total += "
            "left_tag; } "
            "if true %s tagged is .Payload(right_tag) { total += right_tag; } "
            "} }",
            operators[op],
            operators[op],
            operators[op],
            operators[op],
            operators[op]
        );
        for (int backend = 0; backend < 2; backend++) {
            prepare();
            REQUIRE(compile(source, backends[backend]));
            ecs_entity owner = ecs_world_create_entity(world);
            execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
            REQUIRE(instance);
            const pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL } };
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 10000) == FLOW_OK);
            REQUIRE(number_state(instance, "total", 10));
        }
    }
    return true;
}

TEST_CASE(program_switches_use_narrowed_union_tags)
{
    const char
        *source = "Narrowed :: behavior { Choice :: union { A | B(value: "
                  "number) }; total: number = 0; Positive :: (value: Choice) "
                  "-> number { if value is Choice.B { switch value { case "
                  ".B(payload): return payload; } } return 1; } Negative :: "
                  "(value: Choice) -> number { if value is Choice.A { return "
                  "2; } switch value { case .B(payload): return payload; } } "
                  "on Impulse :: () { total = Positive(Choice.A) + "
                  "Positive(Choice.B(3)) + Negative(Choice.A) + "
                  "Negative(Choice.B(4)); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        REQUIRE(compile(source, backends[backend]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        const pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 10000) == FLOW_OK);
        REQUIRE(number_state(instance, "total", 10));
    }
    return true;
}

TEST_CASE(program_general_unions_construct_match_and_extract)
{
    const char *source =
        "UnionValues :: behavior { AttackState :: union { Idle | "
        "Windup(remaining: number) | Active(damage: number) }; Status :: union "
        "{ \"Yes\" | \"No\" }; total := 0.0; phase: AttackState = "
        "AttackState.Windup(0.3); authored: Status = \"Yes\"; zero: number? = "
        "0; Empty :: () -> number? { return nil; } Default :: () -> (value: "
        "AttackState = AttackState.Idle) { return; } Extract :: (value: number "
        "| Cancelled) -> number? { return value as number; } on "
        "Impulse :: () { default_phase := Default(); if "
        "default_phase is AttackState.Idle { total += 1; } switch phase { case "
        ".Idle: total += 10; case .Windup(remaining): total += remaining; case "
        ".Active(damage): total += damage; } status: Status = authored; switch "
        "status { case \"Yes\": total += 1; case \"No\": total += 2; } value: "
        "number | Cancelled = 0; if value is number num_value { total += "
        "num_value + 3; } present := value as number; if present { total += "
        "present + 4; } if present is number zero_number { total += "
        "zero_number + 1; } returned := Extract(value); if returned { total += "
        "returned; } absent := value as bool; if absent { total += 100; } "
        "empty := Empty(); if empty { total += 100; } if zero { total += zero; "
        "} flag: bool | Cancelled = false; flag_value := flag as bool; if "
        "flag_value { if !flag_value { total += 5; } } phase = "
        "AttackState.Active(2); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int i = 0; i < 2; i++) {
        prepare();
        REQUIRE(compile(source, backends[i]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 10000) == FLOW_OK);
        REQUIRE(number_state(instance, "total", 15.3));
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 10000) == FLOW_OK);
        REQUIRE(number_state(instance, "total", 32.3));
    }
    return true;
}

TEST_CASE(program_union_arrays_keep_owned_writable_values)
{
    /* Two preparation parses and four dispatches, each capped at 10000
     * instructions. All arrays contain at most two tagged scalar values. */
    const char
        *source = "UnionArrays :: behavior { "
                  "Value :: union { Item(value: int) }; "
                  "total: double = 0; saved_result: Value[] = []; "
                  "Read :: (value: Value) -> int { "
                  "switch value { case .Item(number): return number; } } "
                  "Edit :: (values: Value[]) -> Value[] { "
                  "local: Value[] = values; "
                  "local = append(local, Value.Item(2)); "
                  "local[0] = Value.Item(3); return local; } "
                  "on Impulse :: (enabled: bool) { "
                  "if enabled { "
                  "original: Value[] = [Value.Item(1)]; copy := original; "
                  "changed := Edit(original); saved_result = changed; "
                  "total = Read(original[0])*1000 + Read(copy[0])*100 + "
                  "Read(changed[0])*10 + Read(changed[1]); "
                  "} else { total = Read(saved_result[0])*10 + "
                  "Read(saved_result[1]); } "
                  "} }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_pack(source, backends[b], &NATIVE_ARRAY_DEFINITION_PACK));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 10000) == FLOW_OK);
        REQUIRE(number_state(instance, "total", 1132));
        args[1].b = false;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 10000) == FLOW_OK);
        REQUIRE(number_state(instance, "total", 32));
    }
    return true;
}

TEST_CASE(program_reduction_updates_preserve_clock_and_event_order)
{
    enum
    {
        COUNT = sizeof capabilities / sizeof *capabilities
    };
    script_catalog_capability with_clock[COUNT + 1];
    memcpy(with_clock, capabilities, sizeof capabilities);
    with_clock[COUNT] = (script_catalog_capability){ .id = 61,
                                                     .name = "InstanceClock",
                                                     .kind = SCRIPT_CAPABILITY_FUNCTION,
                                                     .results = &failed_result,
                                                     .result_count = 1,
                                                     .effects = SCRIPT_EFFECT_READ };
    script_catalog active = catalog;
    active.capabilities = with_clock;
    active.capability_count = COUNT + 1;
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    /* Ordinary update, reducing update, reducing step, and a paused update. */
    for (int backend = 0; backend < 2; backend++) {
        for (int mode = 0; mode < 4; mode++) {
            prepare();
            ecs_entity entity = positioned(0, true);
            uint64_t owner = ecs_entity_to_u64(entity);
            char source[PROGRAM_SOURCE_CAPACITY];
            snprintf(
                source,
                sizeof source,
                "Ordered :: system(p:Position,v:Velocity) { "
                "resumed:retain double=0; event_clock:retain double=0; "
                "queued_value:retain double=0; finished:retain double=0; "
                "on Impulse :: (amount: double, enabled: bool) { "
                "if enabled { await Delay(1); resumed=InstanceClock(); v.x=7; "
                "} "
                "else { event_clock=InstanceClock(); queued_value=p.x; "
                "v.x=100; } } "
                "%sp.x+=v.x; finished+=1; }",
                mode ? "reduce " : ""
            );
            REQUIRE(compile_catalog(source, backends[backend], &active));
            program = execution_program_new(module, world, NULL);
            REQUIRE(program);
            REQUIRE(execution_program_pump(program, 0, 0, 1000) == FLOW_OK);
            execution_instance *instance = execution_program_instance(program, owner);
            REQUIRE(instance);
            pin_value arguments[] = { { .type = PIN_F64 },
                                      { .type = PIN_BOOL, .b = true } };
            REQUIRE(execution_instance_dispatch(instance, 11, arguments, 2, 0, 0, 1000) == FLOW_OK);
            REQUIRE(execution_instance_pending(instance) == 1);
            arguments[1].b = false;
            REQUIRE(execution_program_emit(program, 11, owner, arguments, 2) == FLOW_OK);
            uint64_t function = 0, point = 0;
            if (mode == 3) {
                debugger = execution_program_inspector(program);
                REQUIRE(debugger);
                for (int i = 0; i < execution_module_function_count(module); i++) {
                    execution_module_function_info info = execution_module_function_at(module, i);
                    if (info.kind == EXECUTION_FUNCTION_SYSTEM) {
                        function = info.id;
                    }
                }
                for (int i = 0; i < analysis->statement_count; i++) {
                    const script_statement *s = &analysis->statements[i];
                    if (s->kind == SCRIPT_STMT_ASSIGN && s->expression >= 0 &&
                        analysis->expressions[s->expression].symbol >= 0 &&
                        !strcmp(
                            analysis
                                ->symbols[analysis->expressions[s->expression].symbol]
                                .name,
                            "finished"
                        )) {
                        script_occurrences *occurrences = script_occurrences_new(analysis);
                        point = script_record_id(occurrences, SCRIPT_RECORD_STATEMENT, i);
                        script_occurrences_free(occurrences);
                    }
                }
                REQUIRE(function && point);
                REQUIRE(
                    execution_inspector_breakpoint(debugger, execution_module_id(module), function, point, true)
                );
            }
            int status = mode == 2 ? execution_program_step(program, 1, .5, 1000)
                                   : execution_program_update(program, 1, .5, 1000);
            if (mode == 3) {
                REQUIRE(status == FLOW_PAUSED_RESULT);
                REQUIRE(number_state(instance, "resumed", .5));
                REQUIRE(number_state(instance, "event_clock", 0));
                REQUIRE(
                    ((const program_position *)ecs_component_data_const(world, entity, 501))
                        ->x == 0
                );
                REQUIRE(execution_program_memory_usage(program).reduction_contributions == 1);
                REQUIRE(execution_program_update(program, 20, 5, 1000) == FLOW_PAUSED_RESULT);
                REQUIRE(number_state(instance, "event_clock", 0));
                REQUIRE(
                    execution_inspector_breakpoint(debugger, execution_module_id(module), function, point, false)
                );
                REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_RESUME) == FLOW_OK);
                status = execution_program_update(program, 30, 7, 1000);
            }
            REQUIRE(status == FLOW_OK);
            REQUIRE(number_state(instance, "resumed", .5));
            REQUIRE(number_state(instance, "event_clock", .5));
            REQUIRE(number_state(instance, "queued_value", 7));
            REQUIRE(number_state(instance, "finished", 1));
            REQUIRE(
                ((const program_position *)ecs_component_data_const(world, entity, 501))
                    ->x == 7
            );
            REQUIRE(
                ((const program_velocity *)ecs_component_data_const(world, entity, 502))
                    ->x == 100
            );
        }
    }
    return true;
}
TEST_CASE(program_reduction_query_rows_count_filtered_candidates)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        ecs_entity entities[3];
        for (int i = 0; i < 3; i++) {
            entities[i] = positioned(0, true);
            ((program_velocity *)ecs_component_data(world, entities[i], 502))->x = i - 1;
        }
        REQUIRE(
            compile_module("Count :: system( p:Position,v:Velocity) where v.x>0 { reduce p.x+=v.x; }", backends[backend])
        );
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        REQUIRE(execution_program_prepare_system(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_memory_usage(program).query_rows == 3);
        REQUIRE(execution_program_run_system(program) == FLOW_OK);
        REQUIRE(execution_program_memory_usage(program).query_rows == 3);
        REQUIRE(execution_program_finish_system(program) == FLOW_OK);
        REQUIRE(execution_program_memory_usage(program).query_rows == 3);
        REQUIRE(execution_program_update(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_memory_usage(program).query_rows == 3);
        for (int i = 0; i < 3; i++) {
            REQUIRE(
                ((const program_position *)ecs_component_data_const(world, entities[i], 501))
                    ->x == (i == 2 ? 2 : 0)
            );
        }
    }
    return true;
}
void test_language_query_program()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(program_reduction_updates_preserve_clock_and_event_order);
    RUN_TEST_CASE(program_reduction_query_rows_count_filtered_candidates);
    RUN_TEST_CASE(program_query_sources_resolve_current_targets_and_nearest_ancestors);
    RUN_TEST_CASE(program_query_required_path_keeps_one_instance_per_root);
    RUN_TEST_CASE(program_query_joins_filter_targets_and_refresh_across_suspension);
    RUN_TEST_CASE(program_query_optional_joins_preserve_missing_target_exclusions);
    RUN_TEST_CASE(program_relationship_loops_stream_targets_explicitly);
    RUN_TEST_CASE(program_query_chained_variables_filter_values_and_keep_event_tuples);
    RUN_TEST_CASE(program_query_rows_keep_locals_and_cancel_removed_tuple);
    RUN_TEST_CASE(program_query_changed_preserves_busy_published_writes);
    RUN_TEST_CASE(program_query_admission_is_frozen_before_row_writes);
    RUN_TEST_CASE(program_query_value_filters_preserve_short_circuit_failure_order);
    RUN_TEST_CASE(program_query_predicates_reuse_bounded_scratch_and_fresh_values);
    pu_clear_setup();
}

void test_language_program()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(program_reduction_updates_preserve_clock_and_event_order);
    RUN_TEST_CASE(program_reduction_query_rows_count_filtered_candidates);
    RUN_TEST_CASE(program_pattern_bindings_share_and_spellings);
    RUN_TEST_CASE(program_switches_use_narrowed_union_tags);
    RUN_TEST_CASE(program_general_unions_construct_match_and_extract);
    RUN_TEST_CASE(program_union_arrays_keep_owned_writable_values);
    RUN_TEST_CASE(program_failed_call_stacks_unwind_at_once);
    RUN_TEST_CASE(program_module_finish_preserves_failure_and_consumes_success);
    RUN_TEST_CASE(program_typed_event_changes_state_on_vm_and_sljit);
    RUN_TEST_CASE(program_handlers_name_payload_fields);
    RUN_TEST_CASE(program_occurrences_survive_whitespace_and_follow_the_source);
    RUN_TEST_CASE(program_nested_typed_records_preserve_explicit_labels);
    RUN_TEST_CASE(program_int_to_float_compare_and_cast_run_on_vm_and_sljit);
    RUN_TEST_CASE(program_systems_name_their_step);
    RUN_TEST_CASE(program_query_owner_snapshots_preserve_parallel_effects);
    RUN_TEST_CASE(program_system_membership_exit_cancels_pending_work);
    RUN_TEST_CASE(program_query_sources_resolve_current_targets_and_nearest_ancestors);
    RUN_TEST_CASE(program_query_required_path_keeps_one_instance_per_root);
    RUN_TEST_CASE(program_query_joins_filter_targets_and_refresh_across_suspension);
    RUN_TEST_CASE(program_query_optional_joins_preserve_missing_target_exclusions);
    RUN_TEST_CASE(program_relationship_loops_stream_targets_explicitly);
    RUN_TEST_CASE(program_query_chained_variables_filter_values_and_keep_event_tuples);
    RUN_TEST_CASE(program_query_rows_keep_locals_and_cancel_removed_tuple);
    RUN_TEST_CASE(program_query_changed_preserves_busy_published_writes);
    RUN_TEST_CASE(program_query_admission_is_frozen_before_row_writes);
    RUN_TEST_CASE(program_query_value_filters_preserve_short_circuit_failure_order);
    RUN_TEST_CASE(program_query_predicates_reuse_bounded_scratch_and_fresh_values);
    RUN_TEST_CASE(program_component_binding_runs_and_source_cycle_is_rejected);
    RUN_TEST_CASE(program_imported_library_preserves_owner_namespace_and_reader_after_replace);
    RUN_TEST_CASE(program_system_shared_state_survives_membership_and_is_registration_owned);
    RUN_TEST_CASE(program_managed_state_survives_overwrite_and_multiple_resumes);
    RUN_TEST_CASE(program_result_tuple_copies_survive_mutation_loop_and_resume);
    RUN_TEST_CASE(program_array_literals_are_fresh_copies);
    RUN_TEST_CASE(program_managed_payload_memory_is_bounded_across_root_and_outlined_resumes);
    RUN_TEST_CASE(program_completed_payloads_are_released_without_marking);
    RUN_TEST_CASE(program_temporary_payloads_preserve_roots_across_collection);
    RUN_TEST_CASE(program_sibling_timer_actions_complete_independently_and_cancel_on_detach);
    RUN_TEST_CASE(program_failure_snapshot_owns_outlined_source_diagnostics);
    RUN_TEST_CASE(program_assert_fails_with_its_message_or_condition);
    RUN_TEST_CASE(program_assert_binds_names_and_keeps_helpers_pure);
    RUN_TEST_CASE(program_integer_conversions_check_wrap_and_clamp);
    RUN_TEST_CASE(program_loops_count_half_open_and_in_reverse);
    RUN_TEST_CASE(program_char_conversions_and_arithmetic_stay_unicode_scalars);
    RUN_TEST_CASE(program_char_compares_with_ints_and_steps_below_surrogates);
    RUN_TEST_CASE(program_local_array_survives_collection_mid_build);
    RUN_TEST_CASE(program_local_array_store_checks_its_range);
    RUN_TEST_CASE(program_array_methods_change_their_array);
    RUN_TEST_CASE(program_integer_and_nominal_enum_literals_preserve_exact_bits);
    RUN_TEST_CASE(program_concrete_assignments_preserve_name_byte_and_handle_storage);
    RUN_TEST_CASE(program_source_float_width_conversion_preserves_native_input);
    RUN_TEST_CASE(program_state_actions_demand_inputs_route_and_preserve_per_owner_state);
    RUN_TEST_CASE(program_retrigger_evaluates_each_duration_once);
    RUN_TEST_CASE(program_debugger_steps_without_replaying_and_retains_owned_watches);
    RUN_TEST_CASE(program_task_runs_to_first_suspension_then_beside_caller);
    RUN_TEST_CASE(program_task_cancel_cascades_and_wakes_awaiters);
    RUN_TEST_CASE(program_task_cancelling_itself_stops_at_once);
    RUN_TEST_CASE(program_task_owns_tasks_started_by_its_continuations);
    RUN_TEST_CASE(program_task_continuation_cancels_but_cannot_await_its_task);
    RUN_TEST_CASE(program_task_slots_are_reused_without_reviving_old_handles);
    RUN_TEST_CASE(program_task_deadlines_use_start_time_and_do_not_cancel_waiters);
    RUN_TEST_CASE(program_task_copies_retain_results_and_unobserved_results_are_reclaimed);
    RUN_TEST_CASE(program_task_handle_survives_in_a_detached_capture);
    RUN_TEST_CASE(program_task_handles_survive_in_suspended_array_values);
    RUN_TEST_CASE(program_task_scope_exits_drop_only_unobserved_handles);
    RUN_TEST_CASE(program_native_nested_collections_retain_task_handles);
    RUN_TEST_CASE(program_task_cancellation_is_distinct_from_zero_false_and_empty_text);
    RUN_TEST_CASE(program_task_waiters_receive_independent_success_snapshots);
    RUN_TEST_CASE(program_task_result_snapshot_outlives_the_last_handle);
    RUN_TEST_CASE(program_task_await_copies_large_payload_once);
    RUN_TEST_CASE(program_task_collection_indexes_handles_across_many_owners);
    RUN_TEST_CASE(program_task_results_keep_nested_task_handles_alive);
    RUN_TEST_CASE(program_prepared_tasks_preserve_cancellation_union_and_deadlines);
    RUN_TEST_CASE(program_task_stops_with_owner_and_does_not_block_events);
    RUN_TEST_CASE(program_many_tasks_collect_in_linear_mark_work);
    RUN_TEST_CASE(program_task_semantics_reject_invalid_uses);

    RUN_TEST_CASE(program_handler_payloads_do_not_hide_parameters);
    RUN_TEST_CASE(program_nested_functions_wait_for_themselves);

    RUN_TEST_CASE(program_input_map_validates_actions);
    RUN_TEST_CASE(program_scene_children_are_referenced);
    RUN_TEST_CASE(program_input_actions_read_through_typed_names);
    RUN_TEST_CASE(program_scene_bindings_are_typed_names_before_play);

    RUN_TEST_CASE(program_constant_definitions_execute);

    RUN_TEST_CASE(program_single_result_call_fields_read_the_result);
    RUN_TEST_CASE(program_results_are_inferred_from_returns);
    RUN_TEST_CASE(program_task_with_several_results_awaits_a_tuple);
    RUN_TEST_CASE(program_library_call_starts_a_task);
    RUN_TEST_CASE(program_library_task_writes_the_callers_state);
    RUN_TEST_CASE(program_task_keeps_state_and_component_references);
    RUN_TEST_CASE(program_nested_calls_number_indexes_and_mixed_clamp);
    RUN_TEST_CASE(program_returns_convert_to_their_result_type);
    RUN_TEST_CASE(program_yield_resumes_on_the_next_update_without_time);
    RUN_TEST_CASE(program_task_state_awaits_its_function_results);
    RUN_TEST_CASE(program_task_handles_belong_to_their_instance);
    RUN_TEST_CASE(program_component_paths_test_presence);
    RUN_TEST_CASE(program_branch_completes_inside_action_continuation);
    RUN_TEST_CASE(program_parameters_defaults_and_contextual_words);
    RUN_TEST_CASE(program_task_shares_boxed_locals);
    RUN_TEST_CASE(program_detached_string_capture_retention_audit);
    RUN_TEST_CASE(program_detached_array_capture_retention_audit);
    RUN_TEST_CASE(program_attached_string_capture_retention_control);
    RUN_TEST_CASE(program_descendant_capture_keeps_inspector_snapshots_across_shrink);
    RUN_TEST_CASE(program_detached_capture_preserves_task_handles_until_replaced);
    RUN_TEST_CASE(program_assignments_and_reference_writes_do_not_allocate);
    RUN_TEST_CASE(program_component_scalar_reference_writes_do_not_copy_rows);
    RUN_TEST_CASE(program_repeated_helpers_have_bounded_allocations);
    RUN_TEST_CASE(program_reused_helpers_reset_locals_and_preserve_nested_calls);
    RUN_TEST_CASE(program_reused_helpers_keep_managed_results_and_resume_fresh_locals);
    pu_clear_setup();
}
