#include "reflect_internal.h"

#include <stdlib.h>
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

/* Records, enums and handles keep their nominal type names. */
static const char *primitive_spelling(const char *pin)
{
    if (!strcmp(pin, "PIN_STRUCT") || !strcmp(pin, "PIN_ENUM") ||
        !strcmp(pin, "PIN_HANDLE")) {
        return NULL;
    }
    static const struct
    {
        const char *name;
        const char *spelling;
    } table[] = {
#define X(id, str) { #id, str },
        PIN_TYPE_LIST(X)
#undef X
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) {
        if (strcmp(table[i].name, pin) == 0) {
            return table[i].spelling;
        }
    }
    return NULL;
}

static void emit_pin_spec_flags(FILE *out, const char *name, const char *pin, const char *spelling, unsigned flags)
{
    const char *canon = primitive_spelling(pin);
    if (canon && !(flags & PIN_FLAG_SERVICE)) {
        spelling = canon;
    }
    if (flags) {
        fprintf(
            out,
            "    { .name = \"%s\", .type = %s, .type_name = \"%s\", .kind = "
            "PIN_DATA, .flags = %u },\n",
            name,
            pin,
            spelling ? spelling : "",
            flags
        );
    } else {
        fprintf(out, "    { \"%s\", %s, \"%s\" },\n", name, pin, spelling ? spelling : "");
    }
}

void emit_pin_spec(FILE *out, const char *name, const char *pin, const char *spelling)
{
    emit_pin_spec_flags(out, name, pin, spelling, 0);
}

/* Emit a C expression that reads the value for param idx out of pin_value. */
static void emit_arg(FILE *out, const char *spelling, const char *pin, int idx)
{
    if (!strcmp(pin, "PIN_STRUCT")) {
        fprintf(out, "*(%s *)args[%d].ptr", spelling, idx);
    } else {
        fprintf(out, "args[%d].%s", idx, pin_member(pin));
    }
}

static void emit_native_arg(FILE *out, const fn_info *f, int index)
{
    if (f->pout[index]) {
        fprintf(out, "&__out%d", index);
        return;
    }
    if (f->pservice[index]) {
        fputs("NULL", out); /* service binding is supplied by the host bridge */
        return;
    }
    emit_arg(out, f->ptype[index], f->ppin[index], index);
}

static int fn_fast_supported(const fn_info *f)
{
    if (f->ret_void) {
        return 0;
    }
    for (int i = 0; i < acount(f->pname); i++) {
        if (f->pout[i] || f->pservice[i] || !f->ppin[i]) {
            return 0;
        }
    }
    return 1;
}

static int fn_direct_supported(const fn_info *f)
{
    return fn_fast_supported(f) && acount(f->pname) <= 4;
}

static void emit_fast_arg(FILE *out, const fn_info *f, int index)
{
    fprintf(out, "*(%s *)inputs[%d]", f->ptype[index], index);
}

static void emit_direct_arg(FILE *out, const fn_info *f, int index)
{
    fprintf(out, "*(%s *)input%d", f->ptype[index], index);
}

static void emit_fast_direct_fn(FILE *out, const fn_info *f)
{
    if (!fn_direct_supported(f)) {
        return;
    }
    fprintf(out, "static int __fast_direct_%s_%s(void *out", g_prefix, f->name);
    for (int i = 0; i < acount(f->pname); i++) {
        fprintf(out, ", const void *input%d", i);
    }
    fprintf(out, ") {\n    *(%s *)out = %s(", f->ret, f->name);
    for (int i = 0; i < acount(f->pname); i++) {
        if (i) {
            fputs(", ", out);
        }
        emit_direct_arg(out, f, i);
    }
    fputs(");\n    return 0;\n}\n\n", out);
}

/* Emit a typed adapter for the system bytecode backend. */
static void emit_fast_fn(FILE *out, const fn_info *f)
{
    if (!fn_fast_supported(f)) {
        return;
    }
    fprintf(out, "static int __fast_%s_%s(void *out, const void *const *inputs) {\n", g_prefix, f->name);
    fprintf(out, "    *(%s *)out = %s(", f->ret, f->name);
    for (int i = 0; i < acount(f->pname); i++) {
        if (i) {
            fputs(", ", out);
        }
        emit_fast_arg(out, f, i);
    }
    fputs(");\n    return 0;\n}\n\n", out);
}

/* Emit the uniform-signature wrapper that adapts a pure native fn to node_fn. */
void emit_fn(FILE *out, const fn_info *f)
{
    emit_fast_direct_fn(out, f);
    emit_fast_fn(out, f);
    fprintf(out, "static pin_value __wrap_%s_%s(const pin_value *args, int nargs, arena *a) {\n", g_prefix, f->name);
    fprintf(out, "    (void)nargs;\n");
    int struct_ret = f->ret_pin && !strcmp(f->ret_pin, "PIN_STRUCT");
    if (!struct_ret) {
        fprintf(out, "    (void)a;\n");
    }
    for (int j = 0; j < acount(f->pname); j++) {
        if (f->pout[j]) {
            fprintf(out, "    %s __out%d = { 0 };\n", f->pout_type[j], j);
        }
    }
    fprintf(out, "    pin_value r = { 0 };\n");
    fprintf(out, "    r.type = %s;\n", f->ret_void ? "PIN_FLOW" : f->ret_pin);
    if (!f->ret_void) {
        fprintf(out, "    r.type_name = \"%s\";\n", f->ret);
    }
    if (f->jit_kind && !strcmp(f->jit_kind, "identity")) {
        fputs("    if (nargs != 1 || !args[0].ptr) return r;\n", out);
    }

    if (f->ret_void) {
        fprintf(out, "    %s(", f->name);
        for (int j = 0; j < acount(f->pname); j++) {
            if (j) {
                fputs(", ", out);
            }
            emit_native_arg(out, f, j);
        }
        fputs(");\n", out);
    } else if (struct_ret) {
        fprintf(out, "    %s __r = %s(", f->ret, f->name);
        for (int j = 0; j < acount(f->pname); j++) {
            if (j) {
                fputs(", ", out);
            }
            emit_native_arg(out, f, j);
        }
        fputs(");\n", out);
        fprintf(out, "    r.ptr = arena_alloc(a, sizeof(%s));\n", f->ret);
        fputs("    if (!r.ptr) return r;\n", out);
        fprintf(out, "    *(%s *)r.ptr = __r;\n", f->ret);
    } else {
        fprintf(out, "    r.%s = %s(", pin_member(f->ret_pin), f->name);
        for (int j = 0; j < acount(f->pname); j++) {
            if (j) {
                fputs(", ", out);
            }
            emit_native_arg(out, f, j);
        }
        fputs(");\n", out);
    }
    fputs("    return r;\n}\n\n", out);
}

static const char *param_label(const fn_info *f, int index)
{
    return f->plabel[index] ? f->plabel[index] : f->pname[index];
}

static void emit_read_value_assignment(
    FILE *out,
    const char *target,
    const char *pin,
    const char *type_name,
    const char *member,
    const char *expr
)
{
    fprintf(
        out,
        "    %s = (pin_value){ .type = %s, .type_name = \"%s\" };\n",
        target,
        pin,
        type_name ? type_name : ""
    );
    if (!strcmp(pin, "PIN_STRUCT")) {
        fprintf(out, "    %s.ptr = arena_alloc(a, sizeof(%s));\n", target, type_name);
        fprintf(out, "    if (!%s.ptr) return \"read node output allocation failed\";\n", target);
        fprintf(out, "    *(%s *)%s.ptr = %s;\n", type_name, target, expr);
    } else {
        fprintf(out, "    %s.%s = %s;\n", target, member, expr);
    }
}

/* Both adapters invoke the declared C function. Input pin indices are compact
 * even when NODE_OUT parameters appear between inputs in the C signature. */
static void emit_read_call(FILE *out, const fn_info *f)
{
    int input = 0;
    for (int i = 0; i < acount(f->pname); i++) {
        if (f->pout[i]) {
            fprintf(out, "    %s __out%d = { 0 };\n", f->pout_type[i], i);
            continue;
        }
        if (!strcmp(f->ppin[i], "PIN_STRUCT")) {
            fprintf(
                out,
                "    if (!args[%d].ptr) return \"read node requires %s\";\n",
                input,
                param_label(f, i)
            );
        }
        input++;
    }
    fprintf(out, "    const char *__error = %s(", f->name);
    input = 0;
    for (int i = 0; i < acount(f->pname); i++) {
        if (i) {
            fputs(", ", out);
        }
        if (f->pout[i]) {
            fprintf(out, "&__out%d", i);
        } else {
            emit_arg(out, f->ptype[i], f->ppin[i], input++);
        }
    }
    fputs(");\n    if (__error) return __error;\n", out);
}
static bool read_host_supported(const fn_info *f)
{
    if (f->effect_flags != (1u << 3) || acount(f->reads) || acount(f->writes)) {
        return false;
    }
    for (int i = 0; i < acount(f->pname); i++) {
        if (f->pservice[i] || (f->pout[i] && !strcmp(f->ppin[i], "PIN_STR"))) {
            return false;
        }
    }
    return true;
}
/* Typed native-call adapters use caller-owned result backing. They neither
 * allocate nor recurse into evaluation. The behavior compiler checks reflected
 * struct ownership/layout before accepting the signature. */
static void emit_read_host(FILE *out, const fn_info *f)
{
    if (!read_host_supported(f)) {
        return;
    }
    fprintf(
        out,
        "static const char *__host_%s_%s(void *service, const pin_value *args, "
        "pin_value *out) {\n    (void)service;\n",
        g_prefix,
        f->name
    );
    emit_read_call(out, f);
    int output = 0;
    for (int i = 0; i < acount(f->pname); i++) {
        if (!f->pout[i]) {
            continue;
        }
        if (!strcmp(f->ppin[i], "PIN_STRUCT")) {
            fprintf(out, "    *(%s *)out[%d].ptr = __out%d;\n", f->pout_type[i], output, i);
        } else {
            fprintf(out, "    out[%d].%s = __out%d;\n", output, pin_member(f->ppin[i]), i);
        }
        output++;
    }
    fprintf(
        out,
        "    return NULL;\n}\nstatic const node_host_call %s_%s_host = { "
        ".invoke = __host_%s_%s };\n\n",
        g_prefix,
        f->name,
        g_prefix,
        f->name
    );
}
void emit_read_fn(FILE *out, const fn_info *f)
{
    int has_struct_output = 0;
    for (int i = 0; i < acount(f->pname); i++) {
        has_struct_output |= f->pout[i] && !strcmp(f->ppin[i], "PIN_STRUCT");
    }
    fprintf(
        out,
        "static const char *__read_%s_%s(const eval_context *ctx, const "
        "pin_value *args, arena *a, pin_value *out) {\n    (void)ctx;\n",
        g_prefix,
        f->name
    );
    if (!has_struct_output) {
        fputs("    (void)a;\n", out);
    }
    emit_read_call(out, f);
    int output = 0;
    for (int i = 0; i < acount(f->pname); i++) {
        if (!f->pout[i]) {
            continue;
        }
        char target[32], expr[32];
        snprintf(target, sizeof target, "out[%d]", output++);
        snprintf(expr, sizeof expr, "__out%d", i);
        emit_read_value_assignment(out, target, f->ppin[i], f->pout_type[i], pin_member(f->ppin[i]), expr);
    }
    fputs("    return NULL;\n}\n\n", out);
    emit_read_host(out, f);
}

/* Emit Make + Break node_desc for one struct. */
void emit_struct_nodes(FILE *out, const struct_info *s)
{
    fprintf(out,"static const node_access %s_%s_value_access = {.flags=NODE_EFFECT_PURE};\n",g_prefix,s->name);
    fprintf(out, "static pin_spec %s_%s_make_inputs[] = {\n", g_prefix, s->name);
    for (int j = 0; j < acount(s->fields); j++) {
        emit_pin_spec(out, s->fields[j].name, s->fields[j].pin, field_type_name(&s->fields[j]));
    }
    fputs("};\n", out);
    fprintf(out, "static pin_spec %s_%s_make_outputs[] = {\n", g_prefix, s->name);
    fprintf(out, "    { \"%s\", PIN_STRUCT, \"%s\" },\n", s->name, s->name);
    fputs("};\n", out);
    fprintf(out, "node_desc %s_NODE_Make_%s = {\n", g_prefix, s->name);
    fprintf(out, "    .name = \"Make_%s\",\n", s->name);
    fprintf(out, "    .input_count = %d,\n", acount(s->fields));
    fprintf(out, "    .output_count = 1,\n");
    fprintf(out, "    .inputs = %s_%s_make_inputs,\n", g_prefix, s->name);
    fprintf(out, "    .outputs = %s_%s_make_outputs,\n", g_prefix, s->name);
    fprintf(out, "    .pure_kind = NODE_PURE_MAKE,\n");
    fprintf(out,"    .access = &%s_%s_value_access,\n",g_prefix,s->name);
    fprintf(out, "    .type = &%s_T_%s,\n", g_prefix, s->name);
    fprintf(out, "    .flow_kind = NODE_FLOW_NONE,\n");
    fputs("};\n\n", out);

    fprintf(out, "static pin_spec %s_%s_break_inputs[] = {\n", g_prefix, s->name);
    fprintf(out, "    { \"%s\", PIN_STRUCT, \"%s\" },\n", s->name, s->name);
    fputs("};\n", out);
    fprintf(out, "static pin_spec %s_%s_break_outputs[] = {\n", g_prefix, s->name);
    for (int j = 0; j < acount(s->fields); j++) {
        emit_pin_spec(out, s->fields[j].name, s->fields[j].pin, field_type_name(&s->fields[j]));
    }
    fputs("};\n", out);
    fprintf(out, "node_desc %s_NODE_Break_%s = {\n", g_prefix, s->name);
    fprintf(out, "    .name = \"Break_%s\",\n", s->name);
    fprintf(out, "    .input_count = 1,\n");
    fprintf(out, "    .output_count = %d,\n", acount(s->fields));
    fprintf(out, "    .inputs = %s_%s_break_inputs,\n", g_prefix, s->name);
    fprintf(out, "    .outputs = %s_%s_break_outputs,\n", g_prefix, s->name);
    fprintf(out, "    .pure_kind = NODE_PURE_BREAK,\n");
    fprintf(out,"    .access = &%s_%s_value_access,\n",g_prefix,s->name);
    fprintf(out, "    .type = &%s_T_%s,\n", g_prefix, s->name);
    fprintf(out, "    .flow_kind = NODE_FLOW_NONE,\n");
    fputs("};\n\n", out);
}

static void emit_value_assignment(
    FILE *out,
    const char *target,
    const char *pin,
    const char *type_name,
    const char *member,
    const char *expr
)
{
    fprintf(
        out,
        "    %s = (pin_value){ .type = %s, .type_name = \"%s\" };\n",
        target,
        pin,
        type_name ? type_name : ""
    );
    if (!strcmp(pin, "PIN_STRUCT")) {
        fprintf(out, "    %s.ptr = flow_context_scratch_alloc(ctx, sizeof(%s));\n", target, type_name);
        fprintf(out, "    if (!%s.ptr) return FLOW_ERR_NODE;\n", target);
        fprintf(out, "    *(%s *)%s.ptr = %s;\n", type_name, target, expr);
    } else {
        fprintf(out, "    %s.%s = %s;\n", target, member, expr);
    }
}

static void emit_local_arg(FILE *out, const char *spelling, const char *pin, int index);

/* REFL-9/10: generated executable wrappers evaluate ordinary inputs, invoke
 * the native function once, then expose its return and explicit outputs as
 * stable data pins. */
void emit_exec_fn(FILE *out, const fn_info *f)
{
    fprintf(out, "static int __flow_%s_%s(node *n, flow_context *ctx) {\n", g_prefix, f->name);
    fputs("    (void)n;\n", out);
    int input_pin = 1;
    for (int j = 0; j < acount(f->pname); j++) {
        if (f->pout[j]) {
            continue;
        }
        if (f->pservice[j]) {
            fprintf(out, "    pin_value __service_value%d;\n", j);
            fprintf(out, "    void *__service%d = NULL;\n", j);
            fprintf(
                out,
                "    if (flow_context_eval_data(ctx, %d, &__service_value%d) "
                "== FLOW_OK && __service_value%d.u64)\n",
                input_pin++,
                j,
                j
            );
            fprintf(out, "        __service%d = (void *)(uintptr_t)__service_value%d.u64;\n", j, j);
            fprintf(out, "    if (!__service%d) __service%d = flow_context_service(ctx, \"%s\");\n", j, j, f->pservice_type[j]);
            fprintf(
                out,
                "    if (!__service%d) return flow_context_fail(ctx, "
                "FLOW_ERR_NODE, \"missing service %s\");\n",
                j,
                f->pservice_type[j]
            );
        } else {
            fprintf(out, "    pin_value __in%d;\n", j);
            fprintf(out, "    if (flow_context_eval_data(ctx, %d, &__in%d)) return FLOW_ERR_NODE;\n", input_pin++, j);
        }
    }
    for (int j = 0; j < acount(f->pname); j++) {
        if (f->pout[j]) {
            fprintf(out, "    %s __out%d = { 0 };\n", f->pout_type[j], j);
        }
    }
    if (!f->ret_void) {
        fprintf(out, "    %s __ret;\n", f->ret);
    }
    if (!f->ret_void) {
        fprintf(out, "    __ret = %s(", f->name);
    } else {
        fprintf(out, "    %s(", f->name);
    }
    for (int j = 0; j < acount(f->pname); j++) {
        if (j) {
            fputs(", ", out);
        }
        if (f->pout[j]) {
            fprintf(out, "&__out%d", j);
        } else if (f->pservice[j]) {
            fprintf(out, "__service%d", j);
        } else {
            emit_local_arg(out, f->ptype[j], f->ppin[j], j);
        }
    }
    fputs(");\n", out);
    int output_pin = 1;
    if (!f->ret_void) {
        const char *member = pin_member(f->ret_pin);
        emit_value_assignment(out, "(*flow_context_output_value(ctx, 1))", f->ret_pin, f->ret, member, "__ret");
        output_pin++;
    }
    for (int j = 0; j < acount(f->pname); j++) {
        if (f->pout[j]) {
            char target[64];
            snprintf(target, sizeof target, "(*flow_context_output_value(ctx, %d))", output_pin++);
            char expr[32];
            snprintf(expr, sizeof expr, "__out%d", j);
            emit_value_assignment(out, target, f->ppin[j], f->pout_type[j], pin_member(f->ppin[j]), expr);
        }
    }
    fputs("    return flow_context_continue(ctx, 0);\n}\n\n", out);
}

/* Emit the call node_desc for one function. */
static void emit_local_arg(FILE *out, const char *spelling, const char *pin, int index)
{
    if (!strcmp(pin, "PIN_STRUCT")) {
        fprintf(out, "*(%s *)__in%d.ptr", spelling, index);
    } else {
        fprintf(out, "__in%d.%s", index, pin_member(pin));
    }
}

static int fn_has_access(const fn_info *f)
{
    return acount(f->reads) || acount(f->writes) || f->effect_flags;
}

static const char *jit_kind_name(const char *kind)
{
    if (!kind) {
        return NULL;
    }
    if (strcmp(kind, "v2-scale") == 0) {
        return "NODE_JIT_MUL";
    }
    if (strcmp(kind, "v2-add") == 0) {
        return "NODE_JIT_ADD";
    }
    if (strcmp(kind, "v2-move") == 0) {
        return "NODE_JIT_V2_MOVE";
    }
    if (strcmp(kind, "identity") == 0) {
        return "NODE_JIT_IDENTITY";
    }
    return NULL;
}

static void emit_jit_kind(FILE *out, const fn_info *f)
{
    const char *kind = jit_kind_name(f->jit_kind);
    if (kind) {
        fprintf(out, "    .jit_kind = %s,\n", kind);
    }
    if (f->jit_flags) {
        fprintf(out, "    .jit_flags = NODE_JIT_FLAG_FAST_MATH,\n");
    }
}

static void emit_conversion_fields(FILE *out, const fn_info *f)
{
    if (!f->conversion_from || !f->conversion_to) {
        return;
    }
    fprintf(out, "    .conversion_from = \"%s\", .conversion_to = \"%s\",\n", f->conversion_from, f->conversion_to);
}

static void emit_fn_access(FILE *out, const fn_info *f)
{
    if (!fn_has_access(f)) {
        return;
    }
    if (acount(f->reads)) {
        fprintf(out, "static const uint64_t %s_%s_reads[] = {", g_prefix, f->name);
        for (int i = 0; i < acount(f->reads); i++) {
            fprintf(
                out,
                "%sUINT64_C(%llu)",
                i ? ", " : "",
                (unsigned long long)stable_service_id(f->reads[i])
            );
        }
        fputs("};\n", out);
    }
    if (acount(f->writes)) {
        fprintf(out, "static const uint64_t %s_%s_writes[] = {", g_prefix, f->name);
        for (int i = 0; i < acount(f->writes); i++) {
            fprintf(
                out,
                "%sUINT64_C(%llu)",
                i ? ", " : "",
                (unsigned long long)stable_service_id(f->writes[i])
            );
        }
        fputs("};\n", out);
    }
    fprintf(out, "static const node_access %s_%s_access = { .reads = ", g_prefix, f->name);
    if (acount(f->reads)) {
        fprintf(out, "%s_%s_reads", g_prefix, f->name);
    } else {
        fputs("NULL", out);
    }
    fprintf(out, ", .read_count = %d, .writes = ", acount(f->reads));
    if (acount(f->writes)) {
        fprintf(out, "%s_%s_writes", g_prefix, f->name);
    } else {
        fputs("NULL", out);
    }
    fprintf(out, ", .write_count = %d, .flags = %u };\n", acount(f->writes), f->effect_flags);
}

void emit_exec_node(FILE *out, const fn_info *f)
{
    int input_count = 1;
    int output_count = 1 + (f->ret_void ? 0 : 1);
    for (int i = 0; i < acount(f->pname); i++) {
        if (f->pout[i]) {
            output_count++;
        } else {
            input_count++;
        }
    }
    fprintf(out, "static pin_spec %s_%s_exec_inputs[] = {\n", g_prefix, f->name);
    fprintf(out, "    { \"Exec\", PIN_FLOW, NULL, PIN_EXEC },\n");
    for (int i = 0; i < acount(f->pname); i++) {
        if (!f->pout[i]) {
            emit_pin_spec_flags(
                out,
                param_label(f, i),
                f->pservice[i] ? "PIN_HANDLE" : f->ppin[i],
                f->pservice[i] ? f->pservice_type[i] : f->ptype[i],
                f->pservice[i] ? PIN_FLAG_SERVICE : 0
            );
        }
    }
    fputs("};\n", out);
    fprintf(out, "static pin_spec %s_%s_exec_outputs[] = {\n", g_prefix, f->name);
    fputs("    { \"Then\", PIN_FLOW, NULL, PIN_EXEC },\n", out);
    if (!f->ret_void) {
        emit_pin_spec(out, "return", f->ret_pin, f->ret);
    }
    for (int i = 0; i < acount(f->pname); i++) {
        if (f->pout[i]) {
            emit_pin_spec(out, param_label(f, i), f->ppin[i], f->pout_type[i]);
        }
    }
    fputs("};\n", out);
    emit_fn_access(out, f);
    fprintf(out, "node_desc %s_NODE_%s = {\n", g_prefix, f->name);
    fprintf(
        out,
        "    .name = \"%s\", .input_count = %d, .output_count = %d,\n",
        f->display_name ? f->display_name : f->name,
        input_count,
        output_count
    );
    fprintf(
        out,
        "    .inputs = %s_%s_exec_inputs, .outputs = %s_%s_exec_outputs,\n",
        g_prefix,
        f->name,
        g_prefix,
        f->name
    );
    fprintf(out, "    .flow_kind = NODE_FLOW_CUSTOM, .flow = __flow_%s_%s,\n", g_prefix, f->name);
    emit_conversion_fields(out, f);
    if (fn_has_access(f)) {
        fprintf(out, "    .access = &%s_%s_access,\n", g_prefix, f->name);
    }
    if (f->category) {
        fprintf(out, "    .category = \"%s\",\n", f->category);
    }
    if (f->keywords) {
        fprintf(out, "    .keywords = \"%s\",\n", f->keywords);
    }
    fputs("    .display_class = NODE_DISPLAY_FUNCTION,\n};\n\n", out);
}

/* Emit the call node_desc for one function. */
void emit_fn_node(FILE *out, const fn_info *f)
{
    if (acount(f->pname) == 0) {
        fprintf(out, "static pin_spec %s_%s_inputs[1];\n", g_prefix, f->name);
    } else {
        fprintf(out, "static pin_spec %s_%s_inputs[] = {\n", g_prefix, f->name);
        for (int j = 0; j < acount(f->pname); j++) {
            emit_pin_spec(out, param_label(f, j), f->ppin[j], f->ptype[j]);
        }
        fputs("};\n", out);
    }
    int output_count = f->ret_void ? 0 : 1;
    fprintf(out, "static pin_spec %s_%s_outputs[%d] = {\n", g_prefix, f->name, output_count ? output_count : 1);
    if (!f->ret_void) {
        fprintf(out, "    { \"return\", %s, \"%s\" },\n", f->ret_pin, f->ret);
    }
    fputs("};\n", out);
    emit_fn_access(out, f);
    fprintf(out, "node_desc %s_NODE_%s = {\n", g_prefix, f->name);
    fprintf(out, "    .name = \"%s\",\n", f->display_name ? f->display_name : f->name);
    fprintf(out, "    .input_count = %d,\n", acount(f->pname));
    fprintf(out, "    .output_count = %d,\n", output_count);
    fprintf(out, "    .inputs = %s_%s_inputs,\n", g_prefix, f->name);
    if (output_count) {
        fprintf(out, "    .outputs = %s_%s_outputs,\n", g_prefix, f->name);
    } else {
        fputs("    .outputs = NULL,\n", out);
    }
    fprintf(out, "    .pure_kind = %s,\n", f->ret_void ? "NODE_PURE_VOID_CALL" : "NODE_PURE_CALL");
    emit_jit_kind(out, f);
    fprintf(out, "    .fn = __wrap_%s_%s,\n", g_prefix, f->name);
    if (fn_fast_supported(f)) {
        fprintf(out, "    .fast = __fast_%s_%s,\n", g_prefix, f->name);
    }
    if (fn_direct_supported(f)) {
        static const char *member[] = { "zero", "one", "two", "three", "four" };
        int count = acount(f->pname);
        fprintf(out, "    .direct = { .%s = __fast_direct_%s_%s },\n", member[count], g_prefix, f->name);
        fprintf(out, "    .direct_count = %d,\n", count + 1);
    }
    fprintf(out, "    .flow_kind = NODE_FLOW_NONE,\n");
    emit_conversion_fields(out, f);
    if (fn_has_access(f)) {
        fprintf(out, "    .access = &%s_%s_access,\n", g_prefix, f->name);
    }
    if (f->category) {
        fprintf(out, "    .category = \"%s\",\n", f->category);
    }
    if (f->keywords) {
        fprintf(out, "    .keywords = \"%s\",\n", f->keywords);
    }
    fputs("};\n\n", out);
}

/* Emit the descriptor for a contextual, multi-output pure read. */
void emit_read_node(FILE *out, const fn_info *f)
{
    int input_count = 0;
    int output_count = 0;
    for (int i = 0; i < acount(f->pname); i++) {
        if (f->pout[i]) {
            output_count++;
        } else {
            input_count++;
        }
    }
    if (input_count) {
        fprintf(out, "static pin_spec %s_%s_read_inputs[] = {\n", g_prefix, f->name);
        for (int i = 0; i < acount(f->pname); i++) {
            if (!f->pout[i]) {
                emit_pin_spec(out, param_label(f, i), f->ppin[i], f->ptype[i]);
            }
        }
        fputs("};\n", out);
    } else {
        fprintf(out, "static pin_spec %s_%s_read_inputs[1];\n", g_prefix, f->name);
    }
    fprintf(out, "static pin_spec %s_%s_read_outputs[] = {\n", g_prefix, f->name);
    for (int i = 0; i < acount(f->pname); i++) {
        if (f->pout[i]) {
            emit_pin_spec(out, param_label(f, i), f->ppin[i], f->pout_type[i]);
        }
    }
    fputs("};\n", out);
    emit_fn_access(out, f);
    fprintf(out, "node_desc %s_NODE_%s = {\n", g_prefix, f->name);
    fprintf(out, "    .name = \"%s\",\n", f->display_name ? f->display_name : f->name);
    fprintf(out, "    .input_count = %d,\n", input_count);
    fprintf(out, "    .output_count = %d,\n", output_count);
    fprintf(out, "    .inputs = %s_%s_read_inputs,\n", g_prefix, f->name);
    fprintf(out, "    .outputs = %s_%s_read_outputs,\n", g_prefix, f->name);
    fputs("    .pure_kind = NODE_PURE_READ,\n", out);
    fprintf(out, "    .read_inputs = __read_%s_%s,\n", g_prefix, f->name);
    if (read_host_supported(f)) {
        fprintf(out, "    .host_call = &%s_%s_host,\n", g_prefix, f->name);
    }
    fputs("    .flow_kind = NODE_FLOW_NONE,\n", out);
    fputs("    .display_class = NODE_DISPLAY_FUNCTION,\n", out);
    if (f->category) {
        fprintf(out, "    .category = \"%s\",\n", f->category);
    }
    if (f->keywords) {
        fprintf(out, "    .keywords = \"%s\",\n", f->keywords);
    }
    if (fn_has_access(f)) {
        fprintf(out, "    .access = &%s_%s_access,\n", g_prefix, f->name);
    }
    fputs("};\n\n", out);
}

/* ---------- component emission ---------- */
