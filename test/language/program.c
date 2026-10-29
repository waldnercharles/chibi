#include "assets/input_map.h"
#include "authoring/native_bindings.h"
#include "authoring/source_catalog.h"
#include "authoring/source_package_prepare.h"
#include "backend.h"
#include "ecs/ecs.h"
#include "execution/delay.h"
#include "execution/function_builder.h"
#include "execution/inspection.h"
#include "execution/integer_report.h"
#include "execution/module.h"
#include "execution/native.h"
#include "execution/prepared.h"
#include "execution/program_internal.h"
#include "execution/status.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "language/identity.h"
#include "language/lower.h"
#include "runtime/arithmetic_definition.h"
#include "runtime/arrays.h"
#include "runtime/arrays_definition.h"
#include "runtime/core_definition.h"
#include "runtime/native_definitions.h"
#include "runtime/state_definition.h"
#include "runtime/text_definition.h"
#include "runtime/text_format.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/copy.h"
#include "types/map.h"
#include "types/pointer.h"
#include "types/query.h"
#include "types/record.h"
#include "types/text.h"
#include "types/trace.h"

#include <assert.h>
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
    { .name = "x", .type = PIN_F64, .size = sizeof(double), .type_name = "f64" },
    { .name = "y",
      .type = PIN_F64,
      .offset = offsetof(program_position, y),
      .size = sizeof(double),
      .type_name = "f64" },
};
static const type_field velocity_field = {
    .name = "x",
    .type = PIN_F64,
    .size = sizeof(double),
    .type_name = "f64",
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
static const char *const delay_entries[] = { "In" };
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
      .operation = SCRIPT_OPERATION_DELAY,
      .entries = delay_entries,
      .entry_count = 1,
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
static const native_capability_binding operation_binding = { .id = 20,
                                                             .intrinsic = BF_DELAY };
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
static authoring_packages *call_packages[3];
static execution_inspector *debugger;
static behavior_function_context parser_context;
static behavior_function_activation parser_activation;
static arena parser_storage;
static const node_command lifetime_command;
static flow_host_request *lifetime_requests[8];
static int lifetime_request_count;
static ecs_component_cell *lifetime_cells[2];
static ecs_component_cell *lifetime_targets[2];
static ecs_component_cell *lifetime_child_targets[3];
static int lifetime_target_count;
static int lifetime_child_target_count;
static ecs_world *lifetime_foreign_world;
static ecs_entity lifetime_target;
static const behavior_function *lifetime_bound;
static const behavior_function *lifetime_extra_bound;
static bool lifetime_remove_in_begin, lifetime_complete_in_begin,
    lifetime_begin_completed, lifetime_inferred;

static void lifetime_cleanup()
{
    for (int i = 0; i < lifetime_request_count; i++) {
        if (lifetime_requests[i]) {
            flow_host_release(lifetime_requests[i]);
        }
        lifetime_requests[i] = NULL;
    }
    lifetime_request_count = 0;
    ecs_world_free(lifetime_foreign_world);
    lifetime_foreign_world = NULL;
    for (int i = 0; i < 2; i++) {
        if (lifetime_cells[i]) {
            ecs_component_cell_release(lifetime_cells[i]);
        }
        lifetime_cells[i] = lifetime_targets[i] = NULL;
    }
    lifetime_target_count = 0;
    lifetime_child_target_count = 0;
    memset(lifetime_child_targets, 0, sizeof lifetime_child_targets);
    lifetime_bound = lifetime_extra_bound = NULL;
    lifetime_remove_in_begin = lifetime_complete_in_begin = lifetime_begin_completed = false;
    lifetime_inferred = false;
}

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
    lifetime_cleanup();
    for (int i = 0; i < 3; i++) {
        authoring_packages_free(call_packages[i]);
        call_packages[i] = NULL;
    }
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
    const pin_value value = {
        .type = PIN_STR,
        .text = &(const pin_string){ .bytes = text, .length = (uint32_t)strlen(text) }
    };
    return pin_value_snapshot_to_arena(storage, out, &value)
             ? "managed text snapshot failed"
             : NULL;
}
static const node_host_call managed_call = { .value = managed_text };
#define CAPTURE_TEXT_BYTES 512
#define CAPTURE_WRITES 32
#define CAPTURE_KEEP_UPDATES 40
#define CAPTURE_STORAGE_LIMIT ((size_t)256 * 1024)
/* A box holding short text: one arena block and the table that charges the
 * shared text backing it retains. */
#define SMALL_STORAGE_BYTES 1024
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
    const pin_value value = {
        .type = PIN_STR,
        .text = &(const pin_string){ .bytes = text, .length = (uint32_t)strlen(text) }
    };
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
    assert(count == 1 && !strcmp(pin_text(in[0]).bytes, "Jump"));
    *out = (pin_value){ .type = PIN_BOOL, .b = jump_held };
    return NULL;
}
static const char *action_pressed(arena *, const pin_value *in, int count, pin_value *out)
{
    assert(count == 1 && !strcmp(pin_text(in[0]).bytes, "Jump"));
    *out = (pin_value){ .type = PIN_BOOL, .b = jump_pressed };
    return NULL;
}
static const node_host_call held_call = { .value = action_held };
static const node_host_call pressed_call = { .value = action_pressed };
static ecs_entity bound_level;
static bool binding_available;
static const char *scene_binding(arena *, const pin_value *in, int count, pin_value *out)
{
    assert(count == 1 && !strcmp(pin_text(in[0]).bytes, "Level"));
    if (!binding_available) {
        return "Get Global Variable references an unknown value";
    }
    out->u64 = ecs_entity_to_u64(bound_level);
    return NULL;
}
static const node_host_call scene_binding_call = { .value = scene_binding };
static const char *lifetime_invalidate(arena *, const pin_value *, int count, pin_value *out)
{
    assert(count == 0);
    assert(ecs_world_record_remove(world, lifetime_target, 501, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *out = (pin_value){ .type = PIN_F64, .d = 7 };
    return NULL;
}
static const node_host_call lifetime_invalidate_call = { .value = lifetime_invalidate };
static bool host(void *, uint64_t id, script_lower_host *out)
{
    if (id == 77) {
        *out = (script_lower_host){ .command = &lifetime_command };
        return true;
    }
    if (id == 78) {
        *out = (script_lower_host){ .call = &lifetime_invalidate_call };
        return true;
    }
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
        .bindings = &operation_binding,
        .binding_count = 1,
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
        .bindings = &operation_binding,
        .binding_count = 1,
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
    return value && (value->type == PIN_F32
                         ? value->f == (float)expected
                         : value->type == PIN_F64 && value->d == expected);
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

/* Inject the private target set without choosing authored inference semantics.
 * All task bodies and awaiters still run authored code. */
static int lifetime_begin(
    void *,
    const char *,
    const pin_value *,
    int count,
    flow_host_request *request,
    const char **
)
{
    assert(count == 0 && lifetime_request_count < 8);
    lifetime_requests[lifetime_request_count++] = request;
    if (lifetime_remove_in_begin) {
        assert(ecs_world_record_remove(world, lifetime_target, 501, 0) == ECS_OK);
        assert(ecs_world_apply_commands(world) == ECS_OK);
    }
    if (lifetime_remove_in_begin || lifetime_complete_in_begin) {
        lifetime_begin_completed = flow_host_complete(request, -1, (pin_value){ 0 });
    }
    return 1;
}
static const node_command lifetime_command = { .begin = lifetime_begin };
static bool lifetime_compile(const char *source, behavior_backend backend)
{
    static script_catalog_capability
        active_capabilities[sizeof capabilities / sizeof *capabilities + 2];
    memcpy(active_capabilities, capabilities, sizeof capabilities);
    active_capabilities[sizeof capabilities / sizeof *capabilities] = (script_catalog_capability){
        .id = 77,
        .name = "Pending",
        .kind = SCRIPT_CAPABILITY_ACTION,
        .outcomes = completed,
        .outcome_count = 1,
        .completion_outcome = 0,
        .effects = SCRIPT_EFFECT_SUSPEND | SCRIPT_EFFECT_HOST
    };
    active_capabilities[sizeof capabilities / sizeof *capabilities + 1] = (script_catalog_capability){
        .id = 78,
        .name = "Invalidate",
        .kind = SCRIPT_CAPABILITY_FUNCTION,
        .results = &failed_result,
        .result_count = 1,
        .effects = SCRIPT_EFFECT_HOST | SCRIPT_EFFECT_WRITE
    };
    static script_catalog active;
    active = catalog;
    active.capabilities = active_capabilities;
    active.capability_count += 2;
    if (!compile_catalog(source, backend, &active)) {
        return false;
    }
    for (int i = 0; i < analysis->symbol_count; i++) {
        bool extra = !strcmp(analysis->symbols[i].name, "ExtraBound");
        if (strcmp(analysis->symbols[i].name, "Bound") && !extra) {
            continue;
        }
        for (int j = 0; j < module->plan.function_count; j++) {
            if (module->plan.functions[j].id == analysis->symbols[i].id) {
                if (extra) {
                    lifetime_extra_bound = module->plan.functions[j].body;
                } else {
                    lifetime_bound = module->plan.functions[j].body;
                }
            }
        }
    }
    program = execution_program_new(module, world, NULL);
    return program && (lifetime_bound || lifetime_inferred);
}
static int lifetime_start(behavior_function_context *ctx, int index)
{
    execution_activation *caller = ctx->inspection_user;
    const behavior_function *function = caller->function;
    behavior_function_active_context(&caller->context, &function);
    bool bound = function->calls[index].function == lifetime_bound;
    bool extra = lifetime_extra_bound &&
                 function->calls[index].function == lifetime_extra_bound;
    if (extra) {
        return program_task_start_bound(ctx, index, lifetime_child_targets, lifetime_child_target_count);
    }
    return program_task_start_bound(ctx, index, bound ? lifetime_targets : NULL, bound ? lifetime_target_count : 0);
}
static void lifetime_install_start(behavior_function_context *ctx)
{
    ctx->start_task = lifetime_start;
}
static int lifetime_dispatch(execution_instance *instance, double now)
{
    for (int i = 0; i < module->plan.function_count; i++) {
        const execution_module_function *function = &module->plan.functions[i];
        if (function->event != 11) {
            continue;
        }
        execution_activation
            *a = program_activation_new(instance, function->body, NULL, 0, now, 0, 1000);
        assert(a);
        if (!lifetime_inferred) {
            a->context.start_task = lifetime_start;
        }
        int status = program_activation_run(a, now, 0, 1000);
        if (!a->finished || acount(a->requests)) {
            apush(instance->activations, a);
        } else {
            program_activation_free(a);
        }
        for (int j = 0; !lifetime_inferred && j < acount(instance->activations); j++) {
            behavior_function_visit_contexts(&instance->activations[j]->context, lifetime_install_start);
        }
        return status < 0 ? status : FLOW_OK;
    }
    return FLOW_ERR_ARGUMENT;
}
static void lifetime_bind(ecs_world *target_world, ecs_entity target)
{
    lifetime_target = target;
    lifetime_cells[0] = ecs_component_cell_acquire(target_world, target, 501);
    assert(lifetime_cells[0]);
    lifetime_targets[0] = lifetime_targets[1] = lifetime_cells[0];
    lifetime_target_count = 2; /* Duplicate original identities are one dependency. */
}
static bool lifetime_remove()
{
    return ecs_world_record_remove(world, lifetime_target, 501, 0) == ECS_OK &&
           ecs_world_apply_commands(world) == ECS_OK;
}
static bool lifetime_requests_cancelled(int expected)
{
    if (lifetime_request_count != expected) {
        return false;
    }
    for (int i = 0; i < lifetime_request_count; i++) {
        flow_host_request *request = lifetime_requests[i];
        if (flow_host_status(request) != FLOW_HOST_CANCELLED ||
            execution_request_owner(request) ||
            flow_host_complete(request, -1, (pin_value){ 0 })) {
            return false;
        }
    }
    return true;
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
        "A :: behavior { total: f64 = 0;\n"
        "    on Impulse :: (enabled: bool, amount: f64) { if (enabled) { "
        "total += amount; } } }",
        "A :: behavior { total: f64 = 0;\n"
        "    on Impulse :: (enabled: bool) { if (enabled) { total += 7; } } }",
        "A :: behavior { total: f64 = 0;\n"
        "    on Impulse :: (Amount: f64) { total += Amount; } }",
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
        { "A :: behavior { on Impulse :: (force: f64) { } }",
          "Impulse has no payload field 'force'" },
        { "A :: behavior { on Impulse :: (amount: f64, Amount: f64) { } "
          "}",
          "listed twice" },
        { "A :: behavior { on Impulse :: (amount) { } }",
          "Write the type of payload field 'amount'" },
        { "A :: behavior { on Impulse :: (amount: bool) { } }", "" },
        { "A :: behavior { on Impulse :: (amount: f64 = 1) { } }", "takes no default" },
        { "A :: behavior { x: f64 = 0; on Impulse :: () { x = amount; } }", "" },
        { "A :: behavior { x: f64 = 0; on Impulse :: () { x = event.amount; "
          "} }",
          "list the field instead" },
        { "M :: system(p: ^Position) { on Impulse :: (enabled: bool) { p.x = "
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
                         "    total: f64 = 0;\n"
                         "    seen: bool = false;\n"
                         "\n"
                         "    on Impulse :: (amount: f64, enabled: bool) {\n"
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

static int take_reports(execution_integer_report *found)
{
    execution_integer_report *reports = NULL;
    execution_module_take_integer_reports(module, &reports);
    int count = acount(reports);
    for (int i = 0; i < count && i < EXECUTION_INTEGER_REPORT_KINDS; i++) {
        found[reports[i].kind] = reports[i];
    }
    afree(reports);
    return count;
}
/* A development build reports each overflow, zero divisor and clamping
 * conversion once per source line until the sites reset, and keeps the
 * values a shipped build computes; a module published without reports has
 * none. Checks the compiler adds, like char's scalar test, never report. */
TEST_CASE(program_integer_reports_are_taken_once_per_site_until_reset)
{
    const char *source = "Counter :: behavior {\n"
                         "    total: int = 2147483647;\n"
                         "    small: u8 = 0;\n"
                         "    quotient: int = 5;\n"
                         "    zero: int = 0;\n"
                         "    letter: char = 'a';\n"
                         "\n"
                         "    on Impulse :: (amount: f64, enabled: bool) {\n"
                         "        total *= 3;\n"
                         "        small = u8(amount);\n"
                         "        quotient = 7 / zero;\n"
                         "        letter = char(zero + 98);\n"
                         "    }\n"
                         "}\n";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int development = 0; development < 2; development++) {
        for (int b = 0; b < 2; b++) {
            prepare();
            execution_integer_reports_enable(development);
            bool compiled = compile(source, backends[b]);
            execution_integer_reports_enable(false);
            REQUIRE(compiled);
            ecs_entity owner = ecs_world_create_entity(world);
            uint64_t handle = ecs_entity_to_u64(owner);
            execution_instance *instance = execution_program_attach(program, handle);
            REQUIRE(instance);
            pin_value payload[] = { { .type = PIN_F64, .d = 300 },
                                    { .type = PIN_BOOL, .b = true } };
            execution_integer_report found[EXECUTION_INTEGER_REPORT_KINDS] = { 0 };
            for (int round = 0; round < 3; round++) {
                if (round == 2) {
                    execution_module_reset_integer_reports(module);
                }
                REQUIRE(execution_program_emit(program, 11, handle, payload, 2) == FLOW_OK);
                REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
                int count = take_reports(found);
                REQUIRE(count == (development && round != 1 ? EXECUTION_INTEGER_REPORT_KINDS : 0));
                REQUIRE(execution_instance_state(instance, state_id("small"))->u8 == 255);
                REQUIRE(execution_instance_state(instance, state_id("quotient"))->i == 0);
                REQUIRE(execution_instance_state(instance, state_id("letter"))->i == 'b');
            }
            /* Each round triples the total and wraps: 3 * (2^31 - 1) - 2^32, twice more. */
            REQUIRE(execution_instance_state(instance, state_id("total"))->i == 2147483621);
            if (!development) {
                continue;
            }
            REQUIRE(found[EXECUTION_INTEGER_OVERFLOW].line == 9);
            REQUIRE(found[EXECUTION_INTEGER_CLAMP].line == 10);
            REQUIRE(found[EXECUTION_INTEGER_ZERO_DIVISOR].line == 11);
            for (int k = 0; k < EXECUTION_INTEGER_REPORT_KINDS; k++) {
                REQUIRE(found[k].kind == (execution_integer_report_kind)k);
                REQUIRE(found[k].module_id == execution_module_id(module));
                REQUIRE(found[k].start < found[k].end);
            }
        }
    }
    cleanup();
    return true;
}

/* Each used site reports on its own, once per take until a reset, whether or
 * not the run is inspected, through each backend's own entry. T(wrap:) keeps
 * its conversion silent but not the overflow inside it. */
TEST_CASE(program_integer_reports_keep_sites_apart_under_inspection)
{
    const char *source = "Counter :: behavior {\n"
                         "    x: int = 2147483647;\n"
                         "    y: int = 0;\n"
                         "    z: int = 0;\n"
                         "    w: u8 = 0;\n"
                         "    on Impulse :: (amount: f64, enabled: bool) {\n"
                         "        y = x + 1;\n"
                         "        z = x * 2;\n"
                         "        w = u8(wrap: x * 4);\n"
                         "    }\n"
                         "}\n";
    /* Once a program has an inspector it inspects every run, recording or not. */
    static const struct
    {
        bool reset, inspect, record;
        int taken;
    } rounds[] = { { false, false, false, 3 }, { false, false, false, 0 },
                   { false, true, true, 0 },   { true, true, true, 3 },
                   { false, true, false, 0 },  { true, true, false, 3 } };
    /* Each site is its five-character operation, in source order. */
    const int columns[] = { 13, 13, 22 }, starts[] = { 155, 174, 202 };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        execution_integer_reports_enable(true);
        bool compiled = compile(source, backends[b]);
        execution_integer_reports_enable(false);
        REQUIRE(compiled);
        uint64_t handle = ecs_entity_to_u64(ecs_world_create_entity(world));
        execution_instance *instance = execution_program_attach(program, handle);
        REQUIRE(instance);
        const pin_value payload[] = { { .type = PIN_F64, .d = 0 },
                                      { .type = PIN_BOOL, .b = true } };
        for (int r = 0; r < 6; r++) {
            if (rounds[r].reset) {
                execution_module_reset_integer_reports(module);
            }
            if (rounds[r].inspect && !debugger) {
                debugger = execution_program_inspector(program);
            }
            if (debugger) {
                execution_inspector_recording(debugger, rounds[r].record, handle);
            }
            behavior_run_scope runs = { 0 };
            behavior_run_scope_begin(&runs);
            int flow = execution_instance_dispatch(instance, 11, payload, 2, r, 0, 1000);
            behavior_run_scope_end(&runs);
            REQUIRE(flow == FLOW_OK);
            REQUIRE(!runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks);
            if (backends[b] == BEHAVIOR_BYTECODE) {
                REQUIRE(runs.counts.bytecode_entries > 0 && !runs.counts.sljit_entries);
            }
            if (backends[b] == BEHAVIOR_SLJIT) {
                REQUIRE(runs.counts.sljit_entries > 0 && !runs.counts.bytecode_entries);
                REQUIRE(
                    runs.counts.inspected_sljit_entries ==
                    (rounds[r].inspect ? runs.counts.sljit_entries : 0)
                );
            }
            execution_integer_report *reports = NULL;
            execution_module_take_integer_reports(module, &reports);
            REQUIRE(acount(reports) == rounds[r].taken);
            for (int i = 0; i < acount(reports); i++) {
                REQUIRE(reports[i].kind == EXECUTION_INTEGER_OVERFLOW);
                REQUIRE(reports[i].module_id == execution_module_id(module));
                REQUIRE(reports[i].line == 7 + i && reports[i].column == columns[i]);
                REQUIRE(reports[i].start == starts[i] && reports[i].end == starts[i] + 5);
            }
            afree(reports);
            REQUIRE(execution_instance_state(instance, state_id("y"))->i == INT32_MIN);
            REQUIRE(execution_instance_state(instance, state_id("z"))->i == -2);
            REQUIRE(execution_instance_state(instance, state_id("w"))->u8 == 252);
        }
    }
    cleanup();
    return true;
}
static execution_module *prepared_import(void *user, uint64_t id, uint64_t)
{
    execution_module *library = user;
    if (execution_module_id(library) != id) {
        return NULL;
    }
    execution_module_retain(library);
    return library;
}
/* Reads the module back, reporting when development is set. */
static execution_module *reread(
    const execution_module *source,
    execution_module *library,
    behavior_backend backend,
    bool development,
    unsigned char **bytes,
    size_t *length
)
{
    char error[256];
    if (!execution_prepared_write(source, bytes, length, error, sizeof error)) {
        fprintf(stderr, "prepared reports: %s\n", error);
        return NULL;
    }
    const execution_prepared_linker linker = { .user = library,
                                               .type = program_prepared_type,
                                               .import = prepared_import };
    execution_integer_reports_enable(development);
    execution_module *loaded = execution_prepared_read(*bytes, *length, &linker, backend, error, sizeof error);
    execution_integer_reports_enable(false);
    if (!loaded) {
        fprintf(stderr, "prepared reports: %s\n", error);
    }
    return loaded;
}
/* Prepared files carry no report state: a development build that reads a
 * module and its imported library reports from their decoded sources, writes
 * the same bytes back, and reading them otherwise reports nothing. */
TEST_CASE(program_prepared_integer_reports_include_imported_functions)
{
    const char *library_source = "Store :: library {\n"
                                 "    hits: int = 2147483647;\n"
                                 "    Work :: () -> (local_count: f64, "
                                 "position_x: f64) {\n"
                                 "        hits *= 3;\n"
                                 "        return(f64(hits), 0);\n"
                                 "    }\n"
                                 "}\n";
    const char
        *owner_source = "Owner :: behavior {\n"
                        "    local_count: f64 = 0;\n"
                        "    small: u8 = 0;\n"
                        "    on Impulse :: (amount: f64, enabled: bool) {\n"
                        "        values := await library_work();\n"
                        "        local_count = values.local_count;\n"
                        "        small = u8(amount);\n"
                        "    }\n"
                        "}\n";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int development = 0; development < 2; development++) {
        for (int b = 0; b < 2; b++) {
            prepare();
            REQUIRE(compile_module(library_source, backends[b]));
            imported = module;
            module = NULL;
            script_analysis_free(analysis);
            script_syntax_free(syntax);
            analysis = NULL;
            syntax = NULL;
            REQUIRE(compile_module(owner_source, backends[b]));
            uint64_t count_id = state_id("local_count"), small_id = state_id("small");
            unsigned char *bytes[4] = { 0 };
            size_t lengths[4] = { 0 };
            execution_module *library = reread(
                imported,
                NULL,
                backends[b],
                development,
                &bytes[0],
                &lengths[0]
            );
            REQUIRE(library);
            execution_module *
                owner = reread(module, library, backends[b], development, &bytes[1], &lengths[1]);
            execution_module_free(module);
            execution_module_free(imported);
            module = owner;
            imported = library;
            REQUIRE(module && execution_module_import_count(module) == 1);
            program = execution_program_new(module, world, NULL);
            execution_instance *instance = execution_program_attach(
                program,
                ecs_entity_to_u64(ecs_world_create_entity(world))
            );
            REQUIRE(instance);
            const pin_value payload[] = { { .type = PIN_F64, .d = 300 },
                                          { .type = PIN_BOOL, .b = true } };
            /* hits triples and wraps: 3 * (2^31 - 1) - 2^32, then twice more. */
            const double counts[] = { 2147483645, 2147483639, 2147483621 };
            for (int round = 0; round < 3; round++) {
                if (round == 2) {
                    execution_module_reset_integer_reports(module);
                }
                REQUIRE(
                    execution_instance_dispatch(instance, 11, payload, 2, round, 0, 1000) == FLOW_OK
                );
                REQUIRE(execution_program_update(program, round, 0, 1000) == FLOW_OK);
                REQUIRE(execution_instance_state(instance, count_id)->d == counts[round]);
                REQUIRE(execution_instance_state(instance, small_id)->u8 == 255);
                execution_integer_report found[EXECUTION_INTEGER_REPORT_KINDS] = { 0 };
                REQUIRE(take_reports(found) == (development && round != 1 ? 2 : 0));
                if (!development || round == 1) {
                    continue;
                }
                const execution_integer_report *overflow = &found[EXECUTION_INTEGER_OVERFLOW],
                                               *clamp = &found[EXECUTION_INTEGER_CLAMP];
                REQUIRE(
                    overflow->kind == EXECUTION_INTEGER_OVERFLOW && clamp->kind == EXECUTION_INTEGER_CLAMP
                );
                REQUIRE(
                    overflow->module_id == execution_module_id(imported) &&
                    overflow->line == 4
                );
                REQUIRE(clamp->module_id == execution_module_id(module) && clamp->line == 7);
                /* The compound assignment spans its statement, the conversion its call. */
                REQUIRE(overflow->column == 9 && overflow->start == 111 && overflow->end == 121);
                REQUIRE(clamp->column == 17 && clamp->start == 212 && clamp->end == 222);
            }
            /* Marked and delivered sites leave the written bytes unchanged. */
            char error[256];
            REQUIRE(
                execution_prepared_write(imported, &bytes[2], &lengths[2], error, sizeof error)
            );
            REQUIRE(execution_prepared_write(module, &bytes[3], &lengths[3], error, sizeof error));
            for (int i = 0; i < 2; i++) {
                REQUIRE(
                    lengths[i] == lengths[i + 2] &&
                    !memcmp(bytes[i], bytes[i + 2], lengths[i])
                );
            }
            for (int i = 0; i < 4; i++) {
                free(bytes[i]);
            }
        }
    }
    cleanup();
    return true;
}
/* Systems receive dt implicitly in their body, and helpers receive it as
 * an ordinary explicit argument. */
TEST_CASE(program_systems_name_their_step)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        ecs_entity moving = positioned(1, true);
        REQUIRE(compile(
            "Motion :: system(position: ^Position, velocity: Velocity) {"
            " Scale :: (step: f64) -> f64 { return step * 2; }"
            " position.x += velocity.x * Scale(dt); }",
            backends[backend]
        ));
        REQUIRE(execution_program_step(program, 0, .5, 1000) == FLOW_OK);
        const program_position *position = ecs_component_data_const(world, moving, 501);
        REQUIRE(position && position->x == 5);
    }
    const struct
    {
        const char *source, *message;
    } refused[] = {
        { "Motion :: system(p: ^Position) { p.x += event.dt; }", "Unknown value" },
        { "Motion :: system(p: ^Position) { Step :: () -> f64 { return dt; "
          "} p.x += Step(); }",
          "Read the step in the system's body" },
        { "Motion :: system(p: ^Position) { on Impulse :: (amount: f64) { "
          "p.x += dt; } }",
          "Read the step in the system's body" },
        { "Motion :: system(p: ^Position) { bind self.Position.x = dt; p.x += "
          "1; }",
          "Read the step in the system's body" },
        { "Motion :: system(p: ^Position, if dt > 0) { p.x += dt; }",
          "Query predicate cannot read" },
    };
    for (int i = 0; i < (int)(sizeof refused / sizeof *refused); i++) {
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
    const char *obsolete[] = {
        "Motion :: system(p: ^Position) on Tick :: (dt: f64) {}",
        "Motion :: system(p: ^Position) on Impulse :: (amount: f64) {}",
    };
    for (int i = 0; i < 2; i++) {
        prepare();
        syntax = script_parse(obsolete[i]);
        REQUIRE(syntax->diagnostic_count);
    }
    return true;
}
TEST_CASE(program_query_owner_snapshots_preserve_parallel_effects)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        ecs_entity moving = positioned(1, true);
        REQUIRE(compile(
            "Motion :: system( position: ^Position, velocity: ^Velocity)       "
            "{ position.x += "
            "velocity.x * dt; position.y = position.x + velocity.x; }",
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

TEST_CASE(program_query_projected_component_pointers_keep_serial_effects)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        ecs_entity moving = positioned(1, false);
        REQUIRE(compile(
            "Motion :: system(position: ^Position) { "
            "projected := &position.x; projected^ += 1; }",
            backends[backend]
        ));
        execution_effect_summary effects = execution_module_effects(module);
        REQUIRE(effects.declared && effects.deterministic && !effects.parallel_safe);
        REQUIRE(!effects.structural && !effects.host_sink && !effects.world_read);
        REQUIRE(effects.write_count == 1 && effects.writes[0] == 501);
        REQUIRE(execution_program_step(program, 0, .5, 1000) == FLOW_OK);
        const program_position *position = ecs_component_data_const(world, moving, 501);
        REQUIRE(position && position->x == 2 && position->y == 0);
    }
    return true;
}

TEST_CASE(program_system_membership_exit_cancels_pending_work)
{
    const char *source = "Motion :: system(position: ^Position, velocity: "
                         "^Velocity)       {\n"
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
                                              .bindings = &operation_binding,
                                              .binding_count = 1,
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
            "Follow :: system( p: ^Position, aim: ^Position? from Targets, "
            "team: ^Position from "
            "ancestor(ChildOf) , if not Velocity) { p.x = team.x; if aim { p.y "
            "= aim.x; } }",
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
            "Follow :: system( p: ^Position, aim: ^Position from Targets , if "
            "not self.Velocity) { p.x "
            "+= 1; aim.y += 1; }",
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
            "Follow :: system( p: ^Position from self , aim: ^Position from "
            "Targets , if not "
            "self.Velocity and Targets.Velocity and has(Likes)) { count: "
            "retain f64 = 0; count += "
            "1; p.x = count; await Delay(1); p.y = aim.x; }",
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
        /* This invocation retains the component selected before suspension.
         * The new relationship target is bound by the next invocation. */
        REQUIRE(row && row->x == 2 && row->y == 10);
        REQUIRE(ecs_world_record_remove(world, second, 502, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(!execution_program_instance(program, ecs_entity_to_u64(root)));
        row = ecs_component_data_const(world, root, 501);
        REQUIRE(row && row->x == 2 && row->y == 10);
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
            "Follow :: system( p: ^Position , aim: ^Position? from Targets , "
            "if not self.Velocity and "
            "not Targets.Velocity) { p.y += 1; if aim { p.x = aim.x; } }",
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
            "Sum :: system( p: ^Position , if not Velocity) { ticks: retain "
            "f64 = 0; ticks += 1; "
            "p.y = ticks; p.x = 0; for target in self.Targets { if not "
            "target.Position { continue; } "
            "p.x += target.Position.x; } }",
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
    syntax = script_parse("Bad :: system( p: ^Position) { for target in self.Targets { await Delay(1); } }");
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
            "Binding :: behavior { saved: f64=2; Read :: ()->f64 { "
            "return saved } "
            "bind self.Position.y=Read(); on Impulse :: (amount: f64) { "
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
                "Chained :: system( p:^Position , middle in self.Targets , "
                "leaf in middle.Leaves , "
                "mid:^Position from middle , aim:^Position from leaf , if "
                "aim.x > mid.x) { ticks:retain "
                "f64=0; seen:retain f64=0; on Impulse :: (amount: "
                "f64, enabled: bool) { if "
                "enabled { seen=amount; p.x+=amount+mid.x+aim.x; } } ticks+=1; "
                "p.y+=mid.x+aim.x; "
                "aim.y+=mid.x; }",
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
            "Rows :: system( p:^Position , target in self.Targets , "
            "aim:^Position from target) { "
            "starts:retain f64=0; starts+=1; saved:=aim.x; await Delay(1); "
            "p.y+=saved; }",
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
            "Changed :: system( p:^Position , if changed(p) and Accept(p.x)) { "
            "Accept :: "
            "(value:f64)->bool { return value>=0; } calls:retain f64=0; "
            "calls+=1; await "
            "Delay(1); }",
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
            "Snapshot :: system( p:^Position , target in self.Targets , if "
            "p.x==0) { p.x+=1; }",
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
        if (memory.instance_count || memory.activation_bytes) {
            execution_effect_summary effects = execution_module_effects(module);
            fprintf(
                stderr,
                "stream row backend=%d instances=%d activation=%zu rows=%llu "
                "history=%zu effects=%d/%d/%d\n",
                backend,
                memory.instance_count,
                memory.activation_bytes,
                (unsigned long long)memory.query_rows,
                memory.query_history_bytes,
                effects.declared,
                effects.parallel_safe,
                effects.deterministic
            );
            fprintf(
                stderr,
                "stream eligibility selected=%d functions=%d states=%d "
                "bindings=%d actions=%d imports=%d changes=%d structural=%d "
                "sink=%d\n",
                program->query_streaming,
                module->plan.function_count,
                module->plan.state_count,
                module->plan.binding_count,
                module->plan.action_count,
                module->plan.import_count,
                acount(program->change_queries),
                effects.structural,
                effects.host_sink
            );
        }
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
        "Ordered :: system( if Risky(int(self.Position.x)) and has(Velocity)) "
        "{ Risky :: "
        "(index:int)->bool { values:=[10,20]; return values[index]>0; } }",
        "Guarded :: system( if has(Velocity) and Risky(int(self.Position.x))) "
        "{ Risky :: "
        "(index:int)->bool { values:=[10,20]; return values[index]>0; } }",
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
            "Filter :: system( p:^Position , if Above(p.x) and Below(p.x) and "
            "p.x<100) { Above :: "
            "(value:f64)->bool { return Positive(value); } Positive :: "
            "(value:f64)->bool { "
            "return value>0; } Below :: (value:f64)->bool { return value<4; "
            "} p.y+=1; }",
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
            "Filter :: system( p:^Position , if Above(p.x) and Below(p.x)) { "
            "Above :: "
            "(value:f64)->bool { return Negative(value); } Negative :: "
            "(value:f64)->bool { "
            "return value<0; } Below :: (value:f64)->bool { return value<4; "
            "} p.y+=1; }",
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

static int native_predicate_calls;
static void native_above(execution_native_context *, const uint64_t *arguments, uint64_t *results)
{
    native_predicate_calls++;
    double value;
    memcpy(&value, &arguments[0], sizeof value);
    results[0] = value > 0;
}
/* A query predicate's calls run the program's native entries. */
TEST_CASE(program_query_predicates_call_bound_natives)
{
    static const uint8_t types[] = { EXECUTION_NATIVE_F64, EXECUTION_NATIVE_BOOL };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        ecs_entity owners[3] = { positioned(-1, false),
                                 positioned(1, false),
                                 positioned(3, false) };
        REQUIRE(compile(
            "Filter :: system(p: ^Position, if Above(p.x)) { "
            "Above :: (value: f64) -> bool { return value > 0; } p.y += 1; }",
            backends[backend]
        ));
        execution_native_export above = { .name = "Above",
                                          .types = types,
                                          .parameter_count = 1,
                                          .result_count = 1,
                                          .entry = native_above };
        for (int i = 0; i < execution_module_function_count(module); i++) {
            execution_module_function_info info = execution_module_function_at(module, i);
            int count;
            const pin_value *arguments = info.kind == EXECUTION_FUNCTION_AUTHORED
                                           ? behavior_function_arguments(info.body, &count)
                                           : NULL;
            above.id = arguments && count == 1 && arguments[0].type == PIN_F64
                         ? info.id
                         : above.id;
        }
        const execution_native_manifest manifest = { EXECUTION_NATIVE_ABI, 1, &above };
        execution_natives *natives = execution_natives_new(
            (execution_native_runtime){ NATIVE_FORMAT_TEXT_HOST.value }
        );
        char error[256] = "";
        REQUIRE(execution_natives_bind(natives, module, &manifest, error, sizeof error));
        execution_program_set_natives(program, natives);
        native_predicate_calls = 0;
        behavior_run_scope scope = { 0 };
        behavior_run_scope_begin(&scope);
        int status = execution_program_step(program, 0, 0, 1000);
        behavior_run_scope_end(&scope);
        REQUIRE(status == FLOW_OK);
        REQUIRE(native_predicate_calls == 3 && scope.counts.native_entries == 3);
        for (int i = 0; i < 3; i++) {
            const program_position *position = ecs_component_data_const(world, owners[i], 501);
            REQUIRE(position->y == (i ? 1 : 0));
        }
        execution_program_free(program);
        program = NULL;
        execution_natives_free(natives);
    }
    return true;
}

TEST_CASE(program_imported_library_preserves_owner_namespace_and_reader_after_replace)
{
    const char *library_source = "Store :: library { count: f64 = 10;Work "
                                 ":: () -> (local_count: f64, position_x: "
                                 "f64) {count += 1; await Delay(1); "
                                 "return(count, self.Position.x); } }";
    const char
        *owner_source = "Owner :: behavior { count: f64 = 0; local_count: "
                        "f64 = 0; position_x: f64 = 0;on Impulse :: "
                        "(amount: "
                        "f64) { count += amount; values := await "
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
    const char *source = "Shared :: system(position: ^Position) { total: "
                         "retain f64 = 0; position.x += 1; total "
                         "+= 1; position.y = total; }";
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
    return value && value->type == PIN_STR &&
           !strcmp(pin_text(*value).bytes, expected);
}
TEST_CASE(program_managed_state_survives_overwrite_and_multiple_resumes)
{
    const char *
        source = "Managed :: behavior { count: f64 = 0; text: string = "
                 "\"initial\"; observed: string = \"\";on Impulse :: (amount: "
                 "f64) { count += amount; saved := text;text = "
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
                    (pin_value){ .type = PIN_STR,
                                 .text = &(const pin_string){
                                     .bytes = initial,
                                     .length = (uint32_t)strlen(initial) } }
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
                    (pin_value){ .type = PIN_STR,
                                 .text = &(const pin_string){
                                     .bytes = replacement,
                                     .length = (uint32_t)strlen(replacement) } }
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
                    (pin_value){ .type = PIN_STR,
                                 .text = &(const pin_string){ .bytes = "after",
                                                              .length = sizeof("after") - 1 } }
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
        "TupleCopies :: behavior { first: f64 = 0; second: f64 = "
        "0;before_text: string = \"\"; after_text: string = \"\";Pair :: (n: "
        "f64, t: string) -> (total: f64, message: string) {return(n, t); "
        "}on Impulse :: () { i: f64 = 0; while i < 2 {current := "
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
        "Literals :: behavior { first: f64 = 0; second: f64 = 0; "
        "mixed_total: f64 = 0; text: string = \"\"; kept: f64[] = []; "
        "loop_total: f64 = 0; Tiles :: () -> (tiles: f64[]) { return [3, "
        "-1.5, 7]; } on Impulse :: (amount: f64) { changed := Tiles(); "
        "changed[0] = 99; fresh := Tiles(); first = fresh[0] + fresh[1]; "
        "second = changed[0]; mixed: f64[] = [amount, 2]; mixed_total = "
        "mixed[0] + mixed[1]; names: string[] = [\"a\", \"b\"]; text = "
        "names[1]; kept = [1, 2]; kept[0] = kept[0] + 10; loop_total = 0; i: "
        "f64 = 0; while i < 2 { row: f64[] = [3, 4]; row[0] = row[0] + "
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
    return value && value->type == PIN_STR &&
           pin_text(*value).bytes[0] == 'A' + produced % 26 &&
           strlen(pin_text(*value).bytes) == (size_t)size &&
           pin_text(*value).bytes[size - 1] == 'x';
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
        strlen(pin_text(*value).bytes) != CAPTURE_TEXT_BYTES ||
        pin_text(*value).bytes[0] != 'A' + produced / 26 ||
        pin_text(*value).bytes[1] != 'a' + produced % 26) {
        return false;
    }
    for (int i = 2; i < CAPTURE_TEXT_BYTES; i++) {
        if (pin_text(*value).bytes[i] != 'x') {
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
    /* Text bytes live in shared immutable backing that each arena charges
     * once, so the box holds its allocations and the backing they retain. */
    used += box->storage.retained_bytes;
    /* A fresh capture is a bounded reference size for the current value,
     * including nested array headers and the text backing it retains, not the
     * old versions. */
    arena current = { 0 };
    pin_value snapshot;
    REQUIRE(!pin_value_capture_to_arena(&current, &snapshot, box->value));
    size_t live, live_capacity;
    arena_usage(&current, &live, &live_capacity);
    live += current.retained_bytes;
    REQUIRE(live >= CAPTURE_TEXT_BYTES && live < (array ? 6 : 4) * CAPTURE_TEXT_BYTES);
    pin_value captured = array ? pin_array_get(snapshot.ptr, 0) : snapshot;
    REQUIRE(capture_text_is(&captured, array ? writes ? writes + 1 : 0 : writes));
    arena_free(&current);
    if (!attached && !array) {
        REQUIRE(used == live);
    }
    /* An array lives on the program's managed heap, which the box shares:
     * the overwritten versions are collected, so the heap holds the current
     * array and the few states that keep earlier ones, never all 32. */
    if (array) {
        REQUIRE(!used);
        REQUIRE(execution_program_memory_usage(program).managed_bytes < 16 * CAPTURE_TEXT_BYTES);
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
        "Capture :: behavior { writer: task; keeper: task; count: f64 = 0;"
        "seen: string = \"\"; latest: string = \"\"; tail: string = \"\";"
        "initial: %s; saved: %s;"
        "Write :: (value: ^(%s), alias: ^(%s)) { yield; i := 0;"
        "while i < %d { %s = capture_text(); seen = %s; %s"
        "if i == 7 { saved = alias^; } count += 1; i += 1; yield; } }"
        "Keep :: (value: ^(%s)) { yield; i := 0; while i < %d {"
        "latest = %s; i += 1; yield; } }"
        "on Impulse :: (amount: f64, enabled: bool) { if amount == 0 { v := "
        "%s; initial = v;"
        "writer = Write(&v, &v); keeper = Keep(&v); if enabled { "
        "await writer; } }"
        "else if amount == 1 { cancel writer; } else { cancel keeper; } } }",
        array ? "string[]" : "string = \"\"",
        array ? "string[]" : "string = \"\"",
        array ? "string[]" : "string",
        array ? "string[]" : "string",
        CAPTURE_WRITES,
        array ? "value^[0]" : "value^",
        array ? "alias^[0]" : "alias^",
        array ? "tail = alias^[1];" : "",
        array ? "string[]" : "string",
        CAPTURE_KEEP_UPDATES,
        array ? "value^[0]" : "value^",
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
    const char *
        source = "Descendant :: behavior { before: string = \"\"; observed: "
                 "string = \"\";done: f64 = 0;Child :: (value: ^string) "
                 "{ yield; value^ = value^; before = value^;yield; value^ = "
                 "\"small\"; observed = value^;yield; value^ = \"\"; done = 1; "
                 "yield; }Relay :: (value: ^string) { yield; Child("
                 "value); "
                 "}on Impulse :: () { v := capture_text(); "
                 "Relay(&v); } }";
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
        REQUIRE(
            !strcmp(pin_text(*(box->value)).bytes, "small") &&
            arena_storage_bytes(&box->storage) < SMALL_STORAGE_BYTES
        );
        REQUIRE(execution_program_update(program, 4, 0, 10000) == FLOW_OK);
        REQUIRE(
            !strcmp(pin_text(*(box->value)).bytes, "") &&
            arena_storage_bytes(&box->storage) < SMALL_STORAGE_BYTES
        );
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
        *source = "Handles :: behavior { seen: f64 = 0;Result :: () -> "
                  "(value: f64) { yield; return 7; }Use :: (jobs: ^("
                  "task(f64)[])) { yield; jobs^ = jobs^; yield;answer := "
                  "await "
                  "jobs^[0]; if answer is f64 n { seen = n; }jobs^ = []; "
                  "yield; }on Impulse :: () { jobs: task(f64)[] "
                  "= [Result()]; Use(&jobs); } }";
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
        REQUIRE(arena_storage_bytes(&box->storage) < SMALL_STORAGE_BYTES);
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
    const char *body = "saved := managed_text(); i: f64 = 0; while i < 64 {"
                       "current := Pair(i, managed_text()); copied := current;"
                       "current.message = \"discarded\"; text = "
                       "copied.message; observed = saved;"
                       "if i == 3 { saved = text; } await Delay(1); count += "
                       "1; i += 1; }";
    const char *starts[] = { "on Impulse :: (amount: f64, enabled: bool) {",
                             "Run :: () -> () {" };
    const char *ends[] = { "}", "} on Impulse :: (amount: f64, enabled: bool) { await Run(); }" };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int outlined = 0; outlined < 2; outlined++) {
        char source[2048];
        int size = snprintf(
            source,
            sizeof source,
            "Retained :: behavior { count: f64 = 0; text: string = \"\";"
            "observed: string = \"\";"
            "Pair :: (n: f64, t: string) -> (total: f64, message: "
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
                     * plus 4KiB, rather than retaining every smaller dead slot.
                     * Either sample may fall anywhere between collections, so
                     * only the retained text bounds them from below. */
                    REQUIRE(steady.payload_bytes >= MEMORY_TEXT_MIN);
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
        *source = "Completed :: behavior { seen: f64 = 0;Pair :: (n: "
                  "f64) -> (first: f64, second: f64) {return(n, n + "
                  "1); }on Impulse :: () {i := 0; while i < 512 { "
                  "current := Pair(i);values: f64[] = [current.first, "
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
        /* The completed call owns no suspended work, so its payloads are freed
         * directly. Its dead arrays live on the managed heap, which collects
         * at call boundaries inside the run: those collections trace the few
         * live values, never the 512 dead arrays. */
        REQUIRE(complete.managed_bytes == 0);
        REQUIRE(arena_collect_marks() - marks < 16);
    }
    return true;
}
TEST_CASE(program_temporary_payloads_preserve_roots_across_collection)
{
    const char
        *source = "Payloads :: behavior { seen: f64 = 0;Pair :: (n: f64) "
                  "-> (first: f64, second: f64) {return(n, n + 1); }on "
                  "Impulse :: () {saved := Pair(40); kept: f64[] "
                  "= [2, 3, 5];i := 0; while i < 512 {current := Pair(i); "
                  "values: f64[] = [current.first, "
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
        *source = "Timers :: behavior { early_count: f64 = 0; late_count: "
                  "f64 = 0;action early = timer(1); action late = "
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
                         "  Outline :: () -> (Result: f64) {\n"
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
    const char *source = "Checks :: behavior {\n"
                         "  Positive :: (value: f64) { assert(value > 0.0); }\n"
                         "  on Impulse :: (amount: f64, enabled: bool) {\n"
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
        { -1, "Assertion failed: value > 0.0", 2, 30 },
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
        "Checks :: behavior { on Impulse :: (amount: f64, enabled: bool) { "
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
                 "  total: f64 = 0;\n"
                 "  Check :: (n: f64) { assert(n >= 0.0); }\n"
                 "  on Impulse :: (amount: f64) {\n"
                 "    local: f64? = amount;\n"
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
    const char *source = "Failure :: behavior { n: f64 = 0;Inner :: () -> "
                         "(Result: f64) { Delay(1); return fail_native() "
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
            .initial = { .type = PIN_STR,
                         .text = &(const pin_string){ .bytes = initial,
                                                      .length = (uint32_t)strlen(initial) } },
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
    REQUIRE(value && value->type == PIN_STR && !strcmp(pin_text(*value).bytes, "producer text"));
    return true;
}
static bool int_state(execution_instance *instance, const char *name, pin_type type, int64_t expected)
{
    const pin_value *value = execution_instance_state(instance, state_id(name));
    return value && value->type == type &&
           (int64_t)behavior_scalar_bits(*value) == expected;
}
/* T(value) clamps an integer that does not fit; T(wrap: value) keeps its low bits. */
TEST_CASE(program_integer_conversions_clamp_or_wrap)
{
    const char
        *source = "Convert :: behavior {\n"
                  "  wide: int = -300; wrapped: int = 0; clamped: int = 0;\n"
                  "  mixed: i64 = 0; narrow: u8 = 7;\n"
                  "  same: bool = true; below: bool = false; small: int = 0;\n"
                  "  on Impulse :: (enabled: bool) {\n"
                  "    wrapped = u8(wrap: wide); clamped = u8(-wide);\n"
                  "    mixed = wide + int(u32(7));\n"
                  "    same = wide == int(u32(0)); below = wide < "
                  "int(u32(7));\n"
                  "    small = int(u8(200)) + int(i8(-100));\n"
                  "    if (enabled) { narrow = u8(wide); }\n"
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
        REQUIRE(int_state(instance, "narrow", PIN_U8, 7));
        args[1].b = true;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 1, 0, 1000) >= 0);
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
/* A char beside an int compares as an int, and ASCII steps below the
 * surrogates pass the scalar check; -c is no scalar. */
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
/* These calls use the real native arithmetic pack. A nominal char output
 * must be checked before an assignment or a discarded result can escape. */
static bool native_char_result(const char *expression, int scalar, bool fails, bool discard, bool integer, int expected)
{
    char statement[160], source[PROGRAM_SOURCE_CAPACITY];
    snprintf(
        statement,
        sizeof statement,
        discard   ? "unused := %s; code = letter;"
        : integer ? "code = %s;"
                  : "letter = %s; code = letter;",
        expression
    );
    snprintf(
        source,
        sizeof source,
        "NativeChars :: behavior { letter: char = char(%d); code: int = -1; "
        "entered: int = 0; finished: int = 0; on Impulse :: (enabled: bool) { "
        "entered = 1; %s finished = 1; } }",
        scalar,
        statement
    );
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    bool passed = true;
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_pack(source, backends[b], &NATIVE_ARITHMETIC_DEFINITION_PACK));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL } };
        int status = execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000);
        bool same = int_state(instance, "entered", PIN_I32, 1);
        if (fails) {
            const execution_program_failure *failure = execution_program_error(program);
            same &= status < 0 && failure && failure->kind == BEHAVIOR_FUNCTION_DATA &&
                    !strcmp(failure->message, "Value does not fit in char") &&
                    failure->start == (int)(strstr(source, expression) - source) &&
                    int_state(instance, "letter", PIN_I32, scalar) &&
                    int_state(instance, "code", PIN_I32, -1) &&
                    int_state(instance, "finished", PIN_I32, 0);
        } else {
            same &= status == FLOW_OK &&
                    int_state(instance, "code", PIN_I32, expected) &&
                    int_state(instance, "letter", PIN_I32, integer ? scalar : expected) &&
                    int_state(instance, "finished", PIN_I32, 1);
        }
        if (!same) {
            const pin_value *letter = execution_instance_state(instance, state_id("letter"));
            fprintf(
                stderr,
                "native char %s scalar %d backend %d: status %d letter %d\n",
                expression,
                scalar,
                b,
                status,
                letter ? letter->i : 0
            );
        }
        passed &= same;
    }
    REQUIRE(passed);
    return true;
}
TEST_CASE(program_native_char_results_reject_invalid_scalars)
{
    bool passed = native_char_result("negate(letter)", 65, true, false, false, 0);
    passed &= native_char_result("negate(letter)", 65, true, true, false, 0);
    passed &= native_char_result("negate(char(65))", 65, true, false, false, 0);
    passed &= native_char_result("add(letter, letter)", 0x6C00, true, false, false, 0);
    passed &= native_char_result("add(letter, letter)", 0x88000, true, false, false, 0);
    REQUIRE(passed);
    return true;
}
TEST_CASE(program_native_char_results_preserve_valid_scalars_and_ints)
{
    REQUIRE(native_char_result("negate(letter)", 0, false, false, false, 0));
    const int scalars[] = { 0, 0xD7FF, 0xE000, 0x10FFFF };
    for (int i = 0; i < (int)(sizeof scalars / sizeof *scalars); i++) {
        REQUIRE(native_char_result("abs(letter)", scalars[i], false, false, false, scalars[i]));
    }
    REQUIRE(native_char_result("negate(int(letter))", 65, false, false, true, -65));
    return true;
}
/* A direct catalog preserves char's nominal identity, including a later
 * tuple field that the caller may never read. These hosts deliberately model
 * a native implementation violating its declared scalar output contract. */
static int native_letter;
static const char *fixed_char(arena *, const pin_value *, int count, pin_value *out)
{
    assert(count == 0);
    *out = (pin_value){ .type = PIN_I32, .i = native_letter };
    return NULL;
}
static const char *pair_char(void *, const pin_value *, pin_value *out)
{
    out[0].i = -1;
    out[1].i = native_letter;
    return NULL;
}
static const char *optional_char(arena *, const pin_value *, int count, pin_value *out)
{
    assert(count == 0);
    *out = (pin_value){ .type = PIN_UNION };
    return NULL;
}
static bool char_host(void *user, uint64_t id, script_lower_host *out)
{
    static const node_host_call fixed = { .value = fixed_char };
    static const node_host_call pair = { .invoke = pair_char };
    static const node_host_call optional = { .value = optional_char };
    switch (id) {
        case 9001: *out = (script_lower_host){ .call = &fixed }; return true;
        case 9002: *out = (script_lower_host){ .call = &pair }; return true;
        case 9003: *out = (script_lower_host){ .call = &optional }; return true;
        default: return host(user, id, out);
    }
}
static bool compile_char_catalog(const char *source, behavior_backend backend)
{
    script_catalog_parameter scalar = { .name = "letter", .type = script_char_type() };
    script_catalog_parameter pair[] = {
        { .name = "count", .type = { .storage = PIN_I32 } },
        { .name = "letter", .type = script_char_type() },
    };
    script_catalog_parameter optional = scalar;
    optional.type.flags = SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_BOXED_OPTIONAL;
    enum
    {
        COUNT = sizeof capabilities / sizeof *capabilities
    };
    script_catalog_capability caps[COUNT + 3];
    memcpy(caps, capabilities, sizeof capabilities);
    const script_catalog_parameter *results[] = { &scalar, pair, &optional };
    const char *names[] = { "NativeChar", "NativePair", "NativeOptionalChar" };
    for (int i = 0; i < 3; i++) {
        caps[COUNT + i] = (script_catalog_capability){
            .id = 9001 + i,
            .name = names[i],
            .kind = SCRIPT_CAPABILITY_FUNCTION,
            .results = results[i],
            .result_count = i == 1 ? 2 : 1,
            .completion_outcome = -1,
            .effects = SCRIPT_EFFECT_HOST,
        };
    }
    script_catalog active = catalog;
    active.capabilities = caps;
    active.capability_count = COUNT + 3;
    syntax = script_parse(source);
    analysis = script_analyze(syntax, &active);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(
            stderr,
            "native char catalog semantic: %s\n",
            analysis->diagnostics[i].message
        );
    }
    REQUIRE(analysis->valid && !syntax->diagnostic_count);
    const script_lower_bindings bindings = { .global = global, .host = char_host };
    owned_plan = script_lower(analysis, &active, &bindings);
    REQUIRE(owned_plan && owned_plan->valid);
    char error[192];
    module = execution_module_finish(&owned_plan, backend, error, sizeof error);
    if (!module) {
        fprintf(stderr, "native char catalog publication: %s\n", error);
    }
    REQUIRE(module && !owned_plan);
    program = execution_program_new(module, world, NULL);
    REQUIRE(program);
    return true;
}
static bool fixed_native_char_result(const char *statement, int scalar, bool fails, int letter, int code)
{
    char source[PROGRAM_SOURCE_CAPACITY];
    snprintf(
        source,
        sizeof source,
        "NativeChars :: behavior { letter: char = 'A'; code: int = 17; "
        "entered: int = 0; finished: int = 0; on Tick :: () { "
        "entered = 1; %s finished = 1; } }",
        statement
    );
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    bool passed = true;
    for (int b = 0; b < 2; b++) {
        prepare();
        native_letter = scalar;
        REQUIRE(compile_char_catalog(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        const pin_value dt = { .type = PIN_F64 };
        int status = execution_instance_dispatch(instance, 10, &dt, 1, 0, 0, 1000);
        bool same = int_state(instance, "entered", PIN_I32, 1);
        if (fails) {
            const execution_program_failure *failure = execution_program_error(program);
            same &= status < 0 && failure && failure->kind == BEHAVIOR_FUNCTION_DATA &&
                    !strcmp(failure->message, "Value does not fit in char") &&
                    failure->start ==
                        (int)(strstr(source + sizeof "NativeChars" - 1, "Native") - source);
        } else {
            same &= status == FLOW_OK;
        }
        same &= int_state(instance, "letter", PIN_I32, fails ? 65 : letter) &&
                int_state(instance, "code", PIN_I32, fails ? 17 : code) &&
                int_state(instance, "finished", PIN_I32, !fails);
        if (!same) {
            fprintf(
                stderr,
                "fixed native char %s scalar %d backend %d: status %d\n",
                statement,
                scalar,
                b,
                status
            );
        }
        passed &= same;
    }
    REQUIRE(passed);
    return true;
}
TEST_CASE(program_fixed_native_char_outputs_are_checked_before_use)
{
    const char *statements[] = {
        "letter = NativeChar();",
        "letter = NativeChar().letter;",
        "code = NativeChar();",
        "values := NativePair(); code = values.count; letter = values.letter;",
        "code = NativePair().count;",
        "NativeChar();",
        "NativePair();",
    };
    const int invalid[] = { -1, 0xD800, 0xDFFF, 0x110000 };
    bool passed = true;
    for (int i = 0; i < 7; i++) {
        for (int j = 0; j < 4; j++) {
            passed &= fixed_native_char_result(statements[i], invalid[j], true, 0, 0);
        }
    }
    REQUIRE(passed);
    return true;
}
TEST_CASE(program_fixed_native_char_outputs_preserve_valid_and_optional_values)
{
    const int scalars[] = { 0, 0xD7FF, 0xE000, 0x10FFFF };
    for (int i = 0; i < 4; i++) {
        REQUIRE(
            fixed_native_char_result("letter = NativeChar();", scalars[i], false, scalars[i], 17)
        );
        REQUIRE(fixed_native_char_result(
            "values := NativePair(); code = values.count; letter = "
            "values.letter;",
            scalars[i],
            false,
            scalars[i],
            -1
        ));
    }
    REQUIRE(fixed_native_char_result(
        "value := NativeOptionalChar(); if value == nil { code = 7; }",
        0,
        false,
        65,
        7
    ));
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
                         "  total: f64 = 0;\n"
                         "  on Impulse :: () {\n"
                         "    xs: f64[] = [];\n"
                         "    i: f64 = 0;\n"
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
        REQUIRE(suspended.managed_bytes >= 256 * sizeof(double));
        REQUIRE(suspended.managed_bytes < (256 + 248) * sizeof(double));
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
                         "  total: f64 = 0;\n"
                         "  count: int = 0;\n"
                         "  on Impulse :: () {\n"
                         "    xs: f64[] = [];\n"
                         "    i: f64 = 0;\n"
                         "    while (i < 300) { xs.append(i); i += 1; }\n"
                         "    ys: f64[] = [1000, 2000];\n"
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
                          "    xs: f64[] = [];\n"
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
    const char *source = "Exact :: behavior { low:i64=0; high:i64=0; "
                         "unsigned_max:u64=0; precise:u64=0; mode:Mode=1; on "
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
        "Bad :: behavior { amount:i64=9223372036854775808; }",
        "Bad :: behavior { amount:i64=-9223372036854775809; }",
        "Bad :: behavior { amount:u64=18446744073709551616; }",
        "Bad :: behavior { amount:u64=-1; }",
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
        *source = "Copies :: behavior { source_byte:u8=255; "
                  "copied_byte:u8=0; source_name:name=\"token\"; "
                  "copied_name:name=\"\"; source_handle:Entity=0; "
                  "copied_handle:Entity=0; source_big:u64=9007199254740993; "
                  "copied_big:u64=0; count:int=0; on Tick :: () { "
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
        REQUIRE(
            name && name->type == PIN_NAME && pin_text(*name).bytes &&
            !strcmp(pin_text(*name).bytes, "token")
        );
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
        *source = "Widths :: behavior { widened:f64=0; narrowed:number=0; on "
                  "Tick :: () { widened=f64(NativeFloat()); "
                  "narrowed=number(widened+0.25); } }";
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
                "number conversion semantic: %s\n",
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
            fprintf(stderr, "number conversion publication: %s\n", error);
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
        *source = "Mixed :: behavior { count:int=3; level:number=2.5; "
                  "below:bool=false; scaled:number=0; on Tick :: () { below = "
                  "level < count; scaled = number(count); } }";
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
        *source = "StateMachines :: behavior { evaluated:f64=0; "
                  "order:f64=0; once_hits:f64=0; count_hits:f64=0; "
                  "flips_a:f64=0; flips_b:f64=0; gate_hits:f64=0; "
                  "multi_a:f64=0; multi_b:f64=0; counter:f64=-1; "
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
    const char *source = "Retriggers :: behavior { evaluations:f64=0; "
                         "completions:f64=0; Duration :: "
                         "()->(seconds:f64){evaluations+=1;return(3);} action "
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
    static const char tight[] = "Debugger :: behavior { calls:f64=0; "
                                "observed:f64=0; Work :: "
                                "()->(Value:f64,Text:string){calls+=1;"
                                "return(calls,\"a b\");} "
                                "on Tick :: () { values:=Work(); "
                                "observed=values.Value; observed+=10; } }";
    static const char loose
        [] = "Debugger :: behavior {\n    calls: f64 = 0;\n    observed: "
             "f64 = 0;\n"
             "    Work :: () -> (Value: f64, Text: string) {\n        calls "
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
    const char *source = "Debugger :: behavior { calls:f64=0; "
                         "observed:f64=0; text:string=\"unset\"; Work :: "
                         "()->(Value:f64,Text:string){calls+=1;return(calls,"
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
            if (w->availability == EXECUTION_WATCH_AVAILABLE &&
                w->value.type == PIN_STR && pin_text(w->value).bytes &&
                !strcmp(pin_text(w->value).bytes, "snapshot")) {
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
        REQUIRE(!strcmp(
            pin_text(execution_inspector_watch_at(debugger, saved)->value).bytes,
            "snapshot"
        ));
        REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_RESUME) == FLOW_ERR_STATE);
        REQUIRE(!execution_inspector_pause(debugger, owner));
    }
    return true;
}

static const char *const task_source =
    "Tasks :: behavior { steps: f64 = 0; out: f64 = 0; seen: f64 = "
    "0;Count :: (n: f64) -> (doubled: f64) { steps += 1; yield; steps += "
    "1;await Delay(1); steps += 1; return(n * 2); }on Impulse :: (amount: "
    "f64) "
    "{ t := Count(amount); if t { seen += 1; }answer := await t; if answer is "
    "f64 value { out = value; } if !t { seen += 10; } } }";
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
TEST_CASE(program_bound_task_checks_admission_before_its_body)
{
    const char *source = "L :: behavior { began: f64 = 0; done: f64 = 0; "
                         "cancelled: f64 = 0; "
                         "worker: task; Bound :: () -> () { began += 1; await "
                         "Pending(); done += 1; } "
                         "Watch :: () -> () { result := await worker; "
                         "if result is Cancelled { cancelled += 1; } } "
                         "on Impulse :: () { worker = Bound(); Watch(); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        for (int mode = 0; mode < 4; mode++) {
            prepare();
            REQUIRE(lifetime_compile(source, backends[b]));
            if (mode == 1) {
                lifetime_target_count = 1; /* An absent explicit target. */
            } else if (mode == 2) {
                lifetime_bind(world, positioned(2, false));
                REQUIRE(lifetime_remove());
                program_position replacement = { .x = 10 };
                REQUIRE(
                    ecs_world_record_add(world, lifetime_target, 501, &replacement, 0) == ECS_OK
                );
                REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
                REQUIRE(!ecs_component_cell_alive(lifetime_cells[0]));
            } else if (mode == 3) {
                lifetime_foreign_world = ecs_world_new();
                REQUIRE(
                    ecs_world_register_component(lifetime_foreign_world, &position_component, NULL) == ECS_OK
                );
                ecs_entity foreign = ecs_world_create_entity(lifetime_foreign_world);
                program_position position = { .x = 2 };
                REQUIRE(
                    ecs_world_record_add(lifetime_foreign_world, foreign, 501, &position, 0) == ECS_OK
                );
                REQUIRE(ecs_world_apply_commands(lifetime_foreign_world) == ECS_OK);
                lifetime_bind(lifetime_foreign_world, foreign);
            }
            execution_instance *instance = execution_program_attach(
                program,
                ecs_entity_to_u64(ecs_world_create_entity(world))
            );
            REQUIRE(instance);
            REQUIRE(lifetime_dispatch(instance, 0) == FLOW_OK);
            REQUIRE(number_state(instance, "began", mode == 0 ? 1 : 0));
            REQUIRE(number_state(instance, "done", 0));
            REQUIRE(execution_program_update(program, 0.1, 0, 1000) == FLOW_OK);
            REQUIRE(number_state(instance, "cancelled", mode == 0 ? 0 : 1));
            REQUIRE(lifetime_request_count == (mode == 0 ? 1 : 0));
            if (mode == 3) {
                REQUIRE(ecs_component_cell_alive(lifetime_cells[0]));
            }
        }
    }
    return true;
}
TEST_CASE(program_effect_free_bound_task_uses_the_owning_program_world)
{
    const char *source = "L :: behavior { worker: task(f64); running: "
                         "f64 = 0; cancelled: f64 = 0; "
                         "Bound :: () -> f64 { yield; return 7; } "
                         "Watch :: () -> () { result := await worker; "
                         "if result is Cancelled { cancelled += 1; } } "
                         "on Impulse :: () { worker = Bound(); if worker { "
                         "running += 1; } Watch(); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        for (int foreign = 0; foreign < 2; foreign++) {
            prepare();
            REQUIRE(lifetime_compile(source, backends[b]));
            ecs_world *target_world = world;
            if (foreign) {
                lifetime_foreign_world = ecs_world_new();
                target_world = lifetime_foreign_world;
                REQUIRE(
                    ecs_world_register_component(target_world, &position_component, NULL) == ECS_OK
                );
            }
            ecs_entity target = ecs_world_create_entity(target_world);
            program_position position = { .x = 2 };
            REQUIRE(ecs_world_record_add(target_world, target, 501, &position, 0) == ECS_OK);
            REQUIRE(ecs_world_apply_commands(target_world) == ECS_OK);
            lifetime_bind(target_world, target);
            execution_instance *instance = execution_program_attach(
                program,
                ecs_entity_to_u64(ecs_world_create_entity(world))
            );
            REQUIRE(instance && lifetime_dispatch(instance, 0) == FLOW_OK);
            REQUIRE(number_state(instance, "running", foreign ? 0 : 1));
            REQUIRE(execution_program_update(program, 0.1, 0, 1000) == FLOW_OK);
            REQUIRE(number_state(instance, "cancelled", foreign ? 1 : 0));
            REQUIRE(ecs_component_cell_alive(lifetime_cells[0]));
        }
    }
    return true;
}
TEST_CASE(program_bound_task_cancels_pending_children_and_preserves_unbound_pointer_users)
{
    const char *source =
        "L :: behavior { worker: task; began: f64 = 0; done: f64 = 0; "
        "woke: f64 = 0; cancelled: f64 = 0; survived: f64 = 0; "
        "Leaf :: () -> () { await Pending(); done += 1; } "
        "Bound :: (held: ^Position) -> () { began += 1; Leaf(); Leaf(); "
        "await Pending(); done += 10; } "
        "Unbound :: (held: ^Position) -> () { await Delay(2); "
        "if held == held { survived += 1; } } "
        "Watch :: () -> () { result := await worker; "
        "if result is Cancelled { cancelled += 1; } woke += 1; } "
        "on Impulse :: () { for row in query(p: ^Position) { "
        "worker = Bound(row.p); Unbound(row.p); cancel worker after 100; "
        "Watch(); } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(lifetime_compile(source, backends[b]));
        lifetime_bind(world, positioned(2, false));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        REQUIRE(instance && lifetime_dispatch(instance, 0) == FLOW_OK);
        REQUIRE(number_state(instance, "began", 1) && lifetime_request_count == 3);
        REQUIRE(number_state(instance, "woke", 0));
        REQUIRE(lifetime_remove());
        program_position replacement = { .x = 10 };
        REQUIRE(ecs_world_record_add(world, lifetime_target, 501, &replacement, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_update(program, 0.1, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "cancelled", 1) && number_state(instance, "woke", 1));
        REQUIRE(number_state(instance, "done", 0));
        REQUIRE(lifetime_requests_cancelled(3));
        REQUIRE(execution_program_update(program, 2.1, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "survived", 1));
        REQUIRE(number_state(instance, "done", 0) && number_state(instance, "woke", 1));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}
TEST_CASE(program_bound_task_does_not_consume_ready_external_results_after_removal)
{
    const char
        *source = "L :: behavior { worker: task; consumed: f64 = 0; "
                  "cancelled: f64 = 0; "
                  "Bound :: () -> () { await Pending(); consumed += 1; } "
                  "Watch :: () -> () { result := await worker; "
                  "if result is Cancelled { cancelled += 1; } } "
                  "on Impulse :: () { worker = Bound(); Watch(); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        for (int synchronous = 0; synchronous < 2; synchronous++) {
            prepare();
            REQUIRE(lifetime_compile(source, backends[b]));
            lifetime_bind(world, positioned(2, false));
            lifetime_remove_in_begin = synchronous;
            execution_instance *instance = execution_program_attach(
                program,
                ecs_entity_to_u64(ecs_world_create_entity(world))
            );
            REQUIRE(instance && lifetime_dispatch(instance, 0) == FLOW_OK);
            REQUIRE(lifetime_request_count == 1);
            if (synchronous) {
                REQUIRE(lifetime_begin_completed);
            } else {
                REQUIRE(lifetime_remove());
                /* Completion races with removal; consumption must still stop. */
                flow_host_complete(lifetime_requests[0], -1, (pin_value){ 0 });
            }
            REQUIRE(execution_program_update(program, 0.1, 0, 1000) == FLOW_OK);
            REQUIRE(number_state(instance, "consumed", 0));
            REQUIRE(number_state(instance, "cancelled", 1));
            REQUIRE(lifetime_requests_cancelled(1));
            REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
            REQUIRE(
                number_state(instance, "consumed", 0) &&
                number_state(instance, "cancelled", 1)
            );
        }
    }
    return true;
}
TEST_CASE(program_bound_task_stops_when_an_inline_outcome_invalidates_its_target)
{
    const char *source = "L :: behavior { worker: task; consumed: f64 = 0; "
                         "cancelled: f64 = 0; "
                         "action gate = Pending(); "
                         "Bound :: () -> () { gate(); await Delay(100); "
                         "consumed += 10; } "
                         "after gate { await Pending(); consumed += 1; } "
                         "Watch :: () -> () { result := await worker; "
                         "if result is Cancelled { cancelled += 1; } } "
                         "on Impulse :: () { worker = Bound(); Watch(); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(lifetime_compile(source, backends[b]));
        lifetime_bind(world, positioned(2, false));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        REQUIRE(instance && lifetime_dispatch(instance, 0) == FLOW_OK);
        REQUIRE(lifetime_request_count == 1);
        lifetime_remove_in_begin = true;
        REQUIRE(flow_host_complete(lifetime_requests[0], -1, (pin_value){ 0 }));
        REQUIRE(execution_program_update(program, 0.1, 0, 1000) == FLOW_OK);
        REQUIRE(lifetime_begin_completed && lifetime_requests_cancelled(2));
        REQUIRE(number_state(instance, "consumed", 0) && number_state(instance, "cancelled", 1));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}
TEST_CASE(program_failing_inline_outcome_detaches_its_completed_request)
{
    const char
        *source = "L :: behavior { worker: task; consumed: f64 = 0; "
                  "cancelled: f64 = 0; action "
                  "gate = Pending(); "
                  "Bound :: () -> () { yield; await gate(); "
                  "consumed += 10; } "
                  "after gate { value := fail_native(); consumed += 1; } "
                  "Watch :: () -> () { result := await worker; "
                  "if result is Cancelled { cancelled += 1; } } "
                  "on Impulse :: () { worker = Bound(); Watch(); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        for (int synchronous = 0; synchronous < 2; synchronous++) {
            prepare();
            REQUIRE(lifetime_compile(source, backends[b]));
            lifetime_bind(world, positioned(2, false));
            lifetime_complete_in_begin = synchronous;
            strcpy(failure_message, "inline outcome failed");
            execution_instance *instance = execution_program_attach(
                program,
                ecs_entity_to_u64(ecs_world_create_entity(world))
            );
            REQUIRE(instance && lifetime_dispatch(instance, 0) == FLOW_OK);
            int status = execution_program_update(program, 0.1, 0, 1000);
            REQUIRE(lifetime_request_count == 1);
            if (!synchronous) {
                REQUIRE(status == FLOW_OK);
                REQUIRE(flow_host_complete(lifetime_requests[0], -1, (pin_value){ 0 }));
                status = execution_program_update(program, 0.2, 0, 1000);
            }
            REQUIRE(number_state(instance, "consumed", 0));
            REQUIRE(number_state(instance, "cancelled", 1));
            REQUIRE(flow_host_status(lifetime_requests[0]) == FLOW_HOST_FINISHED);
            REQUIRE(!flow_host_complete(lifetime_requests[0], -1, (pin_value){ 0 }));
            REQUIRE(flow_host_resume(lifetime_requests[0]) < 0);
            REQUIRE(flow_host_resume(lifetime_requests[0]) < 0);
            REQUIRE(
                number_state(instance, "consumed", 0) &&
                number_state(instance, "cancelled", 1)
            );
            /* Releasing the service's reference detaches the finished request;
             * a retained inspection handle must not be mistaken for a leak. */
            flow_host_release(lifetime_requests[0]);
            lifetime_requests[0] = NULL;
            lifetime_request_count = 0;
            status = execution_program_update(program, 0.3, 0, 1000);
            REQUIRE(status < 0);
            REQUIRE(execution_program_error(program));
            REQUIRE(strstr(execution_program_error(program)->message, "inline outcome failed"));
            execution_program_clear_error(program);
            REQUIRE(execution_program_update(program, 0.4, 0, 1000) == FLOW_OK);
            REQUIRE(execution_instance_pending(instance) == 0);
            REQUIRE(execution_program_memory_usage(program).activation_bytes == 0);
        }
    }
    return true;
}
TEST_CASE(program_bound_task_cannot_succeed_after_its_last_expression_removes_the_target)
{
    const char *source = "L :: behavior { worker: task(f64); got: f64 = 0; "
                         "cancelled: f64 = 0; "
                         "Bound :: () -> f64 { yield; return Invalidate(); } "
                         "Watch :: () -> () { result := await worker; "
                         "if result is f64 value { got = value; } "
                         "if result is Cancelled { cancelled += 1; } } "
                         "on Impulse :: () { worker = Bound(); Watch(); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(lifetime_compile(source, backends[b]));
        lifetime_bind(world, positioned(2, false));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        REQUIRE(instance && lifetime_dispatch(instance, 0) == FLOW_OK);
        REQUIRE(execution_program_update(program, 0.1, 0, 1000) == FLOW_OK);
        REQUIRE(!ecs_component_cell_alive(lifetime_cells[0]));
        REQUIRE(number_state(instance, "got", 0) && number_state(instance, "cancelled", 1));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}
/* A child binds only its own targets. Losing one cancels the child alone;
 * losing a target of its active parent cancels the parent and, through it,
 * the child, even though the child does not depend on that component. */
TEST_CASE(program_bound_child_keeps_its_own_targets_and_follows_parent_cancellation)
{
    const char *
        source = "L :: behavior { parent: task; child: task; started: f64 = "
                 "0; "
                 "parent_done: f64 = 0; child_done: f64 = 0; "
                 "parent_cancelled: f64 = 0; child_cancelled: f64 = 0; "
                 "ExtraBound :: () -> () { await Pending(); child_done += 1; } "
                 "Bound :: () -> () { yield; child = ExtraBound(); await "
                 "Pending(); parent_done += 1; } "
                 "ParentWatch :: () -> () { result := await parent; "
                 "if result is Cancelled { parent_cancelled += 1; } } "
                 "ChildWatch :: () -> () { result := await child; "
                 "if result is Cancelled { child_cancelled += 1; } } "
                 "on Impulse :: () { if started == 0 { parent = Bound(); "
                 "started = 1; ParentWatch(); } "
                 "else { ChildWatch(); } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        for (int parent_first = 0; parent_first < 2; parent_first++) {
            prepare();
            REQUIRE(lifetime_compile(source, backends[b]));
            lifetime_bind(world, positioned(2, false));
            ecs_entity second = positioned(3, false);
            lifetime_cells[1] = ecs_component_cell_acquire(world, second, 501);
            REQUIRE(lifetime_cells[1]);
            lifetime_child_targets[0] = lifetime_child_targets[1] = lifetime_cells[1];
            lifetime_child_target_count = 2;
            execution_instance *instance = execution_program_attach(
                program,
                ecs_entity_to_u64(ecs_world_create_entity(world))
            );
            REQUIRE(instance && lifetime_dispatch(instance, 0) == FLOW_OK);
            REQUIRE(execution_program_update(program, 0.1, 0, 1000) == FLOW_OK);
            REQUIRE(lifetime_request_count == 2);
            REQUIRE(lifetime_dispatch(instance, 0.1) == FLOW_OK);
            if (parent_first) {
                REQUIRE(lifetime_remove());
                REQUIRE(execution_program_update(program, 0.2, 0, 1000) == FLOW_OK);
                REQUIRE(ecs_component_cell_alive(lifetime_cells[1]));
                REQUIRE(number_state(instance, "parent_cancelled", 1));
                REQUIRE(number_state(instance, "child_cancelled", 1));
                REQUIRE(lifetime_requests_cancelled(2));
            } else {
                REQUIRE(ecs_world_record_remove(world, second, 501, 0) == ECS_OK);
                REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
                REQUIRE(execution_program_update(program, 0.2, 0, 1000) == FLOW_OK);
                REQUIRE(number_state(instance, "child_cancelled", 1));
                REQUIRE(number_state(instance, "parent_cancelled", 0));
                REQUIRE(flow_host_status(lifetime_requests[0]) == FLOW_HOST_CANCELLED);
                REQUIRE(execution_request_owner(lifetime_requests[0]) == NULL);
                REQUIRE(flow_host_status(lifetime_requests[1]) == FLOW_HOST_PENDING);
                REQUIRE(ecs_component_cell_alive(lifetime_cells[0]));
                REQUIRE(lifetime_remove());
                REQUIRE(execution_program_update(program, 0.3, 0, 1000) == FLOW_OK);
                REQUIRE(number_state(instance, "parent_cancelled", 1));
                REQUIRE(number_state(instance, "child_cancelled", 1));
                REQUIRE(lifetime_requests_cancelled(2));
            }
            REQUIRE(
                number_state(instance, "parent_done", 0) &&
                number_state(instance, "child_done", 0)
            );
            REQUIRE(execution_instance_pending(instance) == 0);
            REQUIRE(execution_program_memory_usage(program).activation_bytes == 0);
        }
    }
    return true;
}
/* A parent that finishes keeps its result and leaves its children running:
 * removing its component afterwards cancels only the child that depends on
 * that component itself, and an unrelated child finishes. */
TEST_CASE(program_completed_bound_parent_leaves_only_dependent_children_bound)
{
    const char
        *source = "L :: behavior { worker: task(f64); started: f64 = 0; "
                  "got: f64 = 0; "
                  "cancelled: f64 = 0; child_done: f64 = 0; "
                  "dependent_done: f64 = 0; "
                  "Child :: () -> () { await Pending(); child_done += 1; } "
                  "ExtraBound :: () -> () { await Pending(); dependent_done += "
                  "1; } "
                  "Bound :: () -> f64 { yield; Child(); ExtraBound(); yield; "
                  "return 7; } "
                  "Watch :: () -> () { result := await worker; "
                  "if result is f64 value { got += value; } "
                  "if result is Cancelled { cancelled += 1; } } "
                  "on Impulse :: () { if started == 0 { worker = Bound(); "
                  "started = 1; } Watch(); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(lifetime_compile(source, backends[b]));
        lifetime_bind(world, positioned(2, false));
        lifetime_child_targets[0] = lifetime_cells[0];
        lifetime_child_target_count = 1;
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        REQUIRE(instance && lifetime_dispatch(instance, 0) == FLOW_OK);
        REQUIRE(execution_program_update(program, 0.1, 0, 1000) == FLOW_OK);
        REQUIRE(lifetime_request_count == 2);
        REQUIRE(execution_program_update(program, 0.15, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "got", 7));
        REQUIRE(flow_host_status(lifetime_requests[0]) == FLOW_HOST_PENDING);
        REQUIRE(flow_host_status(lifetime_requests[1]) == FLOW_HOST_PENDING);
        REQUIRE(lifetime_remove());
        REQUIRE(execution_program_update(program, 0.2, 0, 1000) == FLOW_OK);
        REQUIRE(flow_host_status(lifetime_requests[0]) == FLOW_HOST_PENDING);
        REQUIRE(flow_host_status(lifetime_requests[1]) == FLOW_HOST_CANCELLED);
        REQUIRE(flow_host_complete(lifetime_requests[0], -1, (pin_value){ 0 }));
        REQUIRE(execution_program_update(program, 0.3, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "child_done", 1));
        REQUIRE(number_state(instance, "dependent_done", 0));
        REQUIRE(lifetime_dispatch(instance, 0.4) == FLOW_OK);
        REQUIRE(number_state(instance, "got", 14) && number_state(instance, "cancelled", 0));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}
/* Authored tasks depend on the components their pointer arguments root at
 * when the task may dereference them, on any path and through synchronous
 * callees, including scoped parameters such as Poison's, which reach the
 * lent target through references. Holding, comparing or handing a pointer to
 * another task does not. */
TEST_CASE(program_task_infers_component_dependencies)
{
    static const char *const starts[] = {
        "worker = Poison(row.p);",       "worker = Poison(row.p);",
        "worker = Later(row.p, false);", "worker = Holds(row.p, row.v);",
        "worker = Holds(row.p, row.v);", "worker = Spawns(row.p);",
    };
    /* Component each scenario removes during the first wait, or zero. */
    static const uint64_t removed[] = { 501, 0, 501, 501, 502, 501 };
    static const double cancelled[] = { 1, 0, 1, 0, 1, 0 };
    static const double done[] = { 0, 1, 0, 1, 0, 1 };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        for (int scenario = 0; scenario < 6; scenario++) {
            char source[2048];
            snprintf(
                source,
                sizeof source,
                "L :: behavior { worker: task; done: f64 = 0; "
                "cancelled: f64 = 0; child: f64 = 0; "
                "Poison :: (p: ^Position) -> () { p.x -= 1; await Delay(2); "
                "done += 1; } "
                "Nudge :: (p: ^Position) -> () { p.x += 1; } "
                "Later :: (p: ^Position, go: bool) -> () { await Delay(2); "
                "done += 1; if go { Nudge(p); } } "
                "Holds :: (p: ^Position, v: ^Velocity) -> () { v.x += 1; "
                "await Delay(2); if p == p { done += 1; } } "
                "Child :: (p: ^Position) -> () { await Delay(1); p.x += 1; "
                "child += 1; } "
                "Spawns :: (p: ^Position) -> () { Child(p); await Delay(2); "
                "done += 1; } "
                "Watch :: () -> () { result := await worker; "
                "if result is Cancelled { cancelled += 1; } } "
                "on Impulse :: () { for row in query(p: ^Position, v: "
                "^Velocity) { "
                "%s Watch(); } } }",
                starts[scenario]
            );
            prepare();
            lifetime_inferred = true;
            REQUIRE(lifetime_compile(source, backends[b]));
            ecs_entity target = positioned(2, true);
            execution_instance *instance = execution_program_attach(
                program,
                ecs_entity_to_u64(ecs_world_create_entity(world))
            );
            REQUIRE(instance && lifetime_dispatch(instance, 0) == FLOW_OK);
            if (removed[scenario]) {
                REQUIRE(ecs_world_record_remove(world, target, removed[scenario], 0) == ECS_OK);
                REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
            }
            REQUIRE(execution_program_update(program, 0.1, 0, 1000) == FLOW_OK);
            REQUIRE(number_state(instance, "cancelled", cancelled[scenario]));
            if (removed[scenario] == 501) {
                /* A later add is a different target and revives nothing. */
                program_position replacement = { .x = 10 };
                REQUIRE(ecs_world_record_add(world, target, 501, &replacement, 0) == ECS_OK);
                REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
            }
            REQUIRE(execution_program_update(program, 2.1, 0, 1000) == FLOW_OK);
            REQUIRE(number_state(instance, "done", done[scenario]));
            REQUIRE(number_state(instance, "cancelled", cancelled[scenario]));
            /* The child that dereferences the removed target stops alone. */
            REQUIRE(number_state(instance, "child", 0));
            /* An awaiter can wake on the update after its task finishes. */
            REQUIRE(execution_program_update(program, 2.2, 0, 1000) == FLOW_OK);
            REQUIRE(execution_instance_pending(instance) == 0);
        }
    }
    return true;
}
TEST_CASE(program_bound_task_resources_detach_in_each_destruction_order)
{
    const char
        *source = "L :: behavior { worker: task; done: f64 = 0; "
                  "Child :: () -> () { await Pending(); done += 1; } "
                  "Bound :: () -> () { Child(); await Pending(); done += 10; } "
                  "on Impulse :: () { worker = Bound(); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        for (int order = 0; order < 4; order++) {
            prepare();
            REQUIRE(lifetime_compile(source, backends[b]));
            lifetime_bind(world, positioned(2, false));
            ecs_entity owner = ecs_world_create_entity(world);
            execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
            REQUIRE(instance && lifetime_dispatch(instance, 0) == FLOW_OK);
            REQUIRE(lifetime_request_count == 2);
            if (order == 0) {
                execution_program_detach(program, ecs_entity_to_u64(owner));
            } else if (order == 1) {
                execution_program_free(program);
                program = NULL;
            } else if (order == 2) {
                ecs_world_free(world);
                world = NULL;
                REQUIRE(!ecs_component_cell_alive(lifetime_cells[0]));
                execution_program_free(program);
                program = NULL;
            } else {
                REQUIRE(ecs_world_destroy_entity(world, lifetime_target) == ECS_OK);
                REQUIRE(execution_program_update(program, 0.1, 0, 1000) == FLOW_OK);
                REQUIRE(number_state(instance, "done", 0));
            }
            REQUIRE(lifetime_requests_cancelled(2));
            /* A second teardown must not touch the detached host-owned handles. */
            execution_program_free(program);
            program = NULL;
            REQUIRE(lifetime_requests_cancelled(2));
        }
    }
    return true;
}
TEST_CASE(program_task_cancel_cascades_and_wakes_awaiters)
{
    const char *
        source = "Cancel :: behavior { inner: f64 = 0; outer: f64 = 0; "
                 "woke: f64 = 0;worker: task;Leaf :: () -> () { await "
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
        *source = "K :: behavior { kept: f64 = 0; worker: task; self_task: "
                  "task;inline: task; got: f64 = 0;A :: () -> () { await "
                  "Delay(1); kept += 1; }C :: () -> () { yield; cancel worker; "
                  "kept += 10 }P :: () -> () { C(); A(); await Delay(5); kept "
                  "+= 100 }Q :: () -> (v: f64) { yield; cancel self_task; "
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
        *source = "C :: behavior { ticks: f64 = 0; worker: task;Spin :: () "
                  "-> () { while true { ticks += 1; yield } }Count :: () -> "
                  "(shown: f64) { return 1 }action act = Count();P :: () -> "
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
            "S :: behavior { worker: task; hit: f64 = 0;"
            "Count :: () -> (shown: f64) { return 1 }"
            "action act = Count();"
            "P :: () -> () { yield; act(); await Delay(10) }"
            "after act { %s }"
            "on Impulse :: (amount: f64, enabled: bool) { worker = P() } }",
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
        "Many :: behavior { done: f64 = 0; kept: f64 = 0; "
        "stale: f64 = 0; got: f64 = -1; first: task;Short :: "
        "() -> (v: f64) { yield; done += 1; return 7 }Long :: () "
        "-> () { await Delay(1); kept += 1; }on Impulse :: (amount: f64, "
        "enabled: bool) { if !enabled { cancel first; if first { stale += "
        "1; } } else if amount == 0 { first = Short(); } else if "
        "amount == 1 { Short(); } else if amount == 3 { answer := "
        "await first; if answer is f64 value { got = value } } "
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
        "Deadlines :: behavior { worker: task; began: f64 = 0; "
        "done: f64 = 0;scheduled: f64 = 0; cancelled: f64 = "
        "0; continued: f64 = 0;Work :: () -> (value: f64) { "
        "began += 1; await Delay(100); done += 1; return 7 }Watch :: "
        "() -> () { result := await worker; if result is Cancelled { "
        "cancelled += 1; } continued += 1; }on Impulse :: (amount: f64, "
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
        source = "Results :: behavior { first: task; copy: task; got: f64 = "
                 "0; done: f64 = 0;Work :: () -> (value: f64) { yield; "
                 "done += 1; return 7 }on Impulse :: (amount: f64) { if "
                 "amount "
                 "== 0 { first = Work(); copy = first; } else if amount == 1 { "
                 "Work(); } else { cancel copy after 0; result := await copy; "
                 "if result is f64 value { got = value; } } } }";
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
    const char *source = "Capture :: behavior { got: f64 = 0;Work :: () -> "
                         "(value: f64) { yield; return 7 }Start :: () { "
                         "held := Work(); Observe :: (held: ^task(f64)) "
                         "{ await Delay(10); result := await held^; if result "
                         "is f64 value { got = value; } } Observe(&held); "
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
        *source = "Arrays :: behavior { got: f64 = 0;Work :: () -> f64 { "
                  "yield; return 7; }Pack :: () -> task(f64)[] { held: "
                  "task(f64)[] = [Work()]; return held; }on Impulse :: "
                  "(amount: f64, "
                  "enabled: bool) { first: task(f64)[] = []; i := 0;while i "
                  "< 2 { "
                  "row := Pack(); if i == 0 { first = row; } i += 1; }copy := "
                  "first; first = [];yield; await Delay(10); result := await "
                  "copy[0];if result is f64 value { got = value; } } }";
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
    const char *source = "Nested :: behavior { pulse: task(f64); held: "
                         "u64[] = []; "
                         "Work :: () -> f64 { yield; return 7; } "
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
        pin_map_entry entry = {
            { .type = PIN_STR,
              .text = &(const pin_string){ .bytes = "job", .length = sizeof("job") - 1 } },
            pulse
        };
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
        "Scope :: behavior { got: f64 = 0;Work :: () -> f64 { yield; "
        "return 7; }Pack :: () -> (number: f64, discarded: task(f64)) "
        "{return(number: 1, discarded: Work()); }Observe :: (held: ^"
        "task(f64)) { await Delay(10);result := await held^; if result is "
        "f64 value { got = value; } }on Impulse :: () {Pack(); "
        "unused := Pack().number;if true { held := Work(); Observe(&held); "
        "}i "
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
        "Values :: behavior { zero: f64 = 1; false_seen: bool = false; "
        "empty_seen: bool = false; nil_seen: bool = false; optional_zero_seen: "
        "bool = false;cancelled: f64 = 0;Zero :: () -> (value: f64) { "
        "yield; return 0; }False :: () -> (value: bool) { yield; return false; "
        "}Empty :: () -> (value: string) { yield; return \"\"; }None :: () -> "
        "f64? { yield; return nil; }Some :: () -> f64? { yield; return "
        "0; }on Impulse :: () { z := Zero(); f := False(); s := "
        "Empty(); k := Zero();n := None(); o := Some();cancel k; rz := await "
        "z; rf := await f; rs := await s; rk := await k;if rz is f64 value "
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
        *source = "Waiters :: behavior { worker: task; total: f64 "
                  "= 0;Work :: () -> f64[] { yield; return [7]; }Observe :: "
                  "(offset: f64) { result := await worker; if result is not "
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
        REQUIRE(text && pin_text(*text).bytes && !strcmp(pin_text(*text).bytes, "retained"));
    }
    return true;
}

TEST_CASE(program_task_await_shares_large_payload)
{
    enum
    {
        ITEMS = 8192
    };
    const char
        *source = "Delivery :: behavior { input: f64[] = []; worker: "
                  "task;Work :: () -> f64[] { yield; return "
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
         * The packed state array grows by its items, but the destination
         * shares the delivered array's backing, so delivery costs the same
         * whatever the payload's size. */
        REQUIRE(physical[1] - physical[0] == (ITEMS - 1) * sizeof(double));
        REQUIRE(requested[1] == requested[0]);
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
                         "(value: f64) { yield; return 7; }on "
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

static pin_pointer *managed_cell(arena *heap, pin_value value)
{
    pin_pointer *cell = arena_alloc(heap, sizeof *cell);
    *cell = (pin_pointer){ .value = value, .storage = heap };
    pin_arena_trace_pointer(heap, cell);
    return cell;
}
/* A long chain of managed cells holds the only handle to a finished task, and
 * a long ring of cells sits beside it. Authored records with pointer fields
 * reach a task through the same pointer steps; cells that hold cells give the
 * longest chain this catalog can state. Collection follows every link without
 * native recursion, visits the ring once, and releases the task once the
 * chain is unreachable. */
TEST_CASE(program_task_collection_follows_long_pointer_chains)
{
    enum
    {
        LINKS = 200000
    };
    const char *source = "Keeper :: behavior { saved: ^task?; loop: ^task?; "
                         "held: task; "
                         "Work :: () -> (value: f64) { yield; return 7; } "
                         "on Impulse :: () { held = Work(); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_memory_usage(program).retained_tasks == 1);
        uint64_t saved = state_id("saved"), loop = state_id("loop"),
                 held = state_id("held");
        const pin_value task = *execution_instance_state(instance, held);
        REQUIRE(task.type == PIN_TASK && task.u64);
        /* Each state write collects, so build each structure just before
         * the write that roots it. */
        arena *heap = &program->managed->storage;
        pin_pointer *first = managed_cell(heap, (pin_value){ .type = PIN_POINTER });
        pin_pointer *ring = first;
        for (int i = 0; i < LINKS; i++) {
            ring = managed_cell(heap, (pin_value){ .type = PIN_POINTER, .ptr = ring });
        }
        first->value.ptr = ring;
        /* A state write frees the state's previous type name, so each write
         * starts from a fresh read. */
        pin_value root = *execution_instance_state(instance, loop);
        root.ptr = ring;
        REQUIRE(execution_instance_set_state(instance, loop, root) == FLOW_OK);
        pin_pointer *chain = managed_cell(heap, task);
        for (int i = 0; i < LINKS; i++) {
            chain = managed_cell(heap, (pin_value){ .type = PIN_POINTER, .ptr = chain });
        }
        root = *execution_instance_state(instance, saved);
        root.ptr = chain;
        REQUIRE(execution_instance_set_state(instance, saved, root) == FLOW_OK);
        REQUIRE(
            execution_instance_set_state(instance, held, (pin_value){ .type = PIN_TASK }) == FLOW_OK
        );
        REQUIRE(execution_program_memory_usage(program).retained_tasks == 1);
        root = *execution_instance_state(instance, saved);
        root.ptr = NULL;
        REQUIRE(execution_instance_set_state(instance, saved, root) == FLOW_OK);
        REQUIRE(execution_program_memory_usage(program).retained_tasks == 0);
    }
    return true;
}

TEST_CASE(program_task_results_keep_nested_task_handles_alive)
{
    const char
        *source = "Nested :: behavior { held: task; observed: f64 = 0;Inner "
                  ":: () -> (value: f64) { yield; return 7; }Outer :: () -> "
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

TEST_CASE(program_query_staging_roots_keep_completed_tasks_alive)
{
    const char *source = "Held :: behavior { work: task;"
                         "Work :: () -> (value: f64) { yield; return 7; }"
                         "on Impulse :: () { work = Work(); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        for (int staging = 0; staging < 3; staging++) {
            prepare();
            REQUIRE(compile(source, backends[backend]));
            execution_instance *instance = execution_program_attach(
                program,
                ecs_entity_to_u64(ecs_world_create_entity(world))
            );
            const pin_value arguments[] = { { .type = PIN_F64 }, { .type = PIN_BOOL } };
            REQUIRE(execution_instance_dispatch(instance, 11, arguments, 2, 0, 0, 1000) == FLOW_OK);
            REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
            pin_value handle = *execution_instance_state(instance, state_id("work"));
            REQUIRE(handle.type == PIN_TASK && handle.u64);
            pin_query query = { .kind = PIN_QUERY_ROW, .values = &handle, .count = 1 };
            pin_value row = { .type = PIN_QUERY, .ptr = &query };
            if (staging == 0) {
                instance->query_row = row;
            } else if (staging == 1) {
                apush(program->query_values, row);
            } else {
                apush(program->system_values, row);
            }
            /* Isolate task reachability from payload collection: these row
             * fields are already retained by the caller of the task scan. */
            execution_activation deferred = { 0 };
            instance->running = &deferred;
            int status = execution_instance_set_state(
                instance,
                state_id("work"),
                (pin_value){ .type = PIN_TASK }
            );
            instance->running = NULL;
            REQUIRE(status == FLOW_OK);
            program_tasks_collect(program);
            execution_memory_usage memory = execution_program_memory_usage(program);
            REQUIRE(memory.retained_tasks == 1 && memory.task_result_bytes > 0);
            instance->query_row = (pin_value){ 0 };
            asetlen(program->query_values, 0);
            asetlen(program->system_values, 0);
            program_tasks_collect(program);
            memory = execution_program_memory_usage(program);
            REQUIRE(memory.retained_tasks == 0 && memory.task_result_bytes == 0);
        }
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
        *source = "Prepared :: behavior { worker: task; stored: f64 | "
                  "Cancelled = Cancelled;got: f64 = 0; initial: f64 = "
                  "0;Work :: () -> (value: f64) { yield; return 7; }on "
                  "Impulse :: (amount: f64) { if stored is Cancelled { "
                  "initial "
                  "+= 1; }worker = Work(); cancel worker after amount; answer "
                  ":= await worker; stored = answer;if answer is f64 value "
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
    const char *source = "Loop :: behavior { ticks: f64 = 0; hits: f64 = "
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
        *source = "Many :: behavior { done: f64 = 0;Wait :: (n: f64) -> "
                  "() { row: f64[] = [n, n];await Delay(1); done += row[0] "
                  "- n + 1; }on Impulse :: (amount: f64) { for (i in "
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
        { "B :: behavior { n: f64 = 0; on Impulse :: () { cancel "
          "n; } }",
          "Cancel requires a task" },
        { "B :: behavior { t: task; n: f64 = 0; on Impulse :: () "
          "{ n = t; } }",
          "" },
        /* System-retained bindings belong to the registration, not a row's
         * activation, and cannot own tasks. */
        { "S :: system(p: ^Position) { worker: retain task }", "System-owned retained bindings cannot contain tasks; keep task handles in coroutine locals" },
        /* An unawaited action continues before its result exists. */
        { "B :: behavior { n: f64 = 0; on Impulse :: () { x := "
          "make_thing(); n = x } }",
          "Await this call before using its result" },
        { "B :: behavior { pulse: task; power: f64 = 0;Charge :: (time: "
          "f64) -> (power: f64) { yield; return time }on Impulse :: "
          "(amount: f64, "
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
/* Declaring a waiting function does not make its parent wait. */
TEST_CASE(program_nested_waiting_functions_leave_parents_running)
{
    const char *plain = "P :: behavior { n: f64 = 0;"
                        "Outer :: () -> (v: f64) { "
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
/* Writes content/input.kdl under a new temporary root. */
static bool input_project(char root[], const char *map)
{
    REQUIRE(mkdtemp(root));
    char path[256];
    snprintf(path, sizeof path, "%s/" INPUT_MAP_FILE, root);
    FILE *file = fopen(path, "wb");
    REQUIRE(file);
    fputs(map, file);
    fclose(file);
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
    const char *canonical = "Jump {\n"
                            "    (array)sources \"Space\" \"Z\"\n"
                            "}\n"
                            "Fire {\n"
                            "    (array)sources \"X\"\n"
                            "    firing #true\n"
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
        { "Jump {\n    (array)sources \"Nope\"\n}\n", "Jump: sources must list" },
        { "Jump {\n    (array)sources\n}\n", "Jump: sources must list" },
        { "Jump {\n    (array)sources \"Z\"\n}\nJump {\n    (array)sources "
          "\"X\"\n}\n",
          "duplicate" },
        { "\"\" {\n    (array)sources \"Z\"\n}\n", "an action needs a name" },
        { "Jump {\n    (array)sources \"Z\"\n    firing 1\n}\n", "optional firing flag" },
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
    const char *cooperative = "P1Left {\n"
                              "    (array)sources \"Z\"\n"
                              "    controller 0\n"
                              "    (array)buttons \"Dpad Left\"\n"
                              "}\n"
                              "P2Confirm {\n"
                              "    controller 1\n"
                              "    (array)buttons \"A\"\n"
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
        "P {\n controller 4\n (array)buttons \"A\"\n}\n",
        "P {\n controller -1\n (array)buttons \"A\"\n}\n",
        "P {\n controller 0\n}\n",
        "P {\n (array)buttons \"A\"\n}\n",
        "P {\n controller 0\n (array)buttons \"Unknown\"\n}\n",
    };
    for (size_t i = 0; i < sizeof bad_controllers / sizeof *bad_controllers; i++) {
        REQUIRE(!input_map_read(bad_controllers[i], &map, error, sizeof error) && !map);
    }
    /* A map holds at most INPUT_ACTION_MAX actions. */
    size_t size = (INPUT_ACTION_MAX + 1) *
                  sizeof "A256 {\n    (array)sources \"Z\"\n}\n";
    char *many = xmalloc(size);
    size_t most = 0, used = 0;
    for (int i = 0; i <= INPUT_ACTION_MAX; i++) {
        most = used;
        used += (size_t)snprintf(many + used, size - used, "A%d {\n    (array)sources \"Z\"\n}\n", i);
    }
    REQUIRE(!input_map_read(many, &map, error, sizeof error) && !map);
    REQUIRE(strstr(error, "at most"));
    many[most] = 0;
    REQUIRE(input_map_read(many, &map, error, sizeof error));
    REQUIRE(acount(map) == INPUT_ACTION_MAX);
    input_map_free(map);
    free(many);
    return true;
}
/* The catalog finds the references in a scene's or prototype's nested
 * children as well as in its roots, and reports the ones that name nothing. */
TEST_CASE(program_scene_children_are_referenced)
{
    char root[] = "/tmp/chibi-children-XXXXXX", path[256], ship[256];
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/scenes/Start.kdl", root);
    snprintf(ship, sizeof ship, "%s/prototypes/Ship.kdl", root);
    source_catalog_edit edits[] = {
        {
            .path = path,
            .text = "version 1\n"
                    "name \"Start\"\n"
                    "(array)entities {\n"
                    "    - {\n"
                    "        name \"Ship\"\n"
                    "        prototype \"Hull\"\n"
                    "        (array)children {\n"
                    "            - {\n"
                    "                name \"Turret\"\n"
                    "                (array)children {\n"
                    "                    - name=\"Barrel\" prototype=\"Gun\"\n"
                    "                }\n"
                    "            }\n"
                    "        }\n"
                    "    }\n"
                    "}\n",
        },
        {
            .path = ship,
            .text = "name \"Ship\"\n"
                    "(array)children {\n"
                    "    - name=\"Mast\" prototype=\"Pole\"\n"
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
    REQUIRE(input_project(root, "Jump {\n    (array)sources \"Space\"\n}\n"));
    source_catalog *project = source_catalog_new(root, &native);
    /* An unsaved edit of the map replaces the file, and one without text
     * removes it. */
    char map[256];
    snprintf(map, sizeof map, "%s/" INPUT_MAP_FILE, root);
    source_catalog_edit edits[] = {
        { .path = map, .text = "Dash {\n    (array)sources \"X\"\n}\n" },
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
        .text = "Tick {\n    (array)sources \"X\"\n}\n"
                "Jump {\n    (array)sources \"Space\"\n}\n",
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
    snprintf(event, sizeof event, "%s/Jump.kdl", events);
    REQUIRE(!mkdir(events, 0700));
    FILE *file = fopen(event, "wb");
    REQUIRE(file);
    fputs("version 3\nname \"Jump\"\ndomain \"event\"\n", file);
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
    snprintf(prototype, sizeof prototype, "%s/prototypes/Room.kdl", root);
    snprintf(scene, sizeof scene, "%s/scenes/Start.kdl", root);
    const source_catalog_edit edits[] = {
        { .path = prototype,
          .text = "name \"Room\"\ncomponents {\n    GlobalBinding "
                  "name=\"Level\"\n}\n"
                  "(array)children {\n    - {\n        name \"Player\"\n       "
                  " components {\n            GlobalBinding name=\"Player\"\n  "
                  "      }\n    }\n}\n" },
        { .path = scene,
          .text = "name \"Start\"\n(array)entities {\n    - {\n        name "
                  "\"Room\"\n"
                  "        components {\n            GlobalBinding "
                  "name=\"Level\"\n        }\n"
                  "        (array)children {\n            - {\n                "
                  "name \"Camera\"\n"
                  "                components {\n                    "
                  "GlobalBinding name=\"Camera\"\n                }\n          "
                  "  }\n        }\n    }\n}\n" },
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
    const char *source = "Reader :: behavior { x: f64 = 0; on Tick :: () { x "
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
        *source = "B :: behavior { n: f64 = 0;P :: () -> (v: f64) { "
                  "return(n + 1); }W :: () -> (v: f64) { await Delay(1); "
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
    const char
        *source = "B :: behavior { n: f64 = 0;Faster :: (a: f64, b: "
                  "f64) { if a > b { return true } return false }Pick :: (a: "
                  "f64, b: f64) { if Faster(a, b) { return a } return b "
                  "}Split :: (x: f64) { return(Low: x, High: x * 10) }Later "
                  ":: (x: f64) { yield; return x + 1 }on Impulse :: (amount: "
                  "f64) { n += Pick(2, 5); r := Split(amount); n += r.Low + "
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
    const char
        *nested = "B :: behavior { n: f64 = 0;Outer :: () { F :: () { return "
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
        { "B :: behavior { Count :: (n: f64) { if n < 1 { return 0.0 } "
          "return Count(n - 1) + 1 } }",
          "Recursive functions need declared results" },
        { "B :: behavior { Pair :: (x: f64) { return(x, x) } }", "Name each result" },
        { "B :: behavior { Mixed :: (x: f64) { if x > 0 { return true } "
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
        *source = "B :: behavior { n: f64 = 0;P :: (x: f64) -> (a: "
                  "f64, b: f64) { yield; return(x, x * 10); }on "
                  "Impulse :: (amount: f64) { t := P(amount); r := await t; "
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
        !compile("B :: behavior { n: f64 = 0;P :: (x: f64) -> (a: f64, b: f64) { yield; return(x, x * 10); }on Impulse :: (amount: f64) { t := P(amount); n = t.a } }", BEHAVIOR_BYTECODE)
    );
    return true;
}

TEST_CASE(program_library_call_starts_a_task)
{
    const char *library_source = "Store :: library { count: f64 = 10;Work "
                                 ":: () -> (local_count: f64, position_x: "
                                 "f64) {count += 1; await Delay(1); count "
                                 "+= 1; return(count, self.Position.x); } }";
    const char
        *owner_source = "Owner :: behavior { n: f64 = 0; local_count: "
                        "f64 = 0;on Impulse :: () { t := "
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
        *library_source = "Lib :: library { count: f64 = 10;Bump :: (x: ^"
                          "f64) { yield; x^ += 1; count += 1 } }";
    const char *owner_source = "Owner :: behavior { n: f64 = 5;on "
                               "Impulse :: () { library_bump(&n) } }";
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
/* A parameter that only forwards to a scoped parameter of a callable nested
 * in its body is scoped too, so it can lend a state field. */
TEST_CASE(program_nested_forwarding_keeps_pointer_scoped)
{
    const char
        *source = "R :: behavior { seen: f64 = 0; Relay :: (p: ^f64) { "
                  "Local :: (q: ^f64) { q^ = 5 } Local(p) } on Impulse :: "
                  "(amount: f64, enabled: bool) { Relay(&seen) } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "seen", 5));
    }
    return true;
}
/* A typed argument and parentheses forward a scoped pointer as a plain
 * argument does. */
TEST_CASE(program_typed_and_parenthesized_forwarding_keep_pointer_scoped)
{
    const char
        *source = "R :: behavior { a: f64 = 0; b: f64 = 0; c: f64 = 0; "
                  "Bump :: (value: ^f64) { value^ = 5 } "
                  "Typed :: (p: ^f64) { Bump(value: ^f64 = p) } "
                  "Wrapped :: (p: ^f64) { Bump((p)) } "
                  "Through :: (p: ^^f64) { Bump((p^)) } "
                  "on Impulse :: (amount: f64, enabled: bool) { Typed(&a); "
                  "Wrapped(&b); x: f64 = 0; q := &x; Through(&q); c = x } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(
            number_state(instance, "a", 5) && number_state(instance, "b", 5) &&
            number_state(instance, "c", 5)
        );
    }
    return true;
}
TEST_CASE(program_task_keeps_state_and_component_references)
{
    const char
        *source = "R :: behavior { n: f64 = 0;Bump :: (x: ^f64) -> () "
                  "{ yield; x^ += 1; yield; x^ += 1; }on Impulse :: (amount: "
                  "f64, "
                  "enabled: bool) { Bump(&n); Bump(&self.Position.x); } }";
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
        REQUIRE(ecs_component_read_field(NULL, world, owner, 501, 0, &x) == ECS_OK && x.d == 7);
    }
    return true;
}
/* Found porting Celeste's player: a zero-argument function that only calls
 * others, number array indexes and range bounds, and clamp over an int bound. */
TEST_CASE(program_nested_calls_number_indexes_and_mixed_clamp)
{
    const char
        *source = "N :: behavior { n: f64 = 0; w: int = 4; a: f64[] = "
                  "[1, 2, 3];Leaf :: () -> (v: f64) { j := 1.5; "
                  "return(a[int(j)]) }Inner :: () -> (v: f64) { "
                  "return(Leaf() + "
                  "1) }Start :: () -> (v: f64) { return(Inner()) }Bump :: "
                  "() { n += 1 } Relay :: () { Bump() }Kick :: () { Relay() "
                  "}on Impulse :: (amount: f64) { i := 2; a[i] = 10;n = "
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
        *source = "M :: behavior { n: f64 = 0; k: int = 3;Half :: (v: int) "
                  "{ return v % 2 }First :: () { if k == 2 { return 8.5 } "
                  "return Half(k) }Literal :: () { if k == 2 { return 8.0 } "
                  "return Half(k) }Declared :: () -> (r: f64) { return "
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
        *source = "Y :: behavior { done: f64 = 0;Short :: () { yield; done "
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
        *source = "P :: behavior { pulse: task; power: f64 = 0;Charge :: "
                  "(time: f64) -> (power: f64) { yield; return(time * "
                  "2); }on Impulse :: (amount: f64) { pulse = "
                  "Charge(amount); "
                  "answer := await pulse; if answer is f64 value { power = "
                  "value } } }";
    /* The await is analyzed before the assignment that types the state. */
    const char
        *reordered = "P :: behavior { pulse: task; power: f64 = 0;on "
                     "Impulse :: (amount: f64) { Start(amount); answer := "
                     "await pulse; if answer is f64 value { power = value } "
                     "}Start :: (time: f64) { pulse = Charge(time) }Charge "
                     ":: (time: f64) -> (power: f64) { yield; "
                     "return(time * 2); }Other :: () { pulse := 1 } }";
    const char
        *local = "P :: behavior { power: f64 = 0;Charge :: (time: f64) "
                 "-> (power: f64) { yield; return(time * 2); }on "
                 "Impulse :: (amount: f64) { t: task = Charge(amount); "
                 "answer "
                 ":= await t; if answer is f64 value { power = value } } }";
    const char *wrapped = "P :: behavior { pulse: task; power: f64 = 0;on "
                          "Impulse :: (amount: f64) { Start(amount); answer := "
                          "await pulse; if answer is f64 value { power = "
                          "value } }Start :: (time: f64) { pulse = "
                          "(Charge(time)) }Charge :: (time: f64) -> (power: "
                          "f64) { yield; return(time * 2); } }";
    const char *qualified = "P :: behavior { pulse: task; power: f64 = 0;on "
                            "Impulse :: (amount: f64) { Start(amount); answer "
                            ":= await pulse; if answer is f64 value { power "
                            "= value } }Start :: (time: f64) { state.pulse "
                            "= Charge(time) }Charge :: (time: f64) -> "
                            "(power: f64) { yield; return(time * 2); } }";
    /* A nested callable's own `pulse` does not shadow the outer one. */
    const char
        *nested = "P :: behavior { pulse: task; power: f64 = 0;on "
                  "Impulse :: (amount: f64) { Start(amount); answer := await "
                  "pulse; if answer is f64 value { power = value } }Start "
                  ":: (time: f64) { Other :: (pulse: f64) { } pulse = "
                  "Charge(time) }Charge :: (time: f64) -> (power: f64) { "
                  "yield; return(time * 2); } }";
    /* A sibling block's local `pulse` does not shadow the state. */
    const char
        *sibling = "P :: behavior { pulse: task; power: f64 = 0;on "
                   "Impulse :: (amount: f64) { Start(amount); answer := "
                   "await "
                   "pulse; if answer is f64 value { power = value } }Start "
                   ":: (time: f64) { if (time > 99) { pulse := 1 } pulse = "
                   "Charge(time) }Charge :: (time: f64) -> (power: f64) "
                   "{ yield; return(time * 2); } }";
    /* A nested callee types the state for awaits analyzed after it. */
    const char
        *inner = "P :: behavior { pulse: task; power: f64 = 0;on "
                 "Impulse :: (amount: f64) { Start(amount) }Start :: (time: "
                 "f64) { Charge :: (time: f64) -> (power: f64) { "
                 "yield; return(time * 2); } pulse = Charge(time); answer := "
                 "await pulse; if answer is f64 value { power = value } } }";
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
    syntax = script_parse("P :: behavior { pulse: task; power: f64 = 0;Charge :: (time: f64) -> (power: f64) { yield; return(time) }Keep :: (t: task) { pulse = t }on Impulse :: (amount: f64) { pulse = Charge(amount); power = await pulse } }");
    analysis = script_analyze(syntax, &catalog);
    REQUIRE(!analysis->valid);
    /* So does a state write of another function's task, before or after
     * the await. */
    const char *writes[] = {
        "P :: behavior { pulse: task; power: f64 = 0;Charge :: (time: "
        "f64) -> (power: f64) { yield; return(time) }Other :: () -> "
        "(name: string) { yield; return(\"x\") }on Impulse :: (amount: f64) "
        "{ "
        "pulse = Charge(amount); power = await pulse }Swap :: () { "
        "set_variable<pulse>(Other()) } }",
        "P :: behavior { pulse: task; power: f64 = 0;Charge :: (time: "
        "f64) -> (power: f64) { yield; return(time) }Other :: () -> "
        "(name: string) { yield; return(\"x\") }Swap :: () { "
        "set_variable<pulse>(Other()) }on Impulse :: (amount: f64) { pulse "
        "= "
        "Charge(amount); power = await pulse } }"
    };
    for (int i = 0; i < 4; i++) {
        prepare();
        REQUIRE(
            !compile_pack(writes[i / 2], i % 2 ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE, &NATIVE_STATE_DEFINITION_PACK)
        );
        REQUIRE(
            state_native && syntax && !syntax->diagnostic_count && analysis &&
            !analysis->valid
        );
        bool incompatible = false;
        for (int d = 0; d < analysis->diagnostic_count; d++) {
            incompatible |= strstr(analysis->diagnostics[d].message, "Incompatible types: expected f64") !=
                            NULL;
        }
        REQUIRE(incompatible);
    }
    return true;
}
TEST_CASE(program_task_handles_belong_to_their_instance)
{
    const char *source = "P :: behavior { pulse: task; alive: f64 = "
                         "0;Charge :: (time: f64) -> (power: f64) { "
                         "yield; return(time) }on Impulse :: (amount: f64, "
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
    const char
        *source = "H :: behavior { n: f64 = 0;on Impulse :: (amount: f64, "
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
            "B :: behavior { n: f64 = 0\n"
            "Count :: () -> (shown: f64) { return 2 }\n"
            "Wait :: () { yield }\n"
            "action outer = Count()\n"
            "action inner = branch_if(Condition: outer.shown > 0)\n"
            "on Impulse :: (amount: f64, enabled: bool) { outer() }\n"
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
    const char *source = "V :: behavior { n: f64 = 0\n"
                         "m: f64 = 2\n"
                         "k: f64 = 0\n"
                         "who: entity\n"
                         "tiles: int[]\n"
                         "Approach :: (value: f64, from: f64) -> (r: "
                         "f64) { return value - from }\n"
                         "Step :: (left: f64) -> (r: f64) { while left > "
                         "0 { left -= 1 } return left }\n"
                         "Drain :: (left: f64) -> (r: f64) { while left "
                         "> 0 { left -= 1\n"
                         " yield } return left + 7 }\n"
                         "on Impulse :: (amount: f64) { split := 1.0\n"
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
    const char *
        source = "B :: behavior { seen: number = 0; kept: number = 0;Bump :: "
                 "(x: ^number) -> () { yield; x^ += 1; yield; x^ += 10; "
                 "}Keep :: (x: ^number) -> () { yield; x^ += 1; yield; kept "
                 "+= x^; }Relay :: (x: ^number) -> () { Keep(x); }Pair :: "
                 "(x: number) -> (a: number, b: number) { return(a: x * 1000 "
                 "+ 5, b: 0) }on Impulse :: (amount: f64) {if amount == 0 "
                 "{ v "
                 ":= 1.0; t := Bump(&v); v += 100; await t; seen = v; }else if "
                 "amount == 1 { w := 5.0; Keep(&w); }else if amount == 2 { i "
                 ":= 0.0; while i < 2 { u := i * 10; Keep(&u); i += 1; } }else "
                 "if amount == 3 { j := 0.0; while j < 2 { z := j * 100 + 2; "
                 "Relay(&z); j += 1; } }else { k := 0.0; while k < 2 { p := "
                 "Pair(k); Keep(&p.a); k += 1; } } } }";
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
    const char *source = "R :: behavior { n: f64 = 0; m: f64 = 0;Add :: "
                         "(x: ^f64) -> () { i := 0.0; while i < 1000 { x^ "
                         "+= 1; i += 1; } }on Impulse :: () { i := "
                         "0.0; while i < 1000 { n += 1; i += 1; } Add(&m); } }";
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
    const char *source = "B :: behavior { n: f64 = 0;Twice :: (x: f64) -> (v: "
                         "f64) { return x * 2 }on Impulse :: () { i := "
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
        *source = "B :: behavior { n: f64 = 0; m: f64 = 0;Add :: (x: ^"
                  "f64, v: f64) -> (r: f64) {scratch: f64 = 0; if "
                  "v > 0 { scratch = v }x^ += scratch; return x^ }Other :: (v: "
                  "f64) -> (r: f64) { return v * 10 }Count :: (v: "
                  "f64) -> (r: f64) {if v <= 0 { return 0 } return "
                  "Count(v - 1) + 1 }Outer :: (v: f64) -> (r: f64) { "
                  "return Count(v) + Other(v) }on Impulse :: () {n "
                  "+= Add(&m, 2); n += Add(&m, -1);n += Other(3); n += "
                  "Add(&m, "
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
        *source = "B :: behavior { n: f64 = 0; text: string = \"\";Text :: "
                  "() -> (r: string) { return managed_text() }Tiles :: (v: "
                  "f64) -> (r: f64[]) { return [v, v + 1] }Work :: "
                  "(pause: bool) -> (r: f64) {local: f64 = 0; if !pause "
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
    const char *
        source = "R :: behavior { Add :: (x: ^f64, y: ^f64) -> () "
                 "{ i := 0.0; while i < 1000 { x^ += 1; y^ += 1; i += 1; } }on "
                 "Impulse :: () { Add(&self.Position.x, "
                 "&self.Position.x); self.Position.y = 7; } }";
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
            "on Impulse :: (amount: f64, enabled: bool) { total = Check(2) "
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
                  "total: f64 = 0; saved_result: Value[] = []; "
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
                "Ordered :: system(p:^Position,v:^Velocity) { "
                "resumed:retain f64=0; event_clock:retain f64=0; "
                "queued_value:retain f64=0; finished:retain f64=0; "
                "on Impulse :: (amount: f64, enabled: bool) { "
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
            if (status != FLOW_OK) {
                const execution_program_failure *failure = execution_program_error(program);
                fprintf(
                    stderr,
                    "ordered system backend=%d mode=%d status=%d error=%s\n",
                    backend,
                    mode,
                    status,
                    failure ? failure->message : "none"
                );
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
            compile_module("Count :: system( p:^Position,v:^Velocity , if v.x>0) { reduce p.x+=v.x; }", backends[backend])
        );
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        REQUIRE(execution_program_prepare_system(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_memory_usage(program).query_rows == 3);
        int status = execution_program_run_system(program);
        if (status != FLOW_OK) {
            const execution_program_failure *failure = execution_program_error(program);
            fprintf(
                stderr,
                "count rows backend=%d status=%d error=%s\n",
                backend,
                status,
                failure ? failure->message : "none"
            );
        }
        REQUIRE(status == FLOW_OK);
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
TEST_CASE(program_descriptor_field_observers_compare_all_bytes)
{
    /* One entity and descriptor field; two tiny functions and two program
     * steps capped at 1000 instructions. The changed byte follows a NUL. */
    cleanup();
    world = ecs_world_new();
    static const type_field field = { .name = "text",
                                      .type = PIN_STR,
                                      .size = sizeof(const pin_string *),
                                      .type_name = "const char *",
                                      .text_descriptor = true };
    static const type_desc type = { .name = "test.program.text",
                                    .size = sizeof(const pin_string *),
                                    .fields = &field,
                                    .field_count = 1 };
    static const ecs_component_desc declared = { .id = 816,
                                                 .key = "test.program.text",
                                                 .name = "Text",
                                                 .element_size = sizeof(const pin_string *),
                                                 .type = &type };
    REQUIRE(ecs_world_register_component(world, &declared, NULL) == ECS_OK);
    const ecs_component_desc *component = ecs_world_component(world, declared.id);
    REQUIRE(component && component->layout_hash);
    ecs_entity entity = ecs_world_create_entity(world);
    const pin_string initial = { .bytes = "A\0B", .length = 3 };
    const pin_string *initial_field = &initial;
    REQUIRE(ecs_world_record_add(world, entity, component->id, &initial_field, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    pin_value text = { .type = PIN_STR, .type_name = "const char *" };
    pin_value receiver = { .type = PIN_HANDLE,
                           .type_name = "ecs.entity",
                           .u64 = ecs_entity_to_u64(entity) };
    owned_plan = execution_module_plan_new();
    owned_plan->id = 810;
    owned_plan->valid = true;
    behavior_function_builder *builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, &text, 1, NULL, 0, &text, 1);
    int input = behavior_function_builder_slot(builder, PIN_STR);
    behavior_function_builder_emit(
        builder,
        (
            behavior_function_instruction
        ){ .op = BF_ARGUMENT, .type = PIN_STR, .output = input, .value = text }
    );
    int bank = behavior_function_builder_values(builder, &text, 1);
    int address = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
    REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, address, BEHAVIOR_ADDRESS));
    behavior_function_builder_emit(
        builder,
        (
            behavior_function_instruction
        ){ .op = BF_HOST_SLOT, .type = BF_POINTER_TYPE, .output = address, .left = bank }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_WRITE,
                                         .type = PIN_STR,
                                         .left = input,
                                         .right = address,
                                         .offset = offsetof(pin_value, d) }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_STORE_VALUE, .left = bank, .target = 0 }
    );
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    behavior_function *body = behavior_function_builder_finish(builder, error, sizeof error);
    behavior_function_builder_free(builder);
    REQUIRE(body);
    apush(owned_plan->functions, ((execution_module_function){ .id = 814, .body = body }));
    owned_plan->function_count = 1;
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, NULL, 0, &receiver, 1, &text, 1);
    int target = behavior_function_builder_slot(builder, PIN_HANDLE);
    behavior_function_builder_emit(
        builder,
        (
            behavior_function_instruction
        ){ .op = BF_CONSTANT, .type = PIN_HANDLE, .output = target, .value = receiver }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_RESULT, .type = PIN_HANDLE, .left = target }
    );
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    behavior_function *subscription = behavior_function_builder_finish(builder, error, sizeof error);
    behavior_function_builder_free(builder);
    owned_plan->functions[0].subscription = subscription;
    REQUIRE(subscription);
    apush(owned_plan->states, ((execution_module_state){ .id = 811, .initial = text }));
    owned_plan->state_count = 1;
    execution_module_event_field *event_field = malloc(sizeof *event_field);
    REQUIRE(event_field);
    *event_field = (execution_module_event_field){ .name = "changed", .initial = text };
    apush(
        owned_plan->events,
        ((
            execution_module_event_declaration
        ){ .id = 812, .abi = 813, .name = "Changed", .fields = event_field, .field_count = 1 })
    );
    owned_plan->event_count = 1;
    owned_plan->functions[0] = (execution_module_function){
        .id = 814,
        .kind = EXECUTION_FUNCTION_HANDLER,
        .event = 812,
        .event_abi = 813,
        .body = body,
        .subscription = subscription,
        .observer = { .component = component->id,
                      .layout = component->layout_hash,
                      .field = 815,
                      .type = text }
    };
    module = execution_module_finish(&owned_plan, BEHAVIOR_BYTECODE, error, sizeof error);
    if (!module) {
        fprintf(stderr, "ECS string observer: %s\n", error);
    }
    REQUIRE(module);
    program = execution_program_new(module, world, NULL);
    REQUIRE(program);
    execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(entity));
    REQUIRE(instance);
    REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
    const pin_value *seen = execution_instance_state(instance, 811);
    REQUIRE(seen && !pin_text(*seen).length);
    const pin_string changed = { .bytes = "A\0C", .length = 3 };
    REQUIRE(
        ecs_component_write_field(
            world,
            entity,
            component->id,
            0,
            (pin_value){ .type = PIN_STR, .text = &changed }
        ) == ECS_OK
    );
    REQUIRE(execution_program_step(program, 1, 0, 1000) == FLOW_OK);
    seen = execution_instance_state(instance, 811);
    REQUIRE(seen && pin_text(*seen).length == 3 && !memcmp(pin_text(*seen).bytes, "A\0C", 3));
    return true;
}

void test_language_query_program()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(program_descriptor_field_observers_compare_all_bytes);
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
    RUN_TEST_CASE(program_query_predicates_call_bound_natives);
    pu_clear_setup();
}

/* Two fixed detached batches, two cells and one production payload collection.
 * The previous batch models nested dispatch without running a second scheduler. */
TEST_CASE(program_detached_event_batches_keep_managed_arguments)
{
    prepare();
    REQUIRE(compile("Keeper :: behavior { count: int = 0; }", BEHAVIOR_BYTECODE));
    pin_value arguments[2];
    for (int i = 0; i < 2; i++) {
        pin_pointer *cell = arena_alloc(&program->managed->storage, sizeof *cell);
        *cell = (pin_pointer){ .value = { .type = PIN_I32, .i = i + 7 },
                               .storage = &program->managed->storage };
        pin_arena_trace_pointer(&program->managed->storage, cell);
        arguments[i] = (pin_value){ .type = PIN_POINTER, .ptr = cell };
    }
    queued_event *outer = NULL, *inner = NULL;
    apush(outer, ((queued_event){ .arguments = &arguments[0], .count = 1 }));
    apush(inner, ((queued_event){ .arguments = &arguments[1], .count = 1 }));
    queued_event_batch previous = { .events = outer },
                       current = { .previous = &previous, .events = inner };
    program->dispatch_events = &current;
    arena_alloc(&program->managed->storage, 4096);
    program_collect_payloads(program, NULL);
    program->dispatch_events = NULL;
    afree(outer);
    afree(inner);
    REQUIRE(arena_contains(&program->managed->storage, arguments[0].ptr));
    REQUIRE(arena_contains(&program->managed->storage, arguments[1].ptr));
    REQUIRE(((pin_pointer *)arguments[0].ptr)->value.i == 7);
    REQUIRE(((pin_pointer *)arguments[1].ptr)->value.i == 8);
    behavior_managed_heap_collect(program->managed, NULL, NULL);
    REQUIRE(!arena_contains(&program->managed->storage, arguments[0].ptr));
    REQUIRE(!arena_contains(&program->managed->storage, arguments[1].ptr));
    return true;
}
/* One pointer state, two staged replacements, and four explicit quiescent
 * collections cover discard and commit while the original drops its root. */
TEST_CASE(program_staged_replacement_pins_preserved_pointers)
{
    prepare();
    REQUIRE(
        compile("Keeper :: behavior { saved: ^int?; position: Position = {x: 1, y: 2}; }", BEHAVIOR_BYTECODE)
    );
    ecs_entity owner = ecs_world_create_entity(world);
    uint64_t entity = ecs_entity_to_u64(owner), id = state_id("saved");
    execution_instance *instance = execution_program_attach(program, entity);
    REQUIRE(instance && id);
    /* State headers borrow their current state's arena, which an overwrite
     * may retire. Retain the nil header independently across both writes. */
    pin_value absent;
    REQUIRE(
        !pin_value_snapshot_to_arena(&parser_storage, &absent, execution_instance_state(instance, id))
    );
    for (int commit = 0; commit < 2; commit++) {
        behavior_managed_heap *heap = program->managed;
        pin_pointer *cell = arena_alloc(&heap->storage, sizeof *cell);
        *cell = (pin_pointer){ .value = { .type = PIN_I32, .i = 41 + commit },
                               .storage = &heap->storage };
        pin_arena_trace_pointer(&heap->storage, cell);
        pin_value pointer = absent;
        pointer.ptr = cell;
        REQUIRE(execution_instance_set_state(instance, id, pointer) == FLOW_OK);
        other_program = execution_program_prepare_replace(program, module, false);
        REQUIRE(other_program);
        REQUIRE(execution_instance_set_state(instance, id, absent) == FLOW_OK);
        behavior_managed_heap_collect(heap, NULL, NULL);
        REQUIRE(arena_contains(&heap->storage, cell));
        if (!commit) {
            execution_program_free(other_program);
            other_program = NULL;
            behavior_managed_heap_collect(heap, NULL, NULL);
            REQUIRE(!arena_contains(&heap->storage, cell));
            continue;
        }
        execution_program_commit_replace(program, other_program);
        other_program = NULL;
        /* Reacquire the preserved owner through the public API; internal
         * row lookup requires a complete query-variable tuple even for zero
         * declared query variables. */
        instance = execution_program_attach(program, entity);
        REQUIRE(instance && execution_instance_state(instance, id)->ptr == cell);
        const pin_value *position = execution_instance_state(instance, state_id("position"));
        REQUIRE(position && position->ptr);
        const program_position *native = position->ptr;
        REQUIRE(native->x == 1 && native->y == 2);
        arena_alloc(&program->managed->storage, 4096);
        program_collect_payloads(program, NULL);
        REQUIRE(arena_contains(cell->storage, cell) && cell->value.i == 42);
    }
    return true;
}
/* One entity on bytecode: a pointer preserved beside a native record in a
 * union is rooted alone while the replacement is staged, so the staging
 * succeeds and the target survives the commit and a forced collection. */
TEST_CASE(program_replacement_preserves_pointers_beside_inline_records)
{
    const char
        *source = "Keeper :: behavior { Slot :: union { Empty | "
                  "Full(position: Position, target: ^number) }; "
                  "held: Slot = Slot.Empty; seen: number = 0; "
                  "on Impulse :: (amount: f64, enabled: bool) { if enabled { "
                  "x := 5.0; at: Position = {x: 1, y: 2}; "
                  "held = Slot.Full(at, &x); } "
                  "if !enabled { if held is .Full(position, target) { "
                  "seen = target^; } } } }";
    prepare();
    REQUIRE(compile(source, BEHAVIOR_BYTECODE));
    uint64_t entity = ecs_entity_to_u64(ecs_world_create_entity(world));
    execution_instance *instance = execution_program_attach(program, entity);
    REQUIRE(instance);
    pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL, .b = true } };
    REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
    other_program = execution_program_prepare_replace(program, module, false);
    REQUIRE(other_program);
    execution_program_commit_replace(program, other_program);
    other_program = NULL;
    instance = execution_program_attach(program, entity);
    REQUIRE(instance);
    arena_alloc(&program->managed->storage, 4096);
    program_collect_payloads(program, NULL);
    args[1].b = false;
    REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
    REQUIRE(number_state(instance, "seen", 5));
    return true;
}
/* One entity on two backends. A handler suspends holding a union it read from
 * state, then the state is overwritten. The local owns a copy of the union, so
 * a forced collection still reaches its pointer, and a new cell allocated
 * afterwards does not take its place. */
TEST_CASE(program_suspended_union_reads_keep_pointer_targets)
{
    const char
        *source = "Keeper :: behavior { Slot :: union { Empty | Full(p: "
                  "^number) "
                  "}; "
                  "held: Slot = Slot.Empty; seen: number = 0; "
                  "on Impulse :: (amount: f64, enabled: bool) { "
                  "if amount == 0 { y := 3.0; held = Slot.Full(&y); } "
                  "if amount == 1 { h := held; await Delay(1); "
                  "if h is .Full(p) { seen = p^; } } "
                  "if amount == 2 { held = Slot.Empty; } "
                  "if amount == 3 { z := 7.0; held = Slot.Full(&z); } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        REQUIRE(instance);
        pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL } };
        for (int step = 0; step < 4; step++) {
            if (step == 3) {
                arena_alloc(&program->managed->storage, 4096);
                uint64_t marks = arena_collect_marks();
                program_collect_payloads(program, NULL);
                REQUIRE(arena_collect_marks() > marks);
            }
            args[0].d = step;
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        }
        REQUIRE(execution_instance_pending(instance) == 1);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "seen", 3));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}
/* One moving entity on two backends. A reducing system pauses at a breakpoint
 * while a local array holds the only pointer to a cell whose local went out of
 * scope. The paused activation's slots and payload are listed by no
 * collection, so a collection requested during the pause marks nothing, and
 * the cell is still there when the system resumes. */
TEST_CASE(program_paused_reducing_systems_keep_pointer_targets)
{
    const char *source = "Held :: system(p: ^Position, v: ^Velocity) { "
                         "kept: retain number = 0; finished: retain f64 = 0; "
                         "ps: ^number[] = []; if true { x := 3.0; ps = [&x]; } "
                         "reduce p.x += v.x; finished += 1; kept = ps[0]^; }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        ecs_entity entity = positioned(0, true);
        REQUIRE(compile(source, backends[b]));
        REQUIRE(execution_program_pump(program, 0, 0, 1000) == FLOW_OK);
        execution_instance *instance = execution_program_instance(program, ecs_entity_to_u64(entity));
        REQUIRE(instance);
        debugger = execution_program_inspector(program);
        REQUIRE(debugger);
        uint64_t function = 0, point = 0;
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
        REQUIRE(execution_program_update(program, 1, .5, 1000) == FLOW_PAUSED_RESULT);
        arena_alloc(&program->managed->storage, 4096);
        uint64_t marks = arena_collect_marks();
        program_collect_payloads(program, NULL);
        REQUIRE(arena_collect_marks() == marks);
        REQUIRE(
            execution_inspector_breakpoint(debugger, execution_module_id(module), function, point, false)
        );
        REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_RESUME) == FLOW_OK);
        REQUIRE(execution_program_update(program, 2, .5, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "finished", 1));
        REQUIRE(number_state(instance, "kept", 3));
    }
    return true;
}
/* One entity, one addressed local and one delayed continuation on two backends;
 * each backend dispatches once and advances exactly two scheduled steps. */
TEST_CASE(program_captured_addressed_local_preserves_pointer_alias)
{
    const char *source = "P :: behavior { seen: f64 = 0; Parent :: () { "
                         "x := 3.0; p := &x; action later = Delay(1); later(); "
                         "after later { x += 1; seen = p^; } } "
                         "on Impulse :: () { Parent(); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        REQUIRE(instance);
        const pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "seen", 0));
        int status = execution_program_update(program, 1, 0, 1000);
        if (status != FLOW_OK) {
            const execution_program_failure *failure = execution_program_error(program);
            fprintf(
                stderr,
                "pointer capture backend=%d status=%d kind=%d at %d:%d: %s\n",
                backends[b],
                status,
                failure ? (int)failure->kind : -1,
                failure ? failure->line : 0,
                failure ? failure->column : 0,
                failure ? failure->message : "No program failure detail"
            );
        }
        REQUIRE(status == FLOW_OK);
        REQUIRE(execution_program_update(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "seen", 4));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}
/* One entity on two backends: a helper returns a pointer array built by the
 * borrowing append kernel, a delayed continuation holds it across one forced
 * safe-point collection, then reads and writes both cells. */
TEST_CASE(program_borrowed_pointer_array_results_survive_collection)
{
    const char
        *source = "P :: behavior { seen: f64 = 0; "
                  "Make :: () -> (r: ^number[]) { x := 3.0; y := 4.0; "
                  "ps: ^number[] = [&x]; ps.append(&y); return ps } "
                  "Parent :: () { ps := Make(); action later = Delay(1); "
                  "later(); after later { ps[1]^ += 1; "
                  "if length(ps) == 2 { seen = ps[0]^ * 10 + ps[1]^ } } } "
                  "on Impulse :: () { Parent(); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_pack(source, backends[b], &NATIVE_ARRAY_DEFINITION_PACK));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        REQUIRE(instance);
        const pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_instance_pending(instance) == 1);
        arena_alloc(&program->managed->storage, 4096);
        uint64_t marks = arena_collect_marks();
        program_collect_payloads(program, NULL);
        REQUIRE(arena_collect_marks() > marks);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "seen", 35));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}
/* Emitted host calls whose descriptor carries kernel's production callback
 * and the borrowing flag. Publication copies descriptors, so this pins the
 * callback rather than the descriptor address. */
static int borrowing_calls(const node_host_call *kernel)
{
    int count = 0;
    for (int f = 0; f < execution_module_function_count(module); f++) {
        const behavior_function *function = execution_module_function_at(module, f)
                                                .body;
        for (int pc = 0; pc < acount(function->instructions); pc++) {
            const behavior_function_instruction *i = &function->instructions[pc];
            if (i->op != BF_HOST_CALL) {
                continue;
            }
            const node_host_call *call = function->host_calls[i->target].call;
            count += call->value == kernel->value && call->borrows;
        }
    }
    return count;
}
/* The escaped array is the fresh snapshot returned by the borrowing slice and
 * reverse kernels, not one built by the intrinsic append. A delayed
 * continuation holds it across one forced safe-point collection, then writes
 * through one cell and reads both. */
TEST_CASE(program_borrowing_kernel_results_survive_collection)
{
    const char *
        source = "P :: behavior { seen: f64 = 0; "
                 "Make :: () -> (r: ^number[]) { w := 1.0; x := 3.0; y := 4.0; "
                 "ps: ^number[] = [&w, &x, &y]; return reverse(ps.slice(1, 2)) "
                 "} "
                 "Parent :: () { ps := Make(); action later = Delay(1); "
                 "later(); after later { ps[1]^ += 1; "
                 "if length(ps) == 2 { seen = ps[0]^ * 10 + ps[1]^ } } } "
                 "on Impulse :: () { Parent(); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_pack(source, backends[b], &NATIVE_ARRAY_DEFINITION_PACK));
        REQUIRE(borrowing_calls(&NATIVE_ARRAY_slice_CALL) == 1);
        REQUIRE(borrowing_calls(&NATIVE_ARRAY_reverse_CALL) == 1);
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        REQUIRE(instance);
        const pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_instance_pending(instance) == 1);
        arena_alloc(&program->managed->storage, 4096);
        uint64_t marks = arena_collect_marks();
        program_collect_payloads(program, NULL);
        REQUIRE(arena_collect_marks() > marks);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "seen", 44));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}
/* Two independent program owners, one cell and one single-element array.
 * Every foreign ingress must reject before queueing or changing state. */
TEST_CASE(program_public_ingress_rejects_foreign_managed_pointers)
{
    prepare();
    REQUIRE(compile("Keeper :: behavior { saved: ^int?; }", BEHAVIOR_BYTECODE));
    other_program = execution_program_new(module, world, NULL);
    REQUIRE(other_program);
    ecs_entity owner = ecs_world_create_entity(world);
    uint64_t entity = ecs_entity_to_u64(owner), id = state_id("saved");
    execution_instance *local = execution_program_attach(program, entity),
                       *foreign = execution_program_attach(other_program, entity);
    REQUIRE(local && foreign && id);
    pin_value pointer;
    REQUIRE(
        !pin_value_snapshot_to_arena(&parser_storage, &pointer, execution_instance_state(local, id))
    );
    pin_pointer *cell = arena_alloc(&program->managed->storage, sizeof *cell);
    *cell = (pin_pointer){ .value = { .type = PIN_I32, .i = 17 },
                           .storage = &program->managed->storage };
    pin_arena_trace_pointer(&program->managed->storage, cell);
    pointer.ptr = cell;
    REQUIRE(execution_instance_set_state(local, id, pointer) == FLOW_OK);
    REQUIRE(execution_instance_set_state(foreign, id, pointer) == FLOW_ERR_ARGUMENT);
    REQUIRE(!execution_instance_state(foreign, id)->ptr);
    pin_array items = { .element_type = PIN_POINTER,
                        .type_name = pointer.type_name,
                        .count = 1,
                        .capacity = 1,
                        .items = &pointer };
    pin_value nested = { .type = PIN_POINTER,
                         .type_name = pointer.type_name,
                         .container = PIN_CONTAINER_ARRAY,
                         .ptr = &items };
    REQUIRE(execution_program_emit(other_program, 11, entity, &nested, 1) == FLOW_ERR_ARGUMENT);
    REQUIRE(
        execution_program_dispatch(other_program, 11, entity, &nested, 1, 0, 0, 100) == FLOW_ERR_ARGUMENT
    );
    REQUIRE(execution_instance_dispatch(foreign, 11, &nested, 1, 0, 0, 100) == FLOW_ERR_ARGUMENT);
    REQUIRE(
        execution_program_field_changed(other_program, entity, 501, 0, nested, 0, 0, 100) == FLOW_ERR_ARGUMENT
    );
    REQUIRE(!acount(other_program->events) && !acount(other_program->field_edges));
    REQUIRE(!execution_instance_state(foreign, id)->ptr && cell->value.i == 17);
    return true;
}
static int managed_heap_count()
{
    int count = 0;
    for (const behavior_managed_heap *heap = program->managed; heap;
         heap = heap->previous) {
        count++;
    }
    return count;
}
/* One entity on bytecode. The program keeps one heap handle across reloads,
 * and each commit frees the candidate's heap once it holds no cell, without
 * waiting for a collection. */
TEST_CASE(program_replacement_releases_empty_retired_heaps)
{
    const char
        *source = "Keeper :: behavior { saved: ^f64?; seen: f64 = 0; "
                  "on Impulse :: (amount: f64, enabled: bool) { "
                  "if enabled { x := amount; saved = &x; } "
                  "if !enabled { if saved != nil { seen = saved^; } } } }";
    prepare();
    REQUIRE(compile(source, BEHAVIOR_BYTECODE));
    uint64_t entity = ecs_entity_to_u64(ecs_world_create_entity(world));
    execution_instance *instance = execution_program_attach(program, entity);
    REQUIRE(instance);
    pin_value args[] = { { .type = PIN_F64, .d = 5 }, { .type = PIN_BOOL, .b = true } };
    REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
    behavior_managed_heap *heap = execution_program_managed_heap(program);
    for (int reload = 0; reload < 8; reload++) {
        REQUIRE(execution_program_replace(program, module, false) == FLOW_OK);
        REQUIRE(execution_program_managed_heap(program) == heap && managed_heap_count() == 1);
    }
    instance = execution_program_attach(program, entity);
    REQUIRE(instance);
    args[1].b = false;
    REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
    REQUIRE(number_state(instance, "seen", 5));
    REQUIRE(execution_program_replace(program, module, true) == FLOW_OK);
    REQUIRE(execution_program_managed_heap(program) == heap && managed_heap_count() == 1);
    return true;
}
static bool replace_library(const char *source)
{
    execution_module_free(prior_module);
    prior_module = module;
    module = NULL;
    script_analysis_free(analysis);
    analysis = NULL;
    script_syntax_free(syntax);
    syntax = NULL;
    return compile_module(source, BEHAVIOR_BYTECODE);
}
/* One library owner on bytecode. A reload that keeps Actor's layout preserves
 * a pointer to an Actor. One that adds a field still applies, and resets each
 * state that reaches an old Actor: at preparation, or at commit when the
 * running program stores one in a kept cell after preparation. Other state
 * keeps its cells. */
TEST_CASE(program_replacement_resets_state_reaching_changed_pointee_layouts)
{
    const char
        *same = "Store :: library { Actor :: struct { hp: f64; } "
                "saved: ^Actor?; slot: ^(^Actor?)?; kept: ^int?; }",
        *added = "Store :: library { Actor :: struct { hp: f64; armor: f64; } "
                 "saved: ^Actor?; slot: ^(^Actor?)?; kept: ^int?; }";
    prepare();
    REQUIRE(compile(same, BEHAVIOR_BYTECODE));
    uint64_t entity = ecs_entity_to_u64(ecs_world_create_entity(world));
    uint64_t saved = state_id("saved"), slot = state_id("slot"),
             kept = state_id("kept");
    execution_instance *instance = execution_program_attach(program, entity);
    REQUIRE(instance && saved && slot && kept);
    const pin_record *actor = NULL;
    for (int i = 0; i < module->plan.type_count; i++) {
        if (module->plan.types[i].record &&
            !strcmp(module->plan.types[i].name, "Store.Actor")) {
            actor = module->plan.types[i].record;
        }
    }
    REQUIRE(actor && actor->type.size == sizeof(double));
    arena *heap = &program->managed->storage;
    double hp = 5;
    pin_value data = { .type = PIN_STRUCT,
                       .type_name = actor->type.name,
                       .record = actor,
                       .ptr = &hp };
    REQUIRE(!pin_value_snapshot_to_arena(heap, &data, &data));
    /* Pointer headers are copied from each state's initial nil. */
    pin_value absent, outer, number;
    REQUIRE(
        !pin_value_snapshot_to_arena(&parser_storage, &absent, execution_instance_state(instance, saved))
    );
    REQUIRE(
        !pin_value_snapshot_to_arena(&parser_storage, &outer, execution_instance_state(instance, slot))
    );
    REQUIRE(
        !pin_value_snapshot_to_arena(&parser_storage, &number, execution_instance_state(instance, kept))
    );
    pin_value target = absent;
    target.ptr = managed_cell(heap, data);
    pin_pointer *inner = managed_cell(heap, absent);
    outer.ptr = inner;
    number.ptr = managed_cell(heap, (pin_value){ .type = PIN_I32, .i = 7 });
    REQUIRE(execution_instance_set_state(instance, saved, target) == FLOW_OK);
    REQUIRE(execution_instance_set_state(instance, slot, outer) == FLOW_OK);
    REQUIRE(execution_instance_set_state(instance, kept, number) == FLOW_OK);
    REQUIRE(replace_library(same));
    REQUIRE(execution_program_replace(program, module, false) == FLOW_OK);
    instance = execution_program_attach(program, entity);
    REQUIRE(instance && execution_instance_state(instance, saved)->ptr == target.ptr);
    REQUIRE(replace_library(added));
    REQUIRE(execution_program_replace(program, module, false) == FLOW_OK);
    REQUIRE(execution_program_module(program) == module);
    instance = execution_program_attach(program, entity);
    REQUIRE(instance && !execution_instance_state(instance, saved)->ptr);
    REQUIRE(execution_instance_state(instance, slot)->ptr == inner);
    REQUIRE(execution_instance_state(instance, kept)->ptr == number.ptr);
    REQUIRE(replace_library(added));
    other_program = execution_program_prepare_replace(program, module, false);
    REQUIRE(other_program);
    inner->value = target;
    execution_program_commit_replace(program, other_program);
    other_program = NULL;
    REQUIRE(execution_program_module(program) == module);
    instance = execution_program_attach(program, entity);
    REQUIRE(instance && !execution_instance_state(instance, slot)->ptr);
    REQUIRE(execution_instance_state(instance, kept)->ptr == number.ptr);
    return true;
}
/* One entity on two backends. Items of an array behind a pointer, and of an
 * addressed local, are read and written through the cell: the copies counted
 * stay near one array, not one per access. A copy taken through the pointer
 * keeps its items when the cell changes in place, and an index that empties
 * the cell still reads the array as it was before the index ran. */
TEST_CASE(program_managed_array_items_copy_alone)
{
    const char *source =
        "Keeper :: behavior { total: f64 = 0; copied: f64 = 0; first: f64 = 0; "
        "Clear :: (target: ^(f64[])) -> int { target^ = []; return 0; } "
        "on Impulse :: (amount: f64, enabled: bool) { "
        "xs: f64[] = []; for i in 0..<256 { xs.append(amount); } "
        "ys := xs; p := &ys; sum: f64 = 0; "
        "for i in 0..<256 { p^[i] = amount + 1; sum += p^[i] + ys[i]; } "
        "zs := p^; p^[0] = 9; copied = zs[0] + ys[0]; total = sum; "
        "ws: f64[] = [7]; q := &ws; first = q^[Clear(q)]; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_pack(source, backends[b], &NATIVE_ARRAY_DEFINITION_PACK));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        REQUIRE(instance);
        pin_value args[] = { { .type = PIN_F64, .d = 1 }, { .type = PIN_BOOL } };
        pin_copy_scope copies = { 0 };
        pin_copy_scope_begin(&copies);
        int status = execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1u << 20);
        pin_copy_scope_end(&copies);
        REQUIRE(status == FLOW_OK);
        REQUIRE(number_state(instance, "total", 1024) && number_state(instance, "copied", 11));
        REQUIRE(number_state(instance, "first", 7));
        uint64_t moved = copies.counts.value_buffer_bytes +
                         copies.counts.value_header_moves * sizeof(pin_value);
        REQUIRE(!copies.counts.overflow && moved < 64 * 256 * sizeof(pin_value));
    }
    return true;
}

static const authoring_package *call_package(
    int index,
    int previous,
    const char *source,
    const char *dependency,
    behavior_backend backend
)
{
    if (!state_native) {
        char error[256];
        const native_definition_pack packs[] = { CORE_DEFINITION_PACK };
        state_native = native_capabilities_from_definition_packs(NULL, 0, packs, 1, error, sizeof error);
        assert(state_native);
    }
    source_package_input files[] = {
        { "Main.chibi", source, strlen(source) },
        { "counter/Counter.chibi", dependency, dependency ? strlen(dependency) : 0 },
    };
    source_package_snapshot_options options = { .project_root = "/program-call",
                                                .files = files,
                                                .file_count = dependency ? 2 : 1 };
    source_package_snapshot *snapshot = source_package_snapshot_new(&options);
    script_lower_bindings bindings = authoring_native_bindings(state_native);
    call_packages[index] = authoring_packages_prepare(
        snapshot,
        native_capabilities_catalog(state_native),
        &bindings,
        backend,
        previous >= 0 ? call_packages[previous] : NULL
    );
    if (!authoring_packages_valid(call_packages[index])) {
        for (int i = 0; i < authoring_packages_count(call_packages[index]); i++) {
            const authoring_package *package = authoring_packages_at(call_packages[index], i);
            for (int j = 0; j < package->diagnostic_count; j++) {
                fprintf(stderr, "program package: %s\n", package->diagnostics[j].message);
            }
        }
        return NULL;
    }
    for (int i = 0; i < authoring_packages_count(call_packages[index]); i++) {
        const authoring_package *package = authoring_packages_at(call_packages[index], i);
        if (!strcmp(package->identity, "project:")) {
            return package;
        }
    }
    return NULL;
}
static uint64_t call_id(const authoring_package *package, const char *name)
{
    for (int i = 0; i < package->analysis->symbol_count; i++) {
        const script_symbol *symbol = &package->analysis->symbols[i];
        if (symbol->kind == SCRIPT_FUNCTION &&
            symbol->scope == package->analysis->package_scope &&
            !strcmp(symbol->name, name)) {
            return symbol->id;
        }
    }
    return 0;
}
/* Ordinary package functions run with their own instance and import owners,
 * without a scene or world. A reload keeps both retained counters, converts
 * one's type, ignores its new default and resets invocation locals. */
TEST_CASE(program_calls_keep_package_and_import_state_across_replacement)
{
    const char
        *before = "package app\nimport counter \"project:counter\"\n"
                  "Main :: () -> int { count: retain int = 0; count += 1; "
                  "fresh := 0; fresh += 1; return count * 100 + counter.Next() "
                  "* 10 + fresh; }\n",
        *after = "package app\nimport counter \"project:counter\"\n"
                 "Main :: () -> int { count: retain f64 = 100; count += 1; "
                 "fresh := 0; fresh += 1; return int(count) * 1000 + "
                 "counter.Next() * 10 + fresh; }\n",
        *dependency = "package counter\npub Next :: () -> int { calls: retain "
                      "int = 0; calls += 1; return calls; }\n",
        *changed = "package counter\npub Next :: () -> int { calls: retain int "
                   "= 50; calls += 2; return calls; }\n";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        const authoring_package *original = call_package(0, -1, before, dependency, backends[b]);
        REQUIRE(original);
        program = execution_program_new(original->module, NULL, NULL);
        REQUIRE(program);
        execution_instance *instance = execution_program_attach(program, 1);
        uint64_t main = call_id(original, "Main");
        REQUIRE(instance && main);
        int result = -1;
        REQUIRE(execution_instance_call(instance, main, &result, 0, 0, 1000) == FLOW_OK);
        REQUIRE(result == 111);
        REQUIRE(execution_instance_call(instance, main, &result, 0, 0, 1000) == FLOW_OK);
        REQUIRE(result == 221 && execution_instance_pending(instance) == 0);
        const authoring_package *next = call_package(1, 0, after, changed, backends[b]);
        REQUIRE(next && call_id(next, "Main") == main);
        other_program = execution_program_prepare_replace(program, next->module, false);
        REQUIRE(other_program);
        execution_program_commit_replace(program, other_program);
        other_program = NULL;
        instance = execution_program_instance(program, 1);
        REQUIRE(instance);
        REQUIRE(execution_instance_call(instance, main, &result, 0, 0, 1000) == FLOW_OK);
        REQUIRE(result == 3041 && execution_instance_pending(instance) == 0);
        REQUIRE(execution_program_replace(program, next->module, true) == FLOW_OK);
        instance = execution_program_instance(program, 1);
        REQUIRE(instance);
        REQUIRE(execution_instance_call(instance, main, &result, 0, 0, 1000) == FLOW_OK);
        REQUIRE(result == 101521);
    }
    return true;
}
/* The synchronous call boundary never schedules unsupported work. Rejected
 * calls leave the result alone and the instance ready for another call. */
TEST_CASE(program_calls_reject_signatures_waits_and_tasks_without_pending_work)
{
    const char *source = "package app\n"
                         "Main :: () -> int { return 7; }\n"
                         "Empty :: () {}\n"
                         "Argument :: (value: int) -> int { return value; }\n"
                         "Text :: () -> string { return \"wrong\"; }\n"
                         "Wait :: () { await delay(1); }\n"
                         "Task :: () { Wait(); }\n"
                         "Fails :: () { assert(false, \"call failure\"); }\n";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        const authoring_package *package = call_package(0, -1, source, NULL, backends[b]);
        REQUIRE(package);
        program = execution_program_new(package->module, NULL, NULL);
        REQUIRE(program);
        execution_instance *instance = execution_program_attach(program, 1);
        REQUIRE(instance);
        int result = -1;
        REQUIRE(
            execution_instance_call(instance, call_id(package, "Empty"), &result, 0, 0, 1000) == FLOW_OK
        );
        REQUIRE(result == 0);
        const char *invalid[] = { "Argument", "Text", "Absent" };
        for (int i = 0; i < 3; i++) {
            result = -1;
            REQUIRE(
                execution_instance_call(instance, call_id(package, invalid[i]), &result, 0, 0, 1000) ==
                FLOW_ERR_ARGUMENT
            );
            REQUIRE(result == -1 && execution_instance_pending(instance) == 0);
        }
        const char *rejected[] = { "Wait", "Task", "Fails" };
        const char *messages[] = { "waiting", "task", "call failure" };
        for (int i = 0; i < 3; i++) {
            execution_program_clear_error(program);
            result = -1;
            REQUIRE(
                execution_instance_call(instance, call_id(package, rejected[i]), &result, 0, 0, 1000) < 0
            );
            const execution_program_failure *failure = execution_program_error(program);
            REQUIRE(failure && strstr(failure->message, messages[i]));
            REQUIRE(result == -1 && execution_instance_pending(instance) == 0);
            REQUIRE(
                execution_instance_call(instance, call_id(package, "Main"), &result, 0, 0, 1000) == FLOW_OK
            );
            REQUIRE(result == 7);
        }
    }
    return true;
}

void test_language_program()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(program_calls_keep_package_and_import_state_across_replacement);
    RUN_TEST_CASE(program_calls_reject_signatures_waits_and_tasks_without_pending_work);
    RUN_TEST_CASE(program_captured_addressed_local_preserves_pointer_alias);
    RUN_TEST_CASE(program_borrowed_pointer_array_results_survive_collection);
    RUN_TEST_CASE(program_borrowing_kernel_results_survive_collection);
    RUN_TEST_CASE(program_detached_event_batches_keep_managed_arguments);
    RUN_TEST_CASE(program_staged_replacement_pins_preserved_pointers);
    RUN_TEST_CASE(program_replacement_preserves_pointers_beside_inline_records);
    RUN_TEST_CASE(program_suspended_union_reads_keep_pointer_targets);
    RUN_TEST_CASE(program_paused_reducing_systems_keep_pointer_targets);
    RUN_TEST_CASE(program_public_ingress_rejects_foreign_managed_pointers);
    RUN_TEST_CASE(program_replacement_releases_empty_retired_heaps);
    RUN_TEST_CASE(program_replacement_resets_state_reaching_changed_pointee_layouts);
    RUN_TEST_CASE(program_managed_array_items_copy_alone);
    RUN_TEST_CASE(program_descriptor_field_observers_compare_all_bytes);
    RUN_TEST_CASE(program_reduction_updates_preserve_clock_and_event_order);
    RUN_TEST_CASE(program_reduction_query_rows_count_filtered_candidates);
    RUN_TEST_CASE(program_pattern_bindings_share_and_spellings);
    RUN_TEST_CASE(program_switches_use_narrowed_union_tags);
    RUN_TEST_CASE(program_general_unions_construct_match_and_extract);
    RUN_TEST_CASE(program_union_arrays_keep_owned_writable_values);
    RUN_TEST_CASE(program_failed_call_stacks_unwind_at_once);
    RUN_TEST_CASE(program_module_finish_preserves_failure_and_consumes_success);
    RUN_TEST_CASE(program_typed_event_changes_state_on_vm_and_sljit);
    RUN_TEST_CASE(program_integer_reports_are_taken_once_per_site_until_reset);
    RUN_TEST_CASE(program_integer_reports_keep_sites_apart_under_inspection);
    RUN_TEST_CASE(program_prepared_integer_reports_include_imported_functions);
    RUN_TEST_CASE(program_handlers_name_payload_fields);
    RUN_TEST_CASE(program_occurrences_survive_whitespace_and_follow_the_source);
    RUN_TEST_CASE(program_nested_typed_records_preserve_explicit_labels);
    RUN_TEST_CASE(program_int_to_float_compare_and_cast_run_on_vm_and_sljit);
    RUN_TEST_CASE(program_systems_name_their_step);
    RUN_TEST_CASE(program_query_owner_snapshots_preserve_parallel_effects);
    RUN_TEST_CASE(program_query_projected_component_pointers_keep_serial_effects);
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
    RUN_TEST_CASE(program_query_predicates_call_bound_natives);
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
    RUN_TEST_CASE(program_integer_conversions_clamp_or_wrap);
    RUN_TEST_CASE(program_loops_count_half_open_and_in_reverse);
    RUN_TEST_CASE(program_char_conversions_and_arithmetic_stay_unicode_scalars);
    RUN_TEST_CASE(program_char_compares_with_ints_and_steps_below_surrogates);
    RUN_TEST_CASE(program_native_char_results_reject_invalid_scalars);
    RUN_TEST_CASE(program_native_char_results_preserve_valid_scalars_and_ints);
    RUN_TEST_CASE(program_fixed_native_char_outputs_are_checked_before_use);
    RUN_TEST_CASE(program_fixed_native_char_outputs_preserve_valid_and_optional_values);
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
    RUN_TEST_CASE(program_bound_task_checks_admission_before_its_body);
    RUN_TEST_CASE(program_effect_free_bound_task_uses_the_owning_program_world);
    RUN_TEST_CASE(program_bound_task_cancels_pending_children_and_preserves_unbound_pointer_users);
    RUN_TEST_CASE(program_bound_task_does_not_consume_ready_external_results_after_removal);
    RUN_TEST_CASE(program_bound_task_stops_when_an_inline_outcome_invalidates_its_target);
    RUN_TEST_CASE(program_failing_inline_outcome_detaches_its_completed_request);
    RUN_TEST_CASE(program_bound_task_cannot_succeed_after_its_last_expression_removes_the_target);
    RUN_TEST_CASE(program_bound_child_keeps_its_own_targets_and_follows_parent_cancellation);
    RUN_TEST_CASE(program_completed_bound_parent_leaves_only_dependent_children_bound);
    RUN_TEST_CASE(program_bound_task_resources_detach_in_each_destruction_order);
    RUN_TEST_CASE(program_task_infers_component_dependencies);
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
    RUN_TEST_CASE(program_task_await_shares_large_payload);
    RUN_TEST_CASE(program_task_collection_indexes_handles_across_many_owners);
    RUN_TEST_CASE(program_task_collection_follows_long_pointer_chains);
    RUN_TEST_CASE(program_task_results_keep_nested_task_handles_alive);
    RUN_TEST_CASE(program_query_staging_roots_keep_completed_tasks_alive);
    RUN_TEST_CASE(program_prepared_tasks_preserve_cancellation_union_and_deadlines);
    RUN_TEST_CASE(program_task_stops_with_owner_and_does_not_block_events);
    RUN_TEST_CASE(program_many_tasks_collect_in_linear_mark_work);
    RUN_TEST_CASE(program_task_semantics_reject_invalid_uses);
    RUN_TEST_CASE(program_handler_payloads_do_not_hide_parameters);
    RUN_TEST_CASE(program_nested_waiting_functions_leave_parents_running);
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
    RUN_TEST_CASE(program_nested_forwarding_keeps_pointer_scoped);
    RUN_TEST_CASE(program_typed_and_parenthesized_forwarding_keep_pointer_scoped);
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

void test_language_native_char_results()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(program_char_conversions_and_arithmetic_stay_unicode_scalars);
    RUN_TEST_CASE(program_char_compares_with_ints_and_steps_below_surrogates);
    RUN_TEST_CASE(program_native_char_results_reject_invalid_scalars);
    RUN_TEST_CASE(program_native_char_results_preserve_valid_scalars_and_ints);
    RUN_TEST_CASE(program_fixed_native_char_outputs_are_checked_before_use);
    RUN_TEST_CASE(program_fixed_native_char_outputs_preserve_valid_and_optional_values);
    pu_clear_setup();
}
