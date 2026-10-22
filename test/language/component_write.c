#include "backend.h"
#include "ecs/ecs.h"
#include "ecs/execution.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/module.h"
#include "execution/status.h"
#include "language/lower.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"
#include "types/text.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum
{
    COMPONENT = 9801,
    ROW_TYPE = 9802,
    ENTITY_TYPE = 9803,
    SELF = 9804,
    EVENT = 9805
};
typedef struct
{
    bool flag;
    int32_t bounded;
    uint64_t wide;
    double real;
    const char *text;
} write_row;
static const type_field fields[] = {
    { .name = "flag",
      .type = PIN_BOOL,
      .offset = offsetof(write_row, flag),
      .size = sizeof(bool) },
    { .name = "bounded",
      .type = PIN_I32,
      .offset = offsetof(write_row, bounded),
      .size = sizeof(int32_t),
      .range_contract = true,
      .min = 0,
      .max = 10 },
    { .name = "wide",
      .type = PIN_U64,
      .offset = offsetof(write_row, wide),
      .size = sizeof(uint64_t) },
    { .name = "real",
      .type = PIN_F64,
      .offset = offsetof(write_row, real),
      .size = sizeof(double) },
    { .name = "text",
      .type = PIN_STR,
      .offset = offsetof(write_row, text),
      .size = sizeof(const char *) },
};
static const type_desc row_type = { .name = "CheckedWrite",
                                    .size = sizeof(write_row),
                                    .fields = fields,
                                    .field_count = 5 };
static const type_desc *const types[] = { &row_type };
static ecs_world *world;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module *module;
static execution_program *program;
static bool project_types;

static void cleanup()
{
    execution_program_free(program);
    execution_module_free(module);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    ecs_world_free(world);
    program = NULL;
    module = NULL;
    analysis = NULL;
    syntax = NULL;
    world = NULL;
    if (project_types) {
        type_catalog_set_project(NULL);
        project_types = false;
    }
}
static bool global(void *, uint64_t id, behavior_function_instruction *out)
{
    if (id != SELF) {
        return false;
    }
    *out = (behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE };
    return true;
}
static bool compile_field(int field, int form, behavior_backend backend)
{
    cleanup();
    const type_catalog_pack pack = { .types = types, .type_count = 1 };
    type_catalog_set_project(&pack);
    project_types = true;
    script_catalog_field selected[5] = { 0 };
    for (int j = 0; j < 5; j++) {
        selected[j] = (script_catalog_field){
            .id = (uint64_t)(9900 + j),
            .name = fields[j].name,
            .type = { .storage = fields[j].type },
            .offset = fields[j].offset,
            .writable = true,
            .c_string = fields[j].type == PIN_STR && !fields[j].text_descriptor,
            .range_contract = fields[j].range_contract,
            .min = fields[j].min,
            .max = fields[j].max
        };
    }
    const script_catalog_type root = { .id = ROW_TYPE,
                                       .name = row_type.name,
                                       .type = { .storage = PIN_STRUCT,
                                                 .nominal = ROW_TYPE },
                                       .fields = selected,
                                       .field_count = 5,
                                       .size = sizeof(write_row),
                                       .layout = type_layout_hash(&row_type),
                                       .component = true,
                                       .component_id = COMPONENT };
    const script_catalog_parameter parameter = { .name = "value",
                                                 .type = selected[field].type };
    const script_catalog_capability capabilities[] = {
        { .id = EVENT,
          .name = "Tick",
          .kind = SCRIPT_CAPABILITY_EVENT,
          .parameters = &parameter,
          .parameter_count = 1 },
        { .id = 9810,
          .name = "set_field",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .parameters = &parameter,
          .parameter_count = 1,
          .results = &parameter,
          .result_count = 1,
          .component_access = SCRIPT_COMPONENT_ACCESS_WRITE,
          .component = ROW_TYPE,
          .component_field = selected[field].id,
          .component_entity = -1,
          .component_value = 0,
          .effects = SCRIPT_EFFECT_WRITE }
    };
    const script_catalog_global self = { .id = SELF,
                                         .name = "self",
                                         .type = { .storage = PIN_HANDLE,
                                                   .nominal = ENTITY_TYPE } };
    const script_catalog catalog = { .types = &root,
                                     .type_count = 1,
                                     .capabilities = capabilities,
                                     .capability_count = 2,
                                     .globals = &self,
                                     .global_count = 1,
                                     .entity_type = ENTITY_TYPE };
    char source[512], statement[256];
    {
        snprintf(
            statement,
            sizeof statement,
            "self.CheckedWrite.%s %s value; answer = self.CheckedWrite.%s;",
            fields[field].name,
            form == 2 ? "+=" : "=",
            fields[field].name
        );
    }
    snprintf(
        source,
        sizeof source,
        "Writes :: behavior { answer: %s = %s; on Tick :: (value: %s) { %s } }",
        script_scalar_name(parameter.type.storage),
        field == 0   ? "false"
        : field == 4 ? "\"\""
                     : "0",
        script_scalar_name(parameter.type.storage),
        statement
    );
    if (form == 3) {
        snprintf(
            source,
            sizeof source,
            "Writes :: library { Run :: (value: %s) -> %s { "
            "self.CheckedWrite.%s = value; return self.CheckedWrite.%s; } }",
            script_scalar_name(parameter.type.storage),
            script_scalar_name(parameter.type.storage),
            fields[field].name,
            fields[field].name
        );
    }
    syntax = script_parse(source);
    REQUIRE(syntax && !syntax->diagnostic_count);
    analysis = script_analyze(syntax, &catalog);
    for (int j = 0; analysis && j < analysis->diagnostic_count; j++) {
        fprintf(stderr, "write semantic: %s\n", analysis->diagnostics[j].message);
    }
    REQUIRE(analysis && analysis->valid);
    const script_lower_bindings bindings = { .global = global };
    execution_module_plan *plan = script_lower(analysis, &catalog, &bindings);
    for (int j = 0; plan && j < plan->diagnostic_count; j++) {
        fprintf(stderr, "write lower: %s\n", plan->diagnostics[j].message);
    }
    REQUIRE(plan && plan->valid);
    char error[256];
    module = execution_module_finish(&plan, backend, error, sizeof error);
    execution_module_plan_free(plan);
    if (!module) {
        fprintf(stderr, "write publication: %s\n", error);
    }
    REQUIRE(module);
    world = ecs_world_new();
    const ecs_component_desc component = { .id = COMPONENT,
                                           .key = "test.checked_write",
                                           .name = row_type.name,
                                           .element_size = sizeof(write_row),
                                           .type = &row_type };
    REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_OK);
    REQUIRE(ecs_component_track_changes(world, COMPONENT) == ECS_OK);
    program = execution_program_new(module, world, NULL);
    REQUIRE(program);
    return true;
}
static const pin_value *answer(execution_instance *instance)
{
    for (int j = 0; j < analysis->symbol_count; j++) {
        if (analysis->symbols[j].kind == SCRIPT_STATE &&
            !strcmp(analysis->symbols[j].name, "answer")) {
            return execution_instance_state(instance, analysis->symbols[j].id);
        }
    }
    return NULL;
}
static void notified(ecs_world *, ecs_event, ecs_entity, ecs_component_id, void *user)
{
    (*(int *)user)++;
}
static bool create_row(ecs_entity *owner, execution_instance **instance)
{
    *owner = ecs_world_create_entity(world);
    write_row initial = { .bounded = 3, .wide = 1, .real = 2.5, .text = "original" };
    REQUIRE(ecs_world_record_add(world, *owner, COMPONENT, &initial, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    char retained[] = "owned sibling";
    REQUIRE(
        ecs_component_write_value(
            world,
            *owner,
            COMPONENT,
            fields[4].offset,
            (pin_value){ .type = PIN_STR },
            (pin_value){ .type = PIN_STR,
                         .text = &(const pin_string){ .bytes = retained,
                                                      .length = (uint32_t)strlen(retained) } }
        ) == ECS_OK
    );
    memset(retained, 'X', sizeof retained - 1);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    *instance = execution_program_attach(program, ecs_entity_to_u64(*owner));
    REQUIRE(*instance);
    return true;
}
TEST_CASE(component_write_keeps_scalar_ranges_versions_and_managed_siblings)
{
    const pin_value values[] = { { .type = PIN_BOOL, .b = true },
                                 { .type = PIN_I32, .i = 37 },
                                 { .type = PIN_U64, .u64 = UINT64_C(0xfedcba9876543210) },
                                 { .type = PIN_F64, .d = -0.0 } };
    for (int field = 0; field < 4; field++) {
        for (int backend = 0; backend < 2; backend++) {
            REQUIRE(compile_field(field, 0, backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE));
            ecs_entity owner;
            execution_instance *instance;
            REQUIRE(create_row(&owner, &instance));
            const write_row *row = ecs_component_data_const(world, owner, COMPONENT);
            const char *retained = row->text;
            int notifications = 0;
            REQUIRE(ecs_world_observe(world, ECS_ON_SET, COMPONENT, notified, &notifications) > 0);
            uint64_t version = ecs_component_version(world, owner, COMPONENT);
            for (int repeat = 0; repeat < 2; repeat++) {
                REQUIRE(
                    execution_instance_dispatch(instance, EVENT, &values[field], 1, 0, 0, 100) == FLOW_OK
                );
                REQUIRE(ecs_component_version(world, owner, COMPONENT) > version);
                version = ecs_component_version(world, owner, COMPONENT);
                REQUIRE(notifications == 0);
                row = ecs_component_data_const(world, owner, COMPONENT);
                REQUIRE(row->text == retained && !strcmp(row->text, "owned sibling"));
                pin_value expected = values[field];
                if (field == 1) {
                    expected.i = 10;
                }
                const pin_value *result = answer(instance);
                REQUIRE(result && result->type == expected.type);
                REQUIRE(!memcmp(&result->d, &expected.d, fields[field].size));
                REQUIRE(!memcmp(
                    (const char *)row + fields[field].offset,
                    &expected.d,
                    fields[field].size
                ));
            }
            REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
            REQUIRE(notifications == 1);
            REQUIRE(ecs_world_apply_commands(world) == ECS_OK && notifications == 1);
        }
    }
    return true;
}
TEST_CASE(component_write_preserves_ordinary_and_compound_reference_writes)
{
    for (int form = 1; form <= 2; form++) {
        for (int backend = 0; backend < 2; backend++) {
            REQUIRE(compile_field(1, form, backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE));
            ecs_entity owner;
            execution_instance *instance;
            REQUIRE(create_row(&owner, &instance));
            const pin_value value = { .type = PIN_I32, .i = 4 };
            REQUIRE(execution_instance_dispatch(instance, EVENT, &value, 1, 0, 0, 100) == FLOW_OK);
            const write_row *row = ecs_component_data_const(world, owner, COMPONENT);
            REQUIRE(row && row->bounded == (form == 1 ? 4 : 7));
            REQUIRE(!strcmp(row->text, "owned sibling"));
            const pin_value *result = answer(instance);
            REQUIRE(result && result->type == PIN_I32 && result->i == row->bounded);
        }
    }
    return true;
}
TEST_CASE(component_write_preserves_managed_field_publication)
{
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(compile_field(4, 0, backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE));
        ecs_entity owner;
        execution_instance *instance;
        REQUIRE(create_row(&owner, &instance));
        char text[] = "updated owned text";
        const pin_value value = { .type = PIN_STR,
                                  .text = &(const pin_string){
                                      .bytes = text,
                                      .length = (uint32_t)strlen(text) } };
        REQUIRE(execution_instance_dispatch(instance, EVENT, &value, 1, 0, 0, 100) == FLOW_OK);
        memset(text, 'X', sizeof text - 1);
        const write_row *row = ecs_component_data_const(world, owner, COMPONENT);
        const pin_value *result = answer(instance);
        REQUIRE(row && !strcmp(row->text, "updated owned text"));
        REQUIRE(
            result && result->type == PIN_STR &&
            !strcmp(pin_text(*result).bytes, "updated owned text")
        );
        REQUIRE(row->bounded == 3 && row->wide == 1 && row->real == 2.5);
    }
    return true;
}
static int hook_mode, hook_calls;
static int store(
    behavior_function_context *context,
    uint64_t owner,
    uint64_t component,
    uint64_t layout,
    size_t offset,
    pin_value declared,
    pin_value value
)
{
    hook_calls++;
    assert(component == COMPONENT && layout == type_layout_hash(&row_type));
    if (hook_mode == 1) {
        return FLOW_ERR_ARGUMENT;
    }
    int status = ecs_component_write_value(world, ecs_entity_from_u64(owner), component, offset, declared, value);
    if (hook_mode == 3) {
        context->world = NULL;
    }
    return hook_mode == 2 || status != ECS_OK ? FLOW_ERR_ARGUMENT : FLOW_OK;
}
TEST_CASE(component_write_failure_does_not_publish_results_or_undo_callback_effects)
{
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(compile_field(1, 3, backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE));
        const behavior_function *body = NULL;
        for (int j = 0; j < execution_module_function_count(module); j++) {
            execution_module_function_info info = execution_module_function_at(module, j);
            if (info.kind == EXECUTION_FUNCTION_AUTHORED) {
                REQUIRE(!body);
                body = info.body;
            }
        }
        REQUIRE(body);
        ecs_entity owner;
        execution_instance *instance;
        REQUIRE(create_row(&owner, &instance));
        for (int failure = 0; failure < 6; failure++) {
            arena storage = { 0 };
            behavior_function_activation activation = { 0 };
            const bool resumable = behavior_function_resumable(body);
            if (resumable) {
                behavior_function_activation_init(body, &activation);
            }
            pin_value input = { .type = PIN_I32, .i = 9 },
                      output = { .type = PIN_I32 };
            behavior_function_context context = {
                .world = (const ecs_execution_view *)world,
                .activation = resumable ? &activation : NULL,
                .arguments = &input,
                .argument_count = 1,
                .results = &output,
                .budget = 100,
                .store_component = store
            };
            behavior_function_bind_storage(&context, &storage);
            REQUIRE(behavior_function_bind(body, &context, NULL, ecs_entity_to_u64(owner)));
            REQUIRE(
                ecs_component_write_value(
                    world,
                    owner,
                    COMPONENT,
                    fields[1].offset,
                    input,
                    (pin_value){ .type = PIN_I32, .i = 3 }
                ) == ECS_OK
            );
            uint64_t before = ecs_component_version(world, owner, COMPONENT);
            hook_mode = failure == 3 ? 1
                      : failure == 4 ? 2
                      : failure == 5 ? 3
                                     : 0;
            hook_calls = 0;
            if (failure == 0) {
                context.world = NULL;
            }
            if (failure == 1) {
                context.owner ^= UINT64_C(1) << 32;
            }
            if (failure == 2) {
                context.store_component = NULL;
            }
            output.i = 12345;
            REQUIRE(behavior_function_run(body, &context, backend) == FLOW_ERR_INVALID_GRAPH);
            REQUIRE(output.i == 12345 && context.failure == BEHAVIOR_FUNCTION_HOST_DATA);
            const char
                *expected = failure == 0 || failure == 1 || failure == 5
                              ? "Referenced storage is unavailable"
                              : "Cannot retain referenced component write";
            REQUIRE(context.error_message && !strcmp(context.error_message, expected));
            REQUIRE(hook_calls == (failure >= 3));
            const write_row *row = ecs_component_data_const(world, owner, COMPONENT);
            REQUIRE(row && row->bounded == (failure >= 4 ? 9 : 3));
            REQUIRE(ecs_component_version(world, owner, COMPONENT) == before + (failure >= 4));
            behavior_function_release_host(&context);
            behavior_function_activation_free(&activation);
            arena_free(&storage);
        }
    }
    return true;
}
void test_language_component_write()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(component_write_keeps_scalar_ranges_versions_and_managed_siblings);
    RUN_TEST_CASE(component_write_preserves_ordinary_and_compound_reference_writes);
    RUN_TEST_CASE(component_write_preserves_managed_field_publication);
    RUN_TEST_CASE(component_write_failure_does_not_publish_results_or_undo_callback_effects);
    pu_clear_setup();
}
