/* reflect/reflect.c -- build-time libclang reflection tool.
 *
 * Parses an annotated C source file and emits one registry pack source:
 * one type_desc per "node:type" struct (real sizeof/offsetof), and one
 * Make + Break node_desc per struct plus a call node_desc per "node:fn"
 * function. Typed adapters and optional JIT lowering hints are generated for
 * reflected functions as well.
 */
#include "../core/array.h" // IWYU pragma: keep
#include "../core/pin.h"

#include <clang-c/Index.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed;

static uint64_t stable_service_id(const char *name)
{
    uint64_t id = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        id = (id ^ *p) * UINT64_C(1099511628211);
    }
    return id ? id : 1;
}

static char g_prefix_storage[128];
static const char *g_prefix = "REFLECTION";
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
    long long size;   /* bytes */
} field_info;

typedef struct
{
    char *name;
    char *file; /* header basename where the definition lives */
    field_info *fields;
    long long size;
    int component;
    int dense;
    char *alias_of;
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
    char **reads;         /* NODE_READS type names */
    char **writes;        /* NODE_WRITES type names */
    unsigned effect_flags;
    char *operator_name; /* optional NODE_OPERATOR name */
    char *jit_kind;
    unsigned jit_flags;
    char *conversion_from;
    char *conversion_to;
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
    f.size = clang_Type_getSizeOf(clang_getCanonicalType(t));
    if (f.size < 0) {
        reflect_error(c, "field size is unavailable");
        free(f.name);
        free(f.spelling);
        return CXChildVisit_Continue;
    }
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

static void collect_alias(collect_ctx *ctx, CXCursor c, const char *name, int component, int dense)
{
    CXType underlying = clang_getTypedefDeclUnderlyingType(c);
    CXType record_type = clang_getCanonicalType(underlying);
    CXCursor definition = clang_getTypeDeclaration(record_type);
    if (clang_getCursorKind(definition) != CXCursor_StructDecl ||
        !clang_isCursorDefinition(definition)) {
        reflect_error(c, "type aliases must name a defined struct");
        return;
    }

    struct_info s = { 0 };
    s.name = strdup(name);
    s.file = cursor_file_basename(c);
    s.size = clang_Type_getSizeOf(underlying);
    s.component = component;
    s.dense = dense;
    CXString definition_name = clang_getCursorSpelling(definition);
    const char *underlying_name = clang_getCString(definition_name);
    if (underlying_name && strcmp(name, underlying_name) != 0) {
        s.alias_of = strdup(underlying_name);
    }
    clang_disposeString(definition_name);
    clang_visitChildren(definition, field_visitor, &s.fields);
    if (g_failed) {
        free(s.name);
        free(s.file);
        free(s.alias_of);
        afree(s.fields);
        return;
    }
    apush(ctx->structs, s);
}

static enum CXChildVisitResult visitor(CXCursor c, CXCursor parent, CXClientData data)
{
    collect_ctx *ctx = (collect_ctx *)data;
    enum CXCursorKind k = clang_getCursorKind(c);

    if (k == CXCursor_TypedefDecl) {
        char *component_alias = annotation_value(c, "node:component-alias:");
        char *type_alias = annotation_value(c, "node:type-alias:");
        char *external_type = annotation_value(c, "node:external-type:");
        if (component_alias || type_alias || external_type) {
            char *typedef_name = cxstr(clang_getCursorSpelling(c));
            const char *name = external_type ? external_type : typedef_name;
            collect_alias(ctx, c, name, component_alias != NULL, 0);
            free(typedef_name);
        }
        free(component_alias);
        free(type_alias);
        free(external_type);
    } else if (k == CXCursor_StructDecl && clang_isCursorDefinition(c) && clang_getCursorKind(parent) != CXCursor_TypedefDecl) {
        int is_type = has_annotate(c, "node:type");
        int is_component = has_annotate(c, "node:component") ||
                           has_annotate(c, "node:component:index:dense");
        if (is_type || is_component) {
            struct_info s = { 0 };
            s.name = cxstr(clang_getCursorSpelling(c));
            s.file = cursor_file_basename(c);
            s.size = clang_Type_getSizeOf(clang_getCursorType(c));
            s.component = is_component;
            s.dense = has_annotate(c, "node:component:index:dense");
            s.fields = NULL;
            clang_visitChildren(c, field_visitor, &s.fields);
            if (g_failed) {
                free(s.name);
                free(s.file);
                free(s.alias_of);
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
            f.jit_kind = annotation_value(c, "node:jit:");
            if (f.jit_kind && strcmp(f.jit_kind, "v2-scale") != 0 &&
                strcmp(f.jit_kind, "v2-add") != 0 &&
                strcmp(f.jit_kind, "v2-move") != 0) {
                reflect_error(c, "unknown node JIT lowering kind");
            }
            if (has_annotate(c, "node:jit-fast-math")) {
                f.jit_flags |= 1u << 0;
            }
            f.conversion_from = annotation_value(c, "node:conversion-from:");
            f.conversion_to = annotation_value(c, "node:conversion-to:");
            if ((f.conversion_from == NULL) != (f.conversion_to == NULL)) {
                reflect_error(c, "a conversion needs both source and target types");
            }
            char *read_type = annotation_value(c, "node:reads:");
            char *write_type = annotation_value(c, "node:writes:");
            if (read_type) {
                apush(f.reads, read_type);
            }
            if (write_type) {
                apush(f.writes, write_type);
            }
            if (has_annotate(c, "node:entity-scoped")) {
                f.effect_flags |= 1u << 4;
            }
            if (has_annotate(c, "node:structural")) {
                f.effect_flags |= 1u << 0;
            }
            if (has_annotate(c, "node:world-read")) {
                f.effect_flags |= 1u << 1;
            }
            if (has_annotate(c, "node:host-sink")) {
                f.effect_flags |= 1u << 2;
            }
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
static void emit_fn(FILE *out, const fn_info *f)
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
    fprintf(out, "static pin_spec %s_%s_make_inputs[] = {\n", g_prefix, s->name);
    for (int j = 0; j < acount(s->fields); j++) {
        emit_pin_spec(out, s->fields[j].name, s->fields[j].pin, s->fields[j].spelling);
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
    fprintf(out, "    .type = &%s_T_%s,\n", g_prefix, s->name);
    fprintf(out, "    .flow_kind = NODE_FLOW_NONE,\n");
    fputs("};\n\n", out);

    fprintf(out, "static pin_spec %s_%s_break_inputs[] = {\n", g_prefix, s->name);
    fprintf(out, "    { \"%s\", PIN_STRUCT, \"%s\" },\n", s->name, s->name);
    fputs("};\n", out);
    fprintf(out, "static pin_spec %s_%s_break_outputs[] = {\n", g_prefix, s->name);
    for (int j = 0; j < acount(s->fields); j++) {
        emit_pin_spec(out, s->fields[j].name, s->fields[j].pin, s->fields[j].spelling);
    }
    fputs("};\n", out);
    fprintf(out, "node_desc %s_NODE_Break_%s = {\n", g_prefix, s->name);
    fprintf(out, "    .name = \"Break_%s\",\n", s->name);
    fprintf(out, "    .input_count = 1,\n");
    fprintf(out, "    .output_count = %d,\n", acount(s->fields));
    fprintf(out, "    .inputs = %s_%s_break_inputs,\n", g_prefix, s->name);
    fprintf(out, "    .outputs = %s_%s_break_outputs,\n", g_prefix, s->name);
    fprintf(out, "    .pure_kind = NODE_PURE_BREAK,\n");
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
static void emit_exec_fn(FILE *out, const fn_info *f)
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
    fprintf(out, "static pin_spec %s_%s_exec_inputs[] = {\n", g_prefix, f->name);
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
    fprintf(out, "static pin_spec %s_%s_exec_outputs[] = {\n", g_prefix, f->name);
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
    emit_fn_access(out, f);
    fprintf(out, "node_desc %s_NODE_%s = {\n", g_prefix, f->name);
    fprintf(out, "    .name = \"%s\", .input_count = %d, .output_count = %d,\n", f->name, input_count, output_count);
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
    fputs("    .display_class = NODE_DISPLAY_FUNCTION,\n};\n\n", out);
}

/* Emit the call node_desc for one function. */
static void emit_fn_node(FILE *out, const fn_info *f)
{
    if (acount(f->pname) == 0) {
        fprintf(out, "static pin_spec %s_%s_inputs[1];\n", g_prefix, f->name);
    } else {
        fprintf(out, "static pin_spec %s_%s_inputs[] = {\n", g_prefix, f->name);
        for (int j = 0; j < acount(f->pname); j++) {
            emit_pin_spec(out, f->pname[j], f->ppin[j], f->ptype[j]);
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
    fprintf(out, "    .name = \"%s\",\n", f->name);
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
    fputs("};\n\n", out);
}

/* ---------- component emission ---------- */

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = (const unsigned char *)data;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t hash_string(uint64_t hash, const char *text)
{
    return hash_bytes(hash, text ? text : "", strlen(text ? text : "") + 1);
}

static int pin_value_number(const char *pin)
{
    static const char *names[] = {
        "PIN_FLOW",   "PIN_BOOL", "PIN_I8",   "PIN_U8",  "PIN_I16",
        "PIN_U16",    "PIN_I32",  "PIN_U32",  "PIN_I64", "PIN_U64",
        "PIN_F32",    "PIN_F64",  "PIN_NAME", "PIN_STR", "PIN_HANDLE",
        "PIN_STRUCT", "PIN_ENUM",
    };
    for (int i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
        if (strcmp(names[i], pin) == 0) {
            return i;
        }
    }
    return 0;
}

static uint64_t component_layout_hash(const struct_info *s)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = hash_string(hash, s->name);
    size_t size = (size_t)s->size;
    int field_count = acount(s->fields);
    hash = hash_bytes(hash, &size, sizeof size);
    hash = hash_bytes(hash, &field_count, sizeof field_count);
    for (int i = 0; i < field_count; i++) {
        const field_info *field = &s->fields[i];
        size_t offset = (size_t)field->offset;
        size_t field_size = (size_t)field->size;
        int pin = pin_value_number(field->pin);
        hash = hash_string(hash, field->name);
        hash = hash_bytes(hash, &offset, sizeof offset);
        hash = hash_bytes(hash, &field_size, sizeof field_size);
        hash = hash_bytes(hash, &pin, sizeof pin);
        hash = hash_string(hash, field->spelling);
    }
    return hash;
}

static void emit_component_nodes(FILE *out, const struct_info *s)
{
    const char *name = s->name;
    fprintf(out, "static int __flow_%s_get_%s(node *n, flow_context *ctx) {\n", g_prefix, name);
    fprintf(out, "    (void)n;\n    ecs_world *__world = flow_context_service(ctx, \"ecs.world\");\n");
    fputs("    blueprint_instance *__instance = flow_context_instance(ctx);\n", out);
    fputs("    if (!__world || !__instance) return flow_context_fail(ctx, FLOW_ERR_NODE, \"component Get requires ecs.world\");\n", out);
    fputs("    ecs_entity __entity = ecs_entity_from_u64(blueprint_instance_owner(__instance).u64);\n", out);
    fprintf(out, "    const %s *__value = %s_%s_get(__world, __entity);\n", name, g_prefix, name);
    fputs("    if (!__value) return flow_context_fail(ctx, FLOW_ERR_NODE, \"entity lacks component\");\n", out);
    fprintf(out, "    pin_value *__out = flow_context_output_value(ctx, 1);\n    *__out = (pin_value){ .type = PIN_STRUCT, .type_name = \"%s\" };\n", name);
    fprintf(out, "    __out->ptr = flow_context_scratch_alloc(ctx, sizeof(%s));\n", name);
    fputs("    if (!__out->ptr) return FLOW_ERR_NODE;\n", out);
    fprintf(out, "    memcpy(__out->ptr, __value, sizeof(%s));\n", name);
    fputs("    return flow_context_continue(ctx, 0);\n}\n\n", out);

    fprintf(out, "static int __flow_%s_set_%s(node *n, flow_context *ctx) {\n", g_prefix, name);
    fprintf(out, "    (void)n;\n    ecs_world *__world = flow_context_service(ctx, \"ecs.world\");\n");
    fputs("    blueprint_instance *__instance = flow_context_instance(ctx);\n    pin_value __input;\n", out);
    fputs("    if (!__world || !__instance || flow_context_eval_data(ctx, 1, &__input) != FLOW_OK || !__input.ptr) return flow_context_fail(ctx, FLOW_ERR_NODE, \"component Set requires a value\");\n", out);
    fputs("    ecs_entity __entity = ecs_entity_from_u64(blueprint_instance_owner(__instance).u64);\n", out);
    fprintf(out, "    %s *__value = %s_%s_get(__world, __entity);\n", name, g_prefix, name);
    fputs("    if (!__value) return flow_context_fail(ctx, FLOW_ERR_NODE, \"entity lacks component\");\n", out);
    fprintf(out, "    memcpy(__value, __input.ptr, sizeof(%s));\n", name);
    fprintf(out, "    pin_value *__out = flow_context_output_value(ctx, 1);\n    *__out = (pin_value){ .type = PIN_STRUCT, .type_name = \"%s\" };\n", name);
    fprintf(out, "    __out->ptr = flow_context_scratch_alloc(ctx, sizeof(%s));\n", name);
    fputs("    if (!__out->ptr) return FLOW_ERR_NODE;\n", out);
    fprintf(out, "    memcpy(__out->ptr, __value, sizeof(%s));\n", name);
    fputs("    return flow_context_continue(ctx, 0);\n}\n\n", out);

    fprintf(out, "static int __flow_%s_has_%s(node *n, flow_context *ctx) {\n", g_prefix, name);
    fprintf(out, "    (void)n;\n    ecs_world *__world = flow_context_service(ctx, \"ecs.world\");\n");
    fputs("    blueprint_instance *__instance = flow_context_instance(ctx);\n", out);
    fputs("    if (!__world || !__instance) return flow_context_fail(ctx, FLOW_ERR_NODE, \"component Has requires ecs.world\");\n", out);
    fputs("    ecs_entity __entity = ecs_entity_from_u64(blueprint_instance_owner(__instance).u64);\n", out);
    fprintf(
        out,
        "    pin_value *__out = flow_context_output_value(ctx, 1);\n    "
        "*__out = (pin_value){ .type = PIN_BOOL, .type_name = \"bool\", .b = "
        "%s_%s_has(__world, __entity) };\n",
        g_prefix,
        name
    );
    fputs("    return flow_context_continue(ctx, 0);\n}\n\n", out);

    fprintf(out, "static int __flow_%s_add_%s(node *n, flow_context *ctx) {\n", g_prefix, name);
    fprintf(out, "    (void)n;\n    ecs_world *__world = flow_context_service(ctx, \"ecs.world\");\n");
    fputs("    blueprint_instance *__instance = flow_context_instance(ctx);\n", out);
    fputs("    if (!__world || !__instance) return flow_context_fail(ctx, FLOW_ERR_NODE, \"component Add requires ecs.world\");\n", out);
    fputs("    ecs_entity __entity = ecs_entity_from_u64(blueprint_instance_owner(__instance).u64);\n", out);
    fprintf(
        out,
        "    if (%s_%s_add(__world, __entity, 0) != ECS_OK) return "
        "flow_context_fail(ctx, FLOW_ERR_NODE, \"component Add failed\");\n",
        g_prefix,
        name
    );
    fputs("    return flow_context_continue(ctx, 0);\n}\n\n", out);

    fprintf(out, "static int __flow_%s_remove_%s(node *n, flow_context *ctx) {\n", g_prefix, name);
    fprintf(out, "    (void)n;\n    ecs_world *__world = flow_context_service(ctx, \"ecs.world\");\n");
    fputs("    blueprint_instance *__instance = flow_context_instance(ctx);\n", out);
    fputs("    if (!__world || !__instance) return flow_context_fail(ctx, FLOW_ERR_NODE, \"component Remove requires ecs.world\");\n", out);
    fputs("    ecs_entity __entity = ecs_entity_from_u64(blueprint_instance_owner(__instance).u64);\n", out);
    fprintf(
        out,
        "    if (%s_%s_remove(__world, __entity, 0) != ECS_OK) return "
        "flow_context_fail(ctx, FLOW_ERR_NODE, \"component Remove failed\");\n",
        g_prefix,
        name
    );
    fputs("    return flow_context_continue(ctx, 0);\n}\n\n", out);
}

static void emit_component_nodes_desc(FILE *out, const struct_info *s)
{
    const char *name = s->name;
    fprintf(
        out,
        "static const node_access %s_%s_get_access = { .reads = "
        "&%s_COMPONENT_%s.id, .read_count = 1, .flags = "
        "NODE_EFFECT_ENTITY_SCOPED };\n",
        g_prefix,
        name,
        g_prefix,
        name
    );
    fprintf(
        out,
        "static const node_access %s_%s_set_access = { .writes = "
        "&%s_COMPONENT_%s.id, .write_count = 1, .flags = "
        "NODE_EFFECT_ENTITY_SCOPED };\n",
        g_prefix,
        name,
        g_prefix,
        name
    );
    fprintf(
        out,
        "static const node_access %s_%s_has_access = { .reads = "
        "&%s_COMPONENT_%s.id, .read_count = 1, .flags = "
        "NODE_EFFECT_ENTITY_SCOPED };\n",
        g_prefix,
        name,
        g_prefix,
        name
    );
    fprintf(
        out,
        "static const node_access %s_%s_structural_access = { .writes = "
        "&%s_COMPONENT_%s.id, .write_count = 1, .flags = "
        "NODE_EFFECT_STRUCTURAL };\n\n",
        g_prefix,
        name,
        g_prefix,
        name
    );
    fprintf(out, "static pin_spec %s_%s_get_inputs[] = { { \"Exec\", PIN_FLOW, NULL, PIN_EXEC } };\n", g_prefix, name);
    fprintf(
        out,
        "static pin_spec %s_%s_get_outputs[] = { { \"Then\", PIN_FLOW, NULL, "
        "PIN_EXEC }, { \"Value\", PIN_STRUCT, \"%s\" } };\n",
        g_prefix,
        name,
        name
    );
    fprintf(
        out,
        "node_desc %s_NODE_Get_%s = { .name = \"Get_%s\", .input_count = 1, "
        ".output_count = 2, .inputs = %s_%s_get_inputs, .outputs = "
        "%s_%s_get_outputs, .flow_kind = NODE_FLOW_CUSTOM, .flow = "
        "__flow_%s_get_%s, .type = &%s_T_%s, .access = &%s_%s_get_access "
        "};\n\n",
        g_prefix,
        name,
        name,
        g_prefix,
        name,
        g_prefix,
        name,
        g_prefix,
        name,
        g_prefix,
        name,
        g_prefix,
        name
    );
    fprintf(
        out,
        "static pin_spec %s_%s_set_inputs[] = { { \"Exec\", PIN_FLOW, NULL, "
        "PIN_EXEC }, { \"Value\", PIN_STRUCT, \"%s\" } };\n",
        g_prefix,
        name,
        name
    );
    fprintf(
        out,
        "static pin_spec %s_%s_set_outputs[] = { { \"Then\", PIN_FLOW, NULL, "
        "PIN_EXEC }, { \"Value\", PIN_STRUCT, \"%s\" } };\n",
        g_prefix,
        name,
        name
    );
    fprintf(
        out,
        "node_desc %s_NODE_Set_%s = { .name = \"Set_%s\", .input_count = 2, "
        ".output_count = 1, .inputs = %s_%s_set_inputs, .outputs = "
        "%s_%s_set_outputs, .flow_kind = NODE_FLOW_CUSTOM, .flow = "
        "__flow_%s_set_%s, .type = &%s_T_%s, .access = &%s_%s_set_access "
        "};\n\n",
        g_prefix,
        name,
        name,
        g_prefix,
        name,
        g_prefix,
        name,
        g_prefix,
        name,
        g_prefix,
        name,
        g_prefix,
        name
    );
    fprintf(
        out,
        "static pin_spec %s_%s_has_outputs[] = { { \"Then\", PIN_FLOW, NULL, "
        "PIN_EXEC }, { \"Value\", PIN_BOOL, \"bool\" } };\n",
        g_prefix,
        name
    );
    fprintf(
        out,
        "node_desc %s_NODE_Has_%s = { .name = \"Has_%s\", .input_count = 1, "
        ".output_count = 2, .inputs = %s_%s_get_inputs, .outputs = "
        "%s_%s_has_outputs, .flow_kind = NODE_FLOW_CUSTOM, .flow = "
        "__flow_%s_has_%s, .access = &%s_%s_has_access };\n\n",
        g_prefix,
        name,
        name,
        g_prefix,
        name,
        g_prefix,
        name,
        g_prefix,
        name,
        g_prefix,
        name
    );
    fprintf(
        out,
        "node_desc %s_NODE_Add_%s = { .name = \"Add_%s\", .input_count = 1, "
        ".output_count = 1, .inputs = %s_%s_get_inputs, .outputs = "
        "%s_%s_set_outputs, .flow_kind = NODE_FLOW_CUSTOM, .flow = "
        "__flow_%s_add_%s, .access = &%s_%s_structural_access };\n",
        g_prefix,
        name,
        name,
        g_prefix,
        name,
        g_prefix,
        name,
        g_prefix,
        name,
        g_prefix,
        name
    );
    fprintf(
        out,
        "node_desc %s_NODE_Remove_%s = { .name = \"Remove_%s\", .input_count = "
        "1, .output_count = 1, .inputs = %s_%s_get_inputs, .outputs = "
        "%s_%s_set_outputs, .flow_kind = NODE_FLOW_CUSTOM, .flow = "
        "__flow_%s_remove_%s, .access = &%s_%s_structural_access };\n\n",
        g_prefix,
        name,
        name,
        g_prefix,
        name,
        g_prefix,
        name,
        g_prefix,
        name,
        g_prefix,
        name
    );
}

static void emit_component_accessors(FILE *out, const struct_info *s)
{
    const char *name = s->name;
    uint64_t id = stable_service_id(name);
    fprintf(
        out,
        "const ecs_component_desc %s_COMPONENT_%s = { .id = UINT64_C(%llu), "
        ".name = \"%s\", .element_size = sizeof(%s), .type = &%s_T_%s, "
        ".layout_hash = UINT64_C(%llu), .index = %s };\n",
        g_prefix,
        name,
        (unsigned long long)id,
        name,
        name,
        g_prefix,
        name,
        (unsigned long long)component_layout_hash(s),
        s->dense ? "ECS_INDEX_DENSE" : "ECS_INDEX_BITSET"
    );
    fprintf(
        out,
        "%s *%s_%s_get(ecs_world *world, ecs_entity entity) { return "
        "(%s *)ecs_component_data(world, entity, %s_COMPONENT_%s.id); }\n",
        name,
        g_prefix,
        name,
        name,
        g_prefix,
        name
    );
    fprintf(
        out,
        "const %s *%s_%s_get_const(const ecs_world *world, ecs_entity entity) "
        "{ return (const %s *)ecs_component_data_const(world, entity, "
        "%s_COMPONENT_%s.id); }\n",
        name,
        g_prefix,
        name,
        name,
        g_prefix,
        name
    );
    fprintf(
        out,
        "int %s_%s_has(const ecs_world *world, ecs_entity entity) { return "
        "ecs_component_has(world, entity, %s_COMPONENT_%s.id); }\n",
        g_prefix,
        name,
        g_prefix,
        name
    );
    fprintf(
        out,
        "int %s_%s_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) "
        "{ return ecs_world_record_add(world, entity, %s_COMPONENT_%s.id, "
        "NULL, "
        "sort_key); }\n",
        g_prefix,
        name,
        g_prefix,
        name
    );
    fprintf(
        out,
        "int %s_%s_remove(ecs_world *world, ecs_entity entity, uint64_t "
        "sort_key) "
        "{ return ecs_world_record_remove(world, entity, %s_COMPONENT_%s.id, "
        "sort_key); }\n\n",
        g_prefix,
        name,
        g_prefix,
        name
    );
}

/* ---------- emission ---------- */

static void emit(FILE *out, const collect_ctx *ctx)
{
    fputs("/* generated reflection pack -- generated by reflect, do not edit. */\n\n", out);
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
    fputs("#include \"core/arena.h\"\n", out);
    fputs("#include \"core/type.h\"\n", out);
    fputs("#include \"core/nodes.h\"\n", out);
    fputs("#include \"core/flow.h\"\n", out);
    fputs("#include \"core/service.h\"\n", out);
    fputs("#include \"core/operator.h\"\n", out);
    fputs("#include \"core/registry.h\"\n", out);
    if (acount(ctx->structs)) {
        int has_component = 0;
        for (int i = 0; i < acount(ctx->structs); i++) {
            has_component |= ctx->structs[i].component;
        }
        if (has_component) {
            fputs("#include \"ecs/ecs.h\"\n", out);
            fputs("#include \"core/instance.h\"\n", out);
            fputs("#include <string.h>\n", out);
        }
    }
    fputs("\n", out);

    for (int i = 0; i < acount(ctx->structs); i++) {
        const struct_info *s = &ctx->structs[i];
        fprintf(out, "static const type_field %s_%s_fields[] = {\n", g_prefix, s->name);
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

        fprintf(out, "const type_desc %s_T_%s = {\n", g_prefix, s->name);
        fprintf(out, "    .name = \"%s\",\n", s->name);
        fprintf(out, "    .size = sizeof(%s),\n", s->name);
        fprintf(out, "    .fields = %s_%s_fields,\n", g_prefix, s->name);
        fprintf(out, "    .field_count = %d,\n", acount(s->fields));
        if (s->alias_of) {
            fprintf(out, "    .alias_of = \"%s\",\n", s->alias_of);
        }
        fputs("};\n\n", out);
    }

    fprintf(out, "const type_desc *const %s_REGISTRY_TYPES[] = {\n", g_prefix);
    for (int i = 0; i < acount(ctx->structs); i++) {
        fprintf(out, "    &%s_T_%s,\n", g_prefix, ctx->structs[i].name);
    }
    fputs("};\n", out);
    fprintf(out, "const int %s_REGISTRY_TYPE_COUNT = %d;\n\n", g_prefix, acount(ctx->structs));

    int component_count = 0;
    for (int i = 0; i < acount(ctx->structs); i++) {
        if (ctx->structs[i].component) {
            emit_component_accessors(out, &ctx->structs[i]);
            component_count++;
        }
    }
    if (component_count) {
        fprintf(out, "const ecs_component_desc *const %s_REGISTRY_COMPONENTS[] = {\n", g_prefix);
        for (int i = 0; i < acount(ctx->structs); i++) {
            if (ctx->structs[i].component) {
                fprintf(
                    out,
                    "    &%s_COMPONENT_%s,\n",
                    g_prefix,
                    ctx->structs[i].name
                );
            }
        }
        fputs("};\n", out);
    } else {
        fprintf(out, "const void *const %s_REGISTRY_COMPONENTS[1] = { NULL };\n", g_prefix);
    }
    fprintf(out, "const int %s_REGISTRY_COMPONENT_COUNT = %d;\n\n", g_prefix, component_count);

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
                        "static const service_desc %s_SERVICE_%d = { \"%s\", "
                        "UINT64_C(%llu) };\n",
                        g_prefix,
                        service_count,
                        ctx->fns[i].pservice_type[j],
                        (unsigned long long)stable_service_id(ctx->fns[i].pservice_type[j])
                    );
                    service_count++;
                }
            }
        }
    }
    fprintf(out, "const service_desc *const %s_REGISTRY_SERVICES[] = {\n", g_prefix);
    for (int i = 0; i < service_count; i++) {
        fprintf(out, "    &%s_SERVICE_%d,\n", g_prefix, i);
    }
    fputs("};\n", out);
    fprintf(out, "const int %s_REGISTRY_SERVICE_COUNT = %d;\n\n", g_prefix, service_count);

    int operator_count = 0;
    for (int i = 0; i < acount(ctx->fns); i++) {
        if (ctx->fns[i].operator_name) {
            operator_count++;
        }
    }
    fprintf(out, "const operator_signature %s_REGISTRY_OPERATORS[] = {\n", g_prefix);
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
    fprintf(out, "const int %s_REGISTRY_OPERATOR_COUNT = %d;\n\n", g_prefix, operator_count);

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
    for (int i = 0; i < acount(ctx->structs); i++) {
        if (ctx->structs[i].component) {
            emit_component_nodes(out, &ctx->structs[i]);
            emit_component_nodes_desc(out, &ctx->structs[i]);
        }
    }

    fprintf(out, "node_desc *const %s_REGISTRY_NODES[] = {\n", g_prefix);
    for (int i = 0; i < acount(ctx->structs); i++) {
        fprintf(out, "    &%s_NODE_Make_%s,\n", g_prefix, ctx->structs[i].name);
        fprintf(out, "    &%s_NODE_Break_%s,\n", g_prefix, ctx->structs[i].name);
    }
    for (int i = 0; i < acount(ctx->fns); i++) {
        fprintf(out, "    &%s_NODE_%s,\n", g_prefix, ctx->fns[i].name);
    }
    for (int i = 0; i < acount(ctx->structs); i++) {
        if (!ctx->structs[i].component) {
            continue;
        }
        fprintf(
            out,
            "    &%s_NODE_Get_%s, &%s_NODE_Set_%s, &%s_NODE_Has_%s,\n",
            g_prefix,
            ctx->structs[i].name,
            g_prefix,
            ctx->structs[i].name,
            g_prefix,
            ctx->structs[i].name
        );
        fprintf(
            out,
            "    &%s_NODE_Add_%s, &%s_NODE_Remove_%s,\n",
            g_prefix,
            ctx->structs[i].name,
            g_prefix,
            ctx->structs[i].name
        );
    }
    fputs("};\n", out);
    int component_node_count = component_count * 5;
    int node_count = acount(ctx->structs) * 2 + acount(ctx->fns) + component_node_count;
    fprintf(out, "const int %s_REGISTRY_NODE_COUNT = %d;\n", g_prefix, node_count);
    fprintf(out, "const registry_pack %s_REGISTRY_PACK = {\n", g_prefix);
    fprintf(out, "    .name = \"%s\",\n", g_prefix);
    fprintf(out, "    .nodes = %s_REGISTRY_NODES,\n", g_prefix);
    fprintf(out, "    .node_count = %d,\n", node_count);
    fprintf(out, "    .types = %s_REGISTRY_TYPES,\n", g_prefix);
    fprintf(out, "    .type_count = %d,\n", acount(ctx->structs));
    fprintf(out, "    .services = %s_REGISTRY_SERVICES,\n", g_prefix);
    fprintf(out, "    .service_count = %d,\n", service_count);
    fprintf(out, "    .operators = %s_REGISTRY_OPERATORS,\n", g_prefix);
    fprintf(out, "    .operator_count = %d,\n", operator_count);
    fputs("};\n", out);
}

/* ---------- main ---------- */

static void set_prefix(const char *source)
{
    const char *base = strrchr(source, '/');
    base = base ? base + 1 : source;
    int length = 0;
    while (base[length] && base[length] != '.' &&
           length < (int)sizeof g_prefix_storage - 1) {
        length++;
    }
    int out = 0;
    for (int i = 0; i < length; i++) {
        char c = base[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            c = '_';
        }
        if (out == 0 && c >= '0' && c <= '9') {
            g_prefix_storage[out++] = '_';
        }
        g_prefix_storage[out++] = c;
    }
    if (out == 0) {
        strcpy(g_prefix_storage, "REFLECTION");
    } else {
        g_prefix_storage[out] = '\0';
    }
    g_prefix = g_prefix_storage;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: reflect <input.c> <output_registry.c> [pack_prefix] [--include path ...]\n");
        return 2;
    }
    const char *input = argv[1];
    const char *output = argv[2];
    int option = 3;
    const char *prefix = output;
    if (option < argc && strcmp(argv[option], "--include") != 0) {
        prefix = argv[option++];
    }
    set_prefix(prefix);

    char srcdir[1024];
    char rootdir[1024];
    parent_dir(input, srcdir, sizeof srcdir);
    parent_dir(srcdir, rootdir, sizeof rootdir);

    CXIndex idx = clang_createIndex(0, 0);

    const char *args[32] = { "-x", "c", "-I", rootdir };
    int nargs = 4;
    while (option < argc) {
        if (option + 1 >= argc || nargs + 2 > 32 ||
            (strcmp(argv[option], "--include") != 0 &&
             strcmp(argv[option], "--sysroot") != 0)) {
            fprintf(stderr, "reflect: invalid include option\n");
            clang_disposeIndex(idx);
            return 2;
        }
        args[nargs++] = strcmp(argv[option], "--sysroot") == 0 ? "-isysroot" : "-I";
        args[nargs++] = argv[option + 1];
        option += 2;
    }

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
