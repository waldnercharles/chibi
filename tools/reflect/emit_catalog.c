#include "capabilities/catalog.h"
#include "execution/host.h"
#include "reflect_internal.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static char *join(const char *a, const char *b, const char *c)
{
    size_t size = strlen(a) + strlen(b) + strlen(c) + 1;
    char *text = xmalloc(size);
    snprintf(text, size, "%s%s%s", a, b, c);
    return text;
}
static uint64_t nominal(const char *pin, const char *name)
{
    return name && (!strcmp(pin, "PIN_STRUCT") || !strcmp(pin, "PIN_HANDLE") ||
                    !strcmp(pin, "PIN_ENUM"))
             ? script_catalog_id("type", name)
             : 0;
}
static void add_handle(const collect_ctx *ctx, const char ***names, const char *name)
{
    if (!name || !strcmp(name, "ecs.entity")) {
        return;
    }
    for (int i = 0; i < acount(ctx->structs); ++i) {
        if (!strcmp(ctx->structs[i].name, name)) {
            return;
        }
    }
    for (int i = 0; i < acount(*names); ++i) {
        if (!strcmp((*names)[i], name)) {
            return;
        }
    }
    apush(*names, name);
}
static const char **handles(const collect_ctx *ctx)
{
    const char **names = NULL;
    for (int i = 0; i < acount(ctx->fns); ++i) {
        const fn_info *f = &ctx->fns[i];
        if (f->ret_pin && !strcmp(f->ret_pin, "PIN_HANDLE")) {
            add_handle(ctx, &names, f->ret);
        }
        for (int j = 0; j < acount(f->pname); ++j) {
            if (f->pservice[j]) {
                add_handle(ctx, &names, f->pservice_type[j]);
            } else if (!strcmp(f->ppin[j], "PIN_HANDLE")) {
                add_handle(ctx, &names, f->pout[j] ? f->pout_type[j] : f->ptype[j]);
            }
        }
    }
    for (int i = 0; i < acount(ctx->structs); ++i) {
        for (int j = 0; j < acount(ctx->structs[i].fields); ++j) {
            const field_info *field = &ctx->structs[i].fields[j];
            if (!strcmp(field->pin, "PIN_HANDLE")) {
                add_handle(ctx, &names, field_type_name(field));
            }
        }
    }
    return names;
}
static void type(FILE *out, const char *pin, const char *name, unsigned flags)
{
    fprintf(
        out,
        "{ .storage = %s, .nominal = UINT64_C(%" PRIu64 "), .flags = %u }",
        pin,
        nominal(pin, name),
        flags | (!strcmp(pin, "PIN_FLOW") ? SCRIPT_TYPE_UNTYPED : 0)
    );
}
static void parameter(
    FILE *out,
    const char *name,
    const char *pin,
    const char *spelling,
    bool input,
    unsigned flags,
    uint64_t member,
    bool owner
)
{
    fputs("    { .name = ", out);
    emit_string(out, name);
    fputs(", .type = ", out);
    type(out, pin, spelling, flags);
    fprintf(
        out,
        ", .member = UINT64_C(%" PRIu64
        "), .has_default = %s, .default_owner = %s",
        member,
        input ? "true" : "false",
        owner ? "true" : "false"
    );
    if (input) {
        fprintf(out, ", .default_value = { .type = %s, .type_name = ", pin);
        emit_string(out, nominal(pin, spelling) ? spelling : NULL);
        fputs(" }", out);
    }
    fputs(" },\n", out);
}
static void parameters_begin(FILE *out, const char *symbol, const char *side)
{
    fprintf(out, "static const script_catalog_parameter %s_%s[] = {\n", symbol, side);
}
static void parameters_end(FILE *out)
{
    fputs("};\n", out);
}
static uint64_t field_id(const struct_info *s, const field_info *f)
{
    char *name = join(s->name, ".", f->name);
    uint64_t id = script_catalog_id("field", name);
    free(name);
    return id;
}
static unsigned effects(const fn_info *f)
{
    unsigned result = 0;
    if (f->effect_flags & NODE_EFFECT_WORLD_READ) {
        result |= SCRIPT_EFFECT_READ;
    }
    if (f->effect_flags & NODE_EFFECT_STRUCTURAL) {
        result |= SCRIPT_EFFECT_STRUCTURAL;
    }
    if ((f->effect_flags & NODE_EFFECT_HOST_SINK) &&
        !(f->effect_flags & NODE_EFFECT_RANDOM)) {
        result |= SCRIPT_EFFECT_HOST;
    }
    if (f->effect_flags & NODE_EFFECT_RANDOM) {
        result |= SCRIPT_EFFECT_RANDOM;
    }
    return result;
}
static void fn_parameters(FILE *out, const fn_info *f)
{
    char *symbol = join(g_prefix, "_native_", f->name);
    parameters_begin(out, symbol, "arguments");
    int inputs = 0;
    for (int i = 0; i < acount(f->pname); ++i) {
        if (f->pout[i]) {
            continue;
        }
        parameter(
            out,
            f->plabel[i] ? f->plabel[i] : f->pname[i],
            f->pservice[i] ? "PIN_HANDLE" : f->ppin[i],
            f->pservice[i] ? f->pservice_type[i] : f->ptype[i],
            true,
            0,
            0,
            false
        );
        ++inputs;
    }
    if (!inputs) {
        fputs("    { 0 },\n", out);
    }
    parameters_end(out);
    parameters_begin(out, symbol, "results");
    int outputs = 0;
    if (!f->read && !f->ret_void) {
        parameter(out, "return", f->ret_pin, f->ret, false, 0, 0, false);
        ++outputs;
    }
    for (int i = 0; i < acount(f->pname); ++i) {
        if (!f->pout[i]) {
            continue;
        }
        parameter(
            out,
            f->plabel[i] ? f->plabel[i] : f->pname[i],
            f->ppin[i],
            f->pout_type[i],
            false,
            0,
            0,
            false
        );
        ++outputs;
    }
    if (!outputs) {
        fputs("    { 0 },\n", out);
    }
    parameters_end(out);
    fprintf(out, "static const int %s_pins[] = {", symbol);
    for (int i = 0; i < outputs; ++i) {
        fprintf(out, "%s%d", i ? ", " : " ", i + (f->exec ? 1 : 0));
    }
    if (!outputs) {
        fputs(" 0", out);
    }
    fputs(" };\n", out);
    fprintf(out, "static const node_access %s_access = { .flags = %u };\n", symbol, f->effect_flags);
    free(symbol);
}
static void struct_parameters(FILE *out, const struct_info *s)
{
    char *symbol = join(g_prefix, "_native_", s->name);
    for (int output = 0; output < 2; ++output) {
        parameters_begin(out, symbol, output ? "fields_out" : "fields_in");
        for (int i = 0; i < acount(s->fields); ++i) {
            const field_info *field = &s->fields[i];
            parameter(out, field->name, field->pin, field_type_name(field), !output, 0, field_id(s, field), false);
        }
        if (!acount(s->fields)) {
            fputs("    { 0 },\n", out);
        }
        parameters_end(out);
    }
    parameters_begin(out, symbol, "make_result");
    parameter(out, s->name, "PIN_STRUCT", s->name, false, 0, 0, false);
    parameters_end(out);
    parameters_begin(out, symbol, "break_argument");
    parameter(out, s->name, "PIN_STRUCT", s->name, true, 0, 0, false);
    parameters_end(out);
    if (s->component || component_unwrap(s)) {
        parameters_begin(out, symbol, "value_argument");
        parameter(out, "Value", "PIN_STRUCT", s->name, true, 0, 0, false);
        parameters_end(out);
    }
    if (s->component) {
        parameters_begin(out, symbol, "value_result");
        parameter(out, "Value", "PIN_STRUCT", s->name, false, 0, 0, false);
        parameters_end(out);
    }
    if (component_unwrap(s)) {
        parameters_begin(out, symbol, "unwrap_result");
        const field_info *field = &s->fields[0];
        parameter(out, field->name, field->pin, field_type_name(field), false, 0, 0, false);
        parameters_end(out);
    }
    if (s->component) {
        parameters_begin(out, symbol, "entity_value");
        parameter(out, "Entity", "PIN_HANDLE", "ecs.entity", true, 0, 0, false);
        parameter(out, "Value", "PIN_STRUCT", s->name, true, 0, 0, false);
        parameters_end(out);
        for (int i = 0; i < acount(s->fields); ++i) {
            const field_info *field = &s->fields[i];
            if (!component_field_settable(field)) {
                continue;
            }
            char *field_symbol = join(symbol, "_", field->name);
            parameters_begin(out, field_symbol, "arguments");
            parameter(out, "Value", field->pin, field_type_name(field), true, 0, 0, false);
            parameter(out, "Target", "PIN_HANDLE", "ecs.entity", true, 0, 0, true);
            parameters_end(out);
            free(field_symbol);
        }
    }
    free(symbol);
}
static void cap(
    FILE *out,
    const char *name,
    const char *symbol,
    const char *inputs,
    int input_count,
    const char *outputs,
    int output_count,
    bool action,
    unsigned effect,
    const char *source_name
)
{
    fprintf(out, "    { .id = UINT64_C(%" PRIu64 "), .name = ", script_catalog_id("capability", name));
    emit_string(out, source_name ? source_name : name);
    fputs(", .qualified_name = ", out);
    emit_string(out, name);
    fprintf(
        out,
        ", .kind = %s, .pure = %s, .effects = %u",
        action ? "SCRIPT_CAPABILITY_ACTION" : "SCRIPT_CAPABILITY_FUNCTION",
        action ? "false" : "true",
        effect
    );
    if (inputs) {
        fprintf(out, ", .parameters = %s_%s, .parameter_count = %d", symbol, inputs, input_count);
    }
    if (outputs) {
        fprintf(out, ", .results = %s_%s, .result_count = %d", symbol, outputs, output_count);
    }
    if (action) {
        fprintf(
            out,
            ", .entries = %s_native_entry, .entry_count = 1, .outcomes = "
            "%s_native_outcome, .outcome_count = 1, .completion_outcome = 0",
            g_prefix,
            g_prefix
        );
    } else {
        fputs(", .completion_outcome = -1", out);
    }
}
static void struct_caps(FILE *out, const struct_info *s)
{
    char *symbol = join(g_prefix, "_native_", s->name);
    uint64_t id = script_catalog_id("type", s->name);
    char *name = join("Make_", s->name, "");
    cap(out, name, symbol, "fields_in", acount(s->fields), "make_result", 1, false, 0, s->constructor_name);
    emit_struct_presentation(out, s, NULL, REFLECT_DESC_MAKE, NULL, false);
    fprintf(out, ", .struct_access = SCRIPT_STRUCT_ACCESS_MAKE, .aggregate = UINT64_C(%" PRIu64 ") },\n", id);
    free(name);
    name = join("Break_", s->name, "");
    cap(out, name, symbol, "break_argument", 1, "fields_out", acount(s->fields), false, 0, NULL);
    emit_struct_presentation(out, s, NULL, REFLECT_DESC_BREAK, NULL, false);
    fprintf(out, ", .struct_access = SCRIPT_STRUCT_ACCESS_BREAK, .aggregate = UINT64_C(%" PRIu64 ") },\n", id);
    free(name);
    if (component_unwrap(s)) {
        name = join("Unwrap_", s->name, "");
        cap(out, name, symbol, "value_argument", 1, "unwrap_result", 1, false, 0, NULL);
        emit_struct_presentation(out, s, &s->fields[0], REFLECT_DESC_UNWRAP, "Conversion", false);
        fputs(" },\n", out);
        free(name);
    }
    if (s->component) {
        static const struct
        {
            const char *name, *input, *output, *op;
            int inputs, entity, value;
            bool action;
            unsigned effects;
        } operations[] = {
            { "Get_", NULL, "value_result", "READ", 0, -1, -1, false, SCRIPT_EFFECT_READ },
            { "GetEntity_", "entity_value", "value_result", "READ", 1, 0, -1, false, SCRIPT_EFFECT_READ },
            { "Set_", "value_argument", "value_result", "WRITE", 1, -1, 0, true, SCRIPT_EFFECT_WRITE },
            { "SetEntity_", "entity_value", "value_result", "WRITE", 2, 0, 1, true, SCRIPT_EFFECT_WRITE },
            { "Has_", NULL, "bool_result", "HAS", 0, -1, -1, false, SCRIPT_EFFECT_READ },
            { "HasEntity_", "entity_value", "bool_result", "HAS", 1, 0, -1, false, SCRIPT_EFFECT_READ },
            { "Add_", NULL, "bool_result", "ADD", 0, -1, -1, true, SCRIPT_EFFECT_WRITE | SCRIPT_EFFECT_STRUCTURAL },
            { "Remove_", NULL, "bool_result", "REMOVE", 0, -1, -1, true, SCRIPT_EFFECT_WRITE | SCRIPT_EFFECT_STRUCTURAL },
        };
        static const char *const descriptions[] = {
            REFLECT_DESC_GET_SELF, REFLECT_DESC_GET_ENTITY,
            REFLECT_DESC_SET_SELF, REFLECT_DESC_SET_ENTITY,
            REFLECT_DESC_HAS_SELF, REFLECT_DESC_HAS_ENTITY,
            REFLECT_DESC_ADD,      REFLECT_DESC_REMOVE
        };
        for (size_t i = 0; i < sizeof operations / sizeof *operations; ++i) {
            name = join(operations[i].name, s->name, "");
            const char *ports = !strcmp(operations[i].output, "bool_result")
                                  ? g_prefix
                                  : symbol;
            cap(out,
                name,
                symbol,
                operations[i].input,
                operations[i].inputs,
                NULL,
                0,
                operations[i].action,
                operations[i].effects,
                NULL);
            emit_struct_presentation(out, s, NULL, descriptions[i], NULL, false);
            fprintf(
                out,
                ", .results = %s_%s, .result_count = 1, .component_access = "
                "SCRIPT_COMPONENT_ACCESS_%s, .component = UINT64_C(%" PRIu64
                "), .component_entity = %d, .component_value = %d },\n",
                ports,
                operations[i].output,
                operations[i].op,
                id,
                operations[i].entity,
                operations[i].value
            );
            free(name);
        }
        for (int i = 0; i < acount(s->fields); ++i) {
            const field_info *field = &s->fields[i];
            if (!component_field_settable(field)) {
                continue;
            }
            char *base = join("Set_", s->name, "_");
            name = join(base, field->name, "");
            free(base);
            char *field_symbol = join(symbol, "_", field->name);
            cap(out, name, field_symbol, "arguments", 2, NULL, 0, true, SCRIPT_EFFECT_WRITE, NULL);
            emit_struct_presentation(out, s, field, REFLECT_DESC_FIELD, NULL, true);
            fprintf(
                out,
                ", .results = %s_value_result, .result_count = 1, "
                ".component_access = SCRIPT_COMPONENT_ACCESS_WRITE, .component "
                "= UINT64_C(%" PRIu64 "), .component_field = UINT64_C(%" PRIu64
                "), .component_entity = 1, .component_value = 0 },\n",
                symbol,
                id,
                field_id(s, field)
            );
            free(field_symbol);
            free(name);
        }
    }
    free(symbol);
}

void emit_native_catalog(FILE *out, const collect_ctx *ctx)
{
    const char **opaque = handles(ctx);
    fprintf(
        out,
        "static const char *const %s_native_entry[] = { \"Exec\" };\nstatic "
        "const char *const %s_native_outcome[] = { \"Then\" };\n",
        g_prefix,
        g_prefix
    );
    parameters_begin(out, g_prefix, "bool_result");
    parameter(out, "Value", "PIN_BOOL", NULL, false, 0, 0, false);
    parameters_end(out);
    for (int i = 0; i < acount(ctx->fns); ++i) {
        fn_parameters(out, &ctx->fns[i]);
    }
    for (int i = 0; i < acount(ctx->structs); ++i) {
        struct_parameters(out, &ctx->structs[i]);
    }
    for (int i = 0; i < acount(ctx->structs); ++i) {
        const struct_info *s = &ctx->structs[i];
        fprintf(out, "static const script_catalog_field %s_native_%s_fields[] = {\n", g_prefix, s->name);
        for (int j = 0; j < acount(s->fields); ++j) {
            const field_info *field = &s->fields[j];
            fprintf(out, "    { .id = UINT64_C(%" PRIu64 "), .name = ", field_id(s, field));
            emit_string(out, field->name);
            fputs(", .type = ", out);
            type(out, field->pin, field_type_name(field), 0);
            fprintf(
                out,
                ", .offset = offsetof(%s, %s), .writable = true },\n",
                s->name,
                field->name
            );
        }
        if (!acount(s->fields)) {
            fputs("    { 0 },\n", out);
        }
        fputs("};\n", out);
    }
    fprintf(out, "static const script_catalog_type %s_native_types[] = {\n", g_prefix);
    fprintf(
        out,
        "    { .id = UINT64_C(%" PRIu64
        "), .name = \"ecs.entity\", .type = { .storage = PIN_HANDLE, .nominal "
        "= UINT64_C(%" PRIu64 ") }, .size = sizeof(uint64_t) },\n",
        script_catalog_id("type", "ecs.entity"),
        script_catalog_id("type", "ecs.entity")
    );
    for (int i = 0; i < acount(ctx->structs); ++i) {
        const struct_info *s = &ctx->structs[i];
        fprintf(out, "    { .id = UINT64_C(%" PRIu64 "), .name = ", script_catalog_id("type", s->name));
        emit_string(out, s->name);
        fputs(", .type = ", out);
        type(out, "PIN_STRUCT", s->name, 0);
        fprintf(
            out,
            ", .size = sizeof(%s), .fields = %s_native_%s_fields, .field_count "
            "= %d",
            s->name,
            g_prefix,
            s->name,
            acount(s->fields)
        );
        if (s->alias_of) {
            fprintf(out, ", .underlying = UINT64_C(%" PRIu64 ")", script_catalog_id("type", s->alias_of));
        }
        if (s->component) {
            const char *source = !strncmp(s->name, "Game", 4) &&
                                         isupper((unsigned char)s->name[4])
                                   ? s->name + 4
                                   : s->name;
            fprintf(
                out,
                ", .component = true, .component_id = UINT64_C(%" PRIu64
                "), .source_name = ",
                component_id(s)
            );
            emit_string(out, source);
            fputs(", .key = ", out);
            emit_string(out, component_key(s));
        }
        fputs(" },\n", out);
    }
    for (int i = 0; i < acount(ctx->enums); ++i) {
        fprintf(
            out,
            "    { .id = UINT64_C(%" PRIu64 "), .name = ",
            script_catalog_id("type", ctx->enums[i].name)
        );
        emit_string(out, ctx->enums[i].name);
        fputs(", .type = ", out);
        type(out, "PIN_ENUM", ctx->enums[i].name, 0);
        fputs(", .size = sizeof(int32_t) },\n", out);
    }
    for (int i = 0; i < acount(opaque); ++i) {
        fprintf(out, "    { .id = UINT64_C(%" PRIu64 "), .name = ", script_catalog_id("type", opaque[i]));
        emit_string(out, opaque[i]);
        fputs(", .type = ", out);
        type(out, "PIN_HANDLE", opaque[i], 0);
        fputs(", .size = sizeof(uint64_t) },\n", out);
    }
    fputs("};\n", out);
    fprintf(out, "static const script_catalog_capability %s_native_declarations[] = {\n", g_prefix);
    int capabilities = 0, bindings = 0;
    for (int i = 0; i < acount(ctx->fns); ++i) {
        const fn_info *f = &ctx->fns[i];
        char *symbol = join(g_prefix, "_native_", f->name);
        int inputs = 0, outputs = !f->read && !f->ret_void;
        for (int j = 0; j < acount(f->pname); ++j) {
            inputs += !f->pout[j];
            outputs += !!f->pout[j];
        }
        cap(out,
            f->display_name ? f->display_name : f->name,
            symbol,
            "arguments",
            inputs,
            "results",
            outputs,
            f->exec,
            effects(f),
            NULL);
        fputs(",\n", out);
        emit_function_presentation(out, f);
        fputs(" },\n", out);
        free(symbol);
        ++capabilities;
        ++bindings;
    }
    for (int i = 0; i < acount(ctx->structs); ++i) {
        const struct_info *s = &ctx->structs[i];
        struct_caps(out, s);
        capabilities += 2 + component_unwrap(s);
        bindings += component_unwrap(s);
        if (s->component) {
            capabilities += 8;
            for (int j = 0; j < acount(s->fields); ++j) {
                capabilities += component_field_settable(&s->fields[j]);
            }
        }
    }
    if (!capabilities) {
        fputs("    { 0 },\n", out);
    }
    fputs("};\n", out);
    fprintf(out, "static const native_capability_binding %s_native_bindings[] = {\n", g_prefix);
    for (int i = 0; i < acount(ctx->fns); ++i) {
        const fn_info *f = &ctx->fns[i];
        int outputs = !f->read && !f->ret_void;
        for (int j = 0; j < acount(f->pname); ++j) {
            outputs += !!f->pout[j];
        }
        fprintf(
            out,
            "    { .id = UINT64_C(%" PRIu64
            "), .call = &%s_%s_execution, .access = &%s_native_%s_access, "
            ".result_pins = %s_native_%s_pins, .result_count = %d },\n",
            script_catalog_id("capability", f->display_name ? f->display_name : f->name),
            g_prefix,
            f->name,
            g_prefix,
            f->name,
            g_prefix,
            f->name,
            outputs
        );
    }
    for (int i = 0; i < acount(ctx->structs); ++i) {
        const struct_info *s = &ctx->structs[i];
        if (!component_unwrap(s)) {
            continue;
        }
        char *name = join("Unwrap_", s->name, "");
        fprintf(
            out,
            "    { .id = UINT64_C(%" PRIu64
            "), .call = &%s_Unwrap_%s_execution },\n",
            script_catalog_id("capability", name),
            g_prefix,
            s->name
        );
        free(name);
    }
    if (!bindings) {
        fputs("    { 0 },\n", out);
    }
    fputs("};\n", out);
    fprintf(
        out,
        "static const script_catalog %s_native_catalog = { .types = "
        "%s_native_types, .type_count = %d, .capabilities = "
        "%s_native_declarations, .capability_count = %d, .entity_type = "
        "UINT64_C(%" PRIu64 ") };\n",
        g_prefix,
        g_prefix,
        1 + acount(ctx->structs) + acount(ctx->enums) + acount(opaque),
        g_prefix,
        capabilities,
        script_catalog_id("type", "ecs.entity")
    );
    fprintf(
        out,
        "const native_capability_pack %s_NATIVE_PACK = { .catalog = "
        "&%s_native_catalog, .bindings = %s_native_bindings, .binding_count = "
        "%d, .layouts = %s_REGISTRY_TYPES, .layout_count = %d };\n",
        g_prefix,
        g_prefix,
        g_prefix,
        bindings,
        g_prefix,
        acount(ctx->structs)
    );
    afree(opaque);
}
