#include "assets/input_map.h"
#include "assets/project.h"
#include "assets/project_migrate.h"
#include "authoring/native_bindings.h"
#include "authoring/source_catalog.h"
#include "authoring/source_minimize.h"
#include "backend.h"
#include "ecs/ecs.h"
#include "execution/delay.h"
#include "execution/function_builder.h"
#include "execution/inspection.h"
#include "execution/integer_report.h"
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
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/text.h"

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
/* Historical migration cases assert their intermediate spelling, then run
 * it through every later source step: version 23 declarations, 24
 * retention, 25 syntax, 26 conversions, 27 parameter names, 28 function
 * bodies and 29 handler payloads, with the test catalog's Impulse declared. */
static const char *const
    IMPULSE_DECLARATION = "version 3\nname \"Impulse\"\ndomain "
                          "\"event\"\n(array)variables {\n"
                          "    - id=1 name=\"amount\" type=\"double\"\n    - "
                          "id=2 name=\"enabled\" type=\"bool\"\n}\n";
static int migrate_step(int step, const char *source, char **migrated, char *error, size_t size)
{
    switch (step) {
        case 0:
            return chibi_migrate_declarations(source, migrated, error, size);
        case 1: return chibi_migrate_retention(source, migrated, error, size);
        case 2: return chibi_migrate_syntax(source, migrated, error, size);
        case 3: return chibi_migrate_conversions(source, migrated, error, size);
        case 4: return chibi_migrate_entry(source, migrated, error, size);
        case 5: return chibi_migrate_after_entry(source, migrated, error, size);
        default:
            return chibi_migrate_handlers(source, &IMPULSE_DECLARATION, 1, migrated, error, size);
    }
}
static char *current_source(const char *source)
{
    char error[192], *current = xstrdup(source);
    for (int step = 0; step < 7; step++) {
        char *migrated = NULL;
        if (migrate_step(step, current, &migrated, error, sizeof error)) {
            fprintf(stderr, "migration: %s\n", error);
            free(current);
            return NULL;
        }
        if (migrated) {
            free(current);
            current = migrated;
        }
    }
    return current;
}
static bool compile_migrated(const char *source, behavior_backend backend)
{
    char *current = current_source(source);
    bool valid = current && compile(current, backend);
    free(current);
    return valid;
}
static bool declarations_equal(const char *actual, const char *historical)
{
    char *current = current_source(historical);
    bool equal = current && !strcmp(actual, current);
    free(current);
    return equal;
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
                         "    small: byte = 0;\n"
                         "    quotient: int = 5;\n"
                         "    zero: int = 0;\n"
                         "    letter: char = 'a';\n"
                         "\n"
                         "    on Impulse :: (amount: double, enabled: bool) {\n"
                         "        total *= 3;\n"
                         "        small = byte(amount);\n"
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
                         "    w: byte = 0;\n"
                         "    on Impulse :: (amount: double, enabled: bool) {\n"
                         "        y = x + 1;\n"
                         "        z = x * 2;\n"
                         "        w = byte(wrap: x * 4);\n"
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
    const int columns[] = { 13, 13, 24 }, starts[] = { 160, 179, 209 };
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
                                 "    Work :: () -> (local_count: double, "
                                 "position_x: double) {\n"
                                 "        hits *= 3;\n"
                                 "        return(double(hits), 0);\n"
                                 "    }\n"
                                 "}\n";
    const char
        *owner_source = "Owner :: behavior {\n"
                        "    local_count: double = 0;\n"
                        "    small: byte = 0;\n"
                        "    on Impulse :: (amount: double, enabled: bool) {\n"
                        "        values := await library_work();\n"
                        "        local_count = values.local_count;\n"
                        "        small = byte(amount);\n"
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
                REQUIRE(overflow->column == 9 && overflow->start == 117 && overflow->end == 127);
                REQUIRE(clamp->column == 17 && clamp->start == 220 && clamp->end == 232);
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
    return value && value->type == PIN_STR &&
           !strcmp(pin_text(*value).bytes, expected);
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
        REQUIRE(
            !strcmp(pin_text(*(box->value)).bytes, "small") &&
            arena_storage_bytes(&box->storage) < 512
        );
        REQUIRE(execution_program_update(program, 4, 0, 10000) == FLOW_OK);
        REQUIRE(
            !strcmp(pin_text(*(box->value)).bytes, "") &&
            arena_storage_bytes(&box->storage) < 512
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
        *source = "Handles :: behavior { seen: double = 0;Result :: () -> "
                  "(value: double) { yield; return 7; }Use :: (jobs: ref "
                  "task(double)[]) { yield; jobs = jobs; yield;answer := await "
                  "jobs[0]; if answer is double n { seen = n; }jobs = []; "
                  "yield; }on Impulse :: () { jobs: task(double)[] "
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
                  "  mixed: int64 = 0; narrow: byte = 7;\n"
                  "  same: bool = true; below: bool = false; small: int = 0;\n"
                  "  on Impulse :: (enabled: bool) {\n"
                  "    wrapped = byte(wrap: wide); clamped = byte(-wide);\n"
                  "    mixed = wide + int(uint(7));\n"
                  "    same = wide == int(uint(0)); below = wide < "
                  "int(uint(7));\n"
                  "    small = int(byte(200)) + int(int8(-100));\n"
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
                         "held := Work(); Observe :: (held: ref task(double)) "
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
                  "yield; return 7; }Pack :: () -> task(double)[] { held: "
                  "task(double)[] = [Work()]; return held; }on Impulse :: "
                  "(amount: double, "
                  "enabled: bool) { first: task(double)[] = []; i := 0;while i "
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
    const char *source = "Nested :: behavior { pulse: task(double); held: "
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
        "Scope :: behavior { got: double = 0;Work :: () -> double { yield; "
        "return 7; }Pack :: () -> (number: double, discarded: task(double)) "
        "{return(number: 1, discarded: Work()); }Observe :: (held: ref "
        "task(double)) { await Delay(10);result := await held; if result is "
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
        *source = "Waiters :: behavior { worker: task; total: double "
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
        REQUIRE(text && pin_text(*text).bytes && !strcmp(pin_text(*text).bytes, "retained"));
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
                  "task;Work :: () -> double[] { yield; return "
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
    /* Version 10 blocked on nested callees too; action initializers keep
     * their own lifetime, parenthesized or not. */
    const char *source = "behavior N { n: double = 0;"
                         "action later = (Outer());"
                         "function Outer() -> () { function Inner() -> () { "
                         "await Delay(1); } Inner(); n += 1; }"
                         "function Pause() -> () { await Delay(1); }"
                         "function Start() -> () { action Pause = timer(1); "
                         "Pause(); } }";
    /* A nested action shadows the waiting function of the same name. */
    const char *expected = "behavior N { n: double = 0;"
                           "action later = (Outer());"
                           "function Outer() -> () { function Inner() -> () { "
                           "await Delay(1); } await Inner(); n += 1; }"
                           "function Pause() -> () { await Delay(1); }"
                           "function Start() -> () { action Pause = timer(1); "
                           "Pause(); } }";
    char **all = chibi_migrate_task_calls(&source, 1);
    REQUIRE(all[0] && !strcmp(all[0], expected));
    free(all[0]);
    free(all);
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
TEST_CASE(program_version_11_references_migrate_to_values)
{
    const char *source = "behavior R { n: double = 1; m: int = 6;"
                         "function Bump(ref double v) -> () { v += 2; }"
                         "on Impulse(amount, enabled) { Bump(n); &n = n + 1;"
                         " m = m & 3; m &= 7; s := \"a &b\"; /* &c */ } }";
    const char *expected = "behavior R { n: double = 1; m: int = 6;"
                           "function Bump(ref double v) -> () { v += 2; }"
                           "on Impulse(amount, enabled) { Bump(n); n = n + 1;"
                           " m = m & 3; m &= 7; s := \"a &b\"; /* &c */ } }";
    char *migrated = chibi_migrate_references(source);
    REQUIRE(migrated && !strcmp(migrated, expected));
    REQUIRE(!chibi_migrate_references(expected));
    char *bound = chibi_migrate_references("x := fallback(&speed, default: 0)");
    REQUIRE(bound && !strcmp(bound, "x := fallback(binding: speed, default: 0)"));
    free(bound);
    bound = chibi_migrate_references("x := fallback((&speed), default: 0)");
    REQUIRE(bound && !strcmp(bound, "x := fallback(binding: (speed), default: 0)"));
    free(bound);
    bound = chibi_migrate_references("x := fallback(driver, &speed, default: 0)");
    REQUIRE(bound && !strcmp(bound, "x := fallback(driver, binding: speed, default: 0)"));
    free(bound);
    bound = chibi_migrate_references("x := fallback(get(a, b), ((&speed)), default: (0))");
    REQUIRE(bound && !strcmp(bound, "x := fallback(get(a, b), binding: ((speed)), default: (0))"));
    free(bound);
    bound = chibi_migrate_references("x := fallback(binding: &speed, default: f(&n))");
    REQUIRE(bound && !strcmp(bound, "x := fallback(binding: speed, default: f(n))"));
    free(bound);
    bound = chibi_migrate_references("f(Value: ref int split &count, Other: true & ok)");
    REQUIRE(bound && !strcmp(bound, "f(Value: ref int split count, Other: true & ok)"));
    free(bound);
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_migrated(migrated, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 4));
    }
    free(migrated);
    return true;
}
/* The test catalog's Impulse event, declared as a project declares it, so
 * the version 30 migration can type a handler's payload. */
static void declare_impulse(const char *root, bool declared)
{
    char path[300];
    snprintf(path, sizeof path, "%s/content/Impulse.kdl", root);
    if (!declared) {
        unlink(path);
        return;
    }
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs(
        "version 3\nname \"Impulse\"\ndomain \"event\"\n(array)variables {\n"
        "    - id=1 name=\"amount\" type=\"double\"\n    - id=2 "
        "name=\"enabled\" type=\"bool\"\n}\n",
        file
    );
    fclose(file);
}
TEST_CASE(program_version_12_results_migrate_to_returns)
{
    const char *
        source = "behavior R { n: double = 0\n"
                 "    function Pair(x: double) -> (Sum: double, Twice: "
                 "double, Kept: double) {\n"
                 "        result Sum: double = x + 1\n"
                 "        Kept = 4\n"
                 "        result Twice: double = (x) * 2\n"
                 "        if x > 10 {\n"
                 "            return\n"
                 "        }\n"
                 "        return\n"
                 "    }\n"
                 "    pure function Seven() -> (Result: double) { "
                 "result Result: double = 7; }\n"
                 "    function Half(x: double) -> (Out: double) {\n"
                 "        result Out: double = x / 2\n"
                 "        if x > 100 {\n"
                 "            Out := 3\n"
                 "            Out = 4\n"
                 "            return\n"
                 "        }\n"
                 "    }\n"
                 "    function Set(x: double) -> (Value: double) {\n"
                 "        result Value: double = x\n"
                 "        if x > 1 {\n"
                 "            Value = 10\n"
                 "        }\n"
                 "        return\n"
                 "    }\n"
                 "    function List(x: double) -> (Values: double[]) {\n"
                 "        result Values: double[] = [x]\n"
                 "        Values[0] = 2\n"
                 "        return\n"
                 "    }\n"
                 "    function Bump(x: ref double) { x += 1 }\n"
                 "    function Observe(x: double) { return x }\n"
                 "    function Wrap() { function Observe(x: ref double) { "
                 "x = 1 } }\n"
                 "    function Bumped(x: double) -> (Total: double) {\n"
                 "        result Total: double = x\n"
                 "        Bump(Total)\n"
                 "        return\n"
                 "    }\n"
                 "    function Seen(x: double) -> (Last: double) {\n"
                 "        result Last: double = x\n"
                 "        Observe(x: Last)\n"
                 "        return\n"
                 "    }\n"
                 "    function Outer() -> (X: double) { if n > 0 {} /* lead */ "
                 "result X: double = 1; function Inner() { return } return }\n"
                 "    on Impulse(amount, enabled) { p := Pair(2); n = "
                 "p.Sum + p.Twice + p.Kept + Seven() + Half(4) + Set(4) }\n"
                 "}\n";
    const char *
        expected = "behavior R { n: double = 0\n"
                   "    function Pair(x: double) -> (Sum: double, "
                   "Twice: double, Kept: double) {\n"
                   "        Kept = 4\n"
                   "        if x > 10 {\n"
                   "            return(x + 1, (x) * 2, Kept)\n"
                   "        }\n"
                   "        return(x + 1, (x) * 2, Kept)\n"
                   "    }\n"
                   "    pure function Seven() -> (Result: double) { "
                   "return 7 }\n"
                   "    function Half(x: double) -> (Out: double) {\n"
                   "        if x > 100 {\n"
                   "            Out := 3\n"
                   "            Out = 4\n"
                   "            return x / 2\n"
                   "        }\n"
                   "        return x / 2\n"
                   "    }\n"
                   "    function Set(x: double) -> (Value: double) {\n"
                   "        Value = x\n"
                   "        if x > 1 {\n"
                   "            Value = 10\n"
                   "        }\n"
                   "        return Value\n"
                   "    }\n"
                   "    function List(x: double) -> (Values: double[]) {\n"
                   "        Values = [x]\n"
                   "        Values[0] = 2\n"
                   "        return Values\n"
                   "    }\n"
                   "    function Bump(x: ref double) { x += 1 }\n"
                   "    function Observe(x: double) { return x }\n"
                   "    function Wrap() { function Observe(x: ref double) { "
                   "x = 1 } }\n"
                   "    function Bumped(x: double) -> (Total: double) {\n"
                   "        Total = x\n"
                   "        Bump(Total)\n"
                   "        return Total\n"
                   "    }\n"
                   "    function Seen(x: double) -> (Last: double) {\n"
                   "        Observe(x: Last)\n"
                   "        return x\n"
                   "    }\n"
                   "    function Outer() -> (X: double) { if n > 0 {} /* lead "
                   "*/ "
                   "function Inner() { return } return 1 }\n"
                   "    on Impulse(amount, enabled) { p := Pair(2); n "
                   "= p.Sum + p.Twice + p.Kept + Seven() + Half(4) + Set(4) }\n"
                   "}\n";
    char *migrated = chibi_migrate_results(source);
    REQUIRE(migrated && !strcmp(migrated, expected));
    REQUIRE(!chibi_migrate_results(expected));
#ifndef __EMSCRIPTEN__
    /* Links at the staging paths are replaced, not written through, and
     * only the top-level version node changes. */
    char root[] = "/tmp/chibi-migrate-XXXXXX", path[256], outside[256],
         staged[256], error[256];
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/content", root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/content/R.chibi", root);
    FILE *file = fopen(path, "wb");
    fputs(source, file);
    fclose(file);
    snprintf(outside, sizeof outside, "%s/outside", root);
    file = fopen(outside, "wb");
    fputs("keep", file);
    fclose(file);
    snprintf(staged, sizeof staged, "%s/content/R.chibi.migrating", root);
    REQUIRE(!link(outside, staged));
    char manifest[256];
    snprintf(manifest, sizeof manifest, "%s/" CHIBI_PROJECT_MANIFEST, root);
    snprintf(staged, sizeof staged, "%s.migrating", manifest);
    REQUIRE(!symlink(outside, staged));
    file = fopen(manifest, "wb");
    fputs(
        "meta {\n    version 12\n}\n/-\nversion 12\n/- x {\n}\n// metadata "
        "{\nname #\"P\\\"#; /* a /* b */ { */ \"version\" "
        "0xc\ncontent "
        "\"content\"\n"
        "start_scene \"scenes/Start.kdl\"\n",
        file
    );
    fclose(file);
    REQUIRE(!chmod(manifest, 0640) && !chmod(path, 0600));
    declare_impulse(root, true);
    int upgraded = chibi_project_migrate(root, error, sizeof error);
    declare_impulse(root, false);
    if (upgraded < 0) {
        fprintf(stderr, "migration: %s\n", error);
    }
    REQUIRE(upgraded == CHIBI_PROJECT_VERSION - 12);
    char text[2048];
    file = fopen(outside, "rb");
    text[fread(text, 1, sizeof text - 1, file)] = 0;
    fclose(file);
    REQUIRE(!strcmp(text, "keep"));
    struct stat info;
    REQUIRE(!lstat(manifest, &info) && S_ISREG(info.st_mode));
    REQUIRE((info.st_mode & 07777) == 0640);
    REQUIRE(!stat(path, &info) && (info.st_mode & 07777) == 0600);
    file = fopen(manifest, "rb");
    text[fread(text, 1, sizeof text - 1, file)] = 0;
    fclose(file);
    char current[32];
    snprintf(current, sizeof current, "\nversion %d\n", CHIBI_PROJECT_VERSION);
    REQUIRE(strstr(text, "meta version=12\n") && strstr(text, current));
    file = fopen(path, "rb");
    text[fread(text, 1, sizeof text - 1, file)] = 0;
    fclose(file);
    REQUIRE(declarations_equal(text, expected));
    unlink(path);
    unlink(outside);
    unlink(manifest);
    snprintf(path, sizeof path, "%s/content", root);
    rmdir(path);
    rmdir(root);
#endif
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_migrated(migrated, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 30));
    }
    free(migrated);
    return true;
}
TEST_CASE(program_version_10_blocking_calls_migrate_to_awaits)
{
    const char *source = "behavior B { n: double = 0;"
                         "function Wait() -> () { await Delay(1); n += 1; }"
                         "function Outer() -> (v: double, w: double) { Wait(); "
                         "return(n, 0); }"
                         "function Plain() -> () { n += 100; }"
                         "on Impulse(amount, enabled) { Plain(); x := "
                         "Outer().v; n += x; Outer(); await Wait(); } }";
    const char
        *expected = "behavior B { n: double = 0;"
                    "function Wait() -> () { await Delay(1); n += 1; }"
                    "function Outer() -> (v: double, w: double) { await "
                    "Wait(); return(n, 0); }"
                    "function Plain() -> () { n += 100; }"
                    "on Impulse(amount, enabled) { Plain(); x := (await "
                    "Outer()).v; n += x; await Outer(); await Wait(); } }";
    const char
        *library = "library Store { function Work() -> () { await Delay(1); } "
                   "function Idle() -> () { } }";
    const char
        *user = "behavior U { on Impulse(amount, enabled) { Store.Work(); "
                "store.work(); Store.Idle(); Work(); } }";
    const char *sources[] = { source, library, user };
    char **all = chibi_migrate_task_calls(sources, 3);
    char *migrated = all[0];
    REQUIRE(migrated && !strcmp(migrated, expected) && !all[1]);
    REQUIRE(
        all[2] &&
        !strcmp(
            all[2],
            "behavior U { on Impulse(amount, enabled) { await Store.Work(); "
            "await store.work(); Store.Idle(); Work(); } }"
        )
    );
    free(all[2]);
    free(all);
    all = chibi_migrate_task_calls(&expected, 1);
    REQUIRE(!all[0]);
    free(all);
    /* A call names the innermost function of that name in its scope. */
    const char
        *nested = "behavior N { function A() -> () { "
                  "function Tick() -> () { } Tick(); } "
                  "function C() -> () { "
                  "function Tick() -> () { await Delay(1); } Tick(); } }";
    all = chibi_migrate_task_calls(&nested, 1);
    REQUIRE(
        all[0] &&
        !strcmp(
            all[0],
            "behavior N { function A() -> () { function Tick() -> () { } "
            "Tick(); } function C() -> () { function Tick() -> () { await "
            "Delay(1); } await Tick(); } }"
        )
    );
    free(all[0]);
    free(all);
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_migrated(migrated, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        for (int i = 1; i <= 4; i++) {
            REQUIRE(execution_program_update(program, i, 0, 1000) == FLOW_OK);
        }
        REQUIRE(number_state(instance, "n", 204) && execution_instance_pending(instance) == 0);
    }
    free(migrated);

    char root[] = "/tmp/chibi-migrate-XXXXXX", path[256], error[256];
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/content", root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/content/B.chibi", root);
    FILE *file = fopen(path, "wb");
    fputs(source, file);
    fclose(file);
    /* Symlinks are not sources: neither a cycle nor a file outside content is
     * followed. */
    char outside[256], link[256];
    snprintf(outside, sizeof outside, "%s/Outside.chibi", root);
    file = fopen(outside, "wb");
    fputs(source, file);
    fclose(file);
    snprintf(link, sizeof link, "%s/content/Outside.chibi", root);
    REQUIRE(!symlink(outside, link));
    snprintf(link, sizeof link, "%s/content/loop", root);
    REQUIRE(!symlink(root, link));
    snprintf(path, sizeof path, "%s/" CHIBI_PROJECT_MANIFEST, root);
    file = fopen(path, "wb");
    fputs(
        "version  10 // saved by an older build\r\ncontent \"content\"\r\n"
        "start_scene \"scenes/Start.kdl\"\r\n",
        file
    );
    fclose(file);
    declare_impulse(root, true);
    int upgraded = chibi_project_migrate(root, error, sizeof error);
    declare_impulse(root, false);
    if (upgraded < 0) {
        fprintf(stderr, "migration: %s\n", error);
    }
    REQUIRE(upgraded == CHIBI_PROJECT_VERSION - 10);
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == 0);
    char text[1024];
    file = fopen(path, "rb");
    text[fread(text, 1, sizeof text - 1, file)] = 0;
    fclose(file);
    char current[32];
    snprintf(current, sizeof current, "version %d\n", CHIBI_PROJECT_VERSION);
    REQUIRE(!strncmp(text, current, strlen(current)));
    unlink(path);
    snprintf(path, sizeof path, "%s/content/B.chibi", root);
    file = fopen(path, "rb");
    text[fread(text, 1, sizeof text - 1, file)] = 0;
    fclose(file);
    REQUIRE(declarations_equal(text, expected));
    unlink(path);
    file = fopen(outside, "rb");
    text[fread(text, 1, sizeof text - 1, file)] = 0;
    fclose(file);
    REQUIRE(!strcmp(text, source));
    unlink(outside);
    unlink(link);
    snprintf(link, sizeof link, "%s/content/Outside.chibi", root);
    unlink(link);
    snprintf(path, sizeof path, "%s/content", root);
    rmdir(path);
    rmdir(root);
    return true;
}

TEST_CASE(program_version_16_locals_migrate_to_declarations)
{
    const char *source = "behavior L { n: double = 0; hits: double = 0\n"
                         "    local base = n + 1\n"
                         "    local twice: double = local.base * 2\n"
                         "    function Add(x: double) {\n"
                         "        local X = x + 1\n"
                         "        if local.X > 1 { hits += X }\n"
                         "        return twice + X\n"
                         "    }\n"
                         "    on Impulse(amount, enabled) {\n"
                         "        if enabled { n = base + Add(amount) }\n"
                         "    }\n"
                         "}\n";
    const char *expected = "behavior L { n: double = 0; hits: double = 0\n"
                           "    function Add(x: double) {\n"
                           "        X := x + 1\n"
                           "        if X > 1 { hits += X }\n"
                           "        base := n + 1\n"
                           "        twice: double = base * 2\n"
                           "        return twice + X\n"
                           "    }\n"
                           "    on Impulse(amount, enabled) {\n"
                           "        if enabled { base := n + 1; n = "
                           "base + Add(amount) }\n"
                           "    }\n"
                           "}\n";
    char error[256] = "";
    char *migrated = NULL, *unchanged = NULL;
    REQUIRE(!chibi_migrate_locals(source, &migrated, error, sizeof error));
    REQUIRE(migrated && !strcmp(migrated, expected));
    REQUIRE(!chibi_migrate_locals(expected, &unchanged, error, sizeof error) && !unchanged);
    /* Declaring `base` in Shadow would make it read the parameter `n`. */
    const char *shadowed = "behavior L { n: double = 0\n"
                           "    local base = n + 1\n"
                           "    function Shadow(n: double) { return base }\n"
                           "}\n";
    REQUIRE(
        chibi_migrate_locals(shadowed, &unchanged, error, sizeof error) &&
        strstr(error, "`n`")
    );
    /* An action argument reads the expression when the action starts. */
    const char *argument = "behavior L { n: double = 0\n"
                           "    local base = n + 1\n"
                           "    local more: double = local.base\n"
                           "    action wait = timer(more)\n"
                           "}\n";
    REQUIRE(!chibi_migrate_locals(argument, &unchanged, error, sizeof error));
    REQUIRE(
        unchanged && !strcmp(
                         unchanged,
                         "behavior L { n: double = 0\n"
                         "    action wait = timer(cast<double>((n + 1)))\n"
                         "}\n"
                     )
    );
    free(unchanged);
    /* Each run evaluates a local once, so a loop does not re-evaluate it. */
    const char *loop = "behavior L { n: double = 0\n"
                       "    local base = 1 + 1\n"
                       "    on Impulse(amount, enabled) {\n"
                       "        i: double = 0\n"
                       "        while i < 2 { i += 1; n += base }\n"
                       "    }\n"
                       "}\n";
    REQUIRE(!chibi_migrate_locals(loop, &unchanged, error, sizeof error));
    REQUIRE(
        unchanged && !strcmp(
                         unchanged,
                         "behavior L { n: double = 0\n"
                         "    on Impulse(amount, enabled) {\n"
                         "        i: double = 0\n"
                         "        base := 1 + 1\n"
                         "        while i < 2 { i += 1; n += base }\n"
                         "    }\n"
                         "}\n"
                     )
    );
    free(unchanged);
    /* A quoted name needs an identifier no other name spells. */
    const char *names[] = {
        "behavior L { local \"a-b\" = 1; local \"ab\" = 2\n"
        "    on Impulse(amount, enabled) { amount = local.\"a-b\" + "
        "local.\"ab\" }\n}\n",
        "behavior L { ab: double = 0; local \"a b\" = 1\n"
        "    on Impulse(amount, enabled) { ab = local.\"a b\" }\n}\n",
        "behavior L { local \"if\" = 1\n"
        "    on Impulse(amount, enabled) { amount = local.\"if\" }\n}\n",
    };
    for (int i = 0; i < 3; i++) {
        unchanged = NULL;
        REQUIRE(chibi_migrate_locals(names[i], &unchanged, error, sizeof error) && !unchanged);
    }
    /* A branch might skip the first read, or a loop reach it after the state
     * it reads changed, and an expansion runs at every read. */
    const char *later[] = {
        "behavior L { n: double = 0\n"
        "    function One() { return 1 }\n"
        "    local hit = One()\n"
        "    on Tick() { if n > 0 { n = hit }; if n > 1 { n = hit } }\n"
        "}\n",
        "behavior L { n: double = 0\n"
        "    local hit = n + 1\n"
        "    on Tick() { if n > 0 { n = hit }; n = 10; if n > 1 { n = hit } "
        "}\n"
        "}\n",
        "behavior L { n: double = 0\n"
        "    local hit = n + 1\n"
        "    on Tick() { i: double = 0; while i < 2 { i += 1; n += hit } }\n"
        "}\n",
        "behavior L { n: double = 0\n"
        "    function One() { return 1 }\n"
        "    local hit = One()\n"
        "    on Tick() { hit := 0; a := local.hit; b := local.hit }\n"
        "}\n",
        "behavior L { n: double = 0\n"
        "    local hit = n + 1\n"
        "    on Tick() { hit := 0; while n < 2 { n += local.hit } }\n"
        "}\n",
        "behavior L { n: double = 0\n"
        "    function SetN() { n = 5; return 0 }\n"
        "    local hit = n + 1\n"
        "    on Tick() { n = SetN() + hit }\n"
        "}\n",
        "behavior L { n: double = 0; local hit = hit + 1\n"
        "    on Tick() { n = hit }\n}\n",
        "behavior L { n: double = 0; local hit = other; local other = hit\n"
        "    on Tick() { n = hit }\n}\n",
        "behavior L { n: double = 0; items: double = 0\n"
        "    function One() { return 1 }\n"
        "    local index = One(); local hit = items[index]\n"
        "    on Tick() { n = hit; hit = 2 }\n}\n",
        "behavior L { n: double = 0; local hit = [0]\n"
        "    on Tick() { hit := 0; a := local.hit[0]; b := local.hit[0] }\n}\n",
        "behavior L { n: double = 0\n"
        "    function SetN() { n = 5; return 0 }\n"
        "    local hit = n + 1; local other = SetN()\n"
        "    on Tick() { n = other + hit }\n}\n",
        "behavior L { n: double = 0\n"
        "    function One() { return 1 }\n"
        "    local hit = One(); local twice = hit + hit\n"
        "    action wait = timer(twice)\n}\n",
        "behavior L { n: double = 0\n"
        "    function One() { return 1 }\n"
        "    local hit = One()\n"
        "    on Tick() { hit = 2; n = hit }\n}\n",
    };
    for (int i = 0; i < 13; i++) {
        unchanged = NULL;
        REQUIRE(
            chibi_migrate_locals(later[i], &unchanged, error, sizeof error) &&
            !unchanged && strstr(error, "`hit`")
        );
    }
    /* Read in one branch only, it is declared inside that branch. */
    const char
        *one = "behavior L { n: double = 0\n"
               "    function One() { return 1 }\n"
               "    local hit = One()\n"
               "    on Impulse(amount, enabled) { if enabled { n = hit } }\n"
               "}\n";
    REQUIRE(!chibi_migrate_locals(one, &unchanged, error, sizeof error));
    REQUIRE(unchanged && strstr(unchanged, "if enabled { hit := One(); n = hit }"));
    free(unchanged);
    /* A condition is always read, so the local goes before its statement. */
    const char *condition = "behavior L { n: double = 0\n"
                            "    function One() { return 1 }\n"
                            "    local hit = One()\n"
                            "    on Impulse(amount, enabled) { if hit > 0 { n "
                            "= hit } }\n"
                            "}\n";
    REQUIRE(!chibi_migrate_locals(condition, &unchanged, error, sizeof error));
    REQUIRE(unchanged && strstr(unchanged, "{ hit := One(); if hit > 0 { n = hit } }"));
    free(unchanged);
    const char *disabled[] = {
        "behavior L { disabled local X = 1 }\n",
        "behavior L { disabled /* x */ local X = 1 }\n"
    };
    for (int i = 0; i < 2; i++) {
        REQUIRE(
            chibi_migrate_locals(disabled[i], &unchanged, error, sizeof error) &&
            !unchanged && strstr(error, "disabled")
        );
    }
    /* A comment in a result type does not end it. */
    const char *tuple = "behavior L { n: double = 0\n"
                        "    function Pair() { return 1 }\n"
                        "    local pair: (a: double /* ) */) = Pair()\n"
                        "    on Tick() { n = pair }\n}\n";
    REQUIRE(!chibi_migrate_locals(tuple, &unchanged, error, sizeof error));
    REQUIRE(unchanged && strstr(unchanged, "{ pair := Pair(); n = pair }"));
    free(unchanged);
    /* Comments and line breaks may sit between a local's tokens. */
    const char *spaced[] = {
        "behavior L { n: double = 0; local /* note */ X = 1\n"
        "    on Impulse(amount, enabled) { n = X }\n}\n",
        "behavior L { n: double = 0; local X /* note */ = 1\n"
        "    on Impulse(amount, enabled) { n = X }\n}\n",
        "behavior L { n: double = 0; local\n    X = 1\n"
        "    on Impulse(amount, enabled) { n = X }\n}\n",
    };
    for (int i = 0; i < 3; i++) {
        REQUIRE(!chibi_migrate_locals(spaced[i], &unchanged, error, sizeof error));
        REQUIRE(unchanged && !strstr(unchanged, "local") && strstr(unchanged, "X := 1; n = X"));
        free(unchanged);
    }
    /* A local naming storage reads and writes it; an action's output or the
     * owner is read once. */
    const char
        *alias = "behavior L { n: double = 0\n"
                 "    local alias = n\n"
                 "    local typed: double = alias\n"
                 "    action wait = timer(1)\n"
                 "    local done = wait.Done\n"
                 "    local hp = get_component(self).Value\n"
                 "    local me = self\n"
                 "    local amount = event.amount\n"
                 "    local health = self.Health\n"
                 "    local armor = engine[\"Get Component\"](self).Value\n"
                 "    on Tick() { alias = 2; m := typed; typed = 3; n = "
                 "done; hp = 4; e := me; health = 5; armor = 6; a := amount }\n"
                 "}\n";
    REQUIRE(!chibi_migrate_locals(alias, &unchanged, error, sizeof error));
    REQUIRE(
        unchanged &&
        !strcmp(
            unchanged,
            "behavior L { n: double = 0\n"
            "    action wait = timer(1)\n"
            "    on Tick() { n = 2; m := n; n = 3; done := "
            "wait.Done; n = done; "
            "get_component(self).Value = 4; me := self; e := me; "
            "self.Health = 5; engine[\"Get Component\"](self).Value = 6; "
            "amount := event.amount; a := amount }\n"
            "}\n"
        )
    );
    free(unchanged);
    /* A system's component parameter is storage too. */
    const char *system = "system S(position: Position) { local p = position; "
                         "p.x = 1 }\n";
    REQUIRE(!chibi_migrate_locals(system, &unchanged, error, sizeof error));
    REQUIRE(unchanged && strstr(unchanged, "{ position.x = 1 }"));
    free(unchanged);
    /* `local.X` names the root local past a nearer `X`. */
    const char *qualified = "behavior L { n: double = 0\n"
                            "    local X = n + 1\n"
                            "    on Impulse(amount, enabled) { X := 0; n = "
                            "local.X + X }\n"
                            "}\n";
    REQUIRE(!chibi_migrate_locals(qualified, &unchanged, error, sizeof error));
    REQUIRE(unchanged && !strstr(unchanged, "local") && strstr(unchanged, "n = (n + 1) + X"));
    free(unchanged);
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_migrated(migrated, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 5) && number_state(instance, "hits", 2));
    }
    free(migrated);
    return true;
}
TEST_CASE(program_version_13_values_migrate_to_their_expressions)
{
    const char *source = "behavior V { n: double = 0\n"
                         "    value twice = n * 2\n"
                         "    value more = twice + 1\n"
                         "    /* who */ value who: entity = self\n"
                         "    function Add(x: double) {\n"
                         "        value sum: double = x + more\n"
                         "        X := entry.x\n"
                         "        return Keep(Value: sum + X)\n"
                         "    }\n"
                         "    function Keep(Value: double) { return Value }\n"
                         "    function Shadow(more: double) { return more }\n"
                         "    on Impulse(amount, enabled) { w := who; n = "
                         "Add(amount) + "
                         "Shadow(5) + twice }\n"
                         "}\n";
    const char *expected = "behavior V { n: double = 0\n"
                           "    /* who */ function Add(x: double) {\n"
                           "        X := entry.x\n"
                           "        return Keep(Value: cast<double>(x + ((n * "
                           "2) + 1)) + X)\n"
                           "    }\n"
                           "    function Keep(Value: double) { return Value }\n"
                           "    function Shadow(more: double) { return more }\n"
                           "    on Impulse(amount, enabled) { w := "
                           "cast<entity>(self); n = Add(amount) "
                           "+ Shadow(5) + (n * 2) }\n"
                           "}\n";
    char error[256] = "";
    char *migrated = NULL, *unchanged = NULL;
    REQUIRE(!chibi_migrate_values(source, &migrated, error, sizeof error));
    REQUIRE(migrated && !strcmp(migrated, expected) && !error[0]);
    REQUIRE(!chibi_migrate_values(expected, &unchanged, error, sizeof error) && !unchanged);
    /* Expanding `twice` where a local `n` is declared would read the local. */
    const char *shadowed = "behavior V { n: double = 0\n"
                           "    value twice = n * 2\n"
                           "    on Impulse(amount, enabled) { n := amount; m "
                           ":= twice }\n"
                           "}\n";
    REQUIRE(
        chibi_migrate_values(shadowed, &unchanged, error, sizeof error) &&
        strstr(error, "`n`")
    );
    /* The failure does not depend on room for its message. */
    REQUIRE(chibi_migrate_values(shadowed, &unchanged, NULL, 0) == -1 && !unchanged);
    /* Expanding `x` inside Outer would call Outer's own F. */
    const char *callable = "behavior V { n: double = 0\n"
                           "    function F() { return 1 }\n"
                           "    value x = F()\n"
                           "    function Outer() { function F() { return 2 }\n"
                           "        return x }\n"
                           "}\n";
    error[0] = '\0';
    REQUIRE(
        chibi_migrate_values(callable, &unchanged, error, sizeof error) &&
        strstr(error, "`F`")
    );
    /* An action is a callable too. */
    const char *action = "behavior V { n: double = 0\n"
                         "    function F() { return 1 }\n"
                         "    value x = F()\n"
                         "    function Outer() { action F = timer(1)\n"
                         "        return x }\n"
                         "}\n";
    error[0] = '\0';
    REQUIRE(
        chibi_migrate_values(action, &unchanged, error, sizeof error) &&
        strstr(error, "`F`")
    );
    /* A project whose values conflict stays at version 13, even when the
     * caller has no room for the message. */
    char root[] = "/tmp/chibi-values-XXXXXX", path[256], manifest[256], text[256];
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/content", root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/content/V.chibi", root);
    FILE *file = fopen(path, "wb");
    fputs(shadowed, file);
    fclose(file);
    snprintf(manifest, sizeof manifest, "%s/" CHIBI_PROJECT_MANIFEST, root);
    file = fopen(manifest, "wb");
    fputs("version 13\ncontent \"content\"\nstart_scene \"scenes/Start.kdl\"\n", file);
    fclose(file);
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == -1 && strstr(error, "`n`"));
    REQUIRE(chibi_project_migrate(root, NULL, 0) == -1);
    file = fopen(manifest, "rb");
    text[fread(text, 1, sizeof text - 1, file)] = 0;
    fclose(file);
    REQUIRE(strstr(text, "version 13"));
    file = fopen(path, "rb");
    text[fread(text, 1, sizeof text - 1, file)] = 0;
    fclose(file);
    REQUIRE(!strcmp(text, shadowed));
    unlink(path);
    unlink(manifest);
    snprintf(path, sizeof path, "%s/content", root);
    rmdir(path);
    rmdir(root);
    free(migrated);
    prepare();
    REQUIRE(compile_migrated(expected, BEHAVIOR_BYTECODE));
    execution_instance *instance = execution_program_attach(
        program,
        ecs_entity_to_u64(ecs_world_create_entity(world))
    );
    const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                               { .type = PIN_BOOL, .b = true } };
    REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
    REQUIRE(number_state(instance, "n", 8));
    return true;
}
/* Migrates a version 14 project holding content/Level.kdl and a start scene
 * at `path` under the root, which the manifest names `start_scene`. */
static bool migrates_start_scene(
    const char *source,
    const char *expected,
    const char *start_scene,
    const char *path
)
{
    char root[] = "/tmp/chibi-migrate-XXXXXX", manifest[256], error[256];
    REQUIRE(mkdtemp(root));
    char scene[256], start[256], directory[256];
    snprintf(directory, sizeof directory, "%s/content", root);
    REQUIRE(!mkdir(directory, 0700));
    snprintf(scene, sizeof scene, "%s/content/Level.kdl", root);
    snprintf(start, sizeof start, "%s/%s", root, path);
    snprintf(directory, sizeof directory, "%s", start);
    *strrchr(directory, '/') = 0;
    REQUIRE(!mkdir(directory, 0700) || errno == EEXIST);
    const char *scenes[] = { scene, start };
    for (int i = 0; i < 2; i++) {
        FILE *file = fopen(scenes[i], "wb");
        fputs(source, file);
        fclose(file);
    }
    snprintf(manifest, sizeof manifest, "%s/" CHIBI_PROJECT_MANIFEST, root);
    FILE *file = fopen(manifest, "wb");
    fprintf(file, "version 14\ncontent \"content\"\nstart_scene \"%s\"\n", start_scene);
    fclose(file);
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == CHIBI_PROJECT_VERSION - 14);
    for (int i = 0; i < 2; i++) {
        char text[1024];
        file = fopen(scenes[i], "rb");
        text[fread(text, 1, sizeof text - 1, file)] = 0;
        fclose(file);
        REQUIRE(!strcmp(text, expected));
        unlink(scenes[i]);
    }
    unlink(manifest);
    rmdir(directory);
    snprintf(directory, sizeof directory, "%s/content", root);
    rmdir(directory);
    rmdir(root);
    return true;
}
TEST_CASE(program_version_14_scenes_migrate_to_own_behaviors)
{
    const char *source = "version 1\n"
                         "name \"Start\"\n"
                         "(array)entities {\n"
                         "    - name=\"Camera\" prototype=\"Camera\"\n"
                         "    - name=\"Player\" prototype=\"Player\" {\n"
                         "        (array)behaviors \"Move\"\n"
                         "    }\n"
                         "}\n";
    const char *expected = "version 1\n"
                           "name \"Start\"\n"
                           "(array)entities {\n"
                           "    - {\n"
                           "        name \"Camera\"\n"
                           "        prototype \"Camera\"\n"
                           "        (array)behaviors\n"
                           "    }\n"
                           "    - {\n"
                           "        name \"Player\"\n"
                           "        prototype \"Player\"\n"
                           "        (array)behaviors \"Move\"\n"
                           "    }\n"
                           "}\n";
    char *migrated = chibi_migrate_scene_behaviors(source);
    REQUIRE(migrated && !strcmp(migrated, expected));
    REQUIRE(!chibi_migrate_scene_behaviors(expected));
    REQUIRE(!chibi_migrate_scene_behaviors(
        "name \"Camera\"\n(array)entities {\n    - name=\"A\"\n}\n"
    ));
    free(migrated);

    /* Scenes outside content/scenes migrate too, and so does a start scene
     * the scan cannot find: outside the content, or hidden. */
    REQUIRE(migrates_start_scene(source, expected, "../Start.kdl", "Start.kdl"));
    REQUIRE(migrates_start_scene(source, expected, ".scenes/Start.kdl", "content/.scenes/Start.kdl"));
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
/* Migrates a version 15 project whose scene and prototype bind actions, into
 * an input map that already holds one. */
TEST_CASE(program_version_15_bindings_migrate_to_the_input_map)
{
    const char *scene = "version 1\n"
                        "name \"Start\"\n"
                        "(array)entities {\n"
                        "    - {\n"
                        "        name \"Player\"\n"
                        "        prototype \"Player\"\n"
                        "        (array)behaviors\n"
                        "        components {\n"
                        "            GAME.GameActionBinding action=\"Jump\" "
                        "source=\"Z\"\n"
                        "            GAME.GameInput\n"
                        "        }\n"
                        "        retained_components {\n"
                        "            GAME.GameActionBinding action=\"Slide\" "
                        "source=\"X\"\n"
                        "        }\n"
                        "    }\n"
                        "}\n";
    const char
        *prototype = "name \"Player\"\n"
                     "components {\n"
                     "    GAME.GameActionBinding action=\"Fire\" firing=#true "
                     "source=\"X\"\n"
                     "    (object)GAME.GameInput\n"
                     "}\n"
                     "detached_components {\n"
                     "    GAME.GameActionBinding action=\"Dash\" source=\"Z\"\n"
                     "}\n";
    char root[] = "/tmp/chibi-migrate-XXXXXX", path[256], error[256];
    REQUIRE(mkdtemp(root));
    const char *directories[] = { "content", "content/scenes", "content/prototypes" };
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", root, directories[i]);
        REQUIRE(!mkdir(path, 0700));
    }
    const char *files[][2] = {
        { CHIBI_PROJECT_MANIFEST, "version 15\ncontent \"content\"\nstart_scene \"scenes/Start.kdl\"\n" },
        { "content/scenes/Start.kdl", scene },
        { "content/prototypes/Player.kdl", prototype },
        { "content/" INPUT_MAP_FILE, "Jump {\n    (array)sources \"Space\"\n}\n" },
    };
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i][0]);
        FILE *file = fopen(path, "wb");
        fputs(files[i][1], file);
        fclose(file);
    }
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == CHIBI_PROJECT_VERSION - 15);
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == 0);
    const char *expected[] = {
        NULL,
        "version 1\n"
        "name \"Start\"\n"
        "(array)entities {\n"
        "    - {\n"
        "        name \"Player\"\n"
        "        prototype \"Player\"\n"
        "        (array)behaviors\n"
        "        components {\n"
        "            (object)Input\n"
        "        }\n"
        "    }\n"
        "}\n",
        "name \"Player\"\n"
        "components {\n"
        "    (object)Input\n"
        "}\n",
        "Jump {\n"
        "    (array)sources \"Space\" \"Z\"\n"
        "}\n"
        "Fire {\n"
        "    (array)sources \"X\"\n"
        "    firing #true\n"
        "}\n",
    };
    for (int i = 3; i >= 0; i--) {
        snprintf(path, sizeof path, "%s/%s", root, files[i][0]);
        char text[1024];
        FILE *file = fopen(path, "rb");
        text[fread(text, 1, sizeof text - 1, file)] = 0;
        fclose(file);
        if (expected[i] && strcmp(text, expected[i])) {
            fprintf(stderr, "migrated %s:\n%s", files[i][0], text);
        }
        REQUIRE(!expected[i] || !strcmp(text, expected[i]));
    }

    /* Which action a script's binding read meant depends on its entity, so
     * one stops the migration. */
    const char *scripts[][2] = {
        { CHIBI_PROJECT_MANIFEST, "version 15\ncontent \"content\"\nstart_scene \"scenes/Start.kdl\"\n" },
        { "content/Move.chibi", "behavior Move {\n    // ActionBinding\n    on pressed_changed(pressed) {\n    }\n}\n" },
    };
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/%s", root, scripts[i][0]);
        FILE *file = fopen(path, "wb");
        fputs(scripts[i][1], file);
        fclose(file);
    }
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == -1);
    REQUIRE(strstr(error, "Move.chibi reads PressedChanged"));
    snprintf(path, sizeof path, "%s/%s", root, scripts[1][0]);
    unlink(path);

    /* An input map that the input map reader would reject stops the
     * migration before any asset changes. */
    const char *unrelated[][2] = {
        { "content/scenes/Start.kdl", scene },
        { "content/" INPUT_MAP_FILE, "bindings {\n    jump \"Space\"\n}\n" },
    };
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/%s", root, unrelated[i][0]);
        FILE *file = fopen(path, "wb");
        fputs(unrelated[i][1], file);
        fclose(file);
    }
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == -1);
    REQUIRE(strstr(error, "content/" INPUT_MAP_FILE " is not an input map"));
    const char *const *kept[] = { scripts[0], unrelated[0], unrelated[1] };
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", root, kept[i][0]);
        char text[1024];
        FILE *file = fopen(path, "rb");
        text[fread(text, 1, sizeof text - 1, file)] = 0;
        fclose(file);
        REQUIRE(!strcmp(text, kept[i][1]));
    }

    /* So does a map that the moved bindings would take past the action
     * limit. */
    snprintf(path, sizeof path, "%s/content/" INPUT_MAP_FILE, root);
    FILE *full = fopen(path, "wb");
    for (int i = 0; i < INPUT_ACTION_MAX; i++) {
        fprintf(full, "A%d {\n    (array)sources \"Space\"\n}\n", i);
    }
    fclose(full);
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == -1);
    REQUIRE(strstr(error, "more than 256 actions"));
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/%s", root, kept[i][0]);
        char text[1024];
        FILE *file = fopen(path, "rb");
        text[fread(text, 1, sizeof text - 1, file)] = 0;
        fclose(file);
        REQUIRE(!strcmp(text, kept[i][1]));
    }
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i][0]);
        unlink(path);
    }
    for (int i = 2; i >= 0; i--) {
        snprintf(path, sizeof path, "%s/%s", root, directories[i]);
        rmdir(path);
    }
    rmdir(root);
    return true;
}
/* Removes obsolete motion from a version 17 project while preserving an
 * unrelated SdfMotion scene name. */
TEST_CASE(program_version_17_sdf_motion_is_removed)
{
    const char *scene = "version 1\n"
                        "name \"SdfMotion\"\n"
                        "(array)entities {\n"
                        "    - {\n"
                        "        name \"Enemy\"\n"
                        "        prototype \"Enemy\"\n"
                        "        (array)behaviors\n"
                        "        components {\n"
                        "            GAME.GameSdfMotion amplitude=2.0\n"
                        "            (object)GAME.GameInput\n"
                        "        }\n"
                        "        retained_components {\n"
                        "            GameSdfMotion time=1.0\n"
                        "        }\n"
                        "    }\n"
                        "}\n";
    const char *prototype = "name \"Enemy\"\n"
                            "components {\n"
                            "    GAME.GameSdfMotion angular_frequency=4.0\n"
                            "}\n"
                            "detached_components {\n"
                            "    GAME.GameSdfMotion pluck_amplitude=1.0\n"
                            "}\n";
    char root[] = "/tmp/chibi-migrate-XXXXXX", path[256], error[256];
    REQUIRE(mkdtemp(root));
    const char *directories[] = { "content", "content/scenes", "content/prototypes" };
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", root, directories[i]);
        REQUIRE(!mkdir(path, 0700));
    }
    const char *manifest = "version 17\ncontent \"content\"\nstart_scene "
                           "\"scenes/Start.kdl\"\n";
    const char *files[][2] = {
        { CHIBI_PROJECT_MANIFEST, manifest },
        { "content/scenes/Start.kdl", scene },
        { "content/prototypes/Enemy.kdl", prototype },
        { "content/Note.chibi", "// GameSdfMotion is gone.\nbehavior Note {\n}\n" },
        { "content/scenes/SdfMotion.kdl",
          "// GameSdfMotion is removed.\nversion 1\nname \"SdfMotion\"\n"
          "description \"GameSdfMotion example\"\n(array)entities\n" },
    };
    for (int i = 0; i < 5; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i][0]);
        FILE *file = fopen(path, "wb");
        fputs(files[i][1], file);
        fclose(file);
    }
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == CHIBI_PROJECT_VERSION - 17);
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == 0);
    const char *expected[] = {
        NULL,
        "version 1\n"
        "name \"SdfMotion\"\n"
        "(array)entities {\n"
        "    - {\n"
        "        name \"Enemy\"\n"
        "        prototype \"Enemy\"\n"
        "        (array)behaviors\n"
        "        components {\n"
        "            (object)Input\n"
        "        }\n"
        "    }\n"
        "}\n",
        "name \"Enemy\"\n"
        "(object)components\n",
        "// GameSdfMotion is gone.\nNote :: behavior {\n}\n",
        files[4][1],
    };
    for (int i = 1; i < 5; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i][0]);
        char text[1024];
        FILE *file = fopen(path, "rb");
        text[fread(text, 1, sizeof text - 1, file)] = 0;
        fclose(file);
        if (strcmp(text, expected[i])) {
            fprintf(stderr, "migrated %s:\n%s", files[i][0], text);
        }
        REQUIRE(!strcmp(text, expected[i]));
    }

    /* Only a person can remove any other use, so a script that names the
     * component, by name or in a string, or another asset that still names
     * it stops the migration before any asset changes. */
    static const struct
    {
        const char *path, *text, *message;
    } uses[] = {
        { "content/Wobble.chibi",
          "behavior Wobble {\n    on Tick(dt) {\n        "
          "self.GameSdfMotion.time += dt\n    }\n}\n",
          "Wobble.chibi uses GameSdfMotion, which version 18 removes" },
        { "content/Probe.chibi",
          "behavior Probe {\n    on Tick(dt) {\n        moving := "
          "has_component(Component: "
          "\"GAME.GameSdfMotion\", Entity: 0)\n    }\n}\n",
          "Probe.chibi uses GAME.GameSdfMotion" },
        { "content/Escaped.chibi",
          "behavior Escaped {\n    on Tick(dt) {\n        moving := "
          "has_component(Component: "
          "\"GAME.GameSdf\\u004dotion\", Entity: 0)\n    }\n}\n",
          "Escaped.chibi uses GAME.GameSdfMotion" },
        { "content/Scenario.kdl",
          "name \"Wobble\"\ncomponent \"GAME.GameSdfMotion\"\n",
          "Scenario.kdl uses GameSdfMotion" },
    };
    for (size_t u = 0; u < sizeof uses / sizeof *uses; u++) {
        const char *kept[][2] = {
            { CHIBI_PROJECT_MANIFEST, manifest },
            { "content/prototypes/Enemy.kdl", prototype },
            { uses[u].path, uses[u].text },
        };
        for (int i = 0; i < 3; i++) {
            snprintf(path, sizeof path, "%s/%s", root, kept[i][0]);
            FILE *file = fopen(path, "wb");
            fputs(kept[i][1], file);
            fclose(file);
        }
        REQUIRE(chibi_project_migrate(root, error, sizeof error) == -1);
        if (!strstr(error, uses[u].message)) {
            fprintf(stderr, "migration error: %s\n", error);
        }
        REQUIRE(strstr(error, uses[u].message));
        for (int i = 0; i < 3; i++) {
            snprintf(path, sizeof path, "%s/%s", root, kept[i][0]);
            char text[1024];
            FILE *file = fopen(path, "rb");
            text[fread(text, 1, sizeof text - 1, file)] = 0;
            fclose(file);
            REQUIRE(!strcmp(text, kept[i][1]));
        }
        snprintf(path, sizeof path, "%s/%s", root, uses[u].path);
        unlink(path);
    }
    snprintf(path, sizeof path, "%s/%s", root, CHIBI_PROJECT_MANIFEST);
    unlink(path);
    for (int i = 1; i < 5; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i][0]);
        unlink(path);
    }
    for (int i = 2; i >= 0; i--) {
        snprintf(path, sizeof path, "%s/%s", root, directories[i]);
        rmdir(path);
    }
    rmdir(root);
    return true;
}
/* Migrates a version 18 scene whose object names hold the '.' that version 19
 * uses to separate paths, and stops on a rename that would clash. */
TEST_CASE(program_version_18_dotted_names_are_renamed)
{
    const char *scene = "version 1\n"
                        "name \"Start\"\n"
                        "(array)entities {\n"
                        "    - name=\"Ship\"\n"
                        "    - name=\"Ship.Turret.Barrel\"\n"
                        "}\n";
    const char *renamed = "version 1\n"
                          "name \"Start\"\n"
                          "(array)entities {\n"
                          "    - name=\"Ship\"\n"
                          "    - name=\"Ship_Turret_Barrel\"\n"
                          "}\n";
    const char *clash = "version 1\n"
                        "name \"Start\"\n"
                        "(array)entities {\n"
                        "    - name=\"Ship.Turret\"\n"
                        "    - name=\"Ship_Turret\"\n"
                        "}\n";
    /* A saved scenario's targets and entity payloads follow the rename; other
     * strings keep their dots. */
    const char *scenario =
        "scene \"Start\"\n"
        "frames 2\n"
        "(array)inputs {\n"
        "    - {\n"
        "        frame 0\n"
        "        (array)events {\n"
        "            - {\n"
        "                name \"Hit\"\n"
        "                target \"Ship.Turret.Barrel\"\n"
        "                (array)values {\n"
        "                    - type=\"entity\" value=\"Ship.Turret.Barrel\"\n"
        "                    - type=\"string\" value=\"v1.2\"\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "        (array)ui {\n"
        "            - target=\"Ship.Turret.Barrel\" element=\"fire\" "
        "event=\"click\"\n"
        "        }\n"
        "    }\n"
        "}\n";
    const char *manifest = "version 18\ncontent \"content\"\nstart_scene "
                           "\"scenes/Start.kdl\"\n";
    char root[] = "/tmp/chibi-migrate-XXXXXX", path[256], error[256], text[1024];
    REQUIRE(mkdtemp(root));
    const char *directories[] = { "content", "content/scenes", "content/scenarios" };
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", root, directories[i]);
        REQUIRE(!mkdir(path, 0700));
    }
    const char *runs[][2] = {
        { scene, NULL },
        { clash, "Start.kdl has two objects that version 19 names Ship_Turret" }
    };
    for (int run = 0; run < 2; run++) {
        const char *files[][2] = {
            { CHIBI_PROJECT_MANIFEST, manifest },
            { "content/scenes/Start.kdl", runs[run][0] },
            { "content/scenarios/Fight.kdl", scenario },
        };
        for (int i = 0; i < 3; i++) {
            snprintf(path, sizeof path, "%s/%s", root, files[i][0]);
            FILE *file = fopen(path, "wb");
            fputs(files[i][1], file);
            fclose(file);
        }
        int migrated = chibi_project_migrate(root, error, sizeof error);
        snprintf(path, sizeof path, "%s/%s", root, files[1][0]);
        FILE *file = fopen(path, "rb");
        text[fread(text, 1, sizeof text - 1, file)] = 0;
        fclose(file);
        if (runs[run][1]) {
            REQUIRE(migrated == -1 && strstr(error, runs[run][1]));
            REQUIRE(!strcmp(text, clash));
            continue;
        }
        REQUIRE(migrated == CHIBI_PROJECT_VERSION - 18);
        REQUIRE(chibi_project_migrate(root, error, sizeof error) == 0);
        if (strcmp(text, renamed)) {
            fprintf(stderr, "migrated scene:\n%s", text);
        }
        REQUIRE(!strcmp(text, renamed));
        snprintf(path, sizeof path, "%s/content/scenarios/Fight.kdl", root);
        file = fopen(path, "rb");
        text[fread(text, 1, sizeof text - 1, file)] = 0;
        fclose(file);
        int targets = 0;
        for (const char *at = strstr(text, "\"Ship_Turret_Barrel\""); at;
             at = strstr(at + 1, "\"Ship_Turret_Barrel\"")) {
            targets++;
        }
        if (targets != 3 || strstr(text, "Ship.Turret") || !strstr(text, "\"v1.2\"")) {
            fprintf(stderr, "migrated scenario:\n%s", text);
        }
        REQUIRE(targets == 3 && !strstr(text, "Ship.Turret") && strstr(text, "\"v1.2\""));
    }
    snprintf(path, sizeof path, "%s/%s", root, CHIBI_PROJECT_MANIFEST);
    unlink(path);
    snprintf(path, sizeof path, "%s/content/scenes/Start.kdl", root);
    unlink(path);
    snprintf(path, sizeof path, "%s/content/scenarios/Fight.kdl", root);
    unlink(path);
    for (int i = 2; i >= 0; i--) {
        snprintf(path, sizeof path, "%s/%s", root, directories[i]);
        rmdir(path);
    }
    rmdir(root);
    return true;
}
static char *read_whole(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    static char text[1024];
    text[fread(text, 1, sizeof text - 1, file)] = 0;
    fclose(file);
    return text;
}
static void write_whole(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    fputs(text, file);
    fclose(file);
}
TEST_CASE(program_version_19_components_move_to_chibi)
{
    char root[] = "/tmp/chibi-migrate-XXXXXX", path[256], error[256];
    REQUIRE(mkdtemp(root));
    const char *directories[] = { "content", "content/components", "content/scenes" };
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", root, directories[i]);
        REQUIRE(!mkdir(path, 0700));
    }
    const char *manifest = "version 19\ncontent \"content\"\nstart_scene "
                           "\"scenes/Start.kdl\"\n";
    snprintf(path, sizeof path, "%s/content/scenes/Start.kdl", root);
    write_whole(path, "version 1\nname \"Start\"\n(array)entities\n");
    snprintf(path, sizeof path, "%s/" CHIBI_PROJECT_MANIFEST, root);
    write_whole(path, manifest);
    snprintf(path, sizeof path, "%s/content/components/Charge.kdl", root);
    write_whole(
        path,
        "name \"Charge\"\n"
        "description \"Stored energy.\\n\\nRefills.\"\n"
        "(array)fields {\n"
        "    - name=\"level\" type=\"float\" description=\"\"\n"
        "    - name=\"rate\" type=\"float\" description=\"Per second.\\n\"\n"
        "    - name=\"drift\" type=\"CF_V2\"\n"
        "}\n"
    );
    int steps = chibi_project_migrate(root, error, sizeof error);
    if (steps < 0) {
        fprintf(stderr, "migrate: %s\n", error);
    }
    REQUIRE(steps == CHIBI_PROJECT_VERSION - 19);
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == 0);
    REQUIRE(!read_whole(path));
    snprintf(path, sizeof path, "%s/content/components/Charge.chibi", root);
    const char *expected = "// Stored energy.\n"
                           "//\n"
                           "// Refills.\n"
                           "Charge :: component {\n"
                           "    //\n"
                           "    level: float\n"
                           "    // Per second.\n"
                           "    //\n"
                           "    rate: float\n"
                           "    drift: CF_V2\n"
                           "}\n";
    REQUIRE(!strcmp(read_whole(path), expected));

    /* A declaration the step cannot read stops it before any file changes. */
    snprintf(path, sizeof path, "%s/" CHIBI_PROJECT_MANIFEST, root);
    write_whole(path, manifest);
    snprintf(path, sizeof path, "%s/content/components/Bad.kdl", root);
    const char *bad = "name \"Bad\"\n(array)fields {\n    - name=\"x\" "
                      "type=\"float[]\"\n}\n";
    write_whole(path, bad);
    REQUIRE(chibi_project_migrate(root, error, sizeof error) < 0);
    REQUIRE(strstr(error, "Bad.kdl"));
    REQUIRE(!strcmp(read_whole(path), bad));
    snprintf(path, sizeof path, "%s/content/components/Bad.chibi", root);
    REQUIRE(!read_whole(path));
    REQUIRE(!chibi_migrate_component("name \"1x\"\n(array)fields\n", error, sizeof error));
    /* KDL allowed names .chibi reserves, and a person's file is never
     * replaced. */
    remove(path);
    snprintf(path, sizeof path, "%s/content/components/Bad.kdl", root);
    remove(path);
    REQUIRE(
        !chibi_migrate_component("name \"Cell\"\n(array)fields {\n    - name=\"state\" type=\"int\"\n}\n", error, sizeof error)
    );
    REQUIRE(strstr(error, "'state: int' is not valid .chibi"));
    snprintf(path, sizeof path, "%s/content/components/Cell.kdl", root);
    write_whole(path, "name \"Cell\"\n(array)fields {\n    - name=\"x\" type=\"int\"\n}\n");
    snprintf(path, sizeof path, "%s/content/components/Cell.chibi", root);
    write_whole(path, "component Cell {\n    y: int\n}\n");
    snprintf(path, sizeof path, "%s/" CHIBI_PROJECT_MANIFEST, root);
    write_whole(path, manifest);
    REQUIRE(chibi_project_migrate(root, error, sizeof error) < 0);
    REQUIRE(strstr(error, "Cell.chibi already exists"));
    return true;
}
/* Migrates a version 20 material whose Surface Output, node 1, implies its
 * pin IDs. The pins version 21 appends would continue them at 10, which is
 * the Time node's ID, so the material only loads once they take IDs from its
 * counter. */
TEST_CASE(program_version_20_surface_outputs_take_counter_pin_ids)
{
    const char
        *material = "version 3\n"
                    "name \"Glow\"\n"
                    "domain \"material\"\n"
                    "id_counter 12\n"
                    "(array)graphs {\n"
                    "    - {\n"
                    "        kind \"event\"\n"
                    "        name \"\"\n"
                    "        (array)nodes {\n"
                    "            - id=1 type=\"Shader Surface Output\"\n"
                    "            - id=10 type=\"Shader Time\"\n"
                    "        }\n"
                    "        (array)edges {\n"
                    "            - from=\"10.Value\" to=\"1.Roughness\"\n"
                    "        }\n"
                    "    }\n"
                    "}\n";
    char root[] = "/tmp/chibi-migrate-XXXXXX", path[256], error[256];
    REQUIRE(mkdtemp(root));
    const char *directories[] = { "content", "content/materials" };
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/%s", root, directories[i]);
        REQUIRE(!mkdir(path, 0700));
    }
    const char *files[][2] = {
        { CHIBI_PROJECT_MANIFEST, "version 20\ncontent \"content\"\nstart_scene \"scenes/Start.kdl\"\n" },
        { "content/materials/Glow.kdl", material },
    };
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i][0]);
        FILE *file = fopen(path, "wb");
        fputs(files[i][1], file);
        fclose(file);
    }
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == CHIBI_PROJECT_VERSION - 20);
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == 0);
    char text[2048];
    FILE *file = fopen(path, "rb");
    text[fread(text, 1, sizeof text - 1, file)] = 0;
    fclose(file);
    const char
        *expected = "version 3\n"
                    "name \"Glow\"\n"
                    "domain \"material\"\n"
                    "id_counter 15\n"
                    "(array)graphs {\n"
                    "    - {\n"
                    "        kind \"event\"\n"
                    "        name \"\"\n"
                    "        (array)nodes {\n"
                    "            - {\n"
                    "                id 1\n"
                    "                type \"Shader Surface Output\"\n"
                    "                inputs {\n"
                    "                    Occlusion id=12\n"
                    "                    \"Alpha Mode\" id=13\n"
                    "                    \"Alpha Cutoff\" id=14\n"
                    "                }\n"
                    "            }\n"
                    "            - id=10 type=\"Shader Time\"\n"
                    "        }\n"
                    "        (array)edges {\n"
                    "            - from=\"10.Value\" to=\"1.Roughness\"\n"
                    "        }\n"
                    "    }\n"
                    "}\n";
    if (strcmp(text, expected)) {
        fprintf(stderr, "migrated Glow.kdl:\n%s", text);
    }
    REQUIRE(!strcmp(text, expected));
    const registry_pack **previous = NULL;
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous, registry_pack_at(i));
    }
    registry_set_packs((const registry_pack *[]){ &SHADER_PACK }, 1);
    behavior_asset *before = behavior_asset_load(material),
                   *after = behavior_asset_load(text);
    registry_set_packs(previous, acount(previous));
    afree(previous);
    const node *output = after ? graph_node_by_id(after->graph, 1) : NULL;
    bool fresh = output && output->input_pins[SHADER_SURFACE_OCCLUSION].id == 12 &&
                 output->input_pins[SHADER_SURFACE_ALPHA_MODE].id == 13 &&
                 output->input_pins[SHADER_SURFACE_ALPHA_CUTOFF].id == 14 &&
                 after->graph->id_counter == 15;
    behavior_asset_free(before);
    behavior_asset_free(after);
    REQUIRE(!before && fresh);
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i][0]);
        unlink(path);
    }
    for (int i = 1; i >= 0; i--) {
        snprintf(path, sizeof path, "%s/%s", root, directories[i]);
        rmdir(path);
    }
    rmdir(root);
    return true;
}
/* Migrates a version 21 project whose prototype, scene and component
 * presentation name GameHRC2D fields version 22 removes. Version 22 fails to
 * load such assets, so the step drops those fields and keeps the rest. */
TEST_CASE(program_version_21_lighting_drops_lab_fields)
{
    char root[] = "/tmp/chibi-migrate-XXXXXX", path[256], error[256];
    REQUIRE(mkdtemp(root));
    const char *directories[] = { "content",
                                  "content/editor",
                                  "content/prototypes",
                                  "content/scenes" };
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s/%s", root, directories[i]);
        REQUIRE(!mkdir(path, 0700));
    }
    const char *files[][3] = {
        { CHIBI_PROJECT_MANIFEST,
          "version 21\ncontent \"content\"\nstart_scene \"scenes/Start.kdl\"\n",
          "version 21\ncontent \"content\"\nstart_scene "
          "\"scenes/Start.kdl\"\n" },
        { "content/prototypes/Lighting.kdl",
          "name \"Lighting\"\n"
          "components {\n"
          "    GAME.GameHRC2D ambient=0.08 min_resolution=0.25 "
          "particle_focal_length=300.0 "
          "resolution=0.5 trace_levels=3 upscale_mode=\"Edge aware\" "
          "upscale_threshold=0.1\n"
          "    Other min_resolution=0.75 trace_levels=8\n"
          "}\n"
          "detached_components {\n"
          "    GameHRC2D color_resolution=2 gather_resolution=1 "
          "surface_depth_emitters=1 surface_depth_near_detail=#true "
          "surface_thickness=48.0\n"
          "}\n",
          "name \"Lighting\"\n"
          "components {\n"
          "    GAME.GameHRC2D ambient=0.08 resolution=0.5\n"
          "    Other min_resolution=0.75 trace_levels=8\n"
          "}\n"
          "detached_components {\n"
          "    GameHRC2D surface_thickness=48.0\n"
          "}\n" },
        { "content/scenes/Start.kdl",
          "version 1\n"
          "name \"Start\"\n"
          "(array)entities {\n"
          "    - {\n"
          "        name \"Lighting\"\n"
          "        prototype \"Lighting\"\n"
          "        components {\n"
          "            GameHRC2D debug_view=\"Fluence\" horizon_cache=#true "
          "intensity=2.0\n"
          "        }\n"
          "        retained_components {\n"
          "            GAME.GameHRC2D horizon_fuse_relief=#true "
          "horizon_reproject=#true horizon_hierarchy=#true fog=0.2\n"
          "            Other horizon_cache=#true\n"
          "        }\n"
          "        (array)children {\n"
          "            - {\n"
          "                name \"Child\"\n"
          "                prototype \"Lighting\"\n"
          "                components {\n"
          "                    GAME.GameHRC2D intensity=0.5 trace_levels=4\n"
          "                }\n"
          "                retained_components {\n"
          "                    GameHRC2D ambient=0.1 horizon_cache=#true\n"
          "                    Other trace_levels=9\n"
          "                }\n"
          "            }\n"
          "        }\n"
          "    }\n"
          "}\n",
          "version 1\n"
          "name \"Start\"\n"
          "(array)entities {\n"
          "    - {\n"
          "        name \"Lighting\"\n"
          "        prototype \"Lighting\"\n"
          "        components {\n"
          "            GameHRC2D intensity=2.0\n"
          "        }\n"
          "        retained_components {\n"
          "            GAME.GameHRC2D fog=0.2\n"
          "            Other horizon_cache=#true\n"
          "        }\n"
          "        (array)children {\n"
          "            - {\n"
          "                name \"Child\"\n"
          "                prototype \"Lighting\"\n"
          "                components {\n"
          "                    GAME.GameHRC2D intensity=0.5\n"
          "                }\n"
          "                retained_components {\n"
          "                    GameHRC2D ambient=0.1\n"
          "                    Other trace_levels=9\n"
          "                }\n"
          "            }\n"
          "        }\n"
          "    }\n"
          "}\n" },
        { "content/editor/Components.kdl",
          "components {\n"
          "    GAME.GameHRC2D {\n"
          "        tab \"Material\"\n"
          "        fields {\n"
          "            min_resolution {\n"
          "                (array)range 0.1 1\n"
          "            }\n"
          "            target_fps unit=\"fps\"\n"
          "            trace_levels {\n"
          "                (array)range 1 5\n"
          "            }\n"
          "        }\n"
          "    }\n"
          "    GameHRC2D {\n"
          "        fields {\n"
          "            debug_view label=\"Diagnostic\"\n"
          "            intensity unit=\"gain\"\n"
          "        }\n"
          "    }\n"
          "    Other {\n"
          "        fields {\n"
          "            trace_levels unit=\"steps\"\n"
          "        }\n"
          "    }\n"
          "}\n",
          "components {\n"
          "    GAME.GameHRC2D {\n"
          "        tab \"Material\"\n"
          "        fields {\n"
          "            target_fps unit=\"fps\"\n"
          "        }\n"
          "    }\n"
          "    GameHRC2D {\n"
          "        fields {\n"
          "            intensity unit=\"gain\"\n"
          "        }\n"
          "    }\n"
          "    Other {\n"
          "        fields {\n"
          "            trace_levels unit=\"steps\"\n"
          "        }\n"
          "    }\n"
          "}\n" },
    };
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i][0]);
        write_whole(path, files[i][1]);
    }
    char content[256], scene[256];
    snprintf(content, sizeof content, "%s/content", root);
    snprintf(scene, sizeof scene, "%s/content/scenes/Start.kdl", root);
    REQUIRE(!chibi_project_migrate_content(content, scene, 21, error, sizeof error));
    REQUIRE(!chibi_project_migrate_content(content, scene, 21, error, sizeof error));
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i][0]);
        const char *text = read_whole(path);
        if (strcmp(text, files[i][2])) {
            fprintf(stderr, "migrated %s:\n%s", files[i][0], text);
        }
        REQUIRE(!strcmp(text, files[i][2]));
        unlink(path);
    }
    for (int i = 3; i >= 0; i--) {
        snprintf(path, sizeof path, "%s/%s", root, directories[i]);
        rmdir(path);
    }
    rmdir(root);
    return true;
}
TEST_CASE(program_version_22_prefixes_migrate_without_changing_data)
{
    const char
        *source = "// GameTransform and GAME.GameHealth are documented here.\n"
                  "behavior B {\n"
                  "    label: string = \"GAME.GameHealth\"\n"
                  "    other: GameCustom = null\n"
                  "    function game_custom() { }\n"
                  "    function get_game_transform_custom() { }\n"
                  "    pose: struct<\"GameTransform\"> = null\n"
                  "    lower: gametransform = null\n"
                  "    body: enum<\"GameBodyType\"> = \"Dynamic\"\n"
                  "    on Tick(dt: double) {\n"
                  "        p := get_game_transform()\n"
                  "        upper := GET_GAME_TRANSFORM()\n"
                  "        mixed := Get_game_transform()\n"
                  "        compact := GETGAMETRANSFORM()\n"
                  "        h := get_component(Component: \"GAME.GameHealth\")\n"
                  "        any := "
                  "has_entities_with_component(\"GAME.GameCollider\")\n"
                  "        count := count_matching_entities([], [{component: "
                  "\"GAME.GameHealth\"}])\n"
                  "        x := component(\"GAME.Custom\", \"value\")\n"
                  "        y := "
                  "convert<\"Unwrap_GameInput\">(get_game_input())\n"
                  "        game_move_transform(p, 1, 2)\n"
                  "        game_custom()\n"
                  "        get_game_transform_custom()\n"
                  "        self.GameTransform.x = 4\n"
                  "        e := self\n"
                  "        e.GameTransform.x = 5\n"
                  "    }\n"
                  "}\n";
    const char *
        expected = "// GameTransform and GAME.GameHealth are documented here.\n"
                   "behavior B {\n"
                   "    label: string = \"GAME.GameHealth\"\n"
                   "    other: GameCustom = null\n"
                   "    function game_custom() { }\n"
                   "    function get_game_transform_custom() { }\n"
                   "    pose: struct<\"Transform\"> = null\n"
                   "    lower: Transform = null\n"
                   "    body: enum<\"BodyType\"> = \"Dynamic\"\n"
                   "    on Tick(dt: double) {\n"
                   "        p := get_transform()\n"
                   "        upper := GET_TRANSFORM()\n"
                   "        mixed := Get_transform()\n"
                   "        compact := GETTRANSFORM()\n"
                   "        h := get_component(Component: \"Health\")\n"
                   "        any := has_entities_with_component(\"Collider\")\n"
                   "        count := count_matching_entities([], [{component: "
                   "\"Health\"}])\n"
                   "        x := component(\"Custom\", \"value\")\n"
                   "        y := convert<\"Unwrap_Input\">(get_input())\n"
                   "        move_transform(p, 1, 2)\n"
                   "        game_custom()\n"
                   "        get_game_transform_custom()\n"
                   "        self.Transform.x = 4\n"
                   "        e := self\n"
                   "        e.Transform.x = 5\n"
                   "    }\n"
                   "}\n";
    const char *asset = "// GAME.GameTransform keeps this comment.\n"
                        "name \"GAME.GameTransform\"\n"
                        "components {\n"
                        "    GAME.GameTransform x=4.0\n"
                        "    GAME.Custom label=\"GameTransform\"\n"
                        "    GameHealth current=3\n"
                        "}\n"
                        "(array)nodes {\n"
                        "    - type=\"Get_GameTransform\" "
                        "description=\"GameTransform\"\n"
                        "}\n";
    const char *asset_expected = "// GAME.GameTransform keeps this comment.\n"
                                 "name \"GAME.GameTransform\"\n"
                                 "components {\n"
                                 "    Transform x=4.0\n"
                                 "    Custom label=\"GameTransform\"\n"
                                 "    Health current=3\n"
                                 "}\n"
                                 "(array)nodes {\n"
                                 "    - type=\"Get_Transform\" "
                                 "description=\"GameTransform\"\n"
                                 "}\n";
    char error[256] = "";
    char *migrated = chibi_migrate_prefixes(source, true, error, sizeof error);
    REQUIRE(migrated && !strcmp(migrated, expected));
    free(migrated);
    migrated = chibi_migrate_prefixes(asset, false, error, sizeof error);
    REQUIRE(migrated && !strcmp(migrated, asset_expected));
    free(migrated);
    REQUIRE(!chibi_migrate_prefixes(expected, true, error, sizeof error));
    REQUIRE(!chibi_migrate_prefixes(asset_expected, false, error, sizeof error));
    const char *collision = "components {\nGAME.GameHealth current=1\nHealth "
                            "current=2\n}\n";
    REQUIRE(!chibi_migrate_prefixes(collision, false, error, sizeof error) && error[0]);

    char *current_expected = NULL;
    REQUIRE(!chibi_migrate_declarations(expected, &current_expected, error, sizeof error));
    REQUIRE(current_expected);
    char *retained = NULL, *current = NULL;
    REQUIRE(!chibi_migrate_retention(current_expected, &retained, error, sizeof error));
    REQUIRE(
        !chibi_migrate_syntax(retained ? retained : current_expected, &current, error, sizeof error)
    );
    REQUIRE(current);
    free(retained);
    free(current_expected);
    current_expected = current;
    current = NULL;
    REQUIRE(!chibi_migrate_handlers(current_expected, NULL, 0, &current, error, sizeof error));
    if (current) {
        free(current_expected);
        current_expected = current;
    }
    current = NULL;
    REQUIRE(!chibi_migrate_angle_forms(current_expected, &current, error, sizeof error));
    if (current) {
        free(current_expected);
        current_expected = current;
    }
    char root[] = "/tmp/chibi-prefix-migrate-XXXXXX", path[256];
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/content", root);
    REQUIRE(!mkdir(path, 0700));
    const char *files[][2] = {
        { "project.kdl", "version 22\ncontent \"content\"\nstart_scene \"Scene.kdl\"\n" },
        { "content/B.chibi", source },
        { "content/Scene.kdl", asset },
    };
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i][0]);
        FILE *file = fopen(path, "wb");
        REQUIRE(file);
        fputs(files[i][1], file);
        fclose(file);
    }
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == CHIBI_PROJECT_VERSION - 22);
    REQUIRE(!chibi_project_migrate(root, error, sizeof error));
    snprintf(path, sizeof path, "%s/content/B.chibi", root);
    FILE *file = fopen(path, "rb");
    REQUIRE(file);
    char text[2048];
    text[fread(text, 1, sizeof text - 1, file)] = '\0';
    fclose(file);
    REQUIRE(!strcmp(text, current_expected));
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i][0]);
        unlink(path);
    }
    snprintf(path, sizeof path, "%s/content", root);
    rmdir(path);
    rmdir(root);
    free(current_expected);
    return true;
}
TEST_CASE(program_version_22_prefixes_preserve_resolved_helpers)
{
    const char
        *source = "behavior B {\n"
                  "    function get_game_input() { }\n"
                  "    macro get_game_input_custom() -> () { entry Begin {} "
                  "exit Done; }\n"
                  "    on Tick(dt: double) {\n"
                  "        get_game_input()\n"
                  "        get_game_input_custom()\n"
                  "        get_game_transform_custom()\n"
                  "        get_game_transform_macro()\n"
                  "        \"motion_helpers\".\"get-game-transform-custom\"()\n"
                  "        \"Motion Helpers\".get_game_transform()\n"
                  "        \"Motion Helpers\".get_game_transform_macro()\n"
                  "        p := get_game_velocity()\n"
                  "        q := Get_GameTransform()\n"
                  "        Get_GameBodyType()\n"
                  "        Get_GameGlobalIllumination()\n"
                  "        self.GameTransform.x = 4\n"
                  "    }\n"
                  "}\n";
    const char
        *expected = "behavior B {\n"
                    "    function get_game_input() { }\n"
                    "    macro get_game_input_custom() -> () { entry Begin {} "
                    "exit Done; }\n"
                    "    on Tick(dt: double) {\n"
                    "        get_game_input()\n"
                    "        get_game_input_custom()\n"
                    "        get_game_transform_custom()\n"
                    "        get_game_transform_macro()\n"
                    "        "
                    "\"motion_helpers\".\"get-game-transform-custom\"()\n"
                    "        \"Motion Helpers\".get_game_transform()\n"
                    "        \"Motion Helpers\".get_game_transform_macro()\n"
                    "        p := get_velocity()\n"
                    "        q := Get_Transform()\n"
                    "        Get_GameBodyType()\n"
                    "        Get_GameGlobalIllumination()\n"
                    "        self.Transform.x = 4\n"
                    "    }\n"
                    "}\n";
    const char
        *library = "library \"Motion Helpers\" {\n"
                   "    function \"Get Game Transform Custom\"() { }\n"
                   "    function get_game_transform() { }\n"
                   "    function Get_GameBodyType() { }\n"
                   "    function Get_GameGlobalIllumination() { }\n"
                   "    function call_helpers() { get_game_transform(); }\n"
                   "    macro get_game_transform_macro() -> () { entry "
                   "Begin {} exit Done; }\n"
                   "}\n";
    char root[] = "/tmp/chibi-prefix-helpers-XXXXXX", path[256], error[256];
    char *current_expected = NULL, *current_library = NULL;
    REQUIRE(!chibi_migrate_declarations(expected, &current_expected, error, sizeof error));
    REQUIRE(!chibi_migrate_declarations(library, &current_library, error, sizeof error));
    REQUIRE(current_expected && current_library);
    char *retained = NULL, *current = NULL;
    REQUIRE(!chibi_migrate_retention(current_expected, &retained, error, sizeof error));
    REQUIRE(
        !chibi_migrate_syntax(retained ? retained : current_expected, &current, error, sizeof error)
    );
    REQUIRE(current);
    free(retained);
    free(current_expected);
    current_expected = current;
    retained = NULL;
    current = NULL;
    REQUIRE(!chibi_migrate_retention(current_library, &retained, error, sizeof error));
    REQUIRE(
        !chibi_migrate_syntax(retained ? retained : current_library, &current, error, sizeof error)
    );
    REQUIRE(current);
    free(retained);
    free(current_library);
    current_library = current;
    char **targets[] = { &current_expected, &current_library };
    for (int i = 0; i < 2; i++) {
        current = NULL;
        REQUIRE(!chibi_migrate_handlers(*targets[i], NULL, 0, &current, error, sizeof error));
        if (current) {
            free(*targets[i]);
            *targets[i] = current;
        }
    }
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/content", root);
    REQUIRE(!mkdir(path, 0700));
    const char *files[][2] = {
        { "project.kdl", "version 22\ncontent \"content\"\nstart_scene \"Scene.kdl\"\n" },
        { "content/B.chibi", source },
        { "content/Helpers.chibi", library },
        { "content/Scene.kdl", "version 1\nname \"Scene\"\n(array)entities\n" },
    };
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i][0]);
        FILE *file = fopen(path, "wb");
        REQUIRE(file);
        fputs(files[i][1], file);
        fclose(file);
    }
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == CHIBI_PROJECT_VERSION - 22);
    REQUIRE(!chibi_project_migrate(root, error, sizeof error));
    bool source_matches = false, library_matches = false;
    for (int i = 1; i <= 2; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i][0]);
        FILE *file = fopen(path, "rb");
        REQUIRE(file);
        char text[2048];
        text[fread(text, 1, sizeof text - 1, file)] = '\0';
        fclose(file);
        if (i == 1) {
            source_matches = !strcmp(text, current_expected);
            if (!source_matches) {
                fprintf(stderr, "Migrated helper source differs:\n%s", text);
            }
        }
        if (i == 2) {
            library_matches = !strcmp(text, current_library);
        }
    }
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i][0]);
        unlink(path);
    }
    snprintf(path, sizeof path, "%s/content", root);
    rmdir(path);
    rmdir(root);
    free(current_expected);
    free(current_library);
    REQUIRE(source_matches && library_matches);
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

TEST_CASE(program_version_23_declarations_and_switch_migrate)
{
    const char *source = "library Damage {\n"
                         "// function and match stay in comments.\n"
                         "pure /* keep */ function DamageFor(level: int) -> "
                         "(Result: int) {\n"
                         "label := \"function match default\";\n"
                         "match enum (level) { case 1 { return 3 } default { "
                         "return 4 } }\n"
                         "}\n"
                         "}";
    char error[192] = { 0 }, *migrated = NULL, *again = NULL;
    REQUIRE(!chibi_migrate_declarations(source, &migrated, error, sizeof error));
    REQUIRE(migrated);
    REQUIRE(strstr(migrated, "/* keep */ DamageFor :: pure fn(level: int)"));
    REQUIRE(strstr(migrated, "// function and match stay in comments."));
    REQUIRE(strstr(migrated, "\"function match default\""));
    REQUIRE(strstr(migrated, "switch  (level) { case 1: { return 3 } case: { return 4 } }"));
    char *retained = NULL, *current = NULL;
    REQUIRE(!chibi_migrate_retention(migrated, &retained, error, sizeof error));
    REQUIRE(!chibi_migrate_syntax(retained ? retained : migrated, &current, error, sizeof error));
    REQUIRE(current);
    free(retained);
    script_syntax *syntax = script_parse(current);
    REQUIRE(syntax->diagnostic_count == 0);
    script_syntax_free(syntax);
    REQUIRE(!chibi_migrate_declarations(migrated, &again, error, sizeof error));
    REQUIRE(!again);
    char root[] = "/tmp/chibi-declarations-XXXXXX", path[256], sidecar[256];
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/Damage.chibi", root);
    write_whole(path, source);
    /* A leftover sidecar from before they were dropped is left alone. */
    const char *leftover = "{\"version\":2,\"anchors\":[{\"start\":0}]}";
    snprintf(sidecar, sizeof sidecar, "%s.meta", path);
    write_whole(sidecar, leftover);
    REQUIRE(!chibi_project_migrate_content(root, NULL, 23, error, sizeof error));
    /* migrate_content advances exactly one version, unlike project_migrate. */
    const char *written = read_whole(path);
    REQUIRE(written && !strcmp(written, migrated));
    written = read_whole(sidecar);
    REQUIRE(written && !strcmp(written, leftover));
    REQUIRE(!unlink(path) && !unlink(sidecar));
    REQUIRE(!rmdir(root));
    free(migrated);
    free(current);
    REQUIRE(
        chibi_migrate_declarations("library Bad { function", &migrated, error, sizeof error) < 0
    );
    REQUIRE(!migrated);
    return true;
}

TEST_CASE(program_version_23_not_calls_migrate_with_native_identity)
{
    const script_catalog_parameter boolean = { .name = "Value",
                                               .type = { .storage = PIN_BOOL } };
    const script_catalog_capability negate = {
        .id = script_catalog_id("capability", "not"),
        .name = "not",
        .kind = SCRIPT_CAPABILITY_FUNCTION,
        .parameters = &boolean,
        .parameter_count = 1,
        .results = &boolean,
        .result_count = 1,
        .pure = true,
    };
    script_catalog native = catalog;
    native.capabilities = &negate;
    native.capability_count = 1;
    const char *inputs[] = {
        "library NativeNegation { function Evaluate(enabled: bool) -> (Result: "
        "bool) { "
        "Value: bool = not(Value: not(Value: enabled)); return not(Value) } }",
        "library AuthoredNegation { function \"not\"(Value: bool) -> (Result: "
        "bool) { return Value } "
        "function Evaluate(enabled: bool) -> (Result: bool) { return "
        "not(Value: enabled) } }",
    };
    const char *identities[] = { "NativeNegation", "AuthoredNegation" };
    for (int i = 0; i < 2; i++) {
        char error[256] = { 0 }, *declarations = NULL, *retained = NULL,
             *current = NULL;
        REQUIRE(!chibi_migrate_declarations(inputs[i], &declarations, error, sizeof error));
        REQUIRE(declarations && strstr(declarations, "\"not\"("));
        REQUIRE(!chibi_migrate_retention(declarations, &retained, error, sizeof error));
        int status = chibi_migrate_syntax(retained ? retained : declarations, &current, error, sizeof error);
        if (status) {
            fprintf(
                stderr,
                "%s syntax migration: %s\ninput: %s\n",
                identities[i],
                error,
                retained ? retained : declarations
            );
        }
        REQUIRE(!status);
        const char *modern = current  ? current
                           : retained ? retained
                                      : declarations;
        char *normalized = authoring_source_migrate_generated(
            modern,
            &native,
            identities[i],
            error,
            sizeof error
        );
        if (!normalized) {
            fprintf(stderr, "not migration: %s\n", error);
        }
        REQUIRE(normalized);
        REQUIRE(!strstr(normalized, "\"not\""));
        if (!i) {
            REQUIRE(strstr(normalized, "!"));
            REQUIRE(!strstr(normalized, "not("));
        } else {
            REQUIRE(strstr(normalized, "not("));
            REQUIRE(!strstr(normalized, "!"));
        }
        script_syntax *parsed = script_parse(normalized);
        REQUIRE(!parsed->diagnostic_count);
        script_analysis *checked = script_analyze_document(parsed, &native, identities[i]);
        REQUIRE(checked->valid);
        script_analysis_free(checked);
        script_syntax_free(parsed);
        free(normalized);
        free(current);
        free(retained);
        free(declarations);
    }
    return true;
}

TEST_CASE(program_version_24_retention_migrates_and_executes)
{
    const char *source = "behavior Retention {\n"
                         "implicit := 0.0\n"
                         "state inferred := 1\n"
                         "state typed: int = 2\n"
                         "state int backwards = 3\n"
                         "state \"quoted name\" := 4\n"
                         "Count :: fn() {\n"
                         "state calls: double = 0\n"
                         "calls += 1\n"
                         "implicit += calls\n"
                         "}\n"
                         "on Impulse(amount, enabled) { Count() }\n}\n";
    char error[192] = { 0 }, *migrated = NULL, *again = NULL;
    REQUIRE(!chibi_migrate_retention(source, &migrated, error, sizeof error));
    REQUIRE(migrated);
    REQUIRE(strstr(migrated, "implicit := 0"));
    REQUIRE(strstr(migrated, "inferred : retain = 1"));
    REQUIRE(strstr(migrated, "typed: retain int = 2"));
    REQUIRE(strstr(migrated, "backwards: retain int = 3"));
    REQUIRE(strstr(migrated, "\"quoted name\" : retain = 4"));
    REQUIRE(!chibi_migrate_retention(migrated, &again, error, sizeof error) && !again);
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int i = 0; i < 2; i++) {
        prepare();
        REQUIRE(compile_migrated(migrated, backends[i]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        pin_value payload[] = { { .type = PIN_F64 }, { .type = PIN_BOOL } };
        for (int event = 0; event < 2; event++) {
            REQUIRE(
                execution_instance_dispatch(instance, 11, payload, 2, event, 0, 1000) == FLOW_OK
            );
        }
        REQUIRE(execution_instance_state(instance, state_id("implicit"))->d == 3);
        REQUIRE(execution_instance_state(instance, state_id("inferred"))->i == 1);
    }
    free(migrated);

    const char
        *shared = "system Totals { shared total := 0; shared typed: int = 2; "
                  "shared int backwards = 3; total += 1; }";
    REQUIRE(!chibi_migrate_retention(shared, &migrated, error, sizeof error));
    REQUIRE(migrated && strstr(migrated, "total : retain(system) = 0"));
    REQUIRE(strstr(migrated, "typed: retain(system) int = 2"));
    REQUIRE(strstr(migrated, "backwards: retain(system) int = 3"));
    prepare();
    char *cleaned = NULL;
    REQUIRE(!chibi_migrate_syntax(migrated, &cleaned, error, sizeof error));
    syntax = script_parse(cleaned ? cleaned : migrated);
    free(cleaned);
    analysis = script_analyze(syntax, &catalog);
    REQUIRE(analysis->valid);
    bool found_shared = false;
    for (int i = 0; i < analysis->symbol_count; i++) {
        found_shared |= !strcmp(analysis->symbols[i].name, "total") &&
                        analysis->symbols[i].kind == SCRIPT_SHARED_STATE;
    }
    REQUIRE(found_shared);
    free(migrated);

    const char
        *comments = "behavior B {\n"
                    "state /* keep storage */ count /* keep name */ := 0\n"
                    "state\n int /* keep type */ typed = 1\n"
                    "sharedText := \"state shared\"\n"
                    "// state ignored := 9\n}\n";
    REQUIRE(!chibi_migrate_retention(comments, &migrated, error, sizeof error));
    REQUIRE(migrated && strstr(migrated, "/* keep storage */ count /* keep name */ : retain = 0"));
    REQUIRE(strstr(migrated, "/* keep type */ typed: retain int = 1"));
    REQUIRE(strstr(migrated, "sharedText := \"state shared\""));
    REQUIRE(strstr(migrated, "// state ignored := 9"));
    free(migrated);

    /* Version 23 emits version 24 source before retention moves to version 25. */
    const char *old = "behavior B { state n: int = 0; function Next() { "
                      "state saved: int = 1; n += saved; } }";
    REQUIRE(!chibi_migrate_declarations(old, &migrated, error, sizeof error));
    REQUIRE(migrated && strstr(migrated, "Next :: fn"));
    REQUIRE(!chibi_migrate_retention(migrated, &again, error, sizeof error));
    REQUIRE(again && strstr(again, "saved: retain int = 1"));
    cleaned = NULL;
    REQUIRE(!chibi_migrate_syntax(again, &cleaned, error, sizeof error));
    script_syntax *check = script_parse(cleaned ? cleaned : again);
    free(cleaned);
    REQUIRE(!check->diagnostic_count);
    script_syntax_free(check);
    free(migrated);
    free(again);
    migrated = NULL;
    REQUIRE(chibi_migrate_retention("behavior Bad { state", &migrated, error, sizeof error) < 0);
    REQUIRE(!migrated);
    return true;
}

TEST_CASE(program_version_24_retention_content_is_all_or_nothing)
{
    const char *source = "behavior B {\nstate /* keep */ int count = 0\n"
                         "on Impulse(amount, enabled) { count += 1 }\n}\n";
    char error[192] = { 0 }, *migrated = NULL;
    REQUIRE(!chibi_migrate_retention(source, &migrated, error, sizeof error));
    REQUIRE(migrated);
    char root[] = "/tmp/chibi-retention-XXXXXX", path[256];
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/B.chibi", root);
    FILE *file = fopen(path, "wb");
    REQUIRE(file && fputs(source, file) >= 0 && !fclose(file));
    /* One invalid source prevents every staged replacement in this step. */
    snprintf(path, sizeof path, "%s/Bad.chibi", root);
    FILE *bad = fopen(path, "wb");
    REQUIRE(bad && fputs("behavior Bad { state", bad) >= 0 && !fclose(bad));
    REQUIRE(chibi_project_migrate_content(root, NULL, 24, error, sizeof error) < 0);
    REQUIRE(!unlink(path));
    snprintf(path, sizeof path, "%s/B.chibi", root);
    FILE *original = fopen(path, "rb");
    REQUIRE(original);
    char unchanged[512];
    unchanged[fread(unchanged, 1, sizeof unchanged - 1, original)] = '\0';
    REQUIRE(!fclose(original) && !strcmp(unchanged, source));
    error[0] = '\0';
    REQUIRE(!chibi_project_migrate_content(root, NULL, 24, error, sizeof error));
    REQUIRE(!chibi_project_migrate_content(root, NULL, 24, error, sizeof error));
    file = fopen(path, "rb");
    REQUIRE(file);
    char written[512];
    written[fread(written, 1, sizeof written - 1, file)] = '\0';
    REQUIRE(!fclose(file) && !strcmp(written, migrated));
    REQUIRE(!unlink(path));
    REQUIRE(!rmdir(root));
    free(migrated);
    return true;
}

TEST_CASE(program_version_25_syntax_preserves_storage_and_comments)
{
    char action_error[192] = { 0 }, *action_migrated = NULL;
    REQUIRE(!chibi_migrate_syntax(
        "behavior Empty { action once = do_once() }",
        &action_migrated,
        action_error,
        sizeof action_error
    ));
    REQUIRE(
        action_migrated &&
        !strcmp(action_migrated, "Empty :: behavior { action once = do_once() }")
    );
    free(action_migrated);
    action_migrated = NULL;
    REQUIRE(!chibi_migrate_syntax(
        "behavior Choice { action inner = engine[\"Branch\"](Condition: true) "
        "}",
        &action_migrated,
        action_error,
        sizeof action_error
    ));
    REQUIRE(action_migrated && strstr(action_migrated, "action inner = branch_if(Condition: true)"));
    free(action_migrated);
    const char *source = "system /* header */ Totals {\n"
                         "total: retain(system) int = 0\n"
                         "typed: retain(system) int = 1\n"
                         "inferred: retain(system) = 2\n"
                         "shared: retain(system) int = 3\n"
                         "Advance :: fn(int amount) {\n"
                         "depth_limit /* budget */ 64\n"
                         "total += amount\n}\n"
                         "foreach (item in [1, 2]) { Advance(item) }\n}\n";
    char error[192] = { 0 }, *migrated = NULL, *again = NULL;
    REQUIRE(!chibi_migrate_syntax(source, &migrated, error, sizeof error));
    REQUIRE(migrated && strstr(migrated, "/* header */ Totals :: system()"));
    REQUIRE(strstr(migrated, "total: retain int = 0"));
    REQUIRE(strstr(migrated, "typed: retain int = 1"));
    REQUIRE(strstr(migrated, "inferred: retain = 2"));
    REQUIRE(strstr(migrated, "shared: retain int = 3"));
    REQUIRE(!strstr(migrated, "retain(system)"));
    REQUIRE(strstr(migrated, "Advance :: (amount: int)"));
    REQUIRE(strstr(migrated, "/* budget */"));
    REQUIRE(strstr(migrated, "for (item in [1, 2])"));
    REQUIRE(!chibi_migrate_syntax(migrated, &again, error, sizeof error) && !again);
    free(migrated);
    const char *per_row[] = {
        "system S { int visits = 0 }",
        "system S { visits: int = 0 }",
        "system S { visits := 0 }",
        "system S { visits: retain int = 0 }",
        "system S { Next :: fn() { visits: retain int = 0 } }"
    };
    for (size_t i = 0; i < sizeof per_row / sizeof *per_row; i++) {
        REQUIRE(chibi_migrate_syntax(per_row[i], &migrated, error, sizeof error) < 0);
        REQUIRE(!migrated && strstr(error, "Per-row system state"));
    }
    REQUIRE(
        !chibi_migrate_syntax("system S { Next :: fn() { visits: int = 0 } }", &migrated, error, sizeof error)
    );
    REQUIRE(migrated && strstr(migrated, "visits: int = 0"));
    free(migrated);
    const char
        *query = "system Motion query position: Position, velocity: Velocity "
                 "without Body { position.x += velocity.x }";
    REQUIRE(!chibi_migrate_syntax(query, &migrated, error, sizeof error));
    REQUIRE(migrated && strstr(migrated, "Motion :: system( position: Position, velocity: Velocity)"));
    free(migrated);
    REQUIRE(
        !chibi_migrate_syntax("behavior B { F :: fn() { depth_limit 0 } G :: fn() { depth_limit 32 } }", &migrated, error, sizeof error)
    );
    REQUIRE(migrated && !strstr(migrated, "depth_limit"));
    free(migrated);
    REQUIRE(
        chibi_migrate_syntax("behavior B { owner Other }", &migrated, error, sizeof error) < 0
    );
    REQUIRE(!migrated && strstr(error, "owner"));
    return true;
}

TEST_CASE(program_version_25_syntax_content_preserves_header_comments)
{
    const char *source = "// authored source\nbehavior /* header */ Counter { "
                         "count := 0 }";
    char error[192] = { 0 }, *migrated = NULL;
    REQUIRE(!chibi_migrate_syntax(source, &migrated, error, sizeof error) && migrated);
    REQUIRE(strstr(migrated, "// authored source") && strstr(migrated, "/* header */"));
    char root[] = "/tmp/chibi-syntax-XXXXXX", path[256];
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/B.chibi", root);
    FILE *file = fopen(path, "wb");
    REQUIRE(file && fputs(source, file) >= 0 && !fclose(file));
    REQUIRE(!chibi_project_migrate_content(root, NULL, 25, error, sizeof error));
    REQUIRE(!chibi_project_migrate_content(root, NULL, 25, error, sizeof error));
    file = fopen(path, "rb");
    REQUIRE(file);
    char written[1024];
    written[fread(written, 1, sizeof written - 1, file)] = '\0';
    REQUIRE(!fclose(file) && !strcmp(written, migrated));
    const char *per_row = "system S { visits: retain int = 0 }";
    file = fopen(path, "wb");
    REQUIRE(file && fputs(per_row, file) >= 0 && !fclose(file));
    REQUIRE(chibi_project_migrate_content(root, NULL, 25, error, sizeof error) < 0);
    REQUIRE(strstr(error, "B.chibi") && strstr(error, "Per-row system state"));
    file = fopen(path, "rb");
    REQUIRE(file);
    char unchanged[1024];
    unchanged[fread(unchanged, 1, sizeof unchanged - 1, file)] = '\0';
    REQUIRE(!fclose(file) && !strcmp(unchanged, per_row));
    REQUIRE(!unlink(path));
    REQUIRE(!rmdir(root));
    free(migrated);
    return true;
}

TEST_CASE(program_version_25_symbol_names_and_native_shadows_migrate)
{
    const char *source =
        "behavior \"Odd Behavior\" {\n"
        "\"stored count\": int = 0\n"
        "\"Apply Damage\" :: pure fn(\"Damage Amount\": int) {\n"
        "state.\"stored count\" += Damage_Amount\n}\n"
        "action spawn_prototype = engine[\"Spawn Prototype\"](\"Drone\")\n"
        "on Tick { print_string(\"Apply Damage\"); \"Apply Damage\"(1); "
        "target := engine[\"Self\"](); print_string(spawn_prototype.Entity) "
        "}\n}\n";
    char error[192] = { 0 }, *migrated = NULL, *again = NULL;
    REQUIRE(!chibi_migrate_syntax(source, &migrated, error, sizeof error));
    REQUIRE(migrated && strstr(migrated, "Odd_Behavior :: behavior"));
    REQUIRE(strstr(migrated, "stored_count: int = 0"));
    REQUIRE(strstr(migrated, "Apply_Damage :: (Damage_Amount: int)"));
    REQUIRE(strstr(migrated, "state.stored_count += Damage_Amount"));
    REQUIRE(strstr(migrated, "action spawn_prototype_action = spawn_prototype(Drone)"));
    REQUIRE(strstr(migrated, "print_string(\"Apply Damage\")"));
    REQUIRE(strstr(migrated, "target := self; print_string(spawn_prototype_action.Entity)"));
    REQUIRE(!chibi_migrate_syntax(migrated, &again, error, sizeof error) && !again);
    free(migrated);
    REQUIRE(
        chibi_migrate_syntax("behavior B { \"same name\" := 0; same_name := 1 }", &migrated, error, sizeof error) <
        0
    );
    REQUIRE(!migrated && strstr(error, "collide"));
    REQUIRE(
        chibi_migrate_syntax(
            "behavior B { action spawn_prototype = engine[\"Spawn "
            "Prototype\"](); spawn_prototype_action := 0 }",
            &migrated,
            error,
            sizeof error
        ) < 0
    );
    REQUIRE(!migrated && strstr(error, "collide"));
    REQUIRE(
        chibi_migrate_syntax("behavior B { on Tick { engine[\"??\"]() } }", &migrated, error, sizeof error) < 0
    );
    REQUIRE(!migrated && strstr(error, "capability"));
    REQUIRE(!chibi_migrate_syntax(
        "behavior B { left :: fn() {} on Tick { engine[\"Left\"](); "
        "self.Position.left = 1; consume(left: 2); left(); } }",
        &migrated,
        error,
        sizeof error
    ));
    REQUIRE(migrated && strstr(migrated, "left_function :: ()"));
    REQUIRE(strstr(migrated, "self.Position.left = 1"));
    REQUIRE(strstr(migrated, "consume(left: 2)"));
    REQUIRE(strstr(migrated, "left_function();"));
    free(migrated);
    return true;
}

TEST_CASE(program_version_25_reference_flow_crosses_library_parameters)
{
    const char *sources[] = {
        "library Effects { Launch :: fn(kind:name=\"\"){ spawn_prototype(kind) "
        "} "
        "Burst :: fn(kind:name=\"Drone\"){ Launch(kind) } "
        "Mixed :: fn(amount:int, kind:name){ Launch(kind) } "
        "TextOnly :: fn(Prototype:name=\"Label\"){ print_string(Prototype) } }",
        "behavior App { on Tick { label:=\"Drone\"; Effects.Launch(label); "
        "Effects.Burst(); "
        "Effects.Mixed(amount: 1, \"Drone\"); "
        "Effects.Launch(kind:name=\"Drone\"); "
        "add_component(\"Health\"); hit:=component(\"Health\",\"current\"); "
        "print_string(\"Drone\") } }"
    };
    char error[192] = { 0 }, root[] = "/tmp/chibi-reference-flow-XXXXXX", path[256];
    REQUIRE(mkdtemp(root));
    const char *files[] = { "Effects.chibi", "App.chibi" };
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i]);
        FILE *file = fopen(path, "wb");
        REQUIRE(file && fputs(sources[i], file) >= 0 && !fclose(file));
    }
    REQUIRE(!chibi_project_migrate_content(root, NULL, 25, error, sizeof error));
    REQUIRE(!chibi_project_migrate_content(root, NULL, 25, error, sizeof error));
    char contents[2][2048];
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i]);
        FILE *file = fopen(path, "rb");
        REQUIRE(file);
        contents[i][fread(contents[i], 1, sizeof contents[i] - 1, file)] = 0;
        REQUIRE(!fclose(file));
        REQUIRE(!unlink(path));
    }
    REQUIRE(strstr(contents[0], "Launch :: (kind:Prototype)"));
    REQUIRE(strstr(contents[0], "Burst :: (kind:Prototype=Drone)"));
    REQUIRE(strstr(contents[0], "Mixed :: (amount:int, kind:Prototype)"));
    REQUIRE(strstr(contents[0], "TextOnly :: (Prototype:name=\"Label\")"));
    REQUIRE(strstr(contents[1], "label: Prototype =Drone"));
    REQUIRE(strstr(contents[1], "Effects.Mixed(amount: 1, Drone)"));
    REQUIRE(strstr(contents[1], "Effects.Launch(kind:Prototype=Drone)"));
    REQUIRE(strstr(contents[1], "add_component(Health)"));
    REQUIRE(strstr(contents[1], "hit:=self.Health.current"));
    REQUIRE(strstr(contents[1], "print_string(\"Drone\")"));
    REQUIRE(!rmdir(root));
    char *migrated = NULL;
    REQUIRE(
        chibi_migrate_syntax(
            "behavior B { on Tick { label:=\"Health\"; spawn_prototype(label); "
            "add_component(label) } }",
            &migrated,
            error,
            sizeof error
        ) < 0
    );
    REQUIRE(!migrated && strstr(error, "incompatible"));
    REQUIRE(
        chibi_migrate_syntax(
            "behavior B { on Tick { label:=format_text(\"{}\",1); "
            "spawn_prototype(label) } }",
            &migrated,
            error,
            sizeof error
        ) < 0
    );
    REQUIRE(!migrated && strstr(error, "computed"));
    REQUIRE(!chibi_migrate_syntax(
        "behavior B { Show :: fn(message=\"Label\") { print_string(message) } "
        "on Tick { Show() } }",
        &migrated,
        error,
        sizeof error
    ));
    REQUIRE(migrated && strstr(migrated, "Show :: (message=\"Label\")"));
    free(migrated);
    migrated = NULL;
    const char *mixed_text[] = {
        "behavior B { on Tick { label:=\"Drone\"; spawn_prototype(label); "
        "print_string(label) } }",
        "behavior B { Show :: fn(message:string){ print_string(message) } "
        "on Tick { label:=\"Drone\"; spawn_prototype(label); Show(label) } }",
        "behavior B { on Tick { label:=\"Drone\"; text:=label; "
        "spawn_prototype(label); print_string(text) } }",
    };
    for (int i = 0; i < (int)(sizeof mixed_text / sizeof *mixed_text); i++) {
        REQUIRE(chibi_migrate_syntax(mixed_text[i], &migrated, error, sizeof error) < 0);
        REQUIRE(!migrated && strstr(error, "ordinary text"));
    }
    REQUIRE(
        chibi_migrate_syntax("behavior B { Launch :: fn(Ship:name=\"Ship\"){ spawn_prototype(Ship) } }", &migrated, error, sizeof error) <
        0
    );
    REQUIRE(!migrated && strstr(error, "captured"));
    return true;
}

/* Version 28 reads parameters by name: `entry.X` becomes `X`, a bare
 * `entry` becomes the callable's only parameter, and `after entry` stays. The
 * current grammar refuses `entry` as a value. */
TEST_CASE(program_version_27_entry_reads_migrate_to_parameter_names)
{
    const char *source = "Moves :: library {\n"
                         "Step :: (Speed: float, Scale: float) -> float {\n"
                         "    // entry.Speed stays in comments.\n"
                         "    return entry.Speed * entry.Scale\n"
                         "}\n"
                         "Twice :: (Value: float) -> float {\n"
                         "    return entry * 2\n"
                         "}\n"
                         "Across :: (p: Point) -> float {\n"
                         "    return entry.x\n"
                         "}\n"
                         "}";
    const char *expected = "Moves :: library {\n"
                           "Step :: (Speed: float, Scale: float) -> float {\n"
                           "    // entry.Speed stays in comments.\n"
                           "    return Speed * Scale\n"
                           "}\n"
                           "Twice :: (Value: float) -> float {\n"
                           "    return Value * 2\n"
                           "}\n"
                           "Across :: (p: Point) -> float {\n"
                           "    return p.x\n"
                           "}\n"
                           "}";
    char error[192] = { 0 }, *migrated = NULL, *unchanged = NULL;
    REQUIRE(!chibi_migrate_entry(source, &migrated, error, sizeof error));
    REQUIRE(migrated && !strcmp(migrated, expected));
    free(migrated);
    REQUIRE(!chibi_migrate_entry(expected, &unchanged, error, sizeof error));
    REQUIRE(!unchanged);
    const char *handler = "M :: library { macro Go() -> () { exit Then; after "
                          "entry { } } }";
    REQUIRE(!chibi_migrate_entry(handler, &unchanged, error, sizeof error));
    REQUIRE(!unchanged);
    REQUIRE(
        chibi_migrate_entry(
            "L :: library { F :: (a: int, b: int) -> int { return entry } }",
            &unchanged,
            error,
            sizeof error
        ) < 0
    );
    REQUIRE(!unchanged && strstr(error, "names no parameter of its callable"));
    REQUIRE(
        chibi_migrate_entry(
            "L :: library { F :: (a: int) -> int { a := entry.a; return a } }",
            &unchanged,
            error,
            sizeof error
        ) < 0
    );
    REQUIRE(!unchanged && strstr(error, "a binding of the same name hides"));
    const char *captured[] = {
        "L :: library { F :: (v: int) -> int { v := 2; return entry } }",
        "L :: library { F :: (i: int) -> int { for i in 0..3 { } return "
        "entry.i } }",
    };
    for (int i = 0; i < 2; i++) {
        REQUIRE(chibi_migrate_entry(captured[i], &unchanged, error, sizeof error) < 0);
        REQUIRE(!unchanged && strstr(error, "a binding of the same name hides"));
    }
    char root[] = "/tmp/chibi-entry-XXXXXX", path[256];
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/Moves.chibi", root);
    write_whole(path, source);
    REQUIRE(!chibi_project_migrate_content(root, NULL, 27, error, sizeof error));
    const char *text = read_whole(path);
    REQUIRE(text && !strcmp(text, expected));
    REQUIRE(!unlink(path));
    REQUIRE(!rmdir(root));
    script_syntax *refused = script_parse(
        "L :: library { F :: (a: int) -> int { return entry.a } }"
    );
    bool rejected = refused->diagnostic_count > 0;
    script_syntax_free(refused);
    REQUIRE(rejected);
    return true;
}
/* Version 30 handlers name the payload fields they read, typed from the
 * engine's version 29 events or the project's event declarations. */
TEST_CASE(program_version_29_handlers_migrate_to_named_payloads)
{
    const char *damage = "version 3\nname \"Damage\"\ndomain "
                         "\"event\"\n(array)variables {\n"
                         "    - id=2 name=\"Amount\" type=\"int\"\n"
                         "    - id=3 name=\"Source\" type=\"handle\" "
                         "type_name=\"ecs.entity\"\n}\n";
    const char *declarations[] = {
        damage,
        "version 3\nname \"Died\"\ndomain \"event\"\n(array)variables {\n"
        "    - id=1 name=\"Source\" type=\"handle\" "
        "type_name=\"ecs.entity\"\n}\n",
        "version 3\nname \"Big\"\ndomain \"event\"\n(array)variables {\n"
        "    - id=1 name=\"Count\" type=\"int64\"\n"
        "    - id=2 name=\"Tags\" type=\"int\" container=\"set\"\n"
        "    - id=3 name=\"Level\" type=\"int\" type_name=\"int\"\n}\n",
    };
    const struct
    {
        const char *source, *expected;
    } cases[] = {
        { "B :: behavior { x: float = 0; on Tick(dt) { x += dt } }", "B :: behavior { x: float = 0; on Tick :: (dt: float s) { x += dt } }" },
        { "B :: behavior { x: float = 0; on Tick as tick(DeltaSeconds: float = "
          "0.0) { x += tick.DeltaSeconds + "
          "DeltaSeconds } }",
          "B :: behavior { x: float = 0; on Tick :: (dt: float s) { x += dt + "
          "dt } }" },
        { "B :: behavior { on Contact_Begin { destroy(event.Other) } }", "B :: behavior { on Contact_Begin :: (Other: handle<ecs.entity>) { destroy(Other) } }" },
        { "B :: behavior { on Begin { } on Tick { } }",
          "B :: behavior { on Begin :: () { } on Tick :: () { } }" },
        { "B :: behavior { total: int = 0; on Damage as hit { total += "
          "hit.Amount } }",
          "B :: behavior { total: int = 0; on Damage :: (Amount: int) { total "
          "+= Amount } }" },
        { "B :: behavior { last: handle<ecs.entity> = 0; on Died as "
          "died(Source: handle<ecs.entity> = 0) { last = "
          "Source } }",
          "B :: behavior { last: handle<ecs.entity> = 0; on Died :: (Source: "
          "handle<ecs.entity>) { last = Source } }" },
        { "B :: behavior { on Repair as repair() { } }",
          "B :: behavior { on Repair :: () { } }" },
        /* `event.F` names a field before a listed parameter. */
        { "B :: behavior { on Contact_Begin(Point, Other) { last = "
          "event.Other; at = Other } }",
          "B :: behavior { on Contact_Begin :: (Other: handle<ecs.entity>, "
          "Point: struct<CF_V2>) { last = Other; at = Point } }" },
        { "B :: behavior { on Contact_Begin(point, other) { "
          "destroy(event.other) "
          "} }",
          "B :: behavior { on Contact_Begin :: (Other: handle<ecs.entity>) { "
          "destroy(Other) } }" },
        { "B :: behavior { on Contact_Begin(other, point) { last = event.Other "
          "} }",
          "B :: behavior { on Contact_Begin :: (Other: handle<ecs.entity>) { "
          "last = Other } }" },
        /* An empty list binds the fields' own names, and comments take no
         * position. */
        { "B :: behavior { on Damage() { total += Amount } }",
          "B :: behavior { on Damage :: (Amount: int) { total += Amount } }" },
        { "B :: behavior { on Damage as d() { total += d.Amount } }",
          "B :: behavior { on Damage :: (Amount: int) { total += Amount } }" },
        { "B :: behavior { on Contact_Begin(/* note */ o, p) { destroy(o) } }",
          "B :: behavior { on Contact_Begin :: (Other: handle<ecs.entity>) { "
          "destroy(Other) } }" },
        /* An alias declared in source names the event. */
        { "B :: behavior { event hurt = Damage; on hurt { total += Amount } }",
          "B :: behavior { event hurt = Damage; on hurt :: (Amount: int) { "
          "total += Amount } }" },
        { "B :: behavior { Watch :: () { event moved = Died; on moved(a) { "
          "last = a } } event moved = Damage; on moved(a, b) { total += a } }",
          "B :: behavior { Watch :: () { event moved = Died; on moved :: "
          "(Source: handle<ecs.entity>) { last = Source } } event moved = "
          "Damage; on moved :: (Amount: int) { total += Amount } }" },
        { "B :: behavior { event hurt = Damage; on hurt(a: int, s: "
          "handle<ecs.entity>) { total += a } }",
          "B :: behavior { event hurt = Damage; on hurt :: (Amount: int) { "
          "total += Amount } }" },
        /* An array element reads its value; a function's own parameter and a
         * named argument do not read an alias. */
        { "B :: behavior { on Damage(a, s) { values := [a, 1] } }", "B :: behavior { on Damage :: (Amount: int) { values := [Amount, 1] } }" },
        { "B :: behavior { Add :: (hit: int) { total += hit } on Damage as hit "
          "{ Add(hit: hit.Amount) } }",
          "B :: behavior { Add :: (hit: int) { total += hit } on Damage :: "
          "(Amount: int) { Add(hit: Amount) } }" },
        { "B :: behavior { on Vanished(Source: handle<ecs.entity>, Count: int) "
          "{ last = Source } }",
          "B :: behavior { on Vanished :: (Source: handle<ecs.entity>) { last "
          "= Source } }" },
        /* Every scalar spells as itself, and a field the migration cannot
         * spell stops only a handler that reads it. */
        { "B :: behavior { on Big { steps += Count + Level } }",
          "B :: behavior { on Big :: (Count: int64, Level: int) { steps += "
          "Count + Level } }" },
    };
    char error[256], *migrated = NULL;
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        if (chibi_migrate_handlers(cases[i].source, declarations, 3, &migrated, error, sizeof error)) {
            fprintf(stderr, "case %zu: %s\n", i, error);
        }
        if (!migrated || strcmp(migrated, cases[i].expected)) {
            fprintf(stderr, "case %zu: %s\n", i, migrated ? migrated : "(unchanged)");
        }
        REQUIRE(migrated && !strcmp(migrated, cases[i].expected));
        free(migrated);
        REQUIRE(
            !chibi_migrate_handlers(cases[i].expected, declarations, 3, &migrated, error, sizeof error)
        );
        REQUIRE(!migrated);
    }
    const struct
    {
        const char *source, *message;
    } refused[] = {
        { "B :: behavior { action show = Show(menu.Key); on Damage as menu { "
          "show() } }",
          "outside the handler" },
        { "B :: behavior { on Damage { Keep(event) } }", "the whole `event`" },
        { "B :: behavior { on Damage as hit { Keep(hit) } }", "the whole payload" },
        { "B :: behavior { x: float = 0; on Tick(t) { dt := 1.0; x += t } }",
          "declares a name that a payload field" },
        { "B :: behavior { Other: int = 0; on Contact_Begin(o) { Other = 1; "
          "destroy(o) } }",
          "another meaning" },
        { "B :: behavior { on Damage { Keep(event.Force) } }", "does not have" },
        /* A local, loop or pattern that rebinds a listed parameter. */
        { "B :: behavior { on Contact_Begin(o, p) { o := self; last = o } }",
          "declares a name that a payload parameter" },
        { "B :: behavior { on Contact_Begin(o, p) { for o in items { Keep(o) } "
          "} }",
          "declares a name that a payload parameter" },
        { "B :: behavior { on Contact_Begin(o, p) { if p is Point o { Keep(o) "
          "} } }",
          "declares a name that a payload parameter" },
        { "B :: behavior { on Damage as m { for m in items { total += m.Amount "
          "} } }",
          "its payload alias" },
        { "B :: behavior { Peek :: () -> float { if total > 0 { m := 1.0 } "
          "return m.x } on Damage as m { total += m.Amount } }",
          "outside the handler" },
        /* An event the migration cannot find has only the fields of a typed
         * list. */
        { "B :: behavior { on Vanished(source) { } }", "untyped payload field" },
        { "B :: behavior { on Vanished { Keep(event.Source) } }", "does not have" },
        { "B :: behavior { event a = Damage; event b = a; on b { } }", "alias of an alias" },
        { "B :: behavior { on Big { Keep(Tags) } }", "cannot spell" },
    };
    for (size_t i = 0; i < sizeof refused / sizeof *refused; i++) {
        REQUIRE(
            chibi_migrate_handlers(refused[i].source, declarations, 3, &migrated, error, sizeof error) < 0
        );
        if (!strstr(error, refused[i].message)) {
            fprintf(stderr, "refusal %zu: %s\n", i, error);
        }
        REQUIRE(!migrated && strstr(error, refused[i].message));
    }
    /* A project's own event declarations type its handlers' payloads. */
    char root[] = "/tmp/chibi-handlers-XXXXXX", path[300];
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/events", root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/events/Damage.kdl", root);
    write_whole(path, damage);
    snprintf(path, sizeof path, "%s/Hurt.chibi", root);
    write_whole(path, "Hurt :: behavior { total: int = 0; on Damage { total += Amount } }");
    REQUIRE(!chibi_project_migrate_content(root, NULL, 29, error, sizeof error));
    const char *text = read_whole(path);
    REQUIRE(
        text && !strcmp(text, "Hurt :: behavior { total: int = 0; on Damage :: (Amount: int) { total += Amount } }")
    );
    REQUIRE(!unlink(path));
    snprintf(path, sizeof path, "%s/events/Damage.kdl", root);
    REQUIRE(!unlink(path));
    snprintf(path, sizeof path, "%s/events", root);
    REQUIRE(!rmdir(path));
    REQUIRE(!rmdir(root));
    return true;
}
/* Version 31 reads the frame step only through a Tick handler's list or a
 * system's step clause. */
TEST_CASE(program_version_30_step_globals_move_into_tick_lists)
{
    const struct
    {
        const char *source, *expected;
    } cases[] = {
        { "B :: behavior { x: float = 0; on Tick :: () { x += dt } }", "B :: behavior { x: float = 0; on Tick :: (dt: float s) { x += dt } }" },
        { "B :: behavior { x: float = 0; on Tick :: () { x += delta + "
          "delta_time() } }",
          "B :: behavior { x: float = 0; on Tick :: (dt: float s) { x += dt + "
          "dt } }" },
        { "B :: behavior { x: float = 0; on Tick :: (dt: float s) { x += dt } "
          "}",
          NULL },
        { "S :: system(p: Position) { p.x += dt }",
          "S :: system(p: Position) on Tick :: (dt: float s) { p.x += dt }" },
        { "S :: system(p: Position) on Tick :: () { p.x += delta }",
          "S :: system(p: Position) on Tick :: (dt: float s) { p.x += dt }" },
        /* The catalog's spellings name the capability; a local declared later
         * or in an inner block hides only the reads in its scope. */
        { "B :: behavior { x: float = 0; on Tick :: () { x += DeltaTime() } }", "B :: behavior { x: float = 0; on Tick :: (dt: float s) { x += dt } }" },
        { "B :: behavior { x: float = 0; on Tick :: () { x += dt; if x > 1 { "
          "dt := 1; x += dt } } }",
          "B :: behavior { x: float = 0; on Tick :: (dt: float s) { x += dt; "
          "if x > 1 { dt := 1; x += dt } } }" },
        { "B :: behavior { x: float = 0; on Tick :: (dt: float s) { x += dt + "
          "delta } }",
          "B :: behavior { x: float = 0; on Tick :: (dt: float s) { x += dt + "
          "dt } }" },
        { "S :: system(t: Transform) { delta: retain float = 0; delta += 1; "
          "t.x = delta }",
          NULL },
        /* A parameter, local or state of that name reads no global. */
        { "L :: library { F :: (dt: float) -> float { return dt } }", NULL },
        { "B :: behavior { on Begin :: () { delta := 1; x := delta } }", NULL },
        { "B :: behavior { dt: float = 0; on Begin :: () { dt = 1 } }", NULL },
        { "B :: behavior { x: float = 0; on Tick :: () { other.dt = 1; F(dt: "
          "1) } }",
          NULL },
    };
    char error[256], *migrated = NULL;
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        if (chibi_migrate_step_globals(cases[i].source, &migrated, error, sizeof error)) {
            fprintf(stderr, "case %zu: %s\n", i, error);
        }
        bool same = cases[i].expected
                      ? migrated && !strcmp(migrated, cases[i].expected)
                      : !migrated;
        if (!same) {
            fprintf(stderr, "case %zu: %s\n", i, migrated ? migrated : "(unchanged)");
        }
        REQUIRE(same);
        free(migrated);
        migrated = NULL;
    }
    const struct
    {
        const char *source, *message;
    } refused[] = {
        { "B :: behavior { x: float = 0; F :: () { x += dt } }", "outside a Tick" },
        { "B :: behavior { x: float = 0; action go = Go(delta); on Tick :: () "
          "{ "
          "go() } }",
          "outside a Tick" },
        { "B :: behavior { x: float = 0; on Begin :: () { x = delta_time() } }",
          "outside a Tick" },
        { "B :: behavior { x: float = 0; action go = Go(); on Tick :: () { "
          "go() "
          "} after go { x += dt } }",
          "outside a Tick" },
        /* Renaming a read to `dt` must not capture another `dt`. */
        { "B :: behavior { x: float = 0; on Tick :: () { dt := 2.0; x += delta "
          "* "
          "dt } }",
          "another dt" },
        { "B :: behavior { x: float = 0; on Tick :: () { for dt in 0..3 { x += "
          "delta_time() } } }",
          "another dt" },
    };
    for (size_t i = 0; i < sizeof refused / sizeof *refused; i++) {
        REQUIRE(chibi_migrate_step_globals(refused[i].source, &migrated, error, sizeof error) < 0);
        if (!strstr(error, refused[i].message)) {
            fprintf(stderr, "refusal %zu: %s\n", i, error);
        }
        REQUIRE(!migrated && strstr(error, refused[i].message));
    }
    return true;
}
/* Version 32 calls array_add `append` and append `extend`, in every spelling
 * the catalog matches; methods already have those meanings. */
TEST_CASE(program_version_31_array_calls_take_their_new_names)
{
    const struct
    {
        const char *source, *expected;
    } cases[] = {
        { "L :: library { F :: (xs: int[]) -> int[] { return array_add(xs, 1) "
          "} }",
          "L :: library { F :: (xs: int[]) -> int[] { return append(xs, 1) } "
          "}" },
        { "L :: library { F :: (xs: int[], ys: int[]) -> int[] { return "
          "append(append(xs, ys), ArrayAdd(ys, 2)) } }",
          "L :: library { F :: (xs: int[], ys: int[]) -> int[] { return "
          "extend(extend(xs, ys), append(ys, 2)) } }" },
        { "L :: library { F :: (xs: int[]) -> int[] { xs.append(1); "
          "xs.extend([2]); return xs } }",
          NULL },
        /* A source that calls `extend` is already migrated. */
        { "L :: library { F :: (xs: int[]) -> int[] { return extend(append(xs, "
          "1), [2]) } }",
          NULL },
    };
    char error[256], *migrated = NULL;
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        if (chibi_migrate_array_calls(cases[i].source, &migrated, error, sizeof error)) {
            fprintf(stderr, "case %zu: %s\n", i, error);
        }
        bool same = cases[i].expected
                      ? migrated && !strcmp(migrated, cases[i].expected)
                      : !migrated;
        if (!same) {
            fprintf(stderr, "case %zu: %s\n", i, migrated ? migrated : "(unchanged)");
        }
        REQUIRE(same);
        free(migrated);
        migrated = NULL;
    }
    /* Calls of a function declared with one of those names would change
     * meaning. */
    const char *refused[] = {
        "L :: library { append :: (xs: int[]) -> int[] { return xs } }",
        "L :: library { Extend :: (xs: int[]) -> int[] { return xs } }",
        "L :: library { F :: (array_add: int) -> int { return array_add } }",
        "L :: library { F :: () -> int { append := 1; return append } }",
        "B :: behavior { action Append = Go(); on Begin :: () { Append() } }",
    };
    for (size_t i = 0; i < sizeof refused / sizeof *refused; i++) {
        REQUIRE(chibi_migrate_array_calls(refused[i], &migrated, error, sizeof error) < 0);
        if (!strstr(error, "named like an array function")) {
            fprintf(stderr, "refusal %zu: %s\n", i, error);
        }
        REQUIRE(!migrated && strstr(error, "named like an array function"));
    }
    return true;
}
/* Version 33 spells no type arguments in angle brackets; a state-setting
 * action is inlined where it runs, with its after body. */
TEST_CASE(program_version_32_angle_forms_take_version_33_spellings)
{
    const struct
    {
        const char *source, *expected;
    } cases[] = {
        { "L :: library { F :: (a: struct<CF_V2>, b: handle<ecs.entity>[], c: "
          "array<int>, d: task<double>) -> enum<Mode> { return Mode.A } }",
          "L :: library { F :: (a: CF_V2, b: entity[], c: int[], d: "
          "task(double)) -> Mode { return Mode.A } }" },
        { "B :: behavior { n: int = 0\n"
          "    on Tick :: (dt: float s) {\n"
          "        x := convert<To_Real>(n) + 1.0\n"
          "        await set_variable<n>(Value: int = get_variable<n>() + 1)\n"
          "        await send_event<Damage>(self, 3, Source: self)\n"
          "        send_event<Hit>(Amount: 2, Entity: self.Target.other)\n"
          "        aim := direction_to<Direction_To>(self.Transform, x)\n"
          "    }\n"
          "}",
          "B :: behavior { n: int = 0\n"
          "    on Tick :: (dt: float s) {\n"
          "        x := To_Real(n) + 1.0\n"
          "        n = n + 1\n"
          "        await emit self.Damage(3, Source: self)\n"
          "        emit self.Target.other.Hit(Amount: 2)\n"
          "        aim := direction_to(self.Transform, x)\n"
          "    }\n"
          "}" },
        { "B :: behavior {\n"
          "    Age: float = 0.0\n"
          "    action age_2 = set_variable<Age>(Age + 1.0)\n"
          "    on Tick :: (dt: float s) {\n"
          "        if dt > 0.0 {\n"
          "            age_2()\n"
          "        }\n"
          "    }\n"
          "    after age_2 {\n"
          "        if age_2.Value > 2.0 {\n"
          "            destroy_entity(self)\n"
          "        }\n"
          "        log(age_2.Value)\n"
          "    }\n"
          "}",
          "B :: behavior {\n"
          "    Age: float = 0.0\n"
          "    on Tick :: (dt: float s) {\n"
          "        if dt > 0.0 {\n"
          "            Age = Age + 1.0\n"
          "            if Age > 2.0 {\n"
          "                destroy_entity(self)\n"
          "            }\n"
          "            log(Age)\n"
          "        }\n"
          "    }\n"
          "}" },
        { "L :: library { F :: (a: CF_V2) -> int { return 1 } }", NULL },
        /* A rerun finds the version 33 spelling and leaves it. */
        { "B :: behavior { on Tick :: (dt: float s) { emit self.Damage(3) } }", NULL },
    };
    char error[256], *migrated = NULL;
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        if (chibi_migrate_angle_forms(cases[i].source, &migrated, error, sizeof error)) {
            fprintf(stderr, "case %zu: %s\n", i, error);
        }
        bool same = cases[i].expected
                      ? migrated && !strcmp(migrated, cases[i].expected)
                      : !migrated;
        if (!same) {
            fprintf(stderr, "case %zu: %s\n", i, migrated ? migrated : "(unchanged)");
        }
        REQUIRE(same);
        free(migrated);
        migrated = NULL;
    }
    const char *refused[] = {
        "L :: library { F :: (a: handle<Row>) {} }",
        "L :: library { F :: (a: task<int[]>) {} }",
        "L :: library { F :: (a: set<int>) {} }",
        "L :: library { F :: () -> int { return pick<Other>(1) } }",
        "B :: behavior { n: int = 0; on Tick :: (dt: float s) { "
        "log(set_variable<n>(1)) } }",
        "B :: behavior { on Tick :: (dt: float s) { send_event<Damage>(Amount: "
        "1) } }",
        "B :: behavior { n: int = 0; action set_n = set_variable<n>(1); on "
        "Tick :: (dt: float s) { set_n(2) } }",
        "L :: library { F :: (a: array<int | string>) {} }",
        "B :: behavior { on Tick :: (dt: float s) { "
        "send_event<game.Damage>(self, 1) } }",
        "B :: behavior { n: int = 0; action set_n = set_variable<n>(1); "
        "on Tick :: (dt: float s) { set_n() } after set_n { set_n() } }",
    };
    for (size_t i = 0; i < sizeof refused / sizeof *refused; i++) {
        REQUIRE(chibi_migrate_angle_forms(refused[i], &migrated, error, sizeof error) < 0);
        if (!strstr(error, "has no version 33 spelling")) {
            fprintf(stderr, "refusal %zu: %s\n", i, error);
        }
        REQUIRE(!migrated && strstr(error, "has no version 33 spelling"));
    }
    return true;
}
static int fail_first_finalization(const char *, const char *, int, void *user, char *error, size_t size)
{
    int *calls = user;
    if (!(*calls)++) {
        snprintf(error, size, "finalization failed");
        return -1;
    }
    return 0;
}
/* A rerun after finalization failed resumes after the steps that finished:
 * a migrated `append(xs, 1)` that only adds an item stays as it is. */
TEST_CASE(program_migration_retry_resumes_after_finished_steps)
{
    const char *source = "L :: library { F :: (xs: int[]) -> int[] { return "
                         "array_add(xs, 1) } }",
               *expected = "L :: library { F :: (xs: int[]) -> int[] { return "
                           "append(xs, 1) } }";
    char root[] = "/tmp/chibi-retry-XXXXXX", path[300], manifest[300],
         error[256] = { 0 };
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/content", root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/content/Adds.chibi", root);
    write_whole(path, source);
    snprintf(manifest, sizeof manifest, "%s/" CHIBI_PROJECT_MANIFEST, root);
    char text[96];
    snprintf(
        text,
        sizeof text,
        "version %d\ncontent \"content\"\nstart_scene \"Start.kdl\"\n",
        CHIBI_PROJECT_VERSION - 2
    );
    write_whole(manifest, text);
    int calls = 0;
    REQUIRE(
        chibi_project_migrate_finalized(root, fail_first_finalization, &calls, error, sizeof error) < 0
    );
    char marked[96];
    snprintf(
        marked,
        sizeof marked,
        "version %d\nmigrated %d\ncontent \"content\"\nstart_scene "
        "\"Start.kdl\"\n",
        CHIBI_PROJECT_VERSION - 2,
        CHIBI_PROJECT_VERSION
    );
    REQUIRE(!strcmp(read_whole(manifest), marked));
    REQUIRE(!strcmp(read_whole(path), expected));
    int resumed = chibi_project_migrate_finalized(root, fail_first_finalization, &calls, error, sizeof error);
    if (resumed != 2) {
        fprintf(stderr, "resumed %d: %s\n", resumed, error);
    }
    REQUIRE(resumed == 2);
    REQUIRE(calls == 2 && !strcmp(read_whole(path), expected));
    snprintf(text, sizeof text, "version %d\ncontent \"content\"\nstart_scene \"Start.kdl\"\n", CHIBI_PROJECT_VERSION);
    REQUIRE(!strcmp(read_whole(manifest), text));
    REQUIRE(!unlink(path) && !unlink(manifest));
    snprintf(path, sizeof path, "%s/content", root);
    REQUIRE(!rmdir(path) && !rmdir(root));
    return true;
}
/* An upgrade that fails after its steps wrote, for example in finalization,
 * leaves scripts at the current version under an older manifest. Its rerun
 * starts at that version, and every step leaves those scripts alone. */
TEST_CASE(program_migration_rerun_leaves_scripts_later_steps_migrated)
{
    const char *current = "Lead :: behavior {\n    on Tick :: (dt: float s) "
                          "{\n    }\n}\n";
    char root[] = "/tmp/chibi-rerun-XXXXXX", path[300], error[256] = { 0 };
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/Lead.chibi", root);
    write_whole(path, current);
    for (int from = 23; from < CHIBI_PROJECT_VERSION; from++) {
        if (chibi_project_migrate_content(root, NULL, from, error, sizeof error)) {
            fprintf(stderr, "step %d: %s\n", from, error);
        }
        REQUIRE(!error[0]);
        const char *text = read_whole(path);
        REQUIRE(text && !strcmp(text, current));
    }
    REQUIRE(!unlink(path));
    REQUIRE(!rmdir(root));
    return true;
}
/* Version 29 runs a function's own body: the statements of `after entry`
 * move after the function's statements, before the handlers that follow, and
 * macros keep theirs. The current analyzer refuses `after entry` in a
 * function. */
TEST_CASE(program_version_28_after_entry_moves_into_function_bodies)
{
    const char *source = "Moves :: library {\n"
                         "Unindented :: () -> int {\n"
                         "after entry { return 7 }\n"
                         "}\n"
                         "Blank :: () -> int {\n"
                         "after entry {\n"
                         "    return 7\n"
                         "\n"
                         "    // preserved\n"
                         "}\n"
                         "}\n"
                         "Empty :: () {\n"
                         "after entry { }\n"
                         "}\n"
                         "Step :: (Speed: float) -> float {\n"
                         "    scale := 2.0\n"
                         "    after entry {\n"
                         "        // Doubles the speed.\n"
                         "        if Speed > 1.0 {\n"
                         "            return Speed * scale\n"
                         "        }\n"
                         "        return 0.0\n"
                         "    }\n"
                         "}\n"
                         "Spawn :: (Prototype: Prototype) {\n"
                         "    action spawned = spawn_prototype(Prototype)\n"
                         "    after entry {\n"
                         "        spawned()\n"
                         "    }\n"
                         "    after spawned {\n"
                         "        return\n"
                         "    }\n"
                         "}\n"
                         "Last :: () {\n"
                         "    after entry { log('d') }\n"
                         "}\n"
                         "Once :: (Value: int) -> int { after entry { return "
                         "Value } }\n"
                         "}";
    const char *expected = "Moves :: library {\n"
                           "Unindented :: () -> int {\n"
                           "return 7\n"
                           "}\n"
                           "Blank :: () -> int {\n"
                           "return 7\n"
                           "\n"
                           "// preserved\n"
                           "}\n"
                           "Empty :: () {\n"
                           "}\n"
                           "Step :: (Speed: float) -> float {\n"
                           "    scale := 2.0\n"
                           "    // Doubles the speed.\n"
                           "    if Speed > 1.0 {\n"
                           "        return Speed * scale\n"
                           "    }\n"
                           "    return 0.0\n"
                           "}\n"
                           "Spawn :: (Prototype: Prototype) {\n"
                           "    action spawned = spawn_prototype(Prototype)\n"
                           "    spawned()\n"
                           "    after spawned {\n"
                           "        return\n"
                           "    }\n"
                           "}\n"
                           "Last :: () {\n"
                           "    log('d')\n"
                           "}\n"
                           "Once :: (Value: int) -> int { return Value }\n"
                           "}";
    char error[192] = { 0 }, *migrated = NULL, *unchanged = NULL;
    REQUIRE(!chibi_migrate_after_entry(source, &migrated, error, sizeof error));
    REQUIRE(migrated && !strcmp(migrated, expected));
    free(migrated);
    REQUIRE(!chibi_migrate_after_entry(expected, &unchanged, error, sizeof error));
    REQUIRE(!unchanged);
    const char *macro = "M :: library { macro Go() -> () { exit Then; after "
                        "entry { } } }";
    REQUIRE(!chibi_migrate_after_entry(macro, &unchanged, error, sizeof error));
    REQUIRE(!unchanged);
    const struct
    {
        const char *source, *message;
    } refused[] = {
        { "L :: library { F :: (a: int) { after entry { b := event }\n} }", "reads `event`" },
        { "L :: library { F :: (a: int) { after entry as e { b := e.a }\n} }",
          "names the payload" },
        { "L :: library { F :: (a: int) { b := 1; after entry { b := 2 }\n} }", "also uses" },
        { "L :: library { F :: (a: int) { after entry { a := 2 }\n} }", "also uses" },
        { "L :: library { F :: (a: int) { after entry { b := 1 } after entry { "
          "b := 2 }\n} }",
          "also uses" },
        { "L :: library { F :: (p: Prototype) { action spawned = "
          "spawn_prototype(p); "
          "after entry { spawned := 2 }\n} }",
          "also uses" },
        { "L :: library { F :: (a: int) { a = 2; after entry { b := a }\n} }",
          "assign or pass" },
        { "L :: library { F :: (p: Point) { p.x = 2; after entry { b := p }\n} "
          "}",
          "assign or pass" },
        { "L :: library { Bump :: (x: ref int) { x += 1 }\nF :: (v: int) -> "
          "int { Bump(v); "
          "after entry { return v }\n} }",
          "pass by reference" },
        { "L :: library { F :: (a: int) { b := 1; after entry { c := a } } }",
          "on one line" },
    };
    for (int i = 0; i < 10; i++) {
        REQUIRE(chibi_migrate_after_entry(refused[i].source, &unchanged, error, sizeof error) < 0);
        REQUIRE(!unchanged && strstr(error, refused[i].message));
    }
    /* A statement that reads a parameter, passes it to a callable taking
     * copies, or changes one the handler does not read, leaves the handler's
     * reads as they were. */
    const char *kept = "L :: library { F :: (a: int, b: int) -> int { b = 2; "
                       "Log(a); c := a; "
                       "after entry { return a + c }\n} }";
    REQUIRE(!chibi_migrate_after_entry(kept, &migrated, error, sizeof error));
    REQUIRE(migrated && strstr(migrated, "return a + c") && !strstr(migrated, "after entry"));
    free(migrated);
    /* A call names its own library's callable or a qualified one, so a
     * callable of the same name in another library takes copies. */
    char project[] = "/tmp/chibi-after-entry-ref-XXXXXX", file[288];
    REQUIRE(mkdtemp(project));
    const char *names[] = { "W.chibi", "C.chibi", "L.chibi" };
    const char *libraries[] = {
        "W :: library { Bump :: (x: ref int) { x += 1 } }",
        "C :: library { Bump :: (x: int) { } }",
        "L :: library { F :: (v: int) { C.Bump(v); after entry { G(v) }\n} }",
    };
    for (int i = 0; i < 3; i++) {
        snprintf(file, sizeof file, "%s/%s", project, names[i]);
        write_whole(file, libraries[i]);
    }
    REQUIRE(!chibi_project_migrate_content(project, NULL, 28, error, sizeof error));
    write_whole(file, "L :: library { F :: (v: int) { W.Bump(v); after entry { G(v) }\n} }");
    REQUIRE(chibi_project_migrate_content(project, NULL, 28, error, sizeof error) < 0);
    REQUIRE(strstr(error, "pass by reference"));
    for (int i = 0; i < 3; i++) {
        snprintf(file, sizeof file, "%s/%s", project, names[i]);
        REQUIRE(!unlink(file));
    }
    REQUIRE(!rmdir(project));
    char root[] = "/tmp/chibi-after-entry-XXXXXX", path[256];
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/Moves.chibi", root);
    write_whole(path, source);
    REQUIRE(!chibi_project_migrate_content(root, NULL, 28, error, sizeof error));
    const char *text = read_whole(path);
    REQUIRE(text && !strcmp(text, expected));
    REQUIRE(!unlink(path));
    REQUIRE(!rmdir(root));
    prepare();
    syntax = script_parse("L :: library { F :: (a: int) -> int { after entry { return a } } }");
    analysis = script_analyze(syntax, &catalog);
    bool function_refused = false;
    for (int d = 0; d < analysis->diagnostic_count; d++) {
        function_refused |= strstr(analysis->diagnostics[d].message, "A function runs its own body") !=
                            NULL;
    }
    REQUIRE(function_refused && !analysis->valid);
    return true;
}
TEST_CASE(program_version_26_casts_migrate_to_conversions)
{
    const char
        *source = "Bits :: library {\n"
                  "Mask /* m */ :: cast<uint>(/* keep */ 255)\n"
                  "Shift :: cast<uint>(4)\n"
                  "// cast<int>(x) stays in comments.\n"
                  "Low :: (x: int, e: entity) -> byte {\n"
                  "    other := cast<handle<ecs.entity>>(e)\n"
                  "    return cast<byte>(/* low */ cast<uint>(x) & Mask)\n"
                  "}\n"
                  "}";
    const char *expected = "Bits :: library {\n"
                           "Mask /* m */ : uint : /* keep */ 255\n"
                           "Shift: uint : 4\n"
                           "// cast<int>(x) stays in comments.\n"
                           "Low :: (x: int, e: entity) -> byte {\n"
                           "    other := entity(e)\n"
                           "    return byte(/* low */ uint(x) & Mask)\n"
                           "}\n"
                           "}";
    char error[192] = { 0 }, *migrated = NULL, *unchanged = NULL;
    REQUIRE(!chibi_migrate_conversions(source, &migrated, error, sizeof error));
    REQUIRE(migrated && !strcmp(migrated, expected));
    /* A rerun after an interrupted step leaves converted typed constants. */
    REQUIRE(!chibi_migrate_conversions(expected, &unchanged, error, sizeof error));
    REQUIRE(!unchanged);
    REQUIRE(!chibi_migrate_conversions("L :: library { N :: 1 }", &unchanged, error, sizeof error));
    REQUIRE(!unchanged);
    REQUIRE(
        chibi_migrate_conversions(
            "L :: library { F :: (x: int) { y := cast<int[]>(x) } }",
            &unchanged,
            error,
            sizeof error
        ) < 0
    );
    REQUIRE(!unchanged && strstr(error, "no T(value) spelling"));
    char root[] = "/tmp/chibi-conversions-XXXXXX", path[256];
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/Bits.chibi", root);
    write_whole(path, source);
    REQUIRE(!chibi_project_migrate_content(root, NULL, 26, error, sizeof error));
    const char *text = read_whole(path);
    REQUIRE(text && !strcmp(text, expected));
    REQUIRE(!unlink(path));
    REQUIRE(!rmdir(root));
    free(migrated);
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
        REQUIRE(ecs_component_read_field(NULL, world, owner, 501, 0, &x) == ECS_OK && x.d == 7);
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
        REQUIRE(
            state_native && syntax && !syntax->diagnostic_count && analysis &&
            !analysis->valid
        );
        bool incompatible = false;
        for (int d = 0; d < analysis->diagnostic_count; d++) {
            incompatible |= strstr(analysis->diagnostics[d].message, "Incompatible types: expected double") !=
                            NULL;
        }
        REQUIRE(incompatible);
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
    const pin_string initial = { "A\0B", 3 };
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
    const pin_string changed = { "A\0C", 3 };
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
    pu_clear_setup();
}

void test_language_program()
{
    pu_setup(NULL, cleanup);
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
    RUN_TEST_CASE(program_integer_conversions_clamp_or_wrap);
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
    RUN_TEST_CASE(program_version_10_blocking_calls_migrate_to_awaits);
    RUN_TEST_CASE(program_handler_payloads_do_not_hide_parameters);
    RUN_TEST_CASE(program_nested_functions_wait_for_themselves);
    RUN_TEST_CASE(program_version_11_references_migrate_to_values);
    RUN_TEST_CASE(program_version_12_results_migrate_to_returns);
    RUN_TEST_CASE(program_version_13_values_migrate_to_their_expressions);
    RUN_TEST_CASE(program_version_14_scenes_migrate_to_own_behaviors);
    RUN_TEST_CASE(program_version_15_bindings_migrate_to_the_input_map);
    RUN_TEST_CASE(program_input_map_validates_actions);
    RUN_TEST_CASE(program_scene_children_are_referenced);
    RUN_TEST_CASE(program_input_actions_read_through_typed_names);
    RUN_TEST_CASE(program_scene_bindings_are_typed_names_before_play);
    RUN_TEST_CASE(program_version_16_locals_migrate_to_declarations);
    RUN_TEST_CASE(program_version_17_sdf_motion_is_removed);
    RUN_TEST_CASE(program_version_18_dotted_names_are_renamed);
    RUN_TEST_CASE(program_version_19_components_move_to_chibi);
    RUN_TEST_CASE(program_version_20_surface_outputs_take_counter_pin_ids);
    RUN_TEST_CASE(program_version_21_lighting_drops_lab_fields);
    RUN_TEST_CASE(program_version_22_prefixes_migrate_without_changing_data);
    RUN_TEST_CASE(program_version_22_prefixes_preserve_resolved_helpers);
    RUN_TEST_CASE(program_constant_definitions_execute);
    RUN_TEST_CASE(program_version_23_declarations_and_switch_migrate);
    RUN_TEST_CASE(program_version_23_not_calls_migrate_with_native_identity);
    RUN_TEST_CASE(program_version_24_retention_migrates_and_executes);
    RUN_TEST_CASE(program_version_24_retention_content_is_all_or_nothing);
    RUN_TEST_CASE(program_version_25_syntax_preserves_storage_and_comments);
    RUN_TEST_CASE(program_version_25_syntax_content_preserves_header_comments);
    RUN_TEST_CASE(program_version_25_symbol_names_and_native_shadows_migrate);
    RUN_TEST_CASE(program_version_25_reference_flow_crosses_library_parameters);
    RUN_TEST_CASE(program_version_26_casts_migrate_to_conversions);
    RUN_TEST_CASE(program_version_27_entry_reads_migrate_to_parameter_names);
    RUN_TEST_CASE(program_version_28_after_entry_moves_into_function_bodies);
    RUN_TEST_CASE(program_version_29_handlers_migrate_to_named_payloads);
    RUN_TEST_CASE(program_version_30_step_globals_move_into_tick_lists);
    RUN_TEST_CASE(program_version_31_array_calls_take_their_new_names);
    RUN_TEST_CASE(program_version_32_angle_forms_take_version_33_spellings);
    RUN_TEST_CASE(program_migration_retry_resumes_after_finished_steps);
    RUN_TEST_CASE(program_migration_rerun_leaves_scripts_later_steps_migrated);
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
