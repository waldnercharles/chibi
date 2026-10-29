#include "language/lower.h"

#include "ecs/execution.h"
#include "execution/delay.h"
#include "execution/intrinsics.h"
#include "execution/managed.h"
#include "execution/map_intrinsics.h"
#include "execution/operation.h"
#include "execution/scalar.h"
#include "execution/state.h"
#include "execution/task.h"
#include "language/identity.h"
#include "scalar_lowering_reference.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/pointer.h"
#include "types/record.h"
#include "types/text.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int *breaks, *continues;
    int scope;
    int query_cursor; /* One-based local, closed on every function return. */
} lower_loop;
typedef struct
{
    int expression, bank;
    script_type type;
    script_range range;
} expression_temporary;
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
    pin_value *constant_values;
    int function, pc, payload_first, payload_function;
    int *locals, *variables, *parameters, *results, *labels, *addresses;
    int *goto_pc, *goto_symbol;
    lower_loop *loops;
    expression_temporary *temporaries;
    bool failed, initializing, async_emitted;
    int initializer_depth;
    int query_evaluating;
    int *query_banks;
    int *slot_schemas, *local_schemas;
    behavior_function_builder *schema_builder;
    int target_expression, target_reference, target_value;
    bool target_cached, reduction_target;
    bool argument_cached;
    int cached_argument, cached_value;
    bool pattern_cached;
    int pattern_expression, pattern_value;
    /* An element read whose reader is done with it before its array can
     * change, so the read borrows the element instead of copying it. */
    bool borrowing;
    int borrowed_expression;
    script_occurrences *occurrences; /* Made by the first debugger point. */
    bool *written; /* Per symbol, made by the first symbol_written. */
    bool *lent;    /* Per symbol, made by the first lent declaration. */
} lower;
static void schema_clear(lower *l)
{
    afree(l->slot_schemas);
    afree(l->local_schemas);
    l->schema_builder = NULL;
}
static void schema_prepare(lower *l)
{
    if (l->schema_builder != l->builder || !l->pc) {
        schema_clear(l);
        l->schema_builder = l->builder;
    }
}
static int schema_at(int *values, int index)
{
    return index >= 0 && index < acount(values) ? values[index] : -1;
}
static void schema_set(int **values, int index, int schema)
{
    while (acount(*values) <= index) {
        apush(*values, -1);
    }
    (*values)[index] = schema;
}
static int value_op(lower *, behavior_function_instruction, script_range);
static int kind_op(lower *, behavior_function_instruction, script_range, behavior_value_kind);
static int constant(lower *, pin_value, script_range);
static uint64_t document_id(const script_analysis *analysis, script_range range)
{
    const script_source_document
        *document = script_analysis_document(analysis, range.document);
    return document ? script_catalog_id("source-document", document->key) : 0;
}
static void error(lower *l, script_range range, const char *message)
{
    execution_module_diagnostic d = {
        .range = { range.start, range.end, range.line, range.column, range.document }
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
                                    origin->column,
                                    0 };
            for (int i = 0; i < l->a->document_count; i++) {
                if (!strcmp(origin->asset_name, l->a->documents[i].path)) {
                    range.document = i;
                    break;
                }
            }
        }
    }
    error(l, range, message);
}
static pin_type slot_type(script_type type)
{
    if (type.flags & SCRIPT_TYPE_BOXED_OPTIONAL) {
        return PIN_UNION;
    }
    return type.storage == PIN_STRUCT || type.container != PIN_CONTAINER_SINGLE
             ? BF_POINTER_TYPE
             : type.storage;
}
static pin_value initial(lower *l, script_type type)
{
    if (type.flags & SCRIPT_TYPE_BOXED_OPTIONAL) {
        return (pin_value){ .type = PIN_UNION };
    }
    if (type.collection) {
        const script_catalog_type *descriptor = script_catalog_type_by_id(l->catalog, type.collection);
        if (descriptor) {
            return (pin_value){ .type = type.storage,
                                .container = type.container,
                                .type_name = descriptor->name };
        }
    }
    if (type.storage == PIN_UNION) {
        return (pin_value){ .type = PIN_UNION, .container = type.container };
    }
    pin_value value = { .type = type.storage, .container = type.container };
    const script_catalog_type *nominal = script_catalog_type_by_id(l->catalog, type.nominal);
    if (nominal) {
        value.type_name = nominal->name;
        if (!nominal->authored_record) {
            for (int i = 0; i < acount(l->module->types); i++) {
                if (l->module->types[i].id == nominal->id) {
                    value.type_name = l->module->types[i].name;
                    break;
                }
            }
        }
        if (nominal->authored_record) {
            for (int i = 0; i < acount(l->module->types); i++) {
                if (l->module->types[i].id == nominal->id) {
                    value.record = l->module->types[i].record;
                    value.type_name = value.record->type.name;
                    break;
                }
            }
            assert(value.record);
        }
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
        value.record = NULL;
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
    if (type.flags & SCRIPT_TYPE_BOXED_OPTIONAL) {
        return BEHAVIOR_VALUE;
    }
    return (type.storage == PIN_STRUCT && !(type.flags & SCRIPT_TYPE_BOXED_OPTIONAL)) ||
                   type.container != PIN_CONTAINER_SINGLE
             ? BEHAVIOR_ADDRESS
             : BEHAVIOR_VALUE;
}
static void value_root(lower *l, behavior_value_bank bank, int index, script_type type)
{
    if (index >= 0 &&
        !(type.flags & (SCRIPT_TYPE_REFERENCE | SCRIPT_TYPE_RESULTS |
                        SCRIPT_TYPE_BOXED_OPTIONAL)) &&
        (type.storage == PIN_STRUCT || type.container != PIN_CONTAINER_SINGLE)) {
        behavior_function_builder_root(l->builder, bank, index, initial(l, type));
    }
}
static int emit(lower *l, behavior_function_instruction i, script_range range)
{
    schema_prepare(l);
    const script_source_document
        *document = script_analysis_document(l->a, range.document);
    i.source = behavior_function_builder_origin(
        l->builder,
        (behavior_function_source){ .asset_name = document ? document->path : l->a->name,
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
    if (i.op == BF_SAVE) {
        int source_schema = schema_at(l->slot_schemas, i.left);
        int schema_local = schema_at(l->local_schemas, i.target);
        if (source_schema >= 0 || schema_local >= 0) {
            if (schema_local < 0) {
                schema_local = behavior_function_builder_local(
                    l->builder,
                    (pin_value){ .type = BF_POINTER_TYPE }
                );
                set_kind(l, BEHAVIOR_BANK_LOCAL, schema_local, BEHAVIOR_ADDRESS);
                schema_set(&l->local_schemas, i.target, schema_local);
            }
            if (source_schema < 0) {
                source_schema = kind_op(
                    l,
                    (behavior_function_instruction){ .op = BF_CONSTANT,
                                                     .type = BF_POINTER_TYPE,
                                                     .value = { .type = BF_POINTER_TYPE } },
                    range,
                    BEHAVIOR_ADDRESS
                );
            }
            emit(
                l,
                (behavior_function_instruction){ .op = BF_SAVE,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = source_schema,
                                                 .target = schema_local },
                range
            );
        }
    }
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
    int schema = -1;
    if (i.op == BF_LOCAL) {
        int source = schema_at(l->local_schemas, i.left);
        if (source >= 0) {
            schema = value_op(
                l,
                (behavior_function_instruction){ .op = BF_LOCAL,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = source },
                range
            );
        }
    } else if (i.op == BF_ADD) {
        schema = schema_at(l->slot_schemas, i.left);
    } else if (
        (i.op == BF_ARGUMENT || i.op == BF_ARRAY) &&
        i.value.type == PIN_STRUCT && i.value.container == PIN_CONTAINER_SINGLE &&
        (i.op == BF_ARGUMENT || i.target == BF_ARGUMENT || i.target == BF_VARIABLE)
    ) {
        size_t offset = i.op == BF_ARRAY && i.target == BF_VARIABLE
                          ? offsetof(behavior_function_context, variables)
                          : offsetof(behavior_function_context, arguments);
        int bank = kind_op(
            l,
            (behavior_function_instruction){ .op = BF_CONTEXT,
                                             .type = BF_POINTER_TYPE,
                                             .offset = offset },
            range,
            BEHAVIOR_ADDRESS
        );
        int header = value_op(
            l,
            (behavior_function_instruction){
                .op = BF_INDEX,
                .type = BF_POINTER_TYPE,
                .left = bank,
                .right = constant(l, (pin_value){ .type = PIN_I32, .i = i.left }, range),
                .offset = sizeof(pin_value) },
            range
        );
        schema = kind_op(
            l,
            (behavior_function_instruction){ .op = BF_LOAD,
                                             .type = BF_POINTER_TYPE,
                                             .left = header,
                                             .offset = offsetof(pin_value, schema) },
            range,
            BEHAVIOR_ADDRESS
        );
    }
    if (schema >= 0) {
        schema_set(&l->slot_schemas, i.output, schema);
    }
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
static int coalesce(lower *, const script_expression *);
static int present(lower *, int, script_type, script_range);
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
static int retain_bank(lower *, int, script_type, script_range);
static int copy_value(lower *, int, script_type, script_range);
static void drop_value(lower *, int, script_type, script_range);
static int reference(lower *, int);
static int reference_expression(lower *, const script_expression *, bool written);
static int reference_read(lower *, int descriptor, script_type, script_range);
static int local_address(lower *, int);
static int managed_address(lower *, const script_expression *);

static void statement(lower *, int);
static void forget_expression(lower *, int);
static void forget_temporaries(lower *);
static void remember_conversion(lower *, int, script_type, script_range);
static void forget_conversions(lower *, int);
static bool command_tuple(lower *, const script_expression *);
static int command_result(lower *, int, script_type, script_range);
static void assign(lower *, const script_statement *);
static void assign_value(lower *, const script_statement *, int);
static void store_symbol(lower *, int, int, script_range);
/* How a store treats its value: it copies it, takes over a temporary, or
 * reads a place in place and owns nothing. */
typedef enum
{
    STORE_COPY,
    STORE_MOVE,
    STORE_LEND
} store_mode;
static void store_value(lower *, int, int, store_mode, script_range);
static bool symbol_written(lower *, int);
static int query_context(lower *, script_range);
static int query_not(lower *, int, script_range);
static int union_wrap(lower *, int, script_type, script_type, uint64_t, script_range);
static int convert_mode(
    lower *l,
    int slot,
    script_type from,
    script_type to,
    behavior_cast_mode mode,
    script_range range
);
static int convert(lower *l, int slot, script_type from, script_type to, script_range range)
{
    return convert_mode(l, slot, from, to, BEHAVIOR_CAST_EXACT, range);
}
/* Fails unless the integer is a Unicode scalar, 0 through 0x10FFFF outside
 * the surrogates. As a uint64 a negative value lies above the range, and the
 * surrogates are the one 0x800 block starting at 0xD800. */
static void char_check(lower *l, int slot, pin_type storage, script_range range)
{
    static const char message[] = "Value does not fit in char";
    int wide = storage == PIN_U64
                 ? slot
                 : value_op(
                       l,
                       (behavior_function_instruction){ .op = BF_CAST,
                                                        .type = PIN_U64,
                                                        .left = slot,
                                                        .target = BEHAVIOR_CAST_WRAP },
                       range
                   );
    int high = constant(l, (pin_value){ .type = PIN_U64, .u64 = 0x10FFFF }, range);
    int in_range = value_op(
        l,
        (
            behavior_function_instruction
        ){ .op = BF_LE, .type = PIN_U64, .left = wide, .right = high },
        range
    );
    emit(
        l,
        (behavior_function_instruction){ .op = BF_ASSERT,
                                         .left = in_range,
                                         .target = BEHAVIOR_FUNCTION_DATA,
                                         .message = message },
        range
    );
    int mask = constant(l, (pin_value){ .type = PIN_U64, .u64 = ~(uint64_t)0x7FF }, range);
    int block = value_op(
        l,
        (
            behavior_function_instruction
        ){ .op = BF_BIT_AND, .type = PIN_U64, .left = wide, .right = mask },
        range
    );
    int surrogates = constant(l, (pin_value){ .type = PIN_U64, .u64 = 0xD800 }, range);
    int scalar = value_op(
        l,
        (
            behavior_function_instruction
        ){ .op = BF_NE, .type = PIN_U64, .left = block, .right = surrogates },
        range
    );
    emit(
        l,
        (behavior_function_instruction){ .op = BF_ASSERT,
                                         .left = scalar,
                                         .target = BEHAVIOR_FUNCTION_DATA,
                                         .message = message },
        range
    );
}
/* Implicit conversions are lossless; explicit T(value) carries its mode. */
static int convert_mode(lower *l, int slot, script_type from, script_type to, behavior_cast_mode mode, script_range range)
{
    if (script_type_char(to) && !script_type_char(from) && from.storage >= PIN_I8 &&
        from.storage <= PIN_U64 && !(from.flags & SCRIPT_TYPE_UNTYPED)) {
        char_check(l, slot, from.storage, range);
        if (slot_type(from) == PIN_I32) {
            return slot;
        }
        return value_op(
            l,
            (behavior_function_instruction){ .op = BF_CAST,
                                             .type = PIN_I32,
                                             .left = slot,
                                             .target = BEHAVIOR_CAST_WRAP },
            range
        );
    }
    if (((to.flags & SCRIPT_TYPE_UNION) && to.container == PIN_CONTAINER_SINGLE &&
         !(to.flags & SCRIPT_TYPE_BOXED_OPTIONAL) &&
         ((from.flags & SCRIPT_TYPE_BOXED_OPTIONAL) ||
          from.container != PIN_CONTAINER_SINGLE)) ||
        (((to.flags & SCRIPT_TYPE_BOXED_OPTIONAL) ||
          ((to.flags & SCRIPT_TYPE_UNION) && to.container == PIN_CONTAINER_SINGLE)) &&
         (from.storage != PIN_UNION || from.container != PIN_CONTAINER_SINGLE) &&
         !(from.flags & SCRIPT_TYPE_BOXED_OPTIONAL))) {
        return union_wrap(l, slot, from, to, script_type_tag(from), range);
    }
    if (slot_type(from) == slot_type(to)) {
        return slot;
    }
    if (from.container == PIN_CONTAINER_SINGLE && to.container == PIN_CONTAINER_SINGLE &&
        behavior_scalar_string(from.storage) && behavior_scalar_string(to.storage)) {
        pin_value input = initial(l, from), header = initial(l, to);
        int host = behavior_function_builder_host(
            l->builder,
            &EXECUTION_snapshot_call,
            &EXECUTION_value_access,
            &input,
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
        emit(
            l,
            (behavior_function_instruction){ .op = BF_HOST_CALL,
                                             .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                             .target = host },
            range
        );
        int result = value_op(
            l,
            (behavior_function_instruction){
                .op = BF_HOST_SLOT,
                .type = BF_POINTER_TYPE,
                .left = behavior_function_builder_host_results(l->builder, host) },
            range
        );
        return value_op(
            l,
            (behavior_function_instruction){ .op = BF_LOAD,
                                             .type = to.storage,
                                             .left = result,
                                             .offset = offsetof(pin_value, d) },
            range
        );
    }
    bool float_conversion = (from.storage == PIN_F32 && to.storage == PIN_F64) ||
                            (from.storage == PIN_F64 && to.storage == PIN_F32);
    if (float_conversion) {
        return value_op(
            l,
            (behavior_function_instruction){ .op = BF_CONVERT,
                                             .type = to.storage,
                                             .left = slot },
            range
        );
    }
    /* Integers reach float through double, which holds every int32 exactly. */
    if (to.storage == PIN_F32 && behavior_scalar_cast_supported(from.storage, PIN_F64)) {
        script_type wide = { .storage = PIN_F64 };
        return convert(l, convert(l, slot, from, wide, range), wide, to, range);
    }
    pin_type source = slot_type(from), target = slot_type(to);
    if (!behavior_scalar_cast_supported(source, target)) {
        char message[128];
        snprintf(
            message,
            sizeof message,
            "Cannot convert %s to %s",
            script_scalar_name(from.storage),
            script_scalar_name(to.storage)
        );
        error(l, range, message);
        return -1;
    }
    return value_op(
        l,
        (
            behavior_function_instruction
        ){ .op = BF_CAST, .type = target, .left = slot, .target = mode },
        range
    );
}
/* The source text of a range in its own document. */
static const char *range_text(const lower *l, script_range range)
{
    return l->a->document_count ? l->a->documents[range.document].text : l->a->text;
}
/* assert fails the call with its message or, as Odin's #caller_expression,
 * its condition's source text. */
static int assertion(lower *l, const script_expression *e)
{
    int condition = -1;
    const char *message = NULL;
    script_range text = { 0 };
    for (int i = 0; i < e->argument_count; i++) {
        const script_expression *value = &l->a->expressions[e->arguments[i].value];
        if (e->arguments[i].parameter == 0) {
            condition = expression(l, e->arguments[i].value);
            text = value->range;
            continue;
        }
        if (value->kind != SCRIPT_EXPR_CONSTANT) {
            error(l, value->range, "assert's message is a string constant");
            return -1;
        }
        pin_string literal = pin_text(value->constant);
        char *display = arena_alloc(&l->module->storage, (size_t)literal.length * 2 + 1);
        size_t used = 0;
        for (uint32_t at = 0; at < literal.length; at++) {
            if (!literal.bytes[at]) {
                display[used++] = '\\';
                display[used++] = '0';
            } else {
                display[used++] = literal.bytes[at];
            }
        }
        display[used] = 0;
        message = display;
    }
    if (condition < 0) {
        return -1;
    }
    bool own = !message || !*message;
    int length = own ? text.end - text.start : (int)strlen(message);
    size_t size = sizeof "Assertion failed: " + (size_t)length;
    char *failure = arena_alloc(&l->module->storage, size);
    snprintf(failure, size, "Assertion failed: %.*s", length, own ? range_text(l, text) + text.start : message);
    emit(
        l,
        (behavior_function_instruction){ .op = BF_ASSERT,
                                         .left = condition,
                                         .target = BEHAVIOR_FUNCTION_DATA,
                                         .message = failure },
        e->range
    );
    return -1;
}
/* An array index or a range bound as an int: a narrower integer such as a
 * byte widens. A floating value needs int(x) before it gets here. */
static int int_slot(lower *l, int value, script_range range)
{
    script_type from = l->a->expressions[value].type;
    int slot = expression(l, value);
    if (slot_type(from) == PIN_I32) {
        return slot;
    }
    return convert(l, slot, from, (script_type){ .storage = PIN_I32 }, range);
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
static int primitive(
    lower *,
    const node_host_call *,
    const node_access *,
    const pin_value *,
    const int *,
    int,
    script_type,
    script_range
);
static int binary(lower *l, script_operator op, int left, int right, script_type type, script_type operand, script_range range)
{
    behavior_function_op code = binary_op(op);
    if (op == SCRIPT_OP_MOD && (type.storage == PIN_F32 || type.storage == PIN_F64)) {
        code = type.storage == PIN_F32 ? BF_MOD_F32 : BF_MOD_F64;
    }
    if (code == BF_END) {
        error(l, range, "Execution ABI lacks the requested bitwise operation");
        return -1;
    }
    const script_catalog_type
        *nominal = script_catalog_type_by_id(l->catalog, operand.nominal);
    if ((op == SCRIPT_OP_EQ || op == SCRIPT_OP_NE) &&
        (operand.container == PIN_CONTAINER_MAP ||
         (operand.storage == PIN_POINTER && operand.container == PIN_CONTAINER_SINGLE) ||
         (operand.container == PIN_CONTAINER_SINGLE && nominal &&
          nominal->authored_record))) {
        pin_value headers[] = { initial(l, operand), initial(l, operand) };
        int slots[] = { left, right };
        int equal = primitive(
            l,
            &EXECUTION_value_equal_call,
            &EXECUTION_value_access,
            headers,
            slots,
            2,
            (script_type){ .storage = PIN_BOOL },
            range
        );
        return op == SCRIPT_OP_EQ
                 ? equal
                 : binary(
                       l,
                       SCRIPT_OP_EQ,
                       equal,
                       constant(l, (pin_value){ .type = PIN_BOOL }, range),
                       (script_type){ .storage = PIN_BOOL },
                       (script_type){ .storage = PIN_BOOL },
                       range
                   );
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
    bool comparison = code >= BF_LT && code <= BF_NE;
    int result = value_op(
        l,
        (behavior_function_instruction){ .op = code,
                                         .type = comparison ? slot_type(operand)
                                                            : slot_type(type),
                                         .left = left,
                                         .right = right,
                                         .target = -1 },
        range
    );
    if (script_type_char(type) && !comparison) {
        char_check(l, result, PIN_I32, range);
    }
    return result;
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
    if (type.storage == PIN_STRUCT && type.container == PIN_CONTAINER_SINGLE) {
        int schema = schema_at(l->slot_schemas, value);
        if (schema < 0) {
            schema = kind_op(
                l,
                (behavior_function_instruction){ .op = BF_CONSTANT,
                                                 .type = BF_POINTER_TYPE,
                                                 .value = { .type = BF_POINTER_TYPE } },
                range,
                BEHAVIOR_ADDRESS
            );
        }
        emit(
            l,
            (behavior_function_instruction){ .op = BF_WRITE,
                                             .type = BF_POINTER_TYPE,
                                             .left = schema,
                                             .right = address,
                                             .offset = offsetof(pin_value, schema) },
            range
        );
    }
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
    int value = kind_op(
        l,
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = slot_type(type),
                                         .left = address,
                                         .offset = offsetof(pin_value, d) },
        range,
        payload_kind(type) == BEHAVIOR_ADDRESS ? BEHAVIOR_ADDRESS
                                               : bank_kind(l, BEHAVIOR_BANK_DATA, bank)
    );
    if (type.storage == PIN_STRUCT && type.container == PIN_CONTAINER_SINGLE) {
        int schema = kind_op(
            l,
            (behavior_function_instruction){ .op = BF_LOAD,
                                             .type = BF_POINTER_TYPE,
                                             .left = address,
                                             .offset = offsetof(pin_value, schema) },
            range,
            BEHAVIOR_ADDRESS
        );
        schema_set(&l->slot_schemas, value, schema);
    }
    return value;
}
/* Consumed protocol banks must not retain a task after its saved value dies. */
static bool contains_handles(script_type type)
{
    return !(type.flags & SCRIPT_TYPE_REFERENCE) &&
           (type.storage == PIN_POINTER || type.storage == PIN_QUERY ||
            type.storage == PIN_TASK || type.storage == PIN_UNION ||
            type.storage == PIN_STRUCT || type.container != PIN_CONTAINER_SINGLE ||
            (type.flags & SCRIPT_TYPE_BOXED_OPTIONAL));
}
static void clear_value(lower *l, int bank, script_type type, script_range range)
{
    if (!contains_handles(type)) {
        return;
    }
    pin_type physical = slot_type(type);
    int empty = kind_op(
        l,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = physical,
                                         .value = { .type = physical } },
        range,
        payload_kind(type)
    );
    host_argument(l, bank, empty, type, range);
}
static int take_result(lower *l, int bank, script_type type, script_range range)
{
    int value = host_result(l, bank, type, range);
    value_root(l, BEHAVIOR_BANK_SLOT, value, type);
    clear_value(l, bank, type, range);
    return value;
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
                    case EXECUTION_INTRINSIC_STRING_BYTES:
                    case EXECUTION_INTRINSIC_STRING_ITEM:
                    case EXECUTION_INTRINSIC_STRING_NEXT:
                        kind = BEHAVIOR_STRING_CURSOR;
                        break;
                    case EXECUTION_INTRINSIC_REFERENCE_FIELD:
                    case EXECUTION_INTRINSIC_REFERENCE_KEY:
                    case EXECUTION_INTRINSIC_REFERENCE_READ:
                    case EXECUTION_INTRINSIC_REFERENCE_WRITE:
                    case EXECUTION_INTRINSIC_REFERENCE_REDUCE:
                    case EXECUTION_INTRINSIC_LOCAL_RELEASE:
                    case EXECUTION_INTRINSIC_ARRAY_MUTATE_PLACE:
                    case EXECUTION_INTRINSIC_REFERENCE_UNION:
                    case EXECUTION_INTRINSIC_MAP_DELETE_PLACE:
                        kind = BEHAVIOR_REFERENCE;
                        break;
                    case EXECUTION_INTRINSIC_QUERY_MAKE:
                    case EXECUTION_INTRINSIC_QUERY_BEGIN:
                    case EXECUTION_INTRINSIC_QUERY_NEXT:
                    case EXECUTION_INTRINSIC_QUERY_END:
                    case EXECUTION_INTRINSIC_QUERY_CURRENT_ROW:
                    case EXECUTION_INTRINSIC_QUERY_TEST:
                    case EXECUTION_INTRINSIC_COMPONENT_POINTER:
                    case EXECUTION_INTRINSIC_COMPONENT_SNAPSHOT:
                    case EXECUTION_INTRINSIC_REFERENCE_MAKE:
                    case EXECUTION_INTRINSIC_POINTER_MAKE:
                    case EXECUTION_INTRINSIC_ACTION_READ:
                    case EXECUTION_INTRINSIC_ACTION_COMPLETE:
                    case EXECUTION_INTRINSIC_COMPONENT_HAS:
                    case EXECUTION_INTRINSIC_QUERY_SOURCE:
                    case EXECUTION_INTRINSIC_QUERY_TARGET:
                    case EXECUTION_INTRINSIC_COMPONENT_MEMBERSHIP:
                    case EXECUTION_INTRINSIC_BREAK_REQUEST:
                    case EXECUTION_INTRINSIC_BREAK_TAKE:
                    case EXECUTION_INTRINSIC_STATE_DEMAND:
                    case EXECUTION_INTRINSIC_STATE_TRANSITION:
                    case EXECUTION_INTRINSIC_RETRIGGER_ACTION:
                    case EXECUTION_INTRINSIC_ARRAY_PUSH:
                    case EXECUTION_INTRINSIC_ARRAY_STORE:
                    case EXECUTION_INTRINSIC_ARRAY_EXTEND:
                    case EXECUTION_INTRINSIC_MAP_STORE:
                    case EXECUTION_INTRINSIC_MAP_DELETE:
                        kind = BEHAVIOR_ADDRESS;
                        break;
                    default: break;
                }
            }
            if ((intrinsic == EXECUTION_INTRINSIC_QUERY_NEXT ||
                 intrinsic == EXECUTION_INTRINSIC_QUERY_END) &&
                i == 1) {
                kind = BEHAVIOR_QUERY_CURSOR;
            }
            if (intrinsic == EXECUTION_INTRINSIC_REFERENCE_MAKE && i == 4) {
                kind = BEHAVIOR_ADDRESS;
            }
            bool set = behavior_function_builder_kind(builder, BEHAVIOR_BANK_DATA, first + i, kind);
            assert(set);
            (void)set;
        }
        if (intrinsic == EXECUTION_INTRINSIC_QUERY_BEGIN) {
            bool set = behavior_function_builder_kind(
                builder,
                BEHAVIOR_BANK_DATA,
                behavior_function_builder_host_results(builder, index),
                BEHAVIOR_QUERY_CURSOR
            );
            assert(set);
            (void)set;
        }
        if (intrinsic == EXECUTION_INTRINSIC_STRING_CURSOR) {
            bool set = behavior_function_builder_kind(
                builder,
                BEHAVIOR_BANK_DATA,
                behavior_function_builder_host_results(builder, index),
                BEHAVIOR_STRING_CURSOR
            );
            assert(set);
            (void)set;
        }
        if (intrinsic == EXECUTION_INTRINSIC_REFERENCE_MAKE ||
            intrinsic == EXECUTION_INTRINSIC_REFERENCE_UNION ||
            intrinsic == EXECUTION_INTRINSIC_POINTER_REFERENCE ||
            intrinsic == EXECUTION_INTRINSIC_REFERENCE_FIELD ||
            intrinsic == EXECUTION_INTRINSIC_REFERENCE_KEY) {
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
/* A host call that reports `failure` when it fails. */
static int failing_primitive(
    lower *l,
    const node_host_call *call,
    const node_access *access,
    const pin_value *headers,
    const int *slots,
    int count,
    script_type result,
    behavior_function_failure failure,
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
    emit(l, (behavior_function_instruction){ .op = BF_HOST_CALL, .left = failure, .target = host }, range);
    int value = take_result(l, behavior_function_builder_host_results(l->builder, host), result, range);
    for (int i = 0; i < count; i++) {
        if (slots[i] >= 0) {
            clear_value(
                l,
                first + i,
                (script_type){ .storage = headers[i].type,
                               .container = headers[i].container },
                range
            );
        }
    }
    return value;
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
    return failing_primitive(l, call, access, headers, slots, count, result, BEHAVIOR_FUNCTION_HOST_DATA, range);
}
static int union_wrap(lower *l, int value, script_type from, script_type to, uint64_t tag, script_range range)
{
    const script_catalog_type *target = script_catalog_type_by_id(l->catalog, to.nominal);
    for (int i = 0; target && i < target->variant_count; i++) {
        const script_catalog_variant *v = &target->variants[i];
        if (!v->tagged && script_type_equal(v->type, from) &&
            (!v->is_literal || v->type.parameter == from.parameter)) {
            tag = v->tag;
            if (v->is_literal) {
                break;
            }
        }
    }
    pin_value headers[] = { { .type = PIN_U64, .u64 = tag }, initial(l, from) };
    int slots[] = { -1, value };
    int result = primitive(l, &EXECUTION_union_make_call, &EXECUTION_value_access, headers, slots, 2, to, range);
    remember_conversion(l, result, to, range);
    return result;
}
static int union_get(lower *l, int value, uint64_t tag, int field, bool whole, script_type result, script_range range)
{
    pin_value headers[] = { { .type = PIN_UNION },
                            { .type = PIN_U64, .u64 = tag },
                            { .type = PIN_I32, .i = field },
                            { .type = PIN_BOOL, .b = whole } };
    int slots[] = { value, -1, -1, -1 };
    return primitive(l, &EXECUTION_union_get_call, &EXECUTION_value_access, headers, slots, 4, result, range);
}
/* The value an optional or a union of source type holds as type. */
static int unwrap(lower *l, int value, script_type source, script_type type, uint64_t tag, int field, script_range range)
{
    bool nested_optional = (source.flags & SCRIPT_TYPE_UNION) &&
                           !(source.flags & SCRIPT_TYPE_BOXED_OPTIONAL) &&
                           (type.flags & SCRIPT_TYPE_BOXED_OPTIONAL);
    bool whole = type.storage == PIN_UNION &&
                 type.container == PIN_CONTAINER_SINGLE && !nested_optional;
    return union_get(l, value, tag, field, whole, type, range);
}
static int union_construct(lower *l, const script_expression *e)
{
    pin_value *headers = NULL;
    int *slots = NULL;
    apush(headers, ((pin_value){ .type = PIN_U64, .u64 = e->tag }));
    apush(slots, -1);
    for (int i = 0; i < e->result_count; i++) {
        apush(headers, initial(l, e->results[i]));
        apush(slots, -1);
    }
    for (int i = 0; i < e->argument_count; i++) {
        const script_argument *argument = &e->arguments[i];
        int value = expression(l, argument->value);
        slots[argument->parameter + 1] = convert(
            l,
            value,
            l->a->expressions[argument->value].type,
            e->results[argument->parameter],
            argument->range
        );
        slots[argument->parameter + 1] = copy_value(
            l,
            slots[argument->parameter + 1],
            e->results[argument->parameter],
            argument->range
        );
    }
    int value = primitive(
        l,
        &EXECUTION_union_make_call,
        &EXECUTION_value_access,
        headers,
        slots,
        acount(headers),
        e->type,
        e->range
    );
    for (int i = 1; i < acount(slots); i++) {
        drop_value(l, slots[i], e->results[i - 1], e->range);
    }
    afree(headers);
    afree(slots);
    return value;
}
static int union_pattern(lower *l, const script_expression *e)
{
    script_type source = l->a->expressions[e->left].type;
    int value = expression(l, e->left);
    bool tagged = (source.storage == PIN_UNION &&
                   source.container == PIN_CONTAINER_SINGLE) ||
                  (source.flags & SCRIPT_TYPE_BOXED_OPTIONAL);
    bool optional_value = (source.flags & SCRIPT_TYPE_BOXED_OPTIONAL) &&
                          (!(source.flags & SCRIPT_TYPE_UNION) ||
                           source.container != PIN_CONTAINER_SINGLE);
    if (e->kind == SCRIPT_EXPR_AS) {
        if (optional_value && e->tag) {
            return value;
        }
        if (tagged) {
            if (!e->tag) {
                return constant(l, (pin_value){ .type = PIN_UNION }, e->range);
            }
            pin_value *headers = NULL;
            int *slots = NULL;
            apush(headers, ((pin_value){ .type = PIN_UNION }));
            apush(headers, ((pin_value){ .type = PIN_U64, .u64 = e->tag }));
            apush(headers, ((pin_value){ .type = PIN_I32 }));
            apush(headers, ((pin_value){ .type = PIN_BOOL, .b = true }));
            apush(slots, value);
            for (int i = 1; i < 4; i++) {
                apush(slots, -1);
            }
            for (int i = 1; i < e->pattern_tag_count; i++) {
                apush(headers, ((pin_value){ .type = PIN_U64, .u64 = e->pattern_tags[i] }));
                apush(slots, -1);
            }
            int extracted = primitive(
                l,
                &EXECUTION_union_get_call,
                &EXECUTION_value_access,
                headers,
                slots,
                acount(headers),
                e->type,
                e->range
            );
            afree(headers);
            afree(slots);
            return extracted;
        }
        return e->tag ? union_wrap(l, value, source, e->type, e->tag, e->range)
                      : constant(l, (pin_value){ .type = PIN_UNION }, e->range);
    }
    int condition;
    if (optional_value && e->tag) {
        int empty = constant(l, (pin_value){ .type = PIN_UNION }, e->range);
        condition = value_op(
            l,
            (
                behavior_function_instruction
            ){ .op = BF_NE, .type = PIN_UNION, .left = value, .right = empty },
            e->range
        );
    } else if (tagged) {
        pin_value *headers = NULL;
        int *slots = NULL;
        apush(headers, ((pin_value){ .type = PIN_UNION }));
        apush(slots, value);
        apush(headers, ((pin_value){ .type = PIN_U64, .u64 = e->tag }));
        apush(slots, -1);
        for (int i = 1; i < e->pattern_tag_count; i++) {
            apush(headers, ((pin_value){ .type = PIN_U64, .u64 = e->pattern_tags[i] }));
            apush(slots, -1);
        }
        condition = primitive(
            l,
            &EXECUTION_union_test_call,
            &EXECUTION_value_access,
            headers,
            slots,
            acount(headers),
            (script_type){ .storage = PIN_BOOL },
            e->range
        );
        afree(headers);
        afree(slots);
    } else {
        condition = constant(l, (pin_value){ .type = PIN_BOOL, .b = e->tag != 0 }, e->range);
    }
    if (e->symbol >= 0) {
        script_type binding = l->a->symbols[e->symbol].type;
        int extracted = tagged
                          ? union_get(
                                l,
                                value,
                                optional_value || e->pattern_tag_count > 1 ? 0 : e->tag,
                                0,
                                binding.storage == PIN_UNION &&
                                    binding.container == PIN_CONTAINER_SINGLE &&
                                    !(binding.flags & SCRIPT_TYPE_BOXED_OPTIONAL),
                                binding,
                                e->range
                            )
                          : value;
        store_symbol(l, e->symbol, extracted, e->range);
    }
    for (int i = 0; i < e->argument_count; i++) {
        const script_argument *binding = &e->arguments[i];
        script_type type = l->a->symbols[binding->parameter].type;
        int extracted = union_get(
            l,
            value,
            e->tag,
            (int)binding->member,
            false,
            type,
            binding->range
        );
        store_symbol(l, binding->parameter, extracted, binding->range);
    }
    if (e->negated) {
        int zero = constant(l, (pin_value){ .type = PIN_BOOL }, e->range);
        condition = value_op(
            l,
            (
                behavior_function_instruction
            ){ .op = BF_EQ, .type = PIN_BOOL, .left = condition, .right = zero },
            e->range
        );
    }
    return condition;
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
static script_type managed_type(lower *l, script_type pointee)
{
    for (int i = 0; i < l->catalog->type_count; i++) {
        const script_catalog_type *type = &l->catalog->types[i];
        if (type->type.storage == PIN_POINTER &&
            type->type.container == PIN_CONTAINER_SINGLE &&
            script_type_equal(type->element_type, pointee)) {
            return type->type;
        }
    }
    assert(!"Managed pointee type was not published");
    return (script_type){ .flags = SCRIPT_TYPE_ERROR };
}
/* Whether e names an addressed local, which lives in a managed cell. */
static bool managed_symbol(lower *l, const script_expression *e)
{
    return e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0 &&
           l->a->symbols[e->symbol].addressed &&
           (l->parameters[e->symbol] < 0 ||
            (l->captured && l->captured[e->symbol] >= 0));
}
static int managed_read(lower *l, int pointer, script_type pointee, script_range range)
{
    const script_catalog_type
        *component = script_catalog_type_by_id(l->catalog, pointee.nominal);
    if (component && component->component) {
        behavior_function_builder_component(l->builder, component->component_id, false, false);
    }
    pin_value headers[] = { initial(l, managed_type(l, pointee)), initial(l, pointee) };
    int slots[] = { pointer, -1 };
    return primitive(
        l,
        &EXECUTION_pointer_read_call,
        &EXECUTION_assignment_access,
        headers,
        slots,
        2,
        pointee,
        range
    );
}
static void managed_write(lower *l, int pointer, int value, script_type pointee, script_range range)
{
    pin_value headers[] = { initial(l, managed_type(l, pointee)), initial(l, pointee) };
    int slots[] = { pointer, value };
    primitive(
        l,
        &EXECUTION_pointer_write_call,
        &EXECUTION_assignment_access,
        headers,
        slots,
        2,
        (script_type){ .storage = PIN_BOOL },
        range
    );
}
static int managed_local(lower *l, int symbol, script_range range)
{
    if (l->captured && l->captured[symbol] >= 0) {
        int descriptor = value_op(
            l,
            (behavior_function_instruction){ .op = BF_ARGUMENT,
                                             .type = BF_POINTER_TYPE,
                                             .left = l->parameters[symbol],
                                             .value = { .type = BF_POINTER_TYPE } },
            range
        );
        return reference_read(
            l,
            descriptor,
            managed_type(l, l->a->symbols[symbol].type),
            range
        );
    }
    return host_result(
        l,
        local_address(l, symbol),
        managed_type(l, l->a->symbols[symbol].type),
        range
    );
}
static int managed_reference(lower *l, int pointer, script_type pointee, script_range range)
{
    pin_value header = initial(l, managed_type(l, pointee));
    return primitive(
        l,
        &EXECUTION_pointer_reference_call,
        &EXECUTION_value_access,
        &header,
        &pointer,
        1,
        (script_type){ .storage = BF_POINTER_TYPE },
        range
    );
}
static int managed_address(lower *l, const script_expression *e)
{
    if (e->kind == SCRIPT_EXPR_DEREFERENCE) {
        return expression(l, e->left);
    }
    if (e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0 &&
        l->a->symbols[e->symbol].addressed) {
        return managed_local(l, e->symbol, e->range);
    }
    if (e->kind == SCRIPT_EXPR_MEMBER) {
        const script_expression *parent = &l->a->expressions[e->left];
        pin_value headers[] = { initial(l, managed_type(l, parent->type)),
                                initial(l, e->type),
                                { .type = PIN_U64, .u64 = e->offset },
                                { .type = PIN_BOOL },
                                initial(l, parent->type) };
        int slots[] = { managed_address(l, parent), -1, -1, -1, -1 };
        return primitive(
            l,
            &EXECUTION_pointer_field_call,
            &EXECUTION_value_access,
            headers,
            slots,
            5,
            managed_type(l, e->type),
            e->range
        );
    }
    error(l, e->range, "Managed pointer requires a writable local or record field");
    return -1;
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
/* Native char fields hold C pointers; reference reads adapt them to strings. */
static int native_text_field(
    lower *l,
    int source,
    script_type owner,
    const script_catalog_field *field,
    script_range range
)
{
    int bank = retain_bank(l, source, owner, range);
    pin_value headers[] = { { .type = BF_POINTER_TYPE },
                            initial(l, field->type),
                            { .type = PIN_U64, .u64 = field->offset },
                            { .type = PIN_BOOL } };
    int slots[] = { bank_reference(l, bank, owner, range), -1, -1, -1 };
    int reference = primitive(
        l,
        &EXECUTION_reference_field_call,
        &EXECUTION_value_access,
        headers,
        slots,
        4,
        (script_type){ .storage = BF_POINTER_TYPE },
        range
    );
    return reference_read(l, reference, field->type, range);
}
/* Reads an assignment's target; an absent map key reads its value type's zero. */
static int target_read(lower *l, int descriptor, int index, script_range range)
{
    const script_expression *e = &l->a->expressions[index];
    if (e->kind != SCRIPT_EXPR_INDEX || !e->argument_count) {
        return reference_read(l, descriptor, e->type, range);
    }
    int absent = e->arguments[0].value;
    pin_value headers[] = { { .type = BF_POINTER_TYPE }, initial(l, e->type) };
    int slots[] = {
        descriptor,
        convert(l, expression(l, absent), l->a->expressions[absent].type, e->type, range)
    };
    return primitive(
        l,
        &EXECUTION_reference_read_call,
        &EXECUTION_assignment_access,
        headers,
        slots,
        2,
        e->type,
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
static int retain_bank(lower *l, int slot, script_type type, script_range range)
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
    clear_value(l, first, type, range);
    return behavior_function_builder_host_results(l->builder, host);
}
static int retain_value(lower *l, int slot, script_type type, script_range range)
{
    return take_result(l, retain_bank(l, slot, type, range), type, range);
}
static bool copied_value(script_type type)
{
    return !(type.flags & (SCRIPT_TYPE_REFERENCE | SCRIPT_TYPE_RESULTS)) &&
           (type.container != PIN_CONTAINER_SINGLE || type.storage == PIN_STRUCT ||
            type.storage == PIN_UNION || (type.flags & SCRIPT_TYPE_BOXED_OPTIONAL));
}
static int copy_value(lower *l, int value, script_type type, script_range range)
{
    if (!copied_value(type)) {
        return value;
    }
    pin_value header = initial(l, type);
    return primitive(
        l,
        &EXECUTION_value_copy_call,
        &EXECUTION_sequence_read_access,
        &header,
        &value,
        1,
        type,
        range
    );
}
static void drop_value(lower *l, int value, script_type type, script_range range)
{
    if (value < 0 || !copied_value(type)) {
        return;
    }
    pin_value header = initial(l, type);
    primitive(
        l,
        &EXECUTION_value_drop_call,
        &EXECUTION_assignment_access,
        &header,
        &value,
        1,
        (script_type){ .storage = PIN_BOOL },
        range
    );
}
/* Whether evaluating expression `index` may change an array read before it. */
static bool may_change(const lower *l, int index)
{
    return l->a->expressions[index].effects &
           (SCRIPT_EFFECT_WRITE | SCRIPT_EFFECT_HOST |
            SCRIPT_EFFECT_STRUCTURAL | SCRIPT_EFFECT_SUSPEND);
}
static bool index_needs_snapshot(const lower *l, const script_expression *e)
{
    return e->kind == SCRIPT_EXPR_INDEX && may_change(l, e->right);
}
static bool owns_expression_value(const lower *l, const script_expression *e)
{
    switch (e->kind) {
        case SCRIPT_EXPR_CALL:
        case SCRIPT_EXPR_ARRAY:
        case SCRIPT_EXPR_MAP:
        case SCRIPT_EXPR_STRUCT:
        case SCRIPT_EXPR_UNION:
        case SCRIPT_EXPR_MAP_REMOVE: return true;
        case SCRIPT_EXPR_INDEX: return index_needs_snapshot(l, e);
        case SCRIPT_EXPR_SEQUENCE: return e->member != SCRIPT_SEQUENCE_LENGTH;
        default: return false;
    }
}
static void remember_expression(lower *l, int index, int value)
{
    const script_expression *e = &l->a->expressions[index];
    bool tuple = (e->type.flags & SCRIPT_TYPE_RESULTS) != 0;
    if (!owns_expression_value(l, e) || (!tuple && !copied_value(e->type))) {
        return;
    }
    if (tuple) {
        bool managed = false;
        for (int i = 0; i < e->result_count; i++) {
            managed |= copied_value(e->results[i]);
        }
        if (!managed) {
            return;
        }
    }
    int bank = -1;
    for (int i = 0; i < acount(l->temporaries); i++) {
        if (l->temporaries[i].expression == index) {
            bank = l->temporaries[i].bank;
            break;
        }
    }
    int count = tuple ? e->result_count : 1;
    if (bank < 0) {
        pin_value *headers = NULL;
        for (int i = 0; i < count; i++) {
            apush(headers, initial(l, tuple ? e->results[i] : e->type));
        }
        bank = behavior_function_builder_values(l->builder, headers, count);
        afree(headers);
        apush(l->temporaries, ((expression_temporary){ .expression = index, .bank = bank }));
    }
    for (int i = 0; i < count; i++) {
        script_type type = tuple ? e->results[i] : e->type;
        int payload = tuple ? command_tuple(l, e)
                                ? command_result(l, value + i, type, e->range)
                                : host_result(l, value + i, type, e->range)
                            : value;
        host_argument(l, bank + i, payload, type, e->range);
    }
}
static void forget_temporary(lower *l, expression_temporary temporary)
{
    if (temporary.expression < 0) {
        drop_value(
            l,
            host_result(l, temporary.bank, temporary.type, temporary.range),
            temporary.type,
            temporary.range
        );
        clear_value(l, temporary.bank, temporary.type, temporary.range);
        return;
    }
    const script_expression *e = &l->a->expressions[temporary.expression];
    bool tuple = (e->type.flags & SCRIPT_TYPE_RESULTS) != 0;
    int count = tuple ? e->result_count : 1;
    for (int i = 0; i < count; i++) {
        script_type type = tuple ? e->results[i] : e->type;
        drop_value(l, host_result(l, temporary.bank + i, type, e->range), type, e->range);
        clear_value(l, temporary.bank + i, type, e->range);
    }
}
static void remember_conversion(lower *l, int value, script_type type, script_range range)
{
    pin_value header = initial(l, type);
    int bank = behavior_function_builder_values(l->builder, &header, 1);
    apush(
        l->temporaries,
        ((expression_temporary){ .expression = -1, .bank = bank, .type = type, .range = range })
    );
    host_argument(l, bank, value, type, range);
}
static void forget_conversions(lower *l, int first)
{
    for (int i = first; i < acount(l->temporaries); i++) {
        if (l->temporaries[i].expression < 0) {
            forget_temporary(l, l->temporaries[i]);
        }
    }
}
static void forget_expression(lower *l, int index)
{
    if (index < 0) {
        return;
    }
    for (int i = 0; i < acount(l->temporaries); i++) {
        if (l->temporaries[i].expression == index) {
            forget_temporary(l, l->temporaries[i]);
        }
    }
    const script_expression *e = &l->a->expressions[index];
    forget_expression(l, e->left);
    if (e->kind != SCRIPT_EXPR_CALL && e->kind != SCRIPT_EXPR_ARRAY_MUTATE) {
        forget_expression(l, e->right);
    }
    for (int i = 0; i < e->argument_count; i++) {
        forget_expression(l, e->arguments[i].value);
    }
}
static void forget_temporaries(lower *l)
{
    for (int i = 0; i < acount(l->temporaries); i++) {
        forget_temporary(l, l->temporaries[i]);
    }
}
/* Publish the already retained header, including the owned nominal layout.
 * Reading each field through shared IR works in VM, native, Wasm and C without
 * embedding a producer address or depending on an inspection context. */
static void return_record(lower *l, int slot, script_type type, int index, script_range range)
{
    _Static_assert(sizeof(pin_type) == sizeof(int32_t), "pin type header width");
    _Static_assert(sizeof(pin_container) == sizeof(int32_t), "pin container header width");
    int retained = retain_bank(l, slot, type, range);
    int source = host_address(l, retained, false, range);
    int results = value_op(
        l,
        (behavior_function_instruction){
            .op = BF_CONTEXT,
            .type = BF_POINTER_TYPE,
            .offset = offsetof(behavior_function_context, results) },
        range
    );
    int destination = value_op(
        l,
        (behavior_function_instruction){
            .op = BF_INDEX,
            .type = BF_POINTER_TYPE,
            .left = results,
            .right = constant(l, (pin_value){ .type = PIN_I32, .i = index }, range),
            .offset = sizeof(pin_value) },
        range
    );
    const struct
    {
        size_t offset;
        pin_type type;
    } fields[] = {
        { offsetof(pin_value, type), PIN_I32 },
        { offsetof(pin_value, container), PIN_I32 },
        { offsetof(pin_value, type_name), BF_POINTER_TYPE },
        { offsetof(pin_value, record), BF_POINTER_TYPE },
        { offsetof(pin_value, schema), BF_POINTER_TYPE },
        { offsetof(pin_value, ptr), BF_POINTER_TYPE },
    };
    for (int i = 0; i < 6; i++) {
        int value = kind_op(
            l,
            (behavior_function_instruction){ .op = BF_LOAD,
                                             .type = fields[i].type,
                                             .left = source,
                                             .offset = fields[i].offset },
            range,
            i < 2 ? BEHAVIOR_VALUE : BEHAVIOR_ADDRESS
        );
        emit(
            l,
            (behavior_function_instruction){ .op = BF_WRITE,
                                             .type = fields[i].type,
                                             .left = value,
                                             .right = destination,
                                             .offset = fields[i].offset },
            range
        );
    }
    clear_value(l, retained, type, range);
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
        script_type expected = script_catalog_element_type(l->catalog, e->type);
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
    int first = behavior_function_builder_host_first(l->builder, host), cursor = 1;
    if (base >= 0) {
        host_argument(l, first, base, e->type, e->range);
    }
    for (int i = 0; i < e->argument_count; i++) {
        const script_argument *a = &e->arguments[i];
        if (e->kind == SCRIPT_EXPR_STRUCT) {
            cursor++;
        }
        script_type expected = script_catalog_element_type(l->catalog, e->type);
        if (e->kind == SCRIPT_EXPR_STRUCT && type && a->parameter >= 0 &&
            a->parameter < type->field_count) {
            expected = type->fields[a->parameter].type;
        }
        int slot = expression(l, a->value);
        slot = convert(l, slot, l->a->expressions[a->value].type, expected, a->range);
        slot = copy_value(l, slot, expected, a->range);
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
    int output = take_result(
        l,
        behavior_function_builder_host_results(l->builder, host),
        e->type,
        e->range
    );
    cursor = 1;
    for (int i = 0; i < e->argument_count; i++) {
        const script_argument *argument = &e->arguments[i];
        if (e->kind == SCRIPT_EXPR_STRUCT) {
            cursor++;
        }
        script_type expected = script_catalog_element_type(l->catalog, e->type);
        if (e->kind == SCRIPT_EXPR_STRUCT && type && argument->parameter >= 0 &&
            argument->parameter < type->field_count) {
            expected = type->fields[argument->parameter].type;
        }
        drop_value(l, host_result(l, first + cursor++, expected, e->range), expected, e->range);
    }
    for (int i = 0; i < acount(values); i++) {
        clear_value(
            l,
            first + i,
            (script_type){ .storage = values[i].type, .container = values[i].container },
            e->range
        );
    }
    afree(values);
    return output;
}
/* Plain scalar and string literals that need no conversion; nominal types keep
 * the constructor. */
static bool literal_array(lower *l, const script_expression *e)
{
    pin_type storage = e->type.storage;
    if (e->kind != SCRIPT_EXPR_ARRAY || !e->argument_count || e->type.nominal ||
        storage == PIN_HANDLE) {
        return false;
    }
    for (int i = 0; i < e->argument_count; i++) {
        const script_expression *value = &l->a->expressions[e->arguments[i].value];
        if (value->kind != SCRIPT_EXPR_CONSTANT || value->constant.type != storage ||
            value->constant.container != PIN_CONTAINER_SINGLE ||
            value->type.storage != storage || value->type.nominal ||
            (value->type.flags & SCRIPT_TYPE_OPTIONAL)) {
            return false;
        }
    }
    return true;
}
/* Keep literal bytes in one constant, then create a fresh array object on
 * each evaluation. Ordinary retention shares that runtime object. */
static int array_constant(lower *l, const script_expression *e)
{
    pin_value value = initial(l, e->type);
    pin_array *items = pin_array_snapshot_allocate(&l->module->storage, e->type.storage, NULL, e->argument_count);
    for (int i = 0; i < e->argument_count; i++) {
        const pin_value *item = &l->a->expressions[e->arguments[i].value].constant;
        int result = pin_value_capture_to_arena(&l->module->storage, &items->items[i], item);
        assert(result == 0);
        (void)result;
    }
    value.ptr = items;
    int literal = value_op(
        l,
        (behavior_function_instruction){ .op = BF_ARRAY,
                                         .type = BF_POINTER_TYPE,
                                         .target = BF_CONSTANT,
                                         .value = value },
        e->range
    );
    return primitive(
        l,
        &EXECUTION_sequence_clone_call,
        &EXECUTION_sequence_read_access,
        &value,
        &literal,
        1,
        e->type,
        e->range
    );
}
static int constructor(lower *l, const script_expression *e)
{
    return literal_array(l, e) ? array_constant(l, e) : constructor_from(l, e, -1);
}
static int map_constructor(lower *l, const script_expression *e)
{
    script_type key = script_catalog_key_type(l->catalog, e->type);
    script_type value_type = script_catalog_element_type(l->catalog, e->type);
    pin_value *headers = NULL;
    int *slots = NULL;
    apush(headers, initial(l, e->type));
    apush(headers, initial(l, key));
    apush(headers, initial(l, value_type));
    apush(slots, -1);
    apush(slots, -1);
    apush(slots, -1);
    for (int i = 0; i < e->argument_count; i++) {
        int expression_index = e->arguments[i].value;
        script_type target = i % 2 ? value_type : key;
        int value = expression(l, expression_index);
        value = convert(l, value, l->a->expressions[expression_index].type, target, e->range);
        value = copy_value(l, value, target, e->range);
        apush(headers, initial(l, target));
        apush(slots, value);
    }
    int value = primitive(
        l,
        &EXECUTION_map_make_call,
        &EXECUTION_value_access,
        headers,
        slots,
        acount(headers),
        e->type,
        e->range
    );
    for (int i = 0; i < e->argument_count; i++) {
        drop_value(l, slots[i + 3], i % 2 ? value_type : key, e->range);
    }
    afree(headers);
    afree(slots);
    return value;
}
static int map_item(lower *l, int map, int ordinal, bool key, script_type collection, script_type type, script_range range)
{
    pin_value headers[] = { initial(l, collection),
                            { .type = PIN_I32 },
                            { .type = PIN_BOOL, .b = key } };
    int slots[] = { map, ordinal, -1 };
    return primitive(l, &EXECUTION_map_item_call, &EXECUTION_value_access, headers, slots, 3, type, range);
}
static int string_cursor(lower *l, const node_host_call *call, int text, int index, script_type result, script_range range)
{
    pin_value headers[] = { { .type = call == &EXECUTION_string_cursor_call
                                        ? PIN_STR
                                        : BF_POINTER_TYPE },
                            { .type = PIN_I32 } };
    int slots[] = { text, index };
    return primitive(l, call, &EXECUTION_value_access, headers, slots, index < 0 ? 1 : 2, result, range);
}
static const script_function *input_signature(const script_analysis *a, const script_function *f)
{
    return f->kind == SCRIPT_FUNCTION_MACRO_ENTRY && f->parent >= 0 && !f->parameter_count
             ? &a->functions[f->parent]
             : f;
}
static int storage_root(const script_analysis *, int);
static bool reference_argument(const lower *, const script_expression *, int);
/* Whether expression `index` changes local `symbol` in place. A call's
 * `right` is the function it calls, not an operand. */
static bool changes_local(const lower *l, int index, int symbol)
{
    if (index < 0) {
        return false;
    }
    const script_expression *e = &l->a->expressions[index];
    if ((e->kind == SCRIPT_EXPR_ARRAY_MUTATE && storage_root(l->a, e->left) == symbol) ||
        changes_local(l, e->left, symbol) ||
        (e->kind != SCRIPT_EXPR_CALL && changes_local(l, e->right, symbol))) {
        return true;
    }
    for (int i = 0; i < e->argument_count; i++) {
        if ((reference_argument(l, e, i) &&
             storage_root(l->a, e->arguments[i].value) == symbol) ||
            changes_local(l, e->arguments[i].value, symbol)) {
            return true;
        }
    }
    return false;
}
/* Whether a call lends argument `at` to its callee without a copy: nothing can
 * change the value before the call returns. The callee copies a parameter it
 * writes. A temporary lives until the statement ends, and a caller's own
 * local only changes through the caller, unless a pointer, a task, a later
 * argument or another argument's reference reaches it. State, components,
 * references and values behind a pointer stay copied. */
static bool lends_argument(const lower *l, const script_expression *call, int at)
{
    int index = call->arguments[at].value;
    while (index >= 0) {
        const script_expression *e = &l->a->expressions[index];
        if (owns_expression_value(l, e)) {
            return true;
        }
        if (e->type.storage == PIN_POINTER) {
            return false;
        }
        if (e->kind == SCRIPT_EXPR_MEMBER || e->kind == SCRIPT_EXPR_INDEX) {
            index = e->left;
            continue;
        }
        if (e->kind != SCRIPT_EXPR_SYMBOL || e->symbol < 0) {
            return false;
        }
        const script_symbol *s = &l->a->symbols[e->symbol];
        if ((s->kind != SCRIPT_LOCAL && s->kind != SCRIPT_PARAMETER &&
             s->kind != SCRIPT_RESULT) ||
            (s->type.flags & SCRIPT_TYPE_REFERENCE) || s->addressed ||
            s->boxed || l->variables[e->symbol] >= 0 ||
            (l->captured && l->captured[e->symbol] >= 0)) {
            return false;
        }
        /* A later argument may change it before the call, and the callee
         * may write through a reference any other argument passes. */
        for (int i = 0; i < call->argument_count; i++) {
            if ((i > at && changes_local(l, call->arguments[i].value, e->symbol)) ||
                (i != at && reference_argument(l, call, i) &&
                 storage_root(l->a, call->arguments[i].value) == e->symbol)) {
                return false;
            }
        }
        return true;
    }
    return false;
}
/* Argument `at` of a call, as the value its bank slot takes. A task owns a
 * copy of each argument. A call borrows what it can, and its caller keeps
 * each copy it makes until the call returns. */
static int value_argument(lower *l, const script_expression *call, int at, script_type expected)
{
    const script_argument *a = &call->arguments[at];
    int value = expression(l, a->value);
    value = convert(l, value, l->a->expressions[a->value].type, expected, a->range);
    if (call->task) {
        value = copy_value(l, value, expected, a->range);
        forget_expression(l, a->value);
        return value;
    }
    if (!lends_argument(l, call, at) && copied_value(expected)) {
        value = copy_value(l, value, expected, a->range);
        remember_conversion(l, value, expected, a->range);
    }
    return value;
}
/* Drops what a call's arguments held once a borrowing call returns:
 * temporaries from `temporaries` on and each argument's own. */
static void forget_arguments(lower *l, const script_expression *call, int temporaries)
{
    if (call->task) {
        return;
    }
    forget_conversions(l, temporaries);
    for (int i = 0; i < call->argument_count; i++) {
        forget_expression(l, call->arguments[i].value);
    }
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
    const script_function *inputs = input_signature(l->a, f);
    const script_function *output = f->kind == SCRIPT_FUNCTION_MACRO_ENTRY &&
                                            f->parent >= 0
                                      ? &l->a->functions[f->parent]
                                      : f;
    pin_value *args = NULL, *results = NULL;
    for (int i = 0; i < inputs->parameter_count; i++) {
        apush(args, signature_initial(l, l->a->symbols[inputs->parameters[i]].type));
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
    for (int i = 0; i < inputs->parameter_count; i++) {
        if (l->a->symbols[inputs->parameters[i]].type.flags & SCRIPT_TYPE_REFERENCE) {
            set_kind(l, BEHAVIOR_BANK_DATA, argument_first + i, BEHAVIOR_REFERENCE);
        }
    }
    afree(args);
    afree(results);
    if (e->task) {
        behavior_function_builder_call_owned_arguments(l->builder, invocation);
    }
    int first = behavior_function_builder_call_first(l->builder, invocation);
    int temporaries = acount(l->temporaries);
    for (int i = 0; i < e->argument_count; i++) {
        const script_argument *a = &e->arguments[i];
        if (a->parameter < 0 || a->parameter >= inputs->parameter_count) {
            error(l, a->range, "Authored call argument does not match its signature");
            continue;
        }
        script_type expected = l->a->symbols[inputs->parameters[a->parameter]].type;
        int value;
        if (expected.flags & SCRIPT_TYPE_REFERENCE) {
            value = reference(l, a->value);
            debug_input(l, e, a->parameter, value, expected);
            host_argument(l, first + a->parameter, value, (script_type){ .storage = BF_POINTER_TYPE }, a->range);
        } else {
            value = value_argument(l, e, i, expected);
            debug_input(l, e, a->parameter, value, expected);
            host_argument(l, first + a->parameter, value, expected, a->range);
        }
    }
    emit(l, (behavior_function_instruction){ .op = BF_INVOKE, .left = invocation }, e->range);
    forget_arguments(l, e, temporaries);
    for (int i = 0; i < inputs->parameter_count; i++) {
        clear_value(l, first + i, l->a->symbols[inputs->parameters[i]].type, e->range);
    }
    int result_first = behavior_function_builder_call_results(l->builder, invocation);
    if (e->task) {
        return take_result(l, result_first, e->type, e->range);
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
    return take_result(l, result_first, e->type, e->range);
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
    if (e->task) {
        behavior_function_builder_call_owned_arguments(l->builder, invocation);
    }
    int first = behavior_function_builder_call_first(l->builder, invocation);
    int temporaries = acount(l->temporaries);
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
            int value = value_argument(l, e, i, expected);
            debug_input(l, e, arg->parameter, value, expected);
            host_argument(l, first + arg->parameter, value, expected, arg->range);
        }
    }
    emit(l, (behavior_function_instruction){ .op = BF_INVOKE, .left = invocation }, e->range);
    forget_arguments(l, e, temporaries);
    for (int i = 0; i < cap->parameter_count; i++) {
        clear_value(l, first + i, cap->parameters[i].type, e->range);
    }
    int result_first = behavior_function_builder_call_results(l->builder, invocation);
    if (e->task) {
        return take_result(l, result_first, e->type, e->range);
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
    return result_count ? take_result(l, result_first, e->type, e->range) : -1;
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
/* A wildcard parameter takes its argument's type, except that the scalar
 * operands of a numeric aggregate such as clamp share the call's unified type. */
static script_type wildcard_type(
    lower *l,
    const script_expression *e,
    const script_catalog_capability *cap,
    int value
)
{
    script_type type = l->a->expressions[value].type;
    if (cap->numeric_aggregate && type.storage != PIN_STRUCT &&
        e->type.storage != PIN_STRUCT && script_type_numeric(type) &&
        script_type_numeric(e->type)) {
        type.storage = e->type.storage;
    }
    return type;
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
                    type = wildcard_type(l, e, cap, e->arguments[j].value);
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
            expected = wildcard_type(l, e, cap, arg->value);
        }
        int value = expression(l, arg->value);
        value = convert(l, value, l->a->expressions[arg->value].type, expected, arg->range);
        value = copy_value(l, value, expected, arg->range);
        forget_expression(l, arg->value);
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
/* Validate native char outputs before any result or action can expose them. */
static void check_native_results(
    lower *l,
    const script_expression *e,
    const script_catalog_capability *cap,
    int first
)
{
    for (int i = 0; i < cap->result_count; i++) {
        script_type type = i < e->result_count ? e->results[i] : cap->results[i].type;
        if (!e->result_count && cap->result_count == 1 &&
            !(e->type.flags & SCRIPT_TYPE_RESULTS)) {
            type = e->type;
        }
        if (!script_type_char(type) ||
            (type.flags & (SCRIPT_TYPE_REFERENCE | SCRIPT_TYPE_OPTIONAL |
                           SCRIPT_TYPE_BOXED_OPTIONAL))) {
            continue;
        }
        char_check(l, host_result(l, first + i, type, e->range), type.storage, e->range);
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
static int task_command(lower *l, const node_command *command, int handle, const script_expression *e)
{
    script_type result = e->type;
    script_range range = e->range;
    uint64_t id = execution_task_command_id(command);
    pin_value *args = NULL, *results = NULL;
    if (handle >= 0) {
        apush(args, ((pin_value){ .type = PIN_TASK }));
    }
    bool join = command == &EXECUTION_TaskJoin;
    bool timed = command == &EXECUTION_TaskCancelAfter;
    if (join || timed) {
        apush(args, ((pin_value){ .type = join ? PIN_U64 : PIN_F64 }));
    }
    if (result.storage != PIN_FLOW) {
        apush(results, initial(l, result));
    }
    uint64_t action = id ^ (uint64_t)range.start ^ document_id(l->a, range);
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
                .type = PIN_TASK,
                .left = handle,
                .target = behavior_function_builder_command_first(l->builder, index),
                .offset = offsetof(pin_value, d) },
            range
        );
    }
    if (join || timed) {
        int value = join
                      ? constant(l, (pin_value){ .type = PIN_U64, .u64 = e->tag }, range)
                      : convert(
                            l,
                            expression(l, e->right),
                            l->a->expressions[e->right].type,
                            (script_type){ .storage = PIN_F64 },
                            range
                        );
        emit(
            l,
            (behavior_function_instruction){
                .op = BF_COMMAND_ARGUMENT,
                .type = join ? PIN_U64 : PIN_F64,
                .left = value,
                .target = behavior_function_builder_command_first(l->builder, index) + 1,
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
    if (result.storage == PIN_FLOW) {
        return -1;
    }
    int value = command_result(l, first, result, range);
    if (join) {
        int empty = constant(l, (pin_value){ .type = PIN_UNION }, range);
        emit(
            l,
            (behavior_function_instruction){ .op = BF_COMMAND_OUTPUT,
                                             .type = PIN_UNION,
                                             .left = empty,
                                             .target = first,
                                             .offset = offsetof(pin_value, d) },
            range
        );
    }
    return value;
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
                        : e->capability ^ (uint64_t)e->range.start ^
                              document_id(l->a, e->range);
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
                              .range = e->range },
        true
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
static bool bounded_type(const script_catalog *catalog, const script_catalog_type *type, int depth)
{
    if (!type || depth == 16) {
        return false;
    }
    for (int i = 0; i < type->field_count; i++) {
        const script_catalog_field *field = &type->fields[i];
        if (field->range_contract ||
            (field->type.storage == PIN_STRUCT &&
             bounded_type(
                 catalog,
                 script_catalog_type_by_id(catalog, field->type.nominal),
                 depth + 1
             ))) {
            return true;
        }
    }
    return false;
}
/* A selected scalar access consumes its private reference immediately. Prove
 * the reflected byte range before replacing that descriptor with a checked
 * value copy; ordinary member snapshots and escaping references stay generic. */
static bool scalar_component_field(const script_catalog_type *type, const script_catalog_field *field)
{
    script_type value = field->type;
    if (value.storage < PIN_BOOL || value.storage > PIN_F64 ||
        value.container != PIN_CONTAINER_SINGLE || value.flags || value.nominal ||
        field->offset > INT_MAX || !type->layout || !type->size) {
        return false;
    }
    const type_desc *root = type_catalog_find(type->name);
    size_t width = behavior_scalar_size(value.storage);
    if (!root || root->size != type->size || type_layout_hash(root) != type->layout ||
        field->offset > type->size || width > type->size - field->offset) {
        return false;
    }
    for (int j = 0; j < root->field_count; j++) {
        const type_field *actual = &root->fields[j];
        if (!strcmp(actual->name, field->name) && actual->type == value.storage &&
            actual->offset == field->offset && actual->size == width) {
            return true;
        }
    }
    return false;
}
static const script_catalog_capability *native_member_access(lower *l, const script_expression *e, bool write)
{
    if (e->kind != SCRIPT_EXPR_MEMBER || e->left < 0) {
        return NULL;
    }
    const script_catalog_type *type = script_catalog_type_by_id(
        l->catalog,
        l->a->expressions[e->left].type.nominal
    );
    const script_catalog_capability
        *cap = script_catalog_capability_by_id(l->catalog, write ? e->write : e->read);
    if (!type || !type->component || !cap || cap->component != type->id ||
        cap->component_field != e->member ||
        cap->component_access != (write ? SCRIPT_COMPONENT_ACCESS_WRITE
                                        : SCRIPT_COMPONENT_ACCESS_READ)) {
        return NULL;
    }
    for (int i = 0; i < type->field_count; i++) {
        if (type->fields[i].id == e->member && !type->fields[i].read_key) {
            return cap;
        }
    }
    return NULL;
}
static bool native_reference_member(lower *l, const script_expression *e)
{
    return (e->read || e->write) && (!e->read || native_member_access(l, e, false)) &&
           (!e->write || native_member_access(l, e, true));
}
static int component_call_mode(
    lower *l,
    const script_expression *e,
    const script_catalog_capability *cap,
    bool result_used
)
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
    const script_catalog_field *field = NULL;
    if (cap->component_field) {
        field = component_field(l, type, cap->component_field, e->range);
        if (!field) {
            return -1;
        }
        result = field->type;
    }
    if (cap->component_access == SCRIPT_COMPONENT_ACCESS_READ && field &&
        scalar_component_field(type, field)) {
        int owner = entity >= 0
                      ? expression(l, entity)
                      : value_op(
                            l,
                            (behavior_function_instruction){ .op = BF_OWNER,
                                                             .type = PIN_HANDLE },
                            e->range
                        );
        return value_op(
            l,
            (behavior_function_instruction){
                .op = BF_COMPONENT_READ,
                .type = result.storage,
                .left = owner,
                .component = behavior_function_builder_component(l->builder, type->component_id, false, !l->reduction_target),
                .value = { .type = PIN_U64, .u64 = type->component_id },
                .layout = type->layout,
                .offset = type->size,
                .target = (int)field->offset },
            e->range
        );
    }
    if (cap->component_access == SCRIPT_COMPONENT_ACCESS_READ) {
        return reference_read(l, component_reference(l, e, cap, type, entity, NULL), result, e->range);
    }
    if (cap->component_access == SCRIPT_COMPONENT_ACCESS_WRITE) {
        if (value < 0) {
            error(l, e->range, "Component write has no typed value");
            return -1;
        }
        if (field && scalar_component_field(type, field) && !e->instance &&
            script_type_equal(e->type, result)) {
            int owner = entity >= 0
                          ? expression(l, entity)
                          : value_op(
                                l,
                                (behavior_function_instruction){ .op = BF_OWNER,
                                                                 .type = PIN_HANDLE },
                                e->range
                            );
            const script_expression *source = &l->a->expressions[value];
            int updated = expression(l, value);
            updated = convert(l, updated, source->type, result, e->range);
            behavior_function_instruction write = {
                .op = BF_COMPONENT_WRITE,
                .type = result.storage,
                .left = owner,
                .right = updated,
                .component = behavior_function_builder_component(l->builder, type->component_id, false, true),
                .value = { .type = PIN_U64, .u64 = type->component_id },
                .layout = type->layout,
                .offset = type->size,
                .target = (int)field->offset
            };
            emit(l, write, e->range);
            /* The generic setter rereads when any field in the root is bounded,
             * even if this selected field is not. Otherwise its result is the
             * original converted RHS, not a callback-rewritten row value. */
            if (result_used && bounded_type(l->catalog, type, 0)) {
                write.op = BF_COMPONENT_READ;
                write.right = 0;
                updated = value_op(l, write, e->range);
            }
            return updated;
        }
        int root;
        int destination = component_reference(l, e, cap, type, entity, &root);
        const script_expression *source = &l->a->expressions[value];
        int updated = source->kind == SCRIPT_EXPR_STRUCT && source->partial
                        ? constructor_from(l, source, reference_read(l, destination, result, e->range))
                        : expression(l, value);
        updated = convert(l, updated, source->type, result, e->range);
        reference_write(l, destination, updated, result, e->range);
        if (result_used && bounded_type(l->catalog, type, 0)) {
            updated = reference_read(l, destination, result, e->range);
        }
        if (result_used && cap->component_field && e->type.storage == PIN_STRUCT &&
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
static int component_call(lower *l, const script_expression *e, const script_catalog_capability *cap)
{
    return component_call_mode(l, e, cap, true);
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
        if (field->c_string) {
            value = native_text_field(
                l,
                source,
                l->a->expressions[e->arguments[0].value].type,
                field,
                e->range
            );
        } else if (field->type.storage == PIN_STRUCT && field->type.container == PIN_CONTAINER_SINGLE) {
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
static int sequence_count(lower *, int, script_type, script_range);
static int sequence_item(lower *, int, int, script_type, script_type, bool, script_range);
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
/* `a ?? b` keeps a's value when a holds one and evaluates b only when it is
 * nil; both branches save into one local. */
static int coalesce(lower *l, const script_expression *e)
{
    script_type source = l->a->expressions[e->left].type;
    pin_type slot = slot_type(e->type);
    int optional = expression(l, e->left);
    int local = behavior_function_builder_local(l->builder, (pin_value){ .type = slot });
    value_root(l, BEHAVIOR_BANK_LOCAL, local, e->type);
    int branch = emit(
        l,
        (behavior_function_instruction){ .op = BF_BRANCH,
                                         .left = present(l, optional, source, e->range),
                                         .target = -1 },
        e->range
    );
    int fallback = convert(
        l,
        expression(l, e->right),
        l->a->expressions[e->right].type,
        e->type,
        e->range
    );
    emit(
        l,
        (
            behavior_function_instruction
        ){ .op = BF_SAVE, .type = slot, .left = fallback, .target = local },
        e->range
    );
    int end = emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = -1 }, e->range);
    behavior_function_builder_patch_target(l->builder, branch, l->pc);
    int value = e->type.flags & SCRIPT_TYPE_OPTIONAL
                  ? optional
                  : unwrap(l, optional, source, e->type, 0, 0, e->range);
    emit(
        l,
        (
            behavior_function_instruction
        ){ .op = BF_SAVE, .type = slot, .left = value, .target = local },
        e->range
    );
    behavior_function_builder_patch_target(l->builder, end, l->pc);
    return local_slot(l, local, slot, e->range);
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
        array = copy_value(l, array, collection, e->range);
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
        flow_result(
            l,
            e,
            cap,
            first,
            ordinal,
            sequence_item(l, array, current, l->a->expressions[input].type, element, false, e->range)
        );
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
    return id ^ document_id(l->a, e->range);
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
                    host.intrinsic != BF_OWNER && host.intrinsic != BF_ASSERT)) {
        error(l, e->range, "Callable capability has no linked execution contract");
        return -1;
    }
    if (cap->operation != SCRIPT_OPERATION_UNKNOWN
            ? host.intrinsic != behavior_operation_instruction(cap->operation) ||
                  host.call || host.function || host.command || host.query || host.access
            : host.intrinsic && host.intrinsic != BF_ASSERT) {
        error(l, e->range, "Linked native binding differs from engine operation contract");
        return -1;
    }
    if (host.function) {
        return external_call(l, e, cap, host);
    }
    if (host.intrinsic == BF_ASSERT) {
        return assertion(l, e);
    }
    if (host.intrinsic == BF_OWNER) {
        if (l->initializing || e->argument_count || cap->parameter_count ||
            cap->result_count != 1) {
            error(l, e->range, "Context intrinsic requires one result and no arguments");
            return -1;
        }
        pin_type storage = PIN_HANDLE;
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
            size_t nul = 0;
            if (pin_text_cstring(&l->module->storage, arg->constant, &key, &nul)) {
                error(l, arg->range, "Query key contains embedded NUL");
                return -1;
            }
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
                                          : e->capability ^ (uint64_t)e->range.start ^
                                                document_id(l->a, e->range) },
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
    /* A native that only reads its arguments during the call borrows them
     * as an authored call does; any other takes over copies. */
    bool lent = host.call->borrows;
    if (!lent) {
        behavior_function_builder_host_owned_arguments(l->builder, host_index);
    }
    int first = behavior_function_builder_host_first(l->builder, host_index);
    int temporaries = acount(l->temporaries);
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
            expected = wildcard_type(l, e, cap, arg->value);
        }
        int value;
        if (lent) {
            value = value_argument(l, e, i, expected);
        } else {
            value = expression(l, arg->value);
            value = convert(l, value, l->a->expressions[arg->value].type, expected, arg->range);
            value = copy_value(l, value, expected, arg->range);
            forget_expression(l, arg->value);
        }
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
    if (lent) {
        forget_arguments(l, e, temporaries);
    }
    int result_first = behavior_function_builder_host_results(l->builder, host_index);
    check_native_results(l, e, cap, result_first);
    complete_results(l, e, cap, result_first, false);
    for (int i = 0; i < e->argument_count; i++) {
        const script_argument *arg = &e->arguments[i];
        if (arg->parameter < 0 || arg->parameter >= argument_count(l, e, cap)) {
            continue;
        }
        script_type type = call_parameter(cap, arg->parameter)->type;
        if (type.storage == PIN_FLOW || (type.flags & SCRIPT_TYPE_UNTYPED)) {
            type = wildcard_type(l, e, cap, arg->value);
        }
        clear_value(l, first + arg->parameter, type, e->range);
    }
    if (e->type.flags & SCRIPT_TYPE_RESULTS) {
        return result_first; /* A result tuple indexes the host bank, never a VM slot. */
    }
    if (!cap->result_count) {
        return -1;
    }
    return host_result(l, result_first, e->type, e->range);
}
static int keyed_getter(lower *l, const script_expression *e, const script_catalog_field *field)
{
    const script_catalog_capability *cap = script_catalog_capability_by_id(l->catalog, e->read);
    script_lower_host host = { 0 };
    if (!cap || !l->bindings || !l->bindings->host ||
        !l->bindings->host(l->bindings->user, e->read, &host) || !host.call) {
        error(l, e->range, "Keyed field has no linked getter");
        return -1;
    }
    if (l->initializing && (!cap->pure || host.call->service)) {
        error(l, e->range, "State initializer requires a service-free pure capability");
        return -1;
    }
    assert(cap->parameter_count == 1 && cap->result_count == 1);
    pin_value key = initial(l, cap->parameters[0].type);
    if (key.type == PIN_STR) {
        pin_value owned = { 0 };
        const char *failure = pin_value_text(
            &l->module->storage,
            &owned,
            field->read_key,
            strlen(field->read_key)
        );
        if (failure) {
            error(l, e->range, failure);
            return -1;
        }
        key.text = owned.text;
    } else {
        key.str = field->read_key;
    }
    pin_value result = initial(l, e->type);
    int index = linked_host(l->builder, host.call, host.access, &key, 1, &result, 1);
    if (!behavior_function_builder_host_identity(
            l->builder,
            index,
            cap->id,
            script_catalog_capability_abi(l->catalog, cap)
        )) {
        error(l, e->range, "Cannot link keyed getter declaration identity");
    }
    emit(
        l,
        (behavior_function_instruction){ .op = BF_HOST_CALL,
                                         .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                         .target = index,
                                         .node_id = e->member },
        e->range
    );
    return host_result(l, behavior_function_builder_host_results(l->builder, index), e->type, e->range);
}
static int query_variable_value(lower *l, int variable, script_range range)
{
    if (!variable) {
        return value_op(l, (behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE }, range);
    }
    return value_op(
        l,
        (behavior_function_instruction){ .op = BF_QUERY_VARIABLE,
                                         .type = PIN_HANDLE,
                                         .target = variable },
        range
    );
}
static int component_owner(lower *l, const script_symbol *symbol, script_range range)
{
    int owner = query_variable_value(l, symbol ? symbol->source_variable : 0, range);
    if (!symbol || !symbol->source_relationship) {
        return owner;
    }
    const script_catalog_type *type = script_catalog_type_by_id(
        l->catalog,
        symbol->component_type.nominal ? symbol->component_type.nominal
                                       : symbol->type.nominal
    );
    pin_value headers[] = { { .type = BF_POINTER_TYPE },
                            { .type = PIN_HANDLE },
                            { .type = PIN_U64, .u64 = type->component_id },
                            { .type = PIN_U64, .u64 = symbol->source_relationship },
                            { .type = PIN_I32, .i = symbol->source_traversal } };
    int slots[] = { value_op(
                        l,
                        (behavior_function_instruction){ .op = BF_CONTEXT,
                                                         .type = BF_POINTER_TYPE,
                                                         .target = 1 },
                        range
                    ),
                    owner,
                    -1,
                    -1,
                    -1 };
    return primitive(
        l,
        &EXECUTION_query_source_call,
        &EXECUTION_component_read_access,
        headers,
        slots,
        5,
        (script_type){ .storage = PIN_HANDLE },
        range
    );
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
                   : component_owner(
                         l,
                         e->symbol >= 0 && l->a->symbols[e->symbol].kind == SCRIPT_COMPONENT
                             ? &l->a->symbols[e->symbol]
                             : NULL,
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
/* Whether an optional value holds one: a nil optional is all zero. */
static int present(lower *l, int value, script_type type, script_range range)
{
    if (type.storage == PIN_POINTER && type.container == PIN_CONTAINER_SINGLE) {
        int zero = constant(l, initial(l, type), range);
        return binary(l, SCRIPT_OP_NE, value, zero, (script_type){ .storage = PIN_BOOL }, type, range);
    }
    pin_type slot = slot_type(type);
    int zero = kind_op(
        l,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = slot,
                                         .value = { .type = slot } },
        range,
        bank_kind(l, BEHAVIOR_BANK_SLOT, value)
    );
    return value_op(
        l,
        (
            behavior_function_instruction
        ){ .op = BF_NE, .type = slot, .left = value, .right = zero },
        range
    );
}
static int condition_value(lower *l, int index)
{
    const script_expression *e = &l->a->expressions[index];
    if ((e->type.flags & SCRIPT_TYPE_TASK) && !(e->type.flags & SCRIPT_TYPE_OPTIONAL) &&
        e->type.container == PIN_CONTAINER_SINGLE) {
        const script_expression running = { .type = { .storage = PIN_BOOL },
                                            .range = e->range };
        return task_command(l, &EXECUTION_TaskRunning, expression(l, index), &running);
    }
    bool narrowed_optional = (e->type.storage == PIN_POINTER ||
                              e->type.storage == PIN_STRUCT) &&
                             e->symbol >= 0 &&
                             (l->a->symbols[e->symbol].type.flags & SCRIPT_TYPE_OPTIONAL);
    if (narrowed_optional) {
        return present(l, expression(l, index), e->type, e->range);
    }
    if (!(e->type.flags & SCRIPT_TYPE_OPTIONAL)) {
        return expression(l, index);
    }
    if (e->kind == SCRIPT_EXPR_COMPONENT) {
        return component_value(l, e, true);
    }
    if (e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0 &&
        l->a->symbols[e->symbol].query_description) {
        return present(l, expression(l, index), e->type, e->range);
    }
    if (e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0 &&
        l->a->symbols[e->symbol].kind == SCRIPT_COMPONENT) {
        return component_value(
            l,
            &(script_expression){ .capability = e->type.nominal,
                                  .symbol = e->symbol,
                                  .left = -1,
                                  .range = e->range },
            true
        );
    }
    return present(l, expression(l, index), e->type, e->range);
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
/* A system runs once per step with no arguments: its step's payload is Tick's
 * one field, the step the context holds, which a system reads by name. */
static int system_step(lower *l, const script_symbol *s, script_range range)
{
    const script_catalog_capability
        *tick = script_catalog_capability_by_id(l->catalog, l->catalog->tick_event);
    assert(tick && tick->parameter_count == 1);
    (void)tick;
    int slot = value_op(l, (behavior_function_instruction){ .op = BF_DELTA, .type = PIN_F32 }, range);
    script_type actual = s->type;
    actual.storage = PIN_F32;
    return convert(l, slot, actual, s->type, range);
}
static int symbol_value(lower *l, const script_expression *e)
{
    const script_symbol *s = &l->a->symbols[e->symbol];
    if (l->query_evaluating && l->parameters[e->symbol] >= 0) {
        bool aggregate = !(s->type.flags & SCRIPT_TYPE_BOXED_OPTIONAL) &&
                         (s->type.storage == PIN_STRUCT ||
                          s->type.container != PIN_CONTAINER_SINGLE);
        return typed_op(
            l,
            (behavior_function_instruction){ .op = aggregate ? BF_ARRAY : BF_ARGUMENT,
                                             .type = slot_type(s->type),
                                             .left = l->parameters[e->symbol],
                                             .target = aggregate ? BF_ARGUMENT : 0,
                                             .value = initial(l, s->type) },
            e->range,
            s->type
        );
    }
    if (s->query_description && !l->query_evaluating &&
        l->a->descriptions[s->query_description - 1].system) {
        int context = value_op(
            l,
            (behavior_function_instruction){ .op = BF_CONTEXT,
                                             .type = BF_POINTER_TYPE,
                                             .target = 1 },
            e->range
        );
        pin_value context_header = { .type = BF_POINTER_TYPE };
        script_type row_type = l->a->descriptions[s->query_description - 1].row_type;
        int row = primitive(
            l,
            &EXECUTION_query_current_row_call,
            &EXECUTION_value_access,
            &context_header,
            &context,
            1,
            row_type,
            e->range
        );
        pin_value headers[] = { initial(l, row_type),
                                { .type = PIN_I32, .i = s->query_field } };
        int slots[] = { row, -1 };
        return primitive(
            l,
            &EXECUTION_query_row_get_call,
            &EXECUTION_value_access,
            headers,
            slots,
            2,
            s->type,
            e->range
        );
    }
    if (s->query_description && l->query_evaluating && l->locals[e->symbol] >= 0) {
        return typed_op(
            l,
            (behavior_function_instruction){ .op = BF_LOCAL,
                                             .type = slot_type(s->type),
                                             .left = l->locals[e->symbol] },
            e->range,
            s->type
        );
    }
    if (managed_symbol(l, e)) {
        return managed_read(l, managed_local(l, e->symbol, e->range), s->type, e->range);
    }
    if (s->kind == SCRIPT_CONSTANT) {
        pin_value value = l->constant_values[e->symbol];
        if (value.type != PIN_FLOW) {
            if (s->type.storage == PIN_STRUCT ||
                s->type.container != PIN_CONTAINER_SINGLE) {
                return value_op(
                    l,
                    (behavior_function_instruction){ .op = BF_ARRAY,
                                                     .type = BF_POINTER_TYPE,
                                                     .target = BF_CONSTANT,
                                                     .value = value },
                    e->range
                );
            }
            return constant(l, value, e->range);
        }
        if (s->initializer < 0 || l->initializer_depth++ >= l->a->expression_count) {
            error(l, e->range, "Constant dependency has no finite value");
            return -1;
        }
        int slot = expression(l, s->initializer);
        l->initializer_depth--;
        return convert(l, slot, l->a->expressions[s->initializer].type, s->type, e->range);
    }
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
        return reference_read(l, reference_expression(l, e, false), s->type, e->range);
    }
    if (s->kind == SCRIPT_QUERY_VARIABLE) {
        return query_variable_value(l, s->source_variable, e->range);
    }
    if (l->variables[e->symbol] >= 0) {
        if (!(s->type.flags & SCRIPT_TYPE_BOXED_OPTIONAL) &&
            (s->type.container != PIN_CONTAINER_SINGLE || s->type.storage == PIN_STRUCT)) {
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
    if (s->kind == SCRIPT_PARAMETER && s->function >= 0 &&
        l->a->functions[s->function].kind == SCRIPT_FUNCTION_SYSTEM) {
        return system_step(l, s, e->range);
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
        if (!(s->type.flags & SCRIPT_TYPE_BOXED_OPTIONAL) &&
            (s->type.container != PIN_CONTAINER_SINGLE || s->type.storage == PIN_STRUCT)) {
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
        if (s->type.flags & SCRIPT_TYPE_REFERENCE) {
            int descriptor = value_op(
                l,
                (behavior_function_instruction){ .op = BF_LOCAL,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = l->locals[e->symbol] },
                e->range
            );
            return reference_read(l, descriptor, s->type, e->range);
        }
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
    if (s->type.flags & SCRIPT_TYPE_NAMESPACE) {
        error(l, e->range, "A namespace has no value; choose one of its named fields");
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
static int sequence_count(lower *l, int array, script_type sequence, script_range range)
{
    if (sequence.container != PIN_CONTAINER_VIEW) {
        return array_count(l, array, range);
    }
    pin_value header = initial(l, sequence);
    return primitive(
        l,
        &EXECUTION_sequence_length_call,
        &EXECUTION_sequence_read_access,
        &header,
        &array,
        1,
        (script_type){ .storage = PIN_I32 },
        range
    );
}
/* A value the reader is done with at once: no aggregate, text or name
 * outlives the load. */
static bool scalar_value(script_type type)
{
    return !copied_value(type) && type.storage != PIN_STR &&
           type.storage != PIN_NAME && !(type.flags & SCRIPT_TYPE_RESULTS);
}
/* Evaluates `index`, a place a reader is done with before its array can
 * change when `borrow` is set, so its element reads need not copy. */
static int borrowed_place(lower *l, int index, bool borrow)
{
    const script_expression *e = &l->a->expressions[index];
    l->borrowing = borrow &&
                   (e->kind == SCRIPT_EXPR_INDEX || e->kind == SCRIPT_EXPR_MEMBER);
    l->borrowed_expression = index;
    int value = expression(l, index);
    l->borrowing = false;
    return value;
}
static int sequence_item(
    lower *l,
    int array,
    int index,
    script_type sequence,
    script_type item,
    bool borrowed,
    script_range range
)
{
    pin_value headers[] = { initial(l, sequence), { .type = PIN_I32 } };
    int slots[] = { array, index };
    /* The runtime validates both boxed element headers and packed layouts
     * before exposing a payload as the statically expected element type. An
     * index outside the sequence is an argument failure. */
    return failing_primitive(
        l,
        borrowed ? &EXECUTION_sequence_borrow_call : &EXECUTION_sequence_get_call,
        &EXECUTION_sequence_read_access,
        headers,
        slots,
        2,
        item,
        BEHAVIOR_FUNCTION_ARGUMENT,
        range
    );
}
static int reference_expression(lower *l, const script_expression *e, bool written)
{
    if (e->kind == SCRIPT_EXPR_UNWRAP) {
        pin_value headers[] = { { .type = BF_POINTER_TYPE },
                                { .type = PIN_U64, .u64 = e->tag },
                                { .type = PIN_I32, .i = (int)e->offset },
                                initial(l, e->type) };
        int slots[] = { reference(l, e->left), -1, -1, -1 };
        return primitive(
            l,
            &EXECUTION_reference_union_call,
            &EXECUTION_value_access,
            headers,
            slots,
            4,
            (script_type){ .storage = BF_POINTER_TYPE },
            e->range
        );
    }
    if (e->kind == SCRIPT_EXPR_DEREFERENCE) {
        const script_catalog_type
            *component = script_catalog_type_by_id(l->catalog, e->type.nominal);
        if (component && component->component) {
            behavior_function_builder_component(l->builder, component->component_id, false, written);
        }
        return managed_reference(l, expression(l, e->left), e->type, e->range);
    }
    if (managed_symbol(l, e)) {
        return managed_reference(
            l,
            managed_local(l, e->symbol, e->range),
            l->a->symbols[e->symbol].type,
            e->range
        );
    }
    if (e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0 &&
        (l->a->symbols[e->symbol].type.flags & SCRIPT_TYPE_REFERENCE) &&
        l->locals[e->symbol] >= 0) {
        return value_op(
            l,
            (behavior_function_instruction){ .op = BF_LOCAL,
                                             .type = BF_POINTER_TYPE,
                                             .left = l->locals[e->symbol] },
            e->range
        );
    }
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
    if ((e->kind == SCRIPT_EXPR_MEMBER &&
         ((!e->read && !e->write) || native_reference_member(l, e))) ||
        e->kind == SCRIPT_EXPR_INDEX) {
        if (e->kind == SCRIPT_EXPR_INDEX &&
            l->a->expressions[e->left].type.container == PIN_CONTAINER_MAP) {
            script_type key = script_catalog_key_type(
                l->catalog,
                l->a->expressions[e->left].type
            );
            pin_value headers[] = { { .type = BF_POINTER_TYPE },
                                    initial(l, e->type),
                                    initial(l, key) };
            int slots[] = { reference(l, e->left), -1, expression(l, e->right) };
            return primitive(
                l,
                &EXECUTION_reference_key_call,
                &EXECUTION_value_access,
                headers,
                slots,
                3,
                (script_type){ .storage = BF_POINTER_TYPE },
                e->range
            );
        }
        pin_value headers[] = { { .type = BF_POINTER_TYPE },
                                initial(l, e->type),
                                { .type = PIN_U64, .u64 = e->offset },
                                { .type = PIN_BOOL, .b = e->kind == SCRIPT_EXPR_INDEX } };
        int slots[] = { reference(l, e->left), -1, -1, -1 };
        if (e->kind == SCRIPT_EXPR_INDEX) {
            headers[2] = (pin_value){ .type = PIN_I32 };
            slots[2] = int_slot(l, e->right, e->range);
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
        slots[5] = !symbol && e->left >= 0 ? expression(l, e->left)
                                           : component_owner(l, symbol, e->range);
        behavior_function_builder_component(l->builder, type->component_id, false, written && !l->reduction_target);
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
    return reference_expression(l, &l->a->expressions[index], true);
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
static void discard_call_results(lower *l, const script_expression *e, int first)
{
    if (e->kind != SCRIPT_EXPR_CALL || !(e->type.flags & SCRIPT_TYPE_RESULTS) ||
        command_tuple(l, e)) {
        return;
    }
    for (int i = 0; i < e->result_count; i++) {
        clear_value(l, first + i, e->results[i], e->range);
    }
}
static behavior_source_point debug_origin(lower *l, script_record_kind kind, int record, script_range range)
{
    if (!l->occurrences) {
        l->occurrences = script_occurrences_new(l->a);
    }
    return (behavior_source_point){
        .occurrence = script_record_id(l->occurrences, kind, record),
        .meaning = script_record_meaning(l->a, kind, record),
        .start = range.start,
        .end = range.end,
        .line = range.line,
        .column = range.column,
        .kind = kind
    };
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
    if (l->pattern_cached && index == l->pattern_expression) {
        return l->pattern_value;
    }
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
        case SCRIPT_EXPR_SEQUENCE: {
            pin_value headers[] = { initial(l, l->a->expressions[e->left].type),
                                    { .type = PIN_I32 },
                                    { .type = PIN_I32 },
                                    { .type = PIN_U64, .u64 = e->tag } };
            int slots[] = { expression(l, e->left), -1, -1, -1 };
            bool clone = e->member == SCRIPT_SEQUENCE_CLONE;
            bool length = e->member == SCRIPT_SEQUENCE_LENGTH;
            script_type source = l->a->expressions[e->left].type;
            bool snapshot = !clone && !length &&
                            source.container != PIN_CONTAINER_SINGLE &&
                            ((l->a->expressions[e->right].effects |
                              l->a->expressions[e->arguments[0].value].effects) &
                             (SCRIPT_EFFECT_WRITE | SCRIPT_EFFECT_HOST |
                              SCRIPT_EFFECT_STRUCTURAL | SCRIPT_EFFECT_SUSPEND));
            if (snapshot) {
                slots[0] = copy_value(l, slots[0], source, e->range);
            }
            if (!clone && !length) {
                slots[1] = int_slot(l, e->right, e->range);
                slots[2] = int_slot(l, e->arguments[0].value, e->range);
            }
            int value = primitive(
                l,
                length  ? &EXECUTION_sequence_length_call
                : clone ? &EXECUTION_sequence_clone_call
                : e->member == SCRIPT_SEQUENCE_VIEW ? &EXECUTION_sequence_view_call
                                                    : &EXECUTION_sequence_slice_call,
                &EXECUTION_sequence_read_access,
                headers,
                slots,
                clone || length ? 1 : 4,
                e->type,
                e->range
            );
            if (snapshot) {
                drop_value(l, slots[0], source, e->range);
            }
            return value;
        }
        case SCRIPT_EXPR_ARRAY_MUTATE: {
            pin_value *headers = NULL;
            int *slots = NULL;
            apush(headers, ((pin_value){ .type = BF_POINTER_TYPE }));
            apush(headers, ((pin_value){ .type = PIN_I32, .i = (int)e->member }));
            /* A named array is watched where it is evaluated, before the
             * arguments, which can invoke another body; its reference keeps
             * the name's points. */
            const script_expression *array = &l->a->expressions[e->left];
            if (array->kind == SCRIPT_EXPR_SYMBOL) {
                int value = symbol_value(l, array);
                value_root(l, BEHAVIOR_BANK_SLOT, value, array->type);
                debug_watch(l, e->left, value, array->type, true, 0);
                debug_input(l, e, 0, value, array->type);
            }
            apush(slots, reference(l, e->left));
            apush(slots, -1);
            for (int i = 1; i < e->argument_count; i++) {
                apush(headers, ((pin_value){ 0 }));
                apush(slots, -1);
            }
            for (int i = 0; i < e->argument_count; i++) {
                const script_argument *argument = &e->arguments[i];
                if (argument->parameter <= 0) {
                    continue;
                }
                int at = argument->parameter + 1;
                if (at >= acount(slots)) {
                    error(l, e->range, "Array mutation argument is outside its signature");
                    continue;
                }
                headers[at] = initial(l, l->a->expressions[argument->value].type);
                slots[at] = expression(l, argument->value);
                debug_input(
                    l,
                    e,
                    argument->parameter,
                    slots[at],
                    l->a->expressions[argument->value].type
                );
                slots[at] = copy_value(
                    l,
                    slots[at],
                    l->a->expressions[argument->value].type,
                    argument->range
                );
            }
            int value = primitive(
                l,
                &EXECUTION_array_mutate_place_call,
                &EXECUTION_assignment_access,
                headers,
                slots,
                acount(headers),
                e->type,
                e->range
            );
            for (int i = 0; i < e->argument_count; i++) {
                const script_argument *argument = &e->arguments[i];
                if (argument->parameter > 0 && argument->parameter + 1 < acount(slots)) {
                    drop_value(
                        l,
                        slots[argument->parameter + 1],
                        l->a->expressions[argument->value].type,
                        e->range
                    );
                }
            }
            afree(headers);
            afree(slots);
            return value;
        }
        case SCRIPT_EXPR_UNION: return union_construct(l, e);
        case SCRIPT_EXPR_IS:
        case SCRIPT_EXPR_AS: return union_pattern(l, e);
        case SCRIPT_EXPR_UNWRAP:
            return unwrap(
                l,
                expression(l, e->left),
                l->a->expressions[e->left].type,
                e->type,
                e->tag,
                (int)e->offset,
                e->range
            );
        case SCRIPT_EXPR_INDEX: {
            bool borrowed = l->borrowing && l->borrowed_expression == index;
            l->borrowing = false;
            int array = expression(l, e->left);
            script_type collection = l->a->expressions[e->left].type;
            bool snapshot = index_needs_snapshot(l, e);
            if (snapshot) {
                array = copy_value(l, array, collection, e->range);
            }
            if (collection.container == PIN_CONTAINER_MAP) {
                /* An absent key reads the value type's zero when it has one. */
                script_type key = script_catalog_key_type(l->catalog, collection);
                pin_value headers[] = { initial(l, collection),
                                        initial(l, key),
                                        initial(l, e->type) };
                int slots[] = { array, expression(l, e->right), -1 };
                int count = 2;
                if (e->argument_count) {
                    int absent = e->arguments[0].value;
                    slots[count++] = convert(
                        l,
                        expression(l, absent),
                        l->a->expressions[absent].type,
                        e->type,
                        e->range
                    );
                }
                int result = primitive(
                    l,
                    &EXECUTION_map_get_call,
                    &EXECUTION_value_access,
                    headers,
                    slots,
                    count,
                    e->type,
                    e->range
                );
                if (snapshot) {
                    result = copy_value(l, result, e->type, e->range);
                    drop_value(l, array, collection, e->range);
                }
                return result;
            }
            int result = sequence_item(
                l,
                array,
                int_slot(l, e->right, e->range),
                collection,
                e->type,
                borrowed && !snapshot && collection.container != PIN_CONTAINER_SINGLE,
                e->range
            );
            if (snapshot) {
                result = copy_value(l, result, e->type, e->range);
                drop_value(l, array, collection, e->range);
            }
            return result;
        }
        case SCRIPT_EXPR_FALLBACK: return expression(l, e->left);
        case SCRIPT_EXPR_NIL:
            if (e->type.storage == PIN_POINTER &&
                e->type.container == PIN_CONTAINER_SINGLE) {
                return constant(l, initial(l, e->type), e->range);
            }
            if (e->type.flags & SCRIPT_TYPE_BOXED_OPTIONAL) {
                return constant(l, (pin_value){ .type = PIN_UNION }, e->range);
            }
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
        case SCRIPT_EXPR_ADDRESS:
            return managed_address(l, &l->a->expressions[e->left]);
        case SCRIPT_EXPR_DEREFERENCE:
            return managed_read(l, expression(l, e->left), e->type, e->range);
        case SCRIPT_EXPR_CALL: return call(l, e);
        case SCRIPT_EXPR_YIELD: return task_command(l, &EXECUTION_Yield, -1, e);
        case SCRIPT_EXPR_JOIN:
            return task_command(l, &EXECUTION_TaskJoin, expression(l, e->left), e);
        case SCRIPT_EXPR_CANCEL:
            return task_command(
                l,
                e->right >= 0 ? &EXECUTION_TaskCancelAfter : &EXECUTION_TaskCancel,
                expression(l, e->left),
                e
            );
        case SCRIPT_EXPR_STRUCT:
        case SCRIPT_EXPR_ARRAY: return constructor(l, e);
        case SCRIPT_EXPR_MAP: return map_constructor(l, e);
        case SCRIPT_EXPR_MAP_HAS:
        case SCRIPT_EXPR_MAP_REMOVE: {
            script_type collection = l->a->expressions[e->left].type;
            script_type key = script_catalog_key_type(l->catalog, collection);
            pin_value headers[] = { initial(l, collection), initial(l, key) };
            int slots[] = { expression(l, e->left), expression(l, e->right) };
            return primitive(
                l,
                e->kind == SCRIPT_EXPR_MAP_HAS ? &EXECUTION_map_has_call
                                               : &EXECUTION_map_remove_call,
                &EXECUTION_value_access,
                headers,
                slots,
                2,
                e->type,
                e->range
            );
        }
        case SCRIPT_EXPR_COMPONENT:
            return reference_read(l, reference_expression(l, e, false), e->type, e->range);
        case SCRIPT_EXPR_CAST: {
            int left = expression(l, e->left);
            return convert_mode(l, left, l->a->expressions[e->left].type, e->type, e->conversion, e->range);
        }
        case SCRIPT_EXPR_UNARY: {
            int left = e->op == SCRIPT_OP_NOT ? condition_value(l, e->left)
                                              : expression(l, e->left);
            if (e->op == SCRIPT_OP_NONE) {
                return convert(l, left, l->a->expressions[e->left].type, e->type, e->range);
            }
            if (e->op == SCRIPT_OP_NEG || e->op == SCRIPT_OP_BIT_NOT) {
                int result = value_op(
                    l,
                    (behavior_function_instruction){
                        .op = e->op == SCRIPT_OP_NEG ? BF_NEG : BF_BIT_NOT,
                        .type = slot_type(e->type),
                        .left = left },
                    e->range
                );
                if (script_type_char(e->type)) {
                    char_check(l, result, PIN_I32, e->range);
                }
                return result;
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
            if (e->op == SCRIPT_OP_COALESCE) {
                return coalesce(l, e);
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
            /* Text compared at once is done with before its array can change,
             * unless the right operand runs first and changes it. */
            bool texts = (e->op == SCRIPT_OP_EQ || e->op == SCRIPT_OP_NE) &&
                         behavior_scalar_string(l->a->expressions[e->left].type.storage);
            int left = borrowed_place(l, e->left, texts && !may_change(l, e->right));
            int right = borrowed_place(l, e->right, texts);
            script_type operand = e->type.storage != PIN_BOOL
                                    ? e->type
                                    : l->a->expressions[e->left].type;
            if (e->type.storage == PIN_BOOL && script_type_numeric(operand)) {
                /* Integers compare in the type holding both, as arithmetic does;
                 * a char beside another integer compares as that integer. */
                script_type other = l->a->expressions[e->right].type;
                if (script_type_char(operand) != script_type_char(other)) {
                    operand.nominal = script_type_char(operand) ? 0 : operand.nominal;
                    other.nominal = script_type_char(other) ? 0 : other.nominal;
                }
                pin_type common = behavior_scalar_common_integer(
                    operand.storage,
                    other.storage
                );
                if (common == other.storage ||
                    (common == PIN_FLOW && other.storage > operand.storage)) {
                    operand = other;
                } else if (common != PIN_FLOW && common != operand.storage) {
                    operand = (script_type){ .storage = common };
                }
            }
            if (behavior_scalar_string(operand.storage) &&
                behavior_scalar_string(l->a->expressions[e->right].type.storage) &&
                (operand.storage == PIN_STR ||
                 l->a->expressions[e->right].type.storage == PIN_STR)) {
                operand = (script_type){ .storage = PIN_STR };
            }
            left = convert(l, left, l->a->expressions[e->left].type, operand, e->range);
            right = convert(l, right, l->a->expressions[e->right].type, operand, e->range);
            return binary(l, e->op, left, right, e->type, operand, e->range);
        }
        case SCRIPT_EXPR_QUERY: {
            const script_query_description *query = &l->a->descriptions[e->entry];
            pin_value *headers = NULL;
            int *slots = NULL;
            apush(headers, ((pin_value){ .type = BF_POINTER_TYPE }));
            apush(
                slots,
                value_op(
                    l,
                    (behavior_function_instruction){ .op = BF_CONTEXT,
                                                     .type = BF_POINTER_TYPE,
                                                     .target = 1 },
                    e->range
                )
            );
            apush(headers, ((pin_value){ .type = PIN_I32, .i = e->entry }));
            apush(slots, -1);
            for (int i = 0; i < acount(query->captures); i++) {
                const script_symbol *symbol = &l->a->symbols[query->captures[i]];
                apush(headers, initial(l, symbol->type));
                apush(
                    slots,
                    symbol_value(
                        l,
                        &(script_expression){ .kind = SCRIPT_EXPR_SYMBOL,
                                              .symbol = query->captures[i],
                                              .type = symbol->type,
                                              .range = e->range }
                    )
                );
            }
            int result = primitive(
                l,
                &EXECUTION_query_make_call,
                &EXECUTION_value_access,
                headers,
                slots,
                acount(headers),
                e->type,
                e->range
            );
            afree(headers);
            afree(slots);
            return result;
        }
        case SCRIPT_EXPR_QUERY_FIELD: {
            pin_value headers[] = { initial(l, l->a->expressions[e->left].type),
                                    { .type = PIN_I32, .i = (int)e->offset } };
            int slots[] = { expression(l, e->left), -1 };
            return primitive(
                l,
                &EXECUTION_query_row_get_call,
                &EXECUTION_value_access,
                headers,
                slots,
                2,
                e->type,
                e->range
            );
        }
        case SCRIPT_EXPR_MEMBER: {
            if (sole_output(l, e)) {
                return convert(
                    l,
                    expression(l, e->left),
                    l->a->expressions[e->left].type,
                    e->type,
                    e->range
                );
            }
            const script_expression *receiver = &l->a->expressions[e->left];
            if (receiver->kind == SCRIPT_EXPR_UNWRAP &&
                (receiver->type.flags & SCRIPT_TYPE_RESULTS)) {
                return union_get(
                    l,
                    expression(l, receiver->left),
                    receiver->tag,
                    (int)e->offset,
                    false,
                    e->type,
                    e->range
                );
            }
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
                /* Only the handler that receives a payload reads it. */
                assert(
                    source < 0 || source == l->payload_function ||
                    l->a->functions[source].kind != SCRIPT_FUNCTION_HANDLER
                );
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
                const script_catalog_type
                    *type = script_catalog_type_by_id(l->catalog, receiver->type.nominal);
                const script_catalog_field *field = NULL;
                for (int i = 0; type && i < type->field_count; i++) {
                    if (type->fields[i].id == e->member) {
                        field = &type->fields[i];
                        break;
                    }
                }
                const script_catalog_capability
                    *native = script_catalog_capability_by_id(l->catalog, e->read);
                bool native_property = field && !field->read_key && native &&
                                       native->component_access ==
                                           SCRIPT_COMPONENT_ACCESS_READ &&
                                       native->component == type->id &&
                                       native->component_field == field->id;
                if (native_property && receiver->kind == SCRIPT_EXPR_COMPONENT &&
                    receiver->left >= 0) {
                    /* An immediate native property consumes the entity, not a
                     * snapshot of its component. Reuse the scalar layout proof
                     * and generic managed/unproven fallback in component_call.
                     * Explicit aggregate locals and custom accessors stay on
                     * their original snapshot paths. */
                    script_catalog_parameter entity = {
                        .type = l->a->expressions[receiver->left].type
                    };
                    script_catalog_capability access = *native;
                    access.parameters = &entity;
                    access.parameter_count = 1;
                    access.component_entity = 0;
                    script_argument argument = { .value = receiver->left,
                                                 .parameter = 0,
                                                 .range = e->range };
                    script_expression selected = *e;
                    selected.kind = SCRIPT_EXPR_CALL;
                    selected.arguments = &argument;
                    selected.argument_count = 1;
                    return component_call(l, &selected, &access);
                }
                if (!native_property) {
                    if (field && field->read_key) {
                        return keyed_getter(l, e, field);
                    }
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
            }
            const script_catalog_type
                *owner = script_catalog_type_by_id(l->catalog, receiver->type.nominal);
            for (int i = 0; owner && i < owner->field_count; i++) {
                if (owner->fields[i].id == e->member && owner->fields[i].c_string) {
                    return native_text_field(
                        l,
                        expression(l, e->left),
                        receiver->type,
                        &owner->fields[i],
                        e->range
                    );
                }
            }
            bool borrowed = l->borrowing && l->borrowed_expression == index;
            l->borrowing = false;
            int left = borrowed_place(l, e->left, borrowed || scalar_value(e->type));
            if (receiver->type.flags & SCRIPT_TYPE_RESULTS) {
                int value = command_tuple(l, receiver)
                              ? command_result(l, left + (int)e->offset, e->type, e->range)
                              : host_result(l, left + (int)e->offset, e->type, e->range);
                value_root(l, BEHAVIOR_BANK_SLOT, value, e->type);
                discard_call_results(l, receiver, left);
                return value;
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
        remember_expression(l, index, value);
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
            value_root(l, BEHAVIOR_BANK_SLOT, value, e->type);
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
    bool consumed = expression && expression->kind == SCRIPT_EXPR_CALL && !command_bank;
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
            field = copy_value(l, field, type, range);
            int previous = host_result(l, l->locals[symbol] + i, type, range);
            host_argument(l, l->locals[symbol] + i, field, type, range);
            drop_value(l, previous, type, range);
        }
        if (consumed) {
            clear_value(l, value + i, source_type, range);
        }
    }
}
static int local_address(lower *l, int symbol)
{
    if (l->addresses[symbol] < 0) {
        script_type type = l->a->symbols[symbol].type;
        if (l->a->symbols[symbol].addressed) {
            type = managed_type(l, type);
        }
        pin_value header = initial(l, type);
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
static void declare_value(lower *, int, int, store_mode, script_range);
static void declare_symbol(lower *l, int symbol, int value, script_range range)
{
    declare_value(l, symbol, value, STORE_COPY, range);
}
static void declare_value(lower *l, int symbol, int value, store_mode mode, script_range range)
{
    const script_symbol *s = &l->a->symbols[symbol];
    if (s->addressed && l->parameters[symbol] < 0) {
        script_type pointer = managed_type(l, s->type);
        release(l, bank_reference(l, local_address(l, symbol), pointer, range), range);
        pin_value headers[] = { { .type = BF_POINTER_TYPE },
                                initial(l, s->type),
                                initial(l, s->type) };
        int slots[] = { value_op(
                            l,
                            (behavior_function_instruction){ .op = BF_CONTEXT,
                                                             .type = BF_POINTER_TYPE,
                                                             .target = 1 },
                            range
                        ),
                        -1,
                        value };
        int cell = primitive(
            l,
            &EXECUTION_pointer_make_call,
            &EXECUTION_value_access,
            headers,
            slots,
            3,
            pointer,
            range
        );
        host_argument(l, local_address(l, symbol), cell, pointer, range);
        debug_point(l, SCRIPT_RECORD_SYMBOL, symbol, range);
        return;
    }
    if ((s->boxed || s->type.storage == PIN_POINTER) && s->writable &&
        l->variables[symbol] < 0 && l->parameters[symbol] < 0) {
        local_address(l, symbol);
        int descriptor = reference_expression(
            l,
            &(script_expression){ .kind = SCRIPT_EXPR_SYMBOL,
                                  .symbol = symbol,
                                  .left = -1,
                                  .right = -1,
                                  .type = s->type,
                                  .range = range },
            true
        );
        release(l, descriptor, range);
    }
    store_value(l, symbol, value, mode, range);
}
/* Whether a local or parameter lives in this frame's own banks, out of
 * reach of pointers, tasks and state. */
static bool symbol_private(const lower *l, int symbol)
{
    const script_symbol *s = &l->a->symbols[symbol];
    return (s->kind == SCRIPT_LOCAL || s->kind == SCRIPT_PARAMETER) &&
           !(s->type.flags & (SCRIPT_TYPE_REFERENCE | SCRIPT_TYPE_RESULTS)) &&
           s->type.storage != PIN_POINTER && !s->addressed && !s->boxed &&
           l->variables[symbol] < 0 && !(l->captured && l->captured[symbol] >= 0);
}
/* Whether a private local or parameter keeps one value for its whole life:
 * nothing reassigns it or changes it in place. */
static bool symbol_fixed(lower *l, int symbol)
{
    return symbol_private(l, symbol) && !symbol_written(l, symbol);
}
/* How a declaration stores its initializer, which took no conversion when
 * `same`. A fresh temporary moves into the local. A local nothing writes reads
 * a place in place when the local or parameter under that place keeps one
 * value too: that value then outlives every read of the local. */
/* Whether declaration `s` can read its initializer in place: a fixed local
 * initialized from a place under a fixed local or parameter, which nothing
 * changes while the local lives. A view is not such a place: its owner can
 * still change the array under it. */
static bool lends_declaration(lower *l, const script_statement *s)
{
    if (s->expression < 0 || !copied_value(l->a->symbols[s->symbol].type) ||
        l->a->symbols[s->symbol].kind != SCRIPT_LOCAL || !symbol_fixed(l, s->symbol)) {
        return false;
    }
    const script_expression *e = &l->a->expressions[s->expression];
    while (e->kind == SCRIPT_EXPR_MEMBER || e->kind == SCRIPT_EXPR_INDEX) {
        if (e->type.storage == PIN_POINTER ||
            e->type.container == PIN_CONTAINER_VIEW || e->left < 0) {
            return false;
        }
        e = &l->a->expressions[e->left];
    }
    return e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0 &&
           e->type.container != PIN_CONTAINER_VIEW && symbol_fixed(l, e->symbol);
}
static store_mode declaration_mode(lower *l, const script_statement *s, bool same)
{
    const script_symbol *symbol = &l->a->symbols[s->symbol];
    if (!same || s->expression < 0 || !copied_value(symbol->type) ||
        symbol->kind != SCRIPT_LOCAL || !symbol_private(l, s->symbol)) {
        return STORE_COPY;
    }
    if (owns_expression_value(l, &l->a->expressions[s->expression])) {
        for (int i = 0; i < acount(l->temporaries); i++) {
            if (l->temporaries[i].expression == s->expression) {
                l->temporaries[i] = l->temporaries[acount(l->temporaries) - 1];
                asetlen(l->temporaries, acount(l->temporaries) - 1);
                return STORE_MOVE;
            }
        }
        return STORE_COPY;
    }
    if (!lends_declaration(l, s)) {
        return STORE_COPY;
    }
    if (!l->lent) {
        l->lent = xcalloc((size_t)l->a->symbol_count + 1, sizeof *l->lent);
    }
    l->lent[s->symbol] = true;
    return STORE_LEND;
}
/* Writable locals occupy stable data banks, which are also runtime roots.
 * Drop those roots on scope exit, after moving any escaped references into
 * their task-owned boxes. Saved immutable locals use suspension liveness. */
static void leave_scope(lower *l, int scope, script_range range)
{
    bool continuation = false;
    for (int i = 0; i < l->a->function_count; i++) {
        const script_function *child = &l->a->functions[i];
        if (child->parent == l->function && child->kind == SCRIPT_FUNCTION_CONTINUATION) {
            continuation = true;
            break;
        }
    }
    for (int symbol = 0; symbol < l->a->symbol_count; symbol++) {
        const script_symbol *s = &l->a->symbols[symbol];
        if (s->scope != scope || s->kind != SCRIPT_LOCAL ||
            (l->captured && l->captured[symbol] >= 0)) {
            continue;
        }
        /* Continuations capture every parent local bank when the action
         * completes, after this lexical scope may have exited. Their retained
         * parent frame keeps those banks rooted until completion. */
        if (continuation && l->addresses[symbol] >= 0) {
            continue;
        }
        if (s->addressed && l->addresses[symbol] >= 0) {
            script_type pointer = managed_type(l, s->type);
            release(l, bank_reference(l, l->addresses[symbol], pointer, range), range);
            clear_value(l, l->addresses[symbol], pointer, range);
            continue;
        }
        if (s->type.flags & SCRIPT_TYPE_RESULTS) {
            const script_expression *signature = tuple_signature(l, s->initializer);
            if (l->locals[symbol] < 0 || !signature) {
                continue;
            }
            release_tuple(l, symbol, range);
            for (int i = 0; i < signature->result_count; i++) {
                script_type type = signature->results[i];
                drop_value(l, host_result(l, l->locals[symbol] + i, type, range), type, range);
                clear_value(l, l->locals[symbol] + i, signature->results[i], range);
            }
        } else if (l->addresses[symbol] >= 0 && contains_handles(s->type)) {
            int bank = l->addresses[symbol];
            if (s->boxed || s->type.storage == PIN_POINTER) {
                release(l, bank_reference(l, bank, s->type, range), range);
            } else if (!l->lent || !l->lent[symbol]) {
                /* A lent local shares its owner's value: the owner drops it. */
                drop_value(l, host_result(l, bank, s->type, range), s->type, range);
            }
            clear_value(l, bank, s->type, range);
        }
    }
}
static void leave_scopes(lower *l, int scope, int until, script_range range)
{
    for (; scope != until; scope = l->a->scopes[scope].parent) {
        assert(scope >= 0);
        leave_scope(l, scope, range);
    }
}
static void store_symbol(lower *l, int symbol, int value, script_range range)
{
    store_value(l, symbol, value, STORE_COPY, range);
}
static void store_value(lower *l, int symbol, int value, store_mode mode, script_range range)
{
    if (symbol < 0 || symbol >= l->a->symbol_count) {
        error(l, range, "Assignment has no resolved target symbol");
        return;
    }
    const script_symbol *s = &l->a->symbols[symbol];
    debug_point(l, SCRIPT_RECORD_SYMBOL, symbol, range);
    if (s->addressed &&
        (l->parameters[symbol] < 0 || (l->captured && l->captured[symbol] >= 0))) {
        managed_write(l, managed_local(l, symbol, range), value, s->type, range);
        return;
    }
    if ((s->type.flags & SCRIPT_TYPE_REFERENCE) && l->locals[symbol] >= 0) {
        int descriptor = value_op(
            l,
            (behavior_function_instruction){ .op = BF_LOCAL,
                                             .type = BF_POINTER_TYPE,
                                             .left = l->locals[symbol] },
            range
        );
        reference_write(l, descriptor, value, s->type, range);
        return;
    }
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
    bool managed = s->type.storage == PIN_STRUCT || s->type.storage == PIN_UNION ||
                   (s->type.flags & SCRIPT_TYPE_BOXED_OPTIONAL) ||
                   s->type.container != PIN_CONTAINER_SINGLE ||
                   behavior_scalar_string(s->type.storage);
    if (managed && mode == STORE_COPY) {
        value = copy_value(l, value, s->type, range);
        value = retain_value(l, value, s->type, range);
    }
    int previous = copied_value(s->type) && mode != STORE_LEND &&
                           (l->variables[symbol] >= 0 ||
                            l->addresses[symbol] >= 0 || l->locals[symbol] >= 0)
                     ? symbol_value(
                           l,
                           &(script_expression){ .kind = SCRIPT_EXPR_SYMBOL,
                                                 .symbol = symbol,
                                                 .type = s->type,
                                                 .range = range }
                       )
                     : -1;
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
        clear_value(l, behavior_function_builder_host_first(l->builder, host), s->type, range);
        drop_value(l, previous, s->type, range);
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
        drop_value(l, previous, s->type, range);
        return;
    }
    if (l->locals[symbol] < 0) {
        l->locals[symbol] = behavior_function_builder_local(
            l->builder,
            (pin_value){ .type = slot_type(s->type) }
        );
        value_root(l, BEHAVIOR_BANK_LOCAL, l->locals[symbol], s->type);
    }
    emit(
        l,
        (behavior_function_instruction){ .op = BF_SAVE,
                                         .type = slot_type(s->type),
                                         .left = value,
                                         .target = l->locals[symbol] },
        range
    );
    drop_value(l, previous, s->type, range);
}
static void assign_value(lower *l, const script_statement *s, int value)
{
    int symbol = s->symbol;
    if (s->expression < 0) {
        /* A `return` stores into its result, which may be wider. */
        if (symbol >= 0 && symbol < l->a->symbol_count) {
            value = convert(
                l,
                value,
                l->a->expressions[s->second].type,
                l->a->symbols[symbol].type,
                s->range
            );
        }
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
    if (target->kind == SCRIPT_EXPR_DEREFERENCE ||
        target->kind == SCRIPT_EXPR_COMPONENT || target->kind == SCRIPT_EXPR_INDEX ||
        (target->kind == SCRIPT_EXPR_MEMBER &&
         (!target->write || native_reference_member(l, target)))) {
        int parent = s->expression;
        while (parent >= 0 &&
               (l->a->expressions[parent].kind == SCRIPT_EXPR_MEMBER ||
                l->a->expressions[parent].kind == SCRIPT_EXPR_DEREFERENCE)) {
            parent = l->a->expressions[parent].left;
        }
        uint64_t component = parent >= 0 && l->a->expressions[parent].kind == SCRIPT_EXPR_COMPONENT
                               ? l->a->expressions[parent].capability
                               : 0;
        if (parent >= 0 && l->a->expressions[parent].kind == SCRIPT_EXPR_SYMBOL &&
            l->a->symbols[l->a->expressions[parent].symbol].kind == SCRIPT_COMPONENT) {
            component = l->a->symbols[l->a->expressions[parent].symbol]
                            .component_type.nominal;
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
    if (s->addressed ||
        (s->type.flags & (SCRIPT_TYPE_REFERENCE | SCRIPT_TYPE_RESULTS)) ||
        (l->captured && l->captured[e->symbol] >= 0)) {
        return false;
    }
    return l->variables[e->symbol] >= 0 ||
           (l->parameters[e->symbol] >= 0 && s->kind != SCRIPT_PARAMETER) ||
           (s->writable && (s->kind == SCRIPT_LOCAL || s->kind == SCRIPT_RESULT));
}
/* A writable local or result array of the function's own, as stage one's
 * cell locals are. */
static bool local_array(lower *l, const script_expression *e)
{
    if (e->kind != SCRIPT_EXPR_SYMBOL || e->symbol < 0) {
        return false;
    }
    const script_symbol *s = &l->a->symbols[e->symbol];
    return s->writable && (s->kind == SCRIPT_LOCAL || s->kind == SCRIPT_RESULT) &&
           s->type.container == PIN_CONTAINER_ARRAY && !s->boxed && !s->addressed &&
           !(s->type.flags & (SCRIPT_TYPE_REFERENCE | SCRIPT_TYPE_RESULTS |
                              SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_BOXED_OPTIONAL)) &&
           l->variables[e->symbol] < 0 && l->parameters[e->symbol] < 0 &&
           !(l->captured && l->captured[e->symbol] >= 0);
}
static int argument_for(const script_expression *e, int parameter)
{
    for (int i = 0; i < e->argument_count; i++) {
        if (e->arguments[i].parameter == parameter) {
            return e->arguments[i].value;
        }
    }
    return -1;
}
/* `xs = capability(xs, value)` for an array capability named by its stable
 * identity. The value must not write: the copying form reads `xs` first. */
static bool self_call(lower *l, const script_expression *e, int symbol, const char *capability)
{
    if (e->kind != SCRIPT_EXPR_CALL || e->argument_count != 2 || e->right >= 0 ||
        e->capability != script_catalog_id("capability", capability)) {
        return false;
    }
    int array = argument_for(e, 0), value = argument_for(e, 1);
    return array >= 0 && value >= 0 &&
           l->a->expressions[array].kind == SCRIPT_EXPR_SYMBOL &&
           l->a->expressions[array].symbol == symbol &&
           !(l->a->expressions[value].effects & SCRIPT_EFFECT_WRITE);
}
/* `xs = append(xs, x)`, `xs = extend(xs, ys)` and `xs[i] = x` on a local
 * update its array in place, copying only a payload another value shares, so
 * building an array is linear rather than quadratic. */
static bool in_place_local(lower *l, const script_statement *s)
{
    if (s->op != SCRIPT_OP_NONE) {
        return false;
    }
    const script_expression *target = &l->a->expressions[s->expression],
                            *rhs = &l->a->expressions[s->second];
    bool owned = local_array(l, target);
    bool push = owned && self_call(l, rhs, target->symbol, "Append");
    bool extend = owned && self_call(l, rhs, target->symbol, "Extend");
    bool store = target->kind == SCRIPT_EXPR_INDEX &&
                 local_array(l, &l->a->expressions[target->left]);
    if (!push && !extend && !store) {
        return false;
    }
    int symbol = l->a->expressions[store ? target->left : s->expression].symbol;
    script_type item = l->a->symbols[symbol].type;
    if (!extend) {
        item.container = PIN_CONTAINER_SINGLE;
    }
    pin_value headers[3] = { { .type = BF_POINTER_TYPE }, { .type = PIN_I32 } };
    int slots[3], array = store ? -1 : argument_for(rhs, 0),
                  value = store ? s->second : argument_for(rhs, 1),
                  at = store ? 2 : 1;
    /* The statement keeps the call's debugger points and watches, and the
     * target's assignment point follows the value as store_symbol's does. */
    debug_point(l, SCRIPT_RECORD_EXPRESSION, s->expression, target->range);
    if (store) {
        slots[1] = int_slot(l, target->right, s->range);
    } else {
        debug_point(l, SCRIPT_RECORD_EXPRESSION, s->second, rhs->range);
        debug_input(l, rhs, 0, expression(l, array), l->a->symbols[symbol].type);
    }
    slots[at] = convert(l, expression(l, value), l->a->expressions[value].type, item, s->range);
    headers[at] = initial(l, item);
    if (!store) {
        debug_input(l, rhs, 1, slots[at], item);
    }
    debug_point(l, SCRIPT_RECORD_SYMBOL, symbol, s->range);
    /* The address follows every branch the index and value take, so effect
     * analysis traces it straight to the local. */
    slots[0] = value_op(
        l,
        (behavior_function_instruction){ .op = BF_HOST_SLOT,
                                         .type = BF_POINTER_TYPE,
                                         .left = local_address(l, symbol) },
        s->range
    );
    primitive(
        l,
        push     ? &EXECUTION_array_push_call
        : extend ? &EXECUTION_array_extend_call
                 : &EXECUTION_array_store_call,
        &EXECUTION_assignment_access,
        headers,
        slots,
        at + 1,
        (script_type){ .storage = PIN_BOOL },
        s->range
    );
    if (!store) {
        int result = expression_value(l, array);
        value_root(l, BEHAVIOR_BANK_SLOT, result, rhs->type);
        debug_watch(l, s->second, result, rhs->type, true, 0);
    }
    return true;
}
/* Indexed writes retain a logical destination through index and RHS calls. */
static bool in_place_array(lower *l, const script_statement *s)
{
    const script_expression *target = &l->a->expressions[s->expression];
    if (target->kind != SCRIPT_EXPR_INDEX) {
        return false;
    }
    script_type sequence = l->a->expressions[target->left].type;
    if (sequence.container != PIN_CONTAINER_ARRAY &&
        sequence.container != PIN_CONTAINER_VIEW) {
        return false;
    }
    int destination = reference(l, target->left);
    int index = int_slot(l, target->right, s->range);
    /* Reading the element checks the index before the value is evaluated. */
    int receiver = reference_read(l, destination, sequence, s->range);
    int before = sequence_item(l, receiver, index, sequence, target->type, s->op == SCRIPT_OP_NONE, s->range);
    int value = convert(
        l,
        expression(l, s->second),
        l->a->expressions[s->second].type,
        target->type,
        s->range
    );
    if (s->op != SCRIPT_OP_NONE) {
        value = binary(l, s->op, before, value, target->type, target->type, s->range);
    }
    /* The value can shrink the same array through an alias, and the
     * permissive set_array_elem method ignores a missing index, so the
     * index is checked again where it is written. */
    receiver = reference_read(l, destination, sequence, s->range);
    sequence_item(l, receiver, index, sequence, target->type, true, s->range);
    pin_value headers[] = { { .type = BF_POINTER_TYPE },
                            { .type = PIN_I32, .i = 4 },
                            { .type = PIN_I32 },
                            initial(l, target->type) };
    int slots[] = { destination, -1, index, value };
    primitive(
        l,
        &EXECUTION_array_mutate_place_call,
        &EXECUTION_assignment_access,
        headers,
        slots,
        4,
        (script_type){ .storage = PIN_BOOL },
        s->range
    );
    return true;
}
/* A deletion resolves the same logical map place after evaluating its key. */
static bool in_place_map(lower *l, const script_statement *s)
{
    const script_expression *target = &l->a->expressions[s->expression];
    const script_expression *rhs = &l->a->expressions[s->second];
    if (s->op != SCRIPT_OP_NONE || rhs->kind != SCRIPT_EXPR_MAP_REMOVE ||
        target->type.container != PIN_CONTAINER_MAP) {
        return false;
    }
    script_type key = script_catalog_key_type(l->catalog, target->type);
    int destination = reference(l, s->expression);
    int value = expression(l, rhs->right);
    value = convert(l, value, l->a->expressions[rhs->right].type, key, s->range);
    pin_value headers[] = { { .type = BF_POINTER_TYPE }, initial(l, key) };
    int slots[] = { destination, value };
    primitive(
        l,
        &EXECUTION_map_delete_place_call,
        &EXECUTION_assignment_access,
        headers,
        slots,
        2,
        (script_type){ .storage = PIN_BOOL },
        s->range
    );
    return true;
}
static void assign(lower *l, const script_statement *s)
{
    int index = s->expression;
    const script_expression *target = index >= 0 ? &l->a->expressions[index] : NULL;
    const script_catalog_capability *native = target
                                                ? native_member_access(l, target, true)
                                                : NULL;
    const script_expression *receiver = native ? &l->a->expressions[target->left]
                                               : NULL;
    if (native && s->op == SCRIPT_OP_NONE &&
        receiver->kind == SCRIPT_EXPR_COMPONENT && receiver->left >= 0) {
        script_catalog_parameter parameters[] = {
            { .type = l->a->expressions[receiver->left].type },
            { .type = target->type }
        };
        script_catalog_capability access = *native;
        access.parameters = parameters;
        access.parameter_count = 2;
        access.component_entity = 0;
        access.component_value = 1;
        script_argument arguments[] = {
            { .value = receiver->left, .parameter = 0, .range = s->range },
            { .value = s->second, .parameter = 1, .range = s->range }
        };
        script_expression selected = *target;
        selected.kind = SCRIPT_EXPR_CALL;
        selected.type = native->result_count == 1 ? native->results[0].type
                                                  : target->type;
        selected.arguments = arguments;
        selected.argument_count = 2;
        component_call_mode(l, &selected, &access, false);
        return;
    }
    if (index < 0 ||
        (l->a->expressions[index].kind == SCRIPT_EXPR_MEMBER &&
         l->a->expressions[index].write && !native_reference_member(l, target))) {
        assign_value(l, s, expression(l, s->second));
        return;
    }
    if (l->a->expressions[index].kind == SCRIPT_EXPR_SYMBOL &&
        (l->a->expressions[index].type.flags & SCRIPT_TYPE_RESULTS)) {
        store_tuple(l, l->a->expressions[index].symbol, expression(l, s->second), s->second, s->range);
        return;
    }
    if (in_place_local(l, s) || in_place_array(l, s) || in_place_map(l, s)) {
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
                                : target_read(l, destination, index, s->range);
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
static void reduce(lower *l, const script_statement *s)
{
    const script_expression *target = &l->a->expressions[s->expression];
    l->reduction_target = true;
    int destination = reference(l, s->expression);
    l->reduction_target = false;
    int value = convert(
        l,
        expression(l, s->second),
        l->a->expressions[s->second].type,
        target->type,
        s->range
    );
    int op = s->reduction == SCRIPT_REDUCTION_MIN ? EXECUTION_REDUCTION_MIN
           : s->reduction == SCRIPT_REDUCTION_MAX ? EXECUTION_REDUCTION_MAX
           : s->reduction == SCRIPT_REDUCTION_ADD_UNORDERED
               ? EXECUTION_REDUCTION_ADD_UNORDERED
               : EXECUTION_REDUCTION_ADD;
    pin_value headers[] = { { .type = BF_POINTER_TYPE },
                            initial(l, target->type),
                            { .type = PIN_I32, .i = op } };
    int slots[] = { destination, value, -1 };
    primitive(
        l,
        &EXECUTION_reference_reduce_call,
        &EXECUTION_assignment_access,
        headers,
        slots,
        3,
        (script_type){ .storage = PIN_BOOL },
        s->range
    );
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
static int query_iterator_host(lower *, const node_host_call *, int, script_type, int *, script_range);
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
        value = copy_value(l, value, s->type, range);
        if (initial(l, s->type).record ||
            (!(s->type.flags & SCRIPT_TYPE_BOXED_OPTIONAL) && s->type.storage == PIN_STRUCT &&
             s->type.container == PIN_CONTAINER_SINGLE)) {
            return_record(l, value, s->type, i, range);
        } else if (!(s->type.flags & SCRIPT_TYPE_BOXED_OPTIONAL) && s->type.container != PIN_CONTAINER_SINGLE) {
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
        } else {
            emit(
                l,
                (behavior_function_instruction){ .op = BF_RESULT,
                                                 .type = slot_type(s->type),
                                                 .left = value,
                                                 .target = i },
                range
            );
        }
    }
    forget_temporaries(l);
    for (int i = acount(l->loops) - 1; i >= 0; i--) {
        if (l->loops[i].query_cursor) {
            int cursor = value_op(
                l,
                (behavior_function_instruction){ .op = BF_LOCAL,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = l->loops[i].query_cursor - 1 },
                range
            );
            query_iterator_host(l, &EXECUTION_query_end_call, cursor, (script_type){ 0 }, NULL, range);
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
static int query_iterator_host(
    lower *l,
    const node_host_call *call,
    int iterator,
    script_type row_type,
    int *row,
    script_range range
)
{
    pin_value headers[] = { { .type = BF_POINTER_TYPE }, { .type = BF_POINTER_TYPE } };
    pin_value outputs[] = { { .type = PIN_BOOL }, initial(l, row_type) };
    int output_count = row ? 2 : 1;
    int host = linked_host(l->builder, call, &EXECUTION_component_read_access, headers, 2, outputs, output_count);
    int first = behavior_function_builder_host_first(l->builder, host);
    set_kind(l, BEHAVIOR_BANK_DATA, first + 1, BEHAVIOR_QUERY_CURSOR);
    host_argument(l, first, query_context(l, range), (script_type){ .storage = BF_POINTER_TYPE }, range);
    host_argument(l, first + 1, iterator, (script_type){ .storage = BF_POINTER_TYPE }, range);
    emit(
        l,
        (behavior_function_instruction){ .op = BF_HOST_CALL,
                                         .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                         .target = host },
        range
    );
    int result = behavior_function_builder_host_results(l->builder, host);
    if (row) {
        *row = take_result(l, result + 1, row_type, range);
    }
    return host_result(l, result, (script_type){ .storage = PIN_BOOL }, range);
}
static void query_foreach(lower *l, const script_statement *s)
{
    const script_expression *source = &l->a->expressions[s->expression];
    script_type row_type = source->type;
    row_type.flags = SCRIPT_TYPE_QUERY_ROW;
    pin_value headers[] = { { .type = BF_POINTER_TYPE }, initial(l, source->type) };
    pin_value output = { .type = BF_POINTER_TYPE };
    int host = linked_host(
        l->builder,
        &EXECUTION_query_begin_call,
        &EXECUTION_component_read_access,
        headers,
        2,
        &output,
        1
    );
    int first = behavior_function_builder_host_first(l->builder, host);
    host_argument(l, first, query_context(l, s->range), (script_type){ .storage = BF_POINTER_TYPE }, s->range);
    host_argument(l, first + 1, expression(l, s->expression), source->type, s->range);
    int bank = behavior_function_builder_host_results(l->builder, host);
    set_kind(l, BEHAVIOR_BANK_DATA, bank, BEHAVIOR_QUERY_CURSOR);
    emit(
        l,
        (behavior_function_instruction){ .op = BF_HOST_CALL,
                                         .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                         .target = host },
        s->range
    );
    int iterator = host_result(l, bank, (script_type){ .storage = BF_POINTER_TYPE }, s->range);
    int saved = behavior_function_builder_local(l->builder, (pin_value){ .type = BF_POINTER_TYPE });
    set_kind(l, BEHAVIOR_BANK_LOCAL, saved, BEHAVIOR_QUERY_CURSOR);
    emit(
        l,
        (
            behavior_function_instruction
        ){ .op = BF_SAVE, .type = BF_POINTER_TYPE, .left = iterator, .target = saved },
        s->range
    );
    int again = l->pc;
    emit(l, (behavior_function_instruction){ .op = BF_CHECK }, s->range);
    iterator = value_op(
        l,
        (behavior_function_instruction){ .op = BF_LOCAL,
                                         .type = BF_POINTER_TYPE,
                                         .left = saved },
        s->range
    );
    int row = -1;
    int condition = query_iterator_host(l, &EXECUTION_query_next_call, iterator, row_type, &row, s->range);
    int end = emit(
        l,
        (behavior_function_instruction){ .op = BF_BRANCH,
                                         .left = query_not(l, condition, s->range),
                                         .target = -1 },
        s->range
    );
    store_symbol(l, s->symbol, row, s->range);
    apush(
        l->loops,
        ((lower_loop){ .scope = l->a->scopes[l->a->statements[s->body].scope].parent,
                       .query_cursor = saved + 1 })
    );
    statement(l, s->body);
    emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = again }, s->range);
    behavior_function_builder_patch_target(l->builder, end, l->pc);
    loop_finish(l, again, l->pc);
    iterator = value_op(
        l,
        (behavior_function_instruction){ .op = BF_LOCAL,
                                         .type = BF_POINTER_TYPE,
                                         .left = saved },
        s->range
    );
    query_iterator_host(l, &EXECUTION_query_end_call, iterator, row_type, NULL, s->range);
}
static void statement_inner(lower *l, int index)
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
            leave_scope(l, s->scope, s->range);
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
                int condition;
                if (l->a->expressions[arm->expression].kind == SCRIPT_EXPR_IS) {
                    bool old_cached = l->pattern_cached;
                    int old_expression = l->pattern_expression,
                        old_value = l->pattern_value;
                    l->pattern_cached = true;
                    l->pattern_expression = s->expression;
                    l->pattern_value = value;
                    condition = expression(l, arm->expression);
                    l->pattern_cached = old_cached;
                    l->pattern_expression = old_expression;
                    l->pattern_value = old_value;
                } else {
                    int candidate = expression(l, arm->expression);
                    candidate = convert(
                        l,
                        candidate,
                        l->a->expressions[arm->expression].type,
                        type,
                        arm->range
                    );
                    condition = binary(
                        l,
                        SCRIPT_OP_EQ,
                        value,
                        candidate,
                        (script_type){ .storage = PIN_BOOL },
                        type,
                        arm->range
                    );
                }
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
            int produced = borrowed_place(l, s->expression, lends_declaration(l, s));
            int value = convert(
                l,
                produced,
                l->a->expressions[s->expression].type,
                l->a->symbols[s->symbol].type,
                s->range
            );
            if (l->a->symbols[s->symbol].type.flags & SCRIPT_TYPE_RESULTS) {
                release_tuple(l, s->symbol, s->range);
                store_tuple(l, s->symbol, value, s->expression, s->range);
            } else {
                declare_value(l, s->symbol, value, declaration_mode(l, s, value == produced), s->range);
            }
            return;
        }
        case SCRIPT_STMT_ASSIGN: assign(l, s); return;
        case SCRIPT_STMT_REDUCE: reduce(l, s); return;
        case SCRIPT_STMT_EXPRESSION: {
            int value = expression(l, s->expression);
            discard_call_results(l, &l->a->expressions[s->expression], value);
            return;
        }
        case SCRIPT_STMT_IF: {
            int first = acount(l->temporaries);
            int condition = condition_value(l, s->expression);
            forget_expression(l, s->expression);
            forget_conversions(l, first);
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
            int first = acount(l->temporaries);
            int condition = condition_value(l, s->expression);
            forget_expression(l, s->expression);
            forget_conversions(l, first);
            int branch = emit(
                l,
                (behavior_function_instruction){ .op = BF_BRANCH,
                                                 .left = condition,
                                                 .target = -1 },
                s->range
            );
            int end = emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = -1 }, s->range);
            behavior_function_builder_patch_target(l->builder, branch, l->pc);
            apush(
                l->loops,
                ((lower_loop){
                    .scope = l->a->scopes[l->a->statements[s->body].scope].parent })
            );
            statement(l, s->body);
            emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = again }, s->range);
            behavior_function_builder_patch_target(l->builder, end, l->pc);
            loop_finish(l, again, l->pc);
            return;
        }
        case SCRIPT_STMT_FOR: {
            int first = int_slot(l, s->expression, s->range),
                last = int_slot(l, s->second, s->range);
            /* `for <` counts down from last, `..<` stops before it. A reversed
             * half-open range starts at last - 1 only after first < last proves
             * that cannot wrap. Equality ends an inclusive range before its
             * step, so neither INT_MAX nor INT_MIN wraps. */
            int start = s->reverse ? last : first, stop = s->reverse ? first : last;
            int empty = -1;
            if (s->reverse && s->exclusive) {
                int nonempty = value_op(
                    l,
                    (
                        behavior_function_instruction
                    ){ .op = BF_LT, .type = PIN_I32, .left = first, .right = last },
                    s->range
                );
                int enter = emit(
                    l,
                    (behavior_function_instruction){ .op = BF_BRANCH,
                                                     .left = nonempty,
                                                     .target = -1 },
                    s->range
                );
                empty = emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = -1 }, s->range);
                behavior_function_builder_patch_target(l->builder, enter, l->pc);
                int one = constant(l, (pin_value){ .type = PIN_I32, .i = 1 }, s->range);
                start = value_op(
                    l,
                    (
                        behavior_function_instruction
                    ){ .op = BF_SUB, .type = PIN_I32, .left = last, .right = one },
                    s->range
                );
            }
            bool counted = !s->exclusive || s->reverse;
            store_symbol(l, s->symbol, start, s->range);
            int bound = behavior_function_builder_local(l->builder, (pin_value){ .type = PIN_I32 });
            emit(
                l,
                (
                    behavior_function_instruction
                ){ .op = BF_SAVE, .type = PIN_I32, .left = stop, .target = bound },
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
            int limit = value_op(
                l,
                (behavior_function_instruction){ .op = BF_LOCAL,
                                                 .type = PIN_I32,
                                                 .left = bound },
                s->range
            );
            int condition = value_op(
                l,
                (behavior_function_instruction){ .op = s->reverse ? BF_GE
                                                     : counted    ? BF_LE
                                                                  : BF_LT,
                                                 .type = PIN_I32,
                                                 .left = current,
                                                 .right = limit },
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
            apush(
                l->loops,
                ((lower_loop){
                    .scope = l->a->scopes[l->a->statements[s->body].scope].parent })
            );
            statement(l, s->body);
            int advance = l->pc;
            current = value_op(
                l,
                (behavior_function_instruction){ .op = BF_LOCAL,
                                                 .type = PIN_I32,
                                                 .left = l->locals[s->symbol] },
                s->range
            );
            int at_bound = -1;
            if (counted) {
                limit = value_op(
                    l,
                    (behavior_function_instruction){ .op = BF_LOCAL,
                                                     .type = PIN_I32,
                                                     .left = bound },
                    s->range
                );
                int done = value_op(
                    l,
                    (
                        behavior_function_instruction
                    ){ .op = BF_EQ, .type = PIN_I32, .left = current, .right = limit },
                    s->range
                );
                at_bound = emit(
                    l,
                    (behavior_function_instruction){ .op = BF_BRANCH,
                                                     .left = done,
                                                     .target = -1 },
                    s->range
                );
            }
            int one = constant(l, (pin_value){ .type = PIN_I32, .i = 1 }, s->range);
            int next = value_op(
                l,
                (behavior_function_instruction){ .op = s->reverse ? BF_SUB : BF_ADD,
                                                 .type = PIN_I32,
                                                 .left = current,
                                                 .right = one },
                s->range
            );
            store_symbol(l, s->symbol, next, s->range);
            emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = again }, s->range);
            behavior_function_builder_patch_target(l->builder, end, l->pc);
            if (at_bound >= 0) {
                behavior_function_builder_patch_target(l->builder, at_bound, l->pc);
            }
            if (empty >= 0) {
                behavior_function_builder_patch_target(l->builder, empty, l->pc);
            }
            loop_finish(l, advance, l->pc);
            return;
        }
        case SCRIPT_STMT_FOREACH: {
            if (l->a->expressions[s->expression].type.flags & SCRIPT_TYPE_QUERY) {
                query_foreach(l, s);
                return;
            }
            const script_expression *source = &l->a->expressions[s->expression];
            bool streaming = source->kind == SCRIPT_EXPR_RELATION;
            script_type collection = source->type;
            bool borrowed = (l->a->symbols[s->symbol].type.flags &
                             SCRIPT_TYPE_REFERENCE) != 0;
            bool map = collection.container == PIN_CONTAINER_MAP;
            bool text = collection.container == PIN_CONTAINER_SINGLE &&
                        collection.storage == PIN_STR;
            pin_type stored_type = streaming ? PIN_HANDLE
                                 : text      ? BF_POINTER_TYPE
                                             : slot_type(collection);
            int array = streaming
                          ? expression(l, source->left)
                          : retain_value(l, expression(l, s->expression), collection, s->range);
            int original = array;
            /* A relationship streams its targets from the entity; it has no
             * collection to copy. */
            bool sequence = !streaming &&
                            (collection.container == PIN_CONTAINER_ARRAY ||
                             collection.container == PIN_CONTAINER_VIEW);
            int fixed_count = -1;
            if (sequence && borrowed) {
                fixed_count = save_slot(
                    l,
                    sequence_count(l, original, collection, s->range),
                    PIN_I32,
                    s->range
                );
            } else if ((sequence && collection.container == PIN_CONTAINER_ARRAY) || map) {
                array = copy_value(l, original, collection, s->range);
            } else if (sequence) {
                pin_value header = initial(l, collection);
                collection.container = PIN_CONTAINER_ARRAY;
                array = primitive(
                    l,
                    &EXECUTION_sequence_clone_call,
                    &EXECUTION_sequence_read_access,
                    &header,
                    &original,
                    1,
                    collection,
                    s->range
                );
            }
            forget_expression(l, s->expression);
            if (text) {
                array = string_cursor(
                    l,
                    &EXECUTION_string_cursor_call,
                    array,
                    -1,
                    (script_type){ .storage = BF_POINTER_TYPE },
                    s->range
                );
            }
            int saved_array = behavior_function_builder_local(
                l->builder,
                (pin_value){ .type = stored_type }
            );
            if (!streaming && !text) {
                value_root(l, BEHAVIOR_BANK_LOCAL, saved_array, collection);
            }
            emit(
                l,
                (behavior_function_instruction){ .op = BF_SAVE,
                                                 .type = stored_type,
                                                 .left = array,
                                                 .target = saved_array },
                s->range
            );
            int text_bytes = -1;
            int saved_reference = -1;
            if (borrowed) {
                /* The body cannot reach the source but through item, so a
                 * reference to it resolves the same array on each access. */
                int descriptor = reference(l, s->expression);
                saved_reference = behavior_function_builder_local(
                    l->builder,
                    (pin_value){ .type = BF_POINTER_TYPE }
                );
                set_kind(l, BEHAVIOR_BANK_LOCAL, saved_reference, BEHAVIOR_REFERENCE);
                emit(
                    l,
                    (behavior_function_instruction){ .op = BF_SAVE,
                                                     .type = BF_POINTER_TYPE,
                                                     .left = descriptor,
                                                     .target = saved_reference },
                    s->range
                );
            }
            if (text) {
                int bytes = string_cursor(
                    l,
                    &EXECUTION_string_bytes_call,
                    array,
                    -1,
                    (script_type){ .storage = PIN_I32 },
                    s->range
                );
                text_bytes = behavior_function_builder_local(l->builder, (pin_value){ .type = PIN_I32 });
                emit(
                    l,
                    (behavior_function_instruction){ .op = BF_SAVE,
                                                     .type = PIN_I32,
                                                     .left = bytes,
                                                     .target = text_bytes },
                    s->range
                );
            }
            int ordinal = behavior_function_builder_local(l->builder, (pin_value){ .type = PIN_I32 });
            /* `for <` visits an array from its last item down to index 0. */
            int zero = constant(l, (pin_value){ .type = PIN_I32 }, s->range),
                start = zero;
            if (s->reverse) {
                int one = constant(l, (pin_value){ .type = PIN_I32, .i = 1 }, s->range);
                start = value_op(
                    l,
                    (behavior_function_instruction){
                        .op = BF_SUB,
                        .type = PIN_I32,
                        .left = fixed_count >= 0
                                  ? local_slot(l, fixed_count, PIN_I32, s->range)
                                  : sequence_count(l, array, collection, s->range),
                        .right = one },
                    s->range
                );
            }
            emit(
                l,
                (
                    behavior_function_instruction
                ){ .op = BF_SAVE, .type = PIN_I32, .left = start, .target = ordinal },
                s->range
            );
            int again = l->pc;
            array = value_op(
                l,
                (behavior_function_instruction){ .op = BF_LOCAL,
                                                 .type = stored_type,
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
            int item = -1, condition;
            if (streaming) {
                pin_value headers[] = { { .type = BF_POINTER_TYPE },
                                        { .type = PIN_HANDLE },
                                        { .type = PIN_U64, .u64 = source->capability },
                                        { .type = PIN_I32 } };
                int slots[] = { value_op(
                                    l,
                                    (behavior_function_instruction){ .op = BF_CONTEXT,
                                                                     .type = BF_POINTER_TYPE,
                                                                     .target = 1 },
                                    s->range
                                ),
                                array,
                                -1,
                                index };
                item = primitive(
                    l,
                    &EXECUTION_query_target_call,
                    &EXECUTION_component_read_access,
                    headers,
                    slots,
                    4,
                    (script_type){ .storage = PIN_HANDLE },
                    s->range
                );
                int absent = constant(l, (pin_value){ .type = PIN_HANDLE }, s->range);
                condition = value_op(
                    l,
                    (
                        behavior_function_instruction
                    ){ .op = BF_NE, .type = PIN_HANDLE, .left = item, .right = absent },
                    s->range
                );
            } else {
                int count = text ? value_op(
                                       l,
                                       (behavior_function_instruction){
                                           .op = BF_LOCAL,
                                           .type = PIN_I32,
                                           .left = text_bytes },
                                       s->range
                                   )
                          : map  ? value_op(
                                       l,
                                       (behavior_function_instruction){
                                           .op = BF_LOAD,
                                           .type = PIN_I32,
                                           .left = array,
                                           .offset = offsetof(pin_map, count) },
                                       s->range
                                   )
                          : fixed_count >= 0
                              ? local_slot(l, fixed_count, PIN_I32, s->range)
                              : sequence_count(l, array, collection, s->range);
                condition = value_op(
                    l,
                    (behavior_function_instruction){ .op = s->reverse ? BF_GE : BF_LT,
                                                     .type = PIN_I32,
                                                     .left = index,
                                                     .right = s->reverse ? zero : count },
                    s->range
                );
            }
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
            /* The loop iterates its own copy of the array, so a fixed item
             * reads its element in place. */
            bool lent = sequence && !borrowed && copied_value(element) &&
                        symbol_fixed(l, s->symbol);
            if (borrowed) {
                pin_value headers[] = { { .type = BF_POINTER_TYPE },
                                        initial(l, element),
                                        { .type = PIN_I32 },
                                        { .type = PIN_BOOL, .b = true } };
                int slots[] = { value_op(
                                    l,
                                    (behavior_function_instruction){
                                        .op = BF_LOCAL,
                                        .type = BF_POINTER_TYPE,
                                        .left = saved_reference },
                                    s->range
                                ),
                                -1,
                                index,
                                -1 };
                int descriptor = primitive(
                    l,
                    &EXECUTION_reference_field_call,
                    &EXECUTION_value_access,
                    headers,
                    slots,
                    4,
                    (script_type){ .storage = BF_POINTER_TYPE },
                    s->range
                );
                if (l->locals[s->symbol] < 0) {
                    l->locals[s->symbol] = behavior_function_builder_local(
                        l->builder,
                        (pin_value){ .type = BF_POINTER_TYPE }
                    );
                    set_kind(l, BEHAVIOR_BANK_LOCAL, l->locals[s->symbol], BEHAVIOR_REFERENCE);
                }
                emit(
                    l,
                    (behavior_function_instruction){ .op = BF_SAVE,
                                                     .type = BF_POINTER_TYPE,
                                                     .left = descriptor,
                                                     .target = l->locals[s->symbol] },
                    s->range
                );
            } else {
                declare_value(
                    l,
                    s->symbol,
                    streaming ? item
                    : text
                        ? string_cursor(l, &EXECUTION_string_item_call, array, index, element, s->range)
                    : map
                        ? map_item(l, array, index, false, collection, element, s->range)
                        : sequence_item(l, array, index, collection, element, lent, s->range),
                    lent ? STORE_LEND : STORE_COPY,
                    s->range
                );
            }
            if (map && s->key_symbol >= 0) {
                declare_symbol(
                    l,
                    s->key_symbol,
                    map_item(
                        l,
                        array,
                        index,
                        true,
                        collection,
                        l->a->symbols[s->key_symbol].type,
                        s->range
                    ),
                    s->range
                );
            } else if (s->key_symbol >= 0) {
                declare_symbol(l, s->key_symbol, index, s->range);
            }
            apush(
                l->loops,
                ((lower_loop){
                    .scope = l->a->scopes[l->a->statements[s->body].scope].parent })
            );
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
            int next = text ? string_cursor(
                                  l,
                                  &EXECUTION_string_next_call,
                                  value_op(
                                      l,
                                      (behavior_function_instruction){
                                          .op = BF_LOCAL,
                                          .type = BF_POINTER_TYPE,
                                          .left = saved_array },
                                      s->range
                                  ),
                                  index,
                                  (script_type){ .storage = PIN_I32 },
                                  s->range
                              )
                            : value_op(
                                  l,
                                  (behavior_function_instruction){
                                      .op = s->reverse ? BF_SUB : BF_ADD,
                                      .type = PIN_I32,
                                      .left = index,
                                      .right = one },
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
            leave_scope(l, s->scope, s->range);
            if (!borrowed && !streaming && !text) {
                drop_value(l, local_slot(l, saved_array, stored_type, s->range), collection, s->range);
            }
            return;
        }
        case SCRIPT_STMT_BREAK:
        case SCRIPT_STMT_CONTINUE: {
            assert(acount(l->loops));
            lower_loop *loop = &l->loops[acount(l->loops) - 1];
            leave_scopes(l, s->scope, loop->scope, s->range);
            forget_temporaries(l);
            int pc = emit(l, (behavior_function_instruction){ .op = BF_JUMP, .target = -1 }, s->range);
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
            leave_scopes(l, s->scope, l->a->symbols[s->symbol].scope, s->range);
            forget_temporaries(l);
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
static void statement(lower *l, int index)
{
    if (index < 0) {
        return;
    }
    int first = acount(l->temporaries);
    statement_inner(l, index);
    const script_statement *s = &l->a->statements[index];
    if (s->kind != SCRIPT_STMT_RETURN && s->kind != SCRIPT_STMT_BREAK &&
        s->kind != SCRIPT_STMT_CONTINUE && s->kind != SCRIPT_STMT_GOTO) {
        forget_expression(l, s->expression);
        forget_expression(l, s->second);
        forget_expression(l, s->third);
        forget_conversions(l, first);
    }
    /* Completed statements have emitted their cleanup. Retain only temporaries
     * owned by enclosing expressions; later returns must not clean every
     * already retired expression in earlier branches again. */
    asetlen(l->temporaries, first);
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
    for (int i = 0; i < module->description_count; i++) {
        behavior_function_free(module->descriptions[i].evaluate);
        module->descriptions[i].evaluate = NULL;
    }
    for (int i = 0; i < module->query_count; i++) {
        behavior_function_free(module->queries[i].predicate);
        module->queries[i].predicate = NULL;
    }
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
    return id ^ document_id(analysis, function->range);
}
static void dependency_expressions(lower *l, int index, bool *visited, int **out)
{
    if (index < 0 || visited[index]) {
        return;
    }
    visited[index] = true;
    const script_expression *e = &l->a->expressions[index];
    if (e->kind == SCRIPT_EXPR_MEMBER || e->kind == SCRIPT_EXPR_INDEX ||
        e->kind == SCRIPT_EXPR_COMPONENT || e->kind == SCRIPT_EXPR_DEREFERENCE) {
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
        case SCRIPT_EXPR_SEQUENCE:
            dependency_expressions(l, e->left, visited, out);
            dependency_expressions(l, e->right, visited, out);
            for (int i = 0; i < e->argument_count; i++) {
                dependency_expressions(l, e->arguments[i].value, visited, out);
            }
            return;
        case SCRIPT_EXPR_CALL:
            if ((e->effects & (SCRIPT_EFFECT_READ | SCRIPT_EFFECT_INVOCATION_READ)) &&
                !(e->type.flags & SCRIPT_TYPE_RESULTS)) {
                apush(*out, index);
                return;
            }
            /* Fall through to the parameter expressions of a pure call. */
        case SCRIPT_EXPR_ARRAY:
        case SCRIPT_EXPR_MAP:
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
                             l->a->expressions[root].kind == SCRIPT_EXPR_INDEX ||
                             l->a->expressions[root].kind == SCRIPT_EXPR_DEREFERENCE)) {
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
            nominal = l->a->symbols[base->symbol].component_type.nominal;
            any_entity = l->a->symbols[base->symbol].source_relationship != 0 ||
                         l->a->symbols[base->symbol].source_variable != 0;
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
            const script_catalog_type *type = script_catalog_type_by_id(
                l->catalog,
                symbol->component_type.nominal
            );
            if (type && type->component_id) {
                field_access(
                    binding,
                    type->component_id,
                    0,
                    type->size,
                    symbol->source_relationship != 0 || symbol->source_variable != 0
                );
            }
        }
        return;
    }
    if (e->kind == SCRIPT_EXPR_SEQUENCE) {
        binding_fields(l, binding, e->left, visited);
        binding_fields(l, binding, e->right, visited);
        for (int i = 0; i < e->argument_count; i++) {
            binding_fields(l, binding, e->arguments[i].value, visited);
        }
        return;
    }
    if (e->kind == SCRIPT_EXPR_CAST || e->kind == SCRIPT_EXPR_UNARY ||
        e->kind == SCRIPT_EXPR_DEREFERENCE || e->kind == SCRIPT_EXPR_FALLBACK ||
        e->kind == SCRIPT_EXPR_BINARY) {
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
static int query_context(lower *l, script_range range)
{
    return value_op(
        l,
        (behavior_function_instruction){ .op = BF_CONTEXT,
                                         .type = BF_POINTER_TYPE,
                                         .target = 1 },
        range
    );
}
static int query_not(lower *l, int value, script_range range)
{
    int zero = constant(l, (pin_value){ .type = PIN_BOOL }, range);
    return binary(
        l,
        SCRIPT_OP_EQ,
        value,
        zero,
        (script_type){ .storage = PIN_BOOL },
        (script_type){ .storage = PIN_BOOL },
        range
    );
}
static int query_condition(lower *l, int index)
{
    const script_query *query = &l->a->queries[index];
    script_range range = query->range;
    if (query->kind == SCRIPT_QUERY_VALUE) {
        return condition_value(l, query->value);
    }
    if (query->kind == SCRIPT_QUERY_GENERATOR) {
        return constant(l, (pin_value){ .type = PIN_BOOL, .b = true }, range);
    }
    if (query->kind == SCRIPT_QUERY_NOT) {
        return query_not(l, query_condition(l, query->children[0]), range);
    }
    if (query->kind == SCRIPT_QUERY_ALL || query->kind == SCRIPT_QUERY_ANY) {
        bool any = query->kind == SCRIPT_QUERY_ANY;
        int result = behavior_function_builder_local(l->builder, (pin_value){ .type = PIN_BOOL });
        int initial = constant(l, (pin_value){ .type = PIN_BOOL, .b = !any }, range);
        emit(
            l,
            (
                behavior_function_instruction
            ){ .op = BF_SAVE, .type = PIN_BOOL, .left = initial, .target = result },
            range
        );
        int *exits = NULL;
        for (int i = 0; i < query->child_count; i++) {
            int value = query_condition(l, query->children[i]);
            emit(
                l,
                (
                    behavior_function_instruction
                ){ .op = BF_SAVE, .type = PIN_BOOL, .left = value, .target = result },
                range
            );
            if (!any) {
                value = query_not(l, value, range);
            }
            apush(
                exits,
                emit(l, (behavior_function_instruction){ .op = BF_BRANCH, .left = value, .target = -1 }, range)
            );
        }
        for (int i = 0; i < acount(exits); i++) {
            behavior_function_builder_patch_target(l->builder, exits[i], l->pc);
        }
        afree(exits);
        return value_op(
            l,
            (behavior_function_instruction){ .op = BF_LOCAL, .type = PIN_BOOL, .left = result },
            range
        );
    }
    if (query->kind == SCRIPT_QUERY_CHANGED) {
        pin_value headers[] = { { .type = BF_POINTER_TYPE },
                                { .type = PIN_I32, .i = index } };
        int slots[] = { query_context(l, range), -1 };
        return primitive(
            l,
            &EXECUTION_query_test_call,
            &EXECUTION_component_read_access,
            headers,
            slots,
            2,
            (script_type){ .storage = PIN_BOOL },
            range
        );
    }
    int owner = query_variable_value(l, query->source_variable, range);
    int entity;
    if (query->kind == SCRIPT_QUERY_RELATION) {
        pin_value headers[] = { { .type = BF_POINTER_TYPE },
                                { .type = PIN_HANDLE },
                                { .type = PIN_U64, .u64 = query->component },
                                { .type = PIN_I32 } };
        int slots[] = { query_context(l, range), owner, -1, -1 };
        entity = primitive(
            l,
            &EXECUTION_query_target_call,
            &EXECUTION_component_read_access,
            headers,
            slots,
            4,
            (script_type){ .storage = PIN_HANDLE },
            range
        );
    } else {
        pin_value headers[] = { { .type = BF_POINTER_TYPE },
                                { .type = PIN_HANDLE },
                                { .type = PIN_U64, .u64 = query->component },
                                { .type = PIN_U64, .u64 = query->source_relationship },
                                { .type = PIN_I32, .i = query->source_traversal } };
        int slots[] = { query_context(l, range), owner, -1, -1, -1 };
        entity = primitive(
            l,
            &EXECUTION_query_source_call,
            &EXECUTION_component_read_access,
            headers,
            slots,
            5,
            (script_type){ .storage = PIN_HANDLE },
            range
        );
    }
    int absent = constant(l, (pin_value){ .type = PIN_HANDLE }, range);
    return binary(
        l,
        SCRIPT_OP_NE,
        entity,
        absent,
        (script_type){ .storage = PIN_BOOL },
        (script_type){ .storage = PIN_HANDLE },
        range
    );
}
static void query_result(lower *l, int result, int value, script_type type, script_range range)
{
    if (!(type.flags & SCRIPT_TYPE_BOXED_OPTIONAL) &&
        type.storage == PIN_STRUCT && type.container == PIN_CONTAINER_SINGLE) {
        return_record(l, value, type, result, range);
        return;
    }
    value = retain_value(l, value, type, range);
    int bank = value_op(
        l,
        (behavior_function_instruction){
            .op = BF_CONTEXT,
            .type = BF_POINTER_TYPE,
            .offset = offsetof(behavior_function_context, results) },
        range
    );
    int ordinal = constant(l, (pin_value){ .type = PIN_I32, .i = result }, range);
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
static int query_component_binding(lower *l, int symbol, script_range range)
{
    const script_symbol *binding = &l->a->symbols[symbol];
    script_type pointee = binding->component_type;
    pointee.flags &= ~SCRIPT_TYPE_OPTIONAL;
    const script_catalog_type
        *component = script_catalog_type_by_id(l->catalog, pointee.nominal);
    behavior_function_builder_component(l->builder, component->component_id, false, false);
    pin_value headers[] = { { .type = BF_POINTER_TYPE },
                            { .type = PIN_HANDLE },
                            { .type = PIN_U64, .u64 = component->component_id },
                            initial(l, pointee),
                            { .type = PIN_U64, .u64 = component->layout },
                            { .type = PIN_U64, .u64 = component->size } };
    int slots[] = { query_context(l, range),
                    component_owner(l, binding, range),
                    -1,
                    -1,
                    -1,
                    -1 };
    const node_host_call *call = binding->type.storage == PIN_POINTER
                                   ? &EXECUTION_component_pointer_call
                                   : &EXECUTION_component_snapshot_call;
    if (binding->type.storage == PIN_POINTER) {
        return primitive(
            l,
            call,
            &EXECUTION_component_read_access,
            headers,
            slots,
            6,
            binding->type,
            range
        );
    }
    pin_value output = initial(l, binding->type);
    int host = linked_host(l->builder, call, &EXECUTION_component_read_access, headers, 6, &output, 1);
    int first = behavior_function_builder_host_first(l->builder, host);
    for (int i = 0; i < 6; i++) {
        if (slots[i] >= 0) {
            host_argument(
                l,
                first + i,
                slots[i],
                (script_type){ .storage = headers[i].type },
                range
            );
        }
    }
    emit(
        l,
        (behavior_function_instruction){ .op = BF_HOST_CALL,
                                         .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                         .target = host },
        range
    );
    int result = behavior_function_builder_host_results(l->builder, host);
    l->query_banks[symbol] = result;
    return host_result(l, result, binding->type, range);
}
static void query_result_header(lower *l, int result, int source_bank, script_range range)
{
    int source = host_address(l, source_bank, false, range);
    int results = value_op(
        l,
        (behavior_function_instruction){
            .op = BF_CONTEXT,
            .type = BF_POINTER_TYPE,
            .offset = offsetof(behavior_function_context, results) },
        range
    );
    int destination = value_op(
        l,
        (behavior_function_instruction){
            .op = BF_INDEX,
            .type = BF_POINTER_TYPE,
            .left = results,
            .right = constant(l, (pin_value){ .type = PIN_I32, .i = result }, range),
            .offset = sizeof(pin_value) },
        range
    );
    const struct
    {
        size_t offset;
        pin_type type;
    } fields[] = {
        { offsetof(pin_value, type), PIN_I32 },
        { offsetof(pin_value, container), PIN_I32 },
        { offsetof(pin_value, type_name), BF_POINTER_TYPE },
        { offsetof(pin_value, record), BF_POINTER_TYPE },
        { offsetof(pin_value, schema), BF_POINTER_TYPE },
        { offsetof(pin_value, ptr), BF_POINTER_TYPE },
    };
    for (int i = 0; i < 6; i++) {
        int value = kind_op(
            l,
            (behavior_function_instruction){ .op = BF_LOAD,
                                             .type = fields[i].type,
                                             .left = source,
                                             .offset = fields[i].offset },
            range,
            i < 2 ? BEHAVIOR_VALUE : BEHAVIOR_ADDRESS
        );
        emit(
            l,
            (behavior_function_instruction){ .op = BF_WRITE,
                                             .type = fields[i].type,
                                             .left = value,
                                             .right = destination,
                                             .offset = fields[i].offset },
            range
        );
    }
}
static behavior_function *query_evaluator(lower *parent, int index)
{
    const script_query_description *description = &parent->a->descriptions[index];
    lower l = *parent;
    l.slot_schemas = l.local_schemas = NULL;
    l.schema_builder = NULL;
    l.function = -1;
    l.query_evaluating = index + 1;
    l.query_banks = indices(l.a->symbol_count);
    l.builder = behavior_function_builder_new();
    l.pc = 0;
    l.locals = indices(l.a->symbol_count);
    l.addresses = indices(l.a->symbol_count);
    l.parameters = indices(l.a->symbol_count);
    l.results = indices(l.a->symbol_count);
    l.captured = indices(l.a->symbol_count);
    l.loops = NULL;
    pin_value *args = NULL, *results = NULL;
    for (int i = 0; i < acount(description->captures); i++) {
        int symbol = description->captures[i];
        l.parameters[symbol] = i;
        apush(args, initial(&l, l.a->symbols[symbol].type));
    }
    apush(results, ((pin_value){ .type = PIN_BOOL }));
    for (int i = 0; i < acount(description->symbols); i++) {
        apush(results, initial(&l, l.a->symbols[description->symbols[i]].type));
    }
    behavior_function_builder_signature(l.builder, args, acount(args), results, acount(results), NULL, 0);
    afree(args);
    afree(results);
    query_result(
        &l,
        0,
        constant(&l, (pin_value){ .type = PIN_BOOL }, description->range),
        (script_type){ .storage = PIN_BOOL },
        description->range
    );
    int *exits = NULL;
    for (int i = 0; i < acount(description->stages); i++) {
        const script_query_stage *stage = &description->stages[i];
        if (stage->symbol < 0) {
            int condition = query_condition(&l, stage->expression);
            apush(
                exits,
                emit(
                    &l,
                    (behavior_function_instruction){
                        .op = BF_BRANCH,
                        .left = query_not(&l, condition, stage->range),
                        .target = -1 },
                    stage->range
                )
            );
            continue;
        }
        const script_symbol *symbol = &l.a->symbols[stage->symbol];
        if (symbol->kind == SCRIPT_COMPONENT &&
            !(symbol->type.flags & SCRIPT_TYPE_OPTIONAL)) {
            int presence = component_value(
                &l,
                &(script_expression){ .capability = symbol->component_type.nominal,
                                      .symbol = stage->symbol,
                                      .left = -1,
                                      .range = stage->range },
                true
            );
            apush(
                exits,
                emit(
                    &l,
                    (behavior_function_instruction){
                        .op = BF_BRANCH,
                        .left = query_not(&l, presence, stage->range),
                        .target = -1 },
                    stage->range
                )
            );
        }
        int value = symbol->kind == SCRIPT_COMPONENT
                      ? query_component_binding(&l, stage->symbol, stage->range)
                  : symbol->kind == SCRIPT_QUERY_VARIABLE
                      ? query_variable_value(&l, symbol->source_variable, stage->range)
                      : expression(&l, stage->expression);
        if (stage->expression >= 0) {
            value = convert(
                &l,
                value,
                l.a->expressions[stage->expression].type,
                symbol->type,
                stage->range
            );
        }
        if (symbol->kind == SCRIPT_COMPONENT && symbol->type.storage != PIN_POINTER) {
            l.locals[stage->symbol] = behavior_function_builder_local(
                l.builder,
                (pin_value){ .type = slot_type(symbol->type) }
            );
            value_root(&l, BEHAVIOR_BANK_LOCAL, l.locals[stage->symbol], symbol->type);
            emit(
                &l,
                (behavior_function_instruction){ .op = BF_SAVE,
                                                 .type = slot_type(symbol->type),
                                                 .left = value,
                                                 .target = l.locals[stage->symbol] },
                stage->range
            );
        } else {
            store_symbol(&l, stage->symbol, value, stage->range);
        }
    }
    for (int i = 0; i < acount(description->symbols); i++) {
        int symbol = description->symbols[i];
        script_expression value = { .kind = SCRIPT_EXPR_SYMBOL,
                                    .symbol = symbol,
                                    .type = l.a->symbols[symbol].type,
                                    .range = description->range };
        if (l.query_banks[symbol] >= 0) {
            query_result_header(&l, i + 1, l.query_banks[symbol], description->range);
        } else {
            query_result(
                &l,
                i + 1,
                symbol_value(&l, &value),
                value.type,
                description->range
            );
        }
    }
    query_result(
        &l,
        0,
        constant(&l, (pin_value){ .type = PIN_BOOL, .b = true }, description->range),
        (script_type){ .storage = PIN_BOOL },
        description->range
    );
    for (int i = 0; i < acount(exits); i++) {
        behavior_function_builder_patch_target(l.builder, exits[i], l.pc);
    }
    emit(&l, (behavior_function_instruction){ .op = BF_RETURN }, description->range);
    emit(&l, (behavior_function_instruction){ .op = BF_END }, description->range);
    afree(exits);
    behavior_function *function = NULL;
    char message[192];
    if (!l.failed) {
        function = behavior_function_builder_finish(l.builder, message, sizeof message);
        if (!function) {
            error(&l, description->range, message);
        }
    }
    behavior_function_builder_free(l.builder);
    schema_clear(&l);
    free(l.locals);
    free(l.addresses);
    free(l.parameters);
    free(l.results);
    free(l.captured);
    free(l.query_banks);
    parent->failed = l.failed;
    parent->occurrences = l.occurrences;
    return function;
}
static void emit_value_evaluator(
    lower *l,
    const int *expressions,
    const script_type *types,
    int count,
    script_range range
)
{
    l->function = -1;
    l->builder = behavior_function_builder_new();
    asetlen(l->temporaries, 0);
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
        value = copy_value(l, value, type, range);
        if (!(type.flags & SCRIPT_TYPE_BOXED_OPTIONAL) &&
            type.storage == PIN_STRUCT && type.container == PIN_CONTAINER_SINGLE) {
            return_record(l, value, type, i, range);
            continue;
        }
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
        forget_expression(l, expressions[i]);
    }
    emit(l, (behavior_function_instruction){ .op = BF_RETURN }, range);
    emit(l, (behavior_function_instruction){ .op = BF_END }, range);
    free(l->captured);
    l->captured = NULL;
    free(l->locals);
    free(l->addresses);
    free(l->parameters);
    free(l->results);
}
static behavior_function *value_evaluator(
    lower *l,
    const int *expressions,
    const script_type *types,
    int count,
    script_range range
)
{
    emit_value_evaluator(l, expressions, types, count, range);
    behavior_function *function = NULL;
    char message[192];
    if (!l->failed) {
        function = behavior_function_builder_finish(l->builder, message, sizeof message);
        if (!function) {
            error(l, range, message);
        }
    }
    behavior_function_builder_free(l->builder);
    schema_clear(l);
    return function;
}
behavior_function_builder *script_lower_evaluator_reference(
    const script_analysis *a,
    const script_catalog *catalog,
    const execution_module_state *states,
    int state_count,
    const int *expressions,
    const script_type *types,
    int count,
    script_range range,
    char *message,
    size_t size
)
{
    execution_module_plan module = { .states = (execution_module_state *)states,
                                     .state_count = state_count };
    lower l = { .a = a,
                .catalog = catalog,
                .module = &module,
                .variables = indices(a->symbol_count) };
    for (int i = 0; i < a->symbol_count; i++) {
        for (int j = 0; j < state_count; j++) {
            if (a->symbols[i].id == states[j].id) {
                l.variables[i] = j;
                break;
            }
        }
    }
    emit_value_evaluator(&l, expressions, types, count, range);
    free(l.variables);
    free(l.written);
    free(l.lent);
    afree(l.temporaries);
    script_occurrences_free(l.occurrences);
    if (l.failed) {
        snprintf(
            message,
            size,
            "%s",
            acount(module.diagnostics) ? module.diagnostics[0].message : "reference scalar lowering failed"
        );
        behavior_function_builder_free(l.builder);
        schema_clear(&l);
        l.builder = NULL;
    }
    afree(module.diagnostics);
    schema_clear(&l);
    return l.builder;
}
static int storage_root(const script_analysis *a, int index)
{
    while (index >= 0) {
        const script_expression *e = &a->expressions[index];
        if (e->kind != SCRIPT_EXPR_MEMBER && e->kind != SCRIPT_EXPR_INDEX) {
            return e->kind == SCRIPT_EXPR_SYMBOL ? e->symbol : -1;
        }
        index = e->left;
    }
    return -1;
}
/* Whether argument `at` of `call` passes a reference to its place. */
static bool reference_argument(const lower *l, const script_expression *call, int at)
{
    if (call->kind != SCRIPT_EXPR_CALL || call->arguments[at].parameter < 0) {
        return false;
    }
    int index = call->right >= 0 ? (call->entry >= 0 ? call->entry : call->right)
                                 : -1;
    const script_function *function = index >= 0
                                        ? input_signature(l->a, &l->a->functions[index])
                                        : NULL;
    const script_catalog_capability
        *cap = function
                 ? NULL
                 : script_catalog_capability_by_id(l->catalog, call->capability);
    int parameter = call->arguments[at].parameter;
    script_type type = function && parameter < function->parameter_count
                         ? l->a->symbols[function->parameters[parameter]].type
                     : cap && parameter < cap->parameter_count
                         ? cap->parameters[parameter].type
                         : (script_type){ 0 };
    return (type.flags & SCRIPT_TYPE_REFERENCE) != 0;
}
static void mark_root(const lower *l, int index)
{
    int root = storage_root(l->a, index);
    if (root >= 0) {
        l->written[root] = true;
    }
}
/* Whether the analysis reassigns `symbol` or changes it in place: an
 * assignment, a counter, a state store, an array change or a ref argument.
 * A for-each item is bound to each element in turn, never changed. */
static bool symbol_written(lower *l, int symbol)
{
    if (!l->written) {
        l->written = xcalloc((size_t)l->a->symbol_count + 1, sizeof *l->written);
        for (int i = 0; i < l->a->statement_count; i++) {
            const script_statement *s = &l->a->statements[i];
            if (s->kind == SCRIPT_STMT_ASSIGN || s->kind == SCRIPT_STMT_REDUCE) {
                mark_root(l, s->expression);
            }
            if ((s->kind == SCRIPT_STMT_ASSIGN ||
                 s->kind == SCRIPT_STMT_REDUCE || s->kind == SCRIPT_STMT_FOR) &&
                s->symbol >= 0) {
                l->written[s->symbol] = true;
            }
        }
        for (int i = 0; i < l->a->expression_count; i++) {
            const script_expression *e = &l->a->expressions[i];
            if (e->kind == SCRIPT_EXPR_ARRAY_MUTATE) {
                mark_root(l, e->left);
            }
            for (int j = 0; j < e->argument_count; j++) {
                if (reference_argument(l, e, j)) {
                    mark_root(l, e->arguments[j].value);
                }
            }
        }
    }
    return l->written[symbol];
}
/* A call lends its arguments, so their headers belong to the caller. A
 * parameter the body writes or exposes gets a private copy; read-only
 * arguments keep their lowering proof shape. References and captured
 * descriptors intentionally keep their owner. */
static void isolate_parameters(lower *l, script_range range)
{
    for (int i = 0; i < l->a->symbol_count; i++) {
        const script_symbol *symbol = &l->a->symbols[i];
        if (symbol->kind != SCRIPT_PARAMETER || l->parameters[i] < 0 ||
            l->captured[i] >= 0 || (symbol->type.flags & SCRIPT_TYPE_REFERENCE) ||
            (!symbol->addressed && !symbol_written(l, i))) {
            continue;
        }
        script_expression input = { .kind = SCRIPT_EXPR_SYMBOL,
                                    .symbol = i,
                                    .type = symbol->type,
                                    .range = range };
        int value = symbol_value(l, &input);
        l->parameters[i] = -1;
        /* A managed cell copies its initializer. */
        if (symbol->addressed) {
            declare_symbol(l, i, value, range);
        } else {
            value = copy_value(l, value, symbol->type, range);
            host_argument(l, local_address(l, i), value, symbol->type, range);
        }
    }
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
    l->builder = l->builders[index];
    asetlen(l->temporaries, 0);
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
    if (child_callable && (closure || !f->parameter_count)) {
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
                    tuple ? tuple->results[j]
                    : l->a->symbols[i].addressed
                        ? managed_type(l, l->a->symbols[i].type)
                        : l->a->symbols[i].type
                );
                pin_value owned;
                if (pin_value_capture_to_arena(&l->module->storage, &owned, &type)) {
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
        value_root(
            l,
            BEHAVIOR_BANK_LOCAL,
            l->locals[symbol],
            l->a->symbols[symbol].type
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
    isolate_parameters(l, f->range);
    for (int i = 0; i < output->result_count; i++) {
        int symbol = output->results[i];
        const script_symbol *result_symbol = &l->a->symbols[symbol];
        script_type type = result_symbol->type;
        if (result_symbol->initializer >= 0 &&
            (type.storage == PIN_UNION || type.storage == PIN_POINTER ||
             (type.flags & SCRIPT_TYPE_BOXED_OPTIONAL))) {
            int value = expression(l, result_symbol->initializer);
            value = convert(
                l,
                value,
                l->a->expressions[result_symbol->initializer].type,
                type,
                f->range
            );
            store_symbol(l, symbol, value, f->range);
        } else if (
            (type.storage == PIN_STRUCT || type.container != PIN_CONTAINER_SINGLE) &&
            type.container != PIN_CONTAINER_VIEW && !(type.flags & SCRIPT_TYPE_OPTIONAL)
        ) {
            /* A view has no zero: the function's return gives it its window. */
            script_expression zero = { .kind = type.container == PIN_CONTAINER_MAP
                                                 ? SCRIPT_EXPR_MAP
                                             : type.container == PIN_CONTAINER_SINGLE
                                                 ? SCRIPT_EXPR_STRUCT
                                                 : SCRIPT_EXPR_ARRAY,
                                       .type = type,
                                       .range = f->range };
            store_symbol(
                l,
                symbol,
                type.container == PIN_CONTAINER_MAP ? map_constructor(l, &zero)
                                                    : constructor(l, &zero),
                f->range
            );
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
            isolate_parameters(l, entry->range);
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
    schema_clear(l);
}
static pin_value evaluate_default(lower *l, const script_symbol *s)
{
    l->function = -1;
    pin_value result = initial(l, s->type);
    l->builder = behavior_function_builder_new();
    asetlen(l->temporaries, 0);
    l->pc = 0;
    l->initializing = true;
    l->initializer_depth = 0;
    l->locals = indices(l->a->symbol_count);
    l->addresses = indices(l->a->symbol_count);
    l->parameters = indices(l->a->symbol_count);
    l->results = indices(l->a->symbol_count);
    behavior_function_builder_signature(l->builder, NULL, 0, &result, 1, NULL, 0);
    int value = expression(l, s->initializer);
    if (!(s->type.flags & SCRIPT_TYPE_BOXED_OPTIONAL) &&
        (s->type.storage == PIN_STRUCT || s->type.container != PIN_CONTAINER_SINGLE)) {
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
            (behavior_function_instruction){ .op = BF_RESULT,
                                             .type = slot_type(s->type),
                                             .left = value,
                                             .target = 0 },
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
            behavior_managed_heap managed;
            behavior_managed_heap_init(&managed);
            behavior_function_context context = { .results = &result,
                                                  .budget = 100000,
                                                  .managed_heap = &managed };
            behavior_function_bind_storage(&context, &storage);
            if (!behavior_function_bind(f, &context, NULL, 0) ||
                behavior_function_run(f, &context, false)) {
                error(l, s->declaration, context.error_message ? context.error_message : "Cannot evaluate state default");
                result = initial(l, s->type);
            } else {
                pin_value retained;
                if (pin_value_capture_to_arena(&l->module->storage, &retained, &result)) {
                    error(l, s->declaration, "Cannot retain evaluated state default");
                    result = initial(l, s->type);
                } else {
                    result = retained;
                }
            }
            behavior_function_release_host(&context);
            arena_free(&storage);
            behavior_managed_heap_free(&managed);
            behavior_function_free(f);
        }
    }
    behavior_function_builder_free(l->builder);
    schema_clear(l);
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
/* A project catalog already exports the declaring library's records. Only
 * identical authored declarations may share that nominal requirement. */
static bool same_record_text(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}
static bool same_record_type(script_type a, script_type b)
{
    return a.storage == b.storage && a.container == b.container &&
           a.nominal == b.nominal && a.collection == b.collection &&
           a.parameter == b.parameter && a.flags == b.flags;
}
static bool same_record_default(const script_catalog_field *a, const script_catalog_field *b)
{
    const pin_value *x = &a->default_value, *y = &b->default_value;
    if (x->type != a->type.storage || y->type != b->type.storage ||
        x->container != PIN_CONTAINER_SINGLE || y->container != PIN_CONTAINER_SINGLE ||
        x->type_name || y->type_name || x->record || y->record) {
        return false;
    }
    switch (x->type) {
        case PIN_BOOL: return x->b == y->b;
        case PIN_I8: return x->i8 == y->i8;
        case PIN_U8: return x->u8 == y->u8;
        case PIN_I16: return x->i16 == y->i16;
        case PIN_U16: return x->u16 == y->u16;
        case PIN_I32: return x->i == y->i;
        case PIN_U32: return x->u == y->u;
        case PIN_I64: return x->i64 == y->i64;
        case PIN_U64: return x->u64 == y->u64;
        case PIN_F32: return !memcmp(&x->f, &y->f, sizeof x->f);
        case PIN_F64: return !memcmp(&x->d, &y->d, sizeof x->d);
        case PIN_NAME:
        case PIN_STR: return pin_text_equal(pin_text(*x), pin_text(*y));
        default: return false;
    }
}
static bool same_record(const script_catalog_type *a, const script_catalog_type *b)
{
    if (!a->authored_record || !b->authored_record || !a->id || a->id != b->id ||
        !a->name || !*a->name || !same_record_text(a->name, b->name) ||
        !same_record_text(a->source_name, b->source_name) ||
        !same_record_text(a->key, b->key) || !same_record_type(a->type, b->type) ||
        !a->layout || a->layout != b->layout || !a->size ||
        a->size != b->size || a->underlying != b->underlying ||
        a->component_id != b->component_id || a->component != b->component ||
        a->derived != b->derived || a->internal != b->internal ||
        a->variant_count || b->variant_count || a->field_count <= 0 ||
        a->field_count != b->field_count || !a->fields || !b->fields) {
        return false;
    }
    for (int i = 0; i < a->field_count; i++) {
        const script_catalog_field *x = &a->fields[i], *y = &b->fields[i];
        if (x->id != y->id || !same_record_text(x->name, y->name) ||
            !same_record_type(x->type, y->type) || x->offset != y->offset ||
            x->read != y->read || x->write != y->write ||
            !same_record_text(x->read_key, y->read_key) ||
            x->writable != y->writable || x->constant != y->constant ||
            x->c_string != y->c_string || !same_record_text(x->unit, y->unit) ||
            x->range_contract != y->range_contract ||
            memcmp(&x->min, &y->min, sizeof x->min) ||
            memcmp(&x->max, &y->max, sizeof x->max) || x->has_default != y->has_default ||
            (x->has_default && !same_record_default(x, y))) {
            return false;
        }
    }
    return true;
}
static bool record_overlaps(const script_catalog_type *a, const script_catalog_type *b)
{
    return a->id == b->id || (a->name && b->name && !strcmp(a->name, b->name));
}
static bool merge_record(lower *l, script_catalog_type **types, int index)
{
    const script_catalog_type *record = &l->a->types[index];
    int match = -1;
    for (int i = 0; i < index; i++) {
        if (record_overlaps(&l->a->types[i], record)) {
            goto conflict;
        }
    }
    for (int i = 0; i < acount(*types); i++) {
        if (!record_overlaps(&(*types)[i], record)) {
            continue;
        }
        if (match >= 0 || !same_record(&(*types)[i], record)) {
            goto conflict;
        }
        match = i;
    }
    if (match < 0) {
        apush(*types, *record);
    }
    return true;
conflict:
    char message[192];
    snprintf(
        message,
        sizeof message,
        "Conflicting authored record declaration '%s'",
        record->name
    );
    script_range range = { 0 };
    for (int i = 0; i < l->a->symbol_count; i++) {
        const script_symbol *symbol = &l->a->symbols[i];
        if (symbol->kind == SCRIPT_TYPE && symbol->id == record->id) {
            range = symbol->declaration;
            break;
        }
    }
    error(l, range, message);
    return false;
}
static const pin_record *lower_record_layout(
    arena *storage,
    const script_catalog *catalog,
    int index,
    const pin_record **records
)
{
    if (records[index]) {
        return records[index];
    }
    const script_catalog_type *type = &catalog->types[index];
    type_field *fields = NULL;
    for (int i = 0; i < type->field_count; i++) {
        const script_catalog_field *field = &type->fields[i];
        const script_catalog_type
            *child = script_catalog_type_by_id(catalog, field->type.nominal);
        type_field shape = { .name = field->name,
                             .type = field->type.storage,
                             .container = field->type.container,
                             .type_name = child ? child->name : NULL };
        if (field->type.storage == PIN_STRUCT && child && child->authored_record) {
            const pin_record *nested = lower_record_layout(
                storage,
                catalog,
                (int)(child - catalog->types),
                records
            );
            shape.layout = &nested->type;
        }
        apush(fields, shape);
    }
    const pin_record *record = pin_record_create(storage, type->name, type->id, fields, acount(fields));
    afree(fields);
    assert(record && record->layout == type->layout && record->type.size == type->size);
    records[index] = record;
    return record;
}

static execution_module_plan *lower_analysis(
    const script_analysis *a,
    const script_catalog *catalog,
    const script_lower_bindings *bindings,
    script_lower_package_result *package
)
{
    assert(a && catalog);
    execution_module_plan *m = execution_module_plan_new();
    if (!a->valid) {
        for (int i = 0; i < a->diagnostic_count; i++) {
            const script_diagnostic *source = &a->diagnostics[i];
            execution_module_diagnostic diagnostic = {
                .range = { source->range.start,
                           source->range.end,
                           source->range.line,
                           source->range.column,
                           source->range.document }
            };
            memcpy(diagnostic.message, source->message, sizeof diagnostic.message);
            apush(m->diagnostics, diagnostic);
        }
        m->diagnostic_count = acount(m->diagnostics);
        return m;
    }
    /* Analysis can rewrite specialized calls into non-call expressions. Check
     * the entire operation catalog before lowering any of those expressions. */
    for (int i = 0; i < catalog->capability_count; i++) {
        if (!script_catalog_operation_valid(&catalog->capabilities[i])) {
            execution_module_diagnostic diagnostic = { 0 };
            snprintf(
                diagnostic.message,
                sizeof diagnostic.message,
                "Invalid engine operation declaration: %s",
                catalog->capabilities[i].name
            );
            apush(m->diagnostics, diagnostic);
        }
    }
    m->diagnostic_count = acount(m->diagnostics);
    if (m->diagnostic_count) {
        return m;
    }
    assert(a->identity);
    lower l = { .a = a, .catalog = catalog, .bindings = bindings, .module = m };
    script_catalog merged = *catalog;
    script_catalog_type *types = NULL;
    for (int i = 0; i < catalog->type_count; i++) {
        apush(types, catalog->types[i]);
    }
    for (int i = 0; i < a->type_count; i++) {
        if (a->types[i].authored_record) {
            if (!merge_record(&l, &types, i)) {
                m->diagnostic_count = acount(m->diagnostics);
                afree(types);
                return m;
            }
            continue;
        }
        apush(types, a->types[i]);
    }
    merged.types = types;
    merged.type_count = acount(types);
    catalog = &merged;
    m->id = script_catalog_id("module", a->identity);
    const pin_record **records = xcalloc((size_t)catalog->type_count, sizeof *records);
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *type = &catalog->types[i];
        if ((type->internal && type->type.storage != PIN_POINTER &&
             type->type.storage != PIN_QUERY) ||
            (type->type.flags & (SCRIPT_TYPE_NAMESPACE | SCRIPT_TYPE_UNION))) {
            continue;
        }
        const pin_record *record = NULL;
        if (type->authored_record) {
            record = lower_record_layout(&m->storage, catalog, i, records);
        }
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
                                           .record = record,
                                           .name = name,
                                           .size = type->size })
        );
    }
    free(records);
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
    l.catalog = catalog;
    m->query = a->query;
    m->query_variable_count = a->query_variable_count;
    m->owner_type = a->owner_type;
    l.variables = indices(a->symbol_count);
    l.constant_values = xcalloc((size_t)a->symbol_count, sizeof *l.constant_values);
    for (int i = 0; i < a->symbol_count && !l.failed; i++) {
        const script_symbol *symbol = &a->symbols[i];
        if (symbol->kind == SCRIPT_CONSTANT) {
            l.constant_values[i] = evaluate_default(&l, symbol);
        }
    }
    for (int i = 0; package && i < a->function_count && !l.failed; i++) {
        const script_function *function = &a->functions[i];
        if (function->kind != SCRIPT_FUNCTION_AUTHORED || function->parent >= 0 ||
            function->symbol < 0 || a->symbols[function->symbol].disabled ||
            a->symbols[function->symbol].scope != a->package_scope) {
            continue;
        }
        for (int j = 0;
             j < function->parameter_count + function->result_count && !l.failed;
             j++) {
            int index = j < function->parameter_count
                          ? function->parameters[j]
                          : function->results[j - function->parameter_count];
            const script_symbol *symbol = &a->symbols[index];
            if (symbol->initializer < 0) {
                continue;
            }
            if (!script_analysis_constant(a, symbol->initializer)) {
                error(&l, symbol->declaration, "Exported parameter default requires a bounded compile-time value");
                break;
            }
            l.constant_values[index] = evaluate_default(&l, symbol);
        }
    }
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
                type_validate_ranges(type, value.ptr);
            }
        }
        if (s->initializer >= 0) {
            const script_expression *e = &a->expressions[s->initializer];
            if (e->kind == SCRIPT_EXPR_CONSTANT &&
                !(s->type.flags & (SCRIPT_TYPE_UNION | SCRIPT_TYPE_BOXED_OPTIONAL))) {
                value = e->constant;
                if (value.type != s->type.storage && script_type_numeric(s->type) &&
                    !behavior_scalar_cast(value, s->type.storage, BEHAVIOR_CAST_EXACT, &value)) {
                    error(&l, s->declaration, "State constant does not fit its declared storage");
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
        if (pin_value_capture_to_arena(&m->storage, &retained, &value)) {
            error(&l, s->declaration, "State default cannot be retained by the module");
            retained = (pin_value){ .type = value.type };
        }
        l.variables[i] = acount(m->states);
        apush(
            m->states,
            ((execution_module_state){
                .id = s->id,
                .initial = retained,
                .shared = s->kind == SCRIPT_SHARED_STATE,
                .contains_tasks = script_type_contains_tasks(catalog, s->type) })
        );
    }
    m->state_count = acount(m->states);
    for (int i = 0; i < a->query_count; i++) {
        const script_query *source = &a->queries[i];
        execution_module_query q = { .kind = (execution_query_kind)source->kind,
                                     .required = source->required,
                                     .component = source->component,
                                     .source_relationship = source->source_relationship,
                                     .source_traversal = source->source_traversal,
                                     .source_variable = source->source_variable,
                                     .target_variable = source->target_variable,
                                     .child_count = source->child_count };
        q.children = xmalloc((size_t)(q.child_count + 1) * sizeof *q.children);
        if (q.child_count) {
            memcpy(
                q.children,
                source->children,
                (size_t)q.child_count * sizeof *q.children
            );
        }
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
            } else if (pin_value_capture_to_arena(&m->storage, &retained, &value)) {
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
        while (parent >= 0 && (a->expressions[parent].kind == SCRIPT_EXPR_MEMBER ||
                               a->expressions[parent].kind == SCRIPT_EXPR_DEREFERENCE)) {
            if (a->expressions[parent].kind == SCRIPT_EXPR_MEMBER) {
                out.offset += a->expressions[parent].offset;
            }
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
            component_id = a->symbols[a->expressions[parent].symbol]
                               .component_type.nominal;
        }
        out.component = component_id;
        const script_catalog_type *component_type = script_catalog_type_by_id(catalog, component_id);
        out.component = component_type ? component_type->component_id : 0;
        out.layout = component_type ? component_type->layout : 0;
        pin_value target_type = initial(&l, target->type);
        if (pin_value_capture_to_arena(&m->storage, &out.target_type, &target_type)) {
            error(&l, b->range, "Cannot retain binding target layout");
        }
        out.reads = xmalloc((size_t)(b->read_count + 1) * sizeof *out.reads);
        out.writes = xmalloc((size_t)(b->write_count + 1) * sizeof *out.writes);
        if (b->read_count) {
            memcpy(out.reads, b->reads, (size_t)b->read_count * sizeof *out.reads);
        }
        if (b->write_count) {
            memcpy(out.writes, b->writes, (size_t)b->write_count * sizeof *out.writes);
        }
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
                char detail[256];
                int symbol = a->functions[i].symbol;
                snprintf(
                    detail,
                    sizeof detail,
                    "%s (%s)",
                    message,
                    symbol >= 0 ? a->symbols[symbol].name : "initializer"
                );
                builder_error(&l, l.builders[i], a->functions[i].range, detail);
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
        asetlen(l.temporaries, 0);
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
        int value = query_component
                      ? component_owner(&l, &a->symbols[receiver_expression->symbol], f->range)
                  : receiver >= 0
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
        schema_clear(&l);
        free(l.locals);
        free(l.addresses);
        free(l.parameters);
        free(l.results);
    }
    m->system_description = a->system_description;
    for (int i = 0; i < a->description_count && !l.failed; i++) {
        const script_query_description *source = &a->descriptions[i];
        execution_module_query_description description = {
            .query = source->query,
            .query_variable_count = source->query_variable_count,
            .row_type = initial(&l, source->row_type),
            .evaluate = query_evaluator(&l, i)
        };
        apush(m->descriptions, description);
    }
    m->description_count = acount(m->descriptions);
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
    if (package && !l.failed) {
        script_lower_constant *constants = arena_alloc(
            &m->storage,
            (size_t)a->symbol_count * sizeof *constants
        );
        for (int i = 0; i < a->symbol_count; i++) {
            const script_symbol *symbol = &a->symbols[i];
            if (!symbol->disabled && l.constant_values[i].type != PIN_FLOW) {
                constants[package->constant_count++] = (script_lower_constant){
                    symbol->id,
                    l.constant_values[i]
                };
            }
        }
        package->constants = constants;
    }
    free(l.constant_values);
    free(l.written);
    free(l.lent);
    afree(l.loops);
    afree(l.temporaries);
    script_occurrences_free(l.occurrences);
    m->diagnostic_count = acount(m->diagnostics);
    m->valid = !l.failed;
    if (!m->valid) {
        discard_bodies(m);
    }
    afree(types);
    return m;
}

execution_module_plan *script_lower(
    const script_analysis *analysis,
    const script_catalog *catalog,
    const script_lower_bindings *bindings
)
{
    return lower_analysis(analysis, catalog, bindings, NULL);
}
script_lower_package_result script_lower_package(
    const script_analysis *analysis,
    const script_catalog *catalog,
    const script_lower_bindings *bindings
)
{
    script_lower_package_result result = { 0 };
    result.plan = lower_analysis(analysis, catalog, bindings, &result);
    return result;
}
