#include "reflect_internal.h"

#include <string.h>

const char *pin_member(const char *pin)
{
    if (!strcmp(pin, "PIN_BOOL")) {
        return "b";
    }
    if (!strcmp(pin, "PIN_I8")) {
        return "i8";
    }
    if (!strcmp(pin, "PIN_U8")) {
        return "u8";
    }
    if (!strcmp(pin, "PIN_I16")) {
        return "i16";
    }
    if (!strcmp(pin, "PIN_U16")) {
        return "u16";
    }
    if (!strcmp(pin, "PIN_I32")) {
        return "i";
    }
    if (!strcmp(pin, "PIN_U32")) {
        return "u";
    }
    if (!strcmp(pin, "PIN_I64")) {
        return "i64";
    }
    if (!strcmp(pin, "PIN_U64") || !strcmp(pin, "PIN_HANDLE")) {
        return "u64";
    }
    if (!strcmp(pin, "PIN_F32")) {
        return "f";
    }
    if (!strcmp(pin, "PIN_F64")) {
        return "d";
    }
    if (!strcmp(pin, "PIN_STR")) {
        return "str";
    }
    if (!strcmp(pin, "PIN_ENUM")) {
        return "e";
    }
    return "ptr"; /* PIN_STRUCT / PIN_FLOW */
}

static void emit_arg(FILE *out, const char *spelling, const char *pin, int idx)
{
    if (!strcmp(pin, "PIN_STR")) {
        fprintf(out, "__text%d", idx);
    } else if (!strcmp(pin, "PIN_STRUCT")) {
        fprintf(out, "*(%s *)args[%d].ptr", spelling, idx);
    } else {
        fprintf(out, "args[%d].%s", idx, pin_member(pin));
    }
}

static const char *param_label(const fn_info *f, int index)
{
    return f->plabel[index] ? f->plabel[index] : f->pname[index];
}

void emit_execution_result(FILE *out, const char *pin, const char *expr, const char *type, int index)
{
    if (!strcmp(pin, "PIN_STR")) {
        fprintf(
            out,
            "    { const char *__text = %s; pin_value __owned = { 0 }; const "
            "char *__text_error = "
            "pin_value_text(ctx->value_storage, "
            "&__owned, __text, __text ? strlen(__text) : 0); if (__text_error) "
            "return __text_error; out[%d].text = __owned.text; }\n",
            expr,
            index
        );
        return;
    }
    if (strcmp(pin, "PIN_STRUCT") && strcmp(pin, "PIN_STR") && strcmp(pin, "PIN_NAME")) {
        fprintf(out, "    out[%d].%s = %s;\n", index, pin_member(pin), expr);
        return;
    }
    fprintf(out, "    pin_value __value%d = out[%d], __owned%d;\n", index, index, index);
    if (!strcmp(pin, "PIN_STRUCT")) {
        fprintf(out, "    __value%d.ptr = &%s;\n", index, expr);
    } else {
        fprintf(out, "    __value%d.str = %s;\n", index, expr);
    }
    fprintf(
        out,
        "    if (pin_value_snapshot_to_arena(ctx->value_storage, &__owned%d, "
        "&__value%d)) return \"native result cannot be retained\";\n",
        index,
        index
    );
    if (!strcmp(pin, "PIN_STRUCT")) {
        fprintf(out, "    if (!out[%d].ptr) return \"native result backing is unavailable\";\n", index);
        fprintf(out, "    memcpy(out[%d].ptr, __owned%d.ptr, sizeof(%s));\n", index, index, type);
        return;
    }
    fprintf(out, "    out[%d].str = __owned%d.str;\n", index, index);
}

/* Direct native calls bind the declared service inputs from the execution
 * context. Output headers/backing stay owned by the caller; managed payloads
 * are snapshotted before the native function's locals leave this scope. */

void emit_execution_host(FILE *out, const fn_info *f, bool external)
{
    fprintf(
        out,
        "static const char *__execution_%s%s(const behavior_function_context "
        "*ctx, void *service, const pin_value *args, pin_value *out) {\n    "
        "(void)ctx; (void)service; (void)args; (void)out;\n",
        symbol_prefix(),
        f->name
    );
    int input = 0;
    const char *service_name = NULL;
    for (int j = 0; j < acount(f->pname); j++) {
        if (f->pout[j]) {
            fprintf(out, "    %s __out%d = { 0 };\n", f->pout_type[j], j);
            continue;
        }
        if (f->pservice[j]) {
            if (!service_name) {
                service_name = f->pservice_type[j];
            }
            fprintf(out, "    void *__service%d = (void *)(uintptr_t)args[%d].u64;\n", j, input);
            fprintf(out, "    for (int k = 0; !__service%d && k < acount(ctx->services); k++) {\n", j);
            fprintf(
                out,
                "        if (!strcmp(ctx->services[k].type_name, \"%s\")) "
                "__service%d = ctx->services[k].value;\n    }\n",
                f->pservice_type[j],
                j
            );
            fprintf(out, "    if (!__service%d) return \"missing service %s\";\n", j, f->pservice_type[j]);
        } else if (!strcmp(f->ppin[j], "PIN_STR")) {
            fprintf(out, "    const char *__text%d = NULL; size_t __nul%d = 0;\n", input, input);
            fprintf(
                out,
                "    { const char *__error = "
                "pin_text_cstring(ctx->value_storage, args[%d], &__text%d, "
                "&__nul%d); "
                "if (__error) return __error; }\n",
                input,
                input,
                input
            );
        } else if (!strcmp(f->ppin[j], "PIN_STRUCT")) {
            fprintf(
                out,
                "    if (!args[%d].ptr) return \"native call requires %s\";\n",
                input,
                param_label(f, j)
            );
        }
        input++;
    }
    if (f->read) {
        fputs("    const char *__error = ", out);
    } else if (!f->ret_void) {
        fprintf(out, "    %s __ret = ", f->ret);
    } else {
        fputs("    ", out);
    }
    fprintf(out, "%s(", f->name);
    input = 0;
    for (int j = 0; j < acount(f->pname); j++) {
        if (j) {
            fputs(", ", out);
        }
        if (f->pout[j]) {
            fprintf(out, "&__out%d", j);
        } else if (f->pservice[j]) {
            fprintf(out, "__service%d", j);
            input++;
        } else {
            emit_arg(out, f->ptype[j], f->ppin[j], input++);
        }
    }
    fputs(");\n", out);
    int output = 0;
    if (f->read) {
        fputs("    if (__error) return __error;\n", out);
    } else if (!f->ret_void) {
        emit_execution_result(out, f->ret_pin, "__ret", f->ret, output++);
    }
    for (int j = 0; j < acount(f->pname); j++) {
        if (!f->pout[j]) {
            continue;
        }
        char expr[32];
        snprintf(expr, sizeof expr, "__out%d", j);
        emit_execution_result(out, f->ppin[j], expr, f->pout_type[j], output++);
    }
    fprintf(
        out,
        "    return NULL;\n}\n%sconst node_host_call %s%s_execution = { "
        ".context = __execution_%s%s",
        external ? "" : "static ",
        symbol_prefix(),
        f->name,
        symbol_prefix(),
        f->name
    );
    if (service_name) {
        fprintf(out, ", .service = \"%s\", .optional_service = true", service_name);
    }
    fputs(" };\n\n", out);
}
