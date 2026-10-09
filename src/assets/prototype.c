#include "assets/prototype.h"

#include "ecs/component_catalog.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/asset_value.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/catalog.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROTOTYPE_DEFAULT_MAX_DEPTH 64

/* A prototype's identity defaults to one derived from its name, so documents
 * record it only when it differs. */
static char *name_identity(const char *name)
{
    char identity[32];
    snprintf(identity, sizeof identity, "prototype-%016llx", (unsigned long long)ecs_component_marker_id(name));
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
        .id = ecs_component_marker_id(prototype->identity),
        .key = prototype->identity,
        .name = prototype->tag.name,
        .element_size = 1,
        .type = &TYPE_MARKER,
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

void game_prototype_owned_strings_free(char **strings)
{
    for (int i = 0; i < acount(strings); i++) {
        free(strings[i]);
    }
    afree(strings);
}

const ecs_component_desc *game_prototype_registry_find(
    const game_prototype_registry *registry,
    const char *name
)
{
    return ecs_component_catalog_find(
        (ecs_component_catalog){ registry->components, registry->component_count },
        name
    );
}

static const type_desc *find_type(const game_prototype_registry *registry, const char *name)
{
    for (int i = 0; i < registry->component_count; i++) {
        const ecs_component_desc *desc = registry->components[i];
        if (desc && desc->type && strcmp(desc->type->name, name) == 0) {
            return desc->type;
        }
    }
    return type_catalog_find(name);
}

static game_prototype_component *find_component_in(game_prototype_component *components, const char *name)
{
    for (int i = 0; i < acount(components); i++) {
        const ecs_component_desc *desc = components[i].desc;
        if (strcmp(desc->name, name) == 0 ||
            (desc->key && strcmp(desc->key, name) == 0)) {
            return &components[i];
        }
    }
    return NULL;
}

static game_prototype_component *find_component(game_prototype *prototype, const char *name)
{
    return find_component_in(prototype->components, name);
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

static int read_signed(av_value *value, int64_t *out)
{
    if (av_is_sint(value)) {
        *out = av_get_sint(value);
        return 0;
    }
    if (av_is_uint(value) && av_get_uint(value) <= INT64_MAX) {
        *out = (int64_t)av_get_uint(value);
        return 0;
    }
    return -1;
}

static int read_unsigned(av_value *value, uint64_t *out)
{
    if (av_is_uint(value)) {
        *out = av_get_uint(value);
        return 0;
    }
    if (av_is_sint(value) && av_get_sint(value) >= 0) {
        *out = (uint64_t)av_get_sint(value);
        return 0;
    }
    return -1;
}

static int read_signed_range(av_value *value, int64_t minimum, int64_t maximum, int64_t *out)
{
    int64_t number;
    if (read_signed(value, &number) || number < minimum || number > maximum) {
        return -1;
    }
    *out = number;
    return 0;
}

static int read_unsigned_range(av_value *value, uint64_t maximum, uint64_t *out)
{
    uint64_t number;
    if (read_unsigned(value, &number) || number > maximum) {
        return -1;
    }
    *out = number;
    return 0;
}

static int parse_fields(
    game_prototype *prototype,
    const game_prototype_registry *registry,
    void *base,
    const type_desc *type,
    av_value *object,
    char *error,
    size_t error_size
);

static int parse_field(
    game_prototype *prototype,
    const game_prototype_registry *registry,
    void *base,
    const type_field *field,
    av_value *json,
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
            result = !av_is_bool(json);
            value.b = result ? false : av_get_bool(json);
            break;
        case PIN_I8:
            result = read_signed_range(json, INT8_MIN, INT8_MAX, &signed_value);
            if (!result) {
                value.i8 = (int8_t)signed_value;
            }
            break;
        case PIN_I16:
            result = read_signed_range(json, INT16_MIN, INT16_MAX, &signed_value);
            if (!result) {
                value.i16 = (int16_t)signed_value;
            }
            break;
        case PIN_I32:
            result = read_signed_range(json, INT32_MIN, INT32_MAX, &signed_value);
            if (!result) {
                value.i = (int32_t)signed_value;
            }
            break;
        case PIN_I64:
            result = read_signed(json, &signed_value);
            value.i64 = signed_value;
            break;
        case PIN_ENUM: {
            int label_value = 0;
            bool label = av_is_str(json) &&
                         type_catalog_enum_value(field->type_name, av_get_str(json), &label_value);
            result = label ? 0 : read_signed_range(json, INT32_MIN, INT32_MAX, &signed_value);
            if (!result) {
                value.e = label ? label_value : (int32_t)signed_value;
            }
            break;
        }
        case PIN_U8:
            result = read_unsigned_range(json, UINT8_MAX, &unsigned_value);
            if (!result) {
                value.u8 = (uint8_t)unsigned_value;
            }
            break;
        case PIN_U16:
            result = read_unsigned_range(json, UINT16_MAX, &unsigned_value);
            if (!result) {
                value.u16 = (uint16_t)unsigned_value;
            }
            break;
        case PIN_U32:
            result = read_unsigned_range(json, UINT32_MAX, &unsigned_value);
            if (!result) {
                value.u = (uint32_t)unsigned_value;
            }
            break;
        case PIN_U64:
            result = read_unsigned(json, &unsigned_value);
            value.u64 = unsigned_value;
            break;
        case PIN_F32:
            result = !av_is_num(json);
            value.f = result ? 0.0f : (float)av_get_num(json);
            break;
        case PIN_F64:
            result = !av_is_num(json);
            value.d = result ? 0.0 : av_get_num(json);
            break;
        case PIN_NAME:
        case PIN_STR:
            result = !av_is_str(json);
            value.str = result ? NULL : own_string(prototype, av_get_str(json));
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
        /* Runtime handles have no authored identity. A saved component clears
         * them; graph events supply live handles after instantiation. */
        case PIN_HANDLE: result = !av_is_null(json); break;
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
    av_value *object,
    char *error,
    size_t error_size
)
{
    if (!av_is_obj(object)) {
        set_error(error, error_size, "value for %s must be an object", type->name);
        return -1;
    }
    av_obj_iter iterator = av_obj_iter_with(object);
    av_value *key;
    while ((key = av_obj_iter_next(&iterator))) {
        const char *name = av_get_str(key);
        const type_field *field = find_field(type, name);
        if (!field) {
            set_error(error, error_size, "component %s has unknown field %s", type->name, name);
            return -1;
        }
        if (parse_field(prototype, registry, base, field, av_obj_iter_get_val(key), error, error_size)) {
            return -1;
        }
    }
    return 0;
}

static int parse_components(
    game_prototype *prototype,
    game_prototype_component **components,
    av_value *object,
    char *error,
    size_t error_size
)
{
    if (!object) {
        return 0;
    }
    if (!av_is_obj(object)) {
        set_error(error, error_size, "components must be an object");
        return -1;
    }
    av_obj_iter iterator = av_obj_iter_with(object);
    av_value *key;
    while ((key = av_obj_iter_next(&iterator))) {
        const char *name = av_get_str(key);
        const ecs_component_desc *desc = game_prototype_registry_find(prototype->registry, name);
        av_value *value = av_obj_iter_get_val(key);
        if (!desc) {
            set_error(error, error_size, "component %s is unavailable", name);
            return -1;
        }
        if (desc->derived_from) {
            set_error(error, error_size, "component %s is computed by the engine and cannot be authored", name);
            return -1;
        }
        game_prototype_component *component = find_component_in(*components, name);
        if (!component) {
            component = &(game_prototype_component){
                .desc = desc,
                .data = xcalloc(1, desc->element_size),
            };
            apush(*components, *component);
            component = &alast(*components);
        }
        if (parse_fields(prototype, prototype->registry, component->data, desc->type, value, error, error_size)) {
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

/* Counts what `src` read toward `dst`'s load; false once the load reads too
 * many documents. */
static bool expand(game_prototype *dst, const game_prototype *src, char *error, size_t error_size)
{
    dst->expanded += 1 + src->expanded;
    if (dst->expanded < GAME_PROTOTYPE_EXPANSION_MAX) {
        return true;
    }
    set_error(error, error_size, "prototype %s reads more than %d prototypes through its includes and children", dst->name, GAME_PROTOTYPE_EXPANSION_MAX);
    return false;
}

/* An include's children come before the including prototype's own. */
static bool merge_children(game_prototype *dst, game_prototype *src, char *error, size_t error_size)
{
    if (!src->children) {
        return true;
    }
    if (!dst->children) {
        dst->children = src->children;
        src->children = NULL;
    } else if (game_scene_document_take_entities(dst->children, src->children, error, error_size)) {
        return false;
    }
    for (int i = 0; i < acount(src->child_prototypes); i++) {
        apush(dst->child_prototypes, src->child_prototypes[i]);
    }
    aclear(src->child_prototypes);
    return true;
}

static bool merge_prototype(game_prototype *dst, game_prototype *src, char *error, size_t error_size)
{
    if (!merge_children(dst, src, error, error_size)) {
        return false;
    }
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
    return true;
}

static game_prototype *load_document(
    const char *text,
    const game_prototype_registry *registry,
    char *const *stack,
    int stack_count,
    char *error,
    size_t error_size
);

/* The prototype the resolver gives for `name`, loaded under `stack`; `role`
 * says in errors what named it. */
static game_prototype *load_named(
    const game_prototype_registry *registry,
    const char *role,
    const char *name,
    char *const *stack,
    int stack_count,
    char *error,
    size_t error_size
)
{
    const char *source = registry->include(name, registry->include_user);
    if (!source) {
        set_error(error, error_size, "%s %s is unavailable", role, name);
        return NULL;
    }
    game_prototype *prototype = load_document(source, registry, stack, stack_count, error, error_size);
    if (prototype && strcmp(prototype->name, name)) {
        set_error(error, error_size, "%s %s is named %s", role, name, prototype->name);
        game_prototype_free(prototype);
        return NULL;
    }
    return prototype;
}

/* Folds the included prototype's content into `prototype`, before its own. */
static bool merge_include(
    game_prototype *prototype,
    const char *name,
    char *const *stack,
    int stack_count,
    char *error,
    size_t error_size
)
{
    game_prototype *base = load_named(
        prototype->registry,
        "prototype include",
        name,
        stack,
        stack_count,
        error,
        error_size
    );
    bool merged = base && expand(prototype, base, error, error_size) &&
                  merge_prototype(prototype, base, error, error_size);
    game_prototype_free(base);
    return merged;
}

/* What the child spawns as: its prototype with its components and behaviors,
 * under its own name. */
static game_prototype *resolve_child(
    const game_prototype_registry *registry,
    const game_scene_entity *child,
    char *const *stack,
    int stack_count,
    char *error,
    size_t error_size
)
{
    game_prototype *prototype = load_named(
        registry,
        "child prototype",
        child->prototype,
        stack,
        stack_count,
        error,
        error_size
    );
    if (!prototype) {
        return NULL;
    }
    if (parse_components(prototype, &prototype->components, child->components, error, error_size)) {
        game_prototype_free(prototype);
        return NULL;
    }
    if (child->own_behaviors) {
        aclear(prototype->graphs);
        for (int i = 0; i < acount(child->behaviors); i++) {
            apush(prototype->graphs, own_string(prototype, child->behaviors[i]));
        }
    }
    free(prototype->name);
    prototype->name = xstrdup(child->name);
    return prototype;
}

/* Reads the document's children after any its includes gave it and, unless
 * `authored`, resolves each. */
static bool read_children(
    game_prototype *prototype,
    av_value *children,
    bool authored,
    char *const *stack,
    int stack_count,
    char *error,
    size_t error_size
)
{
    if (!authored && !prototype->registry->include) {
        set_error(error, error_size, "prototype children need a resolver");
        return false;
    }
    if (!prototype->children) {
        prototype->children = game_scene_document_new(prototype->name);
    }
    int first = acount(prototype->children->entities);
    char reason[256];
    if (game_scene_document_read_entities(prototype->children, children, reason, sizeof reason)) {
        set_error(error, error_size, "children of prototype %s: %s", prototype->name, reason);
        return false;
    }
    for (int i = first; !authored && i < acount(prototype->children->entities); i++) {
        game_prototype *child = resolve_child(
            prototype->registry,
            &prototype->children->entities[i],
            stack,
            stack_count,
            error,
            error_size
        );
        if (!child) {
            return false;
        }
        apush(prototype->child_prototypes, child);
        if (!expand(prototype, child, error, error_size)) {
            return false;
        }
    }
    return true;
}

/* With `authored`, the included prototypes are named but not loaded, so the
 * result holds only what the document itself says. */
static game_prototype *load_value_document(
    av_value *root,
    const game_prototype_registry *registry,
    bool authored,
    char *const *stack,
    int stack_count,
    char *error,
    size_t error_size
)
{
    av_value *name_value = av_is_obj(root) ? av_obj_get(root, "name") : NULL;
    if (!av_is_str(name_value) || !av_get_len(name_value)) {
        set_error(error, error_size, "prototype name is required");
        return NULL;
    }
    const char *name = av_get_str(name_value);
    if (contains_name(stack, stack_count, name)) {
        set_error(error, error_size, "prototype include cycle at %s", name);
        return NULL;
    }
    game_prototype *prototype = (game_prototype *)xcalloc(1, sizeof *prototype);
    prototype->name = xstrdup(name);
    prototype->identity = name_identity(name);
    prototype->authored = authored;
    prototype->registry_storage = *registry;
    prototype->registry = &prototype->registry_storage;
    av_value *identity = av_obj_get(root, "identity");
    if (identity) {
        if (!av_is_str(identity) || !av_get_len(identity)) {
            set_error(error, error_size, "prototype identity must be a non-empty string");
            game_prototype_free(prototype);
            return NULL;
        }
        free(prototype->identity);
        prototype->identity = xstrdup(av_get_str(identity));
    }
    setup_tag(prototype);
    char *next_stack[64];
    if (stack_count >= (int)(sizeof next_stack / sizeof next_stack[0])) {
        set_error(error, error_size, "prototype include depth exceeded");
        game_prototype_free(prototype);
        return NULL;
    }
    for (int i = 0; i < stack_count; i++) {
        next_stack[i] = (char *)stack[i];
    }
    next_stack[stack_count] = prototype->name;

    av_value *includes = av_obj_get(root, "includes");
    if (includes) {
        if (!av_is_arr(includes) || (!authored && !registry->include)) {
            set_error(error, error_size, "prototype includes need a resolver");
            game_prototype_free(prototype);
            return NULL;
        }
        av_arr_iter iterator = av_arr_iter_with(includes);
        av_value *include;
        while ((include = av_arr_iter_next(&iterator))) {
            if (!av_is_str(include)) {
                set_error(error, error_size, "prototype include name is not a string");
                game_prototype_free(prototype);
                return NULL;
            }
            if (!authored &&
                !merge_include(prototype, av_get_str(include), next_stack, stack_count + 1, error, error_size)) {
                game_prototype_free(prototype);
                return NULL;
            }
            apush(prototype->includes, own_string(prototype, av_get_str(include)));
        }
    }
    if (parse_components(prototype, &prototype->components, av_obj_get(root, "components"), error, error_size) ||
        parse_components(
            prototype,
            &prototype->detached_components,
            av_obj_get(root, "detached_components"),
            error,
            error_size
        )) {
        game_prototype_free(prototype);
        return NULL;
    }
    for (int i = 0; i < acount(prototype->detached_components); i++) {
        const char *name = prototype->detached_components[i].desc->name;
        if (find_component(prototype, name)) {
            set_error(error, error_size, "component %s is both attached and detached", name);
            game_prototype_free(prototype);
            return NULL;
        }
    }
    av_value *children = av_obj_get(root, "children");
    if (children &&
        !read_children(prototype, children, authored, next_stack, stack_count + 1, error, error_size)) {
        game_prototype_free(prototype);
        return NULL;
    }
    av_value *graphs = av_obj_get(root, "behaviors");
    if (graphs) {
        if (!av_is_arr(graphs)) {
            set_error(error, error_size, "behaviors must be an array");
            game_prototype_free(prototype);
            return NULL;
        }
        av_arr_iter iterator = av_arr_iter_with(graphs);
        av_value *graph;
        while ((graph = av_arr_iter_next(&iterator))) {
            if (!av_is_str(graph)) {
                set_error(error, error_size, "behavior graph name is not a string");
                game_prototype_free(prototype);
                return NULL;
            }
            apush(prototype->graphs, own_string(prototype, av_get_str(graph)));
        }
    }
    return prototype;
}

static game_prototype *load_document(
    const char *text,
    const game_prototype_registry *registry,
    char *const *stack,
    int stack_count,
    char *error,
    size_t error_size
)
{
    av_doc *doc = asset_text_read(text, error, error_size);
    game_prototype *result = doc ? load_value_document(
                                       av_doc_root(doc),
                                       registry,
                                       false,
                                       stack,
                                       stack_count,
                                       error,
                                       error_size
                                   )
                                 : NULL;
    av_doc_free(doc);
    return result;
}

game_prototype *game_prototype_load_value(
    av_value *value,
    const game_prototype_registry *registry,
    char *error,
    size_t error_size
)
{
    return load_value_document(value, registry, false, NULL, 0, error, error_size);
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

game_prototype *game_prototype_load_authored(
    const char *text,
    const game_prototype_registry *registry,
    char *error,
    size_t error_size
)
{
    av_doc *doc = asset_text_read(text, error, error_size);
    game_prototype *result = doc
                               ? load_value_document(av_doc_root(doc), registry, true, NULL, 0, error, error_size)
                               : NULL;
    av_doc_free(doc);
    return result;
}

game_prototype *game_prototype_clone(const game_prototype *prototype, char *error, size_t error_size)
{
    if (!prototype || !prototype->registry) {
        set_error(error, error_size, "invalid prototype clone arguments");
        return NULL;
    }
    av_value *value = game_prototype_to_value(prototype);
    game_prototype *copy = value
                             ? game_prototype_load_value(value, prototype->registry, error, error_size)
                             : NULL;
    av_value_free(value);
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

static int game_prototype_register_tag(const game_prototype *prototype, ecs_world *world)
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

game_prototype *game_prototype_new(const char *name)
{
    assert(name && name[0]);
    game_prototype *prototype = (game_prototype *)xcalloc(1, sizeof *prototype);
    prototype->name = xstrdup(name);
    prototype->identity = name_identity(name);
    prototype->registry = &prototype->registry_storage;
    setup_tag(prototype);
    return prototype;
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
    game_prototype *prototype = game_prototype_new(name);
    prototype->registry_storage = *registry;
    for (int i = 0; i < registry->component_count; i++) {
        const ecs_component_desc *desc = registry->components[i];
        if (!desc || desc->derived_from ||
            !ecs_component_has(world, entity, desc->id)) {
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
    for (int i = 0; i < acount(prototype->detached_components); i++) {
        free(prototype->detached_components[i].data);
    }
    afree(prototype->detached_components);
    afree(prototype->graphs);
    afree(prototype->includes);
    game_scene_document_free(prototype->children);
    for (int i = 0; i < acount(prototype->child_prototypes); i++) {
        game_prototype_free(prototype->child_prototypes[i]);
    }
    afree(prototype->child_prototypes);
    game_prototype_owned_strings_free(prototype->strings);
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
    av_doc *doc,
    av_value *object,
    const game_prototype *prototype,
    const void *base,
    const type_desc *type,
    bool sparse
);

/* Loading starts every component from zero, so sparse documents omit zeros. */
static bool zero_field(const void *base, const type_field *field)
{
    pin_value value;
    pin_read_field(base, field, &value);
    switch (field->type) {
        case PIN_BOOL: return !value.b;
        case PIN_I8: return !value.i8;
        case PIN_I16: return !value.i16;
        case PIN_I32: return !value.i;
        case PIN_I64: return !value.i64;
        case PIN_ENUM: return !value.e;
        case PIN_U8: return !value.u8;
        case PIN_U16: return !value.u16;
        case PIN_U32: return !value.u;
        case PIN_U64: return !value.u64;
        case PIN_F32: return value.f == 0.0f && !signbit(value.f);
        case PIN_F64: return value.d == 0.0 && !signbit(value.d);
        case PIN_NAME:
        case PIN_STR: return !value.str;
        case PIN_STRUCT: return !value.ptr;
        case PIN_HANDLE:
        case PIN_FLOW: return true;
    }
    return false;
}

static av_value *write_value(
    av_doc *doc,
    const game_prototype *prototype,
    const void *base,
    const type_field *field
)
{
    pin_value value;
    pin_read_field(base, field, &value);
    switch (field->type) {
        case PIN_BOOL: return av_bool(doc, value.b);
        case PIN_I8: return av_sint(doc, value.i8);
        case PIN_I16: return av_sint(doc, value.i16);
        case PIN_I32: return av_sint(doc, value.i);
        case PIN_I64: return av_sint(doc, value.i64);
        case PIN_ENUM: {
            const char *label = type_catalog_enum_label(field->type_name, value.e);
            return label ? av_str(doc, label) : av_sint(doc, value.e);
        }
        case PIN_U8: return av_uint(doc, value.u8);
        case PIN_U16: return av_uint(doc, value.u16);
        case PIN_U32:
            return field->color ? av_color(doc, value.u) : av_uint(doc, value.u);
        case PIN_U64: return av_uint(doc, value.u64);
        case PIN_F32: return av_float(doc, value.f);
        case PIN_F64: return av_real(doc, value.d);
        case PIN_NAME:
        case PIN_STR: return av_str(doc, value.str ? value.str : "");
        case PIN_STRUCT: {
            av_value *object = av_obj(doc);
            const type_desc *nested = find_type(prototype->registry, field->type_name);
            if (nested && value.ptr) {
                write_struct_fields(doc, object, prototype, value.ptr, nested, false);
            }
            return object;
        }
        case PIN_HANDLE:
        case PIN_FLOW: return av_null(doc);
    }
    return av_null(doc);
}

static void write_struct_fields(
    av_doc *doc,
    av_value *object,
    const game_prototype *prototype,
    const void *base,
    const type_desc *type,
    bool sparse
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
        if (sparse && zero_field(base, field)) {
            continue;
        }
        pin_value value;
        pin_read_field(base, field, &value);
        av_value *json = NULL;
        if (field->type == PIN_STRUCT && value.ptr) {
            json = av_obj(doc);
            write_struct_fields(
                doc,
                json,
                prototype,
                value.ptr,
                find_type(prototype->registry, field->type_name),
                sparse
            );
            if (sparse && !av_obj_size(json)) {
                continue;
            }
        } else {
            json = write_value(doc, prototype, base, field);
        }
        av_obj_add_val(doc, object, field->name, json);
    }
    afree(order);
}

static av_value *write_components(
    av_doc *doc,
    const game_prototype *prototype,
    game_prototype_component *values,
    bool sparse
)
{
    av_value *components = av_obj(doc);
    int *order = NULL;
    for (int i = 0; i < acount(values); i++) {
        apush(order, i);
    }
    for (int i = 1; i < acount(order); i++) {
        int value = order[i];
        int at = i;
        while (at &&
               strcmp(values[order[at - 1]].desc->name, values[value].desc->name) > 0) {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = value;
    }
    for (int i = 0; i < acount(order); i++) {
        game_prototype_component *component = &values[order[i]];
        av_value *object = av_obj(doc);
        write_struct_fields(
            doc,
            object,
            prototype,
            component->data,
            component->desc->type,
            sparse
        );
        av_obj_add_val(
            doc,
            components,
            component->desc->key ? component->desc->key : component->desc->name,
            object
        );
    }
    afree(order);
    return components;
}

av_value *game_prototype_to_value(const game_prototype *prototype)
{
    if (!prototype) {
        return NULL;
    }
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_strcpy(doc, root, "name", prototype->name);
    char *identity = name_identity(prototype->name);
    if (strcmp(identity, prototype->identity) != 0) {
        av_obj_add_strcpy(doc, root, "identity", prototype->identity);
    }
    free(identity);
    if (prototype->authored && acount(prototype->includes)) {
        av_value *includes = av_arr(doc);
        for (int i = 0; i < acount(prototype->includes); i++) {
            av_arr_add_strcpy(doc, includes, prototype->includes[i]);
        }
        av_obj_add_val(doc, root, "includes", includes);
    }
    av_obj_add_val(
        doc,
        root,
        "components",
        write_components(doc, prototype, prototype->components, true)
    );
    if (acount(prototype->detached_components)) {
        av_obj_add_val(
            doc,
            root,
            "detached_components",
            write_components(doc, prototype, prototype->detached_components, true)
        );
    }
    if (acount(prototype->graphs)) {
        char **graphs = NULL;
        for (int i = 0; i < acount(prototype->graphs); i++) {
            apush(graphs, prototype->graphs[i]);
        }
        asort(graphs, compare_strings);
        av_value *array = av_arr(doc);
        for (int i = 0; i < acount(graphs); i++) {
            av_arr_add_strcpy(doc, array, graphs[i]);
        }
        av_obj_add_val(doc, root, "behaviors", array);
        afree(graphs);
    }
    if (prototype->children && acount(prototype->children->entities)) {
        ecs_component_catalog catalog = { prototype->registry->components,
                                          prototype->registry->component_count };
        av_obj_add_val(
            doc,
            root,
            "children",
            game_scene_document_entities_value(doc, prototype->children, catalog)
        );
    }
    av_value *result = av_value_copy(av_doc_root(doc));
    av_doc_free(doc);
    return result;
}

char *game_prototype_save(const game_prototype *prototype)
{
    av_value *value = game_prototype_to_value(prototype);
    char *text = value ? asset_text_write(value) : NULL;
    av_value_free(value);
    return text;
}

av_value *game_prototype_components_value(const game_prototype *prototype)
{
    if (!prototype) {
        return NULL;
    }
    av_doc *doc = av_doc_new();
    av_doc_set_root(doc, write_components(doc, prototype, prototype->components, false));
    av_value *result = av_value_copy(av_doc_root(doc));
    av_doc_free(doc);
    return result;
}

av_value *game_prototype_component_value(const game_prototype *prototype, const char *name)
{
    const game_prototype_component *component = find_component((game_prototype *)prototype, name);
    game_prototype_component *values = NULL;
    if (component) {
        apush(values, *component);
    }
    av_doc *doc = av_doc_new();
    av_doc_set_root(doc, write_components(doc, prototype, values, false));
    av_value *json = av_value_copy(av_doc_root(doc));
    av_doc_free(doc);
    afree(values);
    return json;
}

av_value *game_prototype_entity_component_value(
    const ecs_world *world,
    ecs_entity entity,
    const ecs_component_desc *desc,
    const game_prototype_registry *registry
)
{
    if (!world || !desc || !registry || !ecs_entity_is_alive(world, entity)) {
        return NULL;
    }
    const void *data = ecs_component_data_const(world, entity, desc->id);
    if (!data) {
        return NULL;
    }
    game_prototype schema = { .registry = registry };
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_value *fields = av_obj(doc);
    av_doc_set_root(doc, root);
    write_struct_fields(doc, fields, &schema, data, desc->type, false);
    av_obj_add_val(doc, root, desc->key ? desc->key : desc->name, fields);
    av_value *json = av_value_copy(av_doc_root(doc));
    av_doc_free(doc);
    return json;
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

static av_value *component_overrides_value(const game_prototype *base, const game_prototype_component *current_components)
{
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_value *components = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_val(doc, root, "components", components);
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
        const void *current = NULL;
        for (int j = 0; j < acount(current_components); j++) {
            if (current_components[j].desc->id == component->desc->id) {
                current = current_components[j].data;
                break;
            }
        }
        if (!current) {
            av_obj_add_val(
                doc,
                components,
                component->desc->key ? component->desc->key : component->desc->name,
                av_null(doc)
            );
            continue;
        }
        av_value *fields = NULL;
        for (int field_index = 0; field_index < component->desc->type->field_count;
             field_index++) {
            const type_field *field = &component->desc->type->fields[field_index];
            if (field_equal(base, field, component->data, current)) {
                continue;
            }
            if (!fields) {
                fields = av_obj(doc);
            }
            av_obj_add_val(doc, fields, field->name, write_value(doc, base, current, field));
        }
        if (fields) {
            av_obj_add_val(
                doc,
                components,
                component->desc->key ? component->desc->key : component->desc->name,
                fields
            );
        }
    }
    afree(order);
    for (int i = 0; i < acount(current_components); i++) {
        const game_prototype_component *component = &current_components[i];
        bool existed = false;
        for (int j = 0; j < acount(base->components); j++) {
            existed |= base->components[j].desc->id == component->desc->id;
        }
        if (existed) {
            continue;
        }
        av_value *fields = av_obj(doc);
        write_struct_fields(doc, fields, base, component->data, component->desc->type, false);
        av_obj_add_val(
            doc,
            components,
            component->desc->key ? component->desc->key : component->desc->name,
            fields
        );
    }
    av_value *result = av_value_copy(components);
    av_doc_free(doc);
    return result;
}

av_value *game_prototype_overrides_value(const game_prototype *base, const game_prototype *current)
{
    if (!base || !current) {
        return NULL;
    }
    return component_overrides_value(base, current->components);
}

av_value *game_prototype_capture_overrides_value(
    const game_prototype *base,
    const ecs_world *world,
    ecs_entity entity
)
{
    if (!base || !world || !ecs_entity_is_alive(world, entity)) {
        return NULL;
    }
    game_prototype_component *components = NULL;
    for (int i = 0; i < base->registry->component_count; i++) {
        const ecs_component_desc *desc = base->registry->components[i];
        if (desc->derived_from || !ecs_component_has(world, entity, desc->id)) {
            continue;
        }
        game_prototype_component component = {
            .desc = desc,
            .data = (void *)ecs_component_data_const(world, entity, desc->id),
        };
        apush(components, component);
    }
    av_value *json = component_overrides_value(base, components);
    afree(components);
    return json;
}

static int apply_override_fields(
    game_prototype *parser,
    const game_prototype_registry *registry,
    void *base,
    const type_desc *type,
    av_value *fields,
    char *error,
    size_t error_size
)
{
    if (!av_is_obj(fields)) {
        set_error(error, error_size, "value for %s override must be an object", type->name);
        return -1;
    }
    av_obj_iter iterator = av_obj_iter_with(fields);
    av_value *key;
    while ((key = av_obj_iter_next(&iterator))) {
        const type_field *field = find_field(type, av_get_str(key));
        if (!field) {
            set_error(error, error_size, "type %s has unknown override field %s", type->name, av_get_str(key));
            return -1;
        }
        av_value *value = av_obj_iter_get_val(key);
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
    av_value *fields,
    char *error,
    size_t error_size
)
{
    void *base = ecs_component_data(world, entity, desc->id);
    if (!base || !av_is_obj(fields)) {
        set_error(error, error_size, "component %s override is invalid", desc->name);
        return -1;
    }
    return apply_override_fields(parser, registry, base, desc->type, fields, error, error_size);
}

int game_prototype_apply_components_value(
    ecs_world *world,
    ecs_entity entity,
    av_value *components,
    const game_prototype_registry *registry,
    char ***owned_strings,
    char *error,
    size_t error_size
)
{
    if (!world || !registry || !components || !owned_strings ||
        !ecs_entity_is_alive(world, entity)) {
        set_error(error, error_size, "invalid component override arguments");
        return -1;
    }
    av_value *root = components;
    if (!root || !av_is_obj(root)) {
        set_error(error, error_size, "component overrides must be an object");
        return -1;
    }
    game_prototype parser = { .registry = registry };
    av_obj_iter iterator = av_obj_iter_with(root);
    av_value *key;
    while ((key = av_obj_iter_next(&iterator))) {
        const ecs_component_desc *desc = game_prototype_registry_find(registry, av_get_str(key));
        if (!desc || desc->derived_from) {
            set_error(
                error,
                error_size,
                "override component %s is unknown or computed by the engine",
                av_get_str(key)
            );
            for (int i = 0; i < acount(parser.strings); i++) {
                apush(*owned_strings, parser.strings[i]);
            }
            afree(parser.strings);
            return -1;
        }
        if (av_is_null(av_obj_iter_get_val(key))) {
            if (ecs_component_has(world, entity, desc->id) &&
                ecs_world_record_remove(world, entity, desc->id, 0) != ECS_OK) {
                set_error(error, error_size, "override component %s could not be removed", desc->name);
                for (int i = 0; i < acount(parser.strings); i++) {
                    apush(*owned_strings, parser.strings[i]);
                }
                afree(parser.strings);
                return -1;
            }
            continue;
        }
        if (!ecs_component_has(world, entity, desc->id) &&
            ecs_world_record_add(world, entity, desc->id, NULL, 0) != ECS_OK) {
            set_error(error, error_size, "override component %s could not be added", desc->name);
            for (int i = 0; i < acount(parser.strings); i++) {
                apush(*owned_strings, parser.strings[i]);
            }
            afree(parser.strings);
            return -1;
        }
    }
    if (ecs_world_apply_commands(world) != ECS_OK) {
        set_error(error, error_size, "component override application failed");
        for (int i = 0; i < acount(parser.strings); i++) {
            apush(*owned_strings, parser.strings[i]);
        }
        afree(parser.strings);
        return -1;
    }
    iterator = av_obj_iter_with(root);
    while ((key = av_obj_iter_next(&iterator))) {
        const ecs_component_desc *desc = game_prototype_registry_find(registry, av_get_str(key));
        if (av_is_null(av_obj_iter_get_val(key))) {
            continue;
        }
        if (apply_component_override(&parser, world, entity, registry, desc, av_obj_iter_get_val(key), error, error_size) !=
            0) {
            for (int i = 0; i < acount(parser.strings); i++) {
                apush(*owned_strings, parser.strings[i]);
            }
            afree(parser.strings);
            return -1;
        }
    }
    for (int i = 0; i < acount(parser.strings); i++) {
        apush(*owned_strings, parser.strings[i]);
    }
    afree(parser.strings);
    return 0;
}

/* Creates the prototype's entity under `parent`, unless that is zero, then its
 * children under it, appending each entity with what it spawned as. */
static int create_tree(
    const game_prototype *prototype,
    ecs_world *world,
    ecs_entity parent,
    ecs_entity **entities,
    const game_prototype ***prototypes,
    char *error,
    size_t error_size
)
{
    if (game_prototype_register_tag(prototype, world) != ECS_OK) {
        set_error(error, error_size, "prototype identity tag could not be registered");
        return ECS_ERR_DUPLICATE;
    }
    ecs_entity entity = ecs_world_record_create(world, 0);
    if (!entity.slot || ecs_world_apply_commands(world) != ECS_OK) {
        set_error(error, error_size, "prototype entity allocation failed");
        return ECS_ERR_CAPACITY;
    }
    apush(*entities, entity);
    apush(*prototypes, prototype);
    for (int i = 0; i < acount(prototype->components); i++) {
        const game_prototype_component *component = &prototype->components[i];
        if (ecs_world_record_add(world, entity, component->desc->id, NULL, (uint64_t)i + 1)) {
            set_error(
                error,
                error_size,
                "component %s could not be added",
                component->desc->name
            );
            return ECS_ERR_COMPONENT;
        }
    }
    if (ecs_world_record_add(world, entity, prototype->tag.desc.id, NULL, 0) ||
        (parent.slot &&
         ecs_world_record_add_pair(world, entity, ECS_CHILD_OF, parent, 0))) {
        set_error(
            error,
            error_size,
            "prototype %s could not be placed",
            prototype->name
        );
        return ECS_ERR_COMPONENT;
    }
    if (ecs_world_apply_commands(world) != ECS_OK) {
        set_error(error, error_size, "prototype component application failed");
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
                return ECS_ERR_COMPONENT;
            }
        }
    }
    int count = prototype->children ? acount(prototype->children->entities) : 0;
    assert(acount(prototype->child_prototypes) == count);
    int *children = NULL; /* where each child's entity lands in `entities` */
    int result = ECS_OK;
    for (int i = 0; !result && i < count; i++) {
        int index = prototype->children->entities[i].parent;
        apush(children, acount(*entities));
        result = create_tree(
            prototype->child_prototypes[i],
            world,
            index < 0 ? entity : (*entities)[children[index]],
            entities,
            prototypes,
            error,
            error_size
        );
    }
    afree(children);
    return result;
}

/* Every entity exists before any behavior attaches; without hooks none do. */
static int spawn_tree(
    const game_prototype *prototype,
    ecs_world *world,
    const game_prototype_spawn_hooks *hooks,
    ecs_entity *out_entity,
    char *error,
    size_t error_size
)
{
    ecs_entity *entities = NULL;
    const game_prototype **prototypes = NULL;
    int result = create_tree(prototype, world, (ecs_entity){ 0 }, &entities, &prototypes, error, error_size);
    for (int i = 0; !result && hooks && i < acount(entities); i++) {
        const game_prototype *spawned = prototypes[i];
        for (int j = 0; !result && j < acount(spawned->graphs); j++) {
            if (!hooks->attach) {
                set_error(error, error_size, "prototype has behavior graphs but no attach hook");
                result = ECS_ERR_ARGUMENT;
            } else if (
                hooks->attach(world, entities[i], spawned->graphs[j], hooks->attach_user)
            ) {
                set_error(
                    error,
                    error_size,
                    "behavior graph %s could not attach",
                    spawned->graphs[j]
                );
                result = ECS_ERR_COMMAND;
            }
        }
    }
    if (result) {
        /* Children go first: one whose ChildOf was never applied would
         * outlive its parent. */
        ecs_world_clear_commands(world);
        for (int i = acount(entities) - 1; i >= 0; i--) {
            if (ecs_entity_is_alive(world, entities[i])) {
                ecs_world_destroy_entity(world, entities[i]);
            }
        }
    }
    if (!result) {
        *out_entity = entities[0];
    }
    afree(entities);
    afree(prototypes);
    return result;
}

int game_prototype_spawn_components(
    const game_prototype *prototype,
    ecs_world *world,
    ecs_entity *entity,
    char *error,
    size_t error_size
)
{
    if (!prototype || !world || !entity) {
        set_error(error, error_size, "invalid prototype spawn arguments");
        return ECS_ERR_ARGUMENT;
    }
    return spawn_tree(prototype, world, NULL, entity, error, error_size);
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
    static const game_prototype_spawn_hooks none = { 0 };
    return spawn_tree(prototype, world, hooks ? hooks : &none, out_entity, error, error_size);
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

int game_prototype_write_field_value(
    game_prototype *prototype,
    const char *component_name,
    const char *field_name,
    av_value *json,
    char *error,
    size_t error_size
)
{
    if (!prototype || !component_name || !field_name || !json) {
        set_error(error, error_size, "prototype field arguments are required");
        return -1;
    }
    game_prototype_component *component = find_component(prototype, component_name);
    const type_field *field = component
                                ? find_field(component->desc->type, field_name)
                                : NULL;
    if (!component) {
        set_error(error, error_size, "component %s is not attached", component_name);
        return -1;
    }
    if (!field) {
        set_error(error, error_size, "component %s has unknown field %s", component_name, field_name);
        return -1;
    }
    return parse_field(prototype, prototype->registry, component->data, field, json, error, error_size);
}

static av_value *rebase_fields(av_doc *doc, av_value *values, av_value *previous, av_value *current)
{
    if (!av_is_obj(current)) {
        return av_clone(doc, values);
    }
    av_value *result = av_clone(doc, current);
    av_obj_iter iterator = av_obj_iter_with(values);
    av_value *key;
    while ((key = av_obj_iter_next(&iterator))) {
        const char *name = av_get_str(key);
        av_value *value = av_obj_iter_get_val(key);
        av_value *old = av_obj_get(previous, name);
        av_value *next = av_obj_get(current, name);
        if (old && next && av_equals(old, value)) {
            continue;
        }
        av_value *merged = av_is_obj(value) && av_is_obj(old) && av_is_obj(next)
                             ? rebase_fields(doc, value, old, next)
                             : av_clone(doc, value);
        av_obj_remove_str(result, name);
        av_obj_add_val(doc, result, name, merged);
    }
    return result;
}

av_value *game_prototype_rebase_component_value(
    av_value *values,
    av_value *previous_defaults,
    av_value *current_defaults
)
{
    assert(values && previous_defaults && current_defaults);
    av_value *root[] = { values, previous_defaults, current_defaults };
    for (int i = 0; i < 3; i++) {
        if (!av_is_obj(root[i]) || av_obj_size(root[i]) > 1) {
            return NULL;
        }
    }
    av_obj_iter iterator = av_obj_iter_with(values);
    av_value *key = av_obj_iter_next(&iterator);
    if (!key || !av_is_obj(av_obj_iter_get_val(key))) {
        return NULL;
    }
    const char *name = av_get_str(key);
    av_value *defaults[2];
    for (int i = 0; i < 2; i++) {
        defaults[i] = av_obj_get(root[i + 1], name);
        if (av_obj_size(root[i + 1]) && !av_is_obj(defaults[i])) {
            return NULL;
        }
    }
    av_doc *doc = av_doc_new();
    av_value *object = av_obj(doc);
    av_doc_set_root(doc, object);
    av_obj_add_val(
        doc,
        object,
        name,
        rebase_fields(doc, av_obj_iter_get_val(key), defaults[0], defaults[1])
    );
    av_value *result = av_value_copy(object);
    av_doc_free(doc);
    return result;
}

static bool default_field_valid(const type_field *field, size_t row_size)
{
    size_t size;
    switch (field->type) {
        case PIN_BOOL: size = sizeof(bool); break;
        case PIN_I8:
        case PIN_U8: size = sizeof(uint8_t); break;
        case PIN_I16:
        case PIN_U16: size = sizeof(uint16_t); break;
        case PIN_I32:
        case PIN_U32: size = sizeof(uint32_t); break;
        case PIN_I64:
        case PIN_U64:
        case PIN_HANDLE: size = sizeof(uint64_t); break;
        case PIN_F32: size = sizeof(float); break;
        case PIN_F64: size = sizeof(double); break;
        case PIN_STR:
        case PIN_NAME: size = sizeof(const char *); break;
        case PIN_ENUM:
            if (!field->size || field->size > sizeof(int32_t)) {
                return false;
            }
            size = field->size;
            break;
        case PIN_STRUCT: size = field->size; break;
        default: return false;
    }
    return field->size == size && field->offset <= row_size &&
           size <= row_size - field->offset;
}

static int copy_default_fields(
    game_prototype *prototype,
    void *out,
    const void *initial,
    const type_desc *type,
    int depth
)
{
    if (!type || type->field_count < 0 || (type->field_count && !type->fields) ||
        depth >= PROTOTYPE_DEFAULT_MAX_DEPTH) {
        return -1;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (!default_field_valid(field, type->size)) {
            return -1;
        }
        pin_value value;
        pin_read_field(initial, field, &value);
        if (field->type == PIN_STRUCT) {
            const type_desc *nested = find_type(prototype->registry, field->type_name);
            if (!nested || nested->size != field->size ||
                copy_default_fields(
                    prototype,
                    (char *)out + field->offset,
                    value.ptr,
                    nested,
                    depth + 1
                )) {
                return -1;
            }
            continue;
        }
        if ((field->type == PIN_STR || field->type == PIN_NAME) && value.str) {
            value.str = own_string(prototype, value.str);
        }
        pin_write_field(out, field, value);
    }
    return 0;
}

int game_prototype_add_component(game_prototype *prototype, const ecs_component_desc *desc)
{
    if (!prototype || !desc || !desc->name || desc->derived_from ||
        find_component(prototype, desc->name)) {
        return -1;
    }
    game_prototype_component
        *detached = find_component_in(prototype->detached_components, desc->name);
    if (detached) {
        int index = (int)(detached - prototype->detached_components);
        apush(prototype->components, *detached);
        adel(prototype->detached_components, index);
        return 0;
    }
    game_prototype_component component = { .desc = desc,
                                           .data = xcalloc(1, desc->element_size) };
    const game_prototype_registry *registry = prototype->registry;
    int strings = acount(prototype->strings);
    for (int i = 0; i < registry->default_count; i++) {
        const game_prototype_default *initial = &registry->defaults[i];
        if (initial->component != desc->id) {
            continue;
        }
        if (!initial->data || initial->size != desc->element_size ||
            !desc->type || desc->type->size != desc->element_size ||
            copy_default_fields(prototype, component.data, initial->data, desc->type, 0)) {
            for (int j = strings; j < acount(prototype->strings); j++) {
                free(prototype->strings[j]);
            }
            asetlen(prototype->strings, strings);
            free(component.data);
            return -1;
        }
        break;
    }
    apush(prototype->components, component);
    return 0;
}

int game_prototype_detach_component(game_prototype *prototype, const char *component_name)
{
    if (!prototype || !component_name) {
        return -1;
    }
    game_prototype_component *component = find_component(prototype, component_name);
    if (!component) {
        return -1;
    }
    int index = (int)(component - prototype->components);
    apush(prototype->detached_components, *component);
    adel(prototype->components, index);
    return 0;
}

av_value *game_prototype_component_defaults_value(
    const game_prototype_registry *registry,
    const ecs_component_desc *desc
)
{
    game_prototype *defaults = xcalloc(1, sizeof *defaults);
    defaults->registry = registry;
    av_value *json = game_prototype_add_component(defaults, desc) == 0
                       ? game_prototype_component_value(defaults, desc->name)
                       : NULL;
    game_prototype_free(defaults);
    return json;
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
