#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow.h"
#include "support/array.h"

#include <math.h>
#include <string.h>
static const char *field(pin_type type)
{
    return behavior_scalar_field(type);
}
static void constant(FILE *out, pin_type type, pin_value value)
{
    if (behavior_scalar_integer(type) || type == PIN_HANDLE) {
        uint64_t bits = behavior_scalar_bits(value);
        fprintf(out, "(%s)UINT64_C(%llu)", behavior_scalar_c_type(type), (unsigned long long)bits);
        return;
    }
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
static void type_name(FILE *out, const char *name)
{
    if (!name) {
        fputs("0", out);
        return;
    }
    fputs("(uintptr_t)\"", out);
    for (const char *p = name; *p; p++) {
        if (*p == '\\' || *p == '\"') {
            fputc('\\', out);
        }
        fputc(*p, out);
    }
    fputc('\"', out);
}
static void enter_locals(FILE *out, const behavior_function *f, int frame)
{
    for (int i = 0; i < acount(f->locals); i++) {
        if (f->locals[i].frame != frame) {
            continue;
        }
        fprintf(out, "l%d=", i);
        if (f->locals[i].offset == SIZE_MAX - 1) {
            type_name(out, f->locals[i].initial.str);
        } else {
            constant(out, f->locals[i].initial.type, f->locals[i].initial);
        }
        fprintf(out, ";\n");
    }
}
static void failure(FILE *out, const behavior_function_instruction *i, behavior_function_failure reason)
{
    if (i->op != BF_QUERY) {
        fputs("c->error_message=(const char*)", out);
        type_name(out, i->message);
        fputs(";", out);
    }
    fprintf(out, "c->error_stack=%d;", i->stack);
    fprintf(
        out,
        "c->error_steps=steps;c->steps=%s;c->node_id=%lluULL;c->error_frame=%d;"
        "c->error_source=%d;c->error_pin=%d;c->failure=%d;return %d;\n",
        i->frame ? "s0" : "steps",
        (unsigned long long)i->node_id,
        i->frame,
        i->source,
        reason >= BEHAVIOR_FUNCTION_DATA ? i->pin : -1,
        reason,
        reason == BEHAVIOR_FUNCTION_STATE       ? FLOW_ERR_STATE
        : reason == BEHAVIOR_FUNCTION_HOST_FLOW ? FLOW_ERR_NODE
        : reason >= BEHAVIOR_FUNCTION_NO_RETURN ? FLOW_ERR_INVALID_GRAPH
                                                : FLOW_ERR_STEP_BUDGET
    );
}
static void memory(FILE *out, const behavior_function_instruction *i)
{
    if (i->op == BF_STORE) {
        fprintf(
            out,
            "memcpy((char*)c->variable_storage[%d].data+%zu,&v%d,sizeof(v%d));"
            "\n",
            i->target,
            i->offset,
            i->left,
            i->left
        );
        return;
    }
    if (i->op == BF_RESULT) {
        fprintf(
            out,
            "if(c->results[%d].ptr)memcpy((char*)c->results[%d].ptr+%zu,&v%d,"
            "sizeof(v%d));\n",
            i->target,
            i->target,
            i->offset,
            i->left,
            i->left
        );
        return;
    }
    bool argument = i->op == BF_ARGUMENT ||
                    (i->op == BF_PRESENT && i->target == BF_ARGUMENT);
    const char *base = argument ? "arguments" : "variables";
    if (argument) {
        fprintf(out, "v%d=", i->output);
        constant(out, i->type, i->value);
        fprintf(out, ";\nif(c->argument_count>%d && c->arguments[%d].type==%d){\n", i->left, i->left, PIN_STRUCT);
    }
    if (i->op == BF_PRESENT) {
        fprintf(out, "v%d=c->%s[%d].ptr!=NULL;\n", i->output, base, i->left);
    } else {
        fprintf(
            out,
            "v%d=0; if(c->%s[%d].ptr)memcpy(&v%d,(const "
            "char*)c->%s[%d].ptr+%zu,sizeof(v%d));\n",
            i->output,
            base,
            i->left,
            i->output,
            base,
            i->left,
            i->offset,
            i->output
        );
    }
    if (argument) {
        fprintf(out, "}\n");
    }
}
static void body(const behavior_function *f, FILE *out, char prefix)
{
    for (int j = 0; j < acount(f->slots); j++) {
        fprintf(out, "%s v%d=0;\n", behavior_scalar_c_type(f->slots[j]), j);
    }
    for (int j = 0; j < acount(f->locals); j++) {
        pin_type type = f->locals[j].initial.type;
        fprintf(out, "%s l%d=0;\n", behavior_scalar_c_type(type), j);
    }
    fprintf(out, "uintptr_t steps=c->steps;\n");
    for (int j = 0; j < acount(f->frames); j++) {
        fprintf(out, "uintptr_t s%d=0;\n", j);
    }
    enter_locals(out, f, 0);
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        fprintf(out, "%c%d: ;\n", prefix, pc);
        if (i->indirect || i->op == BF_PRESENT) {
            memory(out, i);
            continue;
        }
        switch (i->op) {
            case BF_QUERY:
                fprintf(out, "{behavior_query_binding *q=&c->queries[%d];c->error_message=q->read(q->service,q->key,&q->snapshot);if(c->error_message){", i->target);
                failure(out, i, BEHAVIOR_FUNCTION_HOST_DATA);
                fprintf(out, "}v%d=(uintptr_t)&q->snapshot;}\n", i->output);
                break;
            case BF_ARRAY:
                fprintf(out, "{const pin_value *a=");
                if (i->target == BF_ARGUMENT) {
                    fprintf(
                        out,
                        "c->argument_count>%d&&c->arguments[%d].type==%d?&c->"
                        "arguments[%d]:0;",
                        i->left,
                        i->left,
                        i->value.type,
                        i->left
                    );
                } else if (i->target == BF_VARIABLE) {
                    fprintf(out, "&c->variables[%d];", i->left);
                } else {
                    fprintf(out, "0;");
                }
                fprintf(out, "v%d=a&&a->container==%d?(uintptr_t)a->ptr:0;}\n", i->output, PIN_CONTAINER_ARRAY);
                break;
            case BF_STORAGE_NAME:
                fprintf(out, "v%d=(uintptr_t)c->variable_storage[%d].type_name;\n", i->output, i->left);
                break;
            case BF_OWN_NAME:
                fprintf(out, "v%d=(uintptr_t)c->own_type_name(c->value_owner,(const char*)(uintptr_t)v%d);\n", i->output, i->left);
                break;
            case BF_STORE_HEADER:
                fprintf(
                    out,
                    "c->variables[%d]=(pin_value){.type=%d,.type_name=(const "
                    "char*)(uintptr_t)v%d,.ptr=v%d?c->variable_storage[%d]."
                    "data:NULL};\n",
                    i->target,
                    PIN_STRUCT,
                    i->left,
                    i->right,
                    i->target
                );
                break;
            case BF_OWNER: fprintf(out, "v%d=c->owner;\n", i->output); break;
            case BF_WORLD:
                fprintf(out, "v%d=(uintptr_t)c->%s;\n", i->output, i->target ? "binding_world" : "world");
                break;
            case BF_ENTITY:
                fprintf(
                    out,
                    "v%d=ecs_execution_entity((const "
                    "ecs_execution_view*)(uintptr_t)v%d,v%d);\n",
                    i->output,
                    i->left,
                    i->right
                );
                break;
            case BF_COMPONENT:
                fprintf(
                    out,
                    "{const ecs_execution_column "
                    "*column=ecs_execution_component((const "
                    "ecs_execution_view*)(uintptr_t)v%d,v%d,UINT64_C(%llu));\n",
                    i->left,
                    i->right,
                    (unsigned long long)i->value.u64
                );
                if (i->type == PIN_BOOL) {
                    fprintf(out, "v%d=column!=NULL;}\n", i->output);
                    break;
                }
                fprintf(
                    out,
                    "if(column&&(column->layout!=UINT64_C(%llu)||column->"
                    "stride!=%zu)){",
                    (unsigned long long)i->layout,
                    i->offset
                );
                failure(out, i, i->target);
                fprintf(out, "}v%d=column?(uintptr_t)column->data+v%d*column->stride:0;}\n", i->output, i->right);
                break;
            case BF_LOAD:
            case BF_READ:
                if (i->op == BF_READ) {
                    fprintf(out, "if(!v%d)v%d=0;else ", i->left, i->output);
                }
                fprintf(
                    out,
                    "memcpy(&v%d,(const "
                    "char*)(uintptr_t)v%d+%zu,sizeof(v%d));\n",
                    i->output,
                    i->left,
                    i->offset,
                    i->output
                );
                break;
            case BF_WRITE:
                fprintf(
                    out,
                    "memcpy((char*)(uintptr_t)v%d+%zu,&v%d,sizeof(v%d));\n",
                    i->right,
                    i->offset,
                    i->left,
                    i->left
                );
                break;
            case BF_ASSERT:
                fprintf(out, "if(!v%d){", i->left);
                failure(out, i, i->target);
                fprintf(out, "}\n");
                break;
            case BF_TYPE_NAME:
                fprintf(out, "v%d=", i->output);
                type_name(out, i->value.str);
                fprintf(out, ";\n");
                if (i->target == BF_ARGUMENT) {
                    fprintf(
                        out,
                        "if(c->argument_count>%d && "
                        "c->arguments[%d].type==%d)v%d=(uintptr_t)c->arguments["
                        "%d].type_name;\n",
                        i->left,
                        i->left,
                        PIN_STRUCT,
                        i->output,
                        i->left
                    );
                } else if (i->target == BF_VARIABLE) {
                    fprintf(out, "v%d=(uintptr_t)c->variables[%d].type_name;\n", i->output, i->left);
                }
                break;
            case BF_RESULT_TYPE_NAME:
                fprintf(out, "c->results[%d].type_name=(const char*)(uintptr_t)v%d;\n", i->target, i->left);
                break;
            case BF_SELECT:
                fprintf(out, "v%d=v%d?v%d:v%d;\n", i->output, i->left, i->right, i->target);
                break;
            case BF_REQUIRE:
                fprintf(out, "if(!v%d){", i->left);
                failure(out, i, BEHAVIOR_FUNCTION_DATA);
                fprintf(out, "}\n");
                break;
            case BF_RESULT_PRESENT:
                fprintf(out, "if(!v%d)c->results[%d].ptr=NULL;\n", i->left, i->target);
                break;
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
                MATH_BINARY(ATAN2_F32, Atan2, atan2f)
                MATH_BINARY(MOD_F32, Modulo, fmodf)
                MATH_BINARY(MOD_F64, Modulo, fmod)
                MATH_BINARY(HYPOT, Length, hypot)
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
                if (i->type == PIN_F32 || i->type == PIN_F64) {
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
            case BF_CAST: {
                pin_type from = f->slots[i->left];
                const char *type = behavior_scalar_c_type(i->type);
                if (i->type == PIN_F64 || from == PIN_I32) {
                    fprintf(out, "v%d=(%s)v%d;\n", i->output, type, i->left);
                    break;
                }
                const char *low = i->type == PIN_U8  ? "0"
                                : i->type == PIN_I32 ? "INT32_MIN"
                                                     : "INT64_MIN";
                const char *high = i->type == PIN_U8  ? "UINT8_MAX"
                                 : i->type == PIN_I32 ? "INT32_MAX"
                                                      : "INT64_MAX";
                fprintf(
                    out,
                    "v%d=isnan(v%d)?0:(double)v%d<=(double)%s?%s:(double)v%d>=("
                    "double)%s?%s:(%s)v%d;\n",
                    i->output,
                    i->left,
                    i->left,
                    low,
                    low,
                    i->left,
                    high,
                    high,
                    type,
                    i->left
                );
                break;
            }
            case BF_FLAGS:
                fprintf(out, "v%d=(v%d & v%d)!=0;\n", i->output, i->left, i->right);
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
            case BF_NEG:
                if (behavior_scalar_integer(i->type)) {
                    fprintf(
                        out,
                        "v%d=(%s)(0-(%s)v%d);\n",
                        i->output,
                        behavior_scalar_c_type(i->type),
                        i->type == PIN_I32 ? "uint32_t" : "uint64_t",
                        i->left
                    );
                } else {
                    fprintf(out, "v%d=-v%d;\n", i->output, i->left);
                }
                break;
            case BF_ABS:
                if (behavior_scalar_integer(i->type)) {
                    fprintf(
                        out,
                        "v%d=v%d<0?(%s)(0-(%s)v%d):v%d;\n",
                        i->output,
                        i->left,
                        behavior_scalar_c_type(i->type),
                        i->type == PIN_I32 ? "uint32_t" : "uint64_t",
                        i->left,
                        i->left
                    );
                    break;
                }
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
                if ((i->op == BF_DIV || i->op == BF_MOD) &&
                    behavior_scalar_integer(i->type)) {
                    fprintf(
                        out,
                        "if(v%d==0 || (v%d==%s && v%d==-1)){",
                        i->right,
                        i->left,
                        i->type == PIN_I32 ? "INT32_MIN" : "INT64_MIN",
                        i->right
                    );
                    failure(out, i, BEHAVIOR_FUNCTION_DATA);
                    fprintf(out, "}\n");
                    if (i->target >= 0) {
                        fprintf(
                            out,
                            "v%d=v%d%s v%d;\n",
                            i->target,
                            i->left,
                            i->op == BF_DIV ? "%" : "/",
                            i->right
                        );
                    }
                }
                if (i->op >= BF_ADD && i->op <= BF_MUL &&
                    behavior_scalar_integer(i->type)) {
                    const char *u = i->type == PIN_I32 ? "uint32_t" : "uint64_t";
                    fprintf(
                        out,
                        "v%d=(%s)((%s)v%d%s(%s)v%d);\n",
                        i->output,
                        behavior_scalar_c_type(i->type),
                        u,
                        i->left,
                        i->op == BF_ADD   ? "+"
                        : i->op == BF_SUB ? "-"
                                          : "*",
                        u,
                        i->right
                    );
                    break;
                }
                const char *op = i->op == BF_ADD ? "+"
                               : i->op == BF_SUB ? "-"
                               : i->op == BF_MUL ? "*"
                               : i->op == BF_DIV ? "/"
                               : i->op == BF_MOD ? "%"
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
    fprintf(out, "#include \"graph/compiler/behavior_function.h\"\n#include <math.h>\n#include <string.h>\n#include \"ecs/execution.h\"\nint %s(behavior_function_context *c) {\n", name);
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
