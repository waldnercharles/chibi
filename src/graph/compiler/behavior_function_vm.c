#include "ecs/execution.h"
#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void enter(const behavior_function *f, pin_value *locals, int frame)
{
    for (int i = 0; i < acount(f->locals); i++) {
        if (f->locals[i].frame == frame) {
            locals[i] = f->locals[i].initial;
        }
    }
}
static int fail(
    behavior_function_context *ctx,
    const behavior_function_instruction *i,
    uintptr_t steps,
    const uintptr_t *parents,
    behavior_function_failure reason
)
{
    ctx->node_id = i->node_id;
    ctx->error_frame = i->frame;
    ctx->error_source = i->source;
    ctx->error_pin = reason >= BEHAVIOR_FUNCTION_DATA ? i->pin : -1;
    ctx->error_steps = steps;
    ctx->steps = i->frame ? parents[0] : steps;
    ctx->failure = reason;
    ctx->error_message = i->message;
    return reason == BEHAVIOR_FUNCTION_HOST_FLOW ? FLOW_ERR_NODE
         : reason >= BEHAVIOR_FUNCTION_NO_RETURN ? FLOW_ERR_INVALID_GRAPH
                                                 : FLOW_ERR_STEP_BUDGET;
}

int behavior_function_run(const behavior_function *f, behavior_function_context *ctx, bool native)
{
    if (native) {
        assert(f->code);
        return ((int (*)(behavior_function_context *))behavior_function_entry(f))(ctx);
    }
    if (behavior_function_fast_allowed(f, ctx)) {
        f = f->fast;
    }
    pin_value *slots = xcalloc((size_t)(acount(f->slots) + acount(f->locals) + 1), sizeof *slots);
    pin_value *locals = slots + acount(f->slots) + 1;
    uintptr_t *parents = acount(f->frames) > 1
                           ? xcalloc((size_t)acount(f->frames), sizeof *parents)
                           : NULL;
    uintptr_t steps = ctx->steps;
    enter(f, locals, 0);
    int result = 0;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        pin_value *out = &slots[i->output], a = { 0 }, b = { 0 };
        if (behavior_function_left_slot(i->op)) {
            a = slots[i->left];
        }
        if (behavior_function_right_slot(i->op)) {
            b = slots[i->right];
        }
        if (i->op < BF_STORE) {
            out->type = i->type;
        }
        if (i->indirect) {
            if (i->op == BF_ARGUMENT && (i->left >= ctx->argument_count ||
                                         ctx->arguments[i->left].type != PIN_STRUCT)) {
                *out = i->value;
                continue;
            }
            const pin_value *base = i->op == BF_ARGUMENT ? &ctx->arguments[i->left]
                                  : i->op == BF_VARIABLE
                                      ? &ctx->variables[i->left]
                                      : &ctx->results[i->target];
            size_t size = behavior_scalar_size(i->type);
            if (i->op == BF_RESULT) {
                if (base->ptr) {
                    memcpy((char *)base->ptr + i->offset, &a.d, size);
                }
            } else {
                memset(&out->d, 0, sizeof out->d);
                if (base->ptr) {
                    memcpy(&out->d, (const char *)base->ptr + i->offset, size);
                }
            }
            continue;
        }
        switch (i->op) {
            case BF_OWNER: out->u64 = ctx->owner; break;
            case BF_WORLD:
                if (BF_POINTER_TYPE == PIN_U64) {
                    out->u64 = (uintptr_t)(i->target ? ctx->binding_world : ctx->world);
                } else {
                    out->u = (uint32_t)(uintptr_t)(i->target ? ctx->binding_world
                                                             : ctx->world);
                }
                break;
            case BF_ENTITY:
                out->u = ecs_execution_entity(
                    (const ecs_execution_view *)(uintptr_t)behavior_scalar_bits(a),
                    b.u64
                );
                break;
            case BF_COMPONENT: {
                const ecs_execution_column *column = ecs_execution_component(
                    (const ecs_execution_view *)(uintptr_t)behavior_scalar_bits(a),
                    b.u,
                    i->value.u64
                );
                if (i->type == PIN_BOOL) {
                    out->b = column != NULL;
                    break;
                }
                if (column &&
                    (column->layout != i->layout || column->stride != i->offset)) {
                    result = fail(ctx, i, steps, parents, i->target);
                    goto done;
                }
                uintptr_t row = column ? (uintptr_t)column->data + b.u * column->stride
                                       : 0;
                if (BF_POINTER_TYPE == PIN_U64) {
                    out->u64 = row;
                } else {
                    out->u = (uint32_t)row;
                }
                break;
            }
            case BF_LOAD:
                memcpy(
                    &out->d,
                    (const char *)(uintptr_t)behavior_scalar_bits(a) + i->offset,
                    behavior_scalar_size(i->type)
                );
                break;
            case BF_WRITE:
                memcpy(
                    (char *)(uintptr_t)behavior_scalar_bits(b) + i->offset,
                    &a.d,
                    behavior_scalar_size(i->type)
                );
                break;
            case BF_ASSERT:
                if (!behavior_scalar_bits(a)) {
                    result = fail(ctx, i, steps, parents, i->target);
                    goto done;
                }
                break;
            case BF_TYPE_NAME: {
                const char *name = i->value.str;
                if (i->target == BF_VARIABLE) {
                    name = ctx->variables[i->left].type_name;
                } else if (
                    i->target == BF_ARGUMENT && i->left < ctx->argument_count &&
                    ctx->arguments[i->left].type == PIN_STRUCT
                ) {
                    name = ctx->arguments[i->left].type_name;
                }
                if (i->type == PIN_U64) {
                    out->u64 = (uintptr_t)name;
                } else {
                    out->u = (uint32_t)(uintptr_t)name;
                }
                break;
            }
            case BF_RESULT_TYPE_NAME:
                ctx->results[i->target]
                    .type_name = (const char *)(uintptr_t)(BF_POINTER_TYPE == PIN_U64
                                                               ? slots[i->left].u64
                                                               : slots[i->left].u);
                break;
            case BF_PRESENT:
                if (i->target == BF_ARGUMENT &&
                    (i->left >= ctx->argument_count ||
                     ctx->arguments[i->left].type != PIN_STRUCT)) {
                    *out = i->value;
                } else {
                    out->b = (i->target == BF_ARGUMENT
                                  ? ctx->arguments[i->left].ptr
                                  : ctx->variables[i->left].ptr) != NULL;
                }
                break;
            case BF_SELECT:
                *out = slots[slots[i->left].b ? i->right : i->target];
                break;
            case BF_RESULT_PRESENT:
                if (!slots[i->left].b) {
                    ctx->results[i->target].ptr = NULL;
                }
                break;
            case BF_REQUIRE:
                if (!slots[i->left].b) {
                    result = fail(ctx, i, steps, parents, BEHAVIOR_FUNCTION_DATA);
                    goto done;
                }
                break;
#define MATH_UNARY(op, name, fn)                                               \
    case BF_##op: out->d = fn(a.d); break;
                BEHAVIOR_MATH_UNARY(MATH_UNARY)
#undef MATH_UNARY
#define MATH_BINARY(op, name, fn)                                              \
    case BF_##op: out->d = fn(a.d, b.d); break;
                BEHAVIOR_MATH_BINARY(MATH_BINARY)
#undef MATH_BINARY
            case BF_ATAN2_F32: out->f = atan2f(a.f, b.f); break;
            case BF_MOD_F32: out->f = fmodf(a.f, b.f); break;
            case BF_MOD_F64: out->d = fmod(a.d, b.d); break;
            case BF_HYPOT: out->d = hypot(a.d, b.d); break;
            case BF_CONSTANT: *out = i->value; break;
            case BF_VARIABLE: *out = ctx->variables[i->left]; break;
            case BF_LOCAL: *out = locals[i->left]; break;
            case BF_DELTA: out->f = (float)ctx->delta_seconds; break;
            case BF_ARGUMENT:
                *out = i->value;
                if (i->left < ctx->argument_count) {
                    pin_value arg = ctx->arguments[i->left];
                    if (arg.type == i->type) {
                        *out = arg;
                    } else if (arg.type == PIN_F32 && i->type == PIN_F64) {
                        out->d = (double)arg.f;
                    } else if (arg.type == PIN_F64 && i->type == PIN_F32) {
                        out->f = (float)arg.d;
                    }
                }
                break;
            case BF_CAST: *out = behavior_scalar_cast(a, i->type); break;
            case BF_FLAGS: out->b = (a.u & b.u) != 0; break;
            case BF_CONVERT:
                if (i->type == PIN_F32) {
                    out->f = (float)a.d;
                } else {
                    out->d = (double)a.f;
                }
                break;
            case BF_SAVE: locals[i->target] = a; break;
            case BF_ENTER:
                parents[i->frame] = steps;
                steps = 0;
                enter(f, locals, i->target);
                break;
            case BF_LEAVE:
                steps = parents[f->frames[i->target].source.parent];
                break;
            case BF_CALL_CHECK: {
                unsigned depth = (unsigned)f->frames[i->frame].depth;
                if (depth >= (unsigned)i->left ||
                    ctx->call_depth >= (unsigned)i->left - depth) {
                    result = fail(ctx, i, steps, parents, BEHAVIOR_FUNCTION_DEPTH);
                    goto done;
                }
                break;
            }
            case BF_FAIL:
                result = fail(ctx, i, steps, parents, i->left);
                goto done;
            case BF_RESULT:
            case BF_STORE: {
                pin_value *target = i->op == BF_STORE ? ctx->variables : ctx->results;
                switch (i->type) {
#define STORE(pin, type, field)                                                \
    case pin: target[i->target].field = a.field; break;
                    BEHAVIOR_SCALARS(STORE)
#undef STORE
                    default: assert(false);
                }
                break;
            }
            case BF_CHECK:
                ctx->node_id = i->node_id;
                if (steps >= ctx->budget) {
                    result = fail(ctx, i, steps, parents, BEHAVIOR_FUNCTION_BUDGET);
                    goto done;
                }
                steps++;
                break;
            case BF_JUMP: pc = i->target - 1; break;
            case BF_BRANCH:
                if (a.b) {
                    pc = i->target - 1;
                }
                break;
            case BF_END: goto done;
            case BF_RETURN: ctx->returned_node_id = i->node_id; goto done;
            default: {
                if (a.type == PIN_I32 || a.type == PIN_I64) {
                    int64_t x = a.type == PIN_I32 ? a.i : a.i64;
                    int64_t y = a.type == PIN_I32 ? b.i : b.i64;
                    int64_t minimum = a.type == PIN_I32 ? INT32_MIN : INT64_MIN;
                    if ((i->op == BF_DIV || i->op == BF_MOD) &&
                        (!y || (x == minimum && y == -1))) {
                        result = fail(ctx, i, steps, parents, BEHAVIOR_FUNCTION_DATA);
                        goto done;
                    }
                    if ((i->op == BF_DIV || i->op == BF_MOD) && i->target >= 0) {
                        pin_value *other = &slots[i->target];
                        other->type = a.type;
                        int64_t value = i->op == BF_DIV ? x % y : x / y;
                        if (a.type == PIN_I32) {
                            other->i = (int32_t)value;
                        } else {
                            other->i64 = value;
                        }
                    }
                    uint64_t value = 0;
                    switch (i->op) {
                        case BF_ADD: value = (uint64_t)x + (uint64_t)y; break;
                        case BF_SUB: value = (uint64_t)x - (uint64_t)y; break;
                        case BF_MUL: value = (uint64_t)x * (uint64_t)y; break;
                        case BF_DIV: value = (uint64_t)(x / y); break;
                        case BF_MOD: value = (uint64_t)(x % y); break;
                        case BF_NEG: value = UINT64_C(0) - (uint64_t)x; break;
                        case BF_ABS:
                            value = x < 0 ? UINT64_C(0) - (uint64_t)x : (uint64_t)x;
                            break;
                        case BF_MIN: value = (uint64_t)(x < y ? x : y); break;
                        case BF_MAX: value = (uint64_t)(x > y ? x : y); break;
                        case BF_LT: out->b = x < y; break;
                        case BF_LE: out->b = x <= y; break;
                        case BF_GT: out->b = x > y; break;
                        case BF_GE: out->b = x >= y; break;
                        case BF_EQ: out->b = x == y; break;
                        case BF_NE: out->b = x != y; break;
                        default: assert(false);
                    }
                    if (i->type == PIN_I32) {
                        out->i = (int32_t)(uint32_t)value;
                    }
                    if (i->type == PIN_I64) {
                        out->i64 = (int64_t)value;
                    }
                    break;
                }
                double x = a.type == PIN_F32 ? a.f : a.d,
                       y = b.type == PIN_F32 ? b.f : b.d, r = 0;
                switch (i->op) {
                    case BF_ADD:
                        r = i->type == PIN_F32 ? (float)(a.f + b.f) : x + y;
                        break;
                    case BF_SUB:
                        r = i->type == PIN_F32 ? (float)(a.f - b.f) : x - y;
                        break;
                    case BF_MUL:
                        r = i->type == PIN_F32 ? (float)(a.f * b.f) : x * y;
                        break;
                    case BF_DIV:
                        r = i->type == PIN_F32 ? (float)(a.f / b.f) : x / y;
                        break;
                    case BF_NEG: r = -x; break;
                    case BF_ABS: r = fabs(x); break;
                    case BF_MIN: r = x < y ? x : y; break;
                    case BF_MAX: r = x > y ? x : y; break;
                    case BF_LT: out->b = x < y; break;
                    case BF_LE: out->b = x <= y; break;
                    case BF_GT: out->b = x > y; break;
                    case BF_GE: out->b = x >= y; break;
                    case BF_EQ: out->b = x == y; break;
                    case BF_NE: out->b = x != y; break;
                    default: assert(false);
                }
                if (i->type == PIN_F32) {
                    out->f = (float)r;
                }
                if (i->type == PIN_F64) {
                    out->d = r;
                }
            }
        }
    }
done:
    if (!result) {
        ctx->steps = steps + f->root_steps;
    }
    free(parents);
    free(slots);
    return result;
}
