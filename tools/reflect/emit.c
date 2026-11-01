#include "reflect_internal.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>

/* A C string literal, or NULL. */
void emit_string(FILE *out, const char *text)
{
    if (!text) {
        fputs("NULL", out);
        return;
    }
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', out);
        }
        if (*p < 32 || *p >= 127) {
            fprintf(out, "\\%03o", *p);
        } else {
            fputc(*p, out);
        }
    }
    fputc('"', out);
}

void emit_function_presentation(FILE *out, const fn_info *f)
{
    const char *names[] = { "category", "keywords", "description" };
    const char *values[] = { f->category, f->keywords, f->description };
    for (int i = 0; i < 3; i++) {
        if (values[i]) {
            fprintf(out, "    .%s = ", names[i]);
            emit_string(out, values[i]);
            fputs(",\n", out);
        }
    }
}
void emit_struct_presentation(
    FILE *out,
    const struct_info *s,
    const field_info *member,
    const char *format,
    const char *category,
    bool field
)
{
    size_t size = strlen(s->name) + (member ? strlen(member->name) : 0) + 256;
    char *description = xmalloc(size);
    if (!strcmp(format, REFLECT_DESC_UNWRAP)) {
        snprintf(description, size, format, member->name, s->name);
    } else {
        snprintf(description, size, format, s->name, member ? member->name : "");
    }
    fputs(", .description = ", out);
    emit_string(out, description);
    fputs(", .category = ", out);
    if (field) {
        snprintf(description, size, "Components/%s", s->name);
        emit_string(out, description);
        snprintf(description, size, "set field %s target self", member->name);
        fputs(", .keywords = ", out);
        emit_string(out, description);
        free(description);
        return;
    }
    emit_string(out, category ? category : "");
    free(description);
}

typedef struct
{
    int services;
} reflection_counts;

static reflection_counts emit_reflection(FILE *out, const collect_ctx *ctx)
{
    for (int i = 0; i < acount(ctx->structs); i++) {
        const struct_info *s = &ctx->structs[i];
        fprintf(out, "const type_field %s%s_fields[] = {\n", symbol_prefix(), struct_c_name(s));
        for (int j = 0; j < acount(s->fields); j++) {
            const field_info *f = &s->fields[j];
            fprintf(
                out,
                "    { \"%s\", %s, offsetof(%s, %s), sizeof(%s), \"%s\", ",
                f->name,
                f->pin,
                struct_c_name(s),
                f->name,
                f->spelling,
                field_type_name(f)
            );
            emit_string(out, f->description);
            fprintf(out, ", %s, false, ", f->color ? "true" : "false");
            emit_string(out, f->unit);
            fprintf(
                out,
                ", %.17g, %.17g, %s, %.17g, NULL, PIN_CONTAINER_SINGLE },\n",
                f->min,
                f->max,
                f->range_contract ? "true" : "false",
                f->range_default
            );
        }
        fputs("};\n\n", out);

        fprintf(out, "const type_desc %sT_%s = {\n", symbol_prefix(), struct_c_name(s));
        fprintf(out, "    .name = \"%s\",\n", s->name);
        fprintf(out, "    .size = sizeof(%s),\n", struct_c_name(s));
        fprintf(out, "    .fields = %s%s_fields,\n", symbol_prefix(), struct_c_name(s));
        fprintf(out, "    .field_count = %d,\n", acount(s->fields));
        if (s->alias_of) {
            fprintf(out, "    .alias_of = \"%s\",\n", authored_type(s->alias_of));
        }
        fputs("};\n\n", out);
    }

    fprintf(out, "const type_desc *const %sREGISTRY_TYPES[] = {\n", symbol_prefix());
    for (int i = 0; i < acount(ctx->structs); i++) {
        fprintf(out, "    &%sT_%s,\n", symbol_prefix(), struct_c_name(&ctx->structs[i]));
    }
    fputs("};\n", out);
    fprintf(out, "const int %sREGISTRY_TYPE_COUNT = %d;\n\n", symbol_prefix(), acount(ctx->structs));

    for (int i = 0; i < acount(ctx->enums); i++) {
        const enum_info *enumeration = &ctx->enums[i];
        fprintf(
            out,
            "static const enum_value_desc %sENUM_%s_VALUES[] = {\n",
            symbol_prefix(),
            enumeration->name
        );
        for (int j = 0; j < acount(enumeration->values); j++) {
            const enum_value_info *value = &enumeration->values[j];
            fprintf(
                out,
                "    { %d, \"%s\", \"%s\" },\n",
                value->value,
                value->name,
                value->label
            );
        }
        fprintf(out, "};\n");
        fprintf(
            out,
            "const enum_desc %sENUM_%s = {\n"
            "    .name = \"%s\",\n"
            "    .values = %sENUM_%s_VALUES,\n"
            "    .value_count = %d,\n"
            "};\n\n",
            symbol_prefix(),
            enumeration->name,
            enumeration->name,
            symbol_prefix(),
            enumeration->name,
            acount(enumeration->values)
        );
    }
    if (acount(ctx->enums)) {
        fprintf(out, "const enum_desc *const %sREGISTRY_ENUMS[] = {\n", symbol_prefix());
        for (int i = 0; i < acount(ctx->enums); i++) {
            fprintf(out, "    &%sENUM_%s,\n", symbol_prefix(), ctx->enums[i].name);
        }
        fputs("};\n", out);
    } else {
        fprintf(out, "const enum_desc *const %sREGISTRY_ENUMS[1] = { NULL };\n", symbol_prefix());
    }
    fprintf(out, "const int %sREGISTRY_ENUM_COUNT = %d;\n\n", symbol_prefix(), acount(ctx->enums));

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
                        "static const service_desc %sSERVICE_%d = { \"%s\", "
                        "UINT64_C(%llu) };\n",
                        symbol_prefix(),
                        service_count,
                        ctx->fns[i].pservice_type[j],
                        (unsigned long long)stable_service_id(ctx->fns[i].pservice_type[j])
                    );
                    service_count++;
                }
            }
        }
    }
    fprintf(out, "const service_desc *const %sREGISTRY_SERVICES[] = {\n", symbol_prefix());
    for (int i = 0; i < service_count; i++) {
        fprintf(out, "    &%sSERVICE_%d,\n", symbol_prefix(), i);
    }
    fputs("};\n", out);
    fprintf(out, "const int %sREGISTRY_SERVICE_COUNT = %d;\n\n", symbol_prefix(), service_count);

    fprintf(
        out,
        "const type_catalog_pack %sTYPE_PACK = { %sREGISTRY_TYPES, %d, "
        "%sREGISTRY_ENUMS, %d };\n\n",
        symbol_prefix(),
        symbol_prefix(),
        acount(ctx->structs),
        symbol_prefix(),
        acount(ctx->enums)
    );
    return (reflection_counts){ service_count };
}

static reflection_counts declare_reflection(FILE *out, const collect_ctx *ctx)
{
    int service_count = 0;
    for (int i = 0; i < acount(ctx->structs); i++) {
        const struct_info *s = &ctx->structs[i];
        fprintf(out, "extern const type_desc %sT_%s;\n", symbol_prefix(), struct_c_name(s));
        fprintf(out, "extern const type_field %s%s_fields[];\n", symbol_prefix(), struct_c_name(s));
        if (!s->component) {
            continue;
        }
        if (component_unwrap(s)) {
            fprintf(out, "extern const node_host_call %sUnwrap_%s_execution;\n", symbol_prefix(), s->name);
        }
    }
    const char **services = NULL;
    for (int i = 0; i < acount(ctx->fns); i++) {
        const fn_info *f = &ctx->fns[i];
        for (int j = 0; j < acount(f->pname); j++) {
            if (!f->pservice[j] || !f->pservice_type[j]) {
                continue;
            }
            bool found = false;
            for (int k = 0; k < acount(services); k++) {
                found |= !strcmp(services[k], f->pservice_type[j]);
            }
            if (!found) {
                apush(services, f->pservice_type[j]);
            }
        }
    }
    service_count = acount(services);
    afree(services);
    fprintf(out, "extern const type_desc *const %sREGISTRY_TYPES[];\n", symbol_prefix());
    fprintf(out, "extern const enum_desc *const %sREGISTRY_ENUMS[];\n", symbol_prefix());
    for (int i = 0; i < acount(ctx->enums); i++) {
        fprintf(
            out,
            "extern const enum_desc %sENUM_%s;\n",
            symbol_prefix(),
            ctx->enums[i].name
        );
    }
    fprintf(out, "extern const service_desc *const %sREGISTRY_SERVICES[];\n\n", symbol_prefix());
    return (reflection_counts){ service_count };
}

static void emit_native_header(FILE *out, const collect_ctx *ctx, bool capabilities)
{
    fputs("/* generated native declarations and execution capabilities */\n", out);
    fputs("#include <stddef.h>\n#include <stdint.h>\n#include <string.h>\n#include <assert.h>\n", out);
    for (int i = 0; i < acount(ctx->structs); i++) {
        const char *file = ctx->structs[i].file;
        if (!file || !ends_with(file, ".h")) {
            continue;
        }
        bool duplicate = false;
        for (int j = 0; j < i; j++) {
            duplicate |= ctx->structs[j].file && !strcmp(ctx->structs[j].file, file);
        }
        if (!duplicate) {
            fprintf(out, "#include \"%s\"\n", !*g_prefix && !strcmp(file, "components.h") ? "runtime/components.h" : file);
        }
    }
    fputs("#include \"types/type.h\"\n#include \"types/text.h\"\n#include \"types/catalog.h\"\n#include \"execution/service.h\"\n", out);
    if (capabilities) {
        fputs("#include \"support/arena.h\"\n#include \"support/array.h\"\n", out);
        fputs("#include \"types/array.h\"\n#include \"execution/function.h\"\n", out);
    }
    if (capabilities) {
        fputs("#include \"runtime/native_capabilities.h\"\n", out);
    }
    fputc('\n', out);
}

void emit_types(FILE *out, const collect_ctx *ctx)
{
    emit_native_header(out, ctx, false);
    emit_reflection(out, ctx);
}

void emit_runtime(FILE *out, const collect_ctx *ctx)
{
    emit_native_header(out, ctx, true);
    declare_reflection(out, ctx);
    for (int i = 0; i < acount(ctx->fns); i++) {
        const fn_info *f = &ctx->fns[i];
        fprintf(out, "%s %s(", f->ret, f->name);
        for (int j = 0; j < acount(f->pname); j++) {
            fprintf(out, "%s%s %s", j ? ", " : "", f->ptype[j], f->pname[j]);
        }
        fputs(");\n", out);
    }
    for (int i = 0; i < acount(ctx->fns); i++) {
        emit_execution_host(out, &ctx->fns[i], true);
    }
    for (int i = 0; i < acount(ctx->structs); i++) {
        const struct_info *s = &ctx->structs[i];
        if (!component_unwrap(s)) {
            continue;
        }
        emit_execution_unwrap(out, s);
    }
    emit_native_catalog(out, ctx);
}
