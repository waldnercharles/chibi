#include "prototype.h"

#include "../core/array.h"
#include "../core/registry.h"
#include "../core/value_array.h"
#include "../core/xalloc.h"
#include "../third_party/yyjson/yyjson.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const type_desc PROTOTYPE_TAG_TYPE = { "PrototypeTag", 1, NULL, 0, NULL };
static uint64_t next_identity = 1;

static uint64_t identity_id(const char *identity)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (const unsigned char *p = (const unsigned char *)identity; *p; p++) {
        hash ^= *p;
        hash *= UINT64_C(1099511628211);
    }
    return hash ? hash : 1;
}

static char *new_identity(void)
{
    char identity[32];
    snprintf(identity, sizeof identity, "prototype-%016llx", (unsigned long long)next_identity++);
    return xstrdup(identity);
}

static void setup_tag(game_prototype *prototype)
{
    prototype->tag.name = xmalloc(strlen(prototype->identity) + 16);
    snprintf(
        prototype->tag.name,
        strlen(prototype->identity) + 16,
        "PrototypeTag:%s",
        prototype->identity
    );
    prototype->tag.desc = (ecs_component_desc){
        .id = identity_id(prototype->identity),
        .key = prototype->identity,
        .name = prototype->tag.name,
        .element_size = 1,
        .type = &PROTOTYPE_TAG_TYPE,
        .index = ECS_INDEX_BITSET,
    };
}

static void set_error(char *error, size_t size, const char *format, ...)
{
    if (!error || !size) {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(error, size, format, args);
    va_end(args);
}

static char *own_string(game_prototype *prototype, const char *text)
{
    char *copy = xstrdup(text ? text : "");
    apush(prototype->strings, copy);
    return copy;
}

static const ecs_component_desc *find_descriptor(const game_prototype_registry *registry, const char *name)
{
    for (int i = 0; i < registry->component_count; i++) {
        const ecs_component_desc *desc = registry->components[i];
        if (desc && (strcmp(desc->name, name) == 0 ||
                     (desc->key && strcmp(desc->key, name) == 0))) {
            return desc;
        }
    }
    return NULL;
}

static const type_desc *find_type(const game_prototype_registry *registry, const char *name)
{
    for (int i = 0; i < registry->component_count; i++) {
        const ecs_component_desc *desc = registry->components[i];
        if (desc && desc->type && strcmp(desc->type->name, name) == 0) {
            return desc->type;
        }
    }
    return registry_type_by_name(name);
}

static game_prototype_component *find_component(game_prototype *prototype, const char *name)
{
    for (int i = 0; i < acount(prototype->components); i++) {
        const ecs_component_desc *desc = prototype->components[i].desc;
        if (strcmp(desc->name, name) == 0 ||
            (desc->key && strcmp(desc->key, name) == 0)) {
            return &prototype->components[i];
        }
    }
    return NULL;
}

static const type_field *find_field(const type_desc *type, const char *name)
{
    for (int i = 0; i < type->field_count; i++) {
        if (strcmp(type->fields[i].name, name) == 0) {
            return &type->fields[i];
        }
    }
    return NULL;
}

static int read_signed(yyjson_val *value, int64_t *out)
{
    if (yyjson_is_sint(value)) {
        *out = yyjson_get_sint(value);
        return 0;
    }
    if (yyjson_is_uint(value) && yyjson_get_uint(value) <= INT64_MAX) {
        *out = (int64_t)yyjson_get_uint(value);
        return 0;
    }
    return -1;
}

static int read_unsigned(yyjson_val *value, uint64_t *out)
{
    if (yyjson_is_uint(value)) {
        *out = yyjson_get_uint(value);
        return 0;
    }
    if (yyjson_is_sint(value) && yyjson_get_sint(value) >= 0) {
        *out = (uint64_t)yyjson_get_sint(value);
        return 0;
    }
    return -1;
}

static int parse_fields(
    game_prototype *prototype,
    const game_prototype_registry *registry,
    void *base,
    const type_desc *type,
    yyjson_val *object,
    char *error,
    size_t error_size
);

static int parse_field(
    game_prototype *prototype,
    const game_prototype_registry *registry,
    void *base,
    const type_field *field,
    yyjson_val *json,
    char *error,
    size_t error_size
)
{
    pin_value value = { .type = field->type, .type_name = field->type_name };
    int result = 0;
    int64_t signed_value = 0;
    uint64_t unsigned_value = 0;
    switch (field->type) {
        case PIN_BOOL:
            result = !yyjson_is_bool(json);
            value.b = result ? false : yyjson_get_bool(json);
            break;
        case PIN_I8:
            result = read_signed(json, &signed_value);
            value.i8 = (int8_t)signed_value;
            break;
        case PIN_I16:
            result = read_signed(json, &signed_value);
            value.i16 = (int16_t)signed_value;
            break;
        case PIN_I32:
            result = read_signed(json, &signed_value);
            value.i = (int32_t)signed_value;
            break;
        case PIN_I64:
            result = read_signed(json, &signed_value);
            value.i64 = signed_value;
            break;
        case PIN_ENUM:
            result = read_signed(json, &signed_value);
            value.e = (int32_t)signed_value;
            break;
        case PIN_U8:
            result = read_unsigned(json, &unsigned_value);
            value.u8 = (uint8_t)unsigned_value;
            break;
        case PIN_U16:
            result = read_unsigned(json, &unsigned_value);
            value.u16 = (uint16_t)unsigned_value;
            break;
        case PIN_U32:
            result = read_unsigned(json, &unsigned_value);
            value.u = (uint32_t)unsigned_value;
            break;
        case PIN_U64:
            result = read_unsigned(json, &unsigned_value);
            value.u64 = unsigned_value;
            break;
        case PIN_F32:
            result = !yyjson_is_num(json);
            value.f = result ? 0.0f : (float)yyjson_get_num(json);
            break;
        case PIN_F64:
            result = !yyjson_is_num(json);
            value.d = result ? 0.0 : yyjson_get_num(json);
            break;
        case PIN_NAME:
        case PIN_STR:
            result = !yyjson_is_str(json);
            value.str = result ? NULL : own_string(prototype, yyjson_get_str(json));
            break;
        case PIN_STRUCT: {
            const type_desc *nested = find_type(registry, field->type_name);
            void *nested_value = xcalloc(1, field->size);
            result = !nested ||
                     parse_fields(prototype, registry, nested_value, nested, json, error, error_size);
            if (!result) {
                value.ptr = nested_value;
                pin_write_field(base, field, value);
            }
            free(nested_value);
            return result;
        }
        case PIN_HANDLE:
        case PIN_FLOW: result = 1; break;
    }
    if (result) {
        set_error(error, error_size, "field %s has the wrong value type", field->name);
        return -1;
    }
    pin_write_field(base, field, value);
    return 0;
}

static int parse_fields(
    game_prototype *prototype,
    const game_prototype_registry *registry,
    void *base,
    const type_desc *type,
    yyjson_val *object,
    char *error,
    size_t error_size
)
{
    if (!yyjson_is_obj(object)) {
        set_error(error, error_size, "value for %s must be an object", type->name);
        return -1;
    }
    yyjson_obj_iter iterator = yyjson_obj_iter_with(object);
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&iterator))) {
        const char *name = yyjson_get_str(key);
        const type_field *field = find_field(type, name);
        if (!field) {
            set_error(error, error_size, "component %s has unknown field %s", type->name, name);
            return -1;
        }
        if (parse_field(prototype, registry, base, field, yyjson_obj_iter_get_val(key), error, error_size)) {
            return -1;
        }
    }
    return 0;
}

static int parse_components(
    game_prototype *prototype,
    const game_prototype_registry *registry,
    yyjson_val *object,
    char *error,
    size_t error_size
)
{
    if (!object) {
        return 0;
    }
    if (!yyjson_is_obj(object)) {
        set_error(error, error_size, "components must be an object");
        return -1;
    }
    yyjson_obj_iter iterator = yyjson_obj_iter_with(object);
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&iterator))) {
        const char *name = yyjson_get_str(key);
        const ecs_component_desc *desc = find_descriptor(registry, name);
        yyjson_val *value = yyjson_obj_iter_get_val(key);
        if (!desc) {
            set_error(error, error_size, "component %s is unavailable", name);
            return -1;
        }
        game_prototype_component *component = find_component(prototype, name);
        if (!component) {
            component = &(game_prototype_component){
                .desc = desc,
                .data = xcalloc(1, desc->element_size),
            };
            apush(prototype->components, *component);
            component = &alast(prototype->components);
        }
        if (parse_fields(prototype, registry, component->data, desc->type, value, error, error_size)) {
            return -1;
        }
    }
    return 0;
}

static int contains_name(char *const *stack, int count, const char *name)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(stack[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void move_strings(game_prototype *dst, game_prototype *src)
{
    for (int i = 0; i < acount(src->strings); i++) {
        apush(dst->strings, src->strings[i]);
    }
    afree(src->strings);
}

static void merge_prototype(game_prototype *dst, game_prototype *src)
{
    move_strings(dst, src);
    for (int i = 0; i < acount(src->components); i++) {
        game_prototype_component component = src->components[i];
        game_prototype_component *existing = find_component(dst, component.desc->name);
        if (existing) {
            memcpy(existing->data, component.data, component.desc->element_size);
            free(component.data);
        } else {
            apush(dst->components, component);
        }
    }
    aclear(src->components);
    for (int i = 0; i < acount(src->graphs); i++) {
        apush(dst->graphs, src->graphs[i]);
    }
    aclear(src->graphs);
}

static game_prototype *load_document(
    const char *json,
    const game_prototype_registry *registry,
    char *const *stack,
    int stack_count,
    char *error,
    size_t error_size
)
{
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    if (!doc) {
        set_error(error, error_size, "malformed prototype JSON");
        return NULL;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *name_value = yyjson_is_obj(root) ? yyjson_obj_get(root, "name") : NULL;
    if (!yyjson_is_str(name_value) || !yyjson_get_len(name_value)) {
        set_error(error, error_size, "prototype name is required");
        yyjson_doc_free(doc);
        return NULL;
    }
    const char *name = yyjson_get_str(name_value);
    if (contains_name(stack, stack_count, name)) {
        set_error(error, error_size, "prototype include cycle at %s", name);
        yyjson_doc_free(doc);
        return NULL;
    }
    game_prototype *prototype = (game_prototype *)xcalloc(1, sizeof *prototype);
    prototype->name = xstrdup(name);
    prototype->identity = new_identity();
    prototype->registry = registry;
    yyjson_val *identity = yyjson_obj_get(root, "identity");
    if (identity) {
        if (!yyjson_is_str(identity) || !yyjson_get_len(identity)) {
            set_error(error, error_size, "prototype identity must be a non-empty string");
            game_prototype_free(prototype);
            yyjson_doc_free(doc);
            return NULL;
        }
        free(prototype->identity);
        prototype->identity = xstrdup(yyjson_get_str(identity));
    }
    setup_tag(prototype);
    char *next_stack[64];
    if (stack_count >= (int)(sizeof next_stack / sizeof next_stack[0])) {
        set_error(error, error_size, "prototype include depth exceeded");
        game_prototype_free(prototype);
        yyjson_doc_free(doc);
        return NULL;
    }
    for (int i = 0; i < stack_count; i++) {
        next_stack[i] = (char *)stack[i];
    }
    next_stack[stack_count] = prototype->name;

    yyjson_val *includes = yyjson_obj_get(root, "includes");
    if (includes) {
        if (!yyjson_is_arr(includes) || !registry->include) {
            set_error(error, error_size, "prototype includes need a resolver");
            game_prototype_free(prototype);
            yyjson_doc_free(doc);
            return NULL;
        }
        yyjson_arr_iter iterator = yyjson_arr_iter_with(includes);
        yyjson_val *include;
        while ((include = yyjson_arr_iter_next(&iterator))) {
            if (!yyjson_is_str(include)) {
                set_error(error, error_size, "prototype include name is not a string");
                game_prototype_free(prototype);
                yyjson_doc_free(doc);
                return NULL;
            }
            const char *source = registry
                                     ->include(yyjson_get_str(include), registry->include_user);
            if (!source) {
                set_error(error, error_size, "prototype include %s is unavailable", yyjson_get_str(include));
                game_prototype_free(prototype);
                yyjson_doc_free(doc);
                return NULL;
            }
            game_prototype *base = load_document(source, registry, next_stack, stack_count + 1, error, error_size);
            if (!base) {
                game_prototype_free(prototype);
                yyjson_doc_free(doc);
                return NULL;
            }
            merge_prototype(prototype, base);
            game_prototype_free(base);
        }
    }
    if (parse_components(prototype, registry, yyjson_obj_get(root, "components"), error, error_size)) {
        game_prototype_free(prototype);
        yyjson_doc_free(doc);
        return NULL;
    }
    yyjson_val *graphs = yyjson_obj_get(root, "behaviors");
    if (graphs) {
        if (!yyjson_is_arr(graphs)) {
            set_error(error, error_size, "behaviors must be an array");
            game_prototype_free(prototype);
            yyjson_doc_free(doc);
            return NULL;
        }
        yyjson_arr_iter iterator = yyjson_arr_iter_with(graphs);
        yyjson_val *graph;
        while ((graph = yyjson_arr_iter_next(&iterator))) {
            if (!yyjson_is_str(graph)) {
                set_error(error, error_size, "behavior graph name is not a string");
                game_prototype_free(prototype);
                yyjson_doc_free(doc);
                return NULL;
            }
            apush(prototype->graphs, own_string(prototype, yyjson_get_str(graph)));
        }
    }
    yyjson_doc_free(doc);
    return prototype;
}

game_prototype *game_prototype_load(
    const char *json,
    const game_prototype_registry *registry,
    char *error,
    size_t error_size
)
{
    if (!json || !registry || registry->component_count < 0 ||
        (registry->component_count && !registry->components)) {
        set_error(error, error_size, "invalid prototype load arguments");
        return NULL;
    }
    if (error && error_size) {
        error[0] = '\0';
    }
    return load_document(json, registry, NULL, 0, error, error_size);
}

game_prototype *game_prototype_clone(const game_prototype *prototype, char *error, size_t error_size)
{
    if (!prototype || !prototype->registry) {
        set_error(error, error_size, "invalid prototype clone arguments");
        return NULL;
    }
    char *json = game_prototype_save(prototype);
    game_prototype *copy = json
                             ? game_prototype_load(json, prototype->registry, error, error_size)
                             : NULL;
    free(json);
    return copy;
}

game_prototype *game_prototype_duplicate(const game_prototype *prototype, char *error, size_t error_size)
{
    game_prototype *copy = game_prototype_clone(prototype, error, error_size);
    if (!copy) {
        return NULL;
    }
    free(copy->identity);
    copy->identity = new_identity();
    free(copy->tag.name);
    setup_tag(copy);
    return copy;
}

const char *game_prototype_identity(const game_prototype *prototype)
{
    return prototype ? prototype->identity : NULL;
}

const ecs_component_desc *game_prototype_tag_desc(const game_prototype *prototype)
{
    return prototype ? &prototype->tag.desc : NULL;
}

int game_prototype_register_tag(const game_prototype *prototype, ecs_world *world)
{
    if (!prototype || !world) {
        return ECS_ERR_ARGUMENT;
    }
    for (int i = 0; i < ecs_world_component_count(world); i++) {
        const ecs_component_desc *desc = ecs_world_component_at(world, i);
        if (desc->id == prototype->tag.desc.id) {
            return desc->key && strcmp(desc->key, prototype->identity) == 0
                     ? ECS_OK
                     : ECS_ERR_DUPLICATE;
        }
    }
    return ecs_world_register_component(world, &prototype->tag.desc, NULL);
}

int game_prototype_matches(const game_prototype *prototype, const ecs_world *world, ecs_entity entity)
{
    return prototype && world && ecs_entity_is_alive(world, entity) &&
           ecs_component_has(world, entity, prototype->tag.desc.id);
}

int game_prototype_set_name(game_prototype *prototype, const char *name)
{
    if (!prototype || !name || !name[0]) {
        return -1;
    }
    char *copy = xstrdup(name);
    free(prototype->name);
    prototype->name = copy;
    return 0;
}

game_prototype *game_prototype_capture_entity(
    const char *name,
    const ecs_world *world,
    ecs_entity entity,
    const game_prototype_registry *registry
)
{
    if (!name || !name[0] || !world || !registry || registry->component_count < 0 ||
        (registry->component_count && !registry->components) ||
        !ecs_entity_is_alive(world, entity)) {
        return NULL;
    }
    game_prototype *prototype = (game_prototype *)xcalloc(1, sizeof *prototype);
    prototype->name = xstrdup(name);
    prototype->identity = new_identity();
    prototype->registry = registry;
    setup_tag(prototype);
    for (int i = 0; i < registry->component_count; i++) {
        const ecs_component_desc *desc = registry->components[i];
        if (!desc || !ecs_component_has(world, entity, desc->id)) {
            continue;
        }
        game_prototype_component component = {
            .desc = desc,
            .data = xmalloc(desc->element_size),
        };
        memcpy(component.data, ecs_component_data_const(world, entity, desc->id), desc->element_size);
        apush(prototype->components, component);
    }
    return prototype;
}

void game_prototype_free(game_prototype *prototype)
{
    if (!prototype) {
        return;
    }
    for (int i = 0; i < acount(prototype->components); i++) {
        free(prototype->components[i].data);
    }
    afree(prototype->components);
    afree(prototype->graphs);
    for (int i = 0; i < acount(prototype->strings); i++) {
        free(prototype->strings[i]);
    }
    afree(prototype->strings);
    free(prototype->name);
    free(prototype->identity);
    free(prototype->tag.name);
    free(prototype);
}

static int compare_strings(const void *left_ptr, const void *right_ptr)
{
    const char *const *left = (const char *const *)left_ptr;
    const char *const *right = (const char *const *)right_ptr;
    return strcmp(*left, *right);
}

static void write_struct_fields(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const game_prototype *prototype,
    const void *base,
    const type_desc *type
);

static yyjson_mut_val *write_value(
    yyjson_mut_doc *doc,
    const game_prototype *prototype,
    const void *base,
    const type_field *field
)
{
    pin_value value;
    pin_read_field(base, field, &value);
    switch (field->type) {
        case PIN_BOOL: return yyjson_mut_bool(doc, value.b);
        case PIN_I8: return yyjson_mut_sint(doc, value.i8);
        case PIN_I16: return yyjson_mut_sint(doc, value.i16);
        case PIN_I32: return yyjson_mut_sint(doc, value.i);
        case PIN_I64: return yyjson_mut_sint(doc, value.i64);
        case PIN_ENUM: return yyjson_mut_sint(doc, value.e);
        case PIN_U8: return yyjson_mut_uint(doc, value.u8);
        case PIN_U16: return yyjson_mut_uint(doc, value.u16);
        case PIN_U32: return yyjson_mut_uint(doc, value.u);
        case PIN_U64: return yyjson_mut_uint(doc, value.u64);
        case PIN_F32: return yyjson_mut_real(doc, value.f);
        case PIN_F64: return yyjson_mut_real(doc, value.d);
        case PIN_NAME:
        case PIN_STR: return yyjson_mut_strcpy(doc, value.str ? value.str : "");
        case PIN_STRUCT: {
            yyjson_mut_val *object = yyjson_mut_obj(doc);
            const type_desc *nested = find_type(prototype->registry, field->type_name);
            if (nested && value.ptr) {
                write_struct_fields(doc, object, prototype, value.ptr, nested);
            }
            return object;
        }
        case PIN_HANDLE:
        case PIN_FLOW: return yyjson_mut_null(doc);
    }
    return yyjson_mut_null(doc);
}

static void write_struct_fields(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const game_prototype *prototype,
    const void *base,
    const type_desc *type
)
{
    int *order = NULL;
    for (int i = 0; i < type->field_count; i++) {
        apush(order, i);
    }
    for (int i = 1; i < acount(order); i++) {
        int value = order[i];
        int at = i;
        while (at &&
               strcmp(type->fields[order[at - 1]].name, type->fields[value].name) > 0) {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = value;
    }
    for (int i = 0; i < acount(order); i++) {
        const type_field *field = &type->fields[order[i]];
        pin_value value;
        pin_read_field(base, field, &value);
        yyjson_mut_val *json = NULL;
        if (field->type == PIN_STRUCT && value.ptr) {
            json = yyjson_mut_obj(doc);
            write_struct_fields(
                doc,
                json,
                prototype,
                value.ptr,
                find_type(prototype->registry, field->type_name)
            );
        } else {
            json = write_value(doc, prototype, base, field);
        }
        yyjson_mut_obj_add_val(doc, object, field->name, json);
    }
    afree(order);
}

char *game_prototype_save(const game_prototype *prototype)
{
    if (!prototype) {
        return NULL;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "name", prototype->name);
    yyjson_mut_obj_add_strcpy(doc, root, "identity", prototype->identity);
    yyjson_mut_val *components = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, root, "components", components);
    int *order = NULL;
    for (int i = 0; i < acount(prototype->components); i++) {
        apush(order, i);
    }
    for (int i = 1; i < acount(order); i++) {
        int value = order[i];
        int at = i;
        while (at && strcmp(
                         prototype->components[order[at - 1]].desc->name,
                         prototype->components[value].desc->name
                     ) > 0) {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = value;
    }
    for (int i = 0; i < acount(order); i++) {
        game_prototype_component *component = &prototype->components[order[i]];
        yyjson_mut_val *object = yyjson_mut_obj(doc);
        write_struct_fields(
            doc,
            object,
            prototype,
            component->data,
            component->desc->type
        );
        yyjson_mut_obj_add_val(
            doc,
            components,
            component->desc->key ? component->desc->key : component->desc->name,
            object
        );
    }
    afree(order);
    if (acount(prototype->graphs)) {
        char **graphs = NULL;
        for (int i = 0; i < acount(prototype->graphs); i++) {
            apush(graphs, prototype->graphs[i]);
        }
        qsort(graphs, (size_t)acount(graphs), sizeof *graphs, compare_strings);
        yyjson_mut_val *array = yyjson_mut_arr(doc);
        for (int i = 0; i < acount(graphs); i++) {
            yyjson_mut_arr_add_strcpy(doc, array, graphs[i]);
        }
        yyjson_mut_obj_add_val(doc, root, "behaviors", array);
        afree(graphs);
    }
    char *result = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES, NULL);
    yyjson_mut_doc_free(doc);
    return result;
}

char *game_prototype_components_json(const game_prototype *prototype)
{
    char *full = game_prototype_save(prototype);
    if (!full) {
        return NULL;
    }
    yyjson_doc *doc = yyjson_read(full, strlen(full), 0);
    free(full);
    if (!doc) {
        return NULL;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *components = yyjson_is_obj(root) ? yyjson_obj_get(root, "components")
                                                 : NULL;
    char *result = components
                     ? yyjson_val_write(components, YYJSON_WRITE_PRETTY_TWO_SPACES, NULL)
                     : NULL;
    yyjson_doc_free(doc);
    return result;
}

static int field_equal(
    const game_prototype *prototype,
    const type_field *field,
    const void *left,
    const void *right
)
{
    if (field->type == PIN_STRUCT) {
        const type_desc *nested = find_type(prototype->registry, field->type_name);
        if (!nested) {
            return 0;
        }
        const unsigned char *left_value = (const unsigned char *)left + field->offset;
        const unsigned char *right_value = (const unsigned char *)right + field->offset;
        for (int i = 0; i < nested->field_count; i++) {
            if (!field_equal(prototype, &nested->fields[i], left_value, right_value)) {
                return 0;
            }
        }
        return 1;
    }
    pin_value left_value = { 0 };
    pin_value right_value = { 0 };
    pin_read_field(left, field, &left_value);
    pin_read_field(right, field, &right_value);
    return pin_value_equal(&left_value, &right_value);
}

char *game_prototype_capture_overrides_json(const game_prototype *base, const ecs_world *world, ecs_entity entity)
{
    if (!base || !world || !ecs_entity_is_alive(world, entity)) {
        return NULL;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_val *components = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_val(doc, root, "components", components);
    int *order = NULL;
    for (int i = 0; i < acount(base->components); i++) {
        apush(order, i);
    }
    for (int i = 1; i < acount(order); i++) {
        int value = order[i];
        int at = i;
        while (at && strcmp(
                         base->components[order[at - 1]].desc->name,
                         base->components[value].desc->name
                     ) > 0) {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = value;
    }
    for (int i = 0; i < acount(order); i++) {
        const game_prototype_component *component = &base->components[order[i]];
        if (!ecs_component_has(world, entity, component->desc->id)) {
            continue;
        }
        const void
            *current = ecs_component_data_const(world, entity, component->desc->id);
        yyjson_mut_val *fields = NULL;
        for (int field_index = 0; field_index < component->desc->type->field_count;
             field_index++) {
            const type_field *field = &component->desc->type->fields[field_index];
            if (field_equal(base, field, component->data, current)) {
                continue;
            }
            if (!fields) {
                fields = yyjson_mut_obj(doc);
            }
            yyjson_mut_obj_add_val(doc, fields, field->name, write_value(doc, base, current, field));
        }
        if (fields) {
            yyjson_mut_obj_add_val(
                doc,
                components,
                component->desc->key ? component->desc->key : component->desc->name,
                fields
            );
        }
    }
    afree(order);
    char *result = yyjson_mut_val_write(components, YYJSON_WRITE_PRETTY_TWO_SPACES, NULL);
    yyjson_mut_doc_free(doc);
    return result;
}

static int apply_override_fields(
    game_prototype *parser,
    const game_prototype_registry *registry,
    void *base,
    const type_desc *type,
    yyjson_val *fields,
    char *error,
    size_t error_size
)
{
    if (!yyjson_is_obj(fields)) {
        set_error(error, error_size, "value for %s override must be an object", type->name);
        return -1;
    }
    yyjson_obj_iter iterator = yyjson_obj_iter_with(fields);
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&iterator))) {
        const type_field *field = find_field(type, yyjson_get_str(key));
        if (!field) {
            set_error(
                error,
                error_size,
                "type %s has unknown override field %s",
                type->name,
                yyjson_get_str(key)
            );
            return -1;
        }
        yyjson_val *value = yyjson_obj_iter_get_val(key);
        if (field->type == PIN_STRUCT) {
            const type_desc *nested = find_type(registry, field->type_name);
            if (!nested || apply_override_fields(
                               parser,
                               registry,
                               (unsigned char *)base + field->offset,
                               nested,
                               value,
                               error,
                               error_size
                           ) != 0) {
                set_error(
                    error,
                    error_size,
                    "field %s has an invalid nested override",
                    field->name
                );
                return -1;
            }
            continue;
        }
        if (parse_field(parser, registry, base, field, value, error, error_size) != 0) {
            return -1;
        }
    }
    return 0;
}

static int apply_component_override(
    game_prototype *parser,
    ecs_world *world,
    ecs_entity entity,
    const game_prototype_registry *registry,
    const ecs_component_desc *desc,
    yyjson_val *fields,
    char *error,
    size_t error_size
)
{
    void *base = ecs_component_data(world, entity, desc->id);
    if (!base || !yyjson_is_obj(fields)) {
        set_error(error, error_size, "component %s override is invalid", desc->name);
        return -1;
    }
    return apply_override_fields(parser, registry, base, desc->type, fields, error, error_size);
}

int game_prototype_apply_components_json(
    ecs_world *world,
    ecs_entity entity,
    const char *components_json,
    const game_prototype_registry *registry,
    char *error,
    size_t error_size
)
{
    if (!world || !registry || !components_json || !ecs_entity_is_alive(world, entity)) {
        set_error(error, error_size, "invalid component override arguments");
        return -1;
    }
    yyjson_doc *doc = yyjson_read(components_json, strlen(components_json), 0);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    if (!root || !yyjson_is_obj(root)) {
        set_error(error, error_size, "component overrides must be an object");
        yyjson_doc_free(doc);
        return -1;
    }
    game_prototype parser = { .registry = registry };
    yyjson_obj_iter iterator = yyjson_obj_iter_with(root);
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&iterator))) {
        const ecs_component_desc *desc = find_descriptor(registry, yyjson_get_str(key));
        if (!desc) {
            set_error(error, error_size, "unknown override component %s", yyjson_get_str(key));
            yyjson_doc_free(doc);
            afree(parser.strings);
            return -1;
        }
        if (!ecs_component_has(world, entity, desc->id) &&
            ecs_world_record_add(world, entity, desc->id, NULL, 0) != ECS_OK) {
            set_error(error, error_size, "override component %s could not be added", desc->name);
            yyjson_doc_free(doc);
            afree(parser.strings);
            return -1;
        }
    }
    if (ecs_world_apply_commands(world) != ECS_OK) {
        set_error(error, error_size, "component override application failed");
        yyjson_doc_free(doc);
        afree(parser.strings);
        return -1;
    }
    iterator = yyjson_obj_iter_with(root);
    while ((key = yyjson_obj_iter_next(&iterator))) {
        const ecs_component_desc *desc = find_descriptor(registry, yyjson_get_str(key));
        if (apply_component_override(&parser, world, entity, registry, desc, yyjson_obj_iter_get_val(key), error, error_size) !=
            0) {
            yyjson_doc_free(doc);
            afree(parser.strings);
            return -1;
        }
    }
    yyjson_doc_free(doc);
    afree(parser.strings);
    return 0;
}

int game_prototype_spawn(
    const game_prototype *prototype,
    ecs_world *world,
    const game_prototype_spawn_hooks *hooks,
    ecs_entity *out_entity,
    char *error,
    size_t error_size
)
{
    if (!prototype || !world || !out_entity) {
        set_error(error, error_size, "invalid prototype spawn arguments");
        return ECS_ERR_ARGUMENT;
    }
    if (acount(prototype->graphs) && (!hooks || !hooks->attach)) {
        set_error(error, error_size, "prototype has behavior graphs but no attach hook");
        return ECS_ERR_ARGUMENT;
    }
    if (game_prototype_register_tag(prototype, world) != ECS_OK) {
        set_error(error, error_size, "prototype identity tag could not be registered");
        return ECS_ERR_DUPLICATE;
    }
    ecs_entity entity = ecs_world_record_create(world, 0);
    if (!entity.slot || ecs_world_apply_commands(world) != ECS_OK) {
        set_error(error, error_size, "prototype entity allocation failed");
        return ECS_ERR_CAPACITY;
    }
    for (int i = 0; i < acount(prototype->components); i++) {
        const game_prototype_component *component = &prototype->components[i];
        if (ecs_world_record_add(world, entity, component->desc->id, NULL, (uint64_t)i + 1)) {
            set_error(
                error,
                error_size,
                "component %s could not be added",
                component->desc->name
            );
            ecs_world_destroy_entity(world, entity);
            return ECS_ERR_COMPONENT;
        }
    }
    if (ecs_world_record_add(world, entity, prototype->tag.desc.id, NULL, 0)) {
        set_error(error, error_size, "prototype identity tag could not be added");
        ecs_world_destroy_entity(world, entity);
        return ECS_ERR_COMPONENT;
    }
    if (ecs_world_apply_commands(world) != ECS_OK) {
        set_error(error, error_size, "prototype component application failed");
        ecs_world_destroy_entity(world, entity);
        return ECS_ERR_COMMAND;
    }
    for (int i = 0; i < acount(prototype->components); i++) {
        const game_prototype_component *component = &prototype->components[i];
        for (int field = 0; field < component->desc->type->field_count; field++) {
            pin_value value;
            pin_read_field(component->data, &component->desc->type->fields[field], &value);
            if (ecs_component_write_field(world, entity, component->desc->id, field, value)) {
                set_error(
                    error,
                    error_size,
                    "component %s field %s could not be written",
                    component->desc->name,
                    component->desc->type->fields[field].name
                );
                ecs_world_destroy_entity(world, entity);
                return ECS_ERR_COMPONENT;
            }
        }
    }
    if (hooks && hooks->attach) {
        for (int i = 0; i < acount(prototype->graphs); i++) {
            if (hooks->attach(world, entity, prototype->graphs[i], hooks->attach_user)) {
                set_error(
                    error,
                    error_size,
                    "behavior graph %s could not attach",
                    prototype->graphs[i]
                );
                ecs_world_destroy_entity(world, entity);
                return ECS_ERR_COMMAND;
            }
        }
    }
    *out_entity = entity;
    return ECS_OK;
}

int game_prototype_read_field(
    const game_prototype *prototype,
    const char *component_name,
    const char *field_name,
    pin_value *out
)
{
    if (!prototype || !component_name || !field_name || !out) {
        return -1;
    }
    game_prototype_component *component = find_component((game_prototype *)prototype, component_name);
    const type_field *field = component
                                ? find_field(component->desc->type, field_name)
                                : NULL;
    if (!field) {
        return -1;
    }
    pin_read_field(component->data, field, out);
    return 0;
}

int game_prototype_write_field(
    game_prototype *prototype,
    const char *component_name,
    const char *field_name,
    pin_value value
)
{
    if (!prototype || !component_name || !field_name) {
        return -1;
    }
    game_prototype_component *component = find_component(prototype, component_name);
    const type_field *field = component
                                ? find_field(component->desc->type, field_name)
                                : NULL;
    if (!field || field->type != value.type) {
        return -1;
    }
    if (field->type == PIN_NAME || field->type == PIN_STR) {
        value.str = own_string(prototype, value.str);
    }
    pin_write_field(component->data, field, value);
    return 0;
}

int game_prototype_add_component(game_prototype *prototype, const ecs_component_desc *desc)
{
    if (!prototype || !desc || !desc->name || find_component(prototype, desc->name)) {
        return -1;
    }
    game_prototype_component component = {
        .desc = desc,
        .data = xcalloc(1, desc->element_size),
    };
    apush(prototype->components, component);
    return 0;
}

int game_prototype_remove_component(game_prototype *prototype, const char *component_name)
{
    if (!prototype || !component_name) {
        return -1;
    }
    for (int i = 0; i < acount(prototype->components); i++) {
        game_prototype_component *component = &prototype->components[i];
        if (strcmp(component->desc->name, component_name) != 0) {
            continue;
        }
        free(component->data);
        adel(prototype->components, i);
        return 0;
    }
    return -1;
}

int game_prototype_has_graph(const game_prototype *prototype, const char *asset_name)
{
    if (!prototype || !asset_name || !asset_name[0]) {
        return 0;
    }
    for (int i = 0; i < acount(prototype->graphs); i++) {
        if (strcmp(prototype->graphs[i], asset_name) == 0) {
            return 1;
        }
    }
    return 0;
}

int game_prototype_add_graph(game_prototype *prototype, const char *asset_name)
{
    if (!prototype || !asset_name || !asset_name[0] ||
        game_prototype_has_graph(prototype, asset_name)) {
        return -1;
    }
    apush(prototype->graphs, own_string(prototype, asset_name));
    return 0;
}

int game_prototype_remove_graph(game_prototype *prototype, const char *asset_name)
{
    if (!prototype || !asset_name) {
        return -1;
    }
    for (int i = 0; i < acount(prototype->graphs); i++) {
        if (strcmp(prototype->graphs[i], asset_name) != 0) {
            continue;
        }
        adel(prototype->graphs, i);
        return 0;
    }
    return -1;
}
