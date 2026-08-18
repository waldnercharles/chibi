/* reflect/reflect.c -- build-time libclang reflection tool.
 *
 * Parses an annotated C source file and emits generated/registry.c:
 * one type_desc per "node:type" struct (real sizeof/offsetof), and one
 * Make + Break node_desc per struct plus a call node_desc per "node:fn"
 * function.
 */
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
    field_info *v;
    int n, cap;
} field_list;

typedef struct
{
    char *name;
    char *file; /* header basename where the definition lives */
    field_list fields;
} struct_info;

typedef struct
{
    struct_info *v;
    int n, cap;
} struct_list;

typedef struct
{
    char *name;
    char *ret;             /* return type spelling */
    const char *ret_pin;   /* pin_type enum name for the return */
    char **pname, **ptype; /* param names / type spellings */
    const char **ppin;     /* pin_type enum names per param */
    int nparams, cap;
} fn_info;

typedef struct
{
    fn_info *v;
    int n, cap;
} fn_list;

typedef struct
{
    struct_list structs;
    fn_list fns;
} collect_ctx;

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n);
    if (!q) {
        fprintf(stderr, "reflect: out of memory\n");
        exit(1);
    }
    return q;
}

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
    if (n >= bufsz) n = bufsz - 1;
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

static void field_push(field_list *l, field_info f)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->v = xrealloc(l->v, (size_t)l->cap * sizeof *l->v);
    }
    l->v[l->n++] = f;
}

static void struct_push(struct_list *l, struct_info s)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->v = xrealloc(l->v, (size_t)l->cap * sizeof *l->v);
    }
    l->v[l->n++] = s;
}

static void fn_push(fn_list *l, fn_info f)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->v = xrealloc(l->v, (size_t)l->cap * sizeof *l->v);
    }
    l->v[l->n++] = f;
}

static void fn_add_param(fn_info *f, char *name, char *type, const char *pin)
{
    if (f->nparams == f->cap) {
        f->cap = f->cap ? f->cap * 2 : 4;
        f->pname = xrealloc(f->pname, (size_t)f->cap * sizeof *f->pname);
        f->ptype = xrealloc(f->ptype, (size_t)f->cap * sizeof *f->ptype);
        f->ppin = xrealloc(f->ppin, (size_t)f->cap * sizeof *f->ppin);
    }
    f->pname[f->nparams] = name;
    f->ptype[f->nparams] = type;
    f->ppin[f->nparams] = pin;
    f->nparams++;
}

/* ---------- annotation detection ---------- */

typedef struct
{
    const char *want;
    int found;
} ann_ctx;

static enum CXChildVisitResult find_annotate(CXCursor c, CXCursor parent, CXClientData data)
{
    (void)parent;
    ann_ctx *ctx = (ann_ctx *)data;
    if (clang_getCursorKind(c) == CXCursor_AnnotateAttr) {
        CXString s = clang_getCursorSpelling(c);
        if (strcmp(clang_getCString(s), ctx->want) == 0) ctx->found = 1;
        clang_disposeString(s);
        if (ctx->found) return CXChildVisit_Break;
    }
    return CXChildVisit_Continue;
}

static int has_annotate(CXCursor c, const char *want)
{
    ann_ctx ctx = { want, 0 };
    clang_visitChildren(c, find_annotate, &ctx);
    return ctx.found;
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
                pointee.kind == CXType_Char_S || pointee.kind == CXType_SChar)
                return "PIN_STR";
            return NULL;
        }
        default: return NULL;
    }
}

/* ---------- collection ---------- */

static enum CXChildVisitResult field_visitor(CXCursor c, CXCursor parent, CXClientData data)
{
    (void)parent;
    if (clang_getCursorKind(c) != CXCursor_FieldDecl)
        return CXChildVisit_Continue;
    field_list *l = (field_list *)data;
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
    field_push(l, f);
    return CXChildVisit_Continue;
}

static enum CXChildVisitResult param_visitor(CXCursor c, CXCursor parent, CXClientData data)
{
    (void)parent;
    if (clang_getCursorKind(c) != CXCursor_ParmDecl)
        return CXChildVisit_Continue;
    fn_info *f = (fn_info *)data;
    CXType t = clang_getCursorType(c);
    const char *pin = pin_name(t);
    if (!pin || (!strcmp(pin, "PIN_ENUM") &&
                 clang_Type_getSizeOf(clang_getCanonicalType(t)) != sizeof(int32_t))) {
        reflect_error(c, "unsupported parameter type");
        return CXChildVisit_Continue;
    }
    fn_add_param(f, cxstr(clang_getCursorSpelling(c)), cxstr(clang_getTypeSpelling(t)), pin);
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
            s.fields.v = NULL;
            s.fields.n = 0;
            s.fields.cap = 0;
            clang_visitChildren(c, field_visitor, &s.fields);
            if (g_failed) {
                free(s.name);
                free(s.file);
                free(s.fields.v);
            } else {
                struct_push(&ctx->structs, s);
            }
        }
    } else if (k == CXCursor_FunctionDecl) {
        if (has_annotate(c, "node:fn")) {
            fn_info f;
            f.name = cxstr(clang_getCursorSpelling(c));
            f.ret = cxstr(clang_getTypeSpelling(clang_getCursorResultType(c)));
            f.ret_pin = pin_name(clang_getCursorResultType(c));
            if (!f.ret_pin || (!strcmp(f.ret_pin, "PIN_ENUM") &&
                               clang_Type_getSizeOf(clang_getCanonicalType(
                                   clang_getCursorResultType(c)
                               )) != sizeof(int32_t))) {
                reflect_error(c, "unsupported return type");
                free(f.name);
                free(f.ret);
                return CXChildVisit_Recurse;
            }
            f.pname = NULL;
            f.ptype = NULL;
            f.ppin = NULL;
            f.nparams = 0;
            f.cap = 0;
            clang_visitChildren(c, param_visitor, &f);
            fn_push(&ctx->fns, f);
        }
    }
    return CXChildVisit_Recurse;
}

/* ---------- emission helpers ---------- */

static const char *pin_member(const char *pin)
{
    if (!strcmp(pin, "PIN_BOOL")) return "b";
    if (!strcmp(pin, "PIN_I8")) return "i8";
    if (!strcmp(pin, "PIN_U8")) return "u8";
    if (!strcmp(pin, "PIN_I16")) return "i16";
    if (!strcmp(pin, "PIN_U16")) return "u16";
    if (!strcmp(pin, "PIN_I32")) return "i";
    if (!strcmp(pin, "PIN_U32")) return "u";
    if (!strcmp(pin, "PIN_I64")) return "i64";
    if (!strcmp(pin, "PIN_U64")) return "u64";
    if (!strcmp(pin, "PIN_F32")) return "f";
    if (!strcmp(pin, "PIN_F64")) return "d";
    if (!strcmp(pin, "PIN_STR")) return "str";
    if (!strcmp(pin, "PIN_ENUM")) return "e";
    return "ptr"; /* PIN_STRUCT / PIN_FLOW */
}

static void emit_pin_spec(FILE *out, const char *name, const char *pin, const char *spelling)
{ fprintf(out, "    { \"%s\", %s, \"%s\" },\n", name, pin, spelling); }

/* Emit a C expression that reads the value for param idx out of pin_value. */
static void emit_arg(FILE *out, const char *spelling, const char *pin, int idx)
{
    if (!strcmp(pin, "PIN_STRUCT"))
        fprintf(out, "*(%s *)args[%d].ptr", spelling, idx);
    else fprintf(out, "args[%d].%s", idx, pin_member(pin));
}

/* Emit the uniform-signature wrapper that adapts a native fn to node_fn. */
static void emit_fn(FILE *out, const fn_info *f)
{
    fprintf(out, "static pin_value __wrap_%s(const pin_value *args, int nargs, arena *a) {\n", f->name);
    fprintf(out, "    (void)nargs;\n");
    int struct_ret = !strcmp(f->ret_pin, "PIN_STRUCT");
    if (!struct_ret) fprintf(out, "    (void)a;\n");
    fprintf(out, "    pin_value r = { 0 };\n");
    fprintf(out, "    r.type = %s;\n", f->ret_pin);
    fprintf(out, "    r.type_name = \"%s\";\n", f->ret);

    if (struct_ret) {
        fprintf(out, "    %s __r = %s(", f->ret, f->name);
        for (int j = 0; j < f->nparams; j++) {
            if (j) fputs(", ", out);
            emit_arg(out, f->ptype[j], f->ppin[j], j);
        }
        fputs(");\n", out);
        fprintf(out, "    r.ptr = arena_alloc(a, sizeof(%s));\n", f->ret);
        fputs("    if (!r.ptr) return r;\n", out);
        fprintf(out, "    *(%s *)r.ptr = __r;\n", f->ret);
    } else {
        fprintf(out, "    r.%s = %s(", pin_member(f->ret_pin), f->name);
        for (int j = 0; j < f->nparams; j++) {
            if (j) fputs(", ", out);
            emit_arg(out, f->ptype[j], f->ppin[j], j);
        }
        fputs(");\n", out);
    }
    fputs("    return r;\n}\n\n", out);
}

/* Emit Make + Break node_desc for one struct. */
static void emit_struct_nodes(FILE *out, const struct_info *s)
{
    fprintf(out, "static pin_spec %s_make_inputs[] = {\n", s->name);
    for (int j = 0; j < s->fields.n; j++)
        emit_pin_spec(
            out,
            s->fields.v[j].name,
            s->fields.v[j].pin,
            s->fields.v[j].spelling
        );
    fputs("};\n", out);
    fprintf(out, "static pin_spec %s_make_outputs[] = {\n", s->name);
    fprintf(out, "    { \"%s\", PIN_STRUCT, \"%s\" },\n", s->name, s->name);
    fputs("};\n", out);
    fprintf(out, "node_desc NODE_Make_%s = {\n", s->name);
    fprintf(out, "    .name = \"Make_%s\",\n", s->name);
    fprintf(out, "    .input_count = %d,\n", s->fields.n);
    fprintf(out, "    .output_count = 1,\n");
    fprintf(out, "    .inputs = %s_make_inputs,\n", s->name);
    fprintf(out, "    .outputs = %s_make_outputs,\n", s->name);
    fprintf(out, "    .eval = node_eval_make,\n");
    fprintf(out, "    .type = &T_%s,\n", s->name);
    fprintf(out, "    .flow_kind = NODE_FLOW_NONE,\n");
    fputs("};\n\n", out);

    fprintf(out, "static pin_spec %s_break_inputs[] = {\n", s->name);
    fprintf(out, "    { \"%s\", PIN_STRUCT, \"%s\" },\n", s->name, s->name);
    fputs("};\n", out);
    fprintf(out, "static pin_spec %s_break_outputs[] = {\n", s->name);
    for (int j = 0; j < s->fields.n; j++)
        emit_pin_spec(
            out,
            s->fields.v[j].name,
            s->fields.v[j].pin,
            s->fields.v[j].spelling
        );
    fputs("};\n", out);
    fprintf(out, "node_desc NODE_Break_%s = {\n", s->name);
    fprintf(out, "    .name = \"Break_%s\",\n", s->name);
    fprintf(out, "    .input_count = 1,\n");
    fprintf(out, "    .output_count = %d,\n", s->fields.n);
    fprintf(out, "    .inputs = %s_break_inputs,\n", s->name);
    fprintf(out, "    .outputs = %s_break_outputs,\n", s->name);
    fprintf(out, "    .eval = node_eval_break,\n");
    fprintf(out, "    .type = &T_%s,\n", s->name);
    fprintf(out, "    .flow_kind = NODE_FLOW_NONE,\n");
    fputs("};\n\n", out);
}

/* Emit the call node_desc for one function. */
static void emit_fn_node(FILE *out, const fn_info *f)
{
    if (f->nparams == 0)
        fprintf(out, "static pin_spec %s_inputs[1];\n", f->name);
    else {
        fprintf(out, "static pin_spec %s_inputs[] = {\n", f->name);
        for (int j = 0; j < f->nparams; j++)
            emit_pin_spec(out, f->pname[j], f->ppin[j], f->ptype[j]);
        fputs("};\n", out);
    }
    fprintf(out, "static pin_spec %s_outputs[] = {\n", f->name);
    fprintf(out, "    { \"return\", %s, \"%s\" },\n", f->ret_pin, f->ret);
    fputs("};\n", out);
    fprintf(out, "node_desc NODE_%s = {\n", f->name);
    fprintf(out, "    .name = \"%s\",\n", f->name);
    fprintf(out, "    .input_count = %d,\n", f->nparams);
    fprintf(out, "    .output_count = 1,\n");
    fprintf(out, "    .inputs = %s_inputs,\n", f->name);
    fprintf(out, "    .outputs = %s_outputs,\n", f->name);
    fprintf(out, "    .eval = node_eval_call,\n");
    fprintf(out, "    .fn = __wrap_%s,\n", f->name);
    fprintf(out, "    .flow_kind = NODE_FLOW_NONE,\n");
    fputs("};\n\n", out);
}

/* ---------- emission ---------- */

static void emit(FILE *out, const collect_ctx *ctx)
{
    fputs("/* generated/registry.c -- generated by reflect, do not edit. */\n\n", out);
    fputs("#include <stddef.h>\n", out);
    for (int i = 0; i < ctx->structs.n; i++) {
        const char *f = ctx->structs.v[i].file;
        if (!f || !ends_with(f, ".h")) continue;
        int dup = 0;
        for (int j = 0; j < i; j++) {
            const char *g = ctx->structs.v[j].file;
            if (g && strcmp(g, f) == 0) dup = 1;
        }
        if (!dup) fprintf(out, "#include \"%s\"\n", f);
    }
    fputs("#include \"core/type.h\"\n", out);
    fputs("#include \"core/nodes.h\"\n\n", out);

    for (int i = 0; i < ctx->structs.n; i++) {
        const struct_info *s = &ctx->structs.v[i];
        fprintf(out, "static const type_field %s_fields[] = {\n", s->name);
        for (int j = 0; j < s->fields.n; j++) {
            const field_info *f = &s->fields.v[j];
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
        fprintf(out, "    .field_count = %d,\n", s->fields.n);
        fputs("};\n\n", out);
    }

    fputs("const type_desc *const REGISTRY_TYPES[] = {\n", out);
    for (int i = 0; i < ctx->structs.n; i++)
        fprintf(out, "    &T_%s,\n", ctx->structs.v[i].name);
    fputs("};\n", out);
    fprintf(out, "const int REGISTRY_TYPE_COUNT = %d;\n\n", ctx->structs.n);

    /* native prototypes, so wrappers can call the annotated functions */
    for (int i = 0; i < ctx->fns.n; i++) {
        const fn_info *f = &ctx->fns.v[i];
        fprintf(out, "%s %s(", f->ret, f->name);
        for (int j = 0; j < f->nparams; j++)
            fprintf(out, "%s%s %s", j ? ", " : "", f->ptype[j], f->pname[j]);
        fputs(");\n", out);
    }
    if (ctx->fns.n) fputc('\n', out);

    for (int i = 0; i < ctx->fns.n; i++) emit_fn(out, &ctx->fns.v[i]);

    for (int i = 0; i < ctx->structs.n; i++)
        emit_struct_nodes(out, &ctx->structs.v[i]);
    for (int i = 0; i < ctx->fns.n; i++) emit_fn_node(out, &ctx->fns.v[i]);

    fputs("node_desc *const REGISTRY_NODES[] = {\n", out);
    for (int i = 0; i < ctx->structs.n; i++) {
        fprintf(out, "    &NODE_Make_%s,\n", ctx->structs.v[i].name);
        fprintf(out, "    &NODE_Break_%s,\n", ctx->structs.v[i].name);
    }
    for (int i = 0; i < ctx->fns.n; i++)
        fprintf(out, "    &NODE_%s,\n", ctx->fns.v[i].name);
    fputs("};\n", out);
    fprintf(
        out,
        "const int REGISTRY_NODE_COUNT = %d;\n",
        ctx->structs.n * 2 + ctx->fns.n
    );
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
        if (clang_getDiagnosticSeverity(d) >= CXDiagnostic_Error) g_failed = 1;
        CXString s = clang_formatDiagnostic(d, clang_defaultDiagnosticDisplayOptions());
        fprintf(stderr, "reflect: %s\n", clang_getCString(s));
        clang_disposeString(s);
        clang_disposeDiagnostic(d);
    }

    collect_ctx ctx = { { NULL, 0, 0 }, { NULL, 0, 0 } };
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

    printf(
        "reflect: %d struct(s), %d function(s) -> %s\n",
        ctx.structs.n,
        ctx.fns.n,
        output
    );
    for (int i = 0; i < ctx.structs.n; i++)
        printf(
            "reflect:   struct %s (%d fields)\n",
            ctx.structs.v[i].name,
            ctx.structs.v[i].fields.n
        );
    for (int i = 0; i < ctx.fns.n; i++) {
        const fn_info *f = &ctx.fns.v[i];
        printf("reflect:   fn %s(", f->name);
        for (int j = 0; j < f->nparams; j++)
            printf("%s%s %s", j ? ", " : "", f->ptype[j], f->pname[j]);
        printf(") -> %s\n", f->ret);
    }

    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(idx);
    return 0;
}
