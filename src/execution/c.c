#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/status.h"
#include "support/array.h"
#include "types/pointer.h"
#include "types/text.h"

#include <assert.h>
#include <math.h>
#include <string.h>
static const char *field(pin_type type)
{
    return behavior_scalar_field(type);
}
static void type_name(FILE *out, const char *name);
static int text_index(const behavior_function *f, const pin_string *text)
{
    for (int n = 0; n < acount(f->instructions); n++) {
        if (f->instructions[n].op != BF_TYPE_NAME &&
            f->instructions[n].value.type == PIN_STR &&
            f->instructions[n].value.text == text) {
            return n;
        }
    }
    for (int n = 0; n < acount(f->locals); n++) {
        if (f->locals[n].offset != SIZE_MAX - 1 && f->locals[n].initial.type == PIN_STR &&
            f->locals[n].initial.text == text) {
            return acount(f->instructions) + n;
        }
    }
    assert(false);
    return -1;
}
static void constant(FILE *out, const behavior_function *f, pin_type type, pin_value value)
{
    if (type == PIN_STR) {
        if (!value.text) {
            fputs("0", out);
        } else {
            fprintf(out, "&text%d", text_index(f, value.text));
        }
        return;
    }
    if (type == PIN_NAME) {
        fputs("(const char*)", out);
        type_name(out, value.str);
        return;
    }
    if (behavior_scalar_integer(type) || (type == PIN_HANDLE || type == PIN_TASK ||
                                          type == PIN_UNION || type == PIN_POINTER)) {
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
        if ((unsigned char)*p < 32 || (unsigned char)*p == 127) {
            fprintf(out, "\\%03o", (unsigned char)*p);
        } else {
            fputc(*p, out);
        }
    }
    fputc('\"', out);
}
static void enter_locals(FILE *out, const behavior_function *f, int frame)
{
    if (f->waits) {
        fprintf(out, "c->activation->wake_times[%d]=0;\n", frame);
    }
    for (int i = 0; i < acount(f->locals); i++) {
        if (f->locals[i].frame != frame) {
            continue;
        }
        fprintf(out, "l%d=", i);
        if (f->locals[i].array_default) {
            fprintf(out, "(uintptr_t)c->data->values[%d].ptr", f->locals[i].data_index);
        } else if (f->locals[i].offset == SIZE_MAX - 1) {
            type_name(out, f->locals[i].initial.str);
        } else {
            constant(out, f, f->locals[i].initial.type, f->locals[i].initial);
        }
        fprintf(out, ";\n");
    }
}
static void failure(FILE *out, const behavior_function_instruction *i, behavior_function_failure reason)
{
    if (i->op != BF_QUERY && i->op != BF_COMMAND_BEGIN && i->op != BF_HOST_CALL &&
        i->op != BF_COMPONENT_WRITE && i->op != BF_INTERN_STRING) {
        fputs("c->error_message=(const char*)", out);
        type_name(
            out,
            i->op == BF_QUERY_VARIABLE   ? "Query variable is not bound"
            : i->op == BF_COMPONENT_READ ? "Referenced storage is unavailable"
                                         : i->message
        );
        fputs(";", out);
    }
    fprintf(out, "c->error_stack=%d;", i->stack);
    fprintf(
        out,
        "c->error_steps=steps;c->steps=%s;c->node_id=%lluULL;c->error_frame=%d;"
        "c->error_source=%d;c->error_pin=%d;c->failure=%d;",
        i->frame ? "s0" : "steps",
        (unsigned long long)i->node_id,
        i->frame,
        i->source,
        reason >= BEHAVIOR_FUNCTION_DATA ? i->pin : -1,
        reason
    );
    if (reason == BEHAVIOR_FUNCTION_COMMAND) {
        fputs("return c->error_status;\n", out);
        return;
    }
    fprintf(
        out,
        "return %d;\n",
        reason == BEHAVIOR_FUNCTION_ARGUMENT    ? FLOW_ERR_ARGUMENT
        : reason == BEHAVIOR_FUNCTION_STATE     ? FLOW_ERR_STATE
        : reason == BEHAVIOR_FUNCTION_HOST_FLOW ? FLOW_ERR_NODE
        : reason >= BEHAVIOR_FUNCTION_NO_RETURN ? FLOW_ERR_INVALID_GRAPH
                                                : FLOW_ERR_STEP_BUDGET
    );
}
/* Emits the branch where the divisor is `divisor`, whose quotient and
 * remainder are given rather than divided. Both are read before either
 * result is written, since a result may share a slot with an operand. */
static void division_case(
    FILE *out,
    const behavior_function_instruction *i,
    const char *divisor,
    const char *quotient,
    const char *remainder
)
{
    fprintf(
        out,
        "if(v%d==%s){%s quotient=%s,remainder=%s;",
        i->right,
        divisor,
        behavior_scalar_c_type(i->type),
        quotient,
        remainder
    );
    if (i->target >= 0) {
        fprintf(out, "v%d=%s;", i->target, i->op == BF_DIV ? "remainder" : "quotient");
    }
    fprintf(out, "v%d=%s;}else ", i->output, i->op == BF_DIV ? "quotient" : "remainder");
}
/* Integer division never fails. x / 0 is 0 with remainder x; a signed
 * divisor of -1 negates, so the minimum wraps to itself, with remainder 0. */
static void integer_division(FILE *out, const behavior_function_instruction *i)
{
    const char *type = behavior_scalar_c_type(i->type);
    char left[32];
    snprintf(left, sizeof left, "v%d", i->left);
    division_case(out, i, "0", "0", left);
    if (behavior_scalar_signed(i->type)) {
        const char *u = i->type == PIN_I32 ? "uint32_t" : "uint64_t";
        char negated[64];
        snprintf(negated, sizeof negated, "(%s)(0-(%s)v%d)", type, u, i->left);
        division_case(out, i, "-1", negated, "0");
    }
    const char *divide = i->op == BF_DIV ? "/" : "%",
               *other = i->op == BF_DIV ? "%" : "/";
    if (i->target >= 0) {
        fprintf(
            out,
            "{%s primary=v%d%s v%d, secondary=v%d%s "
            "v%d;v%d=secondary;v%d=primary;}\n",
            type,
            i->left,
            divide,
            i->right,
            i->left,
            other,
            i->right,
            i->target,
            i->output
        );
        return;
    }
    fprintf(out, "{v%d=v%d%s v%d;}\n", i->output, i->left, divide, i->right);
}
/* INT64_MIN has no literal spelling; its negation overflows. */
static void lower_bound(char *text, size_t size, long long low)
{
    if (low == INT64_MIN) {
        snprintf(text, size, "INT64_MIN");
        return;
    }
    snprintf(text, size, "%lldLL", low);
}
/* Mirrors behavior_scalar_cast: modes apply to integer values outside the
 * destination range; floating sources round toward zero and saturate. */
static void cast(FILE *out, const behavior_function *f, const behavior_function_instruction *i)
{
    pin_type from = f->slots[i->left];
    const char *type = behavior_scalar_c_type(i->type);
    long long low = (long long)behavior_scalar_integer_low(i->type);
    unsigned long long high = behavior_scalar_integer_high(i->type);
    char minimum[32];
    lower_bound(minimum, sizeof minimum, low);
    if (from == i->type || i->type == PIN_F64) {
        fprintf(out, "v%d=(%s)v%d;\n", i->output, type, i->left);
        return;
    }
    if (from == PIN_F32 || from == PIN_F64) {
        fprintf(
            out,
            "v%d=isnan(v%d)?0:(double)v%d<=(double)%s?(%s)%s:(double)v%d>=("
            "double)%lluULL?"
            "(%s)%lluULL:(%s)v%d;\n",
            i->output,
            i->left,
            i->left,
            minimum,
            type,
            minimum,
            i->left,
            high,
            type,
            high,
            type,
            i->left
        );
        return;
    }
    bool sign = behavior_scalar_signed(from);
    bool below = sign && low > (long long)behavior_scalar_integer_low(from);
    bool above = high < behavior_scalar_integer_high(from);
    if (i->target == BEHAVIOR_CAST_WRAP || (!below && !above)) {
        fprintf(out, "v%d=(%s)(uint64_t)v%d;\n", i->output, type, i->left);
        return;
    }
    char low_test[64] = "0", high_test[64] = "0";
    if (below) {
        snprintf(low_test, sizeof low_test, "(int64_t)v%d<%lldLL", i->left, low);
    }
    if (above) {
        snprintf(
            high_test,
            sizeof high_test,
            sign ? "(int64_t)v%d>%lluLL" : "(uint64_t)v%d>%lluULL",
            i->left,
            high
        );
    }
    fprintf(
        out,
        "v%d=%s?(%s)%s:%s?(%s)%lluULL:(%s)v%d;\n",
        i->output,
        low_test,
        type,
        minimum,
        high_test,
        type,
        high,
        type,
        i->left
    );
}
static void memory(FILE *out, const behavior_function *f, const behavior_function_instruction *i)
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
        constant(out, f, i->type, i->value);
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
static void activation_values(
    FILE *out,
    const behavior_function *f,
    const behavior_function_suspension *s,
    bool restore
)
{
    int slots = acount(f->slots), base = slots + acount(f->locals);
    for (int k = 0; k < s->value_count; k++) {
        int v = s->values[k], index = v < slots ? v : v - slots;
        char name = v < slots ? 'v' : 'l';
        if (restore) {
            fprintf(out, "memcpy(&%c%d,&c->activation->values[%d],sizeof(%c%d));\n", name, index, v, name, index);
        } else {
            fprintf(out, "memcpy(&c->activation->values[%d],&%c%d,sizeof(%c%d));\n", v, name, index, name, index);
        }
    }
    for (int frame = f->frames[f->instructions[s->pc].frame].source.parent; frame >= 0;
         frame = f->frames[frame].source.parent) {
        if (restore) {
            fprintf(out, "s%d=c->activation->values[%d];\n", frame, base + frame);
        } else {
            fprintf(out, "c->activation->values[%d]=s%d;\n", base + frame, frame);
        }
    }
}
static void body(const behavior_function *f, FILE *out, char prefix, bool inspected)
{
    behavior_function runtime;
    if (!inspected && f->runtime_body) {
        runtime = *f;
        runtime.instructions = f->runtime_body;
        f = &runtime;
    }
    for (int n = 0; n < acount(f->instructions) + acount(f->locals); n++) {
        if ((n < acount(f->instructions) && f->instructions[n].op == BF_TYPE_NAME) ||
            (n >= acount(f->instructions) &&
             f->locals[n - acount(f->instructions)].offset == SIZE_MAX - 1)) {
            continue;
        }
        pin_value value = n < acount(f->instructions)
                            ? f->instructions[n].value
                            : f->locals[n - acount(f->instructions)].initial;
        if (value.type != PIN_STR || !value.text || text_index(f, value.text) != n) {
            continue;
        }
        pin_string text = pin_text(value);
        fprintf(out, "static const pin_string text%d={(const char*)\"", n);
        for (uint32_t byte = 0; byte < text.length; byte++) {
            fprintf(out, "\\%03o", (unsigned char)text.bytes[byte]);
        }
        fprintf(out, "\",%u};\n", text.length);
    }
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
    if (acount(f->suspensions)) {
        fputs("switch(c->activation->pc){\n", out);
        for (int j = 0; j < acount(f->suspensions); j++) {
            const behavior_function_suspension *s = &f->suspensions[j];
            fprintf(out, "case %d:steps=c->activation->steps;c->activation->pc=0;\n", s->pc + 1);
            activation_values(out, f, s, true);
            fprintf(out, "goto %c%d;\n", prefix, s->pc + 1);
        }
        fputs("default:break;}\n", out);
    }
    enter_locals(out, f, 0);
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        fprintf(out, "%c%d: ;\n", prefix, pc);
        if (!inspected && !behavior_function_runtime_keeps(f, pc)) {
            continue;
        }
        if (i->indirect || i->op == BF_PRESENT) {
            memory(out, f, i);
            continue;
        }
        switch (i->op) {
            case BF_WATCH_BEGIN:
                fprintf(out, "if(c->watch)c->watched=c->watches[%d].initial;\n", i->target);
                break;
            case BF_WATCH_VALUE:
                fprintf(
                    out,
                    "if(c->watch)memcpy((char*)%s+%zu,&v%d,sizeof(v%d));\n",
                    i->right ? "c->watch_storage" : "&c->watched",
                    i->offset,
                    i->left,
                    i->left
                );
                break;
            case BF_WATCH_PRESENT:
                fprintf(out, "if(c->watch)c->watched.ptr=v%d?c->watch_storage:NULL;\n", i->left);
                break;
            case BF_WATCH_RESET:
                fprintf(out, "if(c->watch)c->watch(c->inspection_user,c->watches+%d,NULL);\n", i->target);
                break;
            case BF_WATCH_END:
                fprintf(out, "if(c->watch && v%d)c->watch(c->inspection_user,c->watches+%d,&c->watched);\n", i->left, i->target);
                break;
            case BF_TRACE:
                fprintf(out, "if(c->trace)c->trace(c->inspection_user,c->edges+%d,steps);\n", i->target);
                break;
            case BF_SOURCE_WATCH:
                fprintf(
                    out,
                    "c->watched.%s=v%d;behavior_source_capture(c,%d);\n",
                    field(i->type),
                    i->left,
                    i->target
                );
                break;
            case BF_SOURCE_POINT:
            case BF_POINT:
            case BF_DELAY:
            case BF_INVOKE:
            case BF_SUSPEND: {
                if (i->op == BF_POINT) {
                    fprintf(out, "if(c->inspect && c->inspect(c->inspection_user,c->points+%d)){\n", i->left);
                }
                if (i->op == BF_SOURCE_POINT) {
                    fprintf(out, "if(behavior_source_pause(c,%d)){\n", i->left);
                }
                if (i->op == BF_DELAY) {
                    fprintf(
                        out,
                        "c->wake_time=c->now+v%d;if(!(c->wake_time<=c->now)){c-"
                        ">activation->wake_times[%d]=c->wake_time;\n",
                        i->left,
                        i->frame
                    );
                }
                for (int j = 0; j < acount(f->suspensions); j++) {
                    if (f->suspensions[j].pc == pc) {
                        activation_values(out, f, &f->suspensions[j], false);
                    }
                }
                fprintf(
                    out,
                    "c->activation->pc=%d;c->activation->steps=steps;c->"
                    "activation->frame=%d;"
                    "c->activation->source=%d;c->activation->stack=%d;c->"
                    "activation->node_id=%lluULL;"
                    "c->steps=%s;return %d;\n",
                    pc + 1,
                    i->frame,
                    i->source,
                    i->stack,
                    (unsigned long long)i->node_id,
                    i->frame ? "s0" : "steps",
                    (i->op == BF_POINT || i->op == BF_SOURCE_POINT) ? FLOW_PAUSED_RESULT
                    : i->op == BF_INVOKE ? BEHAVIOR_FUNCTION_CALL_RESULT
                                         : FLOW_SUSPENDED_RESULT
                );
                if (i->op == BF_DELAY || i->op == BF_POINT || i->op == BF_SOURCE_POINT) {
                    fputs("}\n", out);
                }
                break;
            }
            case BF_RETRIGGER:
                fprintf(out, "v%d=c->retrigger(c,%d,%d,v%d)!=0;\n", i->output, i->frame, i->target, i->left);
                break;
            case BF_STRING_EQUAL:
                fprintf(
                    out,
                    "v%d=%s(v%d,v%d);\n",
                    i->output,
                    f->slots[i->left] == PIN_STR ? "behavior_text_equal"
                                                 : "behavior_string_equal",
                    i->left,
                    i->right
                );
                break;
            case BF_INDEX:
                fprintf(
                    out,
                    "v%d=v%d+(uintptr_t)(intptr_t)v%d*%zu;\n",
                    i->output,
                    i->left,
                    i->right,
                    i->offset
                );
                break;
            case BF_COPY_STRING:
                fprintf(out, "{%s s=v%d;if(!s)s=", behavior_scalar_c_type(i->type), i->left);
                constant(out, f, i->type, i->value);
                fprintf(
                    out,
                    ";v%d=s?c->%s(c->value_storage,s):0;}\n",
                    i->output,
                    i->type == PIN_STR ? "copy_text" : "copy_string"
                );
                break;
            case BF_COPY_ARRAY:
                fprintf(
                    out,
                    "v%d=(uintptr_t)c->copy_array(c->value_storage,(const "
                    "pin_array*)(uintptr_t)v%d);\n",
                    i->output,
                    i->left
                );
                break;
            case BF_STATE_SELECT: {
                const behavior_state_selection *s = &f->state_selections[i->target];
                fprintf(
                    out,
                    "v%d=c->select_state(c->value_owner,&(const "
                    "behavior_state_selection){%d,%d},v%d,v%d);\n",
                    i->output,
                    s->index,
                    s->outputs,
                    i->left,
                    i->right
                );
                break;
            }
            case BF_INTERN_STRING:
                if (i->type == PIN_STR) {
                    fprintf(
                        out,
                        "v%d=behavior_function_intern_text(c,(const "
                        "ecs_execution_view*)(uintptr_t)v%d,v%d);if(!v%d){",
                        i->output,
                        i->right,
                        i->left,
                        i->output
                    );
                    failure(out, i, BEHAVIOR_FUNCTION_HOST_DATA);
                    fputs("}\n", out);
                } else {
                    fprintf(
                        out,
                        "{const ecs_execution_view *w=(const "
                        "ecs_execution_view*)(uintptr_t)v%d;v%d=w->intern_"
                        "string(w,v%d);}\n",
                        i->right,
                        i->output,
                        i->left
                    );
                }
                break;
            case BF_STORE_VALUE:
                if (i->offset == BF_STORE_VALUE_SET_ADD) {
                    fprintf(out, "c->add_set_value(c->value_owner,%d,&c->data->values[%d]);\n", i->target, i->left);
                } else {
                    fprintf(out, "c->store_value(c->value_owner,%d,&c->data->values[%d]);\n", i->target, i->left);
                }
                break;
            case BF_HOST_SERVICE:
                fprintf(out, "v%d=(uintptr_t)c->data->calls[%d].service;\n", i->output, i->target);
                break;
            case BF_HOST_SLOT:
                fprintf(
                    out,
                    i->target ? "v%d=(uintptr_t)c->data->buffers[%d];\n"
                              : "v%d=(uintptr_t)&c->data->values[%d];\n",
                    i->output,
                    i->left
                );
                break;
            case BF_HOST_CALL: {
                const behavior_function_host_call *spec = &f->host_calls[i->target];
                fprintf(out, "{c->error_message=behavior_function_host_arguments(c,%d);if(c->error_message){", i->target);
                failure(out, i, BEHAVIOR_FUNCTION_DATA);
                fputs("}}\n", out);
                if (spec->call->action) {
                    fprintf(
                        out,
                        "{c->error_message=behavior_function_host_action(c,%d,"
                        "UINT64_C(%llu));if(c->error_message){",
                        i->target,
                        (unsigned long long)i->node_id
                    );
                    failure(out, i, i->left);
                    fputs("}}\n", out);
                    break;
                }
                if (spec->call->context) {
                    fprintf(
                        out,
                        "{c->node_id=UINT64_C(%llu);c->error_message=c->data->"
                        "calls[%d].context(c,c->data->calls[%d].service,c->"
                        "data->values+%d,c->data->values+%d);if(c->error_"
                        "message){",
                        (unsigned long long)i->node_id,
                        i->target,
                        i->target,
                        spec->first,
                        spec->result_first
                    );
                    failure(out, i, i->left);
                    fputs("}}\n", out);
                    break;
                }
                if (spec->call->value) {
                    /* Exported entries keep their self-contained C ABI. The
                     * shared invocation driver borrows verified arguments;
                     * caller snapshots need its callback identity guard too. */
                    if (i->right == BF_HOST_BORROW_ARRAY && !f->calls) {
                        fprintf(
                            out,
                            "c->error_message=NULL;if(c->call_enter||c->call_"
                            "leave||"
                            "c->bind_instance||c->commit_instance||c->failure_"
                            "observer||"
                            "!behavior_borrow_array(c,"
                            "(behavior_borrow_layout){%d,%d,%d,%d,%d,%d},c->"
                            "data->values+%d,c->"
                            "data->values+%d))",
                            f->variable_count,
                            f->result_count,
                            acount(f->data_values),
                            acount(f->slots),
                            acount(f->locals),
                            acount(f->frames),
                            spec->first,
                            spec->result_first
                        );
                    }
                    fprintf(
                        out,
                        "{c->error_message=c->data->calls[%d].value(c->%s,"
                        "c->data->values+%d,%d,c->data->values+%d);if(c->error_"
                        "message){",
                        i->target,
                        execution_intrinsic_in_result(spec->capability_id)
                            ? "result_storage"
                            : "value_storage",
                        spec->first,
                        spec->result_first - spec->first,
                        spec->result_first
                    );
                    failure(out, i, i->left);
                    fputs("}}\n", out);
                    break;
                }
                fprintf(
                    out,
                    "{behavior_data_binding "
                    "*d=&c->data->calls[%d];c->error_message=d->invoke(d->"
                    "service,c->data->values+%d,c->data->values+%d);if(c->"
                    "error_message){",
                    i->target,
                    spec->first,
                    spec->result_first
                );
                failure(out, i, i->left);
                fputs("}}\n", out);
                break;
            }
            case BF_COMMAND_SERVICE:
                fprintf(out, "v%d=(uintptr_t)c->commands[%d].service;\n", i->output, i->target);
                break;
            case BF_COMMAND_RESULT:
                fprintf(
                    out,
                    "memcpy(&v%d,(const "
                    "char*)&c->command_results[%d]+%zu,sizeof(v%d));\n",
                    i->output,
                    i->left,
                    i->offset,
                    i->output
                );
                break;
            case BF_COMMAND_ARGUMENT:
            case BF_COMMAND_OUTPUT:
                fprintf(
                    out,
                    "memcpy((char*)&c->%s[%d]+%zu,&v%d,sizeof(v%"
                    "d));\n",
                    i->op == BF_COMMAND_OUTPUT ? "command_results" : "command_arguments",
                    i->target,
                    i->offset,
                    i->left,
                    i->left
                );
                break;
            case BF_COMMAND_BEGIN:
                fprintf(out, "c->error_status=c->begin_command(c,%d);if(c->error_status<0){", i->target);
                failure(out, i, BEHAVIOR_FUNCTION_COMMAND);
                fputs("}\n", out);
                break;
            case BF_COMMAND: {
                const behavior_function_command *command = &f->commands[i->target];
                fprintf(
                    out,
                    "{behavior_command_binding "
                    "*b=&c->commands[%d];c->error_status=b->invoke(b->service,"
                    "b->reference,c->command_arguments+%d,%d);if(c->error_"
                    "status<0){",
                    i->target,
                    command->first,
                    command->count
                );
                failure(out, i, BEHAVIOR_FUNCTION_COMMAND);
                fputs("}}\n", out);
                break;
            }
            case BF_QUERY:
                fprintf(out, "{behavior_query_binding *q=&c->queries[%d];c->error_message=q->read(q->service,q->key,&q->snapshot);if(c->error_message){", i->target);
                failure(out, i, BEHAVIOR_FUNCTION_HOST_DATA);
                fprintf(out, "}v%d=(uintptr_t)&q->snapshot;}\n", i->output);
                break;
            case BF_ARRAY:
                fprintf(out, "{const pin_value *fallback=");
                if (i->offset) {
                    fprintf(out, "&c->data->values[%zu];", i->offset - 1);
                } else {
                    fprintf(out, "0;");
                }
                fprintf(out, "const pin_value *a=");
                if (i->target == BF_ARGUMENT) {
                    fprintf(
                        out,
                        "c->argument_count>%d&&c->arguments[%d].type==%d?&c->"
                        "arguments[%d]:fallback;",
                        i->left,
                        i->left,
                        i->value.type,
                        i->left
                    );
                } else if (i->target == BF_VARIABLE) {
                    fprintf(out, "&c->variables[%d];", i->left);
                } else {
                    fprintf(out, "fallback;");
                }
                fprintf(
                    out,
                    "v%d=a&&a->container==%d?(uintptr_t)a->ptr:0;}\n",
                    i->output,
                    i->value.container
                );
                break;
            case BF_STORAGE_NAME:
                fprintf(out, "v%d=(uintptr_t)c->variable_storage[%d].type_name;\n", i->output, i->left);
                break;
            case BF_OWN_NAME:
                fprintf(
                    out,
                    "v%d=v%d?(uintptr_t)c->own_type_name(c->value_owner,(const "
                    "char*)(uintptr_t)v%d):0;\n",
                    i->output,
                    i->left,
                    i->left
                );
                break;
            case BF_STORE_TYPE_NAME:
                fprintf(out, "c->variables[%d].record=NULL;\n", i->target);
                fprintf(
                    out,
                    "c->variables[%d].type_name=(const char*)(uintptr_t)v%d;\n",
                    i->target,
                    i->left
                );
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
            case BF_CONTEXT:
                if (i->target) {
                    fprintf(out, "v%d=(uintptr_t)c;\n", i->output);
                    break;
                }
                fprintf(
                    out,
                    "memcpy(&v%d,(const char*)c+%zu,sizeof(v%d));\n",
                    i->output,
                    i->offset,
                    i->output
                );
                break;
            case BF_OWNER: fprintf(out, "v%d=c->owner;\n", i->output); break;
            case BF_QUERY_VARIABLE:
                fputs("if(!c->query_variables){", out);
                failure(out, i, BEHAVIOR_FUNCTION_HOST_DATA);
                fprintf(out, "}v%d=c->query_variables[%d];c->error_message=0;\n", i->output, i->target - 1);
                break;
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
            case BF_ROW:
                fprintf(
                    out,
                    "{const ecs_execution_column "
                    "*p=ecs_execution_present((const ecs_execution_column "
                    "*)(uintptr_t)v%d,v%d);v%d=p?(uintptr_t)p->data+(size_t)v%"
                    "d*%zu:0;}\n",
                    i->left,
                    i->right,
                    i->output,
                    i->right,
                    i->offset
                );
                break;
            case BF_COMPONENT_WRITE:
                fprintf(
                    out,
                    "{const behavior_function_instruction write={.type=%d,"
                    ".value={.type=PIN_U64,.u64=UINT64_C(%llu)},"
                    ".layout=UINT64_C(%llu),.offset=%zu,.target=%d};"
                    "c->error_message=behavior_component_write(c,&write,&v%d,&"
                    "v%d);"
                    "if(c->error_message){",
                    i->type,
                    (unsigned long long)i->value.u64,
                    (unsigned long long)i->layout,
                    i->offset,
                    i->target,
                    i->left,
                    i->right
                );
                failure(out, i, BEHAVIOR_FUNCTION_HOST_DATA);
                fputs("}}\n", out);
                break;
            case BF_COMPONENT_READ:
                fprintf(
                    out,
                    "{uint32_t "
                    "row=c->world?ecs_execution_entity(c->world,v%d):0;"
                    "const ecs_execution_column "
                    "*column=ecs_execution_component(c->world,row,UINT64_C(%"
                    "llu));"
                    "if(!column||column->layout!=UINT64_C(%llu)||column->"
                    "stride!=%zu){",
                    i->left,
                    (unsigned long long)i->value.u64,
                    (unsigned long long)i->layout,
                    i->offset
                );
                failure(out, i, BEHAVIOR_FUNCTION_HOST_DATA);
                fprintf(
                    out,
                    "}memcpy(&v%d,(const "
                    "char*)column->data+(size_t)row*column->stride+%d,sizeof(v%"
                    "d));"
                    "c->error_message=NULL;}\n",
                    i->output,
                    i->target,
                    i->output
                );
                break;
            case BF_COLUMN:
            case BF_COLUMN_TRY:
            case BF_COMPONENT:
                fprintf(
                    out,
                    "{const ecs_execution_column "
                    "*column=c->columns?c->columns[%d]:ecs_execution_find_"
                    "column((const "
                    "ecs_execution_view*)(uintptr_t)v%d,UINT64_C(%llu));\n",
                    i->component,
                    i->left,
                    (unsigned long long)i->value.u64
                );
                if (i->op == BF_COMPONENT) {
                    fprintf(out, "column=ecs_execution_present(column,v%d);\n", i->right);
                }
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
                if (i->op == BF_COLUMN_TRY) {
                    fprintf(out, "column=NULL;");
                } else {
                    failure(out, i, i->target);
                }
                if (i->op != BF_COMPONENT) {
                    fprintf(out, "}v%d=(uintptr_t)column;}\n", i->output);
                } else {
                    fprintf(out, "}v%d=column?(uintptr_t)column->data+v%d*column->stride:0;}\n", i->output, i->right);
                }
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
                        i->value.type,
                        i->output,
                        i->left
                    );
                } else if (i->target == BF_VARIABLE) {
                    fprintf(out, "v%d=(uintptr_t)c->variables[%d].type_name;\n", i->output, i->left);
                } else if (i->target == BF_OWNER) {
                    fprintf(out, "v%d=(uintptr_t)c->owner_type_name;\n", i->output);
                }
                break;
            case BF_RESULT_TYPE_NAME:
                fprintf(out, "c->results[%d].record=NULL;\n", i->target);
                fprintf(out, "c->results[%d].type_name=(const char*)(uintptr_t)v%d;\n", i->target, i->left);
                break;
            case BF_RESULT_ARRAY:
                fprintf(
                    out,
                    "c->results[%d]=(pin_value){.type=%d,.container=%d,.ptr=("
                    "void*)(uintptr_t)v%d,.type_name=(const "
                    "char*)(uintptr_t)v%d};\n",
                    i->target,
                    i->type,
                    i->value.container,
                    i->left,
                    i->right
                );
                break;
            case BF_SELECT:
                fprintf(out, "v%d=v%d?v%d:v%d;\n", i->output, i->left, i->right, i->target);
                break;
            case BF_FMA:
                fprintf(out, "v%d=fma(v%d,v%d,v%d);\n", i->output, i->left, i->right, i->target);
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
                MATH_BINARY(MOD_F32, Modulo, fmodf)
                MATH_BINARY(MOD_F64, Modulo, fmod)
                MATH_BINARY(HYPOT, Length, hypot)
                MATH_BINARY(RANDOM_RANGE, RandomRange, behavior_random_range)
#undef MATH_BINARY
            case BF_RANDOM: fprintf(out, "v%d=rand();\n", i->output); break;
            case BF_CONSTANT: {
                fprintf(out, "v%d=", i->output);
                constant(out, f, i->type, i->value);
                fprintf(out, ";\n");
                break;
            }
            case BF_VARIABLE:
                fprintf(out, "v%d=c->variables[%d].%s;\n", i->output, i->left, field(i->type));
                break;
            case BF_LOCAL:
                fprintf(out, "v%d=l%d;\n", i->output, i->left);
                break;
            case BF_SAVE:
                fprintf(out, "l%d=v%d;\n", i->target, i->left);
                if (behavior_function_inspectable(f) && f->locals[i->target].parameter) {
                    fprintf(
                        out,
                        "memcpy(c->activation->values+%d,&v%d,sizeof(v%d));\n",
                        acount(f->slots) + i->target,
                        i->left,
                        i->left
                    );
                }
                break;
            case BF_ENTER:
                if (behavior_function_inspectable(f)) {
                    fprintf(
                        out,
                        "c->activation->values[%d]=steps;\n",
                        acount(f->slots) + acount(f->locals) + i->frame
                    );
                }
                fprintf(out, "s%d=steps;steps=0;\n", i->frame);
                enter_locals(out, f, i->target);
                break;
            case BF_LEAVE:
                if (behavior_function_inspectable(f)) {
                    fprintf(
                        out,
                        "c->activation->values[%d]=steps;\n",
                        acount(f->slots) + acount(f->locals) + i->target
                    );
                }
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
                constant(out, f, i->type, i->value);
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
            case BF_CAST: cast(out, f, i); break;
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
                    "c->returned=1;c->returned_node_id=%lluULL;c->steps=steps+%"
                    "du;return "
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
            case BF_BIT_AND:
            case BF_BIT_OR:
            case BF_BIT_XOR:
            case BF_BIT_NOT:
            case BF_SHL:
            case BF_SHR:
                fprintf(
                    out,
                    "v%d=behavior_scalar_from_bits(%d,behavior_scalar_bitwise_"
                    "bits(%d,%d,(uint64_t)v%d,",
                    i->output,
                    i->type,
                    i->op,
                    i->type,
                    i->left
                );
                if (i->op == BF_BIT_NOT) {
                    fputs("0", out);
                } else {
                    fprintf(out, "(uint64_t)v%d", i->right);
                }
                fprintf(out, ")).%s;\n", behavior_scalar_field(i->type));
                break;
            case BF_ABS:
                if (behavior_scalar_integer(i->type)) {
                    if (!behavior_scalar_signed(i->type)) {
                        fprintf(out, "v%d=v%d;\n", i->output, i->left);
                        break;
                    }
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
                if ((i->op == BF_EQ || i->op == BF_NE) &&
                    f->slots[i->left] == PIN_POINTER) {
                    fprintf(
                        out,
                        "v%d=%spin_pointer_equal(v%d,v%d);\n",
                        i->output,
                        i->op == BF_NE ? "!" : "",
                        i->left,
                        i->right
                    );
                    break;
                }
                if ((i->op == BF_DIV || i->op == BF_MOD) &&
                    behavior_scalar_integer(i->type)) {
                    integer_division(out, i);
                    break;
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
static void write_definition(const behavior_function *f, FILE *out, const char *name, int index, bool local)
{
    fprintf(
        out,
        "#include \"execution/scalar.h\"\n#include "
        "\"execution/function.h\"\n#include \"types/pointer.h\"\n#include "
        "\"execution/random.h\"\n#include \"execution/borrow.h\"\n#include "
        "\"execution/component_write.h\"\n#include "
        "<math.h>\n#include "
        "<string.h>\n#include \"support/cos_sin.h\"\n#include "
        "\"ecs/execution.h\"\n%sint %s",
        local ? "static inline " : "",
        name
    );
    if (index) {
        fprintf(out, "_callee_%d", index);
    }
    fputs("(behavior_function_context *c) {\n", out);
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
        body(f->fast, out, 'F', false);
        fprintf(out, "}\n");
    }
    if (f->runtime_map) {
        fprintf(out, "if(!c->inspect && !c->watch && !c->trace && !c->source_pause && !c->source_capture");
        /* Keep detached debug continuations canonical, as the runtime's
         * behavior_function_inspection_required selection does. */
        for (int i = 0; i < acount(f->suspensions); i++) {
            int pc = f->suspensions[i].pc;
            behavior_function_op op = f->instructions[pc].op;
            if (op == BF_POINT || op == BF_SOURCE_POINT) {
                fprintf(out, " && (!c->activation || c->activation->pc!=%d)", pc + 1);
            }
        }
        fprintf(out, "){\n");
        body(f, out, 'N', false);
        fprintf(out, "}\n");
    }
    body(f, out, 'L', true);
    fprintf(out, "}\n");
}

void behavior_function_write_c(const behavior_function *f, FILE *out, const char *name)
{
    write_definition(f, out, name, 0, false);
    for (int i = 0; i < acount(f->definitions); i++) {
        write_definition(f->definitions[i], out, name, i + 1, false);
    }
}

void behavior_function_write_sequence_c(
    const behavior_function *const *functions,
    int count,
    FILE *out,
    const char *name
)
{
    assert(count > 0);
    for (int part = 0; part < count; part++) {
        const behavior_function *f = functions[part];
        assert(f->batchable);
        char body_name[256];
        int length = snprintf(body_name, sizeof body_name, "%s_body%d", name, part);
        assert(length > 0 && length < (int)sizeof body_name);
        (void)length;
        write_definition(f, out, body_name, 0, true);
        for (int i = 0; i < acount(f->definitions); i++) {
            write_definition(f->definitions[i], out, body_name, i + 1, true);
        }
    }
    fprintf(
        out,
        "#include \"execution/update.h\"\n"
        "execution_update_row *%s(execution_update_row *row, "
        "execution_update_row "
        "*end, "
        "const ecs_execution_view *world, int *result) {\n*result=0;\n",
        name
    );
    for (int part = 0; part < count; part++) {
        fprintf(
            out,
            "const behavior_function *function%d=(end-row>%d && "
            "row[%d].binding)?row[%d].binding->function:NULL;\n",
            part,
            part,
            part,
            part
        );
    }
    fputs("for(;;){\n", out);
    for (int part = 0; part < count; part++) {
        fprintf(
            out,
            "{if(row==end) return row;\n"
            "execution_update_state *bi=row->state; execution_update_binding "
            "*binding=row->binding;\n"
            "if(!binding || binding->function!=function%d || !bi->alive || "
            "bi->trace_enabled || bi->pending || "
            "bi->backend!=BEHAVIOR_SLJIT || bi->profile || "
            "bi->generation!=binding->generation || "
            "bi->services_revision!=row->services_revision || "
            "!ecs_execution_entity(world,row->entity)) return row;\n"
            "behavior_function_context *c=&binding->context;\n"
            "c->owner=bi->owner.u64; c->owner_type_name=bi->owner.type_name; "
            "c->delta_seconds=bi->clock->delta_seconds; c->steps=0;\n"
            "c->variables=bi->variables; c->states=bi->node_states; "
            "c->variable_storage=bi->variable_storage; "
            "c->value_owner=bi->value_owner; "
            "c->instance_time=&bi->clock->current_time;\n"
            "if(c->value_storage) arena_reset(c->value_storage);\n"
            "*result=%s_body%d(c); if(*result) return row; row++;}\n",
            part,
            name,
            part
        );
    }
    fputs("}}\n", out);
}
void behavior_function_write_system_c(const behavior_function *f, FILE *out, const char *name)
{
    behavior_function_write_sequence_c(&f, 1, out, name);
}
