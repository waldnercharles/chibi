#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>

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
    ctx->error_pin = reason == BEHAVIOR_FUNCTION_DATA ? 0 : -1;
    ctx->error_steps = steps;
    ctx->steps = i->frame ? parents[0] : steps;
    ctx->failure = reason;
    return reason == BEHAVIOR_FUNCTION_NO_RETURN || reason == BEHAVIOR_FUNCTION_DATA
             ? FLOW_ERR_INVALID_GRAPH
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
        if (i->op >= BF_ADD && i->op <= BF_SAVE) {
            a = slots[i->left];
            b = slots[i->right];
        }
        if (i->op == BF_BRANCH) {
            a = slots[i->left];
        }
        if (i->op < BF_STORE) {
            out->type = i->type;
        }
        switch (i->op) {
#define MATH_UNARY(op, name, fn)                                               \
    case BF_##op: out->d = fn(a.d); break;
            BEHAVIOR_MATH_UNARY(MATH_UNARY)
#undef MATH_UNARY
#define MATH_BINARY(op, name, fn)                                              \
    case BF_##op: out->d = fn(a.d, b.d); break;
            BEHAVIOR_MATH_BINARY(MATH_BINARY)
#undef MATH_BINARY
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
