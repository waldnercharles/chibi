#include "backend.h"
#include "ecs/ecs.h"
#include "ecs/execution.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/jit_internal.h"
#include "execution/module_effects.h"
#include "execution/prepared_internal.h"
#include "execution/scalar.h"
#include "execution/status.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define WRITE_LAYOUT UINT64_C(0x1720394a5b6c7d8e)
#define WRITE_OWNER ((UINT64_C(7) << 32) | WRITE_SLOT)
enum
{
    WRITE_COMPONENT = 9851,
    WRITE_SLOT = 65,
    WRITE_SLOTS = 66,
    WRITE_TYPES = PIN_F64 - PIN_BOOL + 1,
    WRITE_NODE = 9852,
    WRITE_PIN = 7
};
typedef struct
{
    uint64_t before;
    pin_value fields[WRITE_TYPES];
    uint64_t after;
} write_ir_row;
static const type_field preflight_field = { .name = "flag",
                                            .type = PIN_BOOL,
                                            .offset = offsetof(write_ir_row, fields) +
                                                      offsetof(pin_value, d),
                                            .size = sizeof(bool) };
static const type_desc preflight_types[] = { { .name = "WriteIR",
                                               .size = sizeof(write_ir_row),
                                               .fields = &preflight_field,
                                               .field_count = 1 },
                                             { .name = "WriteIR",
                                               .size = sizeof(write_ir_row) + 1,
                                               .fields = &preflight_field,
                                               .field_count = 1 } };
static struct
{
    write_ir_row rows[WRITE_SLOTS], expected;
    uint32_t generations[WRITE_SLOTS];
    unsigned char occupied[WRITE_SLOTS];
    uint64_t membership[2];
    int lookup[4];
    ecs_execution_column columns[2];
    ecs_execution_view world;
    int calls, reject;
    bool valid;
} fixture;
static behavior_function_builder *builder;
static behavior_function *function;
static behavior_function_context context;
static execution_module_plan *plan;
static execution_module *module, *loaded;
static ecs_world *world;
static unsigned char *bytes, *again, *mutation;
static char error[256];

static void cleanup()
{
    behavior_function_release_host(&context);
    context = (behavior_function_context){ 0 };
    behavior_function_free(function);
    if (builder) {
        behavior_function_builder_free(builder);
    }
    execution_module_plan_free(plan);
    execution_module_free(module);
    execution_module_free(loaded);
    ecs_world_free(world);
    free(bytes);
    free(again);
    afree(mutation);
    builder = NULL;
    function = NULL;
    plan = NULL;
    module = loaded = NULL;
    world = NULL;
    bytes = again = NULL;
}
static size_t field_offset(pin_type type)
{
    return offsetof(write_ir_row, fields) +
           (size_t)(type - PIN_BOOL) * sizeof(pin_value) + offsetof(pin_value, d);
}
static int publish(
    behavior_function_context *ctx,
    uint64_t owner,
    uint64_t component,
    uint64_t layout,
    size_t offset,
    pin_value declared,
    pin_value value
)
{
    fixture.calls++;
    bool scalar = value.type >= PIN_BOOL && value.type <= PIN_F64;
    fixture.valid &= ctx->world == &fixture.world && owner == WRITE_OWNER &&
                     component == WRITE_COMPONENT && layout == WRITE_LAYOUT &&
                     scalar && declared.type == value.type &&
                     declared.container == PIN_CONTAINER_SINGLE &&
                     !declared.type_name && !declared.u64 && !value.type_name &&
                     value.container == PIN_CONTAINER_SINGLE && !ctx->value_storage &&
                     !memcmp(
                         &fixture.rows[WRITE_SLOT],
                         &fixture.expected,
                         sizeof fixture.expected
                     );
    if (!scalar || offset != field_offset(value.type)) {
        fixture.valid = false;
        return 1;
    }
    if (fixture.reject != 1) {
        size_t size = behavior_scalar_size(value.type);
        memcpy((unsigned char *)&fixture.rows[WRITE_SLOT] + offset, &value.d, size);
        memcpy((unsigned char *)&fixture.expected + offset, &value.d, size);
    }
    return fixture.reject != 0;
}
static void raw_world()
{
    memset(&fixture, 0, sizeof fixture);
    fixture.valid = true;
    fixture.rows[WRITE_SLOT].before = UINT64_C(0x1122334455667788);
    fixture.rows[WRITE_SLOT].after = UINT64_C(0x8877665544332211);
    for (pin_type type = PIN_BOOL; type <= PIN_F64; type++) {
        fixture.rows[WRITE_SLOT].fields[type - PIN_BOOL].type = type;
    }
    fixture.expected = fixture.rows[WRITE_SLOT];
    fixture.generations[WRITE_SLOT] = 7;
    fixture.occupied[WRITE_SLOT] = 1;
    fixture.membership[1] = 2;
    uint64_t hash = WRITE_COMPONENT * UINT64_C(11400714819323198485);
    int bucket = (int)((hash ^ (hash >> 32)) & 3);
    uint64_t collision = WRITE_COMPONENT + 1;
    for (; collision < WRITE_COMPONENT + 32; collision++) {
        hash = collision * UINT64_C(11400714819323198485);
        if ((int)((hash ^ (hash >> 32)) & 3) == bucket) {
            break;
        }
    }
    fixture.columns[1] = (ecs_execution_column){ .id = WRITE_COMPONENT,
                                                 .layout = WRITE_LAYOUT,
                                                 .stride = sizeof(write_ir_row),
                                                 .data = fixture.rows,
                                                 .occupancy = fixture.membership,
                                                 .count = 1,
                                                 .singleton = WRITE_SLOT };
    fixture.columns[0] = fixture.columns[1];
    fixture.columns[0].id = collision;
    fixture.columns[0].layout ^= 1;
    fixture.lookup[bucket] = 1;
    fixture.lookup[(bucket + 1) & 3] = 2;
    fixture.world = (ecs_execution_view){ .generations = fixture.generations,
                                          .occupied = fixture.occupied,
                                          .slot_count = WRITE_SLOTS,
                                          .columns = fixture.columns,
                                          .lookup = fixture.lookup,
                                          .capacity = 4 };
}
static int emit(behavior_function_instruction i)
{
    return behavior_function_builder_emit(builder, i);
}
static int value(behavior_function_instruction i)
{
    i.output = behavior_function_builder_slot(builder, i.type);
    emit(i);
    return i.output;
}
static int argument(pin_value type, int index)
{
    return value((behavior_function_instruction){ .op = BF_ARGUMENT,
                                                  .type = type.type,
                                                  .left = index,
                                                  .value = { .type = type.type } });
}
static void result(int slot, pin_type type, int index)
{
    emit((
        behavior_function_instruction
    ){ .op = BF_RESULT, .type = type, .left = slot, .target = index });
}
static behavior_function_instruction write_instruction(pin_type type, int owner, int rhs)
{
    return (
        behavior_function_instruction
    ){ .op = BF_COMPONENT_WRITE,
       .type = type,
       .left = owner,
       .right = rhs,
       .component = behavior_function_builder_component(builder, WRITE_COMPONENT, false, true),
       .value = { .type = PIN_U64, .u64 = WRITE_COMPONENT },
       .layout = WRITE_LAYOUT,
       .offset = sizeof(write_ir_row),
       .target = (int)field_offset(type),
       .node_id = WRITE_NODE,
       .pin = WRITE_PIN };
}
static bool finish(bool compiled)
{
    emit((behavior_function_instruction){ .op = BF_END });
    function = behavior_function_builder_finish(builder, error, sizeof error);
    if (!function) {
        fprintf(stderr, "component write IR: %s\n", error);
    }
    REQUIRE(function);
    if (!compiled) {
        return true;
    }
    REQUIRE(behavior_backend_available(TEST_COMPILED_BACKEND));
    REQUIRE(behavior_function_compile(function));
#ifdef __EMSCRIPTEN__
    REQUIRE(function->artifact->wasm_jit);
#else
    REQUIRE(behavior_function_entry(function));
#endif
    return true;
}
static void begin(pin_value *args, int count, pin_value *outputs)
{
    behavior_function_release_host(&context);
    context = (behavior_function_context){
        .arguments = args,
        .argument_count = count,
        .results = outputs,
        .world = &fixture.world,
        .store_component = publish,
        .budget = 10,
        .error_message = "stale write error"
    };
}
static pin_value sample(pin_type type, int index)
{
    pin_value v = { .type = type };
    uint64_t bits = index == 0 ? UINT64_C(0xfedcba9876543281)
                  : index == 1 ? 0
                               : UINT64_MAX;
    if (type == PIN_BOOL) {
        v.b = index != 1;
    } else if (type == PIN_F32) {
        uint32_t f = index == 0 ? UINT32_C(0x80000000)
                   : index == 1 ? UINT32_C(0x7f800000)
                                : UINT32_C(0x7fc12345);
        memcpy(&v.f, &f, sizeof f);
    } else if (type == PIN_F64) {
        bits = index == 0 ? UINT64_C(0x8000000000000000)
             : index == 1 ? UINT64_C(0xfff0000000000000)
                          : UINT64_C(0x7ff8123456789abc);
        memcpy(&v.d, &bits, sizeof bits);
    } else {
        v = behavior_scalar_from_bits(type, bits);
    }
    return v;
}
TEST_CASE(component_write_ir_stack_cells_precede_locals)
{
    /* Only sizing arithmetic runs here. Reverting to the old nonregister-only
     * reservation fails these bounds without emitting or executing unsafe code. */
    behavior_function layout = { 0 };
    asetlen(layout.slots, 16);
    asetlen(layout.instructions, 2);
    int registers[16];
    for (int slot = 0; slot < 16; slot++) {
        registers[slot] = 1;
    }
    layout.instructions[0] = layout.instructions[1] = (behavior_function_instruction){
        .op = BF_END
    };
    int sizes[8];
    sizes[0] = behavior_jit_stack_slots(&layout, registers);
    registers[2] = 0;
    sizes[1] = behavior_jit_stack_slots(&layout, registers);
    layout.instructions[0] = (behavior_function_instruction){ .op = BF_COMPONENT_WRITE,
                                                              .left = 15,
                                                              .right = 4 };
    sizes[2] = behavior_jit_stack_slots(&layout, registers);
    layout.instructions[0].left = 4;
    layout.instructions[0].right = 15;
    sizes[3] = behavior_jit_stack_slots(&layout, registers);
    layout.instructions[0].left = 15;
    sizes[4] = behavior_jit_stack_slots(&layout, registers);
    layout.instructions[0].op = BF_WRITE;
    sizes[5] = behavior_jit_stack_slots(&layout, registers);
    registers[15] = 0;
    sizes[6] = behavior_jit_stack_slots(&layout, registers);
    registers[15] = 1;
    layout.instructions[0] = (behavior_function_instruction){ .op = BF_COMPONENT_WRITE,
                                                              .left = 4,
                                                              .right = 8 };
    layout.instructions[1] = (behavior_function_instruction){ .op = BF_COMPONENT_WRITE,
                                                              .left = 15,
                                                              .right = 4 };
    sizes[7] = behavior_jit_stack_slots(&layout, registers);
    afree(layout.slots);
    afree(layout.instructions);
    const int expected[] = { 0, 3, 16, 16, 16, 3, 16, 16 };
    for (int test = 0; test < 8; test++) {
        REQUIRE(sizes[test] == expected[test]);
    }
    for (int test = 2; test <= 4; test++) {
        size_t locals = (size_t)sizes[test] * sizeof(double);
        REQUIRE(15 * sizeof(double) + sizeof(uint64_t) <= locals);
        REQUIRE(4 * sizeof(double) + sizeof(double) <= locals);
    }
    return true;
}
static int inspect_write(void *, behavior_function_context *, const behavior_source_point *)
{
    return 0;
}
TEST_CASE(component_write_ir_no_result_register_operands_preserve_stack_locals)
{
    builder = behavior_function_builder_new();
    pin_value args[10];
    for (int j = 0; j < 8; j++) {
        args[j] = (pin_value){ .type = PIN_BOOL, .b = true };
    }
    args[8] = (pin_value){ .type = PIN_U64, .u64 = WRITE_OWNER };
    args[9] = (pin_value){ .type = PIN_F64, .d = -17.25 };
    behavior_function_builder_signature(builder, args, 10, NULL, 0, NULL, 0);
    int locals[16];
    for (int j = 0; j < 16; j++) {
        locals[j] = behavior_function_builder_local(
            builder,
            (pin_value){ .type = PIN_F64, .d = 100.25 + j }
        );
    }
    /* These live argument assertions retain eight preceding slots. Both write
     * operands die at the call, so the native allocator can keep them in scratch
     * registers. Straight-line persistent locals remain on the stack. */
    for (int j = 0; j < 8; j++) {
        int condition = argument(args[j], j);
        emit((behavior_function_instruction){ .op = BF_ASSERT,
                                              .left = condition,
                                              .target = BEHAVIOR_FUNCTION_ARGUMENT });
    }
    int owner = argument(args[8], 8), rhs = argument(args[9], 9);
    int write = emit(write_instruction(PIN_F64, owner, rhs));
    for (int j = 0; j < 16; j++) {
        int local = value((behavior_function_instruction){ .op = BF_LOCAL,
                                                           .type = PIN_F64,
                                                           .left = locals[j] });
        int initial = value((behavior_function_instruction){
            .op = BF_CONSTANT,
            .type = PIN_F64,
            .value = { .type = PIN_F64, .d = 100.25 + j } });
        int same = behavior_function_builder_slot(builder, PIN_BOOL);
        emit((
            behavior_function_instruction
        ){ .op = BF_EQ, .type = PIN_F64, .output = same, .left = local, .right = initial });
        emit((
            behavior_function_instruction
        ){ .op = BF_ASSERT, .left = same, .target = BEHAVIOR_FUNCTION_HOST_DATA });
    }
    /* Native emission checks both operand cells against the actual local base
     * before producing callable code. The old under-reservation must fail that
     * hard precondition rather than execute an out-of-frame store. */
    REQUIRE(finish(true));
    REQUIRE(!function->result_count && acount(function->locals) == 16);
    REQUIRE(behavior_function_runtime_keeps(function, write));
    REQUIRE(
        function->instructions[write].left >= 8 && function->instructions[write].right >= 9
    );
    for (int compiled = 0; compiled < 2; compiled++) {
        for (int inspected = 0; inspected < 2; inspected++) {
            raw_world();
            begin(args, 10, NULL);
            context.source_pause = inspected ? inspect_write : NULL;
            REQUIRE(
                behavior_function_inspection_required(function, &context) ==
                (inspected != 0)
            );
#ifndef __EMSCRIPTEN__
            if (compiled && inspected) {
                REQUIRE(behavior_function_compile_inspection(function));
                REQUIRE(function->artifact->inspection_code);
            }
#endif
            REQUIRE(behavior_function_run(function, &context, compiled) == FLOW_OK);
            REQUIRE(fixture.calls == 1 && fixture.valid && !context.error_message);
            REQUIRE(
                fixture.rows[WRITE_SLOT].fields[PIN_F64 - PIN_BOOL].d == args[9].d
            );
            REQUIRE(!context.value_storage && !context.results);
        }
    }
    return true;
}
TEST_CASE(component_write_ir_all_widths_exact_bits_and_live_values)
{
    for (int handle = 0; handle < 2; handle++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_value args[WRITE_TYPES + 3] = { { .type = handle ? PIN_HANDLE : PIN_U64 } };
        pin_value outputs[WRITE_TYPES + 2] = { 0 };
        for (pin_type type = PIN_BOOL; type <= PIN_F64; type++) {
            args[type - PIN_BOOL + 1].type = type;
            outputs[type - PIN_BOOL].type = type;
        }
        args[WRITE_TYPES + 1] = outputs[WRITE_TYPES] = (pin_value){ .type = PIN_I64 };
        args[WRITE_TYPES + 2] = outputs[WRITE_TYPES + 1] = (pin_value){ .type = PIN_F64 };
        behavior_function_builder_signature(builder, args, WRITE_TYPES + 3, outputs, WRITE_TYPES + 2, NULL, 0);
        int slots[WRITE_TYPES + 3], writes[WRITE_TYPES];
        for (int j = 0; j < WRITE_TYPES + 3; j++) {
            slots[j] = argument(args[j], j);
        }
        for (pin_type type = PIN_BOOL; type <= PIN_F64; type++) {
            int j = type - PIN_BOOL;
            writes[j] = emit(write_instruction(type, slots[0], slots[j + 1]));
        }
        for (int j = 0; j < WRITE_TYPES + 2; j++) {
            result(slots[j + 1], outputs[j].type, j);
        }
        REQUIRE(finish(true));
        REQUIRE(!function->host_calls && !function->data_values);
        REQUIRE(!behavior_function_uses_storage(function));
        for (int j = 0; j < WRITE_TYPES; j++) {
            REQUIRE(behavior_function_runtime_keeps(function, writes[j]));
        }
        const ecs_execution_column *stale[] = { &fixture.columns[0] };
        for (int compiled = 0; compiled < 2; compiled++) {
            for (int s = 0; s < 3; s++) {
                raw_world();
                args[0].u64 = WRITE_OWNER;
                for (pin_type type = PIN_BOOL; type <= PIN_F64; type++) {
                    args[type - PIN_BOOL + 1] = sample(type, s);
                }
                args[WRITE_TYPES + 1].i64 = -INT64_C(0x123456789abcde);
                args[WRITE_TYPES + 2].d = -13.75;
                begin(args, WRITE_TYPES + 3, outputs);
                context.columns = stale;
                context.binding_world = &fixture.world;
                REQUIRE(behavior_function_run(function, &context, compiled) == FLOW_OK);
                REQUIRE(
                    fixture.valid && fixture.calls == WRITE_TYPES && !context.error_message
                );
                REQUIRE(!context.value_storage);
                for (int j = 0; j < WRITE_TYPES + 2; j++) {
                    REQUIRE(outputs[j].type == args[j + 1].type);
                    REQUIRE(!memcmp(
                        &outputs[j].d,
                        &args[j + 1].d,
                        behavior_scalar_size(outputs[j].type)
                    ));
                    if (j < WRITE_TYPES) {
                        REQUIRE(!memcmp(
                            &fixture.rows[WRITE_SLOT].fields[j].d,
                            &args[j + 1].d,
                            behavior_scalar_size(outputs[j].type)
                        ));
                    }
                }
                REQUIRE(!memcmp(
                    &fixture.rows[WRITE_SLOT],
                    &fixture.expected,
                    sizeof fixture.expected
                ));
            }
        }
    }
    return true;
}
TEST_CASE(component_write_ir_failure_checks_and_same_context_recovery)
{
    builder = behavior_function_builder_new();
    pin_value args[] = { { .type = PIN_U64 }, { .type = PIN_F64 } };
    pin_value output = { .type = PIN_F64 };
    behavior_function_builder_signature(builder, args, 2, &output, 1, NULL, 0);
    int owner = argument(args[0], 0), rhs = argument(args[1], 1);
    emit(write_instruction(PIN_F64, owner, rhs));
    result(rhs, PIN_F64, 0);
    REQUIRE(finish(true));
    for (int compiled = 0; compiled < 2; compiled++) {
        for (int failure = 0; failure < 12; failure++) {
            raw_world();
            args[0].u64 = WRITE_OWNER;
            args[1].d = -17.25;
            begin(args, 2, &output);
            switch (failure) {
                case 0: context.world = NULL; break;
                case 1: args[0].u64 ^= UINT64_C(1) << 32; break;
                case 2: args[0].u64 = UINT64_C(7) << 32; break;
                case 3: args[0].u64 = (UINT64_C(7) << 32) | WRITE_SLOTS; break;
                case 4: fixture.occupied[WRITE_SLOT] = 0; break;
                case 5: fixture.membership[1] = 0; break;
                case 6: fixture.columns[1].id++; break;
                case 7: fixture.columns[1].layout ^= 1; break;
                case 8: fixture.columns[1].stride++; break;
                case 9: context.store_component = NULL; break;
                case 10: fixture.reject = 1; break;
                case 11: fixture.reject = 2; break;
            }
            output.u64 = UINT64_C(0x123456789abcdef0);
            REQUIRE(behavior_function_run(function, &context, compiled) == FLOW_ERR_INVALID_GRAPH);
            REQUIRE(output.u64 == UINT64_C(0x123456789abcdef0));
            REQUIRE(context.failure == BEHAVIOR_FUNCTION_HOST_DATA);
            REQUIRE(context.node_id == WRITE_NODE && context.error_pin == WRITE_PIN);
            REQUIRE(
                context.error_message &&
                !strcmp(context.error_message, failure < 9 ? "Referenced storage is unavailable" : "Cannot retain referenced component write")
            );
            REQUIRE(fixture.valid && fixture.calls == (failure >= 10));
            REQUIRE(
                fixture.rows[WRITE_SLOT].fields[PIN_F64 - PIN_BOOL].d ==
                (failure == 11 ? args[1].d : 0)
            );
            /* Keep the context and result banks, repair only the failed input. */
            raw_world();
            args[0].u64 = WRITE_OWNER;
            context.world = &fixture.world;
            context.store_component = publish;
            REQUIRE(behavior_function_run(function, &context, compiled) == FLOW_OK);
            REQUIRE(fixture.valid && fixture.calls == 1 && !context.error_message);
            REQUIRE(output.d == args[1].d && !context.value_storage);
        }
    }
    return true;
}
TEST_CASE(component_write_ir_verifier_rejects_invalid_metadata_and_provenance)
{
    for (int invalid = 0; invalid < 26; invalid++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_value args[] = { { .type = invalid == 4 ? PIN_I32 : PIN_U64 },
                             { .type = invalid == 5 ? PIN_I32 : PIN_U64 } };
        behavior_function_builder_signature(builder, args, 2, NULL, 0, NULL, 0);
        int owner = argument(args[0], 0), rhs = argument(args[1], 1);
        behavior_function_instruction i = write_instruction(PIN_U64, owner, rhs);
        switch (invalid) {
            case 0: i.component++; break;
            case 1:
                i.component = behavior_function_builder_component(builder, WRITE_COMPONENT, true, true);
                break;
            case 2: i.value.u64++; break;
            case 3: i.value.type = PIN_HANDLE; break;
            case 4:
            case 5: break;
            case 6: i.left = -1; break;
            case 7: i.right = -1; break;
            case 8: i.left = INT_MAX; break;
            case 9: i.right = INT_MAX; break;
            case 10: i.target = -1; break;
            case 11: i.target = sizeof(write_ir_row); break;
            case 12: i.target = INT_MAX; break;
            case 13: i.offset = 0; break;
            case 14:
                i.offset = field_offset(PIN_U64) + sizeof(uint64_t) - 1;
                break;
            case 15: i.layout = 0; break;
            case 16: i.type = PIN_HANDLE; break;
            case 17: i.type = PIN_NAME; break;
            case 18: i.type = PIN_STR; break;
            case 19: i.type = PIN_STRUCT; break;
            case 20: i.type = PIN_FLOW; break;
            case 21: i.type = PIN_UNION; break;
            case 22: i.indirect = true; break;
            case 23: i.frame = INT_MAX; break;
            case 24: i.source = INT_MAX; break;
            case 25: i.stack = 7; break;
        }
        emit(i);
        emit((behavior_function_instruction){ .op = BF_END });
        function = behavior_function_builder_finish(builder, error, sizeof error);
        REQUIRE(!function && error[0]);
    }
    for (int operand = 0; operand < 2; operand++) {
#if UINTPTR_MAX != UINT64_MAX
        if (operand == 0) {
            continue;
        }
#endif
        for (int reference = 0; reference < 2; reference++) {
            cleanup();
            builder = behavior_function_builder_new();
            pin_value args[] = { { .type = PIN_U64 }, { .type = BF_POINTER_TYPE } };
            behavior_function_builder_signature(builder, args, 2, NULL, 0, NULL, 0);
            behavior_value_kind kind = reference ? BEHAVIOR_REFERENCE : BEHAVIOR_ADDRESS;
            REQUIRE(
                behavior_function_builder_kind(builder, BEHAVIOR_BANK_ARGUMENT, operand, kind)
            );
            int slots[] = { argument(args[0], 0), argument(args[1], 1) };
            REQUIRE(
                behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, slots[operand], kind)
            );
            emit(write_instruction(BF_POINTER_TYPE, slots[0], slots[1]));
            emit((behavior_function_instruction){ .op = BF_END });
            function = behavior_function_builder_finish(builder, error, sizeof error);
            REQUIRE(!function && strstr(error, "provenance"));
        }
    }
    return true;
}
TEST_CASE(component_write_ir_write_is_live_and_invalidates_reads)
{
    builder = behavior_function_builder_new();
    pin_value args[] = { { .type = PIN_U64 }, { .type = PIN_I32 }, { .type = PIN_BOOL } };
    pin_value outputs[] = { { .type = PIN_I32 }, { .type = PIN_I32 } };
    behavior_function_builder_signature(builder, args, 3, outputs, 2, NULL, 0);
    int owner = argument(args[0], 0), rhs = argument(args[1], 1),
        skip = argument(args[2], 2);
    emit((behavior_function_instruction){ .op = BF_CHECK });
    int branch = emit((behavior_function_instruction){ .op = BF_BRANCH, .left = skip });
    behavior_function_instruction read = write_instruction(PIN_I32, owner, rhs);
    read.op = BF_COMPONENT_READ;
    read.right = 0;
    int before_pc = acount(behavior_function_builder_definition(builder)->instructions);
    int before = value(read);
    int write_pc = emit(write_instruction(PIN_I32, owner, rhs));
    int after_pc = acount(behavior_function_builder_definition(builder)->instructions);
    int after = value(read);
    result(before, PIN_I32, 0);
    result(after, PIN_I32, 1);
    int end = emit((behavior_function_instruction){ .op = BF_RETURN });
    REQUIRE(behavior_function_builder_patch_target(builder, branch, end));
    REQUIRE(finish(true));
    REQUIRE(behavior_function_runtime_keeps(function, before_pc));
    REQUIRE(behavior_function_runtime_keeps(function, write_pc));
    REQUIRE(behavior_function_runtime_keeps(function, after_pc));
    const uint64_t related = WRITE_COMPONENT, unrelated = WRITE_COMPONENT + 1;
    REQUIRE(!behavior_function_preserves_components(function, &related, 1));
    REQUIRE(!behavior_function_preserves_components(function, &unrelated, 1));
    REQUIRE(!execution_query_predicate_valid(function, UINT_MAX));
    for (int compiled = 0; compiled < 2; compiled++) {
        for (int phase = 0; phase < 4; phase++) {
            raw_world();
            args[0].u64 = WRITE_OWNER;
            args[1].i = -42;
            args[2].b = phase == 1;
            outputs[0].i = outputs[1].i = 12345;
            begin(args, 3, outputs);
            context.budget = phase == 0 ? 0 : 1;
            if (phase < 3) {
                context.world = NULL;
            }
            int expected = phase == 0 ? FLOW_ERR_STEP_BUDGET
                         : phase == 2 ? FLOW_ERR_INVALID_GRAPH
                                      : FLOW_OK;
            REQUIRE(behavior_function_run(function, &context, compiled) == expected);
            REQUIRE(fixture.calls == (phase == 3) && fixture.valid);
            REQUIRE(outputs[0].i == (phase == 3 ? 0 : 12345));
            REQUIRE(outputs[1].i == (phase == 3 ? -42 : 12345));
            if (phase == 0) {
                REQUIRE(context.failure == BEHAVIOR_FUNCTION_BUDGET && context.error_steps == 0);
            }
        }
    }
    return true;
}

enum
{
    WIRE_TYPE,
    WIRE_RHS,
    WIRE_TARGET,
    WIRE_STRIDE,
    WIRE_BINDING,
    WIRE_LAYOUT,
    WIRE_FIELDS
};
/* One prepared v28 instruction row as read, with where it and its value lie. */
typedef struct
{
    size_t begin, end, value_begin, value_end;
    int op, type, output, left, source, right, target, frame, stack, pin, component;
    uint64_t fields, node, layout, message;
    size_t stride;
    bool indirect;
} wire_row;
/* The v28 instruction row at `offset`, if it decodes and its value is the
 * write's component id. */
static bool read_row(size_t length, size_t offset, wire_row *row)
{
    execution_wire wire = { .data = bytes, .size = length, .offset = offset, .reading = true };
    *row = (wire_row){ .begin = offset, .stack = -1 };
    execution_wire_int(&wire, &row->op);
    execution_wire_int(&wire, &row->type);
    execution_wire_int(&wire, &row->output);
    execution_wire_int(&wire, &row->left);
    execution_wire_int(&wire, &row->source);
    execution_wire_u64(&wire, &row->fields);
    if (wire.error || row->fields & ~(uint64_t)INSTRUCTION_FIELDS) {
        return false;
    }
    if (row->fields & INSTRUCTION_RIGHT) {
        execution_wire_int(&wire, &row->right);
    }
    if (row->fields & INSTRUCTION_TARGET) {
        execution_wire_int(&wire, &row->target);
    }
    if (row->fields & INSTRUCTION_OFFSET) {
        execution_wire_size(&wire, &row->stride);
    }
    row->value_begin = row->value_end = wire.offset;
    if (row->fields & INSTRUCTION_VALUE) {
        /* A component id travels as an unnamed native u64 value. */
        int tag = 0, container = 0;
        size_t name = 0;
        bool authored = true;
        uint64_t identity = 0;
        execution_wire_int(&wire, &tag);
        execution_wire_int(&wire, &container);
        execution_wire_size(&wire, &name);
        execution_wire_bool(&wire, &authored);
        execution_wire_u64(&wire, &identity);
        row->value_end = wire.offset;
        if (tag != PIN_U64 || container != PIN_CONTAINER_SINGLE || name ||
            authored || identity != WRITE_COMPONENT) {
            return false;
        }
    }
    if (row->fields & INSTRUCTION_FRAME) {
        execution_wire_int(&wire, &row->frame);
    }
    if (row->fields & INSTRUCTION_STACK) {
        execution_wire_int(&wire, &row->stack);
    }
    if (row->fields & INSTRUCTION_PIN) {
        execution_wire_int(&wire, &row->pin);
    }
    if (row->fields & INSTRUCTION_INDIRECT) {
        execution_wire_bool(&wire, &row->indirect);
    }
    if (row->fields & INSTRUCTION_COMPONENT) {
        execution_wire_int(&wire, &row->component);
    }
    if (row->fields & INSTRUCTION_NODE) {
        execution_wire_u64(&wire, &row->node);
    }
    if (row->fields & INSTRUCTION_LAYOUT) {
        execution_wire_u64(&wire, &row->layout);
    }
    if (row->fields & INSTRUCTION_MESSAGE) {
        execution_wire_u64(&wire, &row->message);
    }
    row->end = wire.offset;
    return !wire.error && row->value_end > row->value_begin;
}
/* Decode candidate rows using the public varint helpers. Match the complete
 * distinctive write metadata rather than assuming table offsets or widths. */
static bool write_row(size_t length, wire_row *found)
{
    int matches = 0;
    for (size_t offset = 0; offset < length; offset++) {
        wire_row row;
        if (!read_row(length, offset, &row) || row.op != BF_COMPONENT_WRITE ||
            row.type != PIN_BOOL || row.left != 0 || row.right != 1 ||
            row.target != (int)field_offset(PIN_BOOL) || row.pin != WRITE_PIN ||
            row.stride != sizeof(write_ir_row) || row.indirect || row.component != 0 ||
            row.node != WRITE_NODE || row.layout != WRITE_LAYOUT) {
            continue;
        }
        *found = row;
        matches++;
    }
    return matches == 1;
}
/* The prepared bytes with the write row's `field` made invalid, written as
 * present whether or not it held its default. */
static bool mutate_field(size_t length, const wire_row *original, int field)
{
    wire_row row = *original;
    uint64_t present = 0;
    switch (field) {
        case WIRE_TYPE: row.type = PIN_HANDLE; break;
        case WIRE_RHS:
            row.right = INT_MAX;
            present = INSTRUCTION_RIGHT;
            break;
        case WIRE_TARGET:
            row.target = -1;
            present = INSTRUCTION_TARGET;
            break;
        case WIRE_STRIDE:
            row.stride = field_offset(PIN_BOOL);
            present = INSTRUCTION_OFFSET;
            break;
        case WIRE_BINDING:
            row.component = INT_MAX;
            present = INSTRUCTION_COMPONENT;
            break;
        case WIRE_LAYOUT:
            row.layout = 0;
            present = INSTRUCTION_LAYOUT;
            break;
        default: assert(!"unknown wire field"); break;
    }
    row.fields |= present;
    afree(mutation);
    for (size_t j = 0; j < row.begin; j++) {
        apush(mutation, bytes[j]);
    }
    execution_wire wire = { .data = mutation, .size = row.begin, .offset = row.begin };
    execution_wire_int(&wire, &row.op);
    execution_wire_int(&wire, &row.type);
    execution_wire_int(&wire, &row.output);
    execution_wire_int(&wire, &row.left);
    execution_wire_int(&wire, &row.source);
    execution_wire_u64(&wire, &row.fields);
    if (row.fields & INSTRUCTION_RIGHT) {
        execution_wire_int(&wire, &row.right);
    }
    if (row.fields & INSTRUCTION_TARGET) {
        execution_wire_int(&wire, &row.target);
    }
    if (row.fields & INSTRUCTION_OFFSET) {
        execution_wire_size(&wire, &row.stride);
    }
    for (size_t j = row.value_begin; j < row.value_end; j++) {
        apush(wire.data, bytes[j]);
    }
    wire.size = wire.offset = (size_t)acount(wire.data);
    if (row.fields & INSTRUCTION_FRAME) {
        execution_wire_int(&wire, &row.frame);
    }
    if (row.fields & INSTRUCTION_STACK) {
        execution_wire_int(&wire, &row.stack);
    }
    if (row.fields & INSTRUCTION_PIN) {
        execution_wire_int(&wire, &row.pin);
    }
    if (row.fields & INSTRUCTION_INDIRECT) {
        execution_wire_bool(&wire, &row.indirect);
    }
    if (row.fields & INSTRUCTION_COMPONENT) {
        execution_wire_int(&wire, &row.component);
    }
    if (row.fields & INSTRUCTION_NODE) {
        execution_wire_u64(&wire, &row.node);
    }
    if (row.fields & INSTRUCTION_LAYOUT) {
        execution_wire_u64(&wire, &row.layout);
    }
    if (row.fields & INSTRUCTION_MESSAGE) {
        execution_wire_u64(&wire, &row.message);
    }
    mutation = wire.data;
    REQUIRE(!wire.error);
    for (size_t j = row.end; j < length; j++) {
        apush(mutation, bytes[j]);
    }
    return true;
}
static bool check_effects(const execution_module *m)
{
    execution_effect_summary effects = execution_module_effects(m);
    REQUIRE(effects.declared && !effects.structural && !effects.host_sink);
    REQUIRE(!effects.parallel_safe && effects.write_count == 1);
    REQUIRE(effects.writes[0] == WRITE_COMPONENT);
    const behavior_function *body = execution_module_function_at(m, 0).body;
    REQUIRE(!execution_query_predicate_valid(body, UINT_MAX));
    execution_cost_summary costs = behavior_function_costs(body);
    REQUIRE(
        costs.complete && costs.possible == (EXECUTION_COST_ALLOCATE |
                                             EXECUTION_COST_COPY | EXECUTION_COST_ESCAPE)
    );
    /* Exercise the body proof with producer batching metadata and no optional
     * write summary. The callback is still a boundary for unrelated queries. */
    behavior_function proof = *body;
    proof.batchable = true;
    proof.component_writes = NULL;
    proof.component_proofs = NULL;
    const uint64_t ids[] = { WRITE_COMPONENT, WRITE_COMPONENT + 1 };
    bool preserves = false;
    for (int j = 0; j < 2; j++) {
        preserves |= behavior_function_preserves_components(&proof, &ids[j], 1);
    }
    for (int j = 0; j < acount(proof.component_proofs); j++) {
        afree(proof.component_proofs[j].components);
    }
    afree(proof.component_proofs);
    REQUIRE(!preserves);
    return true;
}
TEST_CASE(component_write_ir_prepared_effects_preflight_and_schema)
{
    builder = behavior_function_builder_new();
    pin_value output = { .type = PIN_BOOL };
    behavior_function_builder_signature(builder, NULL, 0, &output, 1, NULL, 0);
    int owner = value((behavior_function_instruction){ .op = BF_OWNER,
                                                       .type = PIN_HANDLE });
    int rhs = value((
        behavior_function_instruction
    ){ .op = BF_CONSTANT, .type = PIN_BOOL, .value = { .type = PIN_BOOL, .b = true } });
    emit(write_instruction(PIN_BOOL, owner, rhs));
    result(rhs, PIN_BOOL, 0);
    REQUIRE(finish(false));
    plan = execution_module_plan_new();
    plan->valid = true;
    plan->id = 9853;
    apush(
        plan->functions,
        ((execution_module_function){ .id = 1,
                                      .kind = EXECUTION_FUNCTION_AUTHORED,
                                      .body = function })
    );
    function = NULL;
    plan->function_count = 1;
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module && !plan);
    REQUIRE(check_effects(module));
    size_t length = 0, repeated = 0;
    REQUIRE(execution_prepared_write(module, &bytes, &length, error, sizeof error));
    REQUIRE(length && length < 16 * 1024);
    wire_row write;
    REQUIRE(write_row(length, &write));
    loaded = execution_prepared_read(bytes, length, NULL, TEST_COMPILED_BACKEND, error, sizeof error);
    REQUIRE(loaded && execution_module_backend(loaded) == TEST_COMPILED_BACKEND);
    REQUIRE(check_effects(loaded));
    REQUIRE(execution_prepared_write(loaded, &again, &repeated, error, sizeof error));
    REQUIRE(length == repeated && !memcmp(bytes, again, length));
    const execution_module *modules[] = { module, loaded };
    for (int compiled = 0; compiled < 2; compiled++) {
        const behavior_function
            *body = execution_module_function_at(modules[compiled], 0).body;
        if (compiled) {
#ifdef __EMSCRIPTEN__
            REQUIRE(body->artifact->wasm_jit);
#else
            REQUIRE(behavior_function_entry(body));
#endif
        }
        raw_world();
        begin(NULL, 0, &output);
        context.owner = WRITE_OWNER;
        REQUIRE(behavior_function_run(body, &context, compiled) == FLOW_OK);
        REQUIRE(output.b && fixture.calls == 1 && fixture.valid && !context.value_storage);
    }
    for (int mismatch = 0; mismatch < 4; mismatch++) {
        ecs_world_free(world);
        world = ecs_world_new();
        if (mismatch) {
            const type_desc *row = &preflight_types[mismatch == 2];
            ecs_component_desc component = { .id = WRITE_COMPONENT,
                                             .key = "test.write_ir",
                                             .name = row->name,
                                             .element_size = row->size,
                                             .type = row,
                                             .layout_hash = WRITE_LAYOUT ^
                                                            (mismatch == 1) };
            REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_OK);
        }
        REQUIRE(execution_module_components_valid(module, world) == (mismatch == 3));
        REQUIRE(execution_module_components_valid(loaded, world) == (mismatch == 3));
    }
    execution_module_free(loaded);
    loaded = NULL;
    for (int field_index = 0; field_index < WIRE_FIELDS; field_index++) {
        REQUIRE(mutate_field(length, &write, field_index));
        loaded = execution_prepared_read(
            mutation,
            (size_t)acount(mutation),
            NULL,
            BEHAVIOR_BYTECODE,
            error,
            sizeof error
        );
        REQUIRE(!loaded && error[0]);
    }
    enum
    {
        SCHEMA_COUNT = BF_END + 1
    };
    const char *names[SCHEMA_COUNT] = {
#define FUNCTION_NAME(op) [BF_##op] = #op,
#define MATH_NAME(op, name, fn) FUNCTION_NAME(op)
        BEHAVIOR_FUNCTION_OPS(FUNCTION_NAME, MATH_NAME)
#undef MATH_NAME
#undef FUNCTION_NAME
    };
    const char *foreign[SCHEMA_COUNT];
    memcpy(foreign, names, sizeof foreign);
    foreign[BF_WRITE] = names[BF_COMPONENT_WRITE];
    foreign[BF_COMPONENT_WRITE] = names[BF_WRITE];
    uint64_t current = prepared_abi(names, SCHEMA_COUNT);
    uint64_t incompatible = prepared_abi(foreign, SCHEMA_COUNT);
    REQUIRE(current != incompatible);
    execution_wire wire = { .data = bytes, .size = length, .reading = true };
    uint64_t header[4] = { 0 };
    for (int j = 0; j < 4; j++) {
        execution_wire_u64(&wire, &header[j]);
    }
    REQUIRE(!wire.error && header[3] == current && wire.offset < length);
    size_t payload = wire.offset;
    afree(mutation);
    wire = (execution_wire){ 0 };
    header[3] = incompatible;
    for (int j = 0; j < 4; j++) {
        execution_wire_u64(&wire, &header[j]);
    }
    mutation = wire.data;
    REQUIRE(!wire.error);
    for (size_t j = payload; j < length; j++) {
        apush(mutation, bytes[j]);
    }
    size_t budget = 1024 * 1024, remaining = budget;
    loaded = execution_prepared_read_budget(
        mutation,
        (size_t)acount(mutation),
        NULL,
        BEHAVIOR_BYTECODE,
        &remaining,
        error,
        sizeof error
    );
    REQUIRE(!loaded && !strcmp(error, "prepared target ABI differs") && remaining == budget);
    return true;
}
void test_language_component_write_layout()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(component_write_ir_stack_cells_precede_locals);
    pu_clear_setup();
}
void test_language_component_write_ir()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(component_write_ir_stack_cells_precede_locals);
    RUN_TEST_CASE(component_write_ir_no_result_register_operands_preserve_stack_locals);
    RUN_TEST_CASE(component_write_ir_all_widths_exact_bits_and_live_values);
    RUN_TEST_CASE(component_write_ir_failure_checks_and_same_context_recovery);
    RUN_TEST_CASE(component_write_ir_verifier_rejects_invalid_metadata_and_provenance);
    RUN_TEST_CASE(component_write_ir_write_is_live_and_invalidates_reads);
    RUN_TEST_CASE(component_write_ir_prepared_effects_preflight_and_schema);
    pu_clear_setup();
}
