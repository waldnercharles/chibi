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

#include <stdio.h>
#include <string.h>

enum
{
    COMPONENT = 9841,
    ROOT = 9842,
    ENTITY = 9843,
    OWNER = 9844,
    RHS = 9845,
    SELF = 9849,
    READ = 9850
};
static type_field fields[2];
static type_desc type;
static const type_desc *const types[] = { &type };
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module *module;
static const behavior_function *body;
static ecs_world *world, *other;
static ecs_entity owner, another;
static behavior_function_context context;
static behavior_function_activation activation;
static arena storage;
static int mode, sequence, owner_calls, rhs_calls, store_calls;
static bool ordered, project_types;

static void release_run()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    context = (behavior_function_context){ 0 };
    ecs_world_free(world);
    ecs_world_free(other);
    world = other = NULL;
}
static void cleanup()
{
    release_run();
    execution_module_free(module);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    module = NULL;
    body = NULL;
    analysis = NULL;
    syntax = NULL;
    if (project_types) {
        type_catalog_set_project(NULL);
        project_types = false;
    }
}
static const char *get_owner(const behavior_function_context *, void *, const pin_value *, pin_value *result)
{
    assert(sequence == 0);
    sequence = 1;
    owner_calls++;
    *result = (pin_value){ .type = PIN_HANDLE, .u64 = ecs_entity_to_u64(owner) };
    return NULL;
}
static const char *get_rhs(const behavior_function_context *ctx, void *, const pin_value *, pin_value *result)
{
    assert(ctx == &context);
    assert(sequence == 1);
    sequence = 2;
    rhs_calls++;
    if (mode == 1) {
        context.world = NULL;
    } else if (mode == 2) {
        context.world = (const ecs_execution_view *)other;
    } else if (mode == 3) {
        assert(ecs_world_destroy_entity(world, owner) == ECS_OK);
    } else if (mode == 4) {
        assert(ecs_world_record_remove(world, owner, COMPONENT, 0) == ECS_OK);
        assert(ecs_world_apply_commands(world) == ECS_OK);
    } else if (mode == 5) {
        context.owner = ecs_entity_to_u64(another);
    }
    *result = (pin_value){ .type = PIN_I32, .i = 9 };
    return NULL;
}
static bool resolve(void *, uint64_t id, script_lower_host *out)
{
    static const node_host_call calls[] = { { .context = get_owner },
                                            { .context = get_rhs } };
    static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    if (id != OWNER && id != RHS) {
        return false;
    }
    *out = (script_lower_host){ .call = &calls[id == RHS], .access = &access };
    return true;
}
static bool resolve_global(void *, uint64_t id, behavior_function_instruction *out)
{
    if (id != SELF) {
        return false;
    }
    *out = (behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE };
    return true;
}
static bool compile(int bounded, bool explicit_owner, int fallback, behavior_backend backend)
{
    cleanup();
    fields[0] = (type_field){ .name = "value",
                              .type = PIN_I32,
                              .size = sizeof(int32_t),
                              .range_contract = bounded == 1,
                              .min = 0,
                              .max = 10 };
    fields[1] = fields[0];
    fields[1].name = "sibling";
    fields[1].offset = sizeof(int32_t);
    fields[1].range_contract = bounded == 2;
    type = (type_desc){ .name = "SourceWrite",
                        .size = 2 * sizeof(int32_t),
                        .fields = fields,
                        .field_count = 2 };
    const type_catalog_pack pack = { .types = types, .type_count = 1 };
    type_catalog_set_project(&pack);
    project_types = true;
    const script_catalog_field selected[] = {
        { .id = 9846,
          .name = fallback == 1 ? "unproven" : "value",
          .type = { .storage = PIN_I32 },
          .read = READ,
          .write = 9847,
          .writable = true,
          .range_contract = bounded == 1,
          .min = 0,
          .max = 10 },
        { .id = 9848,
          .name = "sibling",
          .type = { .storage = PIN_I32 },
          .offset = sizeof(int32_t),
          .writable = true,
          .range_contract = bounded == 2,
          .min = 0,
          .max = 10 }
    };
    const script_catalog_type root = { .id = ROOT,
                                       .name = type.name,
                                       .type = { .storage = PIN_STRUCT, .nominal = ROOT },
                                       .fields = selected,
                                       .field_count = 2,
                                       .size = type.size,
                                       .layout = type_layout_hash(&type),
                                       .component = true,
                                       .component_id = COMPONENT };
    const script_catalog_parameter parameters[] = {
        { .name = "entity", .type = { .storage = PIN_HANDLE, .nominal = ENTITY } },
        { .name = "value", .type = { .storage = PIN_I32 } }
    };
    const script_catalog_parameter result = { .name = "result",
                                              .type = fallback == 2
                                                        ? root.type
                                                        : selected[0].type };
    const script_catalog_parameter field_result = { .name = "result",
                                                    .type = selected[0].type };
    const script_catalog_capability capabilities[] = {
        { .id = 9847,
          .name = "set_value",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .parameters = explicit_owner ? parameters : parameters + 1,
          .parameter_count = explicit_owner ? 2 : 1,
          .results = &result,
          .result_count = 1,
          .component_access = SCRIPT_COMPONENT_ACCESS_WRITE,
          .component = ROOT,
          .component_field = selected[0].id,
          .component_entity = explicit_owner ? 0 : -1,
          .component_value = explicit_owner ? 1 : 0,
          .effects = SCRIPT_EFFECT_WRITE },
        { .id = OWNER,
          .name = "get_owner",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .results = parameters,
          .result_count = 1,
          .effects = SCRIPT_EFFECT_READ },
        { .id = RHS,
          .name = "get_rhs",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .results = parameters + 1,
          .result_count = 1,
          .effects = SCRIPT_EFFECT_WRITE },
        { .id = READ,
          .name = "read_value",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .results = &field_result,
          .result_count = 1,
          .component_access = SCRIPT_COMPONENT_ACCESS_READ,
          .component = ROOT,
          .component_field = selected[0].id,
          .component_entity = -1,
          .component_value = -1,
          .effects = SCRIPT_EFFECT_READ }
    };
    const script_catalog_global self = { .id = SELF,
                                         .name = "self",
                                         .type = { .storage = PIN_HANDLE,
                                                   .nominal = ENTITY } };
    const script_catalog catalog = { .types = &root,
                                     .type_count = 1,
                                     .capabilities = capabilities,
                                     .capability_count = 4,
                                     .globals = &self,
                                     .global_count = 1,
                                     .entity_type = ENTITY };
    char source[512];
    if (explicit_owner && !bounded) {
        snprintf(
            source,
            sizeof source,
            "Writes :: library { Run :: () -> int { "
            "get_owner().SourceWrite.value = get_rhs(); return 9; } }"
        );
    } else if (explicit_owner) {
        snprintf(
            source,
            sizeof source,
            "Writes :: library { Run :: () -> int { entity := get_owner(); "
            "value := get_rhs(); entity.SourceWrite.value = value; return %s; "
            "} }",
            bounded ? "entity.SourceWrite.value" : "value"
        );
    } else if (fallback == 2) {
        snprintf(
            source,
            sizeof source,
            "Writes :: library { Run :: (value:int) -> int { "
            "self.SourceWrite.value = value; root := self.SourceWrite; return "
            "root.value; } }"
        );
    } else {
        snprintf(
            source,
            sizeof source,
            "Writes :: library { Run :: (value:int) -> int { "
            "self.SourceWrite.%s = value; return %s; } }",
            selected[0].name,
            bounded ? "self.SourceWrite.value" : "value"
        );
    }
    syntax = script_parse(source);
    REQUIRE(syntax && !syntax->diagnostic_count);
    analysis = script_analyze(syntax, &catalog);
    for (int j = 0; analysis && j < analysis->diagnostic_count; j++) {
        fprintf(stderr, "write source semantic: %s\n", analysis->diagnostics[j].message);
    }
    REQUIRE(analysis && analysis->valid);
    const script_lower_bindings bindings = { .host = resolve, .global = resolve_global };
    execution_module_plan *plan = script_lower(analysis, &catalog, &bindings);
    for (int j = 0; plan && j < plan->diagnostic_count; j++) {
        fprintf(stderr, "write source lowering: %s\n", plan->diagnostics[j].message);
    }
    REQUIRE(plan && plan->valid);
    char error[192];
    module = execution_module_finish(&plan, backend, error, sizeof error);
    execution_module_plan_free(plan);
    if (!module) {
        fprintf(stderr, "write source publication: %s\n", error);
    }
    REQUIRE(module);
    for (int j = 0; j < execution_module_function_count(module); j++) {
        execution_module_function_info function = execution_module_function_at(module, j);
        if (function.kind == EXECUTION_FUNCTION_AUTHORED) {
            REQUIRE(!body);
            body = function.body;
        }
    }
    REQUIRE(body);
    ordered = explicit_owner;
    return true;
}
static int count(behavior_function_op op)
{
    int result = 0;
    for (int pc = 0; pc < acount(body->instructions); pc++) {
        if (body->instructions[pc].op == op) {
            assert(behavior_function_runtime_keeps(body, pc));
            result++;
        }
    }
    return result;
}
static bool make_world(ecs_world **destination, ecs_entity *first, ecs_entity *second)
{
    *destination = ecs_world_new();
    const ecs_component_desc component = { .id = COMPONENT,
                                           .key = "test.source_write",
                                           .name = type.name,
                                           .element_size = type.size,
                                           .type = &type };
    REQUIRE(ecs_world_register_component(*destination, &component, NULL) == ECS_OK);
    *first = ecs_world_create_entity(*destination);
    *second = ecs_world_create_entity(*destination);
    int32_t value[] = { 3, 0 };
    REQUIRE(ecs_world_record_add(*destination, *first, COMPONENT, &value, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(*destination, *second, COMPONENT, &value, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(*destination) == ECS_OK);
    return true;
}
static int store(
    behavior_function_context *ctx,
    uint64_t entity,
    uint64_t component,
    uint64_t layout,
    size_t offset,
    pin_value declared,
    pin_value value
)
{
    assert(sequence == (ordered ? 2 : 0));
    assert(entity == ecs_entity_to_u64(owner) && component == COMPONENT);
    assert(layout == type_layout_hash(&type) && offset == 0);
    assert(declared.type == PIN_I32 && value.type == PIN_I32 && value.i == 9);
    assert(declared.container == PIN_CONTAINER_SINGLE && !declared.type_name);
    ecs_world *current = (ecs_world *)ctx->world;
    const int32_t *before = ecs_component_data_const(current, owner, COMPONENT);
    assert(before && *before == 3); /* No speculative prewrite. */
    store_calls++;
    value.i = 37;
    return ecs_component_write_value(current, owner, component, offset, declared, value) == ECS_OK
             ? FLOW_OK
             : FLOW_ERR_ARGUMENT;
}
static int inspect(void *, behavior_function_context *, const behavior_source_point *)
{
    return 0;
}
static bool compiled_inspection(bool compiled, bool inspected)
{
#ifndef __EMSCRIPTEN__
    if (compiled && inspected) {
        REQUIRE(behavior_function_compile_inspection(body));
        REQUIRE(body->artifact->inspection_code);
    }
#else
    (void)compiled;
    (void)inspected;
#endif
    return true;
}
static bool begin(pin_value *output, pin_value *input, bool inspected)
{
    release_run();
    REQUIRE(make_world(&world, &owner, &another));
    ecs_entity first, second;
    REQUIRE(make_world(&other, &first, &second));
    REQUIRE(ecs_entity_to_u64(first) == ecs_entity_to_u64(owner));
    REQUIRE(ecs_entity_to_u64(second) == ecs_entity_to_u64(another));
    if (behavior_function_resumable(body)) {
        behavior_function_activation_init(body, &activation);
    }
    context = (behavior_function_context){ .world = (const ecs_execution_view *)world,
                                           .activation = &activation,
                                           .results = output,
                                           .arguments = input,
                                           .argument_count = input != NULL,
                                           .budget = 100,
                                           .store_component = store,
                                           .source_pause = inspected ? inspect : NULL };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(body, &context, NULL, ecs_entity_to_u64(owner)));
    sequence = owner_calls = rhs_calls = store_calls = 0;
    return true;
}
TEST_CASE(component_assignment_keeps_explicit_rhs_and_property_results)
{
    for (int bounded = 0; bounded < 3; bounded++) {
        for (int backend = 0; backend < 2; backend++) {
            REQUIRE(compile(bounded, false, 0, backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE));
            REQUIRE(
                count(BF_COMPONENT_WRITE) == 1 &&
                count(BF_COMPONENT_READ) == (bounded != 0)
            );
            for (int inspected = 0; inspected < 2; inspected++) {
                pin_value input = { .type = PIN_I32, .i = 9 },
                          output = { .type = PIN_I32, .i = -123 };
                REQUIRE(begin(&output, &input, inspected));
                REQUIRE(compiled_inspection(backend, inspected));
                context.value_storage = NULL;
                REQUIRE(behavior_function_run(body, &context, backend) == FLOW_OK);
                REQUIRE(!context.error_message && store_calls == 1);
                const int32_t *row = ecs_component_data_const(world, owner, COMPONENT);
                REQUIRE(row && *row == (bounded == 1 ? 10 : 37));
                REQUIRE(
                    output.i == (bounded == 1   ? 10
                                 : bounded == 2 ? 37
                                                : 9)
                );
            }
        }
    }
    return true;
}
TEST_CASE(component_write_evaluates_owner_then_rhs_then_revalidates_current_storage)
{
    for (int bounded = 0; bounded < 2; bounded++) {
        for (int backend = 0; backend < 2; backend++) {
            REQUIRE(compile(bounded, true, 0, backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE));
            REQUIRE(count(BF_COMPONENT_WRITE) == 1 && count(BF_COMPONENT_READ) == bounded);
            for (int inspected = 0; inspected < 2; inspected++) {
                for (mode = 0; mode < 6; mode++) {
                    pin_value output = { .type = PIN_I32, .i = -123 };
                    REQUIRE(begin(&output, NULL, inspected));
                    REQUIRE(compiled_inspection(backend, inspected));
                    bool unavailable = mode == 1 || mode == 3 || mode == 4;
                    REQUIRE(
                        behavior_function_run(body, &context, backend) ==
                        (unavailable ? FLOW_ERR_INVALID_GRAPH : FLOW_OK)
                    );
                    REQUIRE(owner_calls == 1 && rhs_calls == 1 && sequence == 2);
                    REQUIRE(store_calls == !unavailable);
                    if (unavailable) {
                        REQUIRE(output.i == -123 && context.failure == BEHAVIOR_FUNCTION_HOST_DATA);
                        REQUIRE(
                            context.error_message &&
                            !strcmp(context.error_message, "Referenced storage is unavailable")
                        );
                    } else {
                        REQUIRE(output.i == (bounded ? 10 : 9));
                        const int32_t *row = ecs_component_data_const(
                            mode == 2 ? other : world,
                            owner,
                            COMPONENT
                        );
                        REQUIRE(row && *row == (bounded ? 10 : 37));
                    }
                    const int32_t *untouched = ecs_component_data_const(
                        mode == 2 ? world : other,
                        owner,
                        COMPONENT
                    );
                    REQUIRE(untouched && *untouched == 3);
                    const int32_t *other_owner = ecs_component_data_const(world, another, COMPONENT);
                    REQUIRE(other_owner && *other_owner == 3);
                }
            }
        }
    }
    return true;
}
TEST_CASE(component_write_keeps_unproven_and_root_returning_setters_generic)
{
    for (int fallback = 1; fallback <= 2; fallback++) {
        for (int backend = 0; backend < 2; backend++) {
            REQUIRE(
                compile(false, false, fallback, backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE)
            );
            REQUIRE(count(BF_COMPONENT_WRITE) == 0);
            pin_value input = { .type = PIN_I32, .i = 9 },
                      output = { .type = PIN_I32, .i = -123 };
            REQUIRE(begin(&output, &input, false));
            REQUIRE(behavior_function_run(body, &context, backend) == FLOW_OK);
            REQUIRE(store_calls == 1 && output.i == (fallback == 2 ? 37 : 9));
        }
    }
    return true;
}
void test_language_component_write_source()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(component_assignment_keeps_explicit_rhs_and_property_results);
    RUN_TEST_CASE(component_write_evaluates_owner_then_rhs_then_revalidates_current_storage);
    RUN_TEST_CASE(component_write_keeps_unproven_and_root_returning_setters_generic);
    pu_clear_setup();
}
