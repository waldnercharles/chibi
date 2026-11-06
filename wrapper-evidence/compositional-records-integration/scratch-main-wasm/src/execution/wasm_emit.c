#include "execution/wasm_emit.h"

#include "ecs/execution.h"
#include "execution/definition.h"
#include "execution/machine.h"
#include "execution/scalar.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* The ECS scratch locals are refreshed for each checked read. */
enum
{
    MACHINE,
    VALUES,
    PC,
    STATUS,
    CONTEXT,
    WORLD,
    ENTITY,
    GENERATION,
    COLUMN,
    MASK,
    BUCKET
};

typedef struct
{
    unsigned char *data;
} writer;

static void byte(writer *w, unsigned value)
{
    apush(w->data, (unsigned char)value);
}

static void raw(writer *w, const void *source, size_t size)
{
    const unsigned char *p = source;
    for (size_t i = 0; i < size; i++) {
        byte(w, p[i]);
    }
}

static void unsigned_leb(writer *w, uint32_t value)
{
    do {
        unsigned part = value & 127;
        value >>= 7;
        byte(w, part | (value ? 128 : 0));
    } while (value);
}

static void signed_leb(writer *w, int64_t value)
{
    bool more;
    do {
        unsigned part = (unsigned)value & 127;
        value >>= 7;
        more = !((value == 0 && !(part & 64)) || (value == -1 && (part & 64)));
        byte(w, part | (more ? 128 : 0));
    } while (more);
}

static void name(writer *w, const char *text)
{
    unsigned_leb(w, (uint32_t)strlen(text));
    raw(w, text, strlen(text));
}

static void section(writer *module, unsigned id, writer *contents)
{
    byte(module, id);
    unsigned_leb(module, (uint32_t)acount(contents->data));
    raw(module, contents->data, acount(contents->data));
    afree(contents->data);
}

static void local(writer *w, unsigned op, int index)
{
    byte(w, op);
    unsigned_leb(w, (uint32_t)index);
}

static void constant(writer *w, int value)
{
    byte(w, 0x41);
    signed_leb(w, value);
}

static void memory(writer *w, unsigned op, size_t offset)
{
    byte(w, op);
    unsigned_leb(w, 0); /* All accesses support unaligned engine storage. */
    unsigned_leb(w, (uint32_t)offset);
}

static int value_type(pin_type type)
{
    if (type == PIN_F32) {
        return 0x7d;
    }
    if (type == PIN_F64) {
        return 0x7c;
    }
    if (type == PIN_I64 || type == PIN_U64 || (type == PIN_HANDLE || type == PIN_TASK)) {
        return 0x7e;
    }
    return 0x7f;
}

static unsigned load_op(pin_type type)
{
    switch (type) {
        case PIN_BOOL:
        case PIN_U8: return 0x2d;
        case PIN_I8: return 0x2c;
        case PIN_U16: return 0x2f;
        case PIN_I16: return 0x2e;
        case PIN_F32: return 0x2a;
        case PIN_F64: return 0x2b;
        case PIN_I64:
        case PIN_U64:
        case PIN_TASK:
        case PIN_HANDLE: return 0x29;
        default: return 0x28;
    }
}

static unsigned store_op(pin_type type)
{
    switch (type) {
        case PIN_BOOL:
        case PIN_U8:
        case PIN_I8: return 0x3a;
        case PIN_U16:
        case PIN_I16: return 0x3b;
        case PIN_F32: return 0x38;
        case PIN_F64: return 0x39;
        case PIN_I64:
        case PIN_U64:
        case PIN_TASK:
        case PIN_HANDLE: return 0x37;
        default: return 0x36;
    }
}

static size_t slot_offset(int slot)
{
    return (size_t)slot * sizeof(pin_value) + offsetof(pin_value, d);
}

static void load(writer *w, int slot, pin_type type)
{
    local(w, 0x20, VALUES);
    memory(w, load_op(type), slot_offset(slot));
}

static unsigned binary_op(behavior_function_op op, pin_type type)
{
    int kind = value_type(type);
    bool sign = behavior_scalar_signed(type);
    if (kind == 0x7d || kind == 0x7c) {
        unsigned base = kind == 0x7d ? 0x92 : 0xa0;
        switch (op) {
            case BF_ADD: return base;
            case BF_SUB: return base + 1;
            case BF_MUL: return base + 2;
            case BF_DIV: return base + 3;
            default: break;
        }
        unsigned comparison = kind == 0x7d ? 0x5b : 0x61;
        switch (op) {
            case BF_EQ: return comparison;
            case BF_NE: return comparison + 1;
            case BF_LT: return comparison + 2;
            case BF_GT: return comparison + 3;
            case BF_LE: return comparison + 4;
            case BF_GE: return comparison + 5;
            default: return 0;
        }
    }
    unsigned base = kind == 0x7e ? 0x7c : 0x6a;
    switch (op) {
        case BF_ADD: return base;
        case BF_SUB: return base + 1;
        case BF_MUL: return base + 2;
        case BF_BIT_AND: return base + 7;
        case BF_BIT_OR: return base + 8;
        case BF_BIT_XOR: return base + 9;
        case BF_SHL: return base + 10;
        case BF_SHR: return base + (sign ? 11 : 12);
        default: break;
    }
    unsigned comparison = kind == 0x7e ? 0x51 : 0x46;
    switch (op) {
        case BF_EQ: return comparison;
        case BF_NE: return comparison + 1;
        case BF_LT: return comparison + (sign ? 2 : 3);
        case BF_GT: return comparison + (sign ? 4 : 5);
        case BF_LE: return comparison + (sign ? 6 : 7);
        case BF_GE: return comparison + (sign ? 8 : 9);
        default: return 0;
    }
}

static bool direct(const behavior_function *f, const behavior_function_instruction *i)
{
    if (i->indirect) {
        return false;
    }
    /* The machine helper checks what a development build reports. */
    if (behavior_integer_site(f, i, 0) && behavior_integer_reportable(f, i)) {
        return false;
    }
    if (i->op == BF_SAVE) {
        return !behavior_function_inspectable(f) || !f->locals[i->target].parameter;
    }
    if (i->op == BF_VARIABLE) {
        return true;
    }
    if (i->op == BF_STORE || i->op == BF_RESULT) {
        return (i->type >= PIN_BOOL && i->type <= PIN_F64) || i->type == PIN_ENUM ||
               (i->type == PIN_HANDLE || i->type == PIN_TASK ||
                i->type == PIN_UNION || i->type == PIN_POINTER);
    }
    if (i->op == BF_LOAD || i->op == BF_READ || i->op == BF_WRITE) {
        return ((i->type >= PIN_BOOL && i->type <= PIN_F64) || i->type == PIN_POINTER) &&
               i->offset <= UINT32_MAX;
    }
    if (i->op == BF_JUMP || i->op == BF_BRANCH || i->op == BF_CONSTANT ||
        i->op == BF_LOCAL) {
        return true;
    }
    if (i->op == BF_NEG || i->op == BF_ABS) {
        return i->type == PIN_F32 || i->type == PIN_F64;
    }
    if (i->op == BF_BIT_NOT) {
        return i->type >= PIN_I8 && i->type <= PIN_U64;
    }
    if (i->left < 0 || i->left >= acount(f->slots)) {
        return false;
    }
    pin_type type = f->slots[i->left];
    if (type < PIN_BOOL || (type > PIN_F64 && type != PIN_HANDLE && type != PIN_TASK &&
                            type != PIN_UNION && type != PIN_ENUM)) {
        return false;
    }
    if (i->right < 0 || i->right >= acount(f->slots) || f->slots[i->right] != type) {
        return false;
    }
    if (i->op == BF_EQ || i->op == BF_NE || behavior_function_bitwise(i->op)) {
        return binary_op(i->op, type) != 0;
    }
    if (type != PIN_F32 && type != PIN_F64 && !(type >= PIN_I8 && type <= PIN_U64)) {
        return false;
    }
    return binary_op(i->op, type) != 0;
}

static void copied_value(writer *w, int output, int input)
{
    for (size_t offset = 0; offset < sizeof(pin_value); offset += 4) {
        local(w, 0x20, VALUES);
        local(w, 0x20, VALUES);
        memory(w, 0x28, (size_t)input * sizeof(pin_value) + offset);
        memory(w, 0x36, (size_t)output * sizeof(pin_value) + offset);
    }
}

static void emitted_value(writer *w, const behavior_function *f, const behavior_function_instruction *i)
{
    if (i->op == BF_CONSTANT) {
        for (size_t offset = 0; offset < sizeof(pin_value); offset += 4) {
            int32_t bits;
            memcpy(&bits, (const char *)&i->value + offset, sizeof(bits));
            local(w, 0x20, VALUES);
            constant(w, bits);
            memory(w, 0x36, (size_t)i->output * sizeof(pin_value) + offset);
        }
        return;
    }
    if (i->op == BF_LOCAL) {
        copied_value(w, i->output, acount(f->slots) + 1 + i->left);
        return;
    }
    if (i->op == BF_SAVE) {
        copied_value(w, acount(f->slots) + 1 + i->target, i->left);
        return;
    }
    if (i->op == BF_VARIABLE) {
        for (size_t offset = 0; offset < sizeof(pin_value); offset += 4) {
            local(w, 0x20, VALUES);
            local(w, 0x20, CONTEXT);
            memory(w, 0x28, offsetof(behavior_function_context, variables));
            memory(w, 0x28, (size_t)i->left * sizeof(pin_value) + offset);
            memory(w, 0x36, (size_t)i->output * sizeof(pin_value) + offset);
        }
        return;
    }
    if (i->op == BF_STORE || i->op == BF_RESULT) {
        local(w, 0x20, CONTEXT);
        memory(
            w,
            0x28,
            i->op == BF_STORE ? offsetof(behavior_function_context, variables)
                              : offsetof(behavior_function_context, results)
        );
        load(w, i->left, i->type);
        memory(w, store_op(i->type), slot_offset(i->target));
        return;
    }
    if (i->op == BF_WRITE) {
        load(w, i->right, PIN_U32);
        load(w, i->left, i->type);
        memory(w, store_op(i->type), i->offset);
        return;
    }
    local(w, 0x20, VALUES);
    constant(w, f->slots[i->output]);
    memory(w, 0x36, (size_t)i->output * sizeof(pin_value) + offsetof(pin_value, type));
    local(w, 0x20, VALUES);
    pin_type type = f->slots[i->left];
    if (i->op == BF_LOAD || i->op == BF_READ) {
        if (i->op == BF_READ) {
            load(w, i->left, PIN_U32);
            byte(w, 0x04);
            byte(w, value_type(i->type));
        }
        load(w, i->left, PIN_U32);
        memory(w, load_op(i->type), i->offset);
        if (i->op == BF_READ) {
            byte(w, 0x05);
            int kind = value_type(i->type);
            byte(
                w,
                kind == 0x7e   ? 0x42
                : kind == 0x7d ? 0x43
                : kind == 0x7c ? 0x44
                               : 0x41
            );
            if (kind == 0x7d || kind == 0x7c) {
                static const uint64_t zero = 0;
                raw(w, &zero, kind == 0x7d ? 4 : 8);
            } else {
                signed_leb(w, 0);
            }
            byte(w, 0x0b);
        }
        memory(w, store_op(i->type), slot_offset(i->output));
        return;
    }
    load(w, i->left, type);
    if (i->op == BF_NEG || i->op == BF_ABS) {
        byte(w, (type == PIN_F32 ? 0x8b : 0x99) + (i->op == BF_NEG));
    } else if (i->op == BF_BIT_NOT) {
        byte(w, value_type(type) == 0x7e ? 0x42 : 0x41);
        signed_leb(w, -1);
        byte(w, value_type(type) == 0x7e ? 0x85 : 0x73);
    } else {
        load(w, i->right, type);
        byte(w, binary_op(i->op, type));
    }
    memory(w, store_op(f->slots[i->output]), slot_offset(i->output));
}

static size_t observer_callback(behavior_function_op op)
{
    switch (op) {
        case BF_SOURCE_POINT:
            return offsetof(behavior_function_context, source_pause);
        case BF_SOURCE_WATCH:
            return offsetof(behavior_function_context, source_capture);
        case BF_POINT: return offsetof(behavior_function_context, inspect);
        case BF_WATCH_BEGIN:
        case BF_WATCH_RESET:
        case BF_WATCH_VALUE:
        case BF_WATCH_PRESENT:
        case BF_WATCH_END: return offsetof(behavior_function_context, watch);
        case BF_TRACE: return offsetof(behavior_function_context, trace);
        default: return 0;
    }
}

static void helper(writer *w, int pc)
{
    local(w, 0x20, MACHINE);
    constant(w, pc);
    byte(w, 0x10);
    unsigned_leb(w, 0);
    local(w, 0x22, STATUS);
    constant(w, EXECUTION_CONTINUE);
    byte(w, 0x47); /* i32.ne */
    byte(w, 0x04);
    byte(w, 0x40);
    local(w, 0x20, STATUS);
    byte(w, 0x0f);
    byte(w, 0x0b);
}

static void branch_if(writer *w, int depth)
{
    byte(w, 0x0d);
    unsigned_leb(w, (uint32_t)depth);
}

static void component_read(writer *w, const behavior_function_instruction *i, int pc)
{
    byte(w, 0x02);
    byte(w, 0x40); /* done */
    byte(w, 0x02);
    byte(w, 0x40); /* failed guard */
    local(w, 0x20, CONTEXT);
    memory(w, 0x28, offsetof(behavior_function_context, world));
    local(w, 0x22, WORLD);
    byte(w, 0x45); /* i32.eqz */
    branch_if(w, 0);
    load(w, i->left, PIN_U32);
    local(w, 0x22, ENTITY);
    byte(w, 0x45);
    branch_if(w, 0);
    local(w, 0x20, ENTITY);
    local(w, 0x20, WORLD);
    memory(w, 0x28, offsetof(ecs_execution_view, slot_count));
    byte(w, 0x4f); /* i32.ge_u */
    branch_if(w, 0);
    local(w, 0x20, VALUES);
    memory(w, 0x28, slot_offset(i->left) + sizeof(uint32_t));
    local(w, 0x22, GENERATION);
    byte(w, 0x45);
    branch_if(w, 0);
    local(w, 0x20, WORLD);
    memory(w, 0x28, offsetof(ecs_execution_view, occupied));
    local(w, 0x20, ENTITY);
    byte(w, 0x6a); /* i32.add */
    memory(w, 0x2d, 0);
    byte(w, 0x45);
    branch_if(w, 0);
    local(w, 0x20, WORLD);
    memory(w, 0x28, offsetof(ecs_execution_view, generations));
    local(w, 0x20, ENTITY);
    constant(w, 2);
    byte(w, 0x74); /* i32.shl */
    byte(w, 0x6a);
    memory(w, 0x28, 0);
    local(w, 0x20, GENERATION);
    byte(w, 0x47); /* i32.ne */
    branch_if(w, 0);

    local(w, 0x20, WORLD);
    memory(w, 0x28, offsetof(ecs_execution_view, capacity));
    local(w, 0x22, MASK);
    byte(w, 0x45);
    branch_if(w, 0);
    local(w, 0x20, MASK);
    constant(w, 1);
    byte(w, 0x6b); /* i32.sub */
    local(w, 0x22, MASK);
    uint64_t hash = i->value.u64 * UINT64_C(11400714819323198485);
    constant(w, (int32_t)(hash ^ (hash >> 32)));
    byte(w, 0x71); /* i32.and */
    local(w, 0x21, BUCKET);
    byte(w, 0x02);
    byte(w, 0x40); /* found column */
    byte(w, 0x03);
    byte(w, 0x40); /* lookup probe */
    local(w, 0x20, WORLD);
    memory(w, 0x28, offsetof(ecs_execution_view, lookup));
    local(w, 0x20, BUCKET);
    constant(w, 2);
    byte(w, 0x74);
    byte(w, 0x6a);
    memory(w, 0x28, 0);
    local(w, 0x22, COLUMN);
    byte(w, 0x45);
    branch_if(w, 2);
    local(w, 0x20, COLUMN);
    constant(w, 1);
    byte(w, 0x6b);
    constant(w, sizeof(ecs_execution_column));
    byte(w, 0x6c); /* i32.mul */
    local(w, 0x20, WORLD);
    memory(w, 0x28, offsetof(ecs_execution_view, columns));
    byte(w, 0x6a);
    local(w, 0x22, COLUMN);
    memory(w, 0x29, offsetof(ecs_execution_column, id));
    byte(w, 0x42); /* i64.const */
    signed_leb(w, (int64_t)i->value.u64);
    byte(w, 0x51); /* i64.eq */
    branch_if(w, 1);
    local(w, 0x20, BUCKET);
    constant(w, 1);
    byte(w, 0x6a);
    local(w, 0x20, MASK);
    byte(w, 0x71);
    local(w, 0x21, BUCKET);
    byte(w, 0x0c);
    unsigned_leb(w, 0);
    byte(w, 0x0b); /* lookup probe */
    byte(w, 0x0b); /* found column */

    local(w, 0x20, COLUMN);
    memory(w, 0x28, offsetof(ecs_execution_column, occupancy));
    local(w, 0x20, ENTITY);
    constant(w, 6);
    byte(w, 0x76); /* i32.shr_u */
    constant(w, 3);
    byte(w, 0x74);
    byte(w, 0x6a);
    memory(w, 0x29, 0);
    local(w, 0x20, ENTITY);
    byte(w, 0xad); /* i64.extend_i32_u; shifts mask the count to six bits. */
    byte(w, 0x88); /* i64.shr_u */
    byte(w, 0x42);
    signed_leb(w, 1);
    byte(w, 0x83); /* i64.and */
    byte(w, 0x50); /* i64.eqz */
    branch_if(w, 0);
    local(w, 0x20, COLUMN);
    memory(w, 0x29, offsetof(ecs_execution_column, layout));
    byte(w, 0x42);
    signed_leb(w, (int64_t)i->layout);
    byte(w, 0x52); /* i64.ne */
    branch_if(w, 0);
    local(w, 0x20, COLUMN);
    memory(w, 0x28, offsetof(ecs_execution_column, stride));
    constant(w, (int32_t)i->offset);
    byte(w, 0x47);
    branch_if(w, 0);

    local(w, 0x20, VALUES);
    constant(w, i->type);
    memory(w, 0x36, (size_t)i->output * sizeof(pin_value) + offsetof(pin_value, type));
    local(w, 0x20, VALUES);
    local(w, 0x20, COLUMN);
    memory(w, 0x28, offsetof(ecs_execution_column, data));
    local(w, 0x20, ENTITY);
    constant(w, (int32_t)i->offset);
    byte(w, 0x6c);
    byte(w, 0x6a);
    memory(w, load_op(i->type), (size_t)i->target);
    memory(w, store_op(i->type), slot_offset(i->output));
    local(w, 0x20, CONTEXT);
    constant(w, 0);
    memory(w, 0x36, offsetof(behavior_function_context, error_message));
    byte(w, 0x0c);
    unsigned_leb(w, 1);
    byte(w, 0x0b); /* failed guard */
    /* Keep failure metadata in the canonical VM path. Successful reads never
     * invoke the helper or retain a row pointer between instructions. */
    helper(w, pc);
    byte(w, 0x0b); /* done */
}

static void query_variable(writer *w, const behavior_function_instruction *i, int pc)
{
    local(w, 0x20, CONTEXT);
    memory(w, 0x28, offsetof(behavior_function_context, query_variables));
    local(w, 0x22, STATUS);
    byte(w, 0x04);
    byte(w, 0x40);
    local(w, 0x20, VALUES);
    constant(w, PIN_HANDLE);
    memory(w, 0x36, (size_t)i->output * sizeof(pin_value) + offsetof(pin_value, type));
    local(w, 0x20, VALUES);
    local(w, 0x20, STATUS);
    memory(w, 0x29, (size_t)(i->target - 1) * sizeof(uint64_t));
    memory(w, 0x37, slot_offset(i->output));
    local(w, 0x20, CONTEXT);
    constant(w, 0);
    memory(w, 0x36, offsetof(behavior_function_context, error_message));
    byte(w, 0x05);
    /* The slow failure path publishes the same source and budget diagnostics. */
    helper(w, pc);
    byte(w, 0x0b);
}

bool execution_wasm_emit(
    const behavior_function *f,
    bool inspected,
    unsigned char **bytes,
    size_t *size,
    char *error,
    size_t error_size
)
{
    assert(f && bytes && size);
    behavior_function runtime;
    if (!inspected && f->runtime_body) {
        runtime = *f;
        runtime.instructions = f->runtime_body;
        f = &runtime;
    }
    *bytes = NULL;
    *size = 0;
    int count = acount(f->instructions);
    if (count <= 0 || count > 65536 || acount(f->slots) > 65536) {
        if (error_size) {
            snprintf(error, error_size, "Wasm function exceeds compiler limits");
        }
        return false;
    }
    bool *leader = xcalloc((size_t)count + 1, sizeof(*leader));
    leader[0] = true;
    bool component_reads = false;
    for (int pc = 0; pc < count; pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        component_reads |= i->op == BF_COMPONENT_READ;
        if (i->op == BF_JUMP || i->op == BF_BRANCH) {
            if (i->target < 0 || i->target >= count) {
                free(leader);
                if (error_size) {
                    snprintf(error, error_size, "Wasm branch target is invalid");
                }
                return false;
            }
            leader[i->target] = true;
            leader[pc + 1] = true;
        }
    }
    /* Helpers either terminate this entry or continue at pc + 1. Only actual
     * control transfers and saved continuations need dispatcher entries. */
    for (int index = 0; index < acount(f->suspensions); index++) {
        int resume = f->suspensions[index].pc + 1;
        if (resume >= 0 && resume < count) {
            leader[resume] = true;
        }
    }
    int *starts = NULL;
    int *blocks = xmalloc((size_t)count * sizeof(*blocks));
    for (int pc = 0; pc < count; pc++) {
        if (leader[pc]) {
            apush(starts, pc);
        }
        blocks[pc] = acount(starts) - 1;
    }
    free(leader);
    int block_count = acount(starts);
    writer module = { 0 }, part = { 0 }, code = { 0 };
    static const unsigned char magic[] = { 0, 'a', 's', 'm', 1, 0, 0, 0 };
    raw(&module, magic, sizeof(magic));
    unsigned_leb(&part, 2);
    byte(&part, 0x60);
    unsigned_leb(&part, 2);
    byte(&part, 0x7f);
    byte(&part, 0x7f);
    unsigned_leb(&part, 1);
    byte(&part, 0x7f);
    byte(&part, 0x60);
    unsigned_leb(&part, 1);
    byte(&part, 0x7f);
    unsigned_leb(&part, 1);
    byte(&part, 0x7f);
    section(&module, 1, &part);
    unsigned_leb(&part, 2);
    name(&part, "env");
    name(&part, "step");
    byte(&part, 0);
    unsigned_leb(&part, 0);
    name(&part, "env");
    name(&part, "memory");
    byte(&part, 2);
#ifdef __EMSCRIPTEN_PTHREADS__
    byte(&part, 3);
    unsigned_leb(&part, 0);
    unsigned_leb(&part, 65536);
#else
    byte(&part, 0);
    unsigned_leb(&part, 0);
#endif
    section(&module, 2, &part);
    unsigned_leb(&part, 1);
    unsigned_leb(&part, 1);
    section(&module, 3, &part);
    unsigned_leb(&part, 1);
    name(&part, "run");
    byte(&part, 0);
    unsigned_leb(&part, 1);
    section(&module, 7, &part);
    unsigned_leb(&code, 1);
    unsigned_leb(&code, component_reads ? BUCKET : CONTEXT);
    byte(&code, 0x7f);
    local(&code, 0x20, MACHINE);
    memory(&code, 0x28, offsetof(execution_machine, values));
    local(&code, 0x21, VALUES);
    local(&code, 0x20, MACHINE);
    memory(&code, 0x28, offsetof(execution_machine, context));
    local(&code, 0x21, CONTEXT);
    local(&code, 0x20, MACHINE);
    memory(&code, 0x28, offsetof(execution_machine, pc));
    local(&code, 0x21, PC);
    byte(&code, 0x03);
    byte(&code, 0x40); /* dispatch loop */
    byte(&code, 0x02);
    byte(&code, 0x40); /* invalid PC */
    for (int block = block_count - 1; block >= 0; block--) {
        byte(&code, 0x02);
        byte(&code, 0x40);
    }
    local(&code, 0x20, PC);
    byte(&code, 0x0e);
    unsigned_leb(&code, (uint32_t)count);
    for (int pc = 0; pc < count; pc++) {
        unsigned_leb(&code, (uint32_t)blocks[pc]);
    }
    unsigned_leb(&code, (uint32_t)block_count);
    for (int block = 0; block < block_count; block++) {
        byte(&code, 0x0b);
        int end = block + 1 < block_count ? starts[block + 1] : count;
        int next = end;
        for (int pc = starts[block]; pc < end; pc++) {
            if (!inspected && !behavior_function_runtime_keeps(f, pc)) {
                continue;
            }
            const behavior_function_instruction *i = &f->instructions[pc];
            if (i->op == BF_JUMP) {
                next = i->target;
                break;
            }
            if (i->op == BF_BRANCH) {
                constant(&code, i->target);
                constant(&code, pc + 1);
                load(&code, i->left, PIN_BOOL);
                byte(&code, 0x1b);
                local(&code, 0x21, PC);
                byte(&code, 0x0c);
                unsigned_leb(&code, (uint32_t)(block_count - block));
                break;
            }
            if (i->op == BF_QUERY_VARIABLE) {
                query_variable(&code, i, pc);
            } else if (i->op == BF_COMPONENT_READ) {
                component_read(&code, i, pc);
            } else if (direct(f, i)) {
                bool shift = i->op == BF_SHL || i->op == BF_SHR;
                if (shift) {
                    // Wasm masks raw shift counts, so a count at or past the
                    // width takes the canonical step instead of the direct one.
                    pin_type type = f->slots[i->left];
                    bool wide = value_type(type) == 0x7e;
                    load(&code, i->right, type);
                    byte(&code, wide ? 0x42 : 0x41);
                    signed_leb(&code, (int)behavior_scalar_size(type) * 8);
                    byte(&code, wide ? 0x5a : 0x4f); // i64.ge_u / i32.ge_u
                    byte(&code, 0x04);
                    byte(&code, 0x40);
                    helper(&code, pc);
                    byte(&code, 0x05);
                }
                emitted_value(&code, f, i);
                if (shift) {
                    byte(&code, 0x0b);
                }
            } else {
                /* Checked component writes deliberately use the canonical step
                 * import, including its authoritative publication callback. */
                size_t callback = observer_callback(i->op);
                if (callback) {
                    local(&code, 0x20, CONTEXT);
                    memory(&code, 0x28, callback);
                    byte(&code, 0x04);
                    byte(&code, 0x40);
                }
                helper(&code, pc);
                if (callback) {
                    byte(&code, 0x0b);
                }
            }
        }
        constant(&code, next);
        local(&code, 0x21, PC);
        byte(&code, 0x0c);
        unsigned_leb(&code, (uint32_t)(block_count - block));
    }
    byte(&code, 0x0b); /* invalid PC block */
    constant(&code, -1);
    byte(&code, 0x0f);
    byte(&code, 0x0b); /* loop */
    byte(&code, 0x00); /* unreachable */
    byte(&code, 0x0b); /* function */
    unsigned_leb(&part, 1);
    unsigned_leb(&part, (uint32_t)acount(code.data));
    raw(&part, code.data, acount(code.data));
    section(&module, 10, &part);
    afree(code.data);
    afree(starts);
    free(blocks);
    *size = (size_t)acount(module.data);
    *bytes = xmalloc(*size);
    memcpy(*bytes, module.data, *size);
    afree(module.data);
    return true;
}
