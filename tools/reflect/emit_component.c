#include "reflect_internal.h"

#include <string.h>

bool component_unwrap(const struct_info *s)
{
    return s->component && acount(s->fields) == 1;
}

void emit_execution_unwrap(FILE *out, const struct_info *s)
{
    const field_info *field = &s->fields[0];
    fprintf(
        out,
        "static const char *__execution_%sUnwrap_%s(const "
        "behavior_function_context *ctx, void *service, const pin_value *args, "
        "pin_value *out) {\n",
        symbol_prefix(),
        s->name
    );
    fputs("    (void)ctx; (void)service; if (!args[0].ptr) return \"Unwrap requires a value\";\n", out);
    fprintf(
        out,
        "    %s value = ((const %s *)args[0].ptr)->%s;\n",
        field->spelling,
        s->name,
        field->name
    );
    emit_execution_result(out, field->pin, "value", field->spelling, 0);
    fprintf(
        out,
        "    return NULL;\n}\nconst node_host_call %sUnwrap_%s_execution = { "
        ".context = __execution_%sUnwrap_%s };\n\n",
        symbol_prefix(),
        s->name,
        symbol_prefix(),
        s->name
    );
}

bool component_field_settable(const field_info *field)
{
    if (strcmp(field->pin, "PIN_STRUCT")) {
        return true;
    }
    for (int i = 0; i < acount(g_collection->structs); i++) {
        const struct_info *type = &g_collection->structs[i];
        if (strcmp(type->name, field->spelling)) {
            continue;
        }
        for (int j = 0; j < acount(type->fields); j++) {
            if (!strcmp(type->fields[j].pin, "PIN_STR") ||
                !strcmp(type->fields[j].pin, "PIN_NAME") ||
                !component_field_settable(&type->fields[j])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

const struct_info *derived_source(const struct_info *s)
{
    for (int i = 0; s->derived_from && i < acount(g_collection->structs); i++) {
        const struct_info *source = &g_collection->structs[i];
        if (source->component && source != s && !strcmp(source->name, s->derived_from)) {
            return source;
        }
    }
    return NULL;
}

void emit_component_accessors(FILE *out, const struct_info *s)
{
    const char *name = s->name;
    uint64_t id = component_id(s);
    const struct_info *source = derived_source(s);
    fprintf(
        out,
        "const ecs_component_desc %sCOMPONENT_%s = { .id = UINT64_C(%llu), "
        ".key = \"%s\", .name = \"%s\", .element_size = sizeof(%s), .type = "
        "&%sT_%s, "
        ".index = %s",
        symbol_prefix(),
        name,
        (unsigned long long)id,
        component_key(s),
        name,
        name,
        symbol_prefix(),
        name,
        s->dense ? "ECS_INDEX_DENSE" : "ECS_INDEX_BITSET"
    );
    if (source) {
        fprintf(out, ", .derived_from = UINT64_C(%llu)", (unsigned long long)component_id(source));
    }
    fputs(" };\n", out);
    fprintf(
        out,
        "%s *%s%s_get(ecs_world *world, ecs_entity entity) { return "
        "(%s *)ecs_component_data(world, entity, %sCOMPONENT_%s.id); }\n",
        name,
        symbol_prefix(),
        name,
        name,
        symbol_prefix(),
        name
    );
    fprintf(
        out,
        "const %s *%s%s_get_const(const ecs_world *world, ecs_entity entity) "
        "{ return (const %s *)ecs_component_data_const(world, entity, "
        "%sCOMPONENT_%s.id); }\n",
        name,
        symbol_prefix(),
        name,
        name,
        symbol_prefix(),
        name
    );
    fprintf(
        out,
        "int %s%s_has(const ecs_world *world, ecs_entity entity) { return "
        "ecs_component_has(world, entity, %sCOMPONENT_%s.id); }\n",
        symbol_prefix(),
        name,
        symbol_prefix(),
        name
    );
    fprintf(
        out,
        "int %s%s_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) "
        "{ return ecs_world_record_add(world, entity, %sCOMPONENT_%s.id, "
        "NULL, "
        "sort_key); }\n",
        symbol_prefix(),
        name,
        symbol_prefix(),
        name
    );
    fprintf(
        out,
        "int %s%s_remove(ecs_world *world, ecs_entity entity, uint64_t "
        "sort_key) "
        "{ return ecs_world_record_remove(world, entity, %sCOMPONENT_%s.id, "
        "sort_key); }\n\n",
        symbol_prefix(),
        name,
        symbol_prefix(),
        name
    );
}
