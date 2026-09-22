#include "reflect_internal.h"

#include <stdlib.h>
#include <string.h>

static void emit_retained_strings(FILE *out, const struct_info *s, const char *target, const char *world)
{
    for (int i = 0; i < acount(s->fields); i++) {
        const field_info *field = &s->fields[i];
        char path[1024];
        snprintf(path, sizeof path, "%s.%s", target, field->name);
        if (!strcmp(field->pin, "PIN_STR") || !strcmp(field->pin, "PIN_NAME")) {
            fprintf(out, "    %s = ecs_world_intern_string(%s, %s);\n", path, world, path);
            continue;
        }
        if (strcmp(field->pin, "PIN_STRUCT")) {
            continue;
        }
        for (int j = 0; j < acount(g_collection->structs); j++) {
            const struct_info *nested = &g_collection->structs[j];
            if (!strcmp(nested->name, field->spelling)) {
                emit_retained_strings(out, nested, path, world);
                break;
            }
        }
    }
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
        if (field->handle) {
            hash = hash_string(hash, field->handle);
        }
    }
    return hash;
}

bool component_unwrap(const struct_info *s)
{
    return s->component && acount(s->fields) == 1;
}

void emit_component_unwrap(FILE *out, const struct_info *s)
{
    if (!component_unwrap(s)) {
        return;
    }
    const field_info *field = &s->fields[0];
    fprintf(out, "static pin_value __unwrap_%s_%s(const pin_value *inputs, int count, arena *a) {\n", g_prefix, s->name);
    fputs("    assert(count == 1 && inputs[0].type == PIN_STRUCT && inputs[0].ptr);\n", out);
    fputs("    assert(inputs[0].container == PIN_CONTAINER_SINGLE);\n    (void)a;\n", out);
    fprintf(out, "    %s value = *(const %s *)inputs[0].ptr;\n", s->name, s->name);
    fprintf(
        out,
        "    pin_value result = { .type = %s, .type_name = \"%s\" };\n",
        field->pin,
        field_type_name(field)
    );
    if (strcmp(field->pin, "PIN_STRUCT") == 0) {
        fprintf(
            out,
            "    result.ptr = arena_alloc(a, sizeof(value.%s));\n",
            field->name
        );
        fprintf(
            out,
            "    memcpy(result.ptr, &value.%s, sizeof(value.%s));\n",
            field->name,
            field->name
        );
    } else if (strcmp(field->pin, "PIN_STR") == 0) {
        fprintf(out, "    if (value.%s) {\n", field->name);
        fprintf(out, "        size_t size = strlen(value.%s) + 1;\n", field->name);
        fputs("        char *text = arena_alloc(a, size);\n", out);
        fprintf(out, "        memcpy(text, value.%s, size);\n", field->name);
        fputs("        result.str = text;\n    }\n", out);
    } else {
        fprintf(
            out,
            "    result.%s = value.%s;\n",
            pin_member(field->pin),
            field->name
        );
    }
    fputs("    return result;\n}\n", out);
    fprintf(
        out,
        "static pin_spec %s_%s_unwrap_inputs[] = { { \"Value\", PIN_STRUCT, "
        "\"%s\" } };\n",
        g_prefix,
        s->name,
        s->name
    );
    fprintf(out, "static pin_spec %s_%s_unwrap_outputs[] = {\n", g_prefix, s->name);
    emit_pin_spec(out, field->name, field->pin, field_type_name(field));
    fputs("};\n", out);
    fprintf(out, "static const node_access %s_%s_unwrap_access = { .flags = NODE_EFFECT_PURE };\n", g_prefix, s->name);
    fprintf(out, "node_desc %s_NODE_Unwrap_%s = {\n", g_prefix, s->name);
    fprintf(out, "    .name = \"Unwrap_%s\",\n", s->name);
    fputs("    .input_count = 1, .output_count = 1,\n", out);
    fprintf(
        out,
        "    .inputs = %s_%s_unwrap_inputs, .outputs = %s_%s_unwrap_outputs,\n",
        g_prefix,
        s->name,
        g_prefix,
        s->name
    );
    fprintf(out, "    .pure_kind = NODE_PURE_CALL, .fn = __unwrap_%s_%s,\n", g_prefix, s->name);
    fprintf(out, "    .jit_kind = NODE_JIT_UNWRAP,\n");
    fprintf(out, "    .conversion_from = \"%s\", .conversion_to = \"%s\",\n", s->name, field_type_name(field));
    fputs("    .display_class = NODE_DISPLAY_PURE, .category = \"Conversion\",\n", out);
    fprintf(out, "    .access = &%s_%s_unwrap_access,\n};\n\n", g_prefix, s->name);
}

void emit_component_nodes(FILE *out, const struct_info *s)
{
    const char *name = s->name;
    fprintf(out, "static const char *__read_%s_get_%s(const eval_context *ctx, arena *a, pin_value *out) {\n", g_prefix, name);
    fputs("    ecs_world *world = eval_context_service(ctx, \"ecs.world\");\n", out);
    fputs("    if (!world || !ctx->instance) return \"component Get requires an entity and ecs.world\";\n", out);
    fputs("    ecs_entity entity = ecs_entity_from_u64(behavior_instance_owner(ctx->instance).u64);\n", out);
    fprintf(out, "    const %s *value = %s_%s_get_const(world, entity);\n", name, g_prefix, name);
    fputs("    if (!value) return \"entity lacks component\";\n", out);
    fprintf(out, "    *out = (pin_value){ .type = PIN_STRUCT, .type_name = \"%s\", .ptr = arena_alloc(a, sizeof(%s)) };\n", name, name);
    fputs("    if (!out->ptr) return \"component read allocation failed\";\n", out);
    fprintf(out, "    memcpy(out->ptr, value, sizeof(%s));\n", name);
    fputs("    return NULL;\n}\n\n", out);

    fprintf(out, "static int __flow_%s_set_%s(node *n, flow_context *ctx) {\n", g_prefix, name);
    fprintf(out, "    (void)n;\n    ecs_world *__world = flow_context_service(ctx, \"ecs.world\");\n");
    fputs("    behavior_instance *__instance = flow_context_instance(ctx);\n    pin_value __input;\n", out);
    fputs("    if (!__world || !__instance) return flow_context_fail(ctx, FLOW_ERR_NODE, \"component Set requires an entity and ecs.world\");\n", out);
    fputs("    int result = flow_context_eval_data(ctx, 1, &__input);\n    if (result != FLOW_OK) return result;\n", out);
    fputs("    if (!__input.ptr) return flow_context_fail(ctx, FLOW_ERR_NODE, \"component Set requires a value\");\n", out);
    fputs("    ecs_entity __entity = ecs_entity_from_u64(behavior_instance_owner(__instance).u64);\n", out);
    fprintf(out, "    %s *__value = %s_%s_get(__world, __entity);\n", name, g_prefix, name);
    fputs("    if (!__value) return flow_context_fail(ctx, FLOW_ERR_NODE, \"entity lacks component\");\n", out);
    fprintf(out, "    memcpy(__value, __input.ptr, sizeof(%s));\n", name);
    emit_retained_strings(out, s, "(*__value)", "__world");
    fprintf(out, "    pin_value *__out = flow_context_output_value(ctx, 1);\n    *__out = (pin_value){ .type = PIN_STRUCT, .type_name = \"%s\" };\n", name);
    fprintf(out, "    __out->ptr = flow_context_scratch_alloc(ctx, sizeof(%s));\n", name);
    fputs("    if (!__out->ptr) return FLOW_ERR_NODE;\n", out);
    fprintf(out, "    memcpy(__out->ptr, __value, sizeof(%s));\n", name);
    fputs("    return flow_context_continue(ctx, 0);\n}\n\n", out);

    fprintf(out, "static const char *__read_%s_has_%s(const eval_context *ctx, arena *, pin_value *out) {\n", g_prefix, name);
    fputs("    ecs_world *world = eval_context_service(ctx, \"ecs.world\");\n", out);
    fputs("    if (!world || !ctx->instance) return \"component Has requires an entity and ecs.world\";\n", out);
    fputs("    ecs_entity entity = ecs_entity_from_u64(behavior_instance_owner(ctx->instance).u64);\n", out);
    fprintf(out, "    *out = (pin_value){ .type = PIN_BOOL, .type_name = \"bool\", .b = %s_%s_has(world, entity) };\n", g_prefix, name);
    fputs("    return NULL;\n}\n\n", out);

    fprintf(out, "static int __flow_%s_add_%s(node *n, flow_context *ctx) {\n", g_prefix, name);
    fprintf(out, "    (void)n;\n    ecs_world *__world = flow_context_service(ctx, \"ecs.world\");\n");
    fputs("    behavior_instance *__instance = flow_context_instance(ctx);\n", out);
    fputs("    if (!__world || !__instance) return flow_context_fail(ctx, FLOW_ERR_NODE, \"component Add requires ecs.world\");\n", out);
    fputs("    ecs_entity __entity = ecs_entity_from_u64(behavior_instance_owner(__instance).u64);\n", out);
    fprintf(out, "    int __added = 0;\n    if (!%s_%s_get(__world, __entity)) {\n", g_prefix, name);
    fprintf(out, "        __added = %s_%s_add(__world, __entity, 0) == ECS_OK;\n", g_prefix, name);
    fputs("    }\n    pin_value *__out = flow_context_output_value(ctx, 1);\n    *__out = (pin_value){ .type = PIN_BOOL, .type_name = \"bool\", .b = __added };\n", out);
    fputs("    return flow_context_continue(ctx, 0);\n}\n\n", out);

    fprintf(out, "static int __flow_%s_remove_%s(node *n, flow_context *ctx) {\n", g_prefix, name);
    fprintf(out, "    (void)n;\n    ecs_world *__world = flow_context_service(ctx, \"ecs.world\");\n");
    fputs("    behavior_instance *__instance = flow_context_instance(ctx);\n", out);
    fputs("    if (!__world || !__instance) return flow_context_fail(ctx, FLOW_ERR_NODE, \"component Remove requires ecs.world\");\n", out);
    fputs("    ecs_entity __entity = ecs_entity_from_u64(behavior_instance_owner(__instance).u64);\n", out);
    fprintf(out, "    int __removed = 0;\n    if (%s_%s_get(__world, __entity)) {\n", g_prefix, name);
    fprintf(out, "        __removed = %s_%s_remove(__world, __entity, 0) == ECS_OK;\n", g_prefix, name);
    fputs("    }\n    pin_value *__out = flow_context_output_value(ctx, 1);\n    *__out = (pin_value){ .type = PIN_BOOL, .type_name = \"bool\", .b = __removed };\n", out);
    fputs("    return flow_context_continue(ctx, 0);\n}\n\n", out);
}

void emit_component_entity_nodes(FILE *out, const struct_info *s)
{
    const char *name = s->name;
    const char *reads[] = { "get", "has" };
    for (int i = 0; i < 2; i++) {
        fprintf(
            out,
            "static const char *__read_%s_%s_entity_%s(const eval_context "
            "*ctx, const pin_value *inputs, arena *a, pin_value *out) {\n",
            g_prefix,
            reads[i],
            name
        );
        fputs("    ecs_world *world = eval_context_service(ctx, \"ecs.world\");\n", out);
        fputs("    if (!world) return \"component access requires ecs.world\";\n", out);
        fputs("    ecs_entity entity = ecs_entity_from_u64(inputs[0].u64);\n", out);
        fputs("    if (!ecs_entity_is_alive(world, entity)) return \"component access requires a live entity\";\n", out);
        if (i == 1) {
            fprintf(
                out,
                "    (void)a;\n    *out = (pin_value){ .type = PIN_BOOL, "
                ".type_name = \"bool\", .b = %s_%s_has(world, entity) };\n",
                g_prefix,
                name
            );
            fputs("    return NULL;\n}\n\n", out);
            continue;
        }
        fprintf(out, "    const %s *value = %s_%s_get_const(world, entity);\n", name, g_prefix, name);
        fputs("    if (!value) return \"entity lacks component\";\n", out);
        fprintf(out, "    *out = (pin_value){ .type = PIN_STRUCT, .type_name = \"%s\", .ptr = arena_alloc(a, sizeof(%s)) };\n", name, name);
        fputs("    if (!out->ptr) return \"component read allocation failed\";\n", out);
        fprintf(out, "    memcpy(out->ptr, value, sizeof(%s));\n", name);
        fputs("    return NULL;\n}\n\n", out);
    }
    fprintf(out, "static int __flow_%s_set_entity_%s(node *n, flow_context *ctx) {\n", g_prefix, name);
    fputs("    (void)n;\n    ecs_world *world = flow_context_service(ctx, \"ecs.world\");\n", out);
    fputs("    if (!world) return flow_context_fail(ctx, FLOW_ERR_NODE, \"component Set requires ecs.world\");\n", out);
    fputs("    pin_value target, input;\n    int result = flow_context_eval_data(ctx, 1, &target);\n", out);
    fputs("    if (result != FLOW_OK) return result;\n", out);
    fputs("    ecs_entity entity = ecs_entity_from_u64(target.u64);\n", out);
    fputs("    if (!ecs_entity_is_alive(world, entity)) return flow_context_fail(ctx, FLOW_ERR_NODE, \"component Set requires a live entity\");\n", out);
    fputs("    result = flow_context_eval_data(ctx, 2, &input);\n    if (result != FLOW_OK) return result;\n", out);
    fputs("    if (!input.ptr) return flow_context_fail(ctx, FLOW_ERR_NODE, \"component Set requires a value\");\n", out);
    fprintf(out, "    %s *value = %s_%s_get(world, entity);\n", name, g_prefix, name);
    fputs("    if (!value) return flow_context_fail(ctx, FLOW_ERR_NODE, \"entity lacks component\");\n", out);
    fprintf(out, "    memcpy(value, input.ptr, sizeof(%s));\n", name);
    emit_retained_strings(out, s, "(*value)", "world");
    fprintf(out, "    pin_value *output = flow_context_output_value(ctx, 1);\n    *output = (pin_value){ .type = PIN_STRUCT, .type_name = \"%s\" };\n", name);
    fprintf(out, "    output->ptr = flow_context_scratch_alloc(ctx, sizeof(%s));\n", name);
    fputs("    if (!output->ptr) return flow_context_fail(ctx, FLOW_ERR_NODE, \"component Set allocation failed\");\n", out);
    fprintf(out, "    memcpy(output->ptr, value, sizeof(%s));\n", name);
    fputs("    return flow_context_continue(ctx, 0);\n}\n\n", out);
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

void emit_component_field_nodes(FILE *out, const struct_info *s)
{
    const char *name = s->name;
    fprintf(out, "static const node_access %s_%s_field_access = {\n", g_prefix, name);
    fprintf(out, "    .reads = &%s_COMPONENT_%s.id, .read_count = 1,\n", g_prefix, name);
    fprintf(out, "    .writes = &%s_COMPONENT_%s.id, .write_count = 1,\n", g_prefix, name);
    fputs("    .flags = NODE_EFFECT_WORLD_READ,\n};\n", out);
    for (int i = 0; i < acount(s->fields); i++) {
        const field_info *field = &s->fields[i];
        if (!component_field_settable(field)) {
            continue;
        }
        char symbol[512];
        snprintf(symbol, sizeof symbol, "%s_%s_%s", g_prefix, name, field->name);
        fprintf(out, "static int __flow_set_%s(node *, flow_context *ctx) {\n", symbol);
        fputs("    ecs_world *world = flow_context_service(ctx, \"ecs.world\");\n", out);
        fputs("    if (!world) return flow_context_fail(ctx, FLOW_ERR_NODE, \"component field Set requires ecs.world\");\n", out);
        fputs("    pin_value target, input;\n    int result = flow_context_eval_data(ctx, 2, &target);\n", out);
        fputs("    if (result != FLOW_OK) return result;\n", out);
        fputs("    ecs_entity entity = ecs_entity_from_u64(target.u64);\n", out);
        fputs("    if (!ecs_entity_is_alive(world, entity)) return flow_context_fail(ctx, FLOW_ERR_NODE, \"component field Set requires a live Target\");\n", out);
        fputs("    result = flow_context_eval_data(ctx, 1, &input);\n    if (result != FLOW_OK) return result;\n", out);
        if (!strcmp(field->pin, "PIN_STRUCT")) {
            fputs("    if (!input.ptr) return flow_context_fail(ctx, FLOW_ERR_NODE, \"component field Set requires a value\");\n", out);
        }
        fprintf(out, "    %s *value = %s_%s_get(world, entity);\n", name, g_prefix, name);
        fputs("    if (!value) return flow_context_fail(ctx, FLOW_ERR_NODE, \"Target lacks component\");\n", out);
        if (!strcmp(field->pin, "PIN_STR") || !strcmp(field->pin, "PIN_NAME")) {
            fprintf(
                out,
                "    if (ecs_component_write_field(world, entity, "
                "%s_COMPONENT_%s.id, %d, input) != ECS_OK) return "
                "flow_context_fail(ctx, FLOW_ERR_NODE, \"Could not write "
                "component text field\");\n",
                g_prefix,
                name,
                i
            );
        } else {
            fprintf(out, "    pin_write_field(value, &%s_%s_fields[%d], input);\n", g_prefix, name, i);
        }
        fprintf(out, "    pin_value *output = flow_context_output_value(ctx, 1);\n    *output = (pin_value){ .type = PIN_STRUCT, .type_name = \"%s\" };\n", name);
        fprintf(out, "    output->ptr = flow_context_scratch_alloc(ctx, sizeof(%s));\n", name);
        fprintf(out, "    memcpy(output->ptr, value, sizeof(%s));\n", name);
        fputs("    return flow_context_continue(ctx, 0);\n}\n", out);
        fprintf(out, "static pin_spec %s_inputs[] = {\n", symbol);
        fputs("    { \"Exec\", PIN_FLOW, NULL, PIN_EXEC },\n", out);
        emit_pin_spec(out, "Value", field->pin, field_type_name(field));
        fputs("    { .name = \"Target\", .type = PIN_HANDLE, .type_name = \"ecs.entity\", .flags = PIN_FLAG_DEFAULT_SELF },\n};\n", out);
        fprintf(out, "node_desc %s_NODE_Set_%s_%s = {\n", g_prefix, name, field->name);
        fprintf(
            out,
            "    .name = \"Set_%s_%s\", .input_count = 3, .output_count = 2,\n",
            name,
            field->name
        );
        fprintf(out, "    .inputs = %s_inputs, .outputs = %s_%s_set_outputs,\n", symbol, g_prefix, name);
        fprintf(out, "    .flow_kind = NODE_FLOW_CUSTOM, .flow = __flow_set_%s,\n", symbol);
        fprintf(out, "    .type = &%s_T_%s, .access = &%s_%s_field_access,\n", g_prefix, name, g_prefix, name);
        fprintf(
            out,
            "    .component_kind = NODE_COMPONENT_SET_FIELD, .component = "
            "&%s_COMPONENT_%s, .component_field = %d,\n",
            g_prefix,
            name,
            i
        );
        fprintf(out, "    .display_class = NODE_DISPLAY_FUNCTION, .category = \"Components/%s\",\n", name);
        fprintf(
            out,
            "    .keywords = \"set field %s target self\",\n};\n\n",
            field->name
        );
    }
}

void emit_component_entity_nodes_desc(FILE *out, const struct_info *s)
{
    const char *name = s->name;
    /* Explicit targets can alias another query row: never mark these accesses
     * owner-scoped, even when an author happens to wire Self into Entity. */
    fprintf(
        out,
        "static const node_access %s_%s_entity_read_access = { .reads = "
        "&%s_COMPONENT_%s.id, .read_count = 1, .flags = NODE_EFFECT_WORLD_READ "
        "};\n",
        g_prefix,
        name,
        g_prefix,
        name
    );
    fprintf(
        out,
        "static const node_access %s_%s_entity_write_access = { .writes = "
        "&%s_COMPONENT_%s.id, .write_count = 1 };\n",
        g_prefix,
        name,
        g_prefix,
        name
    );
    fprintf(out, "static pin_spec %s_%s_entity_inputs[] = { { \"Entity\", PIN_HANDLE, \"ecs.entity\" } };\n", g_prefix, name);
    fprintf(
        out,
        "static pin_spec %s_%s_get_entity_outputs[] = { { \"Value\", PIN_STRUCT, \"%s\" } };\n",
        g_prefix,
        name,
        name
    );
    fprintf(
        out,
        "static pin_spec %s_%s_set_entity_inputs[] = { { \"Exec\", PIN_FLOW, "
        "NULL, PIN_EXEC }, { \"Entity\", PIN_HANDLE, \"ecs.entity\" }, { "
        "\"Value\", PIN_STRUCT, \"%s\" } };\n",
        g_prefix,
        name,
        name
    );
    fprintf(
        out,
        "node_desc %s_NODE_GetEntity_%s = { .name = \"GetEntity_%s\", "
        ".input_count = 1, .inputs = %s_%s_entity_inputs, .output_count = 1, "
        ".outputs = %s_%s_get_entity_outputs, .pure_kind = NODE_PURE_READ, "
        ".read_inputs = __read_%s_get_entity_%s, .type = &%s_T_%s, .access = "
        "&%s_%s_entity_read_access, ",
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
    fprintf(out, " .component_kind = NODE_COMPONENT_GET_ENTITY, .component = &%s_COMPONENT_%s };\n\n", g_prefix, name);
    fprintf(
        out,
        "node_desc %s_NODE_HasEntity_%s = { .name = \"HasEntity_%s\", "
        ".input_count = 1, .inputs = %s_%s_entity_inputs, .output_count = 1, "
        ".outputs = %s_%s_has_outputs, .pure_kind = NODE_PURE_READ, "
        ".read_inputs = __read_%s_has_entity_%s, .access = "
        "&%s_%s_entity_read_access, ",
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
    fprintf(out, " .component_kind = NODE_COMPONENT_HAS_ENTITY, .component = &%s_COMPONENT_%s };\n\n", g_prefix, name);
    fprintf(
        out,
        "node_desc %s_NODE_SetEntity_%s = { .name = \"SetEntity_%s\", "
        ".input_count = 3, .inputs = %s_%s_set_entity_inputs, .output_count = "
        "2, .outputs = %s_%s_set_outputs, .flow_kind = NODE_FLOW_CUSTOM, .flow "
        "= __flow_%s_set_entity_%s, .type = &%s_T_%s, .access = "
        "&%s_%s_entity_write_access, ",
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
    fprintf(out, " .component_kind = NODE_COMPONENT_SET_ENTITY, .component = &%s_COMPONENT_%s };\n\n", g_prefix, name);
}

void emit_component_nodes_desc(FILE *out, const struct_info *s)
{
    const char *name = s->name;
    /* Retaining strings writes shared world storage. These aggregate setters
     * must serialize even when their component target is the owner. */
    bool entity_scoped = true;
    for (int i = 0; i < acount(s->fields); i++) {
        entity_scoped &= component_field_settable(&s->fields[i]);
    }
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
        "%s };\n",
        g_prefix,
        name,
        g_prefix,
        name,
        entity_scoped ? "NODE_EFFECT_ENTITY_SCOPED" : "0"
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
    fprintf(out, "static pin_spec %s_%s_exec_inputs[] = { { \"Exec\", PIN_FLOW, NULL, PIN_EXEC } };\n", g_prefix, name);
    fprintf(
        out,
        "static pin_spec %s_%s_get_outputs[] = { { \"Value\", PIN_STRUCT, \"%s\", PIN_DATA, 0, PIN_CONTAINER_SINGLE, { .type = PIN_STRUCT }, 0, PIN_FLAG_REFERENCE } };\n",
        g_prefix,
        name,
        name
    );
    fprintf(
        out,
        "node_desc %s_NODE_Get_%s = { .name = \"Get_%s\", .output_count = 1, "
        ".outputs = %s_%s_get_outputs, .pure_kind = NODE_PURE_READ, .read = "
        "__read_%s_get_%s, "
        ".type = &%s_T_%s, .access = &%s_%s_get_access, .binding_component = "
        "\"%s\", ",
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
        component_key(s)
    );
    fprintf(out, " .component_kind = NODE_COMPONENT_GET_SELF, .component = &%s_COMPONENT_%s };\n\n", g_prefix, name);
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
    fprintf(out, "static pin_spec %s_%s_structural_outputs[] = { { \"Then\", PIN_FLOW, NULL, PIN_EXEC } };\n", g_prefix, name);
    fprintf(
        out,
        "static pin_spec %s_%s_component_outputs[] = { { \"Then\", PIN_FLOW, "
        "NULL, PIN_EXEC }, { \"Value\", PIN_BOOL, \"bool\" } };\n",
        g_prefix,
        name
    );
    fprintf(
        out,
        "node_desc %s_NODE_Set_%s = { .name = \"Set_%s\", .input_count = 2, "
        ".output_count = 2, .inputs = %s_%s_set_inputs, .outputs = "
        "%s_%s_set_outputs, .flow_kind = NODE_FLOW_CUSTOM, .flow = "
        "__flow_%s_set_%s, .type = &%s_T_%s, .access = &%s_%s_set_access, ",
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
    fprintf(out, " .component_kind = NODE_COMPONENT_SET_SELF, .component = &%s_COMPONENT_%s };\n\n", g_prefix, name);
    fprintf(out, "static pin_spec %s_%s_has_outputs[] = { { \"Value\", PIN_BOOL, \"bool\" } };\n", g_prefix, name);
    fprintf(
        out,
        "node_desc %s_NODE_Has_%s = { .name = \"Has_%s\", .output_count = 1, "
        ".outputs = %s_%s_has_outputs, .pure_kind = NODE_PURE_READ, .read = "
        "__read_%s_has_%s, "
        ".access = &%s_%s_has_access, ",
        g_prefix,
        name,
        name,
        g_prefix,
        name,
        g_prefix,
        name,
        g_prefix,
        name
    );
    fprintf(out, " .component_kind = NODE_COMPONENT_HAS_SELF, .component = &%s_COMPONENT_%s };\n\n", g_prefix, name);
    fprintf(
        out,
        "node_desc %s_NODE_Add_%s = { .name = \"Add_%s\", .input_count = 1, "
        ".output_count = 2, .inputs = %s_%s_exec_inputs, .outputs = "
        "%s_%s_component_outputs, .flow_kind = NODE_FLOW_CUSTOM, .flow = "
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
        "1, .output_count = 2, .inputs = %s_%s_exec_inputs, .outputs = "
        "%s_%s_component_outputs, .flow_kind = NODE_FLOW_CUSTOM, .flow = "
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

void emit_component_accessors(FILE *out, const struct_info *s)
{
    const char *name = s->name;
    uint64_t id = component_id(s);
    fprintf(
        out,
        "const ecs_component_desc %s_COMPONENT_%s = { .id = UINT64_C(%llu), "
        ".key = \"%s\", .name = \"%s\", .element_size = sizeof(%s), .type = "
        "&%s_T_%s, "
        ".layout_hash = UINT64_C(%llu), .index = %s };\n",
        g_prefix,
        name,
        (unsigned long long)id,
        component_key(s),
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
