/* reflect/reflect.c -- build-time libclang reflection tool.
 *
 * Parses an annotated C source file and emits generated/registry.c:
 * one type_desc per "node:type" struct (real sizeof/offsetof), and one
 * Make + Break node_desc per struct plus a call node_desc per "node:fn"
 * function.
 */
#include "../core/array.h" // IWYU pragma: keep
#include "../core/pin.h"

#include <clang-c/Index.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed;
static void reflect_error(CXCursor c, const char *message)
{
    CXSourceLocation loc = clang_getCursorLocation(c);
    CXFile file = NULL;
    unsigned line = 0, column = 0, offset = 0;
    clang_getSpellingLocation(loc, &file, &line, &column, &offset);
    CXString name = clang_getCursorSpelling(c);
    fprintf(
        stderr,
        "reflect: %s:%u:%u: %s (%s)\n",
        file ? clang_getCString(clang_getFileName(file)) : "<unknown>",
        line,
        column,
        message,
        clang_getCString(name)
    );
    clang_disposeString(name);
    g_failed = 1;
}

/* ---------- dynamic lists ---------- */

typedef struct
{
    char *name;       /* field name */
    char *spelling;   /* C type spelling, e.g. "float", "Vec3" */
    const char *pin;  /* pin_type enum name, e.g. "PIN_F32" */
    long long offset; /* bytes */
} field_info;

typedef struct
{
    char *name;
    char *file; /* header basename where the definition lives */
    field_info *fields;
} struct_info;

typedef struct
{
    char *name;
    char *ret;           /* return type spelling */
    const char *ret_pin; /* pin_type enum name for the return, or NULL for void */
    int exec;
    int ret_void;
    char **pname;         /* param names */
    char **ptype;         /* param type spellings */
    const char **ppin;    /* pin_type enum names per param */
    int *pout;            /* explicit NODE_OUT pointer parameters */
    int *pservice;        /* service-backed parameters */
    char **pservice_type; /* nominal service type */
    char **pout_type;     /* pointee spelling for explicit outputs */
    char *operator_name;  /* optional NODE_OPERATOR name */
} fn_info;

typedef struct
{
    struct_info *structs;
    fn_info *fns;
} collect_ctx;

static char *cxstr(CXString s)
{
    const char *c = clang_getCString(s);
    char *r = strdup(c ? c : "");
    clang_disposeString(s);
    return r;
}

/* parent_dir: buf = directory containing path (dirname semantics). */
static void parent_dir(const char *path, char *buf, size_t bufsz)
{
    const char *slash = strrchr(path, '/');
    if (!slash) {
        strcpy(buf, ".");
        return;
    }
    size_t n = (size_t)(slash - path);
    if (n == 0) {
        strcpy(buf, "/");
        return;
    }
    if (n >= bufsz) {
        n = bufsz - 1;
    }
    memcpy(buf, path, n);
    buf[n] = '\0';
}

/* basename of the header that declares cursor c (e.g. "vec3.h"). */
static char *cursor_file_basename(CXCursor c)
{
    CXSourceLocation loc = clang_getCursorLocation(c);
    CXFile file = NULL;
    unsigned line, col, off;
    clang_getSpellingLocation(loc, &file, &line, &col, &off);
    char *full = file ? cxstr(clang_getFileName(file)) : strdup("?");
    const char *base = strrchr(full, '/');
    base = base ? base + 1 : full;
    char *r = strdup(base);
    free(full);
    return r;
}

static int ends_with(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls >= lx && strcmp(s + ls - lx, suffix) == 0;
}

/* ---------- annotation detection ---------- */

typedef struct
{
    const char *want;
    int found;
} ann_ctx;

static enum CXChildVisitResult find_annotate(CXCursor c, CXCursor, CXClientData data)
{
    ann_ctx *ctx = (ann_ctx *)data;
    if (clang_getCursorKind(c) == CXCursor_AnnotateAttr) {
        CXString s = clang_getCursorSpelling(c);
        if (strcmp(clang_getCString(s), ctx->want) == 0) {
            ctx->found = 1;
        }
        clang_disposeString(s);
        if (ctx->found) {
            return CXChildVisit_Break;
        }
    }
    return CXChildVisit_Continue;
}

static int has_annotate(CXCursor c, const char *want)
{
    ann_ctx ctx = { want, 0 };
    clang_visitChildren(c, find_annotate, &ctx);
    return ctx.found;
}

typedef struct
{
    const char *prefix;
    char *value;
} ann_value_ctx;

static enum CXChildVisitResult find_annotate_prefix(CXCursor c, CXCursor, CXClientData data)
{
    ann_value_ctx *ctx = (ann_value_ctx *)data;
    if (clang_getCursorKind(c) != CXCursor_AnnotateAttr) {
        return CXChildVisit_Continue;
    }
    CXString spelling = clang_getCursorSpelling(c);
    const char *text = clang_getCString(spelling);
    size_t length = strlen(ctx->prefix);
    if (text && strncmp(text, ctx->prefix, length) == 0) {
        ctx->value = strdup(text + length);
        clang_disposeString(spelling);
        return CXChildVisit_Break;
    }
    clang_disposeString(spelling);
    return CXChildVisit_Continue;
}

static char *annotation_value(CXCursor c, const char *prefix)
{
    ann_value_ctx ctx = { prefix, NULL };
    clang_visitChildren(c, find_annotate_prefix, &ctx);
    return ctx.value;
}

/* ---------- type mapping ---------- */

static const char *pin_name(CXType t)
{
    CXType canon = clang_getCanonicalType(t);
    switch (canon.kind) {
        case CXType_Bool: return "PIN_BOOL";
        case CXType_Char_S:
        case CXType_SChar: return "PIN_I8";
        case CXType_Char_U:
        case CXType_UChar: return "PIN_U8";
        case CXType_Short: return "PIN_I16";
        case CXType_UShort: return "PIN_U16";
        case CXType_Int: return "PIN_I32";
        case CXType_UInt: return "PIN_U32";
        case CXType_Long:
        case CXType_LongLong: {
            long long size = clang_Type_getSizeOf(canon);
            return size == 4 ? "PIN_I32" : size == 8 ? "PIN_I64" : NULL;
        }
        case CXType_ULong:
        case CXType_ULongLong: {
            long long size = clang_Type_getSizeOf(canon);
            return size == 4 ? "PIN_U32" : size == 8 ? "PIN_U64" : NULL;
        }
        case CXType_Float: return "PIN_F32";
        case CXType_Double: return "PIN_F64";
        case CXType_Enum: return "PIN_ENUM";
        case CXType_Record: return "PIN_STRUCT";
        case CXType_Pointer: {
            CXType pointee = clang_getCanonicalType(clang_getPointeeType(t));
            if (pointee.kind == CXType_Char_U || pointee.kind == CXType_UChar ||
                pointee.kind == CXType_Char_S || pointee.kind == CXType_SChar) {
                return "PIN_STR";
            }
            return NULL;
        }
        default: return NULL;
    }
}

/* ---------- collection ---------- */

static enum CXChildVisitResult field_visitor(CXCursor c, CXCursor, CXClientData data)
{
    if (clang_getCursorKind(c) != CXCursor_FieldDecl) {
        return CXChildVisit_Continue;
    }
    field_info **l = (field_info **)data;
    field_info f;
    CXType t = clang_getCursorType(c);
    f.name = cxstr(clang_getCursorSpelling(c));
    f.spelling = cxstr(clang_getTypeSpelling(t));
    f.pin = pin_name(t);
    if (!f.pin) {
        reflect_error(c, "unsupported field type");
        free(f.name);
        free(f.spelling);
        return CXChildVisit_Continue;
    }
    f.offset = clang_Cursor_getOffsetOfField(c) / 8; /* bits -> bytes */
    if (clang_Cursor_isBitField(c)) {
        reflect_error(c, "bit-fields are unsupported");
        free(f.name);
        free(f.spelling);
        return CXChildVisit_Continue;
    }
    if (!strcmp(f.pin, "PIN_ENUM") &&
        clang_Type_getSizeOf(clang_getCanonicalType(t)) != sizeof(int32_t)) {
        reflect_error(c, "enum fields must use a 32-bit representation");
        free(f.name);
        free(f.spelling);
        return CXChildVisit_Continue;
    }
    apush(*l, f);
    return CXChildVisit_Continue;
}

static enum CXChildVisitResult param_visitor(CXCursor c, CXCursor, CXClientData data)
{
    if (clang_getCursorKind(c) != CXCursor_ParmDecl) {
        return CXChildVisit_Continue;
    }
    fn_info *f = (fn_info *)data;
    CXType raw = clang_getCursorType(c);
    int output = has_annotate(c, "node:out");
    char *service = annotation_value(c, "node:service:");
    CXType graph_type = output && raw.kind == CXType_Pointer
                          ? clang_getPointeeType(raw)
                          : raw;
    const char *pin = pin_name(graph_type);
    if ((!pin && !service) ||
        (!strcmp(pin ? pin : "", "PIN_ENUM") &&
         clang_Type_getSizeOf(clang_getCanonicalType(graph_type)) != sizeof(int32_t))) {
        reflect_error(c, "unsupported parameter type or missing explicit output annotation");
        free(service);
        return CXChildVisit_Continue;
    }
    apush(f->pname, cxstr(clang_getCursorSpelling(c)));
    apush(f->ptype, cxstr(clang_getTypeSpelling(raw)));
    apush(f->ppin, service ? "PIN_HANDLE" : pin);
    apush(f->pout, output);
    apush(f->pservice, service != NULL);
    apush(f->pservice_type, service ? strdup(service) : NULL);
    apush(f->pout_type, output ? cxstr(clang_getTypeSpelling(graph_type)) : NULL);
    free(service);
    return CXChildVisit_Continue;
}

static enum CXChildVisitResult visitor(CXCursor c, CXCursor parent, CXClientData data)
{
    collect_ctx *ctx = (collect_ctx *)data;
    enum CXCursorKind k = clang_getCursorKind(c);

    if (k == CXCursor_StructDecl && clang_isCursorDefinition(c) &&
        clang_getCursorKind(parent) != CXCursor_TypedefDecl) {
        if (has_annotate(c, "node:type")) {
            struct_info s;
            s.name = cxstr(clang_getCursorSpelling(c));
            s.file = cursor_file_basename(c);
            s.fields = NULL;
            clang_visitChildren(c, field_visitor, &s.fields);
            if (g_failed) {
                free(s.name);
                free(s.file);
                afree(s.fields);
            } else {
                apush(ctx->structs, s);
            }
        }
    } else if (k == CXCursor_FunctionDecl) {
        int pure = has_annotate(c, "node:fn");
        int exec = has_annotate(c, "node:exec");
        if (pure || exec) {
            if (pure && exec) {
                reflect_error(c, "a function must choose exactly one node contract");
                return CXChildVisit_Recurse;
            }
            fn_info f = { 0 };
            f.name = cxstr(clang_getCursorSpelling(c));
            f.ret = cxstr(clang_getTypeSpelling(clang_getCursorResultType(c)));
            f.ret_void = clang_getCursorResultType(c).kind == CXType_Void;
            f.ret_pin = f.ret_void ? NULL : pin_name(clang_getCursorResultType(c));
            f.exec = exec;
            if (!f.ret_void && (!f.ret_pin || (!strcmp(f.ret_pin, "PIN_ENUM") &&
                                               clang_Type_getSizeOf(clang_getCanonicalType(
                                                   clang_getCursorResultType(c)
                                               )) != sizeof(int32_t)))) {
                reflect_error(c, "unsupported return type");
                free(f.name);
                free(f.ret);
                return CXChildVisit_Recurse;
            }
            f.operator_name = annotation_value(c, "node:operator:");
            clang_visitChildren(c, param_visitor, &f);
            apush(ctx->fns, f);
        }
    }
    return CXChildVisit_Recurse;
}

/* ---------- emission helpers ---------- */

static const char *pin_member(const char *pin)
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
    if (!strcmp(pin, "PIN_U64")) {
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

/* Canonical primitive spelling from PIN_TYPE_LIST; NULL for PIN_STRUCT/
 * PIN_ENUM so those keep the libclang C type name. */
static const char *primitive_spelling(const char *pin)
{
    if (!strcmp(pin, "PIN_STRUCT") || !strcmp(pin, "PIN_ENUM")) {
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

static void emit_pin_spec(FILE *out, const char *name, const char *pin, const char *spelling)
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

/* Emit the uniform-signature wrapper that adapts a pure native fn to node_fn. */
static void emit_fn(FILE *out, const fn_info *f)
{
    fprintf(out, "static pin_value __wrap_%s(const pin_value *args, int nargs, arena *a) {\n", f->name);
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

/* Emit Make + Break node_desc for one struct. */
static void emit_struct_nodes(FILE *out, const struct_info *s)
{
    fprintf(out, "static pin_spec %s_make_inputs[] = {\n", s->name);
    for (int j = 0; j < acount(s->fields); j++) {
        emit_pin_spec(out, s->fields[j].name, s->fields[j].pin, s->fields[j].spelling);
    }
    fputs("};\n", out);
    fprintf(out, "static pin_spec %s_make_outputs[] = {\n", s->name);
    fprintf(out, "    { \"%s\", PIN_STRUCT, \"%s\" },\n", s->name, s->name);
    fputs("};\n", out);
    fprintf(out, "node_desc NODE_Make_%s = {\n", s->name);
    fprintf(out, "    .name = \"Make_%s\",\n", s->name);
    fprintf(out, "    .input_count = %d,\n", acount(s->fields));
    fprintf(out, "    .output_count = 1,\n");
    fprintf(out, "    .inputs = %s_make_inputs,\n", s->name);
    fprintf(out, "    .outputs = %s_make_outputs,\n", s->name);
    fprintf(out, "    .pure_kind = NODE_PURE_MAKE,\n");
    fprintf(out, "    .type = &T_%s,\n", s->name);
    fprintf(out, "    .flow_kind = NODE_FLOW_NONE,\n");
    fputs("};\n\n", out);

    fprintf(out, "static pin_spec %s_break_inputs[] = {\n", s->name);
    fprintf(out, "    { \"%s\", PIN_STRUCT, \"%s\" },\n", s->name, s->name);
    fputs("};\n", out);
    fprintf(out, "static pin_spec %s_break_outputs[] = {\n", s->name);
    for (int j = 0; j < acount(s->fields); j++) {
        emit_pin_spec(out, s->fields[j].name, s->fields[j].pin, s->fields[j].spelling);
    }
    fputs("};\n", out);
    fprintf(out, "node_desc NODE_Break_%s = {\n", s->name);
    fprintf(out, "    .name = \"Break_%s\",\n", s->name);
    fprintf(out, "    .input_count = 1,\n");
    fprintf(out, "    .output_count = %d,\n", acount(s->fields));
    fprintf(out, "    .inputs = %s_break_inputs,\n", s->name);
    fprintf(out, "    .outputs = %s_break_outputs,\n", s->name);
    fprintf(out, "    .pure_kind = NODE_PURE_BREAK,\n");
    fprintf(out, "    .type = &T_%s,\n", s->name);
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
static void emit_exec_fn(FILE *out, const fn_info *f)
{
    fprintf(out, "static int __flow_%s(node *n, flow_context *ctx) {\n", f->name);
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
        emit_value_assignment(out, "n->outputs[1]", f->ret_pin, f->ret, member, "__ret");
        output_pin++;
    }
    for (int j = 0; j < acount(f->pname); j++) {
        if (f->pout[j]) {
            char target[64];
            snprintf(target, sizeof target, "n->outputs[%d]", output_pin++);
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

static void emit_exec_node(FILE *out, const fn_info *f)
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
    fprintf(out, "static pin_spec %s_exec_inputs[] = {\n", f->name);
    fprintf(out, "    { \"Exec\", PIN_FLOW, NULL, PIN_EXEC },\n");
    for (int i = 0; i < acount(f->pname); i++) {
        if (!f->pout[i]) {
            emit_pin_spec_flags(
                out,
                f->pname[i],
                f->pservice[i] ? "PIN_HANDLE" : f->ppin[i],
                f->pservice[i] ? f->pservice_type[i] : f->ptype[i],
                f->pservice[i] ? PIN_FLAG_SERVICE : 0
            );
        }
    }
    fputs("};\n", out);
    fprintf(out, "static pin_spec %s_exec_outputs[] = {\n", f->name);
    fputs("    { \"Then\", PIN_FLOW, NULL, PIN_EXEC },\n", out);
    if (!f->ret_void) {
        emit_pin_spec(out, "return", f->ret_pin, f->ret);
    }
    for (int i = 0; i < acount(f->pname); i++) {
        if (f->pout[i]) {
            emit_pin_spec(out, f->pname[i], f->ppin[i], f->pout_type[i]);
        }
    }
    fputs("};\n", out);
    fprintf(out, "node_desc NODE_%s = {\n", f->name);
    fprintf(out, "    .name = \"%s\", .input_count = %d, .output_count = %d,\n", f->name, input_count, output_count);
    fprintf(out, "    .inputs = %s_exec_inputs, .outputs = %s_exec_outputs,\n", f->name, f->name);
    fprintf(out, "    .flow_kind = NODE_FLOW_CUSTOM, .flow = __flow_%s,\n", f->name);
    fputs("    .display_class = NODE_DISPLAY_FUNCTION,\n};\n\n", out);
}

/* Emit the call node_desc for one function. */
static void emit_fn_node(FILE *out, const fn_info *f)
{
    if (acount(f->pname) == 0) {
        fprintf(out, "static pin_spec %s_inputs[1];\n", f->name);
    } else {
        fprintf(out, "static pin_spec %s_inputs[] = {\n", f->name);
        for (int j = 0; j < acount(f->pname); j++) {
            emit_pin_spec(out, f->pname[j], f->ppin[j], f->ptype[j]);
        }
        fputs("};\n", out);
    }
    int output_count = f->ret_void ? 0 : 1;
    fprintf(out, "static pin_spec %s_outputs[%d] = {\n", f->name, output_count ? output_count : 1);
    if (!f->ret_void) {
        fprintf(out, "    { \"return\", %s, \"%s\" },\n", f->ret_pin, f->ret);
    }
    fputs("};\n", out);
    fprintf(out, "node_desc NODE_%s = {\n", f->name);
    fprintf(out, "    .name = \"%s\",\n", f->name);
    fprintf(out, "    .input_count = %d,\n", acount(f->pname));
    fprintf(out, "    .output_count = %d,\n", output_count);
    fprintf(out, "    .inputs = %s_inputs,\n", f->name);
    if (output_count) {
        fprintf(out, "    .outputs = %s_outputs,\n", f->name);
    } else {
        fputs("    .outputs = NULL,\n", out);
    }
    fprintf(out, "    .pure_kind = %s,\n", f->ret_void ? "NODE_PURE_VOID_CALL" : "NODE_PURE_CALL");
    fprintf(out, "    .fn = __wrap_%s,\n", f->name);
    fprintf(out, "    .flow_kind = NODE_FLOW_NONE,\n");
    fputs("};\n\n", out);
}

/* ---------- emission ---------- */

static void emit(FILE *out, const collect_ctx *ctx)
{
    fputs("/* generated/registry.c -- generated by reflect, do not edit. */\n\n", out);
    fputs("#include <stddef.h>\n", out);
    fputs("#include <stdint.h>\n", out);
    for (int i = 0; i < acount(ctx->structs); i++) {
        const char *f = ctx->structs[i].file;
        if (!f || !ends_with(f, ".h")) {
            continue;
        }
        int dup = 0;
        for (int j = 0; j < i; j++) {
            const char *g = ctx->structs[j].file;
            if (g && strcmp(g, f) == 0) {
                dup = 1;
            }
        }
        if (!dup) {
            fprintf(out, "#include \"%s\"\n", f);
        }
    }
    fputs("#include \"core/type.h\"\n", out);
    fputs("#include \"core/nodes.h\"\n", out);
    fputs("#include \"core/flow.h\"\n", out);
    fputs("#include \"core/service.h\"\n", out);
    fputs("#include \"core/operator.h\"\n\n", out);

    for (int i = 0; i < acount(ctx->structs); i++) {
        const struct_info *s = &ctx->structs[i];
        fprintf(out, "static const type_field %s_fields[] = {\n", s->name);
        for (int j = 0; j < acount(s->fields); j++) {
            const field_info *f = &s->fields[j];
            fprintf(
                out,
                "    { \"%s\", %s, offsetof(%s, %s), sizeof(%s), \"%s\" },\n",
                f->name,
                f->pin,
                s->name,
                f->name,
                f->spelling,
                f->spelling
            );
        }
        fputs("};\n\n", out);

        fprintf(out, "const type_desc T_%s = {\n", s->name);
        fprintf(out, "    .name = \"%s\",\n", s->name);
        fprintf(out, "    .size = sizeof(%s),\n", s->name);
        fprintf(out, "    .fields = %s_fields,\n", s->name);
        fprintf(out, "    .field_count = %d,\n", acount(s->fields));
        fputs("};\n\n", out);
    }

    fputs("const type_desc *const REGISTRY_TYPES[] = {\n", out);
    for (int i = 0; i < acount(ctx->structs); i++) {
        fprintf(out, "    &T_%s,\n", ctx->structs[i].name);
    }
    fputs("};\n", out);
    fprintf(out, "const int REGISTRY_TYPE_COUNT = %d;\n\n", acount(ctx->structs));

    int service_count = 0;
    for (int i = 0; i < acount(ctx->fns); i++) {
        for (int j = 0; j < acount(ctx->fns[i].pname); j++) {
            if (ctx->fns[i].pservice[j] && ctx->fns[i].pservice_type[j]) {
                int seen = 0;
                for (int k = 0; k < i; k++) {
                    for (int l = 0; l < acount(ctx->fns[k].pname); l++) {
                        seen |= ctx->fns[k].pservice[l] &&
                                strcmp(ctx->fns[k].pservice_type[l], ctx->fns[i].pservice_type[j]) ==
                                    0;
                    }
                }
                for (int l = 0; l < j; l++) {
                    seen |= ctx->fns[i].pservice[l] &&
                            strcmp(ctx->fns[i].pservice_type[l], ctx->fns[i].pservice_type[j]) ==
                                0;
                }
                if (!seen) {
                    fprintf(
                        out,
                        "static const service_desc SERVICE_%d = { \"%s\", "
                        "%d };\n",
                        service_count,
                        ctx->fns[i].pservice_type[j],
                        service_count + 1
                    );
                    service_count++;
                }
            }
        }
    }
    fputs("const service_desc *const REGISTRY_SERVICES[] = {\n", out);
    for (int i = 0; i < service_count; i++) {
        fprintf(out, "    &SERVICE_%d,\n", i);
    }
    fputs("};\n", out);
    fprintf(out, "const int REGISTRY_SERVICE_COUNT = %d;\n\n", service_count);

    int operator_count = 0;
    for (int i = 0; i < acount(ctx->fns); i++) {
        if (ctx->fns[i].operator_name) {
            operator_count++;
        }
    }
    fputs("const operator_signature REGISTRY_OPERATORS[] = {\n", out);
    for (int i = 0; i < acount(ctx->fns); i++) {
        const fn_info *f = &ctx->fns[i];
        if (!f->operator_name) {
            continue;
        }
        const char *left = acount(f->ppin) > 0 ? f->ppin[0] : "PIN_FLOW";
        const char *right = acount(f->ppin) > 1 ? f->ppin[1] : "PIN_FLOW";
        const char *result = f->ret_pin ? f->ret_pin : "PIN_FLOW";
        fprintf(out, "    { \"%s\", %s, %s, %s, 0 },\n", f->operator_name, left, right, result);
    }
    fputs("};\n", out);
    fprintf(out, "const int REGISTRY_OPERATOR_COUNT = %d;\n\n", operator_count);

    /* native prototypes, so wrappers can call the annotated functions */
    for (int i = 0; i < acount(ctx->fns); i++) {
        const fn_info *f = &ctx->fns[i];
        fprintf(out, "%s %s(", f->ret, f->name);
        for (int j = 0; j < acount(f->pname); j++) {
            fprintf(out, "%s%s %s", j ? ", " : "", f->ptype[j], f->pname[j]);
        }
        fputs(");\n", out);
    }
    if (acount(ctx->fns)) {
        fputc('\n', out);
    }

    for (int i = 0; i < acount(ctx->fns); i++) {
        if (ctx->fns[i].exec) {
            emit_exec_fn(out, &ctx->fns[i]);
        } else {
            emit_fn(out, &ctx->fns[i]);
        }
    }

    for (int i = 0; i < acount(ctx->structs); i++) {
        emit_struct_nodes(out, &ctx->structs[i]);
    }
    for (int i = 0; i < acount(ctx->fns); i++) {
        if (ctx->fns[i].exec) {
            emit_exec_node(out, &ctx->fns[i]);
        } else {
            emit_fn_node(out, &ctx->fns[i]);
        }
    }

    fputs("node_desc *const REGISTRY_NODES[] = {\n", out);
    for (int i = 0; i < acount(ctx->structs); i++) {
        fprintf(out, "    &NODE_Make_%s,\n", ctx->structs[i].name);
        fprintf(out, "    &NODE_Break_%s,\n", ctx->structs[i].name);
    }
    for (int i = 0; i < acount(ctx->fns); i++) {
        fprintf(out, "    &NODE_%s,\n", ctx->fns[i].name);
    }
    fputs("};\n", out);
    fprintf(out, "const int REGISTRY_NODE_COUNT = %d;\n", acount(ctx->structs) * 2 + acount(ctx->fns));
}

/* ---------- main ---------- */

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: reflect <input.c> <output_registry.c>\n");
        return 2;
    }
    const char *input = argv[1];
    const char *output = argv[2];

    char srcdir[1024];
    char rootdir[1024];
    parent_dir(input, srcdir, sizeof srcdir);
    parent_dir(srcdir, rootdir, sizeof rootdir);

    CXIndex idx = clang_createIndex(0, 0);

    const char *const args[] = { "-x", "c", "-I", rootdir };
    int nargs = (int)(sizeof args / sizeof args[0]);

    CXTranslationUnit tu = clang_parseTranslationUnit(idx, input, args, nargs, NULL, 0, CXTranslationUnit_None);
    if (!tu) {
        fprintf(stderr, "reflect: failed to parse %s\n", input);
        clang_disposeIndex(idx);
        return 1;
    }
    unsigned ndiag = clang_getNumDiagnostics(tu);
    for (unsigned i = 0; i < ndiag; i++) {
        CXDiagnostic d = clang_getDiagnostic(tu, i);
        if (clang_getDiagnosticSeverity(d) >= CXDiagnostic_Error) {
            g_failed = 1;
        }
        CXString s = clang_formatDiagnostic(d, clang_defaultDiagnosticDisplayOptions());
        fprintf(stderr, "reflect: %s\n", clang_getCString(s));
        clang_disposeString(s);
        clang_disposeDiagnostic(d);
    }

    collect_ctx ctx = { NULL, NULL };
    clang_visitChildren(clang_getTranslationUnitCursor(tu), visitor, &ctx);
    if (g_failed) {
        clang_disposeTranslationUnit(tu);
        clang_disposeIndex(idx);
        return 1;
    }

    FILE *out = fopen(output, "w");
    if (!out) {
        perror("reflect: fopen");
        return 1;
    }
    emit(out, &ctx);
    fclose(out);

    printf("reflect: %d struct(s), %d function(s) -> %s\n", acount(ctx.structs), acount(ctx.fns), output);
    for (int i = 0; i < acount(ctx.structs); i++) {
        printf(
            "reflect:   struct %s (%d fields)\n",
            ctx.structs[i].name,
            acount(ctx.structs[i].fields)
        );
    }
    for (int i = 0; i < acount(ctx.fns); i++) {
        const fn_info *f = &ctx.fns[i];
        printf("reflect:   fn %s(", f->name);
        for (int j = 0; j < acount(f->pname); j++) {
            printf("%s%s %s", j ? ", " : "", f->ptype[j], f->pname[j]);
        }
        printf(") -> %s\n", f->ret);
    }

    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(idx);
    return 0;
}
