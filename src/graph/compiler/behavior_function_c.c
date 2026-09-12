#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow.h"
#include "support/array.h"

#include <math.h>
static const char *field(pin_type type)
{
    return type == PIN_BOOL ? "b" : type == PIN_F32 ? "f" : "d";
}
static void constant(FILE *out, pin_type type, pin_value value)
{
    double v = type == PIN_F32 ? value.f : value.d;
    if (type == PIN_BOOL) {
        fprintf(out, "%d", value.b);
    } else if (isnan(v)) {
        fprintf(out, "NAN");
    } else if (isinf(v)) {
        fprintf(out, "%sINFINITY", signbit(v) ? "-" : "");
    } else {
        fprintf(out, "%a", v);
    }
}
static void enter_locals(FILE *out, const behavior_function *f, int frame)
{
    for (int i = 0; i < acount(f->locals); i++) {
        if (f->locals[i].frame != frame) {
            continue;
        }
        fprintf(out, "l%d=", i);
        constant(out, f->locals[i].initial.type, f->locals[i].initial);
        fprintf(out, ";\n");
    }
}
static void failure(FILE *out, const behavior_function_instruction *i, behavior_function_failure reason)
{
    fprintf(
        out,
        "c->error_steps=steps;c->steps=%s;c->node_id=%lluULL;c->error_frame=%d;"
        "c->failure=%d;return %d;\n",
        i->frame ? "s0" : "steps",
        (unsigned long long)i->node_id,
        i->frame,
        reason,
        reason == BEHAVIOR_FUNCTION_NO_RETURN ? FLOW_ERR_INVALID_GRAPH : FLOW_ERR_STEP_BUDGET
    );
}
static void body(const behavior_function *f, FILE *out, char prefix)
{
    for (int j = 0; j < acount(f->slots); j++) {
        fprintf(
            out,
            "%s v%d=0;\n",
            f->slots[j] == PIN_BOOL  ? "bool"
            : f->slots[j] == PIN_F32 ? "float"
                                     : "double",
            j
        );
    }
    for (int j = 0; j < acount(f->locals); j++) {
        pin_type type = f->locals[j].initial.type;
        fprintf(
            out,
            "%s l%d=0;\n",
            type == PIN_BOOL  ? "bool"
            : type == PIN_F32 ? "float"
                              : "double",
            j
        );
    }
    fprintf(out, "uintptr_t steps=c->steps;\n");
    for (int j = 0; j < acount(f->frames); j++) {
        fprintf(out, "uintptr_t s%d=0;\n", j);
    }
    enter_locals(out, f, 0);
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        fprintf(out, "%c%d: ;\n", prefix, pc);
        switch (i->op) {
#define MATH_UNARY(op, name, fn)                                               \
    case BF_##op:                                                              \
        fprintf(out, "v%d=" #fn "(v%d);\n", i->output, i->left);               \
        break;
            BEHAVIOR_MATH_UNARY(MATH_UNARY)
#undef MATH_UNARY
#define MATH_BINARY(op, name, fn)                                              \
    case BF_##op:                                                              \
        fprintf(out, "v%d=" #fn "(v%d,v%d);\n", i->output, i->left, i->right); \
        break;
            BEHAVIOR_MATH_BINARY(MATH_BINARY)
#undef MATH_BINARY
            case BF_CONSTANT: {
                fprintf(out, "v%d=", i->output);
                constant(out, i->type, i->value);
                fprintf(out, ";\n");
                break;
            }
            case BF_VARIABLE:
                fprintf(out, "v%d=c->variables[%d].%s;\n", i->output, i->left, field(i->type));
                break;
            case BF_LOCAL:
                fprintf(out, "v%d=l%d;\n", i->output, i->left);
                break;
            case BF_SAVE: fprintf(out, "l%d=v%d;\n", i->target, i->left); break;
            case BF_ENTER:
                fprintf(out, "s%d=steps;steps=0;\n", i->frame);
                enter_locals(out, f, i->target);
                break;
            case BF_LEAVE:
                fprintf(out, "steps=s%d;\n", f->frames[i->target].source.parent);
                break;
            case BF_CALL_CHECK: {
                int limit = i->left - f->frames[i->frame].depth;
                fprintf(out, "if(%d<=0 || c->call_depth >= %du){", limit, limit > 0 ? limit : 0);
                failure(out, i, BEHAVIOR_FUNCTION_DEPTH);
                fprintf(out, "}\n");
                break;
            }
            case BF_FAIL: failure(out, i, i->left); break;
            case BF_RESULT:
            case BF_STORE:
                fprintf(
                    out,
                    "c->%s[%d].%s=v%d;\n",
                    i->op == BF_STORE ? "variables" : "results",
                    i->target,
                    field(i->type),
                    i->left
                );
                break;
            case BF_ARGUMENT:
                fprintf(out, "v%d=", i->output);
                constant(out, i->type, i->value);
                fprintf(out, ";\nif(c->argument_count>%d){\n", i->left);
                fprintf(
                    out,
                    "if(c->arguments[%d].type==%d) v%d=c->arguments[%d].%s;\n",
                    i->left,
                    i->type,
                    i->output,
                    i->left,
                    field(i->type)
                );
                if (i->type != PIN_BOOL) {
                    pin_type other = i->type == PIN_F32 ? PIN_F64 : PIN_F32;
                    fprintf(
                        out,
                        "else if(c->arguments[%d].type==%d) "
                        "v%d=(%s)c->arguments[%d].%s;\n",
                        i->left,
                        other,
                        i->output,
                        i->type == PIN_F32 ? "float" : "double",
                        i->left,
                        field(other)
                    );
                }
                fprintf(out, "}\n");
                break;
            case BF_CONVERT:
                fprintf(out, "v%d=(%s)v%d;\n", i->output, i->type == PIN_F32 ? "float" : "double", i->left);
                break;
            case BF_DELTA:
                fprintf(out, "v%d=(float)c->delta_seconds;\n", i->output);
                break;
            case BF_CHECK:
                fprintf(out, "if(steps>=c->budget){");
                failure(out, i, BEHAVIOR_FUNCTION_BUDGET);
                fprintf(out, "}++steps;\n");
                break;
            case BF_JUMP:
                fprintf(out, "goto %c%d;\n", prefix, i->target);
                break;
            case BF_BRANCH:
                fprintf(out, "if(v%d) goto %c%d;\n", i->left, prefix, i->target);
                break;
            case BF_END:
                fprintf(out, "c->steps=steps+%du;return 0;\n", f->root_steps);
                break;
            case BF_RETURN:
                fprintf(
                    out,
                    "c->returned_node_id=%lluULL;c->steps=steps+%du;return "
                    "0;\n",
                    (unsigned long long)i->node_id,
                    f->root_steps
                );
                break;
            case BF_NEG: fprintf(out, "v%d=-v%d;\n", i->output, i->left); break;
            case BF_ABS:
                fprintf(out, "v%d=%s(v%d);\n", i->output, i->type == PIN_F32 ? "fabsf" : "fabs", i->left);
                break;
            case BF_MIN:
            case BF_MAX:
                fprintf(
                    out,
                    "v%d=v%d%s v%d?v%d:v%d;\n",
                    i->output,
                    i->left,
                    i->op == BF_MIN ? "<" : ">",
                    i->right,
                    i->left,
                    i->right
                );
                break;
            default: {
                const char *op = i->op == BF_ADD ? "+"
                               : i->op == BF_SUB ? "-"
                               : i->op == BF_MUL ? "*"
                               : i->op == BF_DIV ? "/"
                               : i->op == BF_LT  ? "<"
                               : i->op == BF_LE  ? "<="
                               : i->op == BF_GT  ? ">"
                               : i->op == BF_GE  ? ">="
                               : i->op == BF_EQ  ? "=="
                                                 : "!=";
                fprintf(out, "v%d=v%d%s v%d;\n", i->output, i->left, op, i->right);
            }
        }
    }
}
void behavior_function_write_c(const behavior_function *f, FILE *out, const char *name)
{
    fprintf(out, "#include \"graph/compiler/behavior_function.h\"\n#include <math.h>\nint %s(behavior_function_context *c) {\n", name);
    if (f->fast) {
        fprintf(
            out,
            "if(c->steps<=c->budget && c->budget-c->steps>=%du && "
            "c->budget>=%du && ",
            f->fast->root_steps,
            f->fast->child_budget
        );
        if (f->fast->depth_limit) {
            fprintf(out, "c->call_depth<%du", f->fast->depth_limit);
        } else {
            fprintf(out, "1");
        }
        fprintf(out, "){\n");
        body(f->fast, out, 'F');
        fprintf(out, "}\n");
    }
    body(f, out, 'L');
    fprintf(out, "}\n");
}
