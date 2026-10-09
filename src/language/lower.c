#include "language/lower.h"

#include "ecs/execution.h"
#include "execution/delay.h"
#include "execution/intrinsics.h"
#include "execution/scalar.h"
#include "execution/state.h"
#include "execution/task.h"
#include "language/identity.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/catalog.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int *breaks, *continues;
} lower_loop;
typedef struct
{
    const script_analysis *a;
    const script_catalog *catalog;
    const script_lower_bindings *bindings;
    execution_module_plan *module;
    behavior_function_builder *builder;
    behavior_function_builder **builders;
    int **function_addresses;
    int *captured;
    int function, pc, payload_first, payload_function;
    int *locals, *variables, *parameters, *results, *labels, *addresses;
    int *goto_pc, *goto_symbol;
    lower_loop *loops;
    bool failed, initializing, async_emitted;
    int initializer_depth;
    int target_expression, target_reference, target_value;
    bool target_cached;
    bool argument_cached;
    int cached_argument, cached_value;
} lower;
static void error(lower *l, script_range range, const char *message)
{
    execution_module_diagnostic d = {
        .range = { range.start, range.end, range.line, range.column }
    };
    snprintf(d.message, sizeof d.message, "%s", message);
    apush(l->module->diagnostics, d);
    l->failed = true;
}
static void builder_error(lower *l, behavior_function_builder *builder, script_range range, const char *message)
{
    int pc;
    const behavior_function *f = behavior_function_builder_definition(builder);
    if (sscanf(message, "instruction %d:", &pc) == 1) {
        const behavior_function_source *origin = behavior_function_instruction_source(f, pc);
        if (origin) {
            range = (script_range){ origin->start,
                                    origin->end,
                                    origin->line,
                                    origin->column };
        }
    }
    error(l, range, message);
}
static pin_type slot_type(script_type type)
{
    return type.storage == PIN_STRUCT || type.container != PIN_CONTAINER_SINGLE
             ? BF_POINTER_TYPE
             : type.storage;
}
static pin_value initial(lower *l, script_type type)
{
    pin_value value = { .type = type.storage, .container = type.container };
    const script_catalog_type *nominal = script_catalog_type_by_id(l->catalog, type.nominal);
    if (nominal) {
        value.type_name = nominal->name;
    } else if (
        type.storage == PIN_HANDLE && type.nominal && type.nominal == l->catalog->entity_type
    ) {
        value.type_name = "ecs.entity";
    }
    return value;
}
static pin_value signature_initial(lower *l, script_type type)
{
    if (type.flags & SCRIPT_TYPE_REFERENCE) {
        pin_value value = initial(l, type);
        value.type = BF_POINTER_TYPE;
        value.container = PIN_CONTAINER_SINGLE;
        value.u64 = 0;
        return value;
    }
    return initial(l, type);
}
static behavior_value_kind bank_kind(lower *l, behavior_value_bank bank, int index)
{
    return behavior_function_kind(behavior_function_builder_definition(l->builder), bank, index);
}
static void set_kind(lower *l, behavior_value_bank bank, int index, behavior_value_kind kind)
{
    bool set = behavior_function_builder_kind(l->builder, bank, index, kind);
    assert(set);
    (void)set;
}
static behavior_value_kind payload_kind(script_type type)
{
    return type.storage == PIN_STRUCT || type.container != PIN_CONTAINER_SINGLE
             ? BEHAVIOR_ADDRESS
             : BEHAVIOR_VALUE;
}
static int emit(lower *l, behavior_function_instruction i, script_range range)
{
    i.source = behavior_function_builder_origin(
        l->builder,
        (behavior_function_source){ .asset_name = l->a->name,
                                    .start = range.start,
                                    .end = range.end,
                                    .line = range.line,
                                    .column = range.column,
                                    .parent = -1 }
    );
    if (i.op == BF_COMMAND_BEGIN || i.op == BF_DELAY || i.op == BF_SUSPEND) {
        l->async_emitted = true;
    }
    if (i.op == BF_SAVE && i.left >= 0) {
        set_kind(l, BEHAVIOR_BANK_LOCAL, i.target, bank_kind(l, BEHAVIOR_BANK_SLOT, i.left));
    }
    i.stack = -1;
    int pc = behavior_function_builder_emit(l->builder, i);
    l->pc = pc + 1;
    return pc;
}
static int value_op(lower *l, behavior_function_instruction i, script_range range)
{
    if (i.type == PIN_FLOW) {
        char message[192];
        int symbol = l->function >= 0 ? l->a->functions[l->function].symbol : -1;
        const char *name = symbol >= 0 ? l->a->symbols[symbol].name : "initializer";
        snprintf(message, sizeof message, "Operation %d in %s requires a value type", i.op, name);
        error(l, range, message);
        return -1;
    }
    i.output = behavior_function_builder_slot(
        l->builder,
        i.op >= BF_LT && i.op <= BF_NE ? PIN_BOOL : i.type
    );
    behavior_value_kind kind = BEHAVIOR_VALUE;
    switch (i.op) {
        case BF_ARGUMENT:
            kind = bank_kind(l, BEHAVIOR_BANK_ARGUMENT, i.left);
            break;
        case BF_LOCAL: kind = bank_kind(l, BEHAVIOR_BANK_LOCAL, i.left); break;
        case BF_ARRAY:
        case BF_TYPE_NAME:
        case BF_STORAGE_NAME:
        case BF_HOST_SLOT:
        case BF_HOST_SERVICE:
        case BF_COMMAND_SERVICE:
        case BF_INDEX:
        case BF_ROW:
        case BF_QUERY:
        case BF_WORLD:
        case BF_OWN_NAME:
        case BF_COPY_ARRAY: kind = BEHAVIOR_ADDRESS; break;
        case BF_COMPONENT:
        case BF_COLUMN:
        case BF_COLUMN_TRY:
            kind = i.type == PIN_BOOL ? BEHAVIOR_VALUE : BEHAVIOR_ADDRESS;
            break;
        case BF_CONTEXT:
            if (i.target == 1 ||
                i.offset == offsetof(behavior_function_context, arguments) ||
                i.offset == offsetof(behavior_function_context, results)) {
                kind = BEHAVIOR_ADDRESS;
            }
            break;
        case BF_SELECT: kind = bank_kind(l, BEHAVIOR_BANK_SLOT, i.right); break;
        case BF_ADD:
            if (bank_kind(l, BEHAVIOR_BANK_SLOT, i.left) == BEHAVIOR_ADDRESS) {
                kind = BEHAVIOR_ADDRESS;
            }
            break;
        default: break;
    }
    set_kind(l, BEHAVIOR_BANK_SLOT, i.output, kind);
    emit(l, i, range);
    return i.output;
}
static int kind_op(lower *l, behavior_function_instruction i, script_range range, behavior_value_kind kind)
{
    int value = value_op(l, i, range);
    if (value >= 0) {
        set_kind(l, BEHAVIOR_BANK_SLOT, value, kind);
    }
    return value;
}
static int typed_op(lower *l, behavior_function_instruction i, script_range range, script_type type)
{
    return kind_op(l, i, range, payload_kind(type));
}
static int constant(lower *l, pin_value value, script_range range)
{
    return value_op(
        l,
        (behavior_function_instruction){ .op = BF_CONSTANT, .type = value.type, .value = value },
        range
    );
}
static int expression(lower *, int);
static void debug_input(lower *, const script_expression *, int ordinal, int value, script_type);
static void debug_point(lower *, script_record_kind, int record, script_range);
static int condition_value(lower *, int);
static uint64_t function_id(const script_analysis *, int);
static const script_symbol *action_symbol(lower *l, const script_expression *expression)
{
    if (expression->instance) {
        for (int i = 0; i < l->a->symbol_count; i++) {
            if (l->a->symbols[i].kind == SCRIPT_ACTION &&
                l->a->symbols[i].id == expression->instance) {
                return &l->a->symbols[i];
            }
        }
    }
    if (expression->symbol >= 0 &&
        l->a->symbols[expression->symbol].kind == SCRIPT_ACTION &&
        l->a->symbols[expression->symbol].initializer >= 0) {
        return &l->a->symbols[expression->symbol];
    }
    return NULL;
}
static int reference(lower *, int);
static int reference_expression(lower *, const script_expression *);
static void statement(lower *, int);
static void assign(lower *, const script_statement *);
static void assign_value(lower *, const script_statement *, int);
static void store_symbol(lower *, int, int, script_range);
static int convert(lower *l, int slot, script_type from, script_type to, script_range range)
{
    if (slot_type(from) == slot_type(to)) {
        return slot;
    }
    if (from.container == PIN_CONTAINER_SINGLE && to.container == PIN_CONTAINER_SINGLE &&
        behavior_scalar_string(from.storage) && behavior_scalar_string(to.storage)) {
        pin_value header = initial(l, to);
        int host = behavior_function_builder_host(
            l->builder,
            &EXECUTION_snapshot_call,
            &EXECUTION_value_access,
            &header,
            1,
            &header,
            1
        );
        uint64_t id = execution_intrinsic_id(&EXECUTION_snapshot_call);
        bool identified = behavior_function_builder_host_identity(
            l->builder,
            host,
            id,
            execution_intrinsic_abi(id)
        );
        assert(identified);
        (void)identified;
        int address = value_op(
            l,
            (behavior_function_instruction){
                .op = BF_HOST_SLOT,
                .type = BF_POINTER_TYPE,
                .left = behavior_function_builder_host_first(l->builder, host) },
            range
        );
        emit(
            l,
            (behavior_function_instruction){ .op = BF_WRITE,
                                             .type = from.storage,
                                             .left = slot,
                                             .right = address,
                                             .offset = offsetof(pin_value, d) },
            range
        );
        return value_op(
            l,
            (behavior_function_instruction){ .op = BF_LOAD,
                                             .type = to.storage,
                                             .left = address,
                                             .offset = offsetof(pin_value, d) },
            range
        );
    }
    bool float_conversion = (from.storage == PIN_F32 && to.storage == PIN_F64) ||
                            (from.storage == PIN_F64 && to.storage == PIN_F32);
    return value_op(
        l,
        (behavior_function_instruction){ .op = float_conversion ? BF_CONVERT : BF_CAST,
                                         .type = slot_type(to),
                                         .left = slot },
        range
    );
}
static behavior_function_op binary_op(script_operator op)
{
    switch (op) {
        case SCRIPT_OP_ADD: return BF_ADD;
        case SCRIPT_OP_SUB: return BF_SUB;
        case SCRIPT_OP_MUL: return BF_MUL;
        case SCRIPT_OP_DIV: return BF_DIV;
        case SCRIPT_OP_MOD: return BF_MOD;
        case SCRIPT_OP_EQ: return BF_EQ;
        case SCRIPT_OP_NE: return BF_NE;
        case SCRIPT_OP_LT: return BF_LT;
        case SCRIPT_OP_LE: return BF_LE;
        case SCRIPT_OP_GT: return BF_GT;
        case SCRIPT_OP_GE: return BF_GE;
        case SCRIPT_OP_BIT_AND: return BF_BIT_AND;
        case SCRIPT_OP_BIT_OR: return BF_BIT_OR;
        case SCRIPT_OP_BIT_XOR: return BF_BIT_XOR;
        case SCRIPT_OP_SHIFT_LEFT: return BF_SHL;
        case SCRIPT_OP_SHIFT_RIGHT: return BF_SHR;
        default: return BF_END;
    }
}
static int binary(lower *l, script_operator op, int left, int right, script_type type, script_type operand, script_range range)
{
    behavior_function_op code = binary_op(op);
    if (code == BF_END) {
        error(l, range, "Execution ABI lacks the requested bitwise operation");
        return -1;
    }
    if ((op == SCRIPT_OP_EQ || op == SCRIPT_OP_NE) &&
        behavior_scalar_string(operand.storage)) {
        int equal = value_op(
            l,
            (
                behavior_function_instruction
            ){ .op = BF_STRING_EQUAL, .type = PIN_BOOL, .left = left, .right = right },
            range
        );
        if (op == SCRIPT_OP_EQ) {
            return equal;
        }
        return value_op(
            l,
            (behavior_function_instruction){
                .op = BF_EQ,
                .type = PIN_BOOL,
                .left = equal,
                .right = constant(l, (pin_value){ .type = PIN_BOOL }, range) },
            range
        );
    }
    return value_op(
        l,
        (behavior_function_instruction){ .op = code,
                                         .type = code >= BF_LT && code <= BF_NE
                                                   ? slot_type(operand)
                                                   : slot_type(type),
                                         .left = left,
                                         .right = right,
                                         .target = -1 },
        range
    );
}
static int short_circuit(lower *l, const script_expression *e)
{
    int left = condition_value(l, e->left);
    int local = behavior_function_builder_local(l->builder, (pin_value){ .type = PIN_BOOL });
    emit(
        l,
        (
            behavior_function_instruction
        ){ .op = BF_SAVE, .type = PIN_BOOL, .left = left, .target = local },
        e->range
    );
    int branch = emit(
        l,
        (behavior_function_instruction){ .op = BF_BRANCH, .left = left, .target = -1 },
        e->range
    );
    int skip = -1;
    if (e->op == SCRIPT_OP_AND) {
        skip = emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = -1 }, e->range);
        behavior_function_builder_patch_target(l->builder, branch, l->pc);
    }
    int right = condition_value(l, e->right);
    emit(
        l,
        (
            behavior_function_instruction
        ){ .op = BF_SAVE, .type = PIN_BOOL, .left = right, .target = local },
        e->range
    );
    behavior_function_builder_patch_target(l->builder, skip >= 0 ? skip : branch, l->pc);
    return value_op(
        l,
        (behavior_function_instruction){ .op = BF_LOCAL, .type = PIN_BOOL, .left = local },
        e->range
    );
}
static int host_address(lower *l, int bank, bool aggregate, script_range range)
{
    return value_op(
        l,
        (behavior_function_instruction){ .op = BF_HOST_SLOT,
                                         .type = BF_POINTER_TYPE,
                                         .left = bank,
                                         .target = aggregate },
        range
    );
}
static void host_argument(lower *l, int bank, int value, script_type type, script_range range)
{
    int address = host_address(l, bank, false, range);
    emit(
        l,
        (behavior_function_instruction){ .op = BF_WRITE,
                                         .type = slot_type(type),
                                         .left = value,
                                         .right = address,
                                         .offset = offsetof(pin_value, d) },
        range
    );
}
static int host_result(lower *l, int bank, script_type type, script_range range)
{
    int address = host_address(l, bank, false, range);
    return kind_op(
        l,
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = slot_type(type),
                                         .left = address,
                                         .offset = offsetof(pin_value, d) },
        range,
        payload_kind(type) == BEHAVIOR_ADDRESS ? BEHAVIOR_ADDRESS
                                               : bank_kind(l, BEHAVIOR_BANK_DATA, bank)
    );
}
static int linked_host(
    behavior_function_builder *builder,
    const node_host_call *call,
    const node_access *access,
    const pin_value *args,
    int count,
    const pin_value *results,
    int result_count
)
{
    int index = behavior_function_builder_host(builder, call, access, args, count, results, result_count);
    uint64_t id = execution_intrinsic_id(call);
    if (id) {
        execution_intrinsic intrinsic = (execution_intrinsic)(id & UINT64_C(0xffffffff));
        int first = behavior_function_builder_host_first(builder, index);
        for (int i = 0; i < count; i++) {
            behavior_value_kind kind = BEHAVIOR_VALUE;
            if (i == 0) {
                switch (intrinsic) {
                    case EXECUTION_INTRINSIC_REFERENCE_FIELD:
                    case EXECUTION_INTRINSIC_REFERENCE_READ:
                    case EXECUTION_INTRINSIC_REFERENCE_WRITE:
                    case EXECUTION_INTRINSIC_LOCAL_RELEASE:
                        kind = BEHAVIOR_REFERENCE;
                        break;
                    case EXECUTION_INTRINSIC_REFERENCE_MAKE:
                    case EXECUTION_INTRINSIC_ACTION_READ:
                    case EXECUTION_INTRINSIC_ACTION_COMPLETE:
                    case EXECUTION_INTRINSIC_COMPONENT_HAS:
                    case EXECUTION_INTRINSIC_COMPONENT_MEMBERSHIP:
                    case EXECUTION_INTRINSIC_BREAK_REQUEST:
                    case EXECUTION_INTRINSIC_BREAK_TAKE:
                    case EXECUTION_INTRINSIC_STATE_DEMAND:
                    case EXECUTION_INTRINSIC_STATE_TRANSITION:
                    case EXECUTION_INTRINSIC_RETRIGGER_ACTION:
                    case EXECUTION_INTRINSIC_EVENT_READ:
                        kind = BEHAVIOR_ADDRESS;
                        break;
                    default: break;
                }
            }
            if (intrinsic == EXECUTION_INTRINSIC_REFERENCE_MAKE && i == 4) {
                kind = BEHAVIOR_ADDRESS;
            }
            bool set = behavior_function_builder_kind(builder, BEHAVIOR_BANK_DATA, first + i, kind);
            assert(set);
            (void)set;
        }
        if (intrinsic == EXECUTION_INTRINSIC_REFERENCE_MAKE ||
            intrinsic == EXECUTION_INTRINSIC_REFERENCE_FIELD) {
            bool set = behavior_function_builder_kind(
                builder,
                BEHAVIOR_BANK_DATA,
                behavior_function_builder_host_results(builder, index),
                BEHAVIOR_REFERENCE
            );
            assert(set);
            (void)set;
        }
    }
    if (id) {
        bool identified = behavior_function_builder_host_identity(
            builder,
            index,
            id,
            execution_intrinsic_abi(id)
        );
        assert(identified);
        (void)identified;
    }
    return index;
}
static int primitive(
    lower *l,
    const node_host_call *call,
    const node_access *access,
    const pin_value *headers,
    const int *slots,
    int count,
    script_type result,
    script_range range
)
{
    pin_value output = initial(l, result);
    int host = linked_host(l->builder, call, access, headers, count, &output, 1);
    int first = behavior_function_builder_host_first(l->builder, host);
    for (int i = 0; i < count; i++) {
        if (slots[i] >= 0) {
            script_type type = { .storage = headers[i].type,
                                 .container = headers[i].container };
            host_argument(l, first + i, slots[i], type, range);
        }
    }
    emit(
        l,
        (behavior_function_instruction){ .op = BF_HOST_CALL,
                                         .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                         .target = host },
        range
    );
    return host_result(l, behavior_function_builder_host_results(l->builder, host), result, range);
}
static int bank_reference(lower *l, int bank, script_type type, script_range range)
{
    pin_value headers[] = { { .type = BF_POINTER_TYPE },
                            initial(l, type),
                            { .type = PIN_I32, .i = LANGUAGE_REFERENCE_VALUE },
                            { .type = PIN_I32, .i = -1 },
                            { .type = BF_POINTER_TYPE },
                            { .type = PIN_HANDLE },
                            { .type = PIN_U64 },
                            { .type = PIN_U64 },
                            { .type = PIN_U64 } };
    int slots[] = { value_op(
                        l,
                        (behavior_function_instruction){ .op = BF_CONTEXT,
                                                         .type = BF_POINTER_TYPE,
                                                         .target = 1 },
                        range
                    ),
                    -1,
                    -1,
                    -1,
                    host_address(l, bank, false, range),
                    -1,
                    -1,
                    -1,
                    -1 };
    return primitive(
        l,
        &EXECUTION_reference_make_call,
        &EXECUTION_value_access,
        headers,
        slots,
        9,
        (script_type){ .storage = BF_POINTER_TYPE },
        range
    );
}
static int reference_read(lower *l, int descriptor, script_type type, script_range range)
{
    pin_value header = { .type = BF_POINTER_TYPE };
    return primitive(
        l,
        &EXECUTION_reference_read_call,
        &EXECUTION_assignment_access,
        &header,
        &descriptor,
        1,
        type,
        range
    );
}
static void reference_write(lower *l, int descriptor, int value, script_type type, script_range range)
{
    pin_value headers[] = { { .type = BF_POINTER_TYPE }, initial(l, type) };
    int slots[] = { descriptor, value };
    primitive(
        l,
        &EXECUTION_reference_write_call,
        &EXECUTION_assignment_access,
        headers,
        slots,
        2,
        (script_type){ .storage = PIN_BOOL },
        range
    );
}
static void suspend_if(lower *l, int condition, uint64_t action, script_range range)
{
    int branch = emit(
        l,
        (behavior_function_instruction){ .op = BF_BRANCH, .left = condition, .target = -1 },
        range
    );
    int done = emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = -1 }, range);
    behavior_function_builder_patch_target(l->builder, branch, l->pc);
    emit(l, (behavior_function_instruction){ .op = BF_SUSPEND, .node_id = action }, range);
    behavior_function_builder_patch_target(l->builder, done, l->pc);
}
static int action_value(lower *l, uint64_t action, int ordinal, script_type type, script_range range)
{
    pin_value headers[] = { { .type = BF_POINTER_TYPE },
                            { .type = PIN_U64, .u64 = action },
                            { .type = PIN_I32, .i = ordinal } };
    int slots[] = { value_op(
                        l,
                        (behavior_function_instruction){ .op = BF_CONTEXT,
                                                         .type = BF_POINTER_TYPE,
                                                         .target = 1 },
                        range
                    ),
                    -1,
                    -1 };
    return primitive(l, &EXECUTION_action_read_call, &EXECUTION_assignment_access, headers, slots, 3, type, range);
}
static void exit_if_requested(lower *l, script_range range)
{
    int requested = value_op(
        l,
        (behavior_function_instruction){
            .op = BF_CONTEXT,
            .type = PIN_BOOL,
            .offset = offsetof(behavior_function_context, exit_requested) },
        range
    );
    int finish = emit(
        l,
        (behavior_function_instruction){ .op = BF_BRANCH, .left = requested, .target = -1 },
        range
    );
    int next = emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = -1 }, range);
    behavior_function_builder_patch_target(l->builder, finish, l->pc);
    emit(l, (behavior_function_instruction){ .op = BF_RETURN }, range);
    behavior_function_builder_patch_target(l->builder, next, l->pc);
}
static void action_complete(
    lower *l,
    uint64_t action,
    const script_type *types,
    const int *values,
    int count,
    script_range range
)
{
    pin_value *headers = NULL;
    int *slots = NULL;
    apush(headers, ((pin_value){ .type = BF_POINTER_TYPE }));
    apush(headers, ((pin_value){ .type = PIN_U64, .u64 = action }));
    apush(
        slots,
        value_op(l, (behavior_function_instruction){ .op = BF_CONTEXT, .type = BF_POINTER_TYPE, .target = 1 }, range)
    );
    apush(slots, -1);
    for (int i = 0; i < count; i++) {
        apush(headers, initial(l, types[i]));
        apush(slots, values[i]);
    }
    int pending = primitive(
        l,
        &EXECUTION_action_complete_call,
        &EXECUTION_assignment_access,
        headers,
        slots,
        acount(headers),
        (script_type){ .storage = PIN_BOOL },
        range
    );
    afree(headers);
    afree(slots);
    suspend_if(l, pending, action, range);
    exit_if_requested(l, range);
}
static int retain_value(lower *l, int slot, script_type type, script_range range)
{
    pin_value value = initial(l, type);
    int host = linked_host(
        l->builder,
        &EXECUTION_snapshot_call,
        &EXECUTION_value_access,
        &value,
        1,
        &value,
        1
    );
    int first = behavior_function_builder_host_first(l->builder, host);
    host_argument(l, first, slot, type, range);
    emit(
        l,
        (behavior_function_instruction){ .op = BF_HOST_CALL,
                                         .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                         .target = host },
        range
    );
    return host_result(l, behavior_function_builder_host_results(l->builder, host), type, range);
}
static int constructor_from(lower *l, const script_expression *e, int base)
{
    pin_value *values = NULL;
    apush(values, initial(l, e->type));
    const script_catalog_type
        *type = script_catalog_type_by_id(l->catalog, e->type.nominal);
    for (int i = 0; i < e->argument_count; i++) {
        const script_argument *a = &e->arguments[i];
        if (e->kind == SCRIPT_EXPR_STRUCT) {
            const script_catalog_field *field = NULL;
            for (int j = 0; type && j < type->field_count; j++) {
                if (type->fields[j].id == a->member) {
                    field = &type->fields[j];
                    break;
                }
            }
            if (!field) {
                error(l, a->range, "Constructor field has no reflected storage layout");
                afree(values);
                return -1;
            }
            apush(values, ((pin_value){ .type = PIN_U64, .u64 = field->offset }));
        }
        script_type expected = e->type;
        expected.container = PIN_CONTAINER_SINGLE;
        if (e->kind == SCRIPT_EXPR_STRUCT && type && a->parameter >= 0 &&
            a->parameter < type->field_count) {
            expected = type->fields[a->parameter].type;
        }
        apush(values, initial(l, expected));
    }
    pin_value result = initial(l, e->type);
    int host = linked_host(
        l->builder,
        &EXECUTION_construct_call,
        &EXECUTION_value_access,
        values,
        acount(values),
        &result,
        1
    );
    afree(values);
    int first = behavior_function_builder_host_first(l->builder, host), cursor = 1;
    if (base >= 0) {
        host_argument(l, first, base, e->type, e->range);
    }
    for (int i = 0; i < e->argument_count; i++) {
        const script_argument *a = &e->arguments[i];
        if (e->kind == SCRIPT_EXPR_STRUCT) {
            cursor++;
        }
        script_type expected = e->type;
        expected.container = PIN_CONTAINER_SINGLE;
        if (e->kind == SCRIPT_EXPR_STRUCT && type && a->parameter >= 0 &&
            a->parameter < type->field_count) {
            expected = type->fields[a->parameter].type;
        }
        int slot = expression(l, a->value);
        slot = convert(l, slot, l->a->expressions[a->value].type, expected, a->range);
        debug_input(l, e, a->parameter, slot, expected);
        host_argument(l, first + cursor++, slot, expected, a->range);
    }
    emit(
        l,
        (behavior_function_instruction){ .op = BF_HOST_CALL,
                                         .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                         .target = host },
        e->range
    );
    return host_result(l, behavior_function_builder_host_results(l->builder, host), e->type, e->range);
}
static int constructor(lower *l, const script_expression *e)
{
    return constructor_from(l, e, -1);
}
static bool concrete_function(const script_analysis *a, const script_function *f)
{
    for (int i = 0; i < f->type_parameter_count; i++) {
        script_type type = a->symbols[f->type_parameters[i]].type;
        if (type.storage == PIN_FLOW ||
            (type.flags & (SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_ERROR)) ||
            (type.storage == PIN_STRUCT && !type.nominal)) {
            return false;
        }
    }
    return true;
}
static int authored_call(lower *l, const script_expression *e)
{
    int index = e->entry >= 0 ? e->entry : e->right;
    if (l->initializing) {
        error(l, e->range, "State initializer authored call requires separately linked constant definitions");
        return -1;
    }
    if (index < 0 || index >= l->a->function_count || l->a->functions[index].disabled) {
        error(l, e->range, "Authored call has no executable definition");
        return -1;
    }
    const script_function *f = &l->a->functions[index];
    const script_function *output = f->kind == SCRIPT_FUNCTION_MACRO_ENTRY &&
                                            f->parent >= 0
                                      ? &l->a->functions[f->parent]
                                      : f;
    if (!concrete_function(l->a, f)) {
        error(l, e->range, "Generic call requires monomorphized signatures before execution linking");
        return -1;
    }
    pin_value *args = NULL, *results = NULL;
    for (int i = 0; i < f->parameter_count; i++) {
        apush(args, signature_initial(l, l->a->symbols[f->parameters[i]].type));
    }
    for (int i = 0; !e->task && i < output->result_count; i++) {
        apush(results, initial(l, l->a->symbols[output->results[i]].type));
    }
    const behavior_function *callee = l->module->functions[index].body
                                        ? l->module->functions[index].body
                                        : behavior_function_builder_definition(
                                              l->builders[index]
                                          );
    int invocation = e->task
                       ? behavior_function_builder_task(l->builder, callee, args, acount(args))
                       : behavior_function_builder_call(
                             l->builder,
                             callee,
                             args,
                             acount(args),
                             results,
                             acount(results)
                         );
    int argument_first = behavior_function_builder_call_first(l->builder, invocation);
    for (int i = 0; i < f->parameter_count; i++) {
        if (l->a->symbols[f->parameters[i]].type.flags & SCRIPT_TYPE_REFERENCE) {
            set_kind(l, BEHAVIOR_BANK_DATA, argument_first + i, BEHAVIOR_REFERENCE);
        }
    }
    afree(args);
    afree(results);
    int first = behavior_function_builder_call_first(l->builder, invocation);
    for (int i = 0; i < e->argument_count; i++) {
        const script_argument *a = &e->arguments[i];
        if (a->parameter < 0 || a->parameter >= f->parameter_count) {
            error(l, a->range, "Authored call argument does not match its signature");
            continue;
        }
        script_type expected = l->a->symbols[f->parameters[a->parameter]].type;
        int value;
        if (expected.flags & SCRIPT_TYPE_REFERENCE) {
            value = reference(l, a->value);
            debug_input(l, e, a->parameter, value, expected);
            host_argument(l, first + a->parameter, value, (script_type){ .storage = BF_POINTER_TYPE }, a->range);
        } else {
            value = expression(l, a->value);
            value = convert(l, value, l->a->expressions[a->value].type, expected, a->range);
            debug_input(l, e, a->parameter, value, expected);
            host_argument(l, first + a->parameter, value, expected, a->range);
        }
    }
    emit(l, (behavior_function_instruction){ .op = BF_INVOKE, .left = invocation }, e->range);
    int result_first = behavior_function_builder_call_results(l->builder, invocation);
    if (e->task) {
        return host_result(l, result_first, e->type, e->range);
    }
    if (e->instance) {
        script_type *types = NULL;
        int *values = NULL;
        for (int i = 0; i < output->result_count; i++) {
            script_type type = l->a->symbols[output->results[i]].type;
            apush(types, type);
            apush(values, host_result(l, result_first + i, type, e->range));
        }
        action_complete(l, e->instance, types, values, acount(types), e->range);
        afree(types);
        afree(values);
    }
    if (e->type.flags & SCRIPT_TYPE_RESULTS) {
        return result_first;
    }
    if (!output->result_count) {
        return -1;
    }
    return host_result(l, result_first, e->type, e->range);
}
static int external_call(
    lower *l,
    const script_expression *e,
    const script_catalog_capability *cap,
    script_lower_host host
)
{
    if (!host.function || !host.module) {
        error(l, e->range, "Authored import has no valid owning executable generation");
        return -1;
    }
    bool retained = false;
    for (int i = 0; i < l->module->import_count; i++) {
        retained |= l->module->imports[i] == host.module;
    }
    if (!retained) {
        execution_module_retain(host.module);
        apush(l->module->imports, host.module);
        l->module->import_count = acount(l->module->imports);
    }
    int argument_count, result_count;
    const pin_value *args = behavior_function_arguments(host.function, &argument_count);
    const pin_value *results = behavior_function_results(host.function, &result_count);
    if (argument_count != cap->parameter_count || result_count != cap->result_count) {
        error(l, e->range, "Imported function signature differs from the declaration catalog");
        return -1;
    }
    int invocation = e->task
                       ? behavior_function_builder_task(l->builder, host.function, args, argument_count)
                       : behavior_function_builder_call(
                             l->builder,
                             host.function,
                             args,
                             argument_count,
                             results,
                             result_count
                         );
    int first = behavior_function_builder_call_first(l->builder, invocation);
    for (int i = 0; i < argument_count; i++) {
        set_kind(
            l,
            BEHAVIOR_BANK_DATA,
            first + i,
            behavior_function_kind(host.function, BEHAVIOR_BANK_ARGUMENT, i)
        );
    }
    int output_first = behavior_function_builder_call_results(l->builder, invocation);
    for (int i = 0; !e->task && i < result_count; i++) {
        set_kind(
            l,
            BEHAVIOR_BANK_DATA,
            output_first + i,
            behavior_function_kind(host.function, BEHAVIOR_BANK_RESULT, i)
        );
    }
    for (int i = 0; i < e->argument_count; i++) {
        const script_argument *arg = &e->arguments[i];
        if (arg->parameter < 0 || arg->parameter >= argument_count) {
            error(l, arg->range, "Imported function argument has no parameter");
            continue;
        }
        script_type expected = cap->parameters[arg->parameter].type;
        if (expected.flags & SCRIPT_TYPE_REFERENCE) {
            int value = reference(l, arg->value);
            debug_input(l, e, arg->parameter, value, expected);
            host_argument(
                l,
                first + arg->parameter,
                value,
                (script_type){ .storage = BF_POINTER_TYPE },
                arg->range
            );
        } else {
            int value = expression(l, arg->value);
            value = convert(l, value, l->a->expressions[arg->value].type, expected, arg->range);
            debug_input(l, e, arg->parameter, value, expected);
            host_argument(l, first + arg->parameter, value, expected, arg->range);
        }
    }
    emit(l, (behavior_function_instruction){ .op = BF_INVOKE, .left = invocation }, e->range);
    int result_first = behavior_function_builder_call_results(l->builder, invocation);
    if (e->task) {
        return host_result(l, result_first, e->type, e->range);
    }
    if (e->instance) {
        script_type *types = NULL;
        int *values = NULL;
        for (int i = 0; i < result_count; i++) {
            script_type type = i < e->result_count ? e->results[i]
                                                   : cap->results[i].type;
            apush(types, type);
            apush(values, host_result(l, result_first + i, type, e->range));
        }
        action_complete(l, e->instance, types, values, acount(types), e->range);
        afree(types);
        afree(values);
    }
    if (e->type.flags & SCRIPT_TYPE_RESULTS) {
        return result_first;
    }
    return result_count ? host_result(l, result_first, e->type, e->range) : -1;
}
static int command_result(lower *l, int bank, script_type type, script_range range)
{
    return typed_op(
        l,
        (behavior_function_instruction){ .op = BF_COMMAND_RESULT,
                                         .type = slot_type(type),
                                         .left = bank,
                                         .offset = offsetof(pin_value, d) },
        range,
        type
    );
}
static int argument_count(lower *l, const script_expression *expression, const script_catalog_capability *cap)
{
    int count = cap->parameter_count;
    if (cap->variadic && count) {
        for (int i = 0; i < expression->argument_count; i++) {
            if (expression->arguments[i].parameter >= count) {
                count = expression->arguments[i].parameter + 1;
            }
        }
    }
    if (cap->variadic_optional && cap->parameter_count) {
        while (count >= cap->parameter_count) {
            const script_expression *spare = NULL;
            for (int i = 0; i < expression->argument_count; i++) {
                if (expression->arguments[i].parameter == count - 1) {
                    spare = &l->a->expressions[expression->arguments[i].value];
                    break;
                }
            }
            if (!spare || spare->type.storage != PIN_FLOW ||
                !(spare->type.flags & SCRIPT_TYPE_UNTYPED) ||
                (spare->kind != SCRIPT_EXPR_NIL && spare->kind != SCRIPT_EXPR_CONSTANT)) {
                break;
            }
            count--;
        }
    }
    return count;
}
static const script_catalog_parameter *call_parameter(const script_catalog_capability *cap, int index)
{
    assert(index >= 0 && cap->parameter_count > 0);
    return &cap->parameters[index < cap->parameter_count ? index : cap->parameter_count - 1];
}
static pin_value *argument_defaults(lower *l, const script_expression *e, const script_catalog_capability *cap)
{
    pin_value *values = NULL;
    int count = argument_count(l, e, cap);
    for (int i = 0; i < count; i++) {
        const script_catalog_parameter *declared = call_parameter(cap, i);
        script_type type = declared->type;
        bool wildcard = type.storage == PIN_FLOW || (type.flags & SCRIPT_TYPE_UNTYPED);
        if (wildcard) {
            for (int j = 0; j < e->argument_count; j++) {
                if (e->arguments[j].parameter == i) {
                    type = l->a->expressions[e->arguments[j].value].type;
                }
            }
        }
        apush(values, declared->has_default && !wildcard ? declared->default_value : initial(l, type));
    }
    return values;
}
static void command_arguments(lower *l, int first, const script_expression *e, const script_catalog_capability *cap)
{
    for (int i = 0; i < e->argument_count; i++) {
        const script_argument *arg = &e->arguments[i];
        if (cap->variadic_optional && arg->parameter >= argument_count(l, e, cap)) {
            continue;
        }
        if (arg->parameter < 0 || arg->parameter >= argument_count(l, e, cap)) {
            error(l, arg->range, "Command argument has no matching parameter");
            continue;
        }
        script_type expected = call_parameter(cap, arg->parameter)->type;
        if (expected.storage == PIN_FLOW || (expected.flags & SCRIPT_TYPE_UNTYPED)) {
            expected = l->a->expressions[arg->value].type;
        }
        int value = expression(l, arg->value);
        value = convert(l, value, l->a->expressions[arg->value].type, expected, arg->range);
        value = retain_value(l, value, expected, arg->range);
        debug_input(l, e, arg->parameter, value, expected);
        emit(
            l,
            (behavior_function_instruction){ .op = BF_COMMAND_ARGUMENT,
                                             .type = slot_type(expected),
                                             .left = value,
                                             .target = first + arg->parameter,
                                             .offset = offsetof(pin_value, d) },
            arg->range
        );
    }
}
static void complete_results(
    lower *l,
    const script_expression *e,
    const script_catalog_capability *cap,
    int first,
    bool command_bank
)
{
    if (!e->instance) {
        return;
    }
    script_type *types = NULL;
    int *values = NULL;
    for (int i = 0; i < cap->result_count; i++) {
        script_type type = i < e->result_count ? e->results[i] : cap->results[i].type;
        apush(types, type);
        apush(
            values,
            command_bank ? command_result(l, first + i, type, e->range)
                         : host_result(l, first + i, type, e->range)
        );
    }
    action_complete(l, e->instance, types, values, acount(values), e->range);
    afree(types);
    afree(values);
}
/* Suspends until the asynchronous command just begun completes. */
static void await_command(lower *l, uint64_t action, script_range range)
{
    int status = value_op(
        l,
        (behavior_function_instruction){
            .op = BF_CONTEXT,
            .type = PIN_I32,
            .offset = offsetof(behavior_function_context, error_status) },
        range
    );
    int zero = constant(l, (pin_value){ .type = PIN_I32 }, range);
    int waiting = value_op(
        l,
        (
            behavior_function_instruction
        ){ .op = BF_GT, .type = PIN_I32, .left = status, .right = zero },
        range
    );
    int branch = emit(
        l,
        (behavior_function_instruction){ .op = BF_BRANCH, .left = waiting, .target = -1 },
        range
    );
    int done = emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = -1 }, range);
    behavior_function_builder_patch_target(l->builder, branch, l->pc);
    emit(l, (behavior_function_instruction){ .op = BF_SUSPEND, .node_id = action }, range);
    behavior_function_builder_patch_target(l->builder, done, l->pc);
}
/* Yield, Join, Cancel and Running are built-in commands; all but Yield take a
 * task handle. Running returns a bool and Join the task's single result. */
/* Join's results are the task's results: one value, or a tuple whose first
 * command result is returned like an awaited call's. */
static int task_command(lower *l, const node_command *command, int handle, const script_expression *e)
{
    script_type result = e->type;
    script_range range = e->range;
    uint64_t id = execution_task_command_id(command);
    pin_value *args = NULL, *results = NULL;
    if (handle >= 0) {
        apush(args, ((pin_value){ .type = PIN_U64 }));
    }
    if (result.flags & SCRIPT_TYPE_RESULTS) {
        for (int i = 0; i < e->result_count; i++) {
            apush(results, initial(l, e->results[i]));
        }
    } else if (result.storage != PIN_FLOW) {
        apush(results, initial(l, result));
    }
    uint64_t action = id ^ (uint64_t)range.start;
    int index = behavior_function_builder_command(
        l->builder,
        command,
        NULL,
        args,
        acount(args),
        results,
        acount(results),
        action
    );
    afree(args);
    afree(results);
    if (!behavior_function_builder_command_identity(l->builder, index, id, EXECUTION_TASK_ABI)) {
        error(l, range, "Cannot link task command identity");
    }
    if (handle >= 0) {
        emit(
            l,
            (behavior_function_instruction){
                .op = BF_COMMAND_ARGUMENT,
                .type = PIN_U64,
                .left = handle,
                .target = behavior_function_builder_command_first(l->builder, index),
                .offset = offsetof(pin_value, d) },
            range
        );
    }
    emit(l, (behavior_function_instruction){ .op = BF_COMMAND_BEGIN, .target = index, .node_id = action }, range);
    /* A cancel waits so a task that cancels itself stops there. */
    if (command != &EXECUTION_TaskRunning) {
        await_command(l, action, range);
    }
    int first = behavior_function_builder_command_results(l->builder, index);
    if (result.flags & SCRIPT_TYPE_RESULTS) {
        return first;
    }
    return result.storage == PIN_FLOW ? -1 : command_result(l, first, result, range);
}
static int command(lower *l, const script_expression *e, const script_catalog_capability *cap, script_lower_host host)
{
    bool timer = host.command && host.command->intrinsic == NODE_COMMAND_DELAY;
    bool asynchronous = timer || (host.command && host.command->begin &&
                                  (e->awaited || !host.command->invoke));
    if (l->initializing || !host.command ||
        (!timer && (asynchronous ? !host.command->begin : !host.command->invoke))) {
        error(l, e->range, "Command lacks the required synchronous or asynchronous host contract");
        return -1;
    }
    pin_value *args = argument_defaults(l, e, cap), *results = NULL;
    for (int i = 0; i < cap->result_count; i++) {
        script_type result = i < e->result_count ? e->results[i]
                                                 : cap->results[i].type;
        if (!e->result_count && cap->result_count == 1 &&
            !(e->type.flags & SCRIPT_TYPE_RESULTS)) {
            result = e->type;
        }
        apush(results, initial(l, result));
    }
    uint64_t action = e->instance ? e->instance
                    : e->symbol >= 0 &&
                              l->a->symbols[e->symbol].kind == SCRIPT_ACTION &&
                              l->a->symbols[e->symbol].initializer >= 0
                        ? l->a->symbols[e->symbol].id
                        : e->capability ^ (uint64_t)e->range.start;
    int index = behavior_function_builder_command(
        l->builder,
        host.command,
        host.reference,
        args,
        acount(args),
        results,
        acount(results),
        action
    );
    if (timer &&
        !behavior_function_builder_command_identity(l->builder, index, EXECUTION_DELAY_ID, EXECUTION_DELAY_ABI)) {
        error(l, e->range, "Cannot link timer command identity");
    }
    if (!timer && !behavior_function_builder_command_identity(
                      l->builder,
                      index,
                      cap->id,
                      script_catalog_capability_abi(
                          l->catalog,
                          script_catalog_capability_by_id(l->catalog, e->capability)
                      )
                  )) {
        error(l, e->range, "Cannot link command declaration identity");
    }
    if (cap->event_sender) {
        const script_catalog_capability
            *event = script_catalog_capability_by_id(l->catalog, e->member);
        if (!event || !behavior_function_builder_command_event_identity(
                          l->builder,
                          index,
                          event->id,
                          script_catalog_capability_abi(l->catalog, event)
                      )) {
            error(l, e->range, "Cannot link selected event declaration identity");
        }
    }
    afree(args);
    afree(results);
    if (host.result_pins &&
        !behavior_function_builder_command_results_map(l->builder, index, host.result_pins, host.result_count)) {
        error(l, e->range, "Command result pin mapping differs from its linked signature");
    }
    int first = behavior_function_builder_command_first(l->builder, index);
    command_arguments(l, first, e, cap);
    emit(
        l,
        (behavior_function_instruction){ .op = asynchronous ? BF_COMMAND_BEGIN : BF_COMMAND,
                                         .target = index,
                                         .node_id = action },
        e->range
    );
    if (e->awaited) {
        await_command(l, action, e->range);
    }
    int first_result = behavior_function_builder_command_results(l->builder, index);
    if (!asynchronous) {
        complete_results(l, e, cap, first_result, true);
    }
    if (asynchronous) {
        exit_if_requested(l, e->range);
    }
    if (e->type.flags & SCRIPT_TYPE_RESULTS) {
        return first_result;
    }
    return cap->result_count ? command_result(l, first_result, e->type, e->range)
                             : -1;
}
static const script_catalog_type *component_target(
    lower *l,
    const script_expression *e,
    const script_catalog_capability *cap,
    int *entity,
    int *value
)
{
    const script_catalog_type *type = script_catalog_type_by_id(l->catalog, cap->component);
    *entity = *value = -1;
    for (int i = 0; i < e->argument_count; i++) {
        const script_argument *argument = &e->arguments[i];
        if (argument->parameter < 0 || argument->parameter >= cap->parameter_count) {
            continue;
        }
        const script_catalog_parameter *parameter = &cap->parameters[argument->parameter];
        if (parameter->reference_kind == SCRIPT_REFERENCE_COMPONENT) {
            type = script_catalog_type_by_id(
                l->catalog,
                l->a->expressions[argument->value].capability
            );
        } else if (argument->parameter == cap->component_entity) {
            *entity = argument->value;
        } else if (argument->parameter == cap->component_value) {
            *value = argument->value;
        }
    }
    if (!type || !type->component_id || !type->component) {
        error(l, e->range, "Component operation requires a declared component selector");
    }
    return type;
}
static const script_catalog_field *component_field(
    lower *l,
    const script_catalog_type *type,
    uint64_t id,
    script_range range
)
{
    for (int i = 0; i < type->field_count; i++) {
        if (type->fields[i].id == id) {
            return &type->fields[i];
        }
    }
    error(l, range, "Component field has no declared storage layout");
    return NULL;
}
static int component_reference(
    lower *l,
    const script_expression *e,
    const script_catalog_capability *cap,
    const script_catalog_type *type,
    int entity,
    int *root
)
{
    script_type actual = type->type;
    actual.nominal = type->id;
    int descriptor = reference_expression(
        l,
        &(script_expression){ .kind = SCRIPT_EXPR_COMPONENT,
                              .capability = type->id,
                              .type = actual,
                              .left = entity,
                              .symbol = -1,
                              .range = e->range }
    );
    if (root) {
        *root = descriptor;
    }
    if (!cap->component_field) {
        return descriptor;
    }
    const script_catalog_field *field = component_field(l, type, cap->component_field, e->range);
    if (!field) {
        return -1;
    }
    pin_value headers[] = { { .type = BF_POINTER_TYPE },
                            initial(l, field->type),
                            { .type = PIN_U64, .u64 = field->offset },
                            { .type = PIN_BOOL } };
    int slots[] = { descriptor, -1, -1, -1 };
    return primitive(
        l,
        &EXECUTION_reference_field_call,
        &EXECUTION_value_access,
        headers,
        slots,
        4,
        (script_type){ .storage = BF_POINTER_TYPE },
        e->range
    );
}
static int component_call(lower *l, const script_expression *e, const script_catalog_capability *cap)
{
    if (l->initializing) {
        error(l, e->range, "Component operations cannot initialize state defaults");
        return -1;
    }
    int entity, value;
    const script_catalog_type *type = component_target(l, e, cap, &entity, &value);
    if (!type || l->failed) {
        return -1;
    }
    script_type result = type->type;
    result.nominal = type->id;
    if (cap->component_field) {
        const script_catalog_field *field = component_field(l, type, cap->component_field, e->range);
        if (!field) {
            return -1;
        }
        result = field->type;
    }
    if (cap->component_access == SCRIPT_COMPONENT_ACCESS_READ) {
        return reference_read(l, component_reference(l, e, cap, type, entity, NULL), result, e->range);
    }
    if (cap->component_access == SCRIPT_COMPONENT_ACCESS_WRITE) {
        if (value < 0) {
            error(l, e->range, "Component write has no typed value");
            return -1;
        }
        int root;
        int destination = component_reference(l, e, cap, type, entity, &root);
        const script_expression *source = &l->a->expressions[value];
        int updated = source->kind == SCRIPT_EXPR_STRUCT && source->partial
                        ? constructor_from(l, source, reference_read(l, destination, result, e->range))
                        : expression(l, value);
        updated = convert(l, updated, source->type, result, e->range);
        reference_write(l, destination, updated, result, e->range);
        if (cap->component_field && e->type.storage == PIN_STRUCT &&
            e->type.nominal == type->id && e->type.container == PIN_CONTAINER_SINGLE) {
            result = e->type;
            updated = reference_read(l, root, result, e->range);
        }
        if (e->instance) {
            action_complete(l, e->instance, &result, &updated, 1, e->range);
        }
        return updated;
    }
    int owner = entity >= 0
                  ? expression(l, entity)
                  : value_op(
                        l,
                        (behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE },
                        e->range
                    );
    pin_value headers[] = {
        { .type = BF_POINTER_TYPE },
        { .type = PIN_HANDLE },
        { .type = PIN_U64, .u64 = type->component_id },
        { .type = PIN_BOOL, .b = cap->component_access == SCRIPT_COMPONENT_ACCESS_ADD }
    };
    int slots[] = { value_op(
                        l,
                        (behavior_function_instruction){ .op = BF_CONTEXT,
                                                         .type = BF_POINTER_TYPE,
                                                         .target = 1 },
                        e->range
                    ),
                    owner,
                    -1,
                    -1 };
    bool has = cap->component_access == SCRIPT_COMPONENT_ACCESS_HAS;
    behavior_function_builder_component(l->builder, type->component_id, false, !has);
    int output = primitive(
        l,
        has ? &EXECUTION_component_has_call : &EXECUTION_component_membership_call,
        has ? &EXECUTION_component_read_access : &EXECUTION_component_membership_access,
        headers,
        slots,
        has ? 3 : 4,
        (script_type){ .storage = PIN_BOOL },
        e->range
    );
    if (e->instance && !has) {
        script_type boolean = { .storage = PIN_BOOL };
        action_complete(l, e->instance, &boolean, &output, 1, e->range);
    }
    return output;
}
static int struct_call(lower *l, const script_expression *e, const script_catalog_capability *cap)
{
    const script_catalog_type *type = script_catalog_type_by_id(l->catalog, cap->aggregate);
    if (!type) {
        error(l, e->range, "Structural operation has no declared aggregate layout");
        return -1;
    }
    if (cap->struct_access == SCRIPT_STRUCT_ACCESS_MAKE) {
        script_argument *arguments = NULL;
        for (int i = 0; i < e->argument_count; i++) {
            script_argument argument = e->arguments[i];
            if (!argument.member && argument.parameter >= 0 &&
                argument.parameter < cap->parameter_count) {
                argument.member = cap->parameters[argument.parameter].member;
            }
            int field = -1;
            for (int j = 0; j < type->field_count; j++) {
                if (type->fields[j].id == argument.member) {
                    field = j;
                    break;
                }
            }
            if (field < 0) {
                error(l, argument.range, "Structural input has no declared field identity");
                afree(arguments);
                return -1;
            }
            argument.parameter = field;
            apush(arguments, argument);
        }
        script_expression constructor = *e;
        constructor.kind = SCRIPT_EXPR_STRUCT;
        constructor.arguments = arguments;
        constructor.type = type->type;
        constructor.type.nominal = type->id;
        int value = constructor_from(l, &constructor, -1);
        afree(arguments);
        return value;
    }
    if (e->argument_count != 1) {
        error(l, e->range, "Structural projection requires one aggregate input");
        return -1;
    }
    int source = expression(l, e->arguments[0].value);
    pin_value *defaults = NULL;
    for (int i = 0; i < cap->result_count; i++) {
        apush(
            defaults,
            initial(l, i < e->result_count ? e->results[i] : cap->results[i].type)
        );
    }
    int first = behavior_function_builder_values(l->builder, defaults, acount(defaults));
    afree(defaults);
    for (int i = 0; i < cap->result_count; i++) {
        const script_catalog_field
            *field = component_field(l, type, cap->results[i].member, e->range);
        if (!field) {
            return -1;
        }
        int value;
        if (field->type.storage == PIN_STRUCT &&
            field->type.container == PIN_CONTAINER_SINGLE) {
            int offset = constant(
                l,
                (pin_value){ .type = BF_POINTER_TYPE, .u64 = field->offset },
                e->range
            );
            value = value_op(
                l,
                (behavior_function_instruction){ .op = BF_ADD,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = source,
                                                 .right = offset },
                e->range
            );
        } else {
            value = typed_op(
                l,
                (behavior_function_instruction){ .op = BF_LOAD,
                                                 .type = slot_type(field->type),
                                                 .left = source,
                                                 .offset = field->offset },
                e->range,
                field->type
            );
        }
        script_type result = i < e->result_count ? e->results[i]
                                                 : cap->results[i].type;
        value = convert(l, value, field->type, result, e->range);
        value = retain_value(l, value, result, e->range);
        host_argument(l, first + i, value, result, e->range);
    }
    if (e->type.flags & SCRIPT_TYPE_RESULTS) {
        return first;
    }
    return cap->result_count ? host_result(l, first, e->type, e->range) : -1;
}
static void flow_outcome(
    lower *l,
    const script_expression *e,
    const script_catalog_capability *cap,
    int outcome,
    int first
)
{
    if (!e->instance) {
        return;
    }
    if (outcome < 0 || outcome >= cap->outcome_count) {
        error(l, e->range, "Control operation has no declared outcome ordinal");
        return;
    }
    int context = value_op(
        l,
        (behavior_function_instruction){ .op = BF_CONTEXT,
                                         .type = BF_POINTER_TYPE,
                                         .target = 1 },
        e->range
    );
    int value = constant(
        l,
        (pin_value){ .type = PIN_U64,
                     .u64 = script_catalog_id("outcome", cap->outcomes[outcome]) },
        e->range
    );
    emit(
        l,
        (behavior_function_instruction){
            .op = BF_WRITE,
            .type = PIN_U64,
            .left = value,
            .right = context,
            .offset = offsetof(behavior_function_context, returned_outcome) },
        e->range
    );
    complete_results(l, e, cap, first, false);
}
static int flow_argument(lower *l, const script_expression *e, int parameter)
{
    for (int i = 0; i < e->argument_count; i++) {
        if (e->arguments[i].parameter == parameter) {
            return e->arguments[i].value;
        }
    }
    error(l, e->range, "Control operation has no resolved input ordinal");
    return -1;
}
static int flow_value(lower *l, const script_expression *e, int index)
{
    int value = expression(l, index);
    for (int i = 0; i < e->argument_count; i++) {
        if (e->arguments[i].value == index) {
            debug_input(
                l,
                e,
                e->arguments[i].parameter,
                value,
                l->a->expressions[index].type
            );
            break;
        }
    }
    return value;
}
static int array_count(lower *, int, script_range);
static int array_item(lower *, int, int, script_type, script_range);
static int flow_control(lower *l, const script_expression *e, bool request, uint64_t action)
{
    if (!action && !request) {
        return constant(l, (pin_value){ .type = PIN_BOOL }, e->range);
    }
    pin_value headers[] = { { .type = BF_POINTER_TYPE },
                            { .type = PIN_U64, .u64 = action } };
    int slots[] = { value_op(
                        l,
                        (behavior_function_instruction){ .op = BF_CONTEXT,
                                                         .type = BF_POINTER_TYPE,
                                                         .target = 1 },
                        e->range
                    ),
                    -1 };
    return primitive(
        l,
        request ? &EXECUTION_break_request_call : &EXECUTION_break_take_call,
        &EXECUTION_assignment_access,
        headers,
        slots,
        2,
        (script_type){ .storage = PIN_BOOL },
        e->range
    );
}
static int save_slot(lower *l, int value, pin_type type, script_range range)
{
    int local = behavior_function_builder_local(l->builder, (pin_value){ .type = type });
    emit(l, (behavior_function_instruction){ .op = BF_SAVE, .type = type, .left = value, .target = local }, range);
    return local;
}
static int local_slot(lower *l, int local, pin_type type, script_range range)
{
    return value_op(l, (behavior_function_instruction){ .op = BF_LOCAL, .type = type, .left = local }, range);
}
static void flow_result(
    lower *l,
    const script_expression *e,
    const script_catalog_capability *cap,
    int first,
    int ordinal,
    int value
)
{
    if (ordinal < 0 || ordinal >= cap->result_count) {
        error(l, e->range, "Control result has no declared output ordinal");
        return;
    }
    script_type type = ordinal < e->result_count ? e->results[ordinal]
                                                 : cap->results[ordinal].type;
    host_argument(l, first + ordinal, retain_value(l, value, type, e->range), type, e->range);
}
static int flow_loop(lower *l, const script_expression *e, const script_catalog_capability *cap, int first)
{
    if (cap->flow.break_entry >= 0 && e->entry == cap->flow.break_entry) {
        flow_control(l, e, true, e->instance);
        return -1;
    }
    int input = flow_argument(l, e, cap->flow.inputs[0]);
    if (input < 0) {
        return -1;
    }
    bool range = cap->flow.kind == SCRIPT_FLOW_ACCESS_RANGE;
    bool each = cap->flow.kind == SCRIPT_FLOW_ACCESS_EACH;
    int current_local = -1, end_local = -1, step_local = -1, array_local = -1;
    pin_type integer = PIN_I32;
    if (range) {
        int last = flow_argument(l, e, cap->flow.inputs[1]);
        if (last < 0) {
            return -1;
        }
        integer = l->a->expressions[input].type.storage;
        int begin = flow_value(l, e, input), end = flow_value(l, e, last);
        current_local = save_slot(l, begin, integer, e->range);
        end_local = save_slot(l, end, integer, e->range);
        int ascending = value_op(
            l,
            (
                behavior_function_instruction
            ){ .op = BF_LE, .type = integer, .left = begin, .right = end },
            e->range
        );
        int one = constant(l, (pin_value){ .type = integer, .u64 = 1 }, e->range);
        int negative = constant(l, (pin_value){ .type = integer, .u64 = UINT64_MAX }, e->range);
        int step = value_op(
            l,
            (behavior_function_instruction){ .op = BF_SELECT,
                                             .type = integer,
                                             .left = ascending,
                                             .right = one,
                                             .target = negative },
            e->range
        );
        step_local = save_slot(l, step, integer, e->range);
    } else if (each) {
        script_type collection = l->a->expressions[input].type;
        if (collection.container != PIN_CONTAINER_ARRAY) {
            error(l, e->range, "Array control requires a typed array");
            return -1;
        }
        int array = retain_value(l, flow_value(l, e, input), collection, e->range);
        array_local = save_slot(l, array, BF_POINTER_TYPE, e->range);
        end_local = save_slot(l, array_count(l, array, e->range), PIN_I32, e->range);
        current_local = save_slot(
            l,
            constant(l, (pin_value){ .type = PIN_I32 }, e->range),
            PIN_I32,
            e->range
        );
    }
    flow_control(l, e, false, e->instance);
    int again = l->pc;
    emit(l, (behavior_function_instruction){ .op = BF_CHECK }, e->range);
    int exit = -1;
    int current = current_local >= 0 ? local_slot(l, current_local, integer, e->range)
                                     : -1;
    if (!range) {
        int condition = each
                          ? value_op(
                                l,
                                (behavior_function_instruction){
                                    .op = BF_LT,
                                    .type = PIN_I32,
                                    .left = current,
                                    .right = local_slot(l, end_local, PIN_I32, e->range) },
                                e->range
                            )
                          : flow_value(l, e, input);
        int body = emit(
            l,
            (behavior_function_instruction){ .op = BF_BRANCH,
                                             .left = condition,
                                             .target = -1 },
            e->range
        );
        exit = emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = -1 }, e->range);
        behavior_function_builder_patch_target(l->builder, body, l->pc);
    }
    if (range || each) {
        flow_result(l, e, cap, first, cap->flow.results[0], current);
    }
    if (each) {
        int ordinal = cap->flow.results[1];
        script_type element = ordinal < e->result_count ? e->results[ordinal]
                                                        : cap->results[ordinal].type;
        int array = local_slot(l, array_local, BF_POINTER_TYPE, e->range);
        flow_result(l, e, cap, first, ordinal, array_item(l, array, current, element, e->range));
    }
    flow_outcome(l, e, cap, cap->flow.outcomes[0], first);
    int stopped = -1;
    if (e->instance) {
        stopped = emit(
            l,
            (behavior_function_instruction){ .op = BF_BRANCH,
                                             .left = flow_control(l, e, false, e->instance),
                                             .target = -1 },
            e->range
        );
    }
    int at_end = -1;
    if (range || each) {
        current = local_slot(l, current_local, integer, e->range);
        if (range) {
            int done = value_op(
                l,
                (behavior_function_instruction){
                    .op = BF_EQ,
                    .type = integer,
                    .left = current,
                    .right = local_slot(l, end_local, integer, e->range) },
                e->range
            );
            at_end = emit(
                l,
                (behavior_function_instruction){ .op = BF_BRANCH,
                                                 .left = done,
                                                 .target = -1 },
                e->range
            );
        }
        int step = range
                     ? local_slot(l, step_local, integer, e->range)
                     : constant(l, (pin_value){ .type = PIN_I32, .i = 1 }, e->range);
        int next = value_op(
            l,
            (
                behavior_function_instruction
            ){ .op = BF_ADD, .type = integer, .left = current, .right = step },
            e->range
        );
        emit(
            l,
            (
                behavior_function_instruction
            ){ .op = BF_SAVE, .type = integer, .left = next, .target = current_local },
            e->range
        );
    }
    emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = again }, e->range);
    if (exit >= 0) {
        behavior_function_builder_patch_target(l->builder, exit, l->pc);
    }
    if (stopped >= 0) {
        behavior_function_builder_patch_target(l->builder, stopped, l->pc);
    }
    if (at_end >= 0) {
        behavior_function_builder_patch_target(l->builder, at_end, l->pc);
    }
    flow_outcome(l, e, cap, cap->flow.outcomes[1], first);
    return e->type.flags & SCRIPT_TYPE_RESULTS ? first
         : cap->result_count ? host_result(l, first, e->type, e->range)
                             : -1;
}
static int flow_call(lower *l, const script_expression *e, const script_catalog_capability *cap)
{
    pin_value *defaults = NULL;
    for (int i = 0; i < cap->result_count; i++) {
        apush(
            defaults,
            initial(l, i < e->result_count ? e->results[i] : cap->results[i].type)
        );
    }
    int first = behavior_function_builder_values(l->builder, defaults, acount(defaults));
    afree(defaults);
    if (cap->flow.kind == SCRIPT_FLOW_ACCESS_BRANCH) {
        int input = flow_argument(l, e, cap->flow.inputs[0]);
        if (input < 0) {
            return -1;
        }
        int condition = flow_value(l, e, input);
        int yes = emit(
            l,
            (behavior_function_instruction){ .op = BF_BRANCH,
                                             .left = condition,
                                             .target = -1 },
            e->range
        );
        flow_outcome(l, e, cap, cap->flow.outcomes[1], first);
        int end = emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = -1 }, e->range);
        behavior_function_builder_patch_target(l->builder, yes, l->pc);
        flow_outcome(l, e, cap, cap->flow.outcomes[0], first);
        behavior_function_builder_patch_target(l->builder, end, l->pc);
        return -1;
    }
    if (cap->flow.kind == SCRIPT_FLOW_ACCESS_SEQUENCE) {
        for (int i = 0; i < cap->outcome_count; i++) {
            flow_outcome(l, e, cap, i, first);
        }
        return -1;
    }
    if (cap->flow.kind == SCRIPT_FLOW_ACCESS_RANGE ||
        cap->flow.kind == SCRIPT_FLOW_ACCESS_WHILE ||
        cap->flow.kind == SCRIPT_FLOW_ACCESS_EACH) {
        return flow_loop(l, e, cap, first);
    }
    if (cap->flow.kind == SCRIPT_FLOW_ACCESS_BREAK) {
        flow_control(l, e, true, 0);
        return -1;
    }
    error(l, e->range, "Control operation has no execution contract");
    return -1;
}
static uint64_t state_action_id(lower *l, const script_expression *e)
{
    if (e->instance) {
        return e->instance;
    }
    size_t size = strlen(l->a->identity) + 64;
    char *name = xmalloc(size);
    snprintf(name, size, "%s:%d:%d", l->a->identity, e->range.start, e->range.end);
    uint64_t id = script_catalog_id("state-action", name);
    free(name);
    return id;
}
static int state_call(lower *l, const script_expression *source, const script_catalog_capability *cap)
{
    if (l->initializing) {
        error(l, source->range, "Stateful action requires an execution instance");
        return -1;
    }
    script_expression copy = *source;
    copy.instance = state_action_id(l, source);
    const script_expression *e = &copy;
    int entry = e->entry < 0 ? 0 : e->entry;
    int context = value_op(
        l,
        (behavior_function_instruction){ .op = BF_CONTEXT,
                                         .type = BF_POINTER_TYPE,
                                         .target = 1 },
        e->range
    );
    pin_value prefix[] = { { .type = BF_POINTER_TYPE },
                           { .type = PIN_U64, .u64 = e->instance },
                           { .type = PIN_I32, .i = entry } };
    int prefix_slots[] = { context, -1, -1 };
    if (cap->state_kind == NATIVE_STATE_RETRIGGER) {
        int input = flow_argument(l, e, 0);
        if (input < 0) {
            return -1;
        }
        int raw_duration = expression(l, input);
        int duration = raw_duration;
        duration = convert(
            l,
            duration,
            l->a->expressions[input].type,
            cap->parameters[0].type,
            e->range
        );
        debug_input(l, source, 0, duration, cap->parameters[0].type);
        prefix[2] = (pin_value){ .type = PIN_F64 };
        prefix_slots[2] = duration;
        int found = primitive(
            l,
            &EXECUTION_retrigger_action_call,
            &EXECUTION_assignment_access,
            prefix,
            prefix_slots,
            3,
            (script_type){ .storage = PIN_BOOL },
            e->range
        );
        int skip = emit(
            l,
            (behavior_function_instruction){ .op = BF_BRANCH, .left = found, .target = -1 },
            e->range
        );
        bool cached = l->argument_cached;
        int cached_argument = l->cached_argument, cached_value = l->cached_value;
        l->argument_cached = true;
        l->cached_argument = input;
        l->cached_value = raw_duration;
        command(l, e, cap, (script_lower_host){ .command = &EXECUTION_Delay });
        l->argument_cached = cached;
        l->cached_argument = cached_argument;
        l->cached_value = cached_value;
        behavior_function_builder_patch_target(l->builder, skip, l->pc);
        return -1;
    }
    int demand = primitive(
        l,
        &EXECUTION_state_demand_call,
        &EXECUTION_assignment_access,
        prefix,
        prefix_slots,
        3,
        (script_type){ .storage = PIN_I32 },
        e->range
    );
    pin_value *args = NULL;
    for (int i = 0; i < 3; i++) {
        apush(args, prefix[i]);
    }
    for (int i = 0; i < cap->parameter_count; i++) {
        pin_value value = cap->parameters[i].has_default
                            ? cap->parameters[i].default_value
                            : initial(l, cap->parameters[i].type);
        apush(args, value);
    }
    pin_value result = { .type = PIN_I32 };
    int host = linked_host(
        l->builder,
        &EXECUTION_state_transition_call,
        &EXECUTION_assignment_access,
        args,
        acount(args),
        &result,
        1
    );
    afree(args);
    int first = behavior_function_builder_host_first(l->builder, host);
    host_argument(l, first, context, (script_type){ .storage = BF_POINTER_TYPE }, e->range);
    for (int order = 0; order < cap->parameter_count; order++) {
        int parameter = native_state_input_order(cap->state_kind, order);
        int bit = constant(l, (pin_value){ .type = PIN_I32, .i = 1 << parameter }, e->range);
        int masked = value_op(
            l,
            (
                behavior_function_instruction
            ){ .op = BF_BIT_AND, .type = PIN_I32, .left = demand, .right = bit },
            e->range
        );
        int zero = constant(l, (pin_value){ .type = PIN_I32 }, e->range);
        int absent = value_op(
            l,
            (
                behavior_function_instruction
            ){ .op = BF_EQ, .type = PIN_I32, .left = masked, .right = zero },
            e->range
        );
        int skip = emit(
            l,
            (behavior_function_instruction){ .op = BF_BRANCH, .left = absent, .target = -1 },
            e->range
        );
        int input = flow_argument(l, e, parameter);
        if (input < 0) {
            return -1;
        }
        int value = expression(l, input);
        value = convert(
            l,
            value,
            l->a->expressions[input].type,
            cap->parameters[parameter].type,
            e->range
        );
        debug_input(l, source, parameter, value, cap->parameters[parameter].type);
        host_argument(
            l,
            first + 3 + parameter,
            value,
            cap->parameters[parameter].type,
            e->range
        );
        behavior_function_builder_patch_target(l->builder, skip, l->pc);
    }
    emit(
        l,
        (behavior_function_instruction){ .op = BF_HOST_CALL,
                                         .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                         .target = host,
                                         .node_id = e->instance },
        e->range
    );
    int selected = host_result(
        l,
        behavior_function_builder_host_results(l->builder, host),
        (script_type){ .storage = PIN_I32 },
        e->range
    );
    pin_value *defaults = NULL;
    for (int i = 0; i < cap->result_count; i++) {
        apush(defaults, initial(l, e->results[i]));
    }
    int outputs = behavior_function_builder_values(l->builder, defaults, acount(defaults));
    afree(defaults);
    for (int i = 0; i < cap->result_count; i++) {
        host_argument(
            l,
            outputs + i,
            action_value(l, e->instance, i, e->results[i], e->range),
            e->results[i],
            e->range
        );
    }
    for (int i = 0; i < cap->outcome_count; i++) {
        int ordinal = constant(l, (pin_value){ .type = PIN_I32, .i = i }, e->range);
        int differs = value_op(
            l,
            (
                behavior_function_instruction
            ){ .op = BF_NE, .type = PIN_I32, .left = selected, .right = ordinal },
            e->range
        );
        int skip = emit(
            l,
            (behavior_function_instruction){ .op = BF_BRANCH, .left = differs, .target = -1 },
            e->range
        );
        flow_outcome(l, e, cap, i, outputs);
        behavior_function_builder_patch_target(l->builder, skip, l->pc);
    }
    return e->type.flags & SCRIPT_TYPE_RESULTS ? outputs
         : cap->result_count ? host_result(l, outputs, e->type, e->range)
                             : -1;
}
static int call(lower *l, const script_expression *e)
{
    const script_symbol *action = action_symbol(l, e);
    if (action && action->disabled) {
        return e->result_count == 1 && !(e->type.flags & SCRIPT_TYPE_RESULTS)
                 ? action_value(l, action->id, 0, e->type, e->range)
                 : -1;
    }
    if (e->right >= 0) {
        return authored_call(l, e);
    }
    const script_catalog_capability *cap = script_catalog_capability_by_id(l->catalog, e->capability);
    if (cap && cap->state_kind != NATIVE_STATE_NONE) {
        return state_call(l, e, cap);
    }
    if (cap && cap->component_access != SCRIPT_COMPONENT_ACCESS_NONE) {
        return component_call(l, e, cap);
    }
    if (cap && cap->struct_access != SCRIPT_STRUCT_ACCESS_NONE) {
        return struct_call(l, e, cap);
    }
    if (cap && cap->flow.kind != SCRIPT_FLOW_ACCESS_NONE) {
        return flow_call(l, e, cap);
    }
    script_lower_host host = { 0 };
    bool linked = false;
    if (cap && l->bindings) {
        if (cap->kind == SCRIPT_CAPABILITY_ACTION && l->bindings->action) {
            uint64_t action = e->instance    ? e->instance
                            : e->symbol >= 0 ? l->a->symbols[e->symbol].id
                                             : 0;
            linked = l->bindings->action(l->bindings->user, e->capability, action, e->entry, &host);
        } else if (l->bindings->host) {
            linked = l->bindings->host(l->bindings->user, e->capability, &host);
        }
    }
    if (!linked || (!host.call && !host.function && !host.command &&
                    !host.query && host.intrinsic != BF_DELAY &&
                    host.intrinsic != BF_OWNER && host.intrinsic != BF_DELTA)) {
        error(l, e->range, "Callable capability has no linked execution contract");
        return -1;
    }
    if (host.function) {
        return external_call(l, e, cap, host);
    }
    if (host.intrinsic == BF_OWNER || host.intrinsic == BF_DELTA) {
        if (l->initializing || e->argument_count || cap->parameter_count ||
            cap->result_count != 1) {
            error(l, e->range, "Context intrinsic requires one result and no arguments");
            return -1;
        }
        pin_type storage = host.intrinsic == BF_OWNER ? PIN_HANDLE : PIN_F32;
        int slot = value_op(
            l,
            (behavior_function_instruction){ .op = host.intrinsic, .type = storage },
            e->range
        );
        script_type actual = e->type;
        actual.storage = storage;
        return convert(l, slot, actual, e->type, e->range);
    }
    if (host.command) {
        if (cap->event_sender) {
            const script_catalog_capability
                *event = script_catalog_capability_by_id(l->catalog, e->member);
            if (!event || event->kind != SCRIPT_CAPABILITY_EVENT) {
                error(l, e->range, "Event send has no selected typed declaration");
                return -1;
            }
            script_catalog_parameter *parameters = NULL;
            for (int i = 0; i < cap->parameter_count; i++) {
                apush(parameters, cap->parameters[i]);
            }
            for (int i = 0; i < event->parameter_count; i++) {
                apush(parameters, event->parameters[i]);
            }
            script_catalog_capability selected = *cap;
            selected.parameters = parameters;
            selected.parameter_count = acount(parameters);
            host.reference = event->name;
            int result = command(l, e, &selected, host);
            afree(parameters);
            return result;
        }
        return command(l, e, cap, host);
    }
    if (host.query) {
        const char *key = host.reference;
        if (l->initializing) {
            error(l, e->range, "Query snapshots cannot initialize state defaults");
            return -1;
        }
        for (int i = 0; i < e->argument_count; i++) {
            const script_expression *arg = &l->a->expressions[e->arguments[i].value];
            if (arg->kind != SCRIPT_EXPR_CONSTANT ||
                !behavior_scalar_string(arg->constant.type)) {
                error(l, arg->range, "Query capability requires a static declared key");
                return -1;
            }
            key = arg->constant.str;
        }
        if (!key || e->type.container != PIN_CONTAINER_ARRAY ||
            e->type.storage != PIN_HANDLE) {
            error(l, e->range, "Query contract must produce an entity snapshot with a declared key");
            return -1;
        }
        int query = behavior_function_builder_query(l->builder, host.query_service, key, host.query);
        if (!behavior_function_builder_query_identity(
                l->builder,
                query,
                cap->id,
                script_catalog_capability_abi(l->catalog, cap)
            )) {
            error(l, e->range, "Cannot link query declaration identity");
        }
        return value_op(
            l,
            (behavior_function_instruction){ .op = BF_QUERY,
                                             .type = BF_POINTER_TYPE,
                                             .target = query },
            e->range
        );
    }
    if (host.intrinsic == BF_DELAY) {
        if (e->instance || !e->awaited) {
            host.command = &EXECUTION_Delay;
            return command(l, e, cap, host);
        }
        if (!e->awaited || l->initializing || cap->parameter_count != 1) {
            error(l, e->range, "Delay requires an awaited duration contract");
            return -1;
        }
        int duration = -1;
        for (int i = 0; i < e->argument_count; i++) {
            if (e->arguments[i].parameter == 0) {
                duration = e->arguments[i].value;
            }
        }
        if (duration < 0) {
            error(l, e->range, "Delay requires an explicit duration");
            return -1;
        }
        int value = expression(l, duration);
        value = convert(
            l,
            value,
            l->a->expressions[duration].type,
            (script_type){ .storage = PIN_F64 },
            e->range
        );
        debug_input(l, e, 0, value, (script_type){ .storage = PIN_F64 });
        emit(
            l,
            (behavior_function_instruction){
                .op = BF_DELAY,
                .left = value,
                .node_id = e->symbol >= 0 ? l->a->symbols[e->symbol].id
                                          : e->capability ^ (uint64_t)e->range.start },
            e->range
        );
        return -1;
    }
    if (l->initializing && (!cap->pure || host.call->service)) {
        error(l, e->range, "State initializer requires a service-free pure capability");
        return -1;
    }
    if (cap->effects & SCRIPT_EFFECT_SUSPEND) {
        error(l, e->range, "Suspending capability requires command and continuation linking");
        return -1;
    }
    pin_value *args = argument_defaults(l, e, cap), *results = NULL;
    for (int i = 0; i < cap->result_count; i++) {
        script_type result = i < e->result_count ? e->results[i]
                                                 : cap->results[i].type;
        if (!e->result_count && cap->result_count == 1 &&
            !(e->type.flags & SCRIPT_TYPE_RESULTS)) {
            result = e->type;
        }
        apush(results, initial(l, result));
    }
    int host_index = linked_host(l->builder, host.call, host.access, args, acount(args), results, acount(results));
    if (!behavior_function_builder_host_identity(
            l->builder,
            host_index,
            cap->id,
            script_catalog_capability_abi(l->catalog, cap)
        )) {
        error(l, e->range, "Cannot link callable declaration identity");
    }
    if (host.call->action) {
        uint64_t *outcomes = arena_alloc(
            &l->module->storage,
            (size_t)(cap->outcome_count + 1) * sizeof *outcomes
        );
        for (int i = 0; i < cap->outcome_count; i++) {
            outcomes[i] = script_catalog_id("outcome", cap->outcomes[i]);
        }
        if (!behavior_function_builder_host_outcomes(l->builder, host_index, outcomes, cap->outcome_count)) {
            error(l, e->range, "Cannot link action outcome schema");
        }
    }
    afree(args);
    afree(results);
    int first = behavior_function_builder_host_first(l->builder, host_index);
    for (int i = 0; i < e->argument_count; i++) {
        const script_argument *arg = &e->arguments[i];
        if (cap->variadic_optional && arg->parameter >= argument_count(l, e, cap)) {
            continue;
        }
        if (arg->parameter < 0 || arg->parameter >= argument_count(l, e, cap)) {
            error(l, arg->range, "Resolved argument does not match the linked signature");
            continue;
        }
        script_type expected = call_parameter(cap, arg->parameter)->type;
        if (expected.storage == PIN_FLOW || (expected.flags & SCRIPT_TYPE_UNTYPED)) {
            expected = l->a->expressions[arg->value].type;
        }
        int value = expression(l, arg->value);
        value = convert(l, value, l->a->expressions[arg->value].type, expected, arg->range);
        debug_input(l, e, arg->parameter, value, expected);
        host_argument(l, first + arg->parameter, value, expected, arg->range);
    }
    emit(
        l,
        (behavior_function_instruction){ .op = BF_HOST_CALL,
                                         .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                         .target = host_index,
                                         .node_id = e->instance ? e->instance
                                                                : e->capability },
        e->range
    );
    int result_first = behavior_function_builder_host_results(l->builder, host_index);
    complete_results(l, e, cap, result_first, false);
    if (e->type.flags & SCRIPT_TYPE_RESULTS) {
        return result_first; /* A result tuple indexes the host bank, never a VM slot. */
    }
    if (!cap->result_count) {
        return -1;
    }
    return host_result(l, result_first, e->type, e->range);
}
static int component_value(lower *l, const script_expression *e, bool presence)
{
    const script_catalog_type *type = script_catalog_type_by_id(l->catalog, e->capability);
    if (!type || !type->component || !type->component_id) {
        error(l, e->range, "Component capability has no reflected execution layout");
        return -1;
    }
    int entity = e->left >= 0
                   ? expression(l, e->left)
                   : value_op(
                         l,
                         (behavior_function_instruction){ .op = BF_OWNER,
                                                          .type = PIN_HANDLE },
                         e->range
                     );
    int world = value_op(
        l,
        (behavior_function_instruction){ .op = BF_WORLD, .type = BF_POINTER_TYPE },
        e->range
    );
    int row = value_op(
        l,
        (
            behavior_function_instruction
        ){ .op = BF_ENTITY, .type = PIN_U32, .left = world, .right = entity },
        e->range
    );
    int pointer = value_op(
        l,
        (behavior_function_instruction){
            .op = BF_COMPONENT,
            .type = presence ? PIN_BOOL : BF_POINTER_TYPE,
            .left = world,
            .right = row,
            .component = behavior_function_builder_component(l->builder, type->component_id, false, false),
            .value = { .type = PIN_U64, .u64 = type->component_id },
            .offset = type->size,
            .layout = type->layout,
            .target = BEHAVIOR_FUNCTION_HOST_DATA },
        e->range
    );
    if (presence) {
        return pointer;
    }
    emit(
        l,
        (behavior_function_instruction){ .op = BF_ASSERT,
                                         .left = pointer,
                                         .target = BEHAVIOR_FUNCTION_HOST_DATA },
        e->range
    );
    return pointer;
}
static int component(lower *l, const script_expression *e)
{
    return component_value(l, e, false);
}
static int condition_value(lower *l, int index)
{
    const script_expression *e = &l->a->expressions[index];
    if (e->type.flags & SCRIPT_TYPE_TASK) {
        const script_expression running = { .type = { .storage = PIN_BOOL },
                                            .range = e->range };
        return task_command(l, &EXECUTION_TaskRunning, expression(l, index), &running);
    }
    if (!(e->type.flags & SCRIPT_TYPE_OPTIONAL)) {
        return expression(l, index);
    }
    if (e->kind == SCRIPT_EXPR_COMPONENT) {
        return component_value(l, e, true);
    }
    if (e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0 &&
        l->a->symbols[e->symbol].kind == SCRIPT_COMPONENT) {
        return component_value(
            l,
            &(script_expression){ .capability = e->type.nominal,
                                  .left = -1,
                                  .range = e->range },
            true
        );
    }
    int value = expression(l, index);
    pin_type type = slot_type(e->type);
    int zero = kind_op(
        l,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = type,
                                         .value = { .type = type } },
        e->range,
        bank_kind(l, BEHAVIOR_BANK_SLOT, value)
    );
    return value_op(
        l,
        (
            behavior_function_instruction
        ){ .op = BF_NE, .type = type, .left = value, .right = zero },
        e->range
    );
}
static const script_expression *tuple_signature(lower *l, int source)
{
    int depth = 0;
    while (source >= 0 && source < l->a->expression_count) {
        const script_expression *expression = &l->a->expressions[source];
        if (expression->result_count) {
            return expression;
        }
        if (expression->kind != SCRIPT_EXPR_SYMBOL || expression->symbol < 0 ||
            depth++ >= l->a->expression_count) {
            return NULL;
        }
        source = l->a->symbols[expression->symbol].initializer;
    }
    return NULL;
}
/* A call with one result produces that value directly, so naming its only
 * output reads the call's value rather than a tuple field. */
static bool sole_output(lower *l, const script_expression *e)
{
    const script_expression *call = tuple_signature(l, e->left);
    if ((l->a->expressions[e->left].type.flags & SCRIPT_TYPE_RESULTS) ||
        !call || call->kind != SCRIPT_EXPR_CALL || call->result_count != 1) {
        return false;
    }
    const script_catalog_capability
        *capability = script_catalog_capability_by_id(l->catalog, call->capability);
    if (capability) {
        return capability->result_count == 1 &&
               e->member == script_catalog_id("port", capability->results[0].name);
    }
    int function = call->entry >= 0 ? call->entry : call->right;
    return function >= 0 && acount(l->a->functions[function].results) == 1 &&
           e->member == l->a->symbols[l->a->functions[function].results[0]].id;
}
static int symbol_value(lower *l, const script_expression *e)
{
    const script_symbol *s = &l->a->symbols[e->symbol];
    if ((s->type.flags & SCRIPT_TYPE_RESULTS) && l->locals[e->symbol] >= 0) {
        return l->locals[e->symbol];
    }
    if ((s->type.flags & SCRIPT_TYPE_RESULTS) && l->captured &&
        l->captured[e->symbol] >= 0) {
        const script_expression *signature = tuple_signature(l, s->initializer);
        if (!signature) {
            error(l, e->range, "Captured tuple has no resolved field signature");
            return -1;
        }
        pin_value *defaults = NULL;
        for (int i = 0; i < signature->result_count; i++) {
            apush(defaults, initial(l, signature->results[i]));
        }
        int first = behavior_function_builder_values(l->builder, defaults, acount(defaults));
        afree(defaults);
        for (int i = 0; i < signature->result_count; i++) {
            int descriptor = value_op(
                l,
                (behavior_function_instruction){ .op = BF_ARGUMENT,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = l->parameters[e->symbol] + i,
                                                 .value = { .type = BF_POINTER_TYPE } },
                e->range
            );
            int field = reference_read(l, descriptor, signature->results[i], e->range);
            host_argument(l, first + i, field, signature->results[i], e->range);
        }
        return first;
    }
    if (e->symbol == l->a->owner_symbol) {
        if (l->initializing) {
            error(l, e->range, "State initializer cannot depend on an entity owner");
            return -1;
        }
        return value_op(l, (behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE }, e->range);
    }
    if (l->initializing && (s->kind == SCRIPT_STATE || s->kind == SCRIPT_SHARED_STATE)) {
        if (s->initializer < 0 || l->initializer_depth++ >= l->a->expression_count) {
            error(l, e->range, "State initializer dependency has no finite default");
            return -1;
        }
        int value = expression(l, s->initializer);
        l->initializer_depth--;
        return convert(l, value, l->a->expressions[s->initializer].type, s->type, e->range);
    }
    if (s->kind == SCRIPT_COMPONENT) {
        int value = component(
            l,
            &(script_expression){ .capability = s->type.nominal, .left = -1, .range = e->range }
        );
        return retain_value(l, value, s->type, e->range);
    }
    if (l->variables[e->symbol] >= 0) {
        if (s->type.container != PIN_CONTAINER_SINGLE || s->type.storage == PIN_STRUCT) {
            int value = value_op(
                l,
                (behavior_function_instruction){ .op = BF_ARRAY,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = l->variables[e->symbol],
                                                 .target = BF_VARIABLE,
                                                 .value = initial(l, s->type) },
                e->range
            );
            return retain_value(l, value, s->type, e->range);
        }
        return value_op(
            l,
            (behavior_function_instruction){ .op = BF_VARIABLE,
                                             .type = slot_type(s->type),
                                             .left = l->variables[e->symbol] },
            e->range
        );
    }
    if (l->parameters[e->symbol] >= 0) {
        if ((s->type.flags & SCRIPT_TYPE_REFERENCE) ||
            (l->captured && l->captured[e->symbol] >= 0)) {
            int address = value_op(
                l,
                (behavior_function_instruction){
                    .op = BF_ARGUMENT,
                    .type = BF_POINTER_TYPE,
                    .left = l->parameters[e->symbol],
                    .value = (pin_value){ .type = BF_POINTER_TYPE } },
                e->range
            );
            return reference_read(l, address, s->type, e->range);
        }
        if (s->type.container != PIN_CONTAINER_SINGLE || s->type.storage == PIN_STRUCT) {
            return value_op(
                l,
                (behavior_function_instruction){ .op = BF_ARRAY,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = l->parameters[e->symbol],
                                                 .target = BF_ARGUMENT,
                                                 .value = initial(l, s->type) },
                e->range
            );
        }
        return value_op(
            l,
            (behavior_function_instruction){ .op = BF_ARGUMENT,
                                             .type = slot_type(s->type),
                                             .left = l->parameters[e->symbol],
                                             .value = initial(l, s->type) },
            e->range
        );
    }
    if (l->addresses[e->symbol] >= 0) {
        return host_result(l, l->addresses[e->symbol], s->type, e->range);
    }
    if (l->locals[e->symbol] >= 0) {
        return value_op(
            l,
            (behavior_function_instruction){ .op = BF_LOCAL,
                                             .type = slot_type(s->type),
                                             .left = l->locals[e->symbol] },
            e->range
        );
    }
    if (l->initializing) {
        error(l, e->range, "State initializer depends on invocation storage");
        return -1;
    }
    if (s->kind == SCRIPT_GLOBAL && l->bindings && l->bindings->global) {
        behavior_function_instruction i = { 0 };
        if (l->bindings->global(l->bindings->user, s->capability ? s->capability : s->id, &i)) {
            int value = value_op(l, i, e->range);
            script_type actual = s->type;
            actual.storage = i.type;
            return convert(l, value, actual, s->type, e->range);
        }
    }
    error(l, e->range, "Symbol has no linked execution storage or intrinsic");
    return -1;
}
static int array_count(lower *l, int array, script_range range)
{
    emit(l, (behavior_function_instruction){ .op = BF_ASSERT, .left = array, .target = BEHAVIOR_FUNCTION_ARGUMENT }, range);
    return value_op(
        l,
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = PIN_I32,
                                         .left = array,
                                         .offset = offsetof(pin_array, count) },
        range
    );
}
static int array_item(lower *l, int array, int index, script_type type, script_range range)
{
    int zero = constant(l, (pin_value){ .type = PIN_I32 }, range);
    int lower_bound = value_op(
        l,
        (
            behavior_function_instruction
        ){ .op = BF_GE, .type = PIN_I32, .left = index, .right = zero },
        range
    );
    emit(
        l,
        (behavior_function_instruction){ .op = BF_ASSERT,
                                         .left = lower_bound,
                                         .target = BEHAVIOR_FUNCTION_ARGUMENT },
        range
    );
    int count = array_count(l, array, range);
    int upper_bound = value_op(
        l,
        (
            behavior_function_instruction
        ){ .op = BF_LT, .type = PIN_I32, .left = index, .right = count },
        range
    );
    emit(
        l,
        (behavior_function_instruction){ .op = BF_ASSERT,
                                         .left = upper_bound,
                                         .target = BEHAVIOR_FUNCTION_ARGUMENT },
        range
    );
    int items = kind_op(
        l,
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = BF_POINTER_TYPE,
                                         .left = array,
                                         .offset = offsetof(pin_array, items) },
        range,
        BEHAVIOR_ADDRESS
    );
    int item = value_op(
        l,
        (behavior_function_instruction){ .op = BF_INDEX,
                                         .type = BF_POINTER_TYPE,
                                         .left = items,
                                         .right = index,
                                         .offset = sizeof(pin_value) },
        range
    );
    return typed_op(
        l,
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = slot_type(type),
                                         .left = item,
                                         .offset = offsetof(pin_value, d) },
        range,
        type
    );
}
static int reference_expression(lower *l, const script_expression *e)
{
    if (e->kind == SCRIPT_EXPR_CALL) {
        const script_catalog_capability
            *cap = script_catalog_capability_by_id(l->catalog, e->capability);
        if (cap && cap->component_access == SCRIPT_COMPONENT_ACCESS_READ) {
            int entity, value;
            const script_catalog_type *type = component_target(l, e, cap, &entity, &value);
            if (!type || l->failed) {
                return -1;
            }
            return component_reference(l, e, cap, type, entity, NULL);
        }
    }
    if (e->kind == SCRIPT_EXPR_MEMBER && e->left >= 0 &&
        (l->a->expressions[e->left].type.flags & SCRIPT_TYPE_RESULTS)) {
        const script_expression *receiver = &l->a->expressions[e->left];
        if (receiver->kind == SCRIPT_EXPR_SYMBOL && l->captured &&
            l->captured[receiver->symbol] >= 0) {
            return value_op(
                l,
                (behavior_function_instruction){
                    .op = BF_ARGUMENT,
                    .type = BF_POINTER_TYPE,
                    .left = l->parameters[receiver->symbol] + (int)e->offset,
                    .value = { .type = BF_POINTER_TYPE } },
                e->range
            );
        }
        if (receiver->kind != SCRIPT_EXPR_SYMBOL || l->locals[receiver->symbol] < 0) {
            error(l, e->range, "Writable result field requires an owned local tuple");
            return -1;
        }
        pin_value headers[] = { { .type = BF_POINTER_TYPE },
                                initial(l, e->type),
                                { .type = PIN_I32, .i = LANGUAGE_REFERENCE_VALUE },
                                { .type = PIN_I32, .i = -1 },
                                { .type = BF_POINTER_TYPE },
                                { .type = PIN_HANDLE },
                                { .type = PIN_U64 },
                                { .type = PIN_U64 },
                                { .type = PIN_U64 } };
        int slots[] = {
            value_op(
                l,
                (behavior_function_instruction){ .op = BF_CONTEXT,
                                                 .type = BF_POINTER_TYPE,
                                                 .target = 1 },
                e->range
            ),
            -1,
            -1,
            -1,
            host_address(l, l->locals[receiver->symbol] + (int)e->offset, false, e->range),
            -1,
            -1,
            -1,
            -1
        };
        return primitive(
            l,
            &EXECUTION_reference_make_call,
            &EXECUTION_value_access,
            headers,
            slots,
            9,
            (script_type){ .storage = BF_POINTER_TYPE },
            e->range
        );
    }
    if ((e->kind == SCRIPT_EXPR_MEMBER && !e->read && !e->write) ||
        e->kind == SCRIPT_EXPR_INDEX) {
        pin_value headers[] = { { .type = BF_POINTER_TYPE },
                                initial(l, e->type),
                                { .type = PIN_U64, .u64 = e->offset },
                                { .type = PIN_BOOL, .b = e->kind == SCRIPT_EXPR_INDEX } };
        int slots[] = { reference(l, e->left), -1, -1, -1 };
        if (e->kind == SCRIPT_EXPR_INDEX) {
            headers[2] = (pin_value){ .type = PIN_I32 };
            slots[2] = expression(l, e->right);
        }
        return primitive(
            l,
            &EXECUTION_reference_field_call,
            &EXECUTION_value_access,
            headers,
            slots,
            4,
            (script_type){ .storage = BF_POINTER_TYPE },
            e->range
        );
    }
    pin_value headers[] = {
        { .type = BF_POINTER_TYPE }, initial(l, e->type),
        { .type = PIN_I32 },         { .type = PIN_I32, .i = -1 },
        { .type = BF_POINTER_TYPE }, { .type = PIN_HANDLE },
        { .type = PIN_U64 },         { .type = PIN_U64 },
        { .type = PIN_U64 }
    };
    int slots[] = { value_op(
                        l,
                        (behavior_function_instruction){ .op = BF_CONTEXT,
                                                         .type = BF_POINTER_TYPE,
                                                         .target = 1 },
                        e->range
                    ),
                    -1,
                    -1,
                    -1,
                    -1,
                    -1,
                    -1,
                    -1,
                    -1,
                    -1 };
    const script_symbol *symbol = e->kind == SCRIPT_EXPR_SYMBOL
                                    ? &l->a->symbols[e->symbol]
                                    : NULL;
    if (symbol &&
        ((symbol->type.flags & SCRIPT_TYPE_REFERENCE) ||
         (l->captured && l->captured[e->symbol] >= 0)) &&
        l->parameters[e->symbol] >= 0) {
        return value_op(
            l,
            (behavior_function_instruction){
                .op = BF_ARGUMENT,
                .type = BF_POINTER_TYPE,
                .left = l->parameters[e->symbol],
                .value = (pin_value){ .type = BF_POINTER_TYPE } },
            e->range
        );
    }
    if (e->kind == SCRIPT_EXPR_COMPONENT ||
        (symbol && symbol->kind == SCRIPT_COMPONENT)) {
        uint64_t nominal = symbol ? symbol->type.nominal : e->capability;
        const script_catalog_type *type = script_catalog_type_by_id(l->catalog, nominal);
        if (!type || !type->component_id) {
            error(l, e->range, "Reference has no declared component layout");
            return -1;
        }
        headers[2].i = LANGUAGE_REFERENCE_COMPONENT;
        headers[6].u64 = type->component_id;
        headers[7].u64 = type->layout;
        headers[8].u64 = type->size;
        slots[5] = !symbol && e->left >= 0
                     ? expression(l, e->left)
                     : value_op(
                           l,
                           (behavior_function_instruction){ .op = BF_OWNER,
                                                            .type = PIN_HANDLE },
                           e->range
                       );
        behavior_function_builder_component(l->builder, type->component_id, false, true);
    } else if (symbol && l->variables[e->symbol] >= 0) {
        headers[2].i = LANGUAGE_REFERENCE_STATE;
        headers[3].i = l->variables[e->symbol];
    } else if (symbol && l->parameters[e->symbol] >= 0) {
        headers[2].i = LANGUAGE_REFERENCE_VALUE;
        int arguments = value_op(
            l,
            (behavior_function_instruction){
                .op = BF_CONTEXT,
                .type = BF_POINTER_TYPE,
                .offset = offsetof(behavior_function_context, arguments) },
            e->range
        );
        int ordinal = constant(
            l,
            (pin_value){ .type = PIN_I32, .i = l->parameters[e->symbol] },
            e->range
        );
        slots[4] = value_op(
            l,
            (behavior_function_instruction){ .op = BF_INDEX,
                                             .type = BF_POINTER_TYPE,
                                             .left = arguments,
                                             .right = ordinal,
                                             .offset = sizeof(pin_value) },
            e->range
        );
    } else if (symbol && l->addresses[e->symbol] >= 0) {
        headers[2].i = LANGUAGE_REFERENCE_VALUE;
        slots[4] = host_address(l, l->addresses[e->symbol], false, e->range);
    } else {
        char message[192];
        snprintf(
            message,
            sizeof message,
            "Reference target has no stable writable storage: %s kind=%d "
            "symbol=%d "
            "flags=%u variable=%d argument=%d bank=%d",
            symbol ? symbol->name : "expression",
            (int)e->kind,
            symbol ? (int)symbol->kind : -1,
            e->type.flags,
            symbol ? l->variables[e->symbol] : -1,
            symbol ? l->parameters[e->symbol] : -1,
            symbol ? l->addresses[e->symbol] : -1
        );
        error(l, e->range, message);
        return -1;
    }
    return primitive(
        l,
        &EXECUTION_reference_make_call,
        &EXECUTION_value_access,
        headers,
        slots,
        9,
        (script_type){ .storage = BF_POINTER_TYPE },
        e->range
    );
}
static int reference(lower *l, int index)
{
    const script_expression *e = &l->a->expressions[index];
    debug_point(l, SCRIPT_RECORD_EXPRESSION, index, e->range);
    if (e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0) {
        debug_point(l, SCRIPT_RECORD_SYMBOL, e->symbol, e->range);
    }
    return reference_expression(l, &l->a->expressions[index]);
}
static bool command_tuple(lower *l, const script_expression *receiver)
{
    int depth = 0;
    while (receiver->kind == SCRIPT_EXPR_SYMBOL && receiver->symbol >= 0) {
        if ((receiver->type.flags & SCRIPT_TYPE_RESULTS) &&
            (l->locals[receiver->symbol] >= 0 ||
             (l->captured && l->captured[receiver->symbol] >= 0))) {
            return false;
        }
        int initializer = l->a->symbols[receiver->symbol].initializer;
        if (initializer < 0 || depth++ >= l->a->expression_count) {
            return false;
        }
        receiver = &l->a->expressions[initializer];
    }
    if (receiver->kind == SCRIPT_EXPR_JOIN) {
        return true;
    }
    if (receiver->kind != SCRIPT_EXPR_CALL || receiver->right >= 0) {
        return false;
    }
    const script_catalog_capability
        *cap = script_catalog_capability_by_id(l->catalog, receiver->capability);
    if (!cap || cap->kind != SCRIPT_CAPABILITY_ACTION || !l->bindings ||
        !l->bindings->action) {
        return false;
    }
    script_lower_host host = { 0 };
    return l->bindings->action(
               l->bindings->user,
               receiver->capability,
               receiver->instance,
               receiver->entry,
               &host
           ) &&
           host.command;
}
static behavior_source_point debug_origin(lower *l, script_record_kind kind, int record, script_range range)
{
    return (behavior_source_point){ .occurrence = script_record_id(l->a, kind, record),
                                    .meaning = script_record_meaning(l->a, kind, record),
                                    .start = range.start,
                                    .end = range.end,
                                    .line = range.line,
                                    .column = range.column,
                                    .kind = kind };
}
static void debug_point(lower *l, script_record_kind kind, int record, script_range range)
{
    if (l->initializing || l->function < 0) {
        return;
    }
    int point = behavior_function_builder_source_point(l->builder, debug_origin(l, kind, record, range));
    emit(l, (behavior_function_instruction){ .op = BF_SOURCE_POINT, .left = point }, range);
}
static void debug_watch(lower *l, int record, int value, script_type type, bool output, int ordinal)
{
    if (value < 0 || l->initializing || l->function < 0 ||
        type.storage == PIN_FLOW || (type.flags & SCRIPT_TYPE_RESULTS)) {
        return;
    }
    script_range range = l->a->expressions[record].range;
    bool reference = bank_kind(l, BEHAVIOR_BANK_SLOT, value) == BEHAVIOR_REFERENCE;
    pin_value header = initial(l, type);
    pin_type physical = behavior_function_slot_type(
        behavior_function_builder_definition(l->builder),
        value
    );
    pin_type expected = reference || header.type == PIN_STRUCT ||
                                header.container != PIN_CONTAINER_SINGLE
                          ? BF_POINTER_TYPE
                          : header.type;
    if (physical != expected) {
        char message[192];
        snprintf(
            message,
            sizeof message,
            "Watch expression %d kind %d has physical type %d, declared type "
            "%d container %d reference %d",
            record,
            l->a->expressions[record].kind,
            physical,
            header.type,
            header.container,
            reference
        );
        error(l, range, message);
        return;
    }
    int watch = behavior_function_builder_source_watch(
        l->builder,
        (behavior_source_watch){
            .origin = debug_origin(l, SCRIPT_RECORD_EXPRESSION, record, range),
            .initial = header,
            .output = output,
            .ordinal = ordinal,
            .reference = reference }
    );
    emit(
        l,
        (
            behavior_function_instruction
        ){ .op = BF_SOURCE_WATCH, .type = physical, .left = value, .target = watch },
        range
    );
}
static void debug_input(lower *l, const script_expression *e, int ordinal, int value, script_type type)
{
    uintptr_t address = (uintptr_t)e, start = (uintptr_t)l->a->expressions;
    size_t bytes = (size_t)l->a->expression_count * sizeof *e;
    if (address < start || address - start >= bytes ||
        (address - start) % sizeof *e || ordinal < 0) {
        return;
    }
    debug_watch(l, (int)((address - start) / sizeof *e), value, type, false, ordinal);
}
static int expression_value(lower *l, int index)
{
    if (index < 0 || index >= l->a->expression_count) {
        error(l, (script_range){ 0 }, "Invalid resolved expression index");
        return -1;
    }
    const script_expression *e = &l->a->expressions[index];
    if (l->argument_cached && index == l->cached_argument) {
        return l->cached_value;
    }
    if (l->target_cached && index == l->target_expression) {
        return l->target_value;
    }
    debug_point(l, SCRIPT_RECORD_EXPRESSION, index, e->range);
    if (e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0) {
        debug_point(l, SCRIPT_RECORD_SYMBOL, e->symbol, e->range);
    }
    switch (e->kind) {
        case SCRIPT_EXPR_STORE: {
            const script_symbol *action = action_symbol(l, e);
            if (action && action->disabled) {
                return action_value(l, action->id, 0, e->type, e->range);
            }
            int value = expression(l, e->right);
            value = convert(l, value, l->a->expressions[e->right].type, e->type, e->range);
            store_symbol(l, e->symbol, value, e->range);
            if (e->instance) {
                action_complete(l, e->instance, &e->type, &value, 1, e->range);
            }
            return value;
        }
        case SCRIPT_EXPR_INDEX: {
            int array = expression(l, e->left), index = expression(l, e->right);
            return array_item(l, array, index, e->type, e->range);
        }
        case SCRIPT_EXPR_FALLBACK: return expression(l, e->left);
        case SCRIPT_EXPR_NIL:
            return kind_op(
                l,
                (behavior_function_instruction){ .op = BF_CONSTANT,
                                                 .type = BF_POINTER_TYPE,
                                                 .value = { .type = BF_POINTER_TYPE } },
                e->range,
                BEHAVIOR_ADDRESS
            );
        case SCRIPT_EXPR_CONSTANT:
            if (e->type.storage == PIN_STRUCT ||
                e->type.container != PIN_CONTAINER_SINGLE) {
                return value_op(
                    l,
                    (behavior_function_instruction){ .op = BF_ARRAY,
                                                     .type = BF_POINTER_TYPE,
                                                     .target = BF_CONSTANT,
                                                     .value = e->constant },
                    e->range
                );
            }
            return constant(l, e->constant, e->range);
        case SCRIPT_EXPR_SYMBOL: return symbol_value(l, e);
        case SCRIPT_EXPR_CALL: return call(l, e);
        case SCRIPT_EXPR_YIELD: return task_command(l, &EXECUTION_Yield, -1, e);
        case SCRIPT_EXPR_JOIN:
            return task_command(l, &EXECUTION_TaskJoin, expression(l, e->left), e);
        case SCRIPT_EXPR_CANCEL:
            return task_command(l, &EXECUTION_TaskCancel, expression(l, e->left), e);
        case SCRIPT_EXPR_STRUCT:
        case SCRIPT_EXPR_ARRAY: return constructor(l, e);
        case SCRIPT_EXPR_COMPONENT:
            return retain_value(l, component(l, e), e->type, e->range);
        case SCRIPT_EXPR_CAST: {
            int left = expression(l, e->left);
            return convert(l, left, l->a->expressions[e->left].type, e->type, e->range);
        }
        case SCRIPT_EXPR_UNARY: {
            int left = e->op == SCRIPT_OP_NOT ? condition_value(l, e->left)
                                              : expression(l, e->left);
            if (e->op == SCRIPT_OP_NONE) {
                return convert(l, left, l->a->expressions[e->left].type, e->type, e->range);
            }
            if (e->op == SCRIPT_OP_NEG) {
                return value_op(
                    l,
                    (behavior_function_instruction){ .op = BF_NEG,
                                                     .type = slot_type(e->type),
                                                     .left = left },
                    e->range
                );
            }
            if (e->op == SCRIPT_OP_BIT_NOT) {
                return value_op(
                    l,
                    (behavior_function_instruction){ .op = BF_BIT_NOT,
                                                     .type = slot_type(e->type),
                                                     .left = left },
                    e->range
                );
            }
            if (e->op == SCRIPT_OP_NOT) {
                pin_type operand = l->a->expressions[e->left].type.flags &
                                           (SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_TASK)
                                     ? PIN_BOOL
                                     : slot_type(l->a->expressions[e->left].type);
                int zero = kind_op(
                    l,
                    (behavior_function_instruction){ .op = BF_CONSTANT,
                                                     .type = operand,
                                                     .value = { .type = operand } },
                    e->range,
                    bank_kind(l, BEHAVIOR_BANK_SLOT, left)
                );
                return value_op(
                    l,
                    (
                        behavior_function_instruction
                    ){ .op = BF_EQ, .type = operand, .left = left, .right = zero },
                    e->range
                );
            }
            break;
        }
        case SCRIPT_EXPR_BINARY: {
            if (e->op == SCRIPT_OP_AND || e->op == SCRIPT_OP_OR) {
                return short_circuit(l, e);
            }
            if (e->op == SCRIPT_OP_EQ || e->op == SCRIPT_OP_NE) {
                int optional = l->a->expressions[e->left].kind == SCRIPT_EXPR_NIL
                                 ? e->right
                             : l->a->expressions[e->right].kind == SCRIPT_EXPR_NIL
                                 ? e->left
                                 : -1;
                if (optional >= 0 && (l->a->expressions[optional].type.flags &
                                      SCRIPT_TYPE_OPTIONAL)) {
                    int present = condition_value(l, optional);
                    int absent = constant(l, (pin_value){ .type = PIN_BOOL }, e->range);
                    return value_op(
                        l,
                        (behavior_function_instruction){
                            .op = e->op == SCRIPT_OP_EQ ? BF_EQ : BF_NE,
                            .type = PIN_BOOL,
                            .left = present,
                            .right = absent },
                        e->range
                    );
                }
            }
            int left = expression(l, e->left), right = expression(l, e->right);
            script_type operand = e->type.storage != PIN_BOOL
                                    ? e->type
                                    : l->a->expressions[e->left].type;
            if (e->type.storage == PIN_BOOL && script_type_numeric(operand)) {
                script_type other = l->a->expressions[e->right].type;
                if (other.storage > operand.storage) {
                    operand = other;
                }
            }
            left = convert(l, left, l->a->expressions[e->left].type, operand, e->range);
            right = convert(l, right, l->a->expressions[e->right].type, operand, e->range);
            return binary(l, e->op, left, right, e->type, operand, e->range);
        }
        case SCRIPT_EXPR_MEMBER: {
            if (sole_output(l, e)) {
                return expression(l, e->left);
            }
            const script_expression *receiver = &l->a->expressions[e->left];
            if (receiver->kind == SCRIPT_EXPR_SYMBOL && receiver->symbol >= 0 &&
                !(receiver->type.flags & SCRIPT_TYPE_RESULTS)) {
                const script_symbol *symbol = &l->a->symbols[receiver->symbol];
                const script_expression
                    *initializer = symbol->initializer >= 0
                                     ? &l->a->expressions[symbol->initializer]
                                     : NULL;
                const script_catalog_capability
                    *capability = initializer ? script_catalog_capability_by_id(
                                                    l->catalog,
                                                    initializer->capability
                                                )
                                              : NULL;
                if (capability && capability->result_count == 1 &&
                    e->member ==
                        script_catalog_id("port", capability->results[0].name)) {
                    return expression(l, e->left);
                }
                const script_function *function = symbol->function >= 0
                                                    ? &l->a->functions[symbol->function]
                                                    : NULL;
                if (function && function->result_count == 1 &&
                    e->member == script_catalog_id(
                                     "port",
                                     l->a->symbols[function->results[0]].name
                                 )) {
                    return expression(l, e->left);
                }
            }
            const script_symbol *called_action = receiver->kind == SCRIPT_EXPR_CALL
                                                   ? action_symbol(l, receiver)
                                                   : NULL;
            if (called_action && called_action->disabled) {
                return action_value(l, called_action->id, (int)e->offset, e->type, e->range);
            }
            if (receiver->kind == SCRIPT_EXPR_SYMBOL && receiver->symbol >= 0 &&
                l->a->symbols[receiver->symbol].kind == SCRIPT_ACTION &&
                l->a->symbols[receiver->symbol].initializer >= 0) {
                return action_value(
                    l,
                    l->a->symbols[receiver->symbol].id,
                    (int)e->offset,
                    e->type,
                    e->range
                );
            }
            if ((receiver->type.flags & SCRIPT_TYPE_RESULTS) &&
                receiver->kind == SCRIPT_EXPR_SYMBOL && receiver->symbol >= 0 &&
                l->a->symbols[receiver->symbol].kind == SCRIPT_PARAMETER) {
                int ordinal = (int)e->offset;
                int source = l->a->symbols[receiver->symbol].function;
                if (source >= 0 && source != l->payload_function &&
                    l->a->functions[source].kind == SCRIPT_FUNCTION_HANDLER) {
                    pin_value headers[] = { { .type = BF_POINTER_TYPE },
                                            { .type = PIN_U64,
                                              .u64 = function_id(l->a, source) },
                                            { .type = PIN_I32, .i = ordinal } };
                    int slots[] = { value_op(
                                        l,
                                        (behavior_function_instruction){
                                            .op = BF_CONTEXT,
                                            .type = BF_POINTER_TYPE,
                                            .target = 1 },
                                        e->range
                                    ),
                                    -1,
                                    -1 };
                    return primitive(
                        l,
                        &EXECUTION_event_read_call,
                        &EXECUTION_assignment_access,
                        headers,
                        slots,
                        3,
                        e->type,
                        e->range
                    );
                }
                if (source == l->payload_function) {
                    ordinal += l->payload_first;
                }
                if (e->type.storage == PIN_STRUCT ||
                    e->type.container != PIN_CONTAINER_SINGLE) {
                    return value_op(
                        l,
                        (behavior_function_instruction){ .op = BF_ARRAY,
                                                         .type = BF_POINTER_TYPE,
                                                         .left = ordinal,
                                                         .target = BF_ARGUMENT,
                                                         .value = initial(l, e->type) },
                        e->range
                    );
                }
                return value_op(
                    l,
                    (behavior_function_instruction){ .op = BF_ARGUMENT,
                                                     .type = slot_type(e->type),
                                                     .left = ordinal,
                                                     .value = initial(l, e->type) },
                    e->range
                );
            }
            if (e->read) {
                script_argument receiver_argument = { .value = e->left,
                                                      .parameter = 0,
                                                      .range = e->range };
                return call(
                    l,
                    &(script_expression){ .kind = SCRIPT_EXPR_CALL,
                                          .type = e->type,
                                          .range = e->range,
                                          .capability = e->read,
                                          .symbol = -1,
                                          .right = -1,
                                          .entry = -1,
                                          .arguments = &receiver_argument,
                                          .argument_count = 1 }
                );
            }
            int left = expression(l, e->left);
            if (receiver->type.flags & SCRIPT_TYPE_RESULTS) {
                return command_tuple(l, receiver)
                         ? command_result(l, left + (int)e->offset, e->type, e->range)
                         : host_result(l, left + (int)e->offset, e->type, e->range);
            }
            if (e->type.storage == PIN_STRUCT &&
                e->type.container == PIN_CONTAINER_SINGLE) {
                int offset = constant(
                    l,
                    (pin_value){ .type = BF_POINTER_TYPE, .u64 = e->offset },
                    e->range
                );
                return value_op(
                    l,
                    (behavior_function_instruction){ .op = BF_ADD,
                                                     .type = BF_POINTER_TYPE,
                                                     .left = left,
                                                     .right = offset },
                    e->range
                );
            }
            return typed_op(
                l,
                (behavior_function_instruction){ .op = BF_LOAD,
                                                 .type = slot_type(e->type),
                                                 .left = left,
                                                 .offset = e->offset },
                e->range,
                e->type
            );
        }
        default: break;
    }
    error(l, e->range, "Resolved expression requires an execution contract not yet linked");
    return -1;
}
static int expression(lower *l, int index)
{
    int value = expression_value(l, index);
    if (index >= 0 && index < l->a->expression_count && value >= 0) {
        const script_expression *e = &l->a->expressions[index];
        if ((e->type.flags & SCRIPT_TYPE_RESULTS) && e->kind == SCRIPT_EXPR_CALL) {
            const script_expression *signature = tuple_signature(l, index);
            if (signature) {
                for (int i = 0; i < signature->result_count; i++) {
                    int field = command_tuple(l, e)
                                  ? command_result(l, value + i, signature->results[i], e->range)
                                  : host_result(l, value + i, signature->results[i], e->range);
                    debug_watch(l, index, field, signature->results[i], true, i);
                }
            }
        } else {
            debug_watch(l, index, value, e->type, true, 0);
        }
    }
    return value;
}
/* The local tuple's field slots, made on first use. */
static void tuple_bank(lower *l, int symbol, const script_expression *signature)
{
    if (l->locals[symbol] >= 0) {
        return;
    }
    pin_value *defaults = NULL;
    for (int i = 0; i < signature->result_count; i++) {
        apush(defaults, initial(l, signature->results[i]));
    }
    l->locals[symbol] = behavior_function_builder_values(l->builder, defaults, acount(defaults));
    l->addresses[symbol] = l->locals[symbol];
    afree(defaults);
}
static void store_tuple(lower *l, int symbol, int value, int source, script_range range)
{
    const script_expression *expression = source >= 0 ? &l->a->expressions[source]
                                                      : NULL;
    bool command_bank = expression && command_tuple(l, expression);
    expression = tuple_signature(l, source);
    if (!expression || !expression->result_count) {
        error(l, range, "Result tuple has no resolved field signature");
        return;
    }
    const script_expression *target = tuple_signature(l, l->a->symbols[symbol].initializer);
    if (!target || target->result_count != expression->result_count) {
        error(l, range, "Result tuple assignment differs from its declared field count");
        return;
    }
    bool captured = l->captured && l->captured[symbol] >= 0;
    if (!captured) {
        tuple_bank(l, symbol, target);
    }
    for (int i = 0; i < expression->result_count; i++) {
        script_type source_type = expression->results[i], type = target->results[i];
        int field = command_bank ? command_result(l, value + i, source_type, range)
                                 : host_result(l, value + i, source_type, range);
        field = convert(l, field, source_type, type, range);
        field = retain_value(l, field, type, range);
        if (captured) {
            int descriptor = value_op(
                l,
                (behavior_function_instruction){ .op = BF_ARGUMENT,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = l->parameters[symbol] + i,
                                                 .value = { .type = BF_POINTER_TYPE } },
                range
            );
            reference_write(l, descriptor, field, type, range);
        } else {
            host_argument(l, l->locals[symbol] + i, field, type, range);
        }
    }
}
static int local_address(lower *l, int symbol)
{
    if (l->addresses[symbol] < 0) {
        pin_value header = initial(l, l->a->symbols[symbol].type);
        int host = linked_host(
            l->builder,
            &EXECUTION_snapshot_call,
            &EXECUTION_value_access,
            &header,
            1,
            &header,
            1
        );
        l->addresses[symbol] = behavior_function_builder_host_first(l->builder, host);
    }
    return l->addresses[symbol];
}
static void release(lower *l, int descriptor, script_range range)
{
    pin_value header = { .type = BF_POINTER_TYPE };
    primitive(
        l,
        &EXECUTION_local_release_call,
        &EXECUTION_assignment_access,
        &header,
        &descriptor,
        1,
        (script_type){ .storage = PIN_BOOL },
        range
    );
}
/* Each declaration of a tuple whose fields a task keeps starts new fields;
 * the task keeps the old ones in their boxes. */
static void release_tuple(lower *l, int symbol, script_range range)
{
    const script_symbol *s = &l->a->symbols[symbol];
    const script_expression *signature = tuple_signature(l, s->initializer);
    if (!s->boxed || !signature || (l->captured && l->captured[symbol] >= 0)) {
        return;
    }
    tuple_bank(l, symbol, signature);
    for (int i = 0; i < signature->result_count; i++) {
        release(l, bank_reference(l, l->locals[symbol] + i, signature->results[i], range), range);
    }
}
/* Each declaration of a local that a task keeps starts a new one; the task
 * keeps the old one in its box. */
static void declare_symbol(lower *l, int symbol, int value, script_range range)
{
    const script_symbol *s = &l->a->symbols[symbol];
    if (s->boxed && s->writable && l->variables[symbol] < 0 &&
        l->parameters[symbol] < 0) {
        local_address(l, symbol);
        int descriptor = reference_expression(
            l,
            &(script_expression){ .kind = SCRIPT_EXPR_SYMBOL,
                                  .symbol = symbol,
                                  .left = -1,
                                  .right = -1,
                                  .type = s->type,
                                  .range = range }
        );
        release(l, descriptor, range);
    }
    store_symbol(l, symbol, value, range);
}
static void store_symbol(lower *l, int symbol, int value, script_range range)
{
    if (symbol < 0 || symbol >= l->a->symbol_count) {
        error(l, range, "Assignment has no resolved target symbol");
        return;
    }
    const script_symbol *s = &l->a->symbols[symbol];
    debug_point(l, SCRIPT_RECORD_SYMBOL, symbol, range);
    if (s->type.flags & SCRIPT_TYPE_RESULTS) {
        store_tuple(l, symbol, value, s->initializer, range);
        return;
    }
    if (((s->type.flags & SCRIPT_TYPE_REFERENCE) ||
         (l->captured && l->captured[symbol] >= 0)) &&
        l->parameters[symbol] >= 0) {
        int address = value_op(
            l,
            (behavior_function_instruction){
                .op = BF_ARGUMENT,
                .type = BF_POINTER_TYPE,
                .left = l->parameters[symbol],
                .value = (pin_value){ .type = BF_POINTER_TYPE } },
            range
        );
        reference_write(l, address, value, s->type, range);
        return;
    }
    bool managed = s->type.storage == PIN_STRUCT ||
                   s->type.container != PIN_CONTAINER_SINGLE ||
                   behavior_scalar_string(s->type.storage);
    if (managed) {
        value = retain_value(l, value, s->type, range);
    }
    if (l->variables[symbol] >= 0 && managed) {
        pin_value header = initial(l, s->type);
        int host = linked_host(
            l->builder,
            &EXECUTION_snapshot_call,
            &EXECUTION_value_access,
            &header,
            1,
            &header,
            1
        );
        host_argument(l, behavior_function_builder_host_first(l->builder, host), value, s->type, range);
        emit(
            l,
            (behavior_function_instruction){
                .op = BF_STORE_VALUE,
                .left = behavior_function_builder_host_first(l->builder, host),
                .target = l->variables[symbol] },
            range
        );
        return;
    }
    if (l->variables[symbol] >= 0) {
        emit(
            l,
            (behavior_function_instruction){ .op = BF_STORE,
                                             .type = s->type.storage,
                                             .left = value,
                                             .target = l->variables[symbol] },
            range
        );
        return;
    }
    if (s->writable && (s->kind == SCRIPT_LOCAL || s->kind == SCRIPT_RESULT)) {
        host_argument(l, local_address(l, symbol), value, s->type, range);
        return;
    }
    if (l->locals[symbol] < 0) {
        l->locals[symbol] = behavior_function_builder_local(
            l->builder,
            (pin_value){ .type = slot_type(s->type) }
        );
    }
    emit(
        l,
        (behavior_function_instruction){ .op = BF_SAVE,
                                         .type = slot_type(s->type),
                                         .left = value,
                                         .target = l->locals[symbol] },
        range
    );
}
static void assign_value(lower *l, const script_statement *s, int value)
{
    int symbol = s->symbol;
    if (s->expression < 0) {
        store_symbol(l, symbol, value, s->range);
        return;
    }
    const script_expression *target = &l->a->expressions[s->expression];
    if (s->op != SCRIPT_OP_NONE) {
        value = convert(l, value, l->a->expressions[s->second].type, target->type, s->range);
        int left = expression(l, s->expression);
        value = binary(l, s->op, left, value, target->type, target->type, s->range);
    } else {
        value = convert(l, value, l->a->expressions[s->second].type, target->type, s->range);
    }
    if (target->kind == SCRIPT_EXPR_SYMBOL) {
        if (l->target_cached && s->expression == l->target_expression &&
            l->target_reference >= 0) {
            reference_write(l, l->target_reference, value, target->type, s->range);
            return;
        }
        store_symbol(l, target->symbol, value, s->range);
        return;
    }
    if (target->kind == SCRIPT_EXPR_COMPONENT || target->kind == SCRIPT_EXPR_INDEX ||
        (target->kind == SCRIPT_EXPR_MEMBER && !target->write)) {
        int parent = s->expression;
        while (parent >= 0 && l->a->expressions[parent].kind == SCRIPT_EXPR_MEMBER) {
            parent = l->a->expressions[parent].left;
        }
        uint64_t component = parent >= 0 && l->a->expressions[parent].kind == SCRIPT_EXPR_COMPONENT
                               ? l->a->expressions[parent].capability
                               : 0;
        if (parent >= 0 && l->a->expressions[parent].kind == SCRIPT_EXPR_SYMBOL &&
            l->a->symbols[l->a->expressions[parent].symbol].kind == SCRIPT_COMPONENT) {
            component = l->a->symbols[l->a->expressions[parent].symbol].type.nominal;
        }
        const script_catalog_type *component_type = script_catalog_type_by_id(l->catalog, component);
        if (component_type) {
            behavior_function_builder_component(l->builder, component_type->component_id, false, true);
        }
        int destination = l->target_cached && s->expression == l->target_expression
                            ? l->target_reference
                            : reference(l, s->expression);
        reference_write(l, destination, value, target->type, s->range);
        return;
    }
    error(l, s->range, "Assignment target requires a linked write capability or aggregate contract");
}
/* A plain state, parameter or local is read and stored in place; anything
 * else is written through a reference. */
static bool direct_target(lower *l, const script_expression *e)
{
    if (e->kind != SCRIPT_EXPR_SYMBOL || e->symbol < 0) {
        return false;
    }
    const script_symbol *s = &l->a->symbols[e->symbol];
    if ((s->type.flags & (SCRIPT_TYPE_REFERENCE | SCRIPT_TYPE_RESULTS)) ||
        (l->captured && l->captured[e->symbol] >= 0)) {
        return false;
    }
    return l->variables[e->symbol] >= 0 ||
           (l->parameters[e->symbol] >= 0 && s->kind != SCRIPT_PARAMETER) ||
           (s->writable && (s->kind == SCRIPT_LOCAL || s->kind == SCRIPT_RESULT));
}
static void assign(lower *l, const script_statement *s)
{
    int index = s->expression;
    if (index < 0 || (l->a->expressions[index].kind == SCRIPT_EXPR_MEMBER &&
                      l->a->expressions[index].write)) {
        assign_value(l, s, expression(l, s->second));
        return;
    }
    if (l->a->expressions[index].kind == SCRIPT_EXPR_SYMBOL &&
        (l->a->expressions[index].type.flags & SCRIPT_TYPE_RESULTS)) {
        store_tuple(l, l->a->expressions[index].symbol, expression(l, s->second), s->second, s->range);
        return;
    }
    int saved_expression = l->target_expression,
        saved_reference = l->target_reference, saved_value = l->target_value;
    bool saved_cached = l->target_cached;
    bool direct = direct_target(l, &l->a->expressions[index]);
    int destination = direct ? -1 : reference(l, index);
    const script_expression *rhs = &l->a->expressions[s->second];
    bool compound = s->op != SCRIPT_OP_NONE;
    if (rhs->kind == SCRIPT_EXPR_CALL && rhs->argument_count) {
        compound |= rhs->arguments[0].value == index;
    }
    l->target_expression = index;
    l->target_reference = destination;
    l->target_value = !compound ? -1
                    : direct    ? expression(l, index)
                                : reference_read(
                                      l,
                                      destination,
                                      l->a->expressions[index].type,
                                      s->range
                                  );
    l->target_cached = compound;
    int value = expression(l, s->second);
    /* Retain the destination for simple writes without replacing RHS reads. */
    l->target_cached = true;
    assign_value(l, s, value);
    l->target_expression = saved_expression;
    l->target_reference = saved_reference;
    l->target_value = saved_value;
    l->target_cached = saved_cached;
}
static void loop_finish(lower *l, int again, int after)
{
    lower_loop *loop = &l->loops[acount(l->loops) - 1];
    for (int i = 0; i < acount(loop->breaks); i++) {
        behavior_function_builder_patch_target(l->builder, loop->breaks[i], after);
    }
    for (int i = 0; i < acount(loop->continues); i++) {
        behavior_function_builder_patch_target(l->builder, loop->continues[i], again);
    }
    afree(loop->breaks);
    afree(loop->continues);
    asetlen(l->loops, acount(l->loops) - 1);
}
static void return_results(lower *l, script_range range)
{
    const script_function *f = &l->a->functions[l->function];
    if (!l->initializing) {
        behavior_source_point point = debug_origin(l, SCRIPT_RECORD_FUNCTION, l->function, range);
        point.completion = true;
        int index = behavior_function_builder_source_point(l->builder, point);
        emit(l, (behavior_function_instruction){ .op = BF_SOURCE_POINT, .left = index }, range);
    }
    for (int i = 0; i < f->result_count; i++) {
        int symbol = f->results[i];
        const script_symbol *s = &l->a->symbols[symbol];
        int value = symbol_value(
            l,
            &(script_expression){ .kind = SCRIPT_EXPR_SYMBOL,
                                  .symbol = symbol,
                                  .type = s->type,
                                  .range = range }
        );
        if (s->type.container != PIN_CONTAINER_SINGLE) {
            value = retain_value(l, value, s->type, range);
            pin_value header = initial(l, s->type);
            int name = value_op(
                l,
                (behavior_function_instruction){
                    .op = BF_TYPE_NAME,
                    .type = BF_POINTER_TYPE,
                    .target = BF_CONSTANT,
                    .value = { .type = PIN_NAME, .str = header.type_name } },
                range
            );
            emit(
                l,
                (behavior_function_instruction){ .op = BF_RESULT_ARRAY,
                                                 .type = s->type.storage,
                                                 .left = value,
                                                 .right = name,
                                                 .target = i,
                                                 .value = header },
                range
            );
        } else if (s->type.storage == PIN_STRUCT) {
            value = retain_value(l, value, s->type, range);
            int bank = value_op(
                l,
                (behavior_function_instruction){
                    .op = BF_CONTEXT,
                    .type = BF_POINTER_TYPE,
                    .offset = offsetof(behavior_function_context, results) },
                range
            );
            int ordinal = constant(l, (pin_value){ .type = PIN_I32, .i = i }, range);
            int header = value_op(
                l,
                (behavior_function_instruction){ .op = BF_INDEX,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = bank,
                                                 .right = ordinal,
                                                 .offset = sizeof(pin_value) },
                range
            );
            emit(
                l,
                (behavior_function_instruction){ .op = BF_WRITE,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = value,
                                                 .right = header,
                                                 .offset = offsetof(pin_value, ptr) },
                range
            );
        } else {
            emit(
                l,
                (behavior_function_instruction){ .op = BF_RESULT,
                                                 .type = s->type.storage,
                                                 .left = value,
                                                 .target = i },
                range
            );
        }
    }
    emit(l, (behavior_function_instruction){ .op = BF_RETURN }, range);
}
static void request_exit(lower *l, script_range range)
{
    int context = value_op(
        l,
        (behavior_function_instruction){ .op = BF_CONTEXT,
                                         .type = BF_POINTER_TYPE,
                                         .target = 1 },
        range
    );
    int value = constant(l, (pin_value){ .type = PIN_BOOL, .b = true }, range);
    emit(
        l,
        (behavior_function_instruction){
            .op = BF_WRITE,
            .type = PIN_BOOL,
            .left = value,
            .right = context,
            .offset = offsetof(behavior_function_context, exit_requested) },
        range
    );
}
static void return_outcome(lower *l, int symbol, script_range range)
{
    int context = value_op(
        l,
        (behavior_function_instruction){ .op = BF_CONTEXT,
                                         .type = BF_POINTER_TYPE,
                                         .target = 1 },
        range
    );
    uint64_t outcome = script_catalog_id("outcome", l->a->symbols[symbol].name);
    int value = constant(l, (pin_value){ .type = PIN_U64, .u64 = outcome }, range);
    emit(
        l,
        (behavior_function_instruction){
            .op = BF_WRITE,
            .type = PIN_U64,
            .left = value,
            .right = context,
            .offset = offsetof(behavior_function_context, returned_outcome) },
        range
    );
}
static void statement(lower *l, int index)
{
    if (index < 0) {
        return;
    }
    const script_statement *s = &l->a->statements[index];
    debug_point(l, SCRIPT_RECORD_STATEMENT, index, s->range);
    emit(l, (behavior_function_instruction){ .op = BF_CHECK }, s->range);
    switch (s->kind) {
        case SCRIPT_STMT_BLOCK:
            for (int i = 0; i < s->child_count; i++) {
                statement(l, s->children[i]);
            }
            return;
        case SCRIPT_STMT_SEQUENCE:
            for (int i = 0; i < s->child_count; i++) {
                const script_statement *arm = &l->a->statements[s->children[i]];
                statement(l, arm->body);
            }
            return;
        case SCRIPT_STMT_MATCH: {
            script_type type = l->a->expressions[s->expression].type;
            int value = expression(l, s->expression);
            int saved = behavior_function_builder_local(
                l->builder,
                (pin_value){ .type = slot_type(type) }
            );
            emit(
                l,
                (behavior_function_instruction){ .op = BF_SAVE,
                                                 .type = slot_type(type),
                                                 .left = value,
                                                 .target = saved },
                s->range
            );
            int *ends = NULL;
            int fallback = -1;
            for (int i = 0; i < s->child_count; i++) {
                const script_statement *arm = &l->a->statements[s->children[i]];
                if (arm->expression < 0) {
                    fallback = arm->body;
                    continue;
                }
                value = value_op(
                    l,
                    (behavior_function_instruction){ .op = BF_LOCAL,
                                                     .type = slot_type(type),
                                                     .left = saved },
                    arm->range
                );
                int candidate = expression(l, arm->expression);
                candidate = convert(
                    l,
                    candidate,
                    l->a->expressions[arm->expression].type,
                    type,
                    arm->range
                );
                int condition = binary(
                    l,
                    SCRIPT_OP_EQ,
                    value,
                    candidate,
                    (script_type){ .storage = PIN_BOOL },
                    type,
                    arm->range
                );
                int branch = emit(
                    l,
                    (behavior_function_instruction){ .op = BF_BRANCH,
                                                     .left = condition,
                                                     .target = -1 },
                    arm->range
                );
                int next = emit(
                    l,
                    (behavior_function_instruction){ .op = BF_JUMP, .target = -1 },
                    arm->range
                );
                behavior_function_builder_patch_target(l->builder, branch, l->pc);
                statement(l, arm->body);
                apush(ends, emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = -1 }, arm->range));
                behavior_function_builder_patch_target(l->builder, next, l->pc);
            }
            statement(l, fallback);
            for (int i = 0; i < acount(ends); i++) {
                behavior_function_builder_patch_target(l->builder, ends[i], l->pc);
            }
            afree(ends);
            return;
        }
        case SCRIPT_STMT_DECLARE: {
            int value = expression(l, s->expression);
            value = convert(
                l,
                value,
                l->a->expressions[s->expression].type,
                l->a->symbols[s->symbol].type,
                s->range
            );
            if (l->a->symbols[s->symbol].type.flags & SCRIPT_TYPE_RESULTS) {
                release_tuple(l, s->symbol, s->range);
                store_tuple(l, s->symbol, value, s->expression, s->range);
            } else {
                declare_symbol(l, s->symbol, value, s->range);
            }
            return;
        }
        case SCRIPT_STMT_ASSIGN: assign(l, s); return;
        case SCRIPT_STMT_EXPRESSION: expression(l, s->expression); return;
        case SCRIPT_STMT_IF: {
            int condition = condition_value(l, s->expression);
            int branch = emit(
                l,
                (behavior_function_instruction){ .op = BF_BRANCH,
                                                 .left = condition,
                                                 .target = -1 },
                s->range
            );
            statement(l, s->alternative);
            int end = emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = -1 }, s->range);
            behavior_function_builder_patch_target(l->builder, branch, l->pc);
            statement(l, s->body);
            behavior_function_builder_patch_target(l->builder, end, l->pc);
            return;
        }
        case SCRIPT_STMT_WHILE: {
            int again = l->pc;
            int condition = condition_value(l, s->expression);
            int branch = emit(
                l,
                (behavior_function_instruction){ .op = BF_BRANCH,
                                                 .left = condition,
                                                 .target = -1 },
                s->range
            );
            int end = emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = -1 }, s->range);
            behavior_function_builder_patch_target(l->builder, branch, l->pc);
            apush(l->loops, ((lower_loop){ 0 }));
            statement(l, s->body);
            emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = again }, s->range);
            behavior_function_builder_patch_target(l->builder, end, l->pc);
            loop_finish(l, again, l->pc);
            return;
        }
        case SCRIPT_STMT_FOR: {
            int first = expression(l, s->expression), last = expression(l, s->second);
            store_symbol(l, s->symbol, first, s->range);
            int bound = behavior_function_builder_local(l->builder, (pin_value){ .type = PIN_I32 });
            emit(
                l,
                (
                    behavior_function_instruction
                ){ .op = BF_SAVE, .type = PIN_I32, .left = last, .target = bound },
                s->range
            );
            int again = l->pc;
            int current = value_op(
                l,
                (behavior_function_instruction){ .op = BF_LOCAL,
                                                 .type = PIN_I32,
                                                 .left = l->locals[s->symbol] },
                s->range
            );
            last = value_op(
                l,
                (behavior_function_instruction){ .op = BF_LOCAL,
                                                 .type = PIN_I32,
                                                 .left = bound },
                s->range
            );
            int condition = value_op(
                l,
                (
                    behavior_function_instruction
                ){ .op = BF_LE, .type = PIN_I32, .left = current, .right = last },
                s->range
            );
            int branch = emit(
                l,
                (behavior_function_instruction){ .op = BF_BRANCH,
                                                 .left = condition,
                                                 .target = -1 },
                s->range
            );
            int end = emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = -1 }, s->range);
            behavior_function_builder_patch_target(l->builder, branch, l->pc);
            apush(l->loops, ((lower_loop){ 0 }));
            statement(l, s->body);
            int advance = l->pc;
            current = value_op(
                l,
                (behavior_function_instruction){ .op = BF_LOCAL,
                                                 .type = PIN_I32,
                                                 .left = l->locals[s->symbol] },
                s->range
            );
            last = value_op(
                l,
                (behavior_function_instruction){ .op = BF_LOCAL,
                                                 .type = PIN_I32,
                                                 .left = bound },
                s->range
            );
            /* Test equality before increment, so an INT_MAX upper bound never wraps. */
            int done = value_op(
                l,
                (
                    behavior_function_instruction
                ){ .op = BF_EQ, .type = PIN_I32, .left = current, .right = last },
                s->range
            );
            int at_bound = emit(
                l,
                (behavior_function_instruction){ .op = BF_BRANCH,
                                                 .left = done,
                                                 .target = -1 },
                s->range
            );
            int one = constant(l, (pin_value){ .type = PIN_I32, .i = 1 }, s->range);
            int next = value_op(
                l,
                (
                    behavior_function_instruction
                ){ .op = BF_ADD, .type = PIN_I32, .left = current, .right = one },
                s->range
            );
            store_symbol(l, s->symbol, next, s->range);
            emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = again }, s->range);
            behavior_function_builder_patch_target(l->builder, end, l->pc);
            behavior_function_builder_patch_target(l->builder, at_bound, l->pc);
            loop_finish(l, advance, l->pc);
            return;
        }
        case SCRIPT_STMT_FOREACH: {
            script_type collection = l->a->expressions[s->expression].type;
            int array = retain_value(l, expression(l, s->expression), collection, s->range);
            int saved_array = behavior_function_builder_local(
                l->builder,
                (pin_value){ .type = BF_POINTER_TYPE }
            );
            emit(
                l,
                (behavior_function_instruction){ .op = BF_SAVE,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = array,
                                                 .target = saved_array },
                s->range
            );
            int ordinal = behavior_function_builder_local(l->builder, (pin_value){ .type = PIN_I32 });
            int zero = constant(l, (pin_value){ .type = PIN_I32 }, s->range);
            emit(
                l,
                (
                    behavior_function_instruction
                ){ .op = BF_SAVE, .type = PIN_I32, .left = zero, .target = ordinal },
                s->range
            );
            int again = l->pc;
            array = value_op(
                l,
                (behavior_function_instruction){ .op = BF_LOCAL,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = saved_array },
                s->range
            );
            int index = value_op(
                l,
                (behavior_function_instruction){ .op = BF_LOCAL,
                                                 .type = PIN_I32,
                                                 .left = ordinal },
                s->range
            );
            int count = array_count(l, array, s->range);
            int condition = value_op(
                l,
                (
                    behavior_function_instruction
                ){ .op = BF_LT, .type = PIN_I32, .left = index, .right = count },
                s->range
            );
            int branch = emit(
                l,
                (behavior_function_instruction){ .op = BF_BRANCH,
                                                 .left = condition,
                                                 .target = -1 },
                s->range
            );
            int end = emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = -1 }, s->range);
            behavior_function_builder_patch_target(l->builder, branch, l->pc);
            script_type element = l->a->symbols[s->symbol].type;
            declare_symbol(l, s->symbol, array_item(l, array, index, element, s->range), s->range);
            apush(l->loops, ((lower_loop){ 0 }));
            statement(l, s->body);
            int advance = l->pc;
            index = value_op(
                l,
                (behavior_function_instruction){ .op = BF_LOCAL,
                                                 .type = PIN_I32,
                                                 .left = ordinal },
                s->range
            );
            int one = constant(l, (pin_value){ .type = PIN_I32, .i = 1 }, s->range);
            int next = value_op(
                l,
                (
                    behavior_function_instruction
                ){ .op = BF_ADD, .type = PIN_I32, .left = index, .right = one },
                s->range
            );
            emit(
                l,
                (
                    behavior_function_instruction
                ){ .op = BF_SAVE, .type = PIN_I32, .left = next, .target = ordinal },
                s->range
            );
            emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = again }, s->range);
            behavior_function_builder_patch_target(l->builder, end, l->pc);
            loop_finish(l, advance, l->pc);
            return;
        }
        case SCRIPT_STMT_BREAK:
        case SCRIPT_STMT_CONTINUE: {
            assert(acount(l->loops));
            int pc = emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = -1 }, s->range);
            lower_loop *loop = &l->loops[acount(l->loops) - 1];
            if (s->kind == SCRIPT_STMT_BREAK) {
                apush(loop->breaks, pc);
            } else {
                apush(loop->continues, pc);
            }
            return;
        }
        case SCRIPT_STMT_RETURN:
            request_exit(l, s->range);
            if (s->symbol >= 0) {
                return_outcome(l, s->symbol, s->range);
            }
            for (int i = 0; i < s->child_count; i++) {
                statement(l, s->children[i]);
            }
            if (s->expression >= 0) {
                const script_function *f = &l->a->functions[l->function];
                assert(f->result_count == 1);
                int value = expression(l, s->expression);
                value = convert(
                    l,
                    value,
                    l->a->expressions[s->expression].type,
                    l->a->symbols[f->results[0]].type,
                    s->range
                );
                store_symbol(l, f->results[0], value, s->range);
            }
            return_results(l, s->range);
            return;
        case SCRIPT_STMT_LABEL: l->labels[s->symbol] = l->pc; return;
        case SCRIPT_STMT_GOTO:
            if (s->symbol >= 0 && l->a->symbols[s->symbol].outcome) {
                request_exit(l, s->range);
                return_outcome(l, s->symbol, s->range);
                return_results(l, s->range);
                return;
            }
            apush(
                l->goto_pc,
                emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = -1 }, s->range)
            );
            apush(l->goto_symbol, s->symbol);
            return;
        default:
            error(l, s->range, "Statement requires an execution contract not yet linked");
            return;
    }
}
static int *indices(int count)
{
    int *out = xmalloc((size_t)(count + 1) * sizeof *out);
    for (int i = 0; i <= count; i++) {
        out[i] = -1;
    }
    return out;
}
static void discard_bodies(execution_module_plan *module)
{
    for (int i = 0; i < module->function_count; i++) {
        behavior_function_free(module->functions[i].body);
        module->functions[i].body = NULL;
        behavior_function_free(module->functions[i].subscription);
        module->functions[i].subscription = NULL;
    }
    for (int i = 0; i < module->binding_count; i++) {
        behavior_function_free(module->bindings[i].body);
        module->bindings[i].body = NULL;
        behavior_function_free(module->bindings[i].dependencies);
        module->bindings[i].dependencies = NULL;
    }
}
static uint64_t function_id(const script_analysis *analysis, int index)
{
    const script_function *function = &analysis->functions[index];
    if (function->symbol >= 0) {
        return analysis->symbols[function->symbol].id;
    }
    size_t size = strlen(analysis->identity) + 64;
    char *identity = xmalloc(size);
    snprintf(
        identity,
        size,
        "%s:%d:%d",
        analysis->identity,
        function->range.start,
        function->kind
    );
    uint64_t id = script_catalog_id("function", identity);
    free(identity);
    return id;
}
static void dependency_expressions(lower *l, int index, bool *visited, int **out)
{
    if (index < 0 || visited[index]) {
        return;
    }
    visited[index] = true;
    const script_expression *e = &l->a->expressions[index];
    if (e->kind == SCRIPT_EXPR_MEMBER || e->kind == SCRIPT_EXPR_INDEX ||
        e->kind == SCRIPT_EXPR_COMPONENT) {
        apush(*out, index);
        return;
    }
    if (e->kind == SCRIPT_EXPR_SYMBOL) {
        const script_symbol *symbol = &l->a->symbols[e->symbol];
        if (symbol->kind == SCRIPT_STATE || symbol->kind == SCRIPT_SHARED_STATE ||
            symbol->kind == SCRIPT_GLOBAL || symbol->kind == SCRIPT_COMPONENT) {
            apush(*out, index);
        }
        return;
    }
    switch (e->kind) {
        case SCRIPT_EXPR_CAST:
        case SCRIPT_EXPR_UNARY:
        case SCRIPT_EXPR_FALLBACK:
            dependency_expressions(l, e->left, visited, out);
            return;
        case SCRIPT_EXPR_BINARY:
            dependency_expressions(l, e->left, visited, out);
            dependency_expressions(l, e->right, visited, out);
            return;
        case SCRIPT_EXPR_CALL:
            if ((e->effects & SCRIPT_EFFECT_READ) &&
                !(e->type.flags & SCRIPT_TYPE_RESULTS)) {
                apush(*out, index);
                return;
            }
            /* Fall through to the parameter expressions of a pure call. */
        case SCRIPT_EXPR_ARRAY:
        case SCRIPT_EXPR_STRUCT:
            for (int i = 0; i < e->argument_count; i++) {
                dependency_expressions(l, e->arguments[i].value, visited, out);
            }
            return;
        default: return;
    }
}
static bool owner_expression(lower *l, int index)
{
    return index < 0 || (l->a->expressions[index].kind == SCRIPT_EXPR_SYMBOL &&
                         l->a->expressions[index].symbol == l->a->owner_symbol);
}
static void field_access(
    execution_module_binding *binding,
    uint64_t component,
    size_t offset,
    size_t size,
    bool any_entity
)
{
    for (int i = 0; i < acount(binding->fields); i++) {
        execution_field_access *field = &binding->fields[i];
        if (field->component == component && field->offset == offset &&
            field->size == size) {
            field->any_entity |= any_entity;
            return;
        }
    }
    apush(binding->fields, ((execution_field_access){ component, offset, size, any_entity }));
    binding->field_count = acount(binding->fields);
}
static void function_read_fields(execution_module_binding *binding, const behavior_function *function)
{
    int count = behavior_function_component_reads(function, NULL, 0);
    uint64_t *ids = xmalloc((size_t)(count + 1) * sizeof *ids);
    behavior_function_component_reads(function, ids, count);
    for (int i = 0; i < count; i++) {
        field_access(binding, ids[i], 0, SIZE_MAX, true);
    }
    free(ids);
}
static void native_read_fields(
    lower *l,
    execution_module_binding *binding,
    const script_expression *e,
    uint64_t capability
)
{
    script_lower_host host = { 0 };
    if (!l->bindings || !l->bindings->host ||
        !l->bindings->host(l->bindings->user, capability, &host)) {
        return;
    }
    if (host.function) {
        function_read_fields(binding, host.function);
        return;
    }
    if (host.query || !host.access) {
        field_access(binding, 0, 0, SIZE_MAX, true);
    }
    if (!host.access) {
        return;
    }
    bool any_entity = true;
    if ((host.access->flags & NODE_EFFECT_ENTITY_SCOPED) &&
        host.access->target_input > 0) {
        for (int i = 0; i < e->argument_count; i++) {
            if (e->arguments[i].parameter == host.access->target_input - 1 &&
                owner_expression(l, e->arguments[i].value)) {
                any_entity = false;
            }
        }
    }
    for (int i = 0; i < host.access->read_count; i++) {
        uint64_t component = host.access->reads[i];
        size_t size = SIZE_MAX;
        for (int j = 0; j < l->catalog->type_count; j++) {
            if (l->catalog->types[j].component_id == component) {
                size = l->catalog->types[j].size;
                break;
            }
        }
        field_access(binding, component, 0, size, any_entity);
    }
    for (int i = 0; host.access->read_refs && host.access->read_refs[i]; i++) {
        field_access(binding, *host.access->read_refs[i], 0, SIZE_MAX, any_entity);
    }
}
static void binding_fields(lower *l, execution_module_binding *binding, int index, bool *visited)
{
    if (index < 0 || visited[index]) {
        return;
    }
    visited[index] = true;
    const script_expression *e = &l->a->expressions[index];
    if (e->kind == SCRIPT_EXPR_MEMBER || e->kind == SCRIPT_EXPR_INDEX ||
        e->kind == SCRIPT_EXPR_COMPONENT) {
        int root = index;
        size_t offset = 0;
        bool indexed = false;
        while (root >= 0 && (l->a->expressions[root].kind == SCRIPT_EXPR_MEMBER ||
                             l->a->expressions[root].kind == SCRIPT_EXPR_INDEX)) {
            const script_expression *step = &l->a->expressions[root];
            offset += step->kind == SCRIPT_EXPR_MEMBER ? step->offset : 0;
            indexed |= step->kind == SCRIPT_EXPR_INDEX;
            if (step->kind == SCRIPT_EXPR_INDEX) {
                binding_fields(l, binding, step->right, visited);
            }
            if (step->read) {
                native_read_fields(l, binding, step, step->read);
            }
            root = step->left;
        }
        const script_expression *base = root >= 0 ? &l->a->expressions[root] : NULL;
        uint64_t nominal = base && base->kind == SCRIPT_EXPR_COMPONENT ? base->capability
                                                                       : 0;
        bool any_entity = base && base->kind == SCRIPT_EXPR_COMPONENT
                            ? !owner_expression(l, base->left)
                            : false;
        if (base && base->kind == SCRIPT_EXPR_SYMBOL && base->symbol >= 0 &&
            l->a->symbols[base->symbol].kind == SCRIPT_COMPONENT) {
            nominal = l->a->symbols[base->symbol].type.nominal;
        }
        const script_catalog_type *type = script_catalog_type_by_id(l->catalog, nominal);
        if (type && type->component_id) {
            size_t size = execution_value_size(initial(l, e->type));
            field_access(
                binding,
                type->component_id,
                indexed ? 0 : offset,
                indexed ? type->size : size,
                any_entity
            );
            if (base->kind == SCRIPT_EXPR_COMPONENT) {
                binding_fields(l, binding, base->left, visited);
            }
            return;
        }
        binding_fields(l, binding, e->left, visited);
        return;
    }
    if (e->kind == SCRIPT_EXPR_SYMBOL) {
        const script_symbol *symbol = &l->a->symbols[e->symbol];
        if (symbol->kind == SCRIPT_COMPONENT) {
            const script_catalog_type
                *type = script_catalog_type_by_id(l->catalog, symbol->type.nominal);
            if (type && type->component_id) {
                field_access(binding, type->component_id, 0, type->size, false);
            }
        }
        return;
    }
    if (e->kind == SCRIPT_EXPR_CAST || e->kind == SCRIPT_EXPR_UNARY ||
        e->kind == SCRIPT_EXPR_FALLBACK || e->kind == SCRIPT_EXPR_BINARY) {
        binding_fields(l, binding, e->left, visited);
        if (e->kind == SCRIPT_EXPR_BINARY) {
            binding_fields(l, binding, e->right, visited);
        }
        return;
    }
    if (e->kind == SCRIPT_EXPR_CALL) {
        if (e->right >= 0) {
            int function = e->entry >= 0 ? e->entry : e->right;
            if (l->module->functions[function].body) {
                function_read_fields(binding, l->module->functions[function].body);
            }
        } else {
            native_read_fields(l, binding, e, e->capability);
        }
    }
    for (int i = 0; i < e->argument_count; i++) {
        binding_fields(l, binding, e->arguments[i].value, visited);
    }
}
static behavior_function *value_evaluator(
    lower *l,
    const int *expressions,
    const script_type *types,
    int count,
    script_range range
)
{
    l->function = -1;
    l->builder = behavior_function_builder_new();
    l->pc = 0;
    l->payload_first = 0;
    l->payload_function = -1;
    l->locals = indices(l->a->symbol_count);
    l->addresses = indices(l->a->symbol_count);
    l->parameters = indices(l->a->symbol_count);
    l->results = indices(l->a->symbol_count);
    l->captured = indices(l->a->symbol_count);
    pin_value *variables = NULL, *results = NULL;
    for (int i = 0; i < l->module->state_count; i++) {
        apush(variables, l->module->states[i].initial);
    }
    for (int i = 0; i < count; i++) {
        apush(results, initial(l, types[i]));
    }
    behavior_function_builder_signature(l->builder, NULL, 0, results, count, variables, acount(variables));
    afree(variables);
    afree(results);
    for (int i = 0; i < count; i++) {
        script_type type = types[i];
        script_type source_type = l->a->expressions[expressions[i]].type;
        int value = expression(l, expressions[i]);
        value = convert(l, value, source_type, type, range);
        if (type.storage == PIN_STRUCT || type.container != PIN_CONTAINER_SINGLE ||
            behavior_scalar_string(type.storage)) {
            value = retain_value(l, value, type, range);
        }
        int bank = value_op(
            l,
            (behavior_function_instruction){
                .op = BF_CONTEXT,
                .type = BF_POINTER_TYPE,
                .offset = offsetof(behavior_function_context, results) },
            range
        );
        int ordinal = constant(l, (pin_value){ .type = PIN_I32, .i = i }, range);
        int header = value_op(
            l,
            (behavior_function_instruction){ .op = BF_INDEX,
                                             .type = BF_POINTER_TYPE,
                                             .left = bank,
                                             .right = ordinal,
                                             .offset = sizeof(pin_value) },
            range
        );
        emit(
            l,
            (behavior_function_instruction){ .op = BF_WRITE,
                                             .type = slot_type(type),
                                             .left = value,
                                             .right = header,
                                             .offset = offsetof(pin_value, d) },
            range
        );
    }
    emit(l, (behavior_function_instruction){ .op = BF_RETURN }, range);
    emit(l, (behavior_function_instruction){ .op = BF_END }, range);
    behavior_function *function = NULL;
    char message[192];
    if (!l->failed) {
        function = behavior_function_builder_finish(l->builder, message, sizeof message);
        if (!function) {
            error(l, range, message);
        }
    }
    behavior_function_builder_free(l->builder);
    free(l->captured);
    l->captured = NULL;
    free(l->locals);
    free(l->addresses);
    free(l->parameters);
    free(l->results);
    return function;
}
static void lower_function(lower *l, int index)
{
    const script_function *f = &l->a->functions[index];
    const script_function *owner = f->parent >= 0 ? &l->a->functions[f->parent] : f;
    bool closure = f->kind == SCRIPT_FUNCTION_CONTINUATION;
    bool child_callable = f->parent >= 0 &&
                          (closure || f->kind == SCRIPT_FUNCTION_MACRO_ENTRY);
    const script_function *output = child_callable ? owner : f;
    l->function = index;
    if (!concrete_function(l->a, f)) {
        error(l, f->range, "Generic definitions require specialized executable instances");
    }
    l->builder = l->builders[index];
    behavior_function_builder_limits(l->builder, 0, f->depth_limit);
    l->pc = 0;
    l->async_emitted = false;
    l->locals = indices(l->a->symbol_count);
    l->addresses = indices(l->a->symbol_count);
    l->parameters = indices(l->a->symbol_count);
    l->results = indices(l->a->symbol_count);
    l->labels = indices(l->a->symbol_count);
    pin_value *args = NULL, *results = NULL, *variables = NULL;
    const script_catalog_capability
        *event = f->event ? script_catalog_capability_by_id(l->catalog, f->event)
                          : NULL;
    bool continuation = f->kind == SCRIPT_FUNCTION_CONTINUATION;
    l->captured = indices(l->a->symbol_count);
    if (closure && f->parent >= 0) {
        for (int i = 0; i < owner->parameter_count; i++) {
            int symbol = owner->parameters[i];
            l->parameters[symbol] = acount(args);
            apush(args, signature_initial(l, l->a->symbols[symbol].type));
        }
    }
    if (closure && f->parent >= 0 && l->function_addresses[f->parent]) {
        int *banks = l->function_addresses[f->parent];
        execution_module_function *definition = &l->module->functions[index];
        for (int i = 0; i < l->a->symbol_count; i++) {
            if (banks[i] >= 0 && l->a->symbols[i].kind == SCRIPT_LOCAL) {
                const script_expression
                    *tuple = l->a->symbols[i].type.flags & SCRIPT_TYPE_RESULTS
                               ? tuple_signature(l, l->a->symbols[i].initializer)
                               : NULL;
                definition->local_capture_count += tuple ? tuple->result_count : 1;
            }
        }
        definition->captures = xcalloc(
            (size_t)definition->local_capture_count,
            sizeof *definition->captures
        );
        int ordinal = 0;
        for (int i = 0; i < l->a->symbol_count; i++) {
            if (banks[i] < 0 || l->a->symbols[i].kind != SCRIPT_LOCAL) {
                continue;
            }
            const script_expression
                *tuple = l->a->symbols[i].type.flags & SCRIPT_TYPE_RESULTS
                           ? tuple_signature(l, l->a->symbols[i].initializer)
                           : NULL;
            int count = tuple ? tuple->result_count : 1;
            l->captured[i] = l->parameters[i] = acount(args);
            for (int j = 0; j < count; j++) {
                pin_value type = initial(
                    l,
                    tuple ? tuple->results[j] : l->a->symbols[i].type
                );
                pin_value owned;
                if (pin_value_snapshot_to_arena(&l->module->storage, &owned, &type)) {
                    error(l, f->range, "Cannot retain closure capture type");
                    owned = type;
                }
                definition->captures[ordinal++] = (execution_module_capture){
                    .id = l->a->symbols[i].id,
                    .value = banks[i] + j,
                    .type = owned
                };
                apush(args, ((pin_value){ .type = BF_POINTER_TYPE }));
            }
        }
    }
    int payload_first = acount(args);
    l->payload_first = payload_first;
    l->payload_function = index;
    if (continuation) {
        for (int i = 0; i < l->a->symbol_count; i++) {
            if (l->a->symbols[i].id == f->action) {
                event = script_catalog_capability_by_id(
                    l->catalog,
                    l->a->symbols[i].capability
                );
                break;
            }
        }
    }
    if (event && !continuation) {
        int count = event->parameter_count;
        const script_catalog_parameter *payload = event->parameters;
        for (int i = 0; i < count; i++) {
            apush(args, initial(l, payload[i].type));
        }
    }
    for (int i = 0; i < f->parameter_count; i++) {
        const script_symbol *s = &l->a->symbols[f->parameters[i]];
        if (!event || continuation) {
            apush(args, signature_initial(l, s->type));
        }
        l->parameters[f->parameters[i]] = payload_first + i;
    }
    if (child_callable && !closure) {
        for (int i = 0; i < owner->parameter_count; i++) {
            l->parameters[owner->parameters[i]] = i;
        }
    }
    for (int i = 0; i < output->result_count; i++) {
        int symbol = output->results[i];
        apush(results, initial(l, l->a->symbols[symbol].type));
        l->results[symbol] = i;
        l->locals[symbol] = behavior_function_builder_local(
            l->builder,
            (pin_value){ .type = slot_type(l->a->symbols[symbol].type) }
        );
        set_kind(
            l,
            BEHAVIOR_BANK_LOCAL,
            l->locals[symbol],
            payload_kind(l->a->symbols[symbol].type)
        );
    }
    for (int i = 0; i < l->module->state_count; i++) {
        apush(variables, l->module->states[i].initial);
    }
    behavior_function_builder_signature(
        l->builder,
        args,
        acount(args),
        results,
        acount(results),
        variables,
        acount(variables)
    );
    for (int i = 0; i < l->a->symbol_count; i++) {
        if (l->parameters[i] < 0) {
            continue;
        }
        if (l->captured[i] >= 0) {
            const script_symbol *symbol = &l->a->symbols[i];
            const script_expression *tuple = symbol->type.flags & SCRIPT_TYPE_RESULTS
                                               ? tuple_signature(l, symbol->initializer)
                                               : NULL;
            int count = tuple ? tuple->result_count : 1;
            for (int j = 0; j < count; j++) {
                set_kind(l, BEHAVIOR_BANK_ARGUMENT, l->parameters[i] + j, BEHAVIOR_REFERENCE);
            }
        } else if (l->a->symbols[i].type.flags & SCRIPT_TYPE_REFERENCE) {
            set_kind(l, BEHAVIOR_BANK_ARGUMENT, l->parameters[i], BEHAVIOR_REFERENCE);
        }
    }
    afree(args);
    afree(results);
    afree(variables);
    for (int i = 0; i < output->result_count; i++) {
        int symbol = output->results[i];
        script_type type = l->a->symbols[symbol].type;
        if ((type.storage == PIN_STRUCT || type.container != PIN_CONTAINER_SINGLE) &&
            !(type.flags & SCRIPT_TYPE_OPTIONAL)) {
            script_expression zero = { .kind = type.container == PIN_CONTAINER_SINGLE
                                                 ? SCRIPT_EXPR_STRUCT
                                                 : SCRIPT_EXPR_ARRAY,
                                       .type = type,
                                       .range = f->range };
            store_symbol(l, symbol, constructor(l, &zero), f->range);
        }
    }
    if (child_callable) {
        l->function = f->parent;
        if (!closure) {
            statement(l, owner->body);
        }
    }
    debug_point(l, SCRIPT_RECORD_FUNCTION, index, f->range);
    statement(l, f->body);
    if (f->kind == SCRIPT_FUNCTION_AUTHORED) {
        for (int i = 0; i < l->a->function_count; i++) {
            const script_function *entry = &l->a->functions[i];
            if (entry->parent != index || entry->kind != SCRIPT_FUNCTION_MACRO_ENTRY ||
                !entry->entry || strcmp(entry->entry, "entry")) {
                continue;
            }
            for (int j = 0; j < entry->parameter_count; j++) {
                l->parameters[entry->parameters[j]] = j;
            }
            statement(l, entry->body);
        }
    }
    if (l->async_emitted) {
        int pending = value_op(
            l,
            (behavior_function_instruction){
                .op = BF_CONTEXT,
                .type = PIN_I32,
                .offset = offsetof(behavior_function_context, pending_commands) },
            f->range
        );
        int zero = constant(l, (pin_value){ .type = PIN_I32 }, f->range);
        int waiting = value_op(
            l,
            (
                behavior_function_instruction
            ){ .op = BF_GT, .type = PIN_I32, .left = pending, .right = zero },
            f->range
        );
        int branch = emit(
            l,
            (behavior_function_instruction){ .op = BF_BRANCH, .left = waiting, .target = -1 },
            f->range
        );
        int immediate = emit(
            l,
            (behavior_function_instruction){ .op = BF_JUMP, .target = -1 },
            f->range
        );
        behavior_function_builder_patch_target(l->builder, branch, l->pc);
        emit(l, (behavior_function_instruction){ .op = BF_SUSPEND }, f->range);
        emit(l, (behavior_function_instruction){ .op = BF_RETURN }, f->range);
        behavior_function_builder_patch_target(l->builder, immediate, l->pc);
    }
    for (int i = 0; i < acount(l->goto_pc); i++) {
        int target = l->labels[l->goto_symbol[i]];
        if (target < 0) {
            error(l, f->range, "Unresolved branch label");
        } else {
            behavior_function_builder_patch_target(l->builder, l->goto_pc[i], target);
        }
    }
    return_results(l, f->range);
    emit(l, (behavior_function_instruction){ .op = BF_END }, f->range);
    free(l->locals);
    l->function_addresses[index] = l->addresses;
    free(l->captured);
    l->captured = NULL;
    free(l->parameters);
    free(l->results);
    free(l->labels);
    afree(l->goto_pc);
    afree(l->goto_symbol);
}
static pin_value evaluate_default(lower *l, const script_symbol *s)
{
    l->function = -1;
    pin_value result = initial(l, s->type);
    l->builder = behavior_function_builder_new();
    l->pc = 0;
    l->initializing = true;
    l->initializer_depth = 0;
    l->locals = indices(l->a->symbol_count);
    l->addresses = indices(l->a->symbol_count);
    l->parameters = indices(l->a->symbol_count);
    l->results = indices(l->a->symbol_count);
    behavior_function_builder_signature(l->builder, NULL, 0, &result, 1, NULL, 0);
    int value = expression(l, s->initializer);
    if (s->type.storage == PIN_STRUCT || s->type.container != PIN_CONTAINER_SINGLE) {
        int bank = value_op(
            l,
            (behavior_function_instruction){
                .op = BF_CONTEXT,
                .type = BF_POINTER_TYPE,
                .offset = offsetof(behavior_function_context, results) },
            s->declaration
        );
        emit(
            l,
            (behavior_function_instruction){ .op = BF_WRITE,
                                             .type = BF_POINTER_TYPE,
                                             .left = value,
                                             .right = bank,
                                             .offset = offsetof(pin_value, ptr) },
            s->declaration
        );
    } else {
        value = convert(l, value, l->a->expressions[s->initializer].type, s->type, s->declaration);
        emit(
            l,
            (
                behavior_function_instruction
            ){ .op = BF_RESULT, .type = s->type.storage, .left = value, .target = 0 },
            s->declaration
        );
    }
    emit(l, (behavior_function_instruction){ .op = BF_RETURN }, s->declaration);
    emit(l, (behavior_function_instruction){ .op = BF_END }, s->declaration);
    if (!l->failed) {
        char message[192];
        behavior_function *f = behavior_function_builder_finish(l->builder, message, sizeof message);
        if (!f) {
            error(l, s->declaration, message);
        } else {
            arena storage = { 0 };
            behavior_function_context context = { .results = &result, .budget = 100000 };
            behavior_function_bind_storage(&context, &storage);
            if (!behavior_function_bind(f, &context, NULL, 0) ||
                behavior_function_run(f, &context, false)) {
                error(l, s->declaration, context.error_message ? context.error_message : "Cannot evaluate state default");
                result = initial(l, s->type);
            } else {
                pin_value retained;
                if (pin_value_snapshot_to_arena(&l->module->storage, &retained, &result)) {
                    error(l, s->declaration, "Cannot retain evaluated state default");
                    result = initial(l, s->type);
                } else {
                    result = retained;
                }
            }
            behavior_function_release_host(&context);
            arena_free(&storage);
            behavior_function_free(f);
        }
    }
    behavior_function_builder_free(l->builder);
    free(l->locals);
    free(l->addresses);
    free(l->parameters);
    free(l->results);
    l->initializing = false;
    return result;
}
static void event_declaration(lower *l, uint64_t id)
{
    if (!id) {
        return;
    }
    for (int i = 0; i < acount(l->module->events); i++) {
        if (l->module->events[i].id == id) {
            return;
        }
    }
    const script_catalog_capability *event = script_catalog_capability_by_id(l->catalog, id);
    if (!event || event->kind != SCRIPT_CAPABILITY_EVENT) {
        error(l, (script_range){ 0 }, "Referenced event has no declaration schema");
        return;
    }
    execution_module_event_declaration declaration = {
        .id = id,
        .abi = script_catalog_capability_abi(l->catalog, event),
        .name = event->name,
        .field_count = event->parameter_count,
        .fields = xcalloc((size_t)event->parameter_count, sizeof *declaration.fields)
    };
    for (int i = 0; i < declaration.field_count; i++) {
        const script_catalog_parameter *parameter = &event->parameters[i];
        declaration.fields[i] = (execution_module_event_field){
            .name = parameter->name,
            .initial = parameter->has_default ? parameter->default_value
                                              : initial(l, parameter->type),
            .nominal = parameter->type.nominal,
            .has_default = parameter->has_default,
            .default_owner = parameter->default_owner
        };
    }
    apush(l->module->events, declaration);
    l->module->event_count = acount(l->module->events);
}
execution_module_plan *script_lower(
    const script_analysis *a,
    const script_catalog *catalog,
    const script_lower_bindings *bindings
)
{
    assert(a && catalog);
    execution_module_plan *m = execution_module_plan_new();
    if (!a->valid) {
        for (int i = 0; i < a->diagnostic_count; i++) {
            const script_diagnostic *source = &a->diagnostics[i];
            execution_module_diagnostic diagnostic = { .range = {
                                                           source->range.start,
                                                           source->range.end,
                                                           source->range.line,
                                                           source->range.column } };
            memcpy(diagnostic.message, source->message, sizeof diagnostic.message);
            apush(m->diagnostics, diagnostic);
        }
        m->diagnostic_count = acount(m->diagnostics);
        return m;
    }
    assert(a->identity);
    m->id = script_catalog_id("module", a->identity);
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *type = &catalog->types[i];
        const char *name = type->name;
        if (name) {
            size_t size = strlen(name) + 1;
            char *owned = arena_alloc(&m->storage, size);
            memcpy(owned, name, size);
            name = owned;
        }
        apush(
            m->types,
            ((execution_type_requirement){ .id = type->id,
                                           .layout = type->layout,
                                           .storage = type->type.storage,
                                           .name = name,
                                           .size = type->size })
        );
    }
    bool entity_declared = false;
    for (int i = 0; i < acount(m->types); i++) {
        entity_declared |= m->types[i].id == catalog->entity_type;
    }
    if (catalog->entity_type && !entity_declared) {
        static const char entity_name[] = "ecs.entity";
        char *name = arena_alloc(&m->storage, sizeof entity_name);
        memcpy(name, entity_name, sizeof entity_name);
        apush(
            m->types,
            ((execution_type_requirement){ .id = catalog->entity_type,
                                           .name = name,
                                           .storage = PIN_HANDLE,
                                           .size = sizeof(uint64_t) })
        );
    }
    m->type_count = acount(m->types);
    lower l = { .a = a, .catalog = catalog, .bindings = bindings, .module = m };
    m->query = a->query;
    m->owner_type = a->owner_type;
    l.variables = indices(a->symbol_count);
    for (int i = 0; i < a->symbol_count; i++) {
        const script_symbol *s = &a->symbols[i];
        if (s->kind != SCRIPT_STATE && s->kind != SCRIPT_SHARED_STATE) {
            continue;
        }
        pin_value value = initial(&l, s->type);
        if (s->initializer < 0 && value.type == PIN_STRUCT &&
            value.container == PIN_CONTAINER_SINGLE &&
            !(s->type.flags & SCRIPT_TYPE_OPTIONAL)) {
            const type_desc *type = type_catalog_find(value.type_name);
            if (type && type_is_plain(type)) {
                value.ptr = arena_alloc(&m->storage, type->size);
                memset(value.ptr, 0, type->size);
            }
        }
        if (s->initializer >= 0) {
            const script_expression *e = &a->expressions[s->initializer];
            if (e->kind == SCRIPT_EXPR_CONSTANT) {
                value = e->constant;
                if (value.type != s->type.storage && script_type_numeric(s->type)) {
                    value = behavior_scalar_cast(value, s->type.storage);
                }
                pin_value declared = initial(&l, s->type);
                if (value.type != declared.type || value.container != declared.container) {
                    error(&l, s->declaration, "State constant differs from its declared storage");
                }
                value.type_name = declared.type_name;
            } else if (!l.failed) {
                value = evaluate_default(&l, s);
            }
        }
        pin_value retained;
        if (pin_value_snapshot_to_arena(&m->storage, &retained, &value)) {
            error(&l, s->declaration, "State default cannot be retained by the module");
            retained = (pin_value){ .type = value.type };
        }
        l.variables[i] = acount(m->states);
        apush(m->states, ((execution_module_state){ s->id, retained, s->kind == SCRIPT_SHARED_STATE }));
    }
    m->state_count = acount(m->states);
    for (int i = 0; i < a->query_count; i++) {
        const script_query *source = &a->queries[i];
        execution_module_query q = { .kind = (execution_query_kind)source->kind,
                                     .component = source->component,
                                     .child_count = source->child_count };
        q.children = xmalloc((size_t)(q.child_count + 1) * sizeof *q.children);
        memcpy(
            q.children,
            a->queries[i].children,
            (size_t)q.child_count * sizeof *q.children
        );
        apush(m->queries, q);
    }
    m->query_count = acount(m->queries);
    for (int i = 0; i < a->symbol_count; i++) {
        const script_symbol *symbol = &a->symbols[i];
        if (symbol->kind != SCRIPT_ACTION || symbol->initializer < 0) {
            continue;
        }
        const script_catalog_capability
            *capability = script_catalog_capability_by_id(catalog, symbol->capability);
        const script_expression *initializer = &a->expressions[symbol->initializer];
        bool explicit_outcomes = false;
        int authored_scope = -1, authored_outcomes = 0;
        if (!capability && initializer->kind == SCRIPT_EXPR_CALL &&
            initializer->right >= 0) {
            int scope = a->functions[initializer->right].scope;
            authored_scope = scope;
            for (int j = 0; j < a->symbol_count; j++) {
                if (a->symbols[j].outcome && a->symbols[j].scope == scope) {
                    explicit_outcomes = true;
                    authored_outcomes++;
                }
            }
        }
        execution_module_action action = {
            .id = symbol->id,
            .capability = symbol->capability,
            .state_kind = capability ? capability->state_kind : NATIVE_STATE_NONE,
            .loop = capability &&
                    (capability->flow.kind == SCRIPT_FLOW_ACCESS_RANGE ||
                     capability->flow.kind == SCRIPT_FLOW_ACCESS_WHILE ||
                     capability->flow.kind == SCRIPT_FLOW_ACCESS_EACH),
            .completion = capability && capability->completion_outcome >= 0 &&
                                  capability->completion_outcome < capability->outcome_count
                            ? script_catalog_id(
                                  "outcome",
                                  capability->outcomes[capability->completion_outcome]
                              )
                        : capability || explicit_outcomes
                            ? 0
                            : script_catalog_id("outcome", "Then"),
            .outcome_count = capability        ? capability->outcome_count
                           : explicit_outcomes ? authored_outcomes
                                               : 1
        };
        action.result_count = initializer->result_count;
        for (int j = 0; j < action.result_count; j++) {
            pin_value value = initial(&l, initializer->results[j]);
            pin_value retained;
            const char *failure = NULL;
            if ((value.type == PIN_STRUCT || value.container != PIN_CONTAINER_SINGLE) &&
                !(initializer->results[j].flags & SCRIPT_TYPE_OPTIONAL)) {
                failure = EXECUTION_construct_call.value(&m->storage, &value, 1, &retained);
            } else if (pin_value_snapshot_to_arena(&m->storage, &retained, &value)) {
                failure = "Cannot retain action output default";
            }
            if (failure) {
                error(&l, symbol->declaration, "Cannot retain action output default");
                retained = value;
            }
            apush(action.defaults, retained);
        }
        action.outcomes = xmalloc(
            (size_t)(action.outcome_count + 1) * sizeof *action.outcomes
        );
        if (capability) {
            for (int j = 0; j < action.outcome_count; j++) {
                action.outcomes[j] = script_catalog_id("outcome", capability->outcomes[j]);
            }
        } else if (explicit_outcomes) {
            int ordinal = 0;
            for (int j = 0; j < a->symbol_count; j++) {
                if (a->symbols[j].outcome && a->symbols[j].scope == authored_scope) {
                    action.outcomes[ordinal++] = script_catalog_id(
                        "outcome",
                        a->symbols[j].name
                    );
                }
            }
        } else {
            action.outcomes[0] = action.completion;
        }
        apush(m->actions, action);
    }
    for (int i = 0; i < a->expression_count; i++) {
        const script_expression *expression = &a->expressions[i];
        if (expression->kind != SCRIPT_EXPR_CALL) {
            continue;
        }
        const script_catalog_capability
            *cap = script_catalog_capability_by_id(catalog, expression->capability);
        if (!cap || cap->state_kind == NATIVE_STATE_NONE) {
            continue;
        }
        bool constructor = false;
        for (int j = 0; j < a->symbol_count; j++) {
            constructor |= a->symbols[j].kind == SCRIPT_ACTION &&
                           a->symbols[j].initializer == i;
        }
        if (constructor) {
            continue;
        }
        uint64_t id = state_action_id(&l, expression);
        bool found = false;
        for (int j = 0; j < acount(m->actions); j++) {
            found |= m->actions[j].id == id;
        }
        if (found) {
            continue;
        }
        execution_module_action action = {
            .id = id,
            .capability = cap->id,
            .state_kind = cap->state_kind,
            .outcome_count = cap->outcome_count,
            .result_count = expression->result_count,
            .completion = cap->completion_outcome >= 0
                            ? script_catalog_id("outcome", cap->outcomes[cap->completion_outcome])
                            : 0
        };
        for (int j = 0; j < action.result_count; j++) {
            apush(action.defaults, initial(&l, expression->results[j]));
        }
        action.outcomes = xmalloc(
            (size_t)(action.outcome_count + 1) * sizeof *action.outcomes
        );
        for (int j = 0; j < action.outcome_count; j++) {
            action.outcomes[j] = script_catalog_id("outcome", cap->outcomes[j]);
        }
        apush(m->actions, action);
    }
    m->action_count = acount(m->actions);
    for (int i = 0; i < a->function_count; i++) {
        event_declaration(&l, a->functions[i].event);
    }
    for (int i = 0; i < a->expression_count; i++) {
        const script_expression *expression = &a->expressions[i];
        const script_catalog_capability
            *capability = script_catalog_capability_by_id(catalog, expression->capability);
        if (expression->kind == SCRIPT_EXPR_CALL && capability &&
            capability->event_sender) {
            event_declaration(&l, expression->member);
        }
    }
    for (int i = 0; i < a->function_count; i++) {
        const script_function *f = &a->functions[i];
        const script_catalog_capability
            *event = f->event ? script_catalog_capability_by_id(catalog, f->event)
                              : NULL;
        uint64_t id = function_id(a, i);
        apush(
            m->functions,
            ((execution_module_function){
                .id = id,
                .event = f->event,
                .event_abi = event ? script_catalog_capability_abi(catalog, event) : 0,
                .event_name = event ? xstrdup(event->name) : NULL,
                .action = f->action,
                .outcome = f->kind == SCRIPT_FUNCTION_CONTINUATION && f->entry
                             ? script_catalog_id("outcome", f->entry)
                             : 0,
                .parent = f->parent >= 0 ? function_id(a, f->parent) : 0,
                .capture_count = f->kind == SCRIPT_FUNCTION_CONTINUATION && f->parent >= 0
                                   ? a->functions[f->parent].parameter_count
                                   : 0,
                .continuation_wait = f->kind == SCRIPT_FUNCTION_AUTHORED,
                .entry = f->entry ? xstrdup(f->entry) : NULL,
                .kind = (execution_function_kind)f->kind,
                .effects = f->effects,
            })
        );
    }
    m->function_count = acount(m->functions);
    for (int i = 0; i < a->binding_count; i++) {
        const script_binding *b = &a->bindings[i];
        execution_module_binding out = { .read_count = b->read_count,
                                         .write_count = b->write_count };
        const script_expression *target = &a->expressions[b->target];
        out.target = target->member      ? target->member
                   : target->symbol >= 0 ? a->symbols[target->symbol].id
                                         : 0;
        int parent = b->target;
        while (parent >= 0 && a->expressions[parent].kind == SCRIPT_EXPR_MEMBER) {
            out.offset += a->expressions[parent].offset;
            parent = a->expressions[parent].left;
        }
        uint64_t component_id = 0;
        if (parent >= 0 && a->expressions[parent].kind == SCRIPT_EXPR_COMPONENT) {
            component_id = a->expressions[parent].capability;
        } else if (
            parent >= 0 && a->expressions[parent].kind == SCRIPT_EXPR_SYMBOL &&
            a->expressions[parent].symbol >= 0 &&
            a->symbols[a->expressions[parent].symbol].kind == SCRIPT_COMPONENT
        ) {
            component_id = a->symbols[a->expressions[parent].symbol].type.nominal;
        }
        out.component = component_id;
        const script_catalog_type *component_type = script_catalog_type_by_id(catalog, component_id);
        out.component = component_type ? component_type->component_id : 0;
        out.layout = component_type ? component_type->layout : 0;
        pin_value target_type = initial(&l, target->type);
        if (pin_value_snapshot_to_arena(&m->storage, &out.target_type, &target_type)) {
            error(&l, b->range, "Cannot retain binding target layout");
        }
        out.reads = xmalloc((size_t)(b->read_count + 1) * sizeof *out.reads);
        out.writes = xmalloc((size_t)(b->write_count + 1) * sizeof *out.writes);
        memcpy(out.reads, b->reads, (size_t)b->read_count * sizeof *out.reads);
        memcpy(out.writes, b->writes, (size_t)b->write_count * sizeof *out.writes);
        apush(m->bindings, out);
    }
    m->binding_count = acount(m->bindings);
    l.builders = xcalloc((size_t)(a->function_count + 1), sizeof *l.builders);
    l.function_addresses = xcalloc((size_t)(a->function_count + 1), sizeof *l.function_addresses);
    for (int i = 0; i < a->function_count; i++) {
        l.builders[i] = behavior_function_builder_new();
    }
    if (!l.failed) {
        for (int i = 0; i < a->function_count; i++) {
            if (!a->functions[i].disabled) {
                lower_function(&l, i);
            }
        }
    }
    for (int i = 0; i < a->function_count; i++) {
        if (!l.failed && !a->functions[i].disabled) {
            char message[192];
            m->functions[i].body = behavior_function_builder_finish(l.builders[i], message, sizeof message);
            if (!m->functions[i].body) {
                builder_error(&l, l.builders[i], a->functions[i].range, message);
            }
        }
    }
    for (int i = 0; i < a->function_count; i++) {
        behavior_function_builder_free(l.builders[i]);
    }
    for (int i = 0; i < a->function_count && !l.failed; i++) {
    }
    for (int i = 0; i < a->function_count && !l.failed; i++) {
        const script_function *f = &a->functions[i];
        if (f->receiver < 0 || f->disabled) {
            continue;
        }
        const script_catalog_capability *event = script_catalog_capability_by_id(catalog, f->event);
        if (event && event->component_field) {
            const script_catalog_type
                *owner = script_catalog_type_by_id(catalog, event->receiver);
            const script_catalog_field *field = NULL;
            for (int j = 0; owner && j < owner->field_count; j++) {
                if (owner->fields[j].id == event->component_field) {
                    field = &owner->fields[j];
                }
            }
            if (!owner || !owner->component_id || !field ||
                event->parameter_count != 1) {
                error(&l, f->range, "Field event lacks an authoritative component field and payload");
                break;
            }
            m->functions[i].observer = (execution_field_observer){
                .component = owner->component_id,
                .layout = owner->layout,
                .field = field->id,
                .offset = field->offset,
                .type = initial(&l, field->type),
                .argument = 0
            };
        }
        int receiver = f->receiver;
        const script_expression *receiver_expression = &a->expressions[receiver];
        script_type type = receiver_expression->type;
        bool query_component = receiver_expression->kind == SCRIPT_EXPR_SYMBOL &&
                               receiver_expression->symbol >= 0 &&
                               receiver_expression->symbol < a->symbol_count &&
                               a->symbols[receiver_expression->symbol].kind ==
                                   SCRIPT_COMPONENT;
        bool component_receiver = receiver_expression->kind == SCRIPT_EXPR_COMPONENT ||
                                  query_component;
        if (component_receiver) {
            receiver = query_component ? -1 : receiver_expression->left;
            type = (script_type){ .storage = PIN_HANDLE, .nominal = a->owner_type };
        }
        l.function = -1;
        l.builder = behavior_function_builder_new();
        l.pc = 0;
        l.locals = indices(a->symbol_count);
        l.addresses = indices(a->symbol_count);
        l.parameters = indices(a->symbol_count);
        l.results = indices(a->symbol_count);
        pin_value *vars = NULL;
        for (int j = 0; j < m->state_count; j++) {
            apush(vars, m->states[j].initial);
        }
        pin_value result = initial(&l, type);
        behavior_function_builder_signature(l.builder, NULL, 0, &result, 1, vars, acount(vars));
        afree(vars);
        int value = receiver >= 0
                      ? expression(&l, receiver)
                      : value_op(
                            &l,
                            (behavior_function_instruction){ .op = BF_OWNER,
                                                             .type = PIN_HANDLE },
                            f->range
                        );
        if (type.container != PIN_CONTAINER_SINGLE) {
            value = retain_value(&l, value, type, f->range);
            int name = value_op(
                &l,
                (behavior_function_instruction){
                    .op = BF_TYPE_NAME,
                    .type = BF_POINTER_TYPE,
                    .target = BF_CONSTANT,
                    .value = { .type = PIN_NAME, .str = result.type_name } },
                f->range
            );
            emit(
                &l,
                (behavior_function_instruction){ .op = BF_RESULT_ARRAY,
                                                 .type = type.storage,
                                                 .left = value,
                                                 .right = name,
                                                 .target = 0,
                                                 .value = result },
                f->range
            );
        } else if (type.storage == PIN_HANDLE) {
            emit(
                &l,
                (
                    behavior_function_instruction
                ){ .op = BF_RESULT, .type = PIN_HANDLE, .left = value, .target = 0 },
                f->range
            );
        } else {
            error(&l, f->range, "Subscription receiver must resolve to an entity or entity collection");
        }
        emit(&l, (behavior_function_instruction){ .op = BF_RETURN }, f->range);
        emit(&l, (behavior_function_instruction){ .op = BF_END }, f->range);
        char message[192];
        if (!l.failed) {
            m->functions[i].subscription = behavior_function_builder_finish(l.builder, message, sizeof message);
            if (!m->functions[i].subscription) {
                error(&l, f->range, message);
            }
        }
        behavior_function_builder_free(l.builder);
        free(l.locals);
        free(l.addresses);
        free(l.parameters);
        free(l.results);
    }
    for (int i = 0; i < a->binding_count && !l.failed; i++) {
        const script_binding *binding = &a->bindings[i];
        script_type target = a->expressions[binding->target].type;
        m->bindings[i]
            .body = value_evaluator(&l, &binding->value, &target, 1, binding->range);
        bool *visited = xcalloc((size_t)(a->expression_count + 1), sizeof *visited);
        int *dependencies = NULL;
        script_type *types = NULL;
        dependency_expressions(&l, binding->value, visited, &dependencies);
        memset(visited, 0, (size_t)(a->expression_count + 1) * sizeof *visited);
        binding_fields(&l, &m->bindings[i], binding->value, visited);
        free(visited);
        for (int j = 0; j < acount(dependencies); j++) {
            apush(types, a->expressions[dependencies[j]].type);
        }
        m->bindings[i].dependency_count = acount(dependencies);
        m->bindings[i].dependencies = value_evaluator(
            &l,
            dependencies,
            types,
            acount(dependencies),
            binding->range
        );
        afree(dependencies);
        afree(types);
    }
    for (int i = 0; i < a->function_count; i++) {
        free(l.function_addresses[i]);
    }
    free(l.function_addresses);
    free(l.builders);
    free(l.variables);
    afree(l.loops);
    m->diagnostic_count = acount(m->diagnostics);
    m->valid = !l.failed;
    if (!m->valid) {
        discard_bodies(m);
    }
    return m;
}
