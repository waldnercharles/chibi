#include "backend.h"
#include "ecs/ecs.h"
#include "execution/intrinsics.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "language/lower.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESULT_COMPONENT 8501
#define INPUT_COMPONENT 8502
#define ENTITY_TYPE 8503
#define REDUCTION_ROWS 8
#define REDUCTION_PREPARED_BYTES 65536
#define REDUCTION_BUDGET 4096

typedef struct
{
    double total, clamped, minimum, maximum;
    int32_t count;
    uint8_t small;
    float single;
    uint64_t wide;
} reduction_result;
typedef struct
{
    uint64_t target;
    double amount;
} reduction_input;
#define FIELD(structure, member, pin, spelling)                                \
    { .name = #member,                                                         \
      .type = pin,                                                             \
      .offset = offsetof(structure, member),                                   \
      .size = sizeof(((structure *)0)->member),                                \
      .type_name = spelling }
static const type_field result_fields[] = {
    FIELD(reduction_result, total, PIN_F64, "f64"),
    { .name = "clamped",
      .type = PIN_F64,
      .offset = offsetof(reduction_result, clamped),
      .size = sizeof(double),
      .type_name = "f64",
      .range_contract = true,
      .min = 0,
      .max = 10 },
    FIELD(reduction_result, minimum, PIN_F64, "f64"),
    FIELD(reduction_result, maximum, PIN_F64, "f64"),
    FIELD(reduction_result, count, PIN_I32, "int"),
    FIELD(reduction_result, small, PIN_U8, "uint8_t"),
    FIELD(reduction_result, single, PIN_F32, "number"),
    FIELD(reduction_result, wide, PIN_U64, "uint64_t"),
};
static const type_field input_fields[] = {
    FIELD(reduction_input, target, PIN_HANDLE, "ecs.entity"),
    FIELD(reduction_input, amount, PIN_F64, "f64"),
};
#undef FIELD
static const type_desc result_type = { .name = "ReductionResult",
                                       .size = sizeof(reduction_result),
                                       .fields = result_fields,
                                       .field_count = 8 };
static const type_desc input_type = { .name = "ReductionInput",
                                      .size = sizeof(reduction_input),
                                      .fields = input_fields,
                                      .field_count = 2 };
static const type_desc *const reflected[] = { &result_type, &input_type };
type_catalog_pack test_reductions_types()
{
    return (type_catalog_pack){ .types = reflected, .type_count = 2 };
}
static script_catalog_field fields[2][8];
static script_catalog_type types[2];
static script_catalog catalog;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static execution_module *module;
static execution_program *program;
static behavior_function_builder *builder;
static ecs_world *world;
static unsigned char *bytes;
static int rows;
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
static void cleanup()
{
    if (builder) {
        behavior_function_builder_free(builder);
        builder = NULL;
    }
    if (program) {
        execution_program_cancel_system(program);
    }
    execution_program_free(program);
    execution_module_free(module);
    execution_module_plan_free(plan);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    if (world) {
        ecs_world_free(world);
    }
    free(bytes);
    program = NULL;
    module = NULL;
    plan = NULL;
    analysis = NULL;
    syntax = NULL;
    world = NULL;
    bytes = NULL;
    rows = 0;
}
static void prepare()
{
    cleanup();
    world = ecs_world_new();
    for (int i = 0; i < 2; i++) {
        const type_desc *t = reflected[i];
        for (int j = 0; j < t->field_count; j++) {
            const type_field *f = &t->fields[j];
            fields[i][j] = (script_catalog_field){
                .id = (uint64_t)(1000 + i * 10 + j),
                .name = f->name,
                .type = { .storage = f->type,
                          .nominal = f->type == PIN_HANDLE ? ENTITY_TYPE : 0 },
                .offset = f->offset,
                .writable = true,
                .range_contract = f->range_contract,
                .min = f->min,
                .max = f->max
            };
        }
        types[i] = (script_catalog_type){
            .id = (uint64_t)(100 + i),
            .name = t->name,
            .type = { .storage = PIN_STRUCT, .nominal = (uint64_t)(100 + i) },
            .fields = fields[i],
            .field_count = t->field_count,
            .size = t->size,
            .layout = type_layout_hash(t),
            .component = true,
            .component_id = (uint64_t)(RESULT_COMPONENT + i)
        };
        ecs_component_desc component = { .id = types[i].component_id,
                                         .key = t->name,
                                         .name = t->name,
                                         .type = t,
                                         .element_size = t->size,
                                         .layout_hash = types[i].layout };
        assert(ecs_world_register_component(world, &component, NULL) == ECS_OK);
    }
    catalog = (script_catalog){ .types = types, .type_count = 2, .entity_type = ENTITY_TYPE };
}
static ecs_entity target(reduction_result value)
{
    ecs_entity entity = ecs_world_create_entity(world);
    assert(ecs_world_record_add(world, entity, RESULT_COMPONENT, &value, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return entity;
}
static void contribute(ecs_entity destination, double amount)
{
    assert(++rows <= REDUCTION_ROWS);
    ecs_entity entity = ecs_world_create_entity(world);
    reduction_input value = { ecs_entity_to_u64(destination), amount };
    assert(ecs_world_record_add(world, entity, INPUT_COMPONENT, &value, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
}
static const reduction_result *value(ecs_entity entity)
{
    return ecs_component_data_const(world, entity, RESULT_COMPONENT);
}
static bool link_type(void *, uint64_t id, execution_prepared_type *out)
{
    for (int i = 0; i < 2; i++) {
        if (types[i].id == id) {
            *out = (execution_prepared_type){ .name = types[i].name,
                                              .type = reflected[i],
                                              .storage = PIN_STRUCT,
                                              .size = types[i].size,
                                              .layout = types[i].layout };
            return true;
        }
    }
    if (id == ENTITY_TYPE) {
        *out = (execution_prepared_type){ .name = "ecs.entity",
                                          .storage = PIN_HANDLE,
                                          .size = sizeof(uint64_t) };
        return true;
    }
    return false;
}
static bool lower_source(const char *source)
{
    syntax = script_parse(source);
    analysis = script_analyze(syntax, &catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "reduction semantic: %s\n", analysis->diagnostics[i].message);
    }
    REQUIRE(analysis->valid);
    const script_lower_bindings bindings = { 0 };
    plan = script_lower(analysis, &catalog, &bindings);
    REQUIRE(plan && plan->valid);
    return true;
}
static bool compile_module(const char *source, behavior_backend backend, bool roundtrip)
{
    REQUIRE(behavior_backend_available(backend));
    REQUIRE(lower_source(source));
    char error[256];
    module = execution_module_finish(&plan, roundtrip ? BEHAVIOR_BYTECODE : backend, error, sizeof error);
    if (!module) {
        fprintf(stderr, "reduction finish: %s\n", error);
    }
    REQUIRE(module);
    script_analysis_free(analysis);
    analysis = NULL;
    script_syntax_free(syntax);
    syntax = NULL;
    if (roundtrip) {
        size_t length = 0;
        REQUIRE(execution_prepared_write(module, &bytes, &length, error, sizeof error));
        REQUIRE(length > 0 && length <= REDUCTION_PREPARED_BYTES);
        execution_module_free(module);
        module = NULL;
        const execution_prepared_linker linker = { .type = link_type };
        module = execution_prepared_read(bytes, length, &linker, backend, error, sizeof error);
        if (!module) {
            fprintf(stderr, "reduction prepared: %s\n", error);
        }
        REQUIRE(module);
        free(bytes);
        bytes = NULL;
    }
    REQUIRE(execution_module_backend(module) == backend);
    program = execution_program_new(module, world, NULL);
    REQUIRE(program);
    return true;
}
static bool compile(const char *source, behavior_backend backend, bool roundtrip)
{
    REQUIRE(compile_module(source, backend, roundtrip));
    execution_effect_summary effects = execution_module_effects(module);
    REQUIRE(effects.declared && effects.parallel_safe && effects.reduce_count > 0);
    REQUIRE(effects.write_count == 0);
    for (int i = 0; i < effects.reduce_count; i++) {
        REQUIRE(effects.reduces[i] == RESULT_COMPONENT);
    }
    return true;
}
TEST_CASE(reductions_prepared_ordered_fold_preserves_storage_and_clamps)
{
    const char
        *source = "Fold :: system(input: ^ReductionInput) {reduce "
                  "input.target.ReductionResult.total += "
                  "input.amount;reduce input.target.ReductionResult.clamped += "
                  "input.amount;reduce "
                  "input.target.ReductionResult.count += 1;reduce "
                  "input.target.ReductionResult.small += "
                  "1;reduce input.target.ReductionResult.single += 1;reduce "
                  "input.target.ReductionResult.wide += 1; }";
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        ecs_entity entity = target((reduction_result){
            .clamped = 8,
            .count = INT32_MAX,
            .small = 254,
            .single = 16777216.0f,
            .wide = UINT64_C(9007199254740992) });
        contribute(entity, 4);
        contribute(entity, -8);
        contribute(entity, 6);
        REQUIRE(compile(source, backends[backend], true));
        REQUIRE(execution_program_prepare_system(program, 1, 0, REDUCTION_BUDGET) == FLOW_OK);
        REQUIRE(execution_program_run_system(program) == FLOW_OK);
        REQUIRE(value(entity)->total == 0 && value(entity)->clamped == 8);
        execution_memory_usage pending = execution_program_memory_usage(program);
        REQUIRE(pending.reduction_contributions == 18 && pending.reduction_records == 18);
        REQUIRE(pending.reduction_bytes > 0);
        REQUIRE(pending.instance_count == 0 && pending.instance_bytes == 0);
        REQUIRE(pending.query_rows == 3);
        REQUIRE(pending.system_row_bytes >= 3 * (sizeof(uint64_t) + sizeof(pin_value)));
        REQUIRE(
            pending.system_row_bytes <= pending.system_row_capacity &&
            pending.payload_bytes > 0
        );
        REQUIRE(execution_program_finish_system(program) == FLOW_OK);
        const reduction_result *result = value(entity);
        REQUIRE(result->total == 2 && result->clamped == 8);
        REQUIRE(result->count == INT32_MIN + 2 && result->small == 1);
        REQUIRE(result->single == 16777216.0f);
        REQUIRE(result->wide == UINT64_C(9007199254740995));
        execution_memory_usage finished = execution_program_memory_usage(program);
        REQUIRE(finished.reduction_bytes == 0 && finished.system_row_bytes == 0);
        REQUIRE(finished.instance_count == 0 && finished.instance_bytes == 0);
        REQUIRE(finished.system_row_capacity > 0 && finished.system_row_capacity <= 64 * 1024);
    }
    return true;
}
TEST_CASE(reductions_unordered_opt_in_regroups_contributions)
{
    for (int backend = 0; backend < 2; backend++) {
        for (int unordered = 0; unordered < 2; unordered++) {
            prepare();
            ecs_entity entity = target((reduction_result){ .total = 1e16 });
            contribute(entity, -1e16);
            contribute(entity, 1);
            char source[192];
            snprintf(
                source,
                sizeof source,
                "Sum :: system(input: ^ReductionInput) { reduce "
                "%sinput.target.ReductionResult.total += input.amount; }",
                unordered ? "unordered " : ""
            );
            REQUIRE(compile(source, backends[backend], false));
            REQUIRE(execution_program_update(program, 1, 0, REDUCTION_BUDGET) == FLOW_OK);
            REQUIRE(value(entity)->total == (unordered ? 0 : 1));
            execution_memory_usage used = execution_program_memory_usage(program);
            REQUIRE(used.reduction_contributions == 2);
            REQUIRE(used.reduction_records == (uint64_t)(unordered ? 1 : 2));
        }
    }
    return true;
}
TEST_CASE(reductions_min_max_preserve_candidate_nan_and_signed_zero)
{
    const char
        *source = "Bounds :: system(input: ^ReductionInput) {reduce "
                  "min(input.target.ReductionResult.minimum, "
                  "input.amount);reduce "
                  "max(input.target.ReductionResult.maximum, input.amount); }";
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        ecs_entity negative = target((reduction_result){ 0 });
        ecs_entity positive = target((reduction_result){ .minimum = -0.0,
                                                         .maximum = -0.0 });
        ecs_entity nan = target((reduction_result){ .minimum = 3, .maximum = 3 });
        contribute(negative, NAN);
        contribute(negative, -0.0);
        contribute(positive, 0.0);
        contribute(nan, NAN);
        REQUIRE(compile(source, backends[backend], false));
        REQUIRE(execution_program_update(program, 1, 0, REDUCTION_BUDGET) == FLOW_OK);
        REQUIRE(value(negative)->minimum == 0 && signbit(value(negative)->minimum));
        REQUIRE(value(negative)->maximum == 0 && signbit(value(negative)->maximum));
        REQUIRE(value(positive)->minimum == 0 && !signbit(value(positive)->minimum));
        REQUIRE(value(positive)->maximum == 0 && !signbit(value(positive)->maximum));
        REQUIRE(isnan(value(nan)->minimum) && isnan(value(nan)->maximum));
    }
    return true;
}
TEST_CASE(reductions_failures_and_discard_do_not_publish_partial_results)
{
    const char *source = "Sum :: system(input: ^ReductionInput) {reduce "
                         "input.target.ReductionResult.total += "
                         "input.amount; }";
    for (int backend = 0; backend < 2; backend++) {
        for (int mode = 0; mode < 3; mode++) {
            prepare();
            ecs_entity first = target((reduction_result){ .total = 10 });
            ecs_entity last = target((reduction_result){ .total = 20 });
            contribute(first, 1);
            contribute(last, 2);
            REQUIRE(compile(source, backends[backend], false));
            if (mode == 0) {
                REQUIRE(ecs_world_destroy_entity(world, last) == ECS_OK);
            }
            REQUIRE(execution_program_prepare_system(program, 1, 0, REDUCTION_BUDGET) == FLOW_OK);
            int run = execution_program_run_system(program);
            REQUIRE(mode == 0 ? run < 0 : run == FLOW_OK);
            REQUIRE(value(first)->total == 10);
            if (mode == 1) {
                /* Deliberately violate the stable-world boundary: preflight
                 * must reject the late target before writing the first. */
                REQUIRE(ecs_world_destroy_entity(world, last) == ECS_OK);
            }
            if (mode == 2) {
                execution_program_discard_reductions(program);
            }
            int finished = execution_program_finish_system(program);
            REQUIRE(mode == 2 ? finished == FLOW_OK : finished < 0);
            REQUIRE(value(first)->total == 10);
            REQUIRE(execution_program_memory_usage(program).reduction_bytes == 0);
        }
    }
    return true;
}
TEST_CASE(reductions_scalar_helper_closure_streams_and_preserves_fallbacks)
{
    static const char *const sources[] = {
        "Sum :: system(input: ^ReductionInput) {Twice :: (amount: f64) -> "
        "f64 { return "
        "amount * 2; }Adjust :: (amount: f64) -> f64 { adjusted := "
        "Twice(amount);if adjusted "
        "> 0 { adjusted += 1; } return adjusted; }reduce "
        "input.target.ReductionResult.total += "
        "Adjust(input.amount); }",
        "Sum :: system(input: ^ReductionInput) {Read :: (extra: f64) -> "
        "f64 { if "
        "self.ReductionInput {return self.ReductionInput.amount + extra; } "
        "return extra; }reduce "
        "input.target.ReductionResult.total += Read(0); }",
        "Sum :: system(input: ^ReductionInput) {First :: (amounts: f64[]) "
        "-> f64 { return "
        "amounts[0]; }reduce input.target.ReductionResult.total += "
        "First([input.amount]); }",
        "Sum :: system(input: ^ReductionInput) {Unused :: (amount: f64) -> "
        "f64 { return "
        "amount * 2; }reduce input.target.ReductionResult.total += "
        "input.amount; }"
    };
    for (int backend = 0; backend < 2; backend++) {
        for (int example = 0; example < 4; example++) {
            prepare();
            ecs_entity entity = target((reduction_result){ 0 });
            contribute(entity, 2);
            contribute(entity, 3);
            REQUIRE(compile_module(sources[example], backends[backend], example == 0));
            REQUIRE(execution_module_function_count(module) == (example ? 2 : 3));
            execution_effect_summary effects = execution_module_effects(module);
            REQUIRE(effects.reduce_count > 0 && !effects.write_count);
            REQUIRE(effects.parallel_safe == (example == 0));
            if (!example) {
                REQUIRE(effects.declared && effects.deterministic);
            }
            REQUIRE(execution_program_update(program, 1, 0, REDUCTION_BUDGET) == FLOW_OK);
            REQUIRE(value(entity)->total == (example ? 5 : 12));
            execution_memory_usage used = execution_program_memory_usage(program);
            REQUIRE(used.query_rows == 2 && used.reduction_contributions == 2);
            if (!example) {
                REQUIRE(used.instance_count == 0 && used.instance_bytes == 0);
            }
        }
    }
    return true;
}
static int raw_value(behavior_function_instruction instruction, behavior_value_kind kind)
{
    int slot = behavior_function_builder_slot(builder, instruction.type);
    assert(behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, slot, kind));
    instruction.output = slot;
    behavior_function_builder_emit(builder, instruction);
    return slot;
}
static int raw_reference_call(
    const node_host_call *call,
    const pin_value *args,
    const int *slots,
    int count,
    int source
)
{
    bool make = call == &EXECUTION_reference_make_call;
    bool reference = call != &EXECUTION_reference_reduce_call;
    pin_value result = { .type = reference ? BF_POINTER_TYPE : PIN_BOOL };
    uint64_t id = execution_intrinsic_id(call);
    int host = behavior_function_builder_host(
        builder,
        call,
        execution_intrinsic_access(id),
        args,
        count,
        &result,
        1
    );
    assert(behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id)));
    int first = behavior_function_builder_host_first(builder, host);
    int output = behavior_function_builder_host_results(builder, host);
    assert(
        behavior_function_builder_kind(builder, BEHAVIOR_BANK_DATA, first, make ? BEHAVIOR_ADDRESS : BEHAVIOR_REFERENCE)
    );
    if (make) {
        assert(
            behavior_function_builder_kind(builder, BEHAVIOR_BANK_DATA, first + 4, BEHAVIOR_ADDRESS)
        );
    }
    assert(
        behavior_function_builder_kind(builder, BEHAVIOR_BANK_DATA, output, reference ? BEHAVIOR_REFERENCE : BEHAVIOR_VALUE)
    );
    for (int i = 0; i < count; i++) {
        if (slots[i] < 0) {
            continue;
        }
        int address = raw_value(
            (behavior_function_instruction){ .op = BF_HOST_SLOT,
                                             .type = BF_POINTER_TYPE,
                                             .left = first + i },
            BEHAVIOR_ADDRESS
        );
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_WRITE,
                                             .type = args[i].type,
                                             .left = slots[i],
                                             .right = address,
                                             .offset = offsetof(pin_value, d) }
        );
    }
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_HOST_CALL,
                                         .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                         .target = host,
                                         .source = source }
    );
    int address = raw_value(
        (behavior_function_instruction){ .op = BF_HOST_SLOT,
                                         .type = BF_POINTER_TYPE,
                                         .left = output },
        BEHAVIOR_ADDRESS
    );
    return raw_value(
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = result.type,
                                         .left = address,
                                         .offset = offsetof(pin_value, d) },
        reference ? BEHAVIOR_REFERENCE : BEHAVIOR_VALUE
    );
}
static behavior_function *raw_mixed_reductions()
{
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, NULL, 0, NULL, 0, NULL, 0);
    int source = behavior_function_builder_origin(
        builder,
        (
            behavior_function_source
        ){ .asset_name = "mixed-reduction-ir", .parent = -1, .line = 17, .column = 3 }
    );
    behavior_function_builder_component(builder, RESULT_COMPONENT, false, false);
    int context = raw_value(
        (behavior_function_instruction){ .op = BF_CONTEXT,
                                         .type = BF_POINTER_TYPE,
                                         .target = 1 },
        BEHAVIOR_ADDRESS
    );
    int owner = raw_value((behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE }, BEHAVIOR_VALUE);
    const pin_value root[] = {
        { .type = BF_POINTER_TYPE },
        { .type = PIN_STRUCT, .type_name = "ReductionResult" },
        { .type = PIN_I32, .i = LANGUAGE_REFERENCE_COMPONENT },
        { .type = PIN_I32, .i = -1 },
        { .type = BF_POINTER_TYPE },
        { .type = PIN_HANDLE },
        { .type = PIN_U64, .u64 = RESULT_COMPONENT },
        { .type = PIN_U64, .u64 = type_layout_hash(&result_type) },
        { .type = PIN_U64, .u64 = sizeof(reduction_result) }
    };
    const int root_slots[] = { context, -1, -1, -1, -1, owner, -1, -1, -1 };
    int reference = raw_reference_call(&EXECUTION_reference_make_call, root, root_slots, 9, source);
    const pin_value field[] = { { .type = BF_POINTER_TYPE },
                                { .type = PIN_F64 },
                                { .type = PIN_U64,
                                  .u64 = offsetof(reduction_result, total) },
                                { .type = PIN_BOOL } };
    const int field_slots[] = { reference, -1, -1, -1 };
    int scalar = raw_reference_call(&EXECUTION_reference_field_call, field, field_slots, 4, source);
    for (int mode = 0; mode < 2; mode++) {
        const pin_value args[] = { { .type = BF_POINTER_TYPE },
                                   { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_I32,
                                     .i = mode ? EXECUTION_REDUCTION_ADD_UNORDERED
                                               : EXECUTION_REDUCTION_ADD } };
        const int slots[] = { scalar, -1, -1 };
        raw_reference_call(&EXECUTION_reference_reduce_call, args, slots, 3, source);
    }
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_RETURN });
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    behavior_function *function = behavior_function_builder_finish(builder, error, sizeof error);
    if (!function) {
        fprintf(stderr, "reduction raw mixed modes: %s\n", error);
    }
    behavior_function_builder_free(builder);
    builder = NULL;
    return function;
}
TEST_CASE(reductions_mixed_public_ir_preserves_serial_failure_owner)
{
    for (int backend = 0; backend < 2; backend++) {
        prepare();
        ecs_entity first = target((reduction_result){ .total = 10 });
        ecs_entity second = target((reduction_result){ .total = 20 });
        REQUIRE(lower_source("Mixed :: system(result: ^ReductionResult) { reduce result.total += 1; }"));
        REQUIRE(plan->function_count == 1);
        behavior_function *body = raw_mixed_reductions();
        REQUIRE(body);
        behavior_function_free(plan->functions[0].body);
        plan->functions[0].body = body;
        char error[256];
        module = execution_module_finish(&plan, backends[backend], error, sizeof error);
        if (!module) {
            fprintf(stderr, "reduction mixed module: %s\n", error);
        }
        REQUIRE(module);
        execution_effect_summary effects = execution_module_effects(module);
        REQUIRE(effects.declared && !effects.parallel_safe && effects.reduce_count > 0);
        REQUIRE(!effects.read_count && !effects.write_count);
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        int status = execution_program_update(program, 1, 0, REDUCTION_BUDGET);
        REQUIRE(status < 0);
        const execution_program_failure *failure = execution_program_error(program);
        REQUIRE(failure && failure->status == status);
        REQUIRE(failure->owner == ecs_entity_to_u64(first));
        REQUIRE(!strcmp(failure->message, "Cannot collect component reduction"));
        REQUIRE(failure->line == 17 && failure->column == 3);
        REQUIRE(value(first)->total == 10 && value(second)->total == 20);
        REQUIRE(execution_program_memory_usage(program).reduction_bytes == 0);
    }
    return true;
}
static const char *read_payload(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 1);
    *out = (pin_value){ .type = PIN_BOOL,
                        .b = ((const reduction_result *)args[0].ptr)->total > 0 };
    return NULL;
}
static const node_host_call payload_call = { .value = read_payload };
static behavior_function *raw_predicate(int mode)
{
    builder = behavior_function_builder_new();
    pin_value boolean = { .type = PIN_BOOL };
    behavior_function_builder_signature(builder, NULL, 0, &boolean, 1, NULL, 0);
    int world_slot = raw_value(
        (behavior_function_instruction){ .op = BF_WORLD, .type = BF_POINTER_TYPE },
        BEHAVIOR_ADDRESS
    );
    int owner = raw_value((behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE }, BEHAVIOR_VALUE);
    int row = raw_value(
        (
            behavior_function_instruction
        ){ .op = BF_ENTITY, .type = PIN_U32, .left = world_slot, .right = owner },
        BEHAVIOR_VALUE
    );
    int component = behavior_function_builder_component(builder, RESULT_COMPONENT, false, false);
    int pointer = raw_value(
        (behavior_function_instruction){ .op = BF_COMPONENT,
                                         .type = BF_POINTER_TYPE,
                                         .left = world_slot,
                                         .right = row,
                                         .component = component,
                                         .value = { .type = PIN_U64, .u64 = RESULT_COMPONENT },
                                         .offset = sizeof(reduction_result),
                                         .layout = type_layout_hash(&result_type),
                                         .target = BEHAVIOR_FUNCTION_HOST_DATA },
        BEHAVIOR_ADDRESS
    );
    if (mode == 1) {
        raw_value(
            (behavior_function_instruction){ .op = BF_READ,
                                             .type = PIN_F64,
                                             .left = pointer,
                                             .offset = offsetof(reduction_result, total) },
            BEHAVIOR_VALUE
        );
    } else {
        const node_host_call *call = mode ? &payload_call : &EXECUTION_snapshot_call;
        pin_value aggregate = { .type = PIN_STRUCT, .type_name = result_type.name };
        pin_value output = mode ? boolean : aggregate;
        int host = behavior_function_builder_host(
            builder,
            call,
            &EXECUTION_value_access,
            &aggregate,
            1,
            &output,
            1
        );
        uint64_t id = mode ? UINT64_C(98503) : execution_intrinsic_id(call);
        assert(
            behavior_function_builder_host_identity(builder, host, id, mode ? 1 : execution_intrinsic_abi(id))
        );
        int address = raw_value(
            (behavior_function_instruction){
                .op = BF_HOST_SLOT,
                .type = BF_POINTER_TYPE,
                .left = behavior_function_builder_host_first(builder, host) },
            BEHAVIOR_ADDRESS
        );
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_WRITE,
                                             .type = BF_POINTER_TYPE,
                                             .left = pointer,
                                             .right = address,
                                             .offset = offsetof(pin_value, d) }
        );
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_HOST_CALL,
                                             .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                             .target = host }
        );
    }
    int yes = raw_value(
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_BOOL,
                                         .value = { .type = PIN_BOOL, .b = true } },
        BEHAVIOR_VALUE
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_RESULT, .type = PIN_BOOL, .left = yes }
    );
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_RETURN });
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    behavior_function *function = behavior_function_builder_finish(builder, error, sizeof error);
    if (!function) {
        fprintf(stderr, "reduction raw predicate: %s\n", error);
    }
    behavior_function_builder_free(builder);
    builder = NULL;
    return function;
}
TEST_CASE(reductions_effects_include_component_payload_reads)
{
    /* Public IR bypasses the authored mixed-access diagnostic. Snapshot, raw
     * predicate load, and pure native payload reads all retain dependencies. */
    for (int mode = 0; mode < 3; mode++) {
        prepare();
        REQUIRE(lower_source("Add :: system(result: ^ReductionResult) { reduce result.total += 1; }"));
        behavior_function *predicate = raw_predicate(mode);
        REQUIRE(predicate);
        int leaf = plan->query_count;
        apush(
            plan->queries,
            ((execution_module_query){ .kind = EXECUTION_QUERY_VALUE, .predicate = predicate })
        );
        int *children = malloc(2 * sizeof *children);
        REQUIRE(children);
        children[0] = plan->query;
        children[1] = leaf;
        apush(
            plan->queries,
            ((execution_module_query){ .kind = EXECUTION_QUERY_ALL,
                                       .children = children,
                                       .child_count = 2 })
        );
        plan->query = leaf + 1;
        plan->query_filter = leaf + 1;
        plan->query_count = acount(plan->queries);
        char error[256];
        module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
        if (!module) {
            fprintf(stderr, "reduction raw effects: %s\n", error);
        }
        REQUIRE(module);
        execution_effect_summary effects = execution_module_effects(module);
        REQUIRE(effects.declared && !effects.parallel_safe && effects.reduce_count > 0);
        bool read = false;
        for (int i = 0; i < effects.read_count; i++) {
            read |= effects.reads[i] == RESULT_COMPONENT;
        }
        REQUIRE(read);
    }
    return true;
}
void test_language_reductions()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(reductions_prepared_ordered_fold_preserves_storage_and_clamps);
    RUN_TEST_CASE(reductions_unordered_opt_in_regroups_contributions);
    RUN_TEST_CASE(reductions_min_max_preserve_candidate_nan_and_signed_zero);
    RUN_TEST_CASE(reductions_failures_and_discard_do_not_publish_partial_results);
    RUN_TEST_CASE(reductions_scalar_helper_closure_streams_and_preserves_fallbacks);
    RUN_TEST_CASE(reductions_mixed_public_ir_preserves_serial_failure_owner);
    RUN_TEST_CASE(reductions_effects_include_component_payload_reads);
    pu_clear_setup();
}
