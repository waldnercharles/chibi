#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow.h"
#include "support/array.h"

#include <math.h>
static const char *field(pin_type type)
{
    return type == PIN_BOOL ? "b" : type == PIN_F32 ? "f" : "d";
}
void behavior_function_write_c(const behavior_function *f, FILE *out, const char *name)
{
    fprintf(out, "#include \"graph/compiler/behavior_function.h\"\n#include <math.h>\nint %s(behavior_function_context *c) {\n", name);
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
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        fprintf(out, "L%d: ;\n", pc);
        switch (i->op) {
            case BF_CONSTANT: {
                double v = i->type == PIN_F32 ? i->value.f : i->value.d;
                fprintf(out, "v%d=", i->output);
                if (i->type == PIN_BOOL) {
                    fprintf(out, "%d", i->value.b);
                } else if (isnan(v)) {
                    fprintf(out, "NAN");
                } else if (isinf(v)) {
                    fprintf(out, "%sINFINITY", signbit(v) ? "-" : "");
                } else {
                    fprintf(out, "%a", v);
                }
                fprintf(out, ";\n");
                break;
            }
            case BF_VARIABLE:
                fprintf(out, "v%d=c->variables[%d].%s;\n", i->output, i->left, field(i->type));
                break;
            case BF_STORE:
                fprintf(out, "c->variables[%d].%s=v%d;\n", i->target, field(i->type), i->left);
                break;
            case BF_DELTA:
                fprintf(out, "v%d=(float)c->delta_seconds;\n", i->output);
                break;
            case BF_CHECK:
                fprintf(
                    out,
                    "if(c->steps>=c->budget){c->node_id=%lluULL;return %d;} "
                    "++c->steps;\n",
                    (unsigned long long)i->node_id,
                    FLOW_ERR_STEP_BUDGET
                );
                break;
            case BF_JUMP: fprintf(out, "goto L%d;\n", i->target); break;
            case BF_BRANCH:
                fprintf(out, "if(v%d) goto L%d;\n", i->left, i->target);
                break;
            case BF_END: fprintf(out, "return 0;\n"); break;
            case BF_NEG: fprintf(out, "v%d=-v%d;\n", i->output, i->left); break;
            case BF_ABS:
                fprintf(out, "v%d=%s(v%d);\n", i->output, i->type == PIN_F32 ? "fabsf" : "fabs", i->left);
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
    fprintf(out, "}\n");
}
