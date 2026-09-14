#include "reflect_internal.h"

#include <stdlib.h>
#include <string.h>

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

static char *cxstr(CXString s)
{
    const char *c = clang_getCString(s);
    char *r = strdup(c ? c : "");
    clang_disposeString(s);
    return r;
}

/* parent_dir: buf = directory containing path (dirname semantics). */
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

int ends_with(const char *s, const char *suffix)
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
    f.handle = annotation_value(c, "node:handle:");
    f.pin = pin_name(t);
    if (f.handle) {
        if (!f.pin || strcmp(f.pin, "PIN_U64") || !f.handle[0]) {
            reflect_error(c, "handle fields require uint64_t storage and a nominal type");
            free(f.name);
            free(f.spelling);
            free(f.handle);
            return CXChildVisit_Continue;
        }
        f.pin = "PIN_HANDLE";
    }
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
        free(f.handle);
        return CXChildVisit_Continue;
    }
    if (clang_Cursor_isBitField(c)) {
        reflect_error(c, "bit-fields are unsupported");
        free(f.name);
        free(f.spelling);
        free(f.handle);
        return CXChildVisit_Continue;
    }
    if (!strcmp(f.pin, "PIN_ENUM") &&
        clang_Type_getSizeOf(clang_getCanonicalType(t)) != sizeof(int32_t)) {
        reflect_error(c, "enum fields must use a 32-bit representation");
        free(f.name);
        free(f.spelling);
        free(f.handle);
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
    char *label = annotation_value(c, "node:pin-name:");
    if (output && raw.kind != CXType_Pointer) {
        reflect_error(c, "explicit output parameters must be pointers");
        free(service);
        free(label);
        return CXChildVisit_Continue;
    }
    CXType graph_type = output && raw.kind == CXType_Pointer
                          ? clang_getPointeeType(raw)
                          : raw;
    const char *pin = pin_name(graph_type);
    if ((!pin && !service) ||
        (!strcmp(pin ? pin : "", "PIN_ENUM") &&
         clang_Type_getSizeOf(clang_getCanonicalType(graph_type)) != sizeof(int32_t))) {
        reflect_error(c, "unsupported parameter type or missing explicit output annotation");
        free(service);
        free(label);
        return CXChildVisit_Continue;
    }
    apush(f->pname, cxstr(clang_getCursorSpelling(c)));
    apush(f->plabel, label);
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
    clang_getSpellingLocation(clang_getCursorLocation(c), NULL, &s.line, NULL, NULL);
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
            clang_getSpellingLocation(clang_getCursorLocation(c), NULL, &s.line, NULL, NULL);
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
        int read = has_annotate(c, "node:read");
        int exec = has_annotate(c, "node:exec");
        if (pure || read || exec) {
            if (pure + read + exec > 1) {
                reflect_error(c, "a function must choose exactly one node contract");
                return CXChildVisit_Recurse;
            }
            fn_info f = { 0 };
            f.name = cxstr(clang_getCursorSpelling(c));
            f.ret = cxstr(clang_getTypeSpelling(clang_getCursorResultType(c)));
            f.ret_void = clang_getCursorResultType(c).kind == CXType_Void;
            f.ret_pin = f.ret_void ? NULL : pin_name(clang_getCursorResultType(c));
            f.read = read;
            f.exec = exec;
            if (read) {
                f.effect_flags |= 1u << 3;
            }
            CXType result_type = clang_getCursorResultType(c);
            int read_error = result_type.kind != CXType_Pointer ||
                             !clang_isConstQualifiedType(clang_getPointeeType(result_type));
            if (read &&
                (read_error || !f.ret_pin || strcmp(f.ret_pin, "PIN_STR") != 0)) {
                reflect_error(c, "read nodes must return const char * errors");
                free(f.name);
                free(f.ret);
                return CXChildVisit_Recurse;
            }
            if (!read && !f.ret_void &&
                (!f.ret_pin || (!strcmp(f.ret_pin, "PIN_ENUM") &&
                                clang_Type_getSizeOf(clang_getCanonicalType(
                                    clang_getCursorResultType(c)
                                )) != sizeof(int32_t)))) {
                reflect_error(c, "unsupported return type");
                free(f.name);
                free(f.ret);
                return CXChildVisit_Recurse;
            }
            f.display_name = annotation_value(c, "node:name:");
            f.category = annotation_value(c, "node:category:");
            f.keywords = annotation_value(c, "node:keywords:");
            f.operator_name = annotation_value(c, "node:operator:");
            f.jit_kind = annotation_value(c, "node:jit:");
            if (f.jit_kind && strcmp(f.jit_kind, "v2-scale") != 0 &&
                strcmp(f.jit_kind, "v2-add") != 0 &&
                strcmp(f.jit_kind, "v2-move") != 0 &&
                strcmp(f.jit_kind, "identity") != 0) {
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
            if (f.jit_kind && !strcmp(f.jit_kind, "identity") &&
                (!pure || !f.ret_pin || strcmp(f.ret_pin, "PIN_STRUCT") ||
                 acount(f.pname) != 1 || f.pout[0] || f.pservice[0] ||
                 !clang_equalTypes(
                     clang_getCanonicalType(result_type),
                     clang_getCanonicalType(clang_getCursorType(clang_Cursor_getArgument(c, 0)))
                 ))) {
                reflect_error(c, "identity requires one struct argument with the same underlying return type");
            }
            if (read) {
                int output_count = 0;
                for (int i = 0; i < acount(f.pname); i++) {
                    output_count += f.pout[i];
                    if (f.pservice[i]) {
                        reflect_error(c, "read nodes cannot use service parameters");
                    }
                }
                if (!output_count) {
                    reflect_error(c, "read nodes need at least one NODE_OUT parameter");
                }
            }
            apush(ctx->fns, f);
        }
    }
    return CXChildVisit_Recurse;
}

/* ---------- emission helpers ---------- */

void reflect_collect(CXTranslationUnit tu, collect_ctx *ctx)
{
    clang_visitChildren(clang_getTranslationUnitCursor(tu), visitor, ctx);
}

static void free_strings(char **strings)
{
    for (int i = 0; i < acount(strings); i++) {
        free(strings[i]);
    }
    afree(strings);
}

static void dispose_fields(field_info *fields)
{
    for (int i = 0; i < acount(fields); i++) {
        free(fields[i].name);
        free(fields[i].spelling);
        free(fields[i].handle);
    }
    afree(fields);
}

void reflect_dispose(collect_ctx *ctx)
{
    for (int i = 0; i < acount(ctx->structs); i++) {
        struct_info *s = &ctx->structs[i];
        free(s->name);
        free(s->file);
        free(s->alias_of);
        dispose_fields(s->fields);
    }
    for (int i = 0; i < acount(ctx->fns); i++) {
        fn_info *f = &ctx->fns[i];
        free(f->name);
        free(f->ret);
        free_strings(f->pname);
        free_strings(f->plabel);
        free_strings(f->ptype);
        afree(f->ppin);
        afree(f->pout);
        afree(f->pservice);
        free_strings(f->pservice_type);
        free_strings(f->pout_type);
        free(f->display_name);
        free(f->category);
        free(f->keywords);
        free_strings(f->reads);
        free_strings(f->writes);
        free(f->operator_name);
        free(f->jit_kind);
        free(f->conversion_from);
        free(f->conversion_to);
    }
    afree(ctx->structs);
    afree(ctx->fns);
    g_collection = NULL;
}
