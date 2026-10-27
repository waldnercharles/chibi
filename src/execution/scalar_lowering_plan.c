#include "execution/scalar_lowering_plan.h"

#include "execution/definition.h"
#include "execution/prepared.h"
#include "types/array.h"
#include "types/record.h"
#include "types/text.h"
#include "types/union.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum
{
    LIMIT = 256
};
/* Wire opcodes are independent of behavior_function_op ordinals. */
static const behavior_function_op operations[] = {
    BF_CONSTANT, BF_VARIABLE, BF_LOCAL,   BF_CONTEXT, BF_INDEX, BF_WRITE,
    BF_SAVE,     BF_BRANCH,   BF_JUMP,    BF_RETURN,  BF_END,   BF_NEG,
    BF_BIT_NOT,  BF_ADD,      BF_SUB,     BF_MUL,     BF_DIV,   BF_MOD,
    BF_EQ,       BF_NE,       BF_LT,      BF_LE,      BF_GT,    BF_GE,
    BF_BIT_AND,  BF_BIT_OR,   BF_BIT_XOR, BF_SHL,     BF_SHR
};
_Static_assert(sizeof operations / sizeof *operations == SCALAR_LOWERING_OPCODES, "scalar lowering wire opcodes changed");
_Static_assert(PIN_BOOL == 1 && PIN_I32 == 6 && PIN_U32 == 7 && PIN_U64 == 9, "scalar lowering storage schema changed");
_Static_assert(EXECUTION_PREPARED_VERSION == 23, "review scalar lowering target schema");
behavior_function_op scalar_lowering_opcode(int wire)
{
    assert(wire >= 0 && wire < SCALAR_LOWERING_OPCODES);
    return operations[wire];
}
static bool fail(char *error, size_t size, const char *message)
{
    snprintf(error, size, "%s", message);
    return false;
}
static bool scalar(pin_type type)
{
    return type == PIN_BOOL || type == PIN_I32;
}
static bool field(const pin_value *row, const char *key, pin_type type, pin_value *out)
{
    if (row->container != PIN_CONTAINER_SINGLE || row->type != PIN_STRUCT ||
        !row->ptr || !row->record) {
        return false;
    }
    const type_field *f = pin_type_field_path(pin_value_type(row), key);
    if (!f || f->type != type) {
        return false;
    }
    pin_read_field(NULL, row->ptr, f, out);
    return out->type == type && out->container == PIN_CONTAINER_SINGLE;
}
static bool integer(const pin_value *row, const char *key, int *out)
{
    pin_value value;
    if (!field(row, key, PIN_I32, &value)) {
        return false;
    }
    *out = value.i;
    return true;
}
static bool array(const pin_value *value, pin_type type, int limit, const pin_array **out)
{
    if (value->type != type || value->container != PIN_CONTAINER_ARRAY) {
        return false;
    }
    const pin_array *a = value->ptr;
    if (a && (!pin_array_valid(a) || a->count > limit)) {
        return false;
    }
    for (int i = 0; a && i < a->count; i++) {
        pin_value item = pin_array_get(a, i);
        if (item.type != type || item.container != PIN_CONTAINER_SINGLE) {
            return false;
        }
    }
    *out = a;
    return true;
}
static int count(const pin_array *a)
{
    return a ? a->count : 0;
}
static const pin_value *item(const pin_array *a, int index, pin_value *out)
{
    *out = pin_array_get(a, index);
    return out;
}
static bool path_valid(pin_value value)
{
    pin_string text = pin_text(value);
    return text.length <= 240 && !memchr(text.bytes, 0, text.length);
}
static bool literal(const pin_value *row, pin_value *out)
{
    int type, payload;
    if (!integer(row, "storage", &type) || !integer(row, "payload", &payload) ||
        !scalar(type) || (type == PIN_BOOL && payload != 0 && payload != 1)) {
        return false;
    }
    *out = (pin_value){ .type = type };
    if (type == PIN_BOOL) {
        out->b = payload != 0;
    } else {
        out->i = payload;
    }
    return true;
}
static bool target(const pin_value *value)
{
    const char *keys[] = { "schema",     "prepared",       "pointer_storage",
                           "value_size", "payload_offset", "results_offset" };
    const int expected[] = { 1,
                             EXECUTION_PREPARED_VERSION,
                             BF_POINTER_TYPE,
                             sizeof(pin_value),
                             offsetof(pin_value, d),
                             offsetof(behavior_function_context, results) };
    for (int i = 0; i < 6; i++) {
        int n;
        if (!integer(value, keys[i], &n) || n != expected[i]) {
            return false;
        }
    }
    return true;
}
static bool instruction(const pin_value *row, behavior_function_instruction *out)
{
    int op, type, value_type, payload, offset;
    *out = (behavior_function_instruction){ 0 };
    if (!integer(row, "op", &op) || op < 0 ||
        op >= (int)(sizeof operations / sizeof *operations) ||
        !integer(row, "storage", &type) || !integer(row, "value_storage", &value_type) ||
        !integer(row, "payload", &payload) ||
        !integer(row, "offset", &offset) || offset < 0) {
        return false;
    }
    out->op = operations[op];
    out->type = type;
    out->offset = (size_t)offset;
    const char *keys[] = { "output", "left",  "right", "target",   "frame",
                           "source", "stack", "pin",   "component" };
    int *values[] = { &out->output, &out->left,  &out->right,
                      &out->target, &out->frame, &out->source,
                      &out->stack,  &out->pin,   &out->component };
    for (int i = 0; i < 9; i++) {
        if (!integer(row, keys[i], values[i])) {
            return false;
        }
    }
    pin_value indirect, node, layout, message;
    if (!field(row, "indirect", PIN_BOOL, &indirect) || indirect.b ||
        !field(row, "node_id", PIN_I32, &node) || node.i ||
        !field(row, "layout", PIN_I32, &layout) || layout.i ||
        !field(row, "message", PIN_STR, &message) || pin_text(message).length ||
        out->frame || out->stack != -1 || out->pin || out->component) {
        return false;
    }
    if (out->op == BF_CONSTANT) {
        if (value_type != type || !scalar(type) ||
            (type == PIN_BOOL && payload != 0 && payload != 1)) {
            return false;
        }
        out->value.type = type;
        if (type == PIN_BOOL) {
            out->value.b = payload != 0;
        } else {
            out->value.i = payload;
        }
    } else if (value_type || payload) {
        return false;
    }
    return true;
}
static bool in(int value, int length)
{
    return value >= 0 && value < length;
}
static bool value_op(behavior_function_op op)
{
    return op == BF_CONSTANT || op == BF_VARIABLE || op == BF_LOCAL ||
           op == BF_CONTEXT || op == BF_INDEX || op == BF_NEG ||
           op == BF_BIT_NOT || (op >= BF_ADD && op <= BF_MOD) ||
           (op >= BF_LT && op <= BF_NE) || (op >= BF_BIT_AND && op <= BF_SHR);
}
/* Structural/provenance validation, not a second source lowerer. Address paths
 * are deliberately closed: CONTEXT(results) -> INDEX(fixed ordinal) -> WRITE.
 * Arbitrary pointer arithmetic, context offsets and indirect access are absent. */
static bool references(
    const behavior_function_instruction *code,
    int code_count,
    const pin_type *slots,
    const behavior_value_kind *kinds,
    int slot_count,
    int local_count,
    const pin_value *defaults,
    int variable_count,
    const pin_value *headers,
    int result_count
)
{
    int producers[LIMIT];
    for (int i = 0; i < LIMIT; i++) {
        producers[i] = -1;
    }
    int next_slot = 0;
    for (int pc = 0; pc < code_count; pc++) {
        const behavior_function_instruction *i = &code[pc];
        bool produces = value_op(i->op);
        if (i->source != pc + 1 ||
            (produces && (!in(i->output, slot_count) || i->output != next_slot)) ||
            (!produces && i->output != 0)) {
            return false;
        }
        if (produces) {
            pin_type expected = i->op >= BF_LT && i->op <= BF_NE ? PIN_BOOL : i->type;
            behavior_value_kind kind = i->op == BF_CONTEXT || i->op == BF_INDEX
                                         ? BEHAVIOR_ADDRESS
                                         : BEHAVIOR_VALUE;
            if (slots[i->output] != expected || kinds[i->output] != kind) {
                return false;
            }
        }
        bool lhs = i->op == BF_NEG || i->op == BF_BIT_NOT ||
                   (i->op >= BF_ADD && i->op <= BF_MOD) ||
                   (i->op >= BF_LT && i->op <= BF_NE) ||
                   (i->op >= BF_BIT_AND && i->op <= BF_SHR) || i->op == BF_SAVE ||
                   i->op == BF_BRANCH || i->op == BF_INDEX || i->op == BF_WRITE;
        bool rhs =
            ((i->op >= BF_ADD && i->op <= BF_MOD) ||
             (i->op >= BF_LT && i->op <= BF_NE) ||
             (i->op >= BF_BIT_AND && i->op <= BF_SHR && i->op != BF_BIT_NOT) ||
             i->op == BF_INDEX || i->op == BF_WRITE);
        if ((lhs && !in(i->left, next_slot)) || (rhs && !in(i->right, next_slot))) {
            return false;
        }
        if (i->op == BF_CONTEXT) {
            if (i->type != BF_POINTER_TYPE ||
                i->offset != offsetof(behavior_function_context, results)) {
                return false;
            }
        } else if (i->op == BF_INDEX) {
            if (i->type != BF_POINTER_TYPE || kinds[i->left] != BEHAVIOR_ADDRESS ||
                i->offset != sizeof(pin_value) || slots[i->right] != PIN_I32 ||
                code[producers[i->left]].op != BF_CONTEXT ||
                code[producers[i->right]].op != BF_CONSTANT ||
                !in(code[producers[i->right]].value.i, result_count)) {
                return false;
            }
        } else if (i->op == BF_WRITE) {
            if (!scalar(i->type) || slots[i->left] != i->type ||
                kinds[i->left] != BEHAVIOR_VALUE || kinds[i->right] != BEHAVIOR_ADDRESS ||
                i->offset != offsetof(pin_value, d) ||
                code[producers[i->right]].op != BF_INDEX ||
                headers[code[producers[code[producers[i->right]].right]].value.i]
                        .type != i->type) {
                return false;
            }
        } else if (i->offset) {
            return false;
        }
        if (i->op == BF_VARIABLE &&
            (!in(i->left, variable_count) || i->type != defaults[i->left].type)) {
            return false;
        }
        switch (i->op) {
            case BF_CONSTANT:
            case BF_CONTEXT:
                if (i->left || i->right || i->target) {
                    return false;
                }
                break;
            case BF_VARIABLE:
            case BF_LOCAL:
                if (i->right || i->target) {
                    return false;
                }
                break;
            case BF_NEG:
            case BF_BIT_NOT:
                if (i->right || i->target || i->type != PIN_I32 ||
                    slots[i->left] != PIN_I32) {
                    return false;
                }
                break;
            case BF_INDEX:
            case BF_WRITE:
                if (i->target) {
                    return false;
                }
                break;
            case BF_SAVE:
            case BF_BRANCH:
                if (i->right) {
                    return false;
                }
                break;
            case BF_JUMP:
                if (i->left || i->right) {
                    return false;
                }
                break;
            case BF_RETURN:
            case BF_END:
                if (i->left || i->right || i->target || i->type != PIN_FLOW ||
                    pc != code_count - (i->op == BF_RETURN ? 2 : 1)) {
                    return false;
                }
                break;
            default:
                /* Binary scalar operations have no secondary DIV/MOD output.
                 * Binary EQ/NE normally use -1, unary NOT emits EQ with zero. */
                if ((i->target != -1 && !(i->op == BF_EQ && i->target == 0)) ||
                    slots[i->left] != i->type || slots[i->right] != i->type ||
                    (i->op != BF_EQ && i->op != BF_NE && i->type != PIN_I32)) {
                    return false;
                }
                break;
        }
        if (i->op == BF_LOCAL && (!in(i->left, local_count) || i->type != PIN_BOOL)) {
            return false;
        }
        if (i->op == BF_SAVE &&
            (!in(i->target, local_count) || i->type != PIN_BOOL ||
             slots[i->left] != PIN_BOOL || kinds[i->left] != BEHAVIOR_VALUE)) {
            return false;
        }
        if ((i->op == BF_JUMP || i->op == BF_BRANCH) &&
            (i->target <= pc || i->target >= code_count || i->type != PIN_FLOW)) {
            return false;
        }
        if (i->op == BF_BRANCH &&
            (slots[i->left] != PIN_BOOL || kinds[i->left] != BEHAVIOR_VALUE)) {
            return false;
        }
        if (produces && i->op != BF_CONTEXT && i->op != BF_INDEX && !scalar(i->type)) {
            return false;
        }
        if (lhs && i->op != BF_INDEX && i->op != BF_WRITE &&
            kinds[i->left] != BEHAVIOR_VALUE) {
            return false;
        }
        if (rhs && i->op != BF_INDEX && i->op != BF_WRITE &&
            kinds[i->right] != BEHAVIOR_VALUE) {
            return false;
        }
        if (produces) {
            producers[next_slot++] = pc;
        }
    }
    return next_slot == slot_count && code_count >= 2 &&
           code[code_count - 2].op == BF_RETURN && code[code_count - 1].op == BF_END;
}
bool scalar_lowering_decode(
    const pin_value *value,
    uint64_t ready_tag,
    scalar_lowering_builder *out,
    char *error,
    size_t size
)
{
    assert(out && !out->builder && !out->variable_count);
    if (value->type != PIN_UNION || value->container != PIN_CONTAINER_SINGLE ||
        !value->ptr) {
        return fail(error, size, "scalar plan is not a union");
    }
    const pin_union *u = value->ptr;
    if (u->tag != ready_tag || u->count != 9 || !target(&u->values[0])) {
        return fail(error, size, "scalar plan schema or target mismatch");
    }
    const pin_array *a[8];
    const pin_type shapes[] = { PIN_U64,    PIN_STRUCT, PIN_STRUCT, PIN_STRUCT,
                                PIN_STRUCT, PIN_STRUCT, PIN_STR,    PIN_STRUCT };
    const int limits[] = { 2, 2, 2, LIMIT, LIMIT, LIMIT, 3, LIMIT };
    for (int n = 0; n < 8; n++) {
        if (!array(&u->values[n + 1], shapes[n], limits[n], &a[n])) {
            return fail(error, size, "invalid scalar plan table");
        }
    }
    int variables = count(a[0]), results = count(a[2]), slot_count = count(a[3]),
        local_count = count(a[4]), code_count = count(a[7]);
    if (variables != count(a[1]) || results < 1 || results > 2 ||
        count(a[5]) != code_count) {
        return fail(error, size, "scalar plan table lengths disagree");
    }
    pin_value defaults[2], headers[2], initials[LIMIT], row;
    pin_type slots[LIMIT];
    behavior_value_kind kinds[LIMIT];
    behavior_function_instruction code[LIMIT];
    behavior_function_source origins[LIMIT];
    for (int n = 0; n < variables; n++) {
        if (!pin_array_get(a[0], n).u64 ||
            (n && pin_array_get(a[0], n).u64 == pin_array_get(a[0], 0).u64) ||
            !literal(item(a[1], n, &row), &defaults[n])) {
            return fail(error, size, "invalid scalar plan variable signature");
        }
    }
    for (int n = 0; n < results; n++) {
        if (!literal(item(a[2], n, &row), &headers[n]) || headers[n].i) {
            return fail(error, size, "invalid scalar plan result signature");
        }
    }
    for (int n = 0; n < slot_count; n++) {
        int type, kind;
        if (!integer(item(a[3], n, &row), "storage", &type) ||
            !integer(item(a[3], n, &row), "kind", &kind) ||
            (!scalar(type) && type != BF_POINTER_TYPE) ||
            (kind != BEHAVIOR_VALUE && kind != BEHAVIOR_ADDRESS)) {
            return fail(error, size, "invalid scalar plan slot");
        }
        slots[n] = type;
        kinds[n] = kind;
    }
    for (int n = 0; n < local_count; n++) {
        int kind;
        if (!literal(item(a[4], n, &row), &initials[n]) ||
            initials[n].type != PIN_BOOL || initials[n].b ||
            !integer(item(a[4], n, &row), "kind", &kind) || kind != BEHAVIOR_VALUE) {
            return fail(error, size, "invalid scalar plan local");
        }
    }
    uint32_t origin_lengths[LIMIT];
    for (int n = 0; n < code_count; n++) {
        int doc;
        behavior_function_source *s = &origins[n];
        *s = (behavior_function_source){ .parent = -1 };
        if (!instruction(item(a[7], n, &row), &code[n]) ||
            !integer(item(a[5], n, &row), "start", &s->start) ||
            !integer(item(a[5], n, &row), "end", &s->end) ||
            !integer(item(a[5], n, &row), "line", &s->line) ||
            !integer(item(a[5], n, &row), "column", &s->column) ||
            !integer(item(a[5], n, &row), "document", &doc) ||
            !in(doc, count(a[6])) || s->start < 0 || s->end < s->start ||
            s->line < 0 || s->column < 0 || !path_valid(pin_array_get(a[6], doc))) {
            return fail(error, size, "invalid scalar plan instruction or origin");
        }
        pin_string name = pin_text(pin_array_get(a[6], doc));
        s->asset_name = name.bytes;
        origin_lengths[n] = name.length;
    }
    if (!references(code, code_count, slots, kinds, slot_count, local_count, defaults, variables, headers, results)) {
        return fail(error, size, "invalid scalar plan references or provenance");
    }
    scalar_lowering_builder holder = { 0 };
    holder.builder = behavior_function_builder_new();
    holder.variable_count = variables;
    behavior_function_builder_signature(holder.builder, NULL, 0, headers, results, defaults, variables);
    for (int n = 0; n < variables; n++) {
        holder.variable_ids[n] = pin_array_get(a[0], n).u64;
    }
    for (int n = 0; n < slot_count; n++) {
        behavior_function_builder_slot(holder.builder, slots[n]);
        bool ok = behavior_function_builder_kind(holder.builder, BEHAVIOR_BANK_SLOT, n, kinds[n]);
        assert(ok);
        (void)ok;
    }
    for (int n = 0; n < local_count; n++) {
        behavior_function_builder_local(holder.builder, initials[n]);
    }
    for (int n = 0; n < code_count; n++) {
        size_t length = (size_t)origin_lengths[n];
        char *name = arena_alloc(&holder.storage, length + 1);
        memcpy(name, origins[n].asset_name, length);
        name[length] = 0;
        origins[n].asset_name = name;
        behavior_function_builder_origin(holder.builder, origins[n]);
        behavior_function_builder_emit(holder.builder, code[n]);
    }
    *out = holder;
    return true;
}
void scalar_lowering_builder_free(scalar_lowering_builder *holder)
{
    if (holder->builder) {
        behavior_function_builder_free(holder->builder);
    }
    arena_free(&holder->storage);
    *holder = (scalar_lowering_builder){ 0 };
}
behavior_function *scalar_lowering_finish(scalar_lowering_builder *holder, char *error, size_t size)
{
    assert(holder->builder);
    return behavior_function_builder_finish(holder->builder, error, size);
}
