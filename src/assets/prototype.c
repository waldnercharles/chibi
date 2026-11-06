#include "assets/prototype.h"

#include "ecs/component_catalog.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/asset_value.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/record.h"
#include "types/text.h"

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

static void setup_tag(prototype *document)
{
    document->tag.name = xmalloc(strlen(document->identity) + 16);
    snprintf(
        document->tag.name,
        strlen(document->identity) + 16,
        "PrototypeTag:%s",
        document->identity
    );
    document->tag.desc = (ecs_component_desc){
        .id = ecs_component_marker_id(document->identity),
        .key = document->identity,
        .name = document->tag.name,
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

static char *own_string(prototype *document, const char *text)
{
    char *copy = xstrdup(text ? text : "");
    apush(document->strings, copy);
    return copy;
}

/* Temporary reads borrow native text through a caller-owned descriptor. */
static void read_field(const void *base, const type_field *field, pin_value *out, pin_string *text)
{
    if (field->type == PIN_STR && field->text_descriptor) {
        const pin_string *stored;
        memcpy(&stored, (const char *)base + field->offset, sizeof stored);
        *out = (pin_value){ .type = PIN_STR, .type_name = field->type_name, .text = stored };
        return;
    }
    if (field->type == PIN_STR) {
        const char *bytes;
        memcpy(&bytes, (const char *)base + field->offset, sizeof bytes);
        size_t length = bytes ? strlen(bytes) : 0;
        assert(length <= UINT32_MAX);
        *text = (pin_string){ bytes ? bytes : "", (uint32_t)length };
        *out = (pin_value){ .type = PIN_STR, .type_name = field->type_name, .text = text };
        return;
    }
    const char *error = pin_read_field(NULL, base, field, out);
    assert(!error);
    (void)error;
}

static int own_field(prototype *document, const type_field *field, pin_value *value)
{
    if (field->type == PIN_NAME) {
        value->str = own_string(document, value->str);
    }
    if (field->type == PIN_STR) {
        pin_string text = pin_text(*value);
        if (!field->text_descriptor) {
            if (memchr(text.bytes, 0, text.length)) {
                return -1;
            }
        }
        pin_value copy = *value;
        if (pin_value_text(NULL, &copy, text.bytes, text.length)) {
            return -1;
        }
        apush(document->strings, (char *)copy.text);
        value->text = copy.text;
    }
    return 0;
}

static const type_desc *find_type(const prototype_registry *, const char *);

static const type_desc *resolve_prototype_type(void *registry, const char *name)
{
    return find_type(registry, name);
}
static type_resolver prototype_types(const prototype *document)
{
    return (type_resolver){ .find = resolve_prototype_type,
                            .user = (void *)document->registry };
}

static int copy_default_fields(prototype *, void *, const void *, const type_desc *, int);

static const char *write_field(prototype *document, void *base, const type_field *field, pin_value value)
{
    if (field->type == PIN_STRUCT && value.ptr) {
        const type_desc *type = find_type(document->registry, field->type_name);
        if (!type || type->size != field->size) {
            return "Field record layout is unavailable";
        }
        if (!value.type_name) {
            value.type_name = field->type_name;
        }
        arena temporary = { 0 };
        void *converted = xcalloc(1, field->size);
        const char *error = pin_write_struct_resolved(&temporary, converted, type, value, prototype_types(document));
        void *owned = xcalloc(1, field->size);
        if (!error && copy_default_fields(document, owned, converted, type, 0)) {
            error = "Field record copy failed";
        }
        if (!error) {
            memcpy((char *)base + field->offset, owned, field->size);
        }
        arena_free(&temporary);
        free(converted);
        free(owned);
        return error;
    }
    if (field->type != PIN_STR && field->type != PIN_NAME) {
        return pin_write_field(NULL, base, field, value);
    }
    if (own_field(document, field, &value)) {
        return "String contains embedded NUL";
    }
    void *destination = (char *)base + field->offset;
    if (field->type == PIN_STR && field->text_descriptor) {
        memcpy(destination, &value.text, sizeof value.text);
    } else {
        const char *bytes = field->type == PIN_NAME ? value.str
                                                    : pin_text(value).bytes;
        memcpy(destination, &bytes, sizeof bytes);
    }
    return NULL;
}

void prototype_owned_strings_free(char **strings)
{
    for (int i = 0; i < acount(strings); i++) {
        free(strings[i]);
    }
    afree(strings);
}

const ecs_component_desc *prototype_registry_find(const prototype_registry *registry, const char *name)
{
    return ecs_component_catalog_find(
        (ecs_component_catalog){ registry->components, registry->component_count },
        name
    );
}

static const type_desc *find_type(const prototype_registry *registry, const char *name)
{
    for (int i = 0; i < registry->component_count; i++) {
        const ecs_component_desc *desc = registry->components[i];
        if (desc && desc->type && strcmp(desc->type->name, name) == 0) {
            return desc->type;
        }
    }
    return type_catalog_find(name);
}

static prototype_component *find_component_in(prototype_component *components, const char *name)
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

static prototype_component *find_component(prototype *document, const char *name)
{
    return find_component_in(document->components, name);
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

static const prototype_link *find_link(
    const prototype_link *links,
    const ecs_component_desc *component,
    int field
)
{
    for (int i = 0; i < acount(links); i++) {
        if (links[i].component->id == component->id && links[i].field == field) {
            return &links[i];
        }
    }
    return NULL;
}

/* Drops the component's links, or only the field's when it is not -1. */
static void drop_links(prototype_link *links, const ecs_component_desc *component, int field)
{
    for (int i = acount(links) - 1; i >= 0; i--) {
        if (links[i].component->id == component->id &&
            (field < 0 || links[i].field == field)) {
            free(links[i].path);
            adel(links, i);
        }
    }
}

void prototype_links_free(prototype_link *links)
{
    for (int i = 0; i < acount(links); i++) {
        free(links[i].path);
    }
    afree(links);
}

/* The component whose top-level fields are being read, and whether the
 * document holding the prototype as an entity authors them. Nested values
 * have none, so they cannot name entities. */
typedef struct
{
    const ecs_component_desc *component;
    bool outer;
} field_scope;

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
    prototype *document,
    const prototype_registry *registry,
    void *base,
    const type_desc *type,
    av_value *object,
    const field_scope *scope,
    char *error,
    size_t error_size
);

static int parse_field(
    prototype *document,
    const prototype_registry *registry,
    void *base,
    const type_field *field,
    av_value *json,
    const field_scope *scope,
    char *error,
    size_t error_size
)
{
    if (field->range_contract && av_is_num(json) &&
        (field->type == PIN_F32 || field->type == PIN_F64)) {
        write_field(document, base, field, pin_field_number(field, av_get_num(json)));
        return 0;
    }
    pin_value value = { .type = field->type, .type_name = field->type_name };
    pin_string text = { 0 };
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
            result = !av_is_str(json) ||
                     memchr(av_get_str(json), 0, av_get_len(json));
            value.str = result ? NULL : av_get_str(json);
            break;
        case PIN_STR: {
            result = !av_is_str(json);
            if (!result) {
                text = (pin_string){ av_get_str(json), (uint32_t)av_get_len(json) };
                value.text = &text;
            }
            break;
        }
        case PIN_STRUCT: {
            const type_desc *nested = find_type(registry, field->type_name);
            void *nested_value = xcalloc(1, field->size);
            result = !nested ||
                     parse_fields(document, registry, nested_value, nested, json, NULL, error, error_size);
            if (!result) {
                value.ptr = nested_value;
                memcpy((char *)base + field->offset, nested_value, field->size);
            }
            free(nested_value);
            return result;
        }
        /* Live handles never save. A component's own handle field may name
         * an entity by path instead, which a scene or spawn resolves. */
        case PIN_HANDLE: {
            bool path = scope && av_is_str(json) && av_get_len(json);
            result = !path && !av_is_null(json);
            if (!result && scope) {
                prototype_link link = { scope->component,
                                        (int)(field - scope->component->type->fields),
                                        NULL,
                                        scope->outer };
                drop_links(document->links, link.component, link.field);
                if (path) {
                    link.path = xstrdup(av_get_str(json));
                    apush(document->links, link);
                }
            }
            break;
        }
        case PIN_POINTER:
        case PIN_TASK:
        case PIN_UNION:
        case PIN_FLOW: result = 1; break;
    }
    if (result && field->range_contract && (av_is_sint(json) || av_is_uint(json))) {
        value = pin_field_number(field, av_get_num(json));
        result = 0;
    }
    if (result) {
        set_error(error, error_size, "field %s has the wrong value type", field->name);
        return -1;
    }
    const char *write_error = write_field(document, base, field, value);
    if (write_error) {
        set_error(error, error_size, "field %s: %s", field->name, write_error);
        return -1;
    }
    return 0;
}

static int parse_fields(
    prototype *document,
    const prototype_registry *registry,
    void *base,
    const type_desc *type,
    av_value *object,
    const field_scope *scope,
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
        if (parse_field(document, registry, base, field, av_obj_iter_get_val(key), scope, error, error_size)) {
            return -1;
        }
    }
    type_validate_ranges(type, base);
    return 0;
}

static int parse_components(
    prototype *document,
    prototype_component **components,
    av_value *object,
    bool outer,
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
        const ecs_component_desc *desc = prototype_registry_find(document->registry, name);
        av_value *value = av_obj_iter_get_val(key);
        if (!desc) {
            set_error(error, error_size, "component %s is unavailable", name);
            return -1;
        }
        if (desc->derived_from) {
            set_error(error, error_size, "component %s is computed by the engine and cannot be authored", name);
            return -1;
        }
        prototype_component *component = find_component_in(*components, name);
        if (!component) {
            component = &(prototype_component){
                .desc = desc,
                .data = xcalloc(1, desc->element_size),
            };
            apush(*components, *component);
            component = &alast(*components);
        }
        field_scope scope = { desc, outer };
        if (parse_fields(document, document->registry, component->data, desc->type, value, &scope, error, error_size)) {
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

static void move_strings(prototype *dst, prototype *src)
{
    for (int i = 0; i < acount(src->strings); i++) {
        apush(dst->strings, src->strings[i]);
    }
    afree(src->strings);
}

/* Counts what `src` read toward `dst`'s load; false once the load reads too
 * many documents or bytes. */
static bool expand(prototype *dst, const prototype *src, char *error, size_t error_size)
{
    dst->expanded += 1 + src->expanded;
    dst->read += src->read;
    if (dst->expanded >= PROTOTYPE_EXPANSION_MAX) {
        set_error(
            error,
            error_size,
            "prototype %s reads more than %d prototypes through its includes "
            "and children",
            dst->name,
            PROTOTYPE_EXPANSION_MAX
        );
        return false;
    }
    if (dst->read >= PROTOTYPE_READ_MAX) {
        set_error(
            error,
            error_size,
            "prototype %s reads more than %d MiB of prototypes through its "
            "includes and children",
            dst->name,
            PROTOTYPE_READ_MAX >> 20
        );
        return false;
    }
    return true;
}

/* An include's children come before the including prototype's own. */
static bool merge_children(prototype *dst, prototype *src, char *error, size_t error_size)
{
    if (!src->children) {
        return true;
    }
    if (!dst->children) {
        dst->children = src->children;
        src->children = NULL;
    } else if (scene_document_take_entities(dst->children, src->children, error, error_size)) {
        return false;
    }
    for (int i = 0; i < acount(src->child_prototypes); i++) {
        apush(dst->child_prototypes, src->child_prototypes[i]);
    }
    aclear(src->child_prototypes);
    return true;
}

static bool merge_prototype(prototype *dst, prototype *src, char *error, size_t error_size)
{
    if (!merge_children(dst, src, error, error_size)) {
        return false;
    }
    move_strings(dst, src);
    /* Only attached components merge, so only their links do. */
    prototype_link *links = NULL;
    for (int i = 0; i < acount(src->links); i++) {
        if (find_component(src, src->links[i].component->name)) {
            apush(links, src->links[i]);
        } else {
            free(src->links[i].path);
        }
    }
    aclear(src->links);
    for (int i = 0; i < acount(src->components); i++) {
        prototype_component component = src->components[i];
        prototype_component *existing = find_component(dst, component.desc->name);
        if (existing) {
            memcpy(existing->data, component.data, component.desc->element_size);
            free(component.data);
            drop_links(dst->links, component.desc, -1);
        } else {
            apush(dst->components, component);
        }
    }
    aclear(src->components);
    for (int i = 0; i < acount(links); i++) {
        apush(dst->links, links[i]);
    }
    afree(links);
    for (int i = 0; i < acount(src->graphs); i++) {
        apush(dst->graphs, src->graphs[i]);
    }
    aclear(src->graphs);
    for (int i = 0; i < acount(src->pairs); i++) {
        apush(dst->pairs, src->pairs[i]);
    }
    aclear(src->pairs);
    return true;
}

static prototype *load_document(
    const char *text,
    const prototype_registry *registry,
    char *const *stack,
    int stack_count,
    char *error,
    size_t error_size
);

/* The prototype the resolver gives for `name`, loaded under `stack`; `role`
 * says in errors what named it. */
static prototype *load_named(
    const prototype_registry *registry,
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
    prototype *document = load_document(source, registry, stack, stack_count, error, error_size);
    if (document) {
        document->read += strlen(source);
    }
    if (document && strcmp(document->name, name)) {
        set_error(error, error_size, "%s %s is named %s", role, name, document->name);
        prototype_free(document);
        return NULL;
    }
    return document;
}
static bool read_pairs(prototype *document, av_value *root, char *error, size_t error_size)
{
    const prototype_registry *registry = document->registry;
    av_value *pairs = av_obj_get(root, "links");
    if (pairs && !av_is_obj(pairs)) {
        set_error(error, error_size, "prototype links must be a relationship object");
        return false;
    }
    for (size_t i = 0; i < av_obj_size(pairs); i++) {
        const char *name = av_get_str(av_obj_key_at(pairs, i));
        av_value *targets = av_obj_value_at(pairs, i);
        const ecs_relationship_desc *relationship = NULL;
        for (int j = 0; j < registry->relationship_count; j++) {
            if (!strcmp(registry->relationships[j]->name, name)) {
                relationship = registry->relationships[j];
            }
        }
        if (!relationship || (!av_is_str(targets) && !av_is_arr(targets))) {
            set_error(error, error_size, "prototype link names an unknown relationship or invalid target");
            return false;
        }
        int count = av_is_str(targets) ? 1 : (int)av_arr_size(targets);
        if ((relationship->traits & ECS_REL_EXCLUSIVE) && count > 1) {
            set_error(error, error_size, "exclusive prototype relationship has multiple targets");
            return false;
        }
        /* A derived prototype replaces the links it explicitly names. */
        for (int j = acount(document->pairs) - 1; j >= 0; j--) {
            if (document->pairs[j].relationship->id == relationship->id) {
                free(document->pairs[j].path);
                memmove(
                    document->pairs + j,
                    document->pairs + j + 1,
                    (size_t)(acount(document->pairs) - j - 1) * sizeof *document->pairs
                );
                asetlen(document->pairs, acount(document->pairs) - 1);
            }
        }
        for (int j = 0; j < count; j++) {
            const char *path = av_get_str(
                av_is_str(targets) ? targets : av_arr_get(targets, (size_t)j)
            );
            if (!path || strncmp(path, "child.", 6) || !path[6]) {
                set_error(error, error_size, "prototype relationship target must name child.<entity-path>");
                return false;
            }
            apush(document->pairs, ((prototype_pair){ relationship, xstrdup(path + 6) }));
        }
    }
    return true;
}

/* Folds the included prototype's content into `prototype`, before its own. */
static bool merge_include(
    prototype *document,
    const char *name,
    char *const *stack,
    int stack_count,
    char *error,
    size_t error_size
)
{
    prototype *base = load_named(document->registry, "prototype include", name, stack, stack_count, error, error_size);
    bool merged = base && expand(document, base, error, error_size) &&
                  merge_prototype(document, base, error, error_size);
    prototype_free(base);
    return merged;
}

/* What the child spawns as: its prototype with its components and behaviors,
 * under its own name. */
static prototype *resolve_child(
    const prototype_registry *registry,
    const scene_entity *child,
    char *const *stack,
    int stack_count,
    char *error,
    size_t error_size
)
{
    prototype *document = load_named(registry, "child prototype", child->prototype, stack, stack_count, error, error_size);
    if (!document) {
        return NULL;
    }
    if (parse_components(document, &document->components, child->components, true, error, error_size)) {
        prototype_free(document);
        return NULL;
    }
    if (child->own_behaviors) {
        aclear(document->graphs);
        for (int i = 0; i < acount(child->behaviors); i++) {
            apush(document->graphs, own_string(document, child->behaviors[i]));
        }
    }
    free(document->name);
    document->name = xstrdup(child->name);
    return document;
}

/* Reads the document's children after any its includes gave it and, unless
 * `authored`, resolves each. */
static bool read_children(
    prototype *document,
    av_value *children,
    bool authored,
    char *const *stack,
    int stack_count,
    char *error,
    size_t error_size
)
{
    if (!authored && !document->registry->include) {
        set_error(error, error_size, "prototype children need a resolver");
        return false;
    }
    if (!document->children) {
        document->children = scene_document_new(document->name);
    }
    int first = acount(document->children->entities);
    char reason[256];
    if (scene_document_read_entities(document->children, children, reason, sizeof reason)) {
        set_error(error, error_size, "children of prototype %s: %s", document->name, reason);
        return false;
    }
    for (int i = first; !authored && i < acount(document->children->entities); i++) {
        prototype *child = resolve_child(
            document->registry,
            &document->children->entities[i],
            stack,
            stack_count,
            error,
            error_size
        );
        if (!child) {
            return false;
        }
        apush(document->child_prototypes, child);
        if (!expand(document, child, error, error_size)) {
            return false;
        }
    }
    return true;
}

/* With `authored`, the included prototypes are named but not loaded, so the
 * result holds only what the document itself says. */
static prototype *load_value_document(
    av_value *root,
    const prototype_registry *registry,
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
    prototype *document = (prototype *)xcalloc(1, sizeof *document);
    document->name = xstrdup(name);
    document->identity = name_identity(name);
    document->authored = authored;
    document->registry_storage = *registry;
    document->registry = &document->registry_storage;
    av_value *identity = av_obj_get(root, "identity");
    if (identity) {
        if (!av_is_str(identity) || !av_get_len(identity)) {
            set_error(error, error_size, "prototype identity must be a non-empty string");
            prototype_free(document);
            return NULL;
        }
        free(document->identity);
        document->identity = xstrdup(av_get_str(identity));
    }
    setup_tag(document);
    char *next_stack[64];
    if (stack_count >= (int)(sizeof next_stack / sizeof next_stack[0])) {
        set_error(error, error_size, "prototype include depth exceeded");
        prototype_free(document);
        return NULL;
    }
    for (int i = 0; i < stack_count; i++) {
        next_stack[i] = (char *)stack[i];
    }
    next_stack[stack_count] = document->name;

    av_value *includes = av_obj_get(root, "includes");
    if (includes) {
        if (!av_is_arr(includes) ||
            (!authored && av_arr_size(includes) && !registry->include)) {
            set_error(error, error_size, "prototype includes need a resolver");
            prototype_free(document);
            return NULL;
        }
        av_arr_iter iterator = av_arr_iter_with(includes);
        av_value *include;
        while ((include = av_arr_iter_next(&iterator))) {
            if (!av_is_str(include)) {
                set_error(error, error_size, "prototype include name is not a string");
                prototype_free(document);
                return NULL;
            }
            if (!authored &&
                !merge_include(document, av_get_str(include), next_stack, stack_count + 1, error, error_size)) {
                prototype_free(document);
                return NULL;
            }
            apush(document->includes, own_string(document, av_get_str(include)));
        }
    }
    if (parse_components(document, &document->components, av_obj_get(root, "components"), false, error, error_size) ||
        parse_components(
            document,
            &document->detached_components,
            av_obj_get(root, "detached_components"),
            false,
            error,
            error_size
        )) {
        prototype_free(document);
        return NULL;
    }
    for (int i = 0; i < acount(document->detached_components); i++) {
        const char *name = document->detached_components[i].desc->name;
        if (find_component(document, name)) {
            set_error(error, error_size, "component %s is both attached and detached", name);
            prototype_free(document);
            return NULL;
        }
    }
    av_value *children = av_obj_get(root, "children");
    if (children &&
        !read_children(document, children, authored, next_stack, stack_count + 1, error, error_size)) {
        prototype_free(document);
        return NULL;
    }
    av_value *graphs = av_obj_get(root, "behaviors");
    if (graphs) {
        if (!av_is_arr(graphs)) {
            set_error(error, error_size, "behaviors must be an array");
            prototype_free(document);
            return NULL;
        }
        av_arr_iter iterator = av_arr_iter_with(graphs);
        av_value *graph;
        while ((graph = av_arr_iter_next(&iterator))) {
            if (!av_is_str(graph)) {
                set_error(error, error_size, "behavior graph name is not a string");
                prototype_free(document);
                return NULL;
            }
            apush(document->graphs, own_string(document, av_get_str(graph)));
        }
    }
    if (!read_pairs(document, root, error, error_size)) {
        prototype_free(document);
        return NULL;
    }
    return document;
}

static prototype *load_document(
    const char *text,
    const prototype_registry *registry,
    char *const *stack,
    int stack_count,
    char *error,
    size_t error_size
)
{
    av_doc *doc = registry->decode
                    ? registry->decode(text, registry, error, error_size)
                    : asset_text_read(text, error, error_size);
    prototype *result = doc
                          ? load_value_document(av_doc_root(doc), registry, false, stack, stack_count, error, error_size)
                          : NULL;
    av_doc_free(doc);
    return result;
}

prototype *prototype_load_value(av_value *value, const prototype_registry *registry, char *error, size_t error_size)
{
    return load_value_document(value, registry, false, NULL, 0, error, error_size);
}

prototype *prototype_load(const char *json, const prototype_registry *registry, char *error, size_t error_size)
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

prototype *prototype_load_authored(
    const char *text,
    const prototype_registry *registry,
    char *error,
    size_t error_size
)
{
    av_doc *doc = registry->decode
                    ? registry->decode(text, registry, error, error_size)
                    : asset_text_read(text, error, error_size);
    prototype *result = doc
                          ? load_value_document(av_doc_root(doc), registry, true, NULL, 0, error, error_size)
                          : NULL;
    av_doc_free(doc);
    return result;
}

int prototype_apply_entity_components(prototype *document, av_value *components, char *error, size_t error_size)
{
    return parse_components(document, &document->components, components, true, error, error_size);
}

prototype_link *prototype_components_links(const prototype_registry *registry, av_value *components)
{
    assert(av_is_obj(components));
    prototype_link *links = NULL;
    av_obj_iter iterator = av_obj_iter_with(components);
    av_value *key;
    while ((key = av_obj_iter_next(&iterator))) {
        const ecs_component_desc *desc = prototype_registry_find(registry, av_get_str(key));
        av_value *fields = av_obj_iter_get_val(key);
        for (int i = 0; desc && av_is_obj(fields) && i < desc->type->field_count; i++) {
            av_value *path = av_obj_get(fields, desc->type->fields[i].name);
            if (desc->type->fields[i].type == PIN_HANDLE && av_is_str(path) &&
                av_get_len(path)) {
                apush(links, ((prototype_link){ desc, i, xstrdup(av_get_str(path)), true }));
            }
        }
    }
    return links;
}

prototype *prototype_clone(const prototype *document, char *error, size_t error_size)
{
    if (!document || !document->registry) {
        set_error(error, error_size, "invalid prototype clone arguments");
        return NULL;
    }
    av_value *value = prototype_to_value(document);
    prototype *copy = value
                        ? prototype_load_value(value, document->registry, error, error_size)
                        : NULL;
    av_value_free(value);
    if (!copy) {
        return NULL;
    }
    /* The document cannot say which paths its holder authored. */
    prototype_links_free(copy->links);
    copy->links = NULL;
    for (int i = 0; i < acount(document->links); i++) {
        prototype_link link = document->links[i];
        link.path = xstrdup(link.path);
        apush(copy->links, link);
    }
    return copy;
}

const char *prototype_identity(const prototype *document)
{
    return document ? document->identity : NULL;
}

const ecs_component_desc *prototype_tag_desc(const prototype *document)
{
    return document ? &document->tag.desc : NULL;
}

static int prototype_register_tag(const prototype *document, ecs_world *world)
{
    if (!document || !world) {
        return ECS_ERR_ARGUMENT;
    }
    for (int i = 0; i < ecs_world_component_count(world); i++) {
        const ecs_component_desc *desc = ecs_world_component_at(world, i);
        if (desc->id == document->tag.desc.id) {
            return desc->key && strcmp(desc->key, document->identity) == 0
                     ? ECS_OK
                     : ECS_ERR_DUPLICATE;
        }
    }
    return ecs_world_register_component(world, &document->tag.desc, NULL);
}

prototype *prototype_new(const char *name)
{
    assert(name && name[0]);
    prototype *document = (prototype *)xcalloc(1, sizeof *document);
    document->name = xstrdup(name);
    document->identity = name_identity(name);
    document->registry = &document->registry_storage;
    setup_tag(document);
    return document;
}

prototype *prototype_capture_entity(
    const char *name,
    const ecs_world *world,
    ecs_entity entity,
    const prototype_registry *registry
)
{
    if (!name || !name[0] || !world || !registry || registry->component_count < 0 ||
        (registry->component_count && !registry->components) ||
        !ecs_entity_is_alive(world, entity)) {
        return NULL;
    }
    prototype *document = prototype_new(name);
    document->registry_storage = *registry;
    for (int i = 0; i < registry->component_count; i++) {
        const ecs_component_desc *desc = registry->components[i];
        if (!desc || desc->derived_from ||
            !ecs_component_has(world, entity, desc->id)) {
            continue;
        }
        prototype_component component = {
            .desc = desc,
            .data = xmalloc(desc->element_size),
        };
        memcpy(component.data, ecs_component_data_const(world, entity, desc->id), desc->element_size);
        apush(document->components, component);
    }
    return document;
}

void prototype_free(prototype *document)
{
    if (!document) {
        return;
    }
    for (int i = 0; i < acount(document->components); i++) {
        free(document->components[i].data);
    }
    afree(document->components);
    for (int i = 0; i < acount(document->detached_components); i++) {
        free(document->detached_components[i].data);
    }
    afree(document->detached_components);
    prototype_links_free(document->links);
    for (int i = 0; i < acount(document->pairs); i++) {
        free(document->pairs[i].path);
    }
    afree(document->pairs);
    afree(document->graphs);
    afree(document->includes);
    scene_document_free(document->children);
    for (int i = 0; i < acount(document->child_prototypes); i++) {
        prototype_free(document->child_prototypes[i]);
    }
    afree(document->child_prototypes);
    prototype_owned_strings_free(document->strings);
    free(document->name);
    free(document->identity);
    free(document->tag.name);
    free(document);
}

static int compare_strings(const void *left_ptr, const void *right_ptr)
{
    const char *const *left = (const char *const *)left_ptr;
    const char *const *right = (const char *const *)right_ptr;
    return strcmp(*left, *right);
}

/* Prototype documents cannot preserve managed pointer identity or ownership. */
static bool serializable_type(const prototype_registry *registry, const type_desc *type, int depth)
{
    if (!type || depth == 64) {
        return false;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->type == PIN_POINTER) {
            return false;
        }
        if (field->type == PIN_STRUCT) {
            const type_desc *nested = find_type(registry, field->type_name);
            if (nested && !serializable_type(registry, nested, depth + 1)) {
                return false;
            }
        }
    }
    return true;
}
static bool serializable_components(const prototype *document, const prototype_component *components)
{
    for (int i = 0; i < acount(components); i++) {
        if (!serializable_type(document->registry, components[i].desc->type, 0)) {
            return false;
        }
    }
    return true;
}

/* `component` is the component whose top-level fields these are, so its
 * entity paths save, or NULL for a nested value. */
static void write_struct_fields(
    av_doc *doc,
    av_value *object,
    const prototype *document,
    const ecs_component_desc *component,
    const void *base,
    const type_desc *type,
    bool sparse
);

/* Loading starts every component from zero, so sparse documents omit zeros. */
static bool zero_field(const void *base, const type_field *field)
{
    pin_value value;
    pin_string text;
    read_field(base, field, &value, &text);
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
        case PIN_NAME: return !value.str;
        case PIN_STR:
            return !*(const void *const *)((const char *)base + field->offset);
        case PIN_STRUCT: return !value.ptr;
        case PIN_POINTER: return false;
        case PIN_HANDLE:
        case PIN_TASK:
        case PIN_UNION:
        case PIN_FLOW: return true;
    }
    return false;
}

static av_value *write_value(av_doc *doc, const prototype *document, const void *base, const type_field *field)
{
    pin_value value;
    pin_string text;
    read_field(base, field, &value, &text);
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
        case PIN_NAME: return av_str(doc, value.str ? value.str : "");
        case PIN_STR: {
            pin_string text = pin_text(value);
            return av_strn(doc, text.bytes, text.length);
        }
        case PIN_STRUCT: {
            av_value *object = av_obj(doc);
            const type_desc *nested = find_type(document->registry, field->type_name);
            if (nested && value.ptr) {
                write_struct_fields(doc, object, document, NULL, value.ptr, nested, false);
            }
            return object;
        }
        case PIN_POINTER: return NULL;
        case PIN_HANDLE:
        case PIN_TASK:
        case PIN_UNION:
        case PIN_FLOW: return av_null(doc);
    }
    return av_null(doc);
}

static void write_struct_fields(
    av_doc *doc,
    av_value *object,
    const prototype *document,
    const ecs_component_desc *component,
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
        const prototype_link *link = component
                                       ? find_link(document->links, component, order[i])
                                       : NULL;
        if (link) {
            av_obj_add_val(doc, object, field->name, av_str(doc, link->path));
            continue;
        }
        if (sparse && zero_field(base, field)) {
            continue;
        }
        pin_value value;
        pin_string text;
        read_field(base, field, &value, &text);
        av_value *json = NULL;
        if (field->type == PIN_STRUCT && value.ptr) {
            json = av_obj(doc);
            write_struct_fields(
                doc,
                json,
                document,
                NULL,
                value.ptr,
                find_type(document->registry, field->type_name),
                sparse
            );
            if (sparse && !av_obj_size(json)) {
                continue;
            }
        } else {
            json = write_value(doc, document, base, field);
        }
        av_obj_add_val(doc, object, field->name, json);
    }
    afree(order);
}

static av_value *write_components(av_doc *doc, const prototype *document, prototype_component *values, bool sparse)
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
        prototype_component *component = &values[order[i]];
        av_value *object = av_obj(doc);
        write_struct_fields(
            doc,
            object,
            document,
            component->desc,
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

av_value *prototype_to_value(const prototype *document)
{
    if (!document || !serializable_components(document, document->components) ||
        !serializable_components(document, document->detached_components)) {
        return NULL;
    }
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_strcpy(doc, root, "name", document->name);
    char *identity = name_identity(document->name);
    if (strcmp(identity, document->identity) != 0) {
        av_obj_add_strcpy(doc, root, "identity", document->identity);
    }
    free(identity);
    if (acount(document->pairs)) {
        av_value *pairs = av_obj_add_obj(doc, root, "links");
        for (int i = 0; i < acount(document->pairs); i++) {
            const prototype_pair *pair = &document->pairs[i];
            av_value *targets = av_obj_get(pairs, pair->relationship->name);
            if (!targets) {
                targets = av_obj_add_arr(doc, pairs, pair->relationship->name);
            }
            size_t length = strlen(pair->path) + sizeof "child.";
            char *path = xmalloc(length);
            snprintf(path, length, "child.%s", pair->path);
            av_arr_add_str(doc, targets, path);
            free(path);
        }
    }
    if (document->authored && acount(document->includes)) {
        av_value *includes = av_arr(doc);
        for (int i = 0; i < acount(document->includes); i++) {
            av_arr_add_strcpy(doc, includes, document->includes[i]);
        }
        av_obj_add_val(doc, root, "includes", includes);
    }
    av_obj_add_val(
        doc,
        root,
        "components",
        write_components(doc, document, document->components, true)
    );
    if (acount(document->detached_components)) {
        av_obj_add_val(
            doc,
            root,
            "detached_components",
            write_components(doc, document, document->detached_components, true)
        );
    }
    if (acount(document->graphs)) {
        char **graphs = NULL;
        for (int i = 0; i < acount(document->graphs); i++) {
            apush(graphs, document->graphs[i]);
        }
        asort(graphs, compare_strings);
        av_value *array = av_arr(doc);
        for (int i = 0; i < acount(graphs); i++) {
            av_arr_add_strcpy(doc, array, graphs[i]);
        }
        av_obj_add_val(doc, root, "behaviors", array);
        afree(graphs);
    }
    if (document->children && acount(document->children->entities)) {
        ecs_component_catalog catalog = { document->registry->components,
                                          document->registry->component_count };
        av_obj_add_val(
            doc,
            root,
            "children",
            scene_document_entities_value(doc, document->children, catalog)
        );
    }
    av_value *result = av_value_copy(av_doc_root(doc));
    av_doc_free(doc);
    return result;
}

char *prototype_save(const prototype *document)
{
    av_value *value = prototype_to_value(document);
    char *text = value ? asset_text_write(value) : NULL;
    av_value_free(value);
    return text;
}

av_value *prototype_components_value(const prototype *document)
{
    if (!document || !serializable_components(document, document->components)) {
        return NULL;
    }
    av_doc *doc = av_doc_new();
    av_doc_set_root(doc, write_components(doc, document, document->components, false));
    av_value *result = av_value_copy(av_doc_root(doc));
    av_doc_free(doc);
    return result;
}

av_value *prototype_component_value(const prototype *document, const char *name)
{
    const prototype_component *component = find_component((prototype *)document, name);
    if (component && !serializable_type(document->registry, component->desc->type, 0)) {
        return NULL;
    }
    prototype_component *values = NULL;
    if (component) {
        apush(values, *component);
    }
    av_doc *doc = av_doc_new();
    av_doc_set_root(doc, write_components(doc, document, values, false));
    av_value *json = av_value_copy(av_doc_root(doc));
    av_doc_free(doc);
    afree(values);
    return json;
}

av_value *prototype_entity_component_value(
    const ecs_world *world,
    ecs_entity entity,
    const ecs_component_desc *desc,
    const prototype_registry *registry
)
{
    if (!world || !desc || !registry || !ecs_entity_is_alive(world, entity) ||
        !serializable_type(registry, desc->type, 0)) {
        return NULL;
    }
    const void *data = ecs_component_data_const(world, entity, desc->id);
    if (!data) {
        return NULL;
    }
    prototype schema = { .registry = registry };
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_value *fields = av_obj(doc);
    av_doc_set_root(doc, root);
    write_struct_fields(doc, fields, &schema, desc, data, desc->type, false);
    av_obj_add_val(doc, root, desc->key ? desc->key : desc->name, fields);
    av_value *json = av_value_copy(av_doc_root(doc));
    av_doc_free(doc);
    return json;
}

static int field_equal(const prototype *document, const type_field *field, const void *left, const void *right)
{
    if (field->type == PIN_STRUCT) {
        const type_desc *nested = find_type(document->registry, field->type_name);
        if (!nested) {
            return 0;
        }
        const unsigned char *left_value = (const unsigned char *)left + field->offset;
        const unsigned char *right_value = (const unsigned char *)right + field->offset;
        for (int i = 0; i < nested->field_count; i++) {
            if (!field_equal(document, &nested->fields[i], left_value, right_value)) {
                return 0;
            }
        }
        return 1;
    }
    pin_value left_value = { 0 };
    pin_value right_value = { 0 };
    pin_string left_text, right_text;
    read_field(left, field, &left_value, &left_text);
    read_field(right, field, &right_value, &right_text);
    return pin_value_equal(&left_value, &right_value);
}

/* An entity path override: the current path where it differs from the
 * base's, or names another document's entities. A current naming none keeps
 * the base's: it may be a captured entity, whose handles never save. */
static av_value *link_override(
    av_doc *doc,
    const prototype *base,
    const prototype *current,
    const ecs_component_desc *component,
    int field
)
{
    const prototype_link *now = find_link(current->links, component, field);
    if (!now) {
        return NULL;
    }
    const prototype_link *was = find_link(base->links, component, field);
    return was && was->outer == now->outer && !strcmp(was->path, now->path)
             ? NULL
             : av_str(doc, now->path);
}

/* `current` holds the entity paths of current_components, or is NULL when
 * they are a live entity's, whose handles never save. */
static av_value *component_overrides_value(
    const prototype *base,
    const prototype_component *current_components,
    const prototype *current
)
{
    if (!serializable_components(base, base->components) ||
        !serializable_components(base, current_components)) {
        return NULL;
    }
    prototype view = { .registry = base->registry,
                       .links = current ? current->links : NULL };
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
        const prototype_component *component = &base->components[order[i]];
        const void *data = NULL;
        for (int j = 0; j < acount(current_components); j++) {
            if (current_components[j].desc->id == component->desc->id) {
                data = current_components[j].data;
                break;
            }
        }
        if (!data) {
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
            av_value *value = NULL;
            if (field->type == PIN_HANDLE) {
                value = current
                          ? link_override(doc, base, current, component->desc, field_index)
                          : NULL;
            } else if (!field_equal(base, field, component->data, data)) {
                value = write_value(doc, base, data, field);
            }
            if (!value) {
                continue;
            }
            if (!fields) {
                fields = av_obj(doc);
            }
            av_obj_add_val(doc, fields, field->name, value);
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
        const prototype_component *component = &current_components[i];
        bool existed = false;
        for (int j = 0; j < acount(base->components); j++) {
            existed |= base->components[j].desc->id == component->desc->id;
        }
        if (existed) {
            continue;
        }
        av_value *fields = av_obj(doc);
        write_struct_fields(
            doc,
            fields,
            &view,
            component->desc,
            component->data,
            component->desc->type,
            false
        );
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

av_value *prototype_overrides_value(const prototype *base, const prototype *current)
{
    if (!base || !current) {
        return NULL;
    }
    return component_overrides_value(base, current->components, current);
}

av_value *prototype_capture_overrides_value(const prototype *base, const ecs_world *world, ecs_entity entity)
{
    if (!base || !world || !ecs_entity_is_alive(world, entity)) {
        return NULL;
    }
    prototype_component *components = NULL;
    for (int i = 0; i < base->registry->component_count; i++) {
        const ecs_component_desc *desc = base->registry->components[i];
        if (desc->derived_from || !ecs_component_has(world, entity, desc->id)) {
            continue;
        }
        prototype_component component = {
            .desc = desc,
            .data = (void *)ecs_component_data_const(world, entity, desc->id),
        };
        apush(components, component);
    }
    av_value *json = component_overrides_value(base, components, NULL);
    afree(components);
    return json;
}

static int apply_override_fields(
    prototype *parser,
    const prototype_registry *registry,
    void *base,
    const type_desc *type,
    av_value *fields,
    const field_scope *scope,
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
                               NULL,
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
        if (parse_field(parser, registry, base, field, value, scope, error, error_size) !=
            0) {
            return -1;
        }
    }
    return 0;
}

static int apply_component_override(
    prototype *parser,
    ecs_world *world,
    ecs_entity entity,
    const prototype_registry *registry,
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
    field_scope scope = { desc, true };
    return apply_override_fields(parser, registry, base, desc->type, fields, &scope, error, error_size);
}

/* Hands the parser's strings to the caller, whose components use them, and
 * drops its links: live entities hold no paths. */
static int release_parser(prototype *parser, char ***owned_strings, int result)
{
    for (int i = 0; i < acount(parser->strings); i++) {
        apush(*owned_strings, parser->strings[i]);
    }
    afree(parser->strings);
    prototype_links_free(parser->links);
    return result;
}

int prototype_apply_components_value(
    ecs_world *world,
    ecs_entity entity,
    av_value *components,
    const prototype_registry *registry,
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
    prototype parser = { .registry = registry };
    av_obj_iter iterator = av_obj_iter_with(root);
    av_value *key;
    while ((key = av_obj_iter_next(&iterator))) {
        const ecs_component_desc *desc = prototype_registry_find(registry, av_get_str(key));
        if (!desc || desc->derived_from) {
            set_error(
                error,
                error_size,
                "override component %s is unknown or computed by the engine",
                av_get_str(key)
            );
            return release_parser(&parser, owned_strings, -1);
        }
        if (av_is_null(av_obj_iter_get_val(key))) {
            if (ecs_component_has(world, entity, desc->id) &&
                ecs_world_record_remove(world, entity, desc->id, 0) != ECS_OK) {
                set_error(error, error_size, "override component %s could not be removed", desc->name);
                return release_parser(&parser, owned_strings, -1);
            }
            continue;
        }
        if (!ecs_component_has(world, entity, desc->id) &&
            ecs_world_record_add(world, entity, desc->id, NULL, 0) != ECS_OK) {
            set_error(error, error_size, "override component %s could not be added", desc->name);
            return release_parser(&parser, owned_strings, -1);
        }
    }
    if (ecs_world_apply_commands(world) != ECS_OK) {
        set_error(error, error_size, "component override application failed");
        return release_parser(&parser, owned_strings, -1);
    }
    iterator = av_obj_iter_with(root);
    while ((key = av_obj_iter_next(&iterator))) {
        const ecs_component_desc *desc = prototype_registry_find(registry, av_get_str(key));
        if (av_is_null(av_obj_iter_get_val(key))) {
            continue;
        }
        if (apply_component_override(&parser, world, entity, registry, desc, av_obj_iter_get_val(key), error, error_size) !=
            0) {
            return release_parser(&parser, owned_strings, -1);
        }
    }
    return release_parser(&parser, owned_strings, 0);
}

/* Writes into `at` the entities that `named`'s links of the given kind name
 * among `holder`'s children, each spawned as spawned[child]. */
static int write_links(
    ecs_world *world,
    ecs_entity at,
    const prototype *named,
    bool outer,
    const prototype *holder,
    const ecs_entity *spawned,
    char *error,
    size_t error_size
)
{
    for (int i = 0; i < acount(named->links); i++) {
        const prototype_link *link = &named->links[i];
        if (link->outer != outer ||
            !find_component_in(named->components, link->component->name)) {
            continue;
        }
        int child = scene_document_entity_index(holder->children, link->path);
        const type_field *field = &link->component->type->fields[link->field];
        if (child < 0) {
            set_error(
                error,
                error_size,
                "%s %s names %s, which is not a child of prototype %s",
                link->component->name,
                field->name,
                link->path,
                holder->name
            );
            return ECS_ERR_COMPONENT;
        }
        pin_value value = { .type = PIN_HANDLE,
                            .type_name = field->type_name,
                            .u64 = ecs_entity_to_u64(spawned[child]) };
        if (ecs_component_write_field_resolved(
                world,
                at,
                link->component->id,
                link->field,
                value,
                prototype_types(named)
            )) {
            set_error(
                error,
                error_size,
                "%s %s could not be written",
                link->component->name,
                field->name
            );
            return ECS_ERR_COMPONENT;
        }
    }
    return ECS_OK;
}

/* Creates the prototype's entity under `parent`, unless that is zero, then its
 * children under it, appending each entity with what it spawned as. */
static int create_tree(
    const prototype *document,
    ecs_world *world,
    ecs_entity parent,
    ecs_entity **entities,
    const prototype ***prototypes,
    char *error,
    size_t error_size
)
{
    for (int i = 0; document->registry && i < document->registry->relationship_count;
         i++) {
        const ecs_relationship_desc *desc = document->registry->relationships[i];
        const ecs_relationship_desc *found = ecs_world_relationship(world, desc->id);
        if ((found && (found->traits != desc->traits ||
                       found->on_delete_target != desc->on_delete_target ||
                       (found->key != desc->key && (!found->key || !desc->key ||
                                                    strcmp(found->key, desc->key))) ||
                       strcmp(found->name, desc->name))) ||
            (!found && ecs_world_register_relationship(world, desc, NULL))) {
            set_error(error, error_size, "prototype relationship schema could not be registered");
            return ECS_ERR_DUPLICATE;
        }
    }
    if (prototype_register_tag(document, world) != ECS_OK) {
        set_error(error, error_size, "prototype identity tag could not be registered");
        return ECS_ERR_DUPLICATE;
    }
    ecs_entity entity = ecs_world_record_create(world, 0);
    if (!entity.slot || ecs_world_apply_commands(world) != ECS_OK) {
        set_error(error, error_size, "prototype entity allocation failed");
        return ECS_ERR_CAPACITY;
    }
    apush(*entities, entity);
    apush(*prototypes, document);
    for (int i = 0; i < acount(document->components); i++) {
        const prototype_component *component = &document->components[i];
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
    if (ecs_world_record_add(world, entity, document->tag.desc.id, NULL, 0) ||
        (parent.slot &&
         ecs_world_record_add_pair(world, entity, ECS_CHILD_OF, parent, 0))) {
        set_error(
            error,
            error_size,
            "prototype %s could not be placed",
            document->name
        );
        return ECS_ERR_COMPONENT;
    }
    if (ecs_world_apply_commands(world) != ECS_OK) {
        set_error(error, error_size, "prototype component application failed");
        return ECS_ERR_COMMAND;
    }
    for (int i = 0; i < acount(document->components); i++) {
        const prototype_component *component = &document->components[i];
        for (int field = 0; field < component->desc->type->field_count; field++) {
            pin_value value;
            pin_string text;
            read_field(component->data, &component->desc->type->fields[field], &value, &text);
            if (ecs_component_write_field_resolved(
                    world,
                    entity,
                    component->desc->id,
                    field,
                    value,
                    prototype_types(document)
                )) {
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
    int count = document->children ? acount(document->children->entities) : 0;
    assert(acount(document->child_prototypes) == count);
    int *children = NULL; /* where each child's entity lands in `entities` */
    int result = ECS_OK;
    for (int i = 0; !result && i < count; i++) {
        int index = document->children->entities[i].parent;
        apush(children, acount(*entities));
        result = create_tree(
            document->child_prototypes[i],
            world,
            index < 0 ? entity : (*entities)[children[index]],
            entities,
            prototypes,
            error,
            error_size
        );
    }
    /* Its own paths and those its document gives its children name them. */
    ecs_entity *spawned = NULL;
    for (int i = 0; !result && i < count; i++) {
        apush(spawned, (*entities)[children[i]]);
    }
    if (!result) {
        result = write_links(world, entity, document, false, document, spawned, error, error_size);
    }
    for (int i = 0; !result && i < count; i++) {
        result = write_links(world, spawned[i], document->child_prototypes[i], true, document, spawned, error, error_size);
    }
    for (int i = 0; !result && i < acount(document->pairs); i++) {
        const prototype_pair *pair = &document->pairs[i];
        int target = document->children
                       ? scene_document_entity_index(document->children, pair->path)
                       : -1;
        if (target < 0) {
            set_error(error, error_size, "prototype relationship names missing child.%s", pair->path);
            result = ECS_ERR_ENTITY;
        } else {
            result = ecs_world_record_add_pair(
                world,
                entity,
                pair->relationship->id,
                spawned[target],
                (uint64_t)i
            );
        }
    }
    if (!result && acount(document->pairs)) {
        result = ecs_world_apply_commands(world);
    }
    afree(spawned);
    afree(children);
    return result;
}

/* Every entity exists before any behavior attaches; without hooks none do. */
/* Whether the prototype or any of its children attaches a behavior. */
static bool has_graphs(const prototype *document)
{
    if (acount(document->graphs)) {
        return true;
    }
    for (int i = 0; i < acount(document->child_prototypes); i++) {
        if (has_graphs(document->child_prototypes[i])) {
            return true;
        }
    }
    return false;
}

static int spawn_tree(
    const prototype *document,
    ecs_world *world,
    const prototype_spawn_hooks *hooks,
    ecs_entity *out_entity,
    char *error,
    size_t error_size
)
{
    ecs_entity *entities = NULL;
    const prototype **prototypes = NULL;
    int result = create_tree(document, world, (ecs_entity){ 0 }, &entities, &prototypes, error, error_size);
    for (int i = 0; !result && hooks && i < acount(entities); i++) {
        const prototype *spawned = prototypes[i];
        for (int j = 0; !result && j < acount(spawned->graphs); j++) {
            if (hooks->attach(world, entities[i], spawned->graphs[j], hooks->attach_user)) {
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
        int cleanup = ecs_world_prepare_owned_destruction(world, entities, acount(entities));
        if (cleanup) {
            set_error(error, error_size, "prototype rollback refused by an external Panic link");
        }
        for (int i = acount(entities) - 1; !cleanup && i >= 0; i--) {
            if (ecs_entity_is_alive(world, entities[i])) {
                cleanup = ecs_world_destroy_entity(world, entities[i]);
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

int prototype_spawn_components(
    const prototype *document,
    ecs_world *world,
    ecs_entity *entity,
    char *error,
    size_t error_size
)
{
    if (!document || !world || !entity) {
        set_error(error, error_size, "invalid prototype spawn arguments");
        return ECS_ERR_ARGUMENT;
    }
    return spawn_tree(document, world, NULL, entity, error, error_size);
}

int prototype_spawn(
    const prototype *document,
    ecs_world *world,
    const prototype_spawn_hooks *hooks,
    ecs_entity *out_entity,
    char *error,
    size_t error_size
)
{
    if (!document || !world || !out_entity) {
        set_error(error, error_size, "invalid prototype spawn arguments");
        return ECS_ERR_ARGUMENT;
    }
    if ((!hooks || !hooks->attach) && has_graphs(document)) {
        set_error(error, error_size, "prototype has behavior graphs but no attach hook");
        return ECS_ERR_ARGUMENT;
    }
    return spawn_tree(document, world, hooks, out_entity, error, error_size);
}

int prototype_read_field(
    arena *storage,
    const prototype *document,
    const char *component_name,
    const char *field_name,
    pin_value *out
)
{
    if (!document || !component_name || !field_name || !out) {
        return -1;
    }
    prototype_component *component = find_component((prototype *)document, component_name);
    const type_field *field = component
                                ? find_field(component->desc->type, field_name)
                                : NULL;
    if (!field) {
        return -1;
    }
    return pin_read_field(storage, component->data, field, out) ? -1 : 0;
}

int prototype_write_field(prototype *document, const char *component_name, const char *field_name, pin_value value)
{
    if (!document || !component_name || !field_name) {
        return -1;
    }
    prototype_component *component = find_component(document, component_name);
    const type_field *field = component
                                ? find_field(component->desc->type, field_name)
                                : NULL;
    if (!field || field->type != value.type ||
        !serializable_type(document->registry, component->desc->type, 0)) {
        return -1;
    }
    if (field->type == PIN_HANDLE) {
        drop_links(
            document->links,
            component->desc,
            (int)(field - component->desc->type->fields)
        );
    }
    return write_field(document, component->data, field, value) ? -1 : 0;
}

int prototype_write_field_value(
    prototype *document,
    const char *component_name,
    const char *field_name,
    av_value *json,
    char *error,
    size_t error_size
)
{
    if (!document || !component_name || !field_name || !json) {
        set_error(error, error_size, "prototype field arguments are required");
        return -1;
    }
    prototype_component *component = find_component(document, component_name);
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
    field_scope scope = { component->desc, false };
    return parse_field(document, document->registry, component->data, field, json, &scope, error, error_size);
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

av_value *prototype_rebase_component_value(av_value *values, av_value *previous_defaults, av_value *current_defaults)
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
    prototype *document,
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
        pin_string text;
        read_field(initial, field, &value, &text);
        if (field->type == PIN_STRUCT) {
            const type_desc *nested = find_type(document->registry, field->type_name);
            if (!nested || nested->size != field->size ||
                copy_default_fields(document, (char *)out + field->offset, value.ptr, nested, depth + 1)) {
                return -1;
            }
            continue;
        }
        if (write_field(document, out, field, value)) {
            return -1;
        }
    }
    return 0;
}

int prototype_add_component(prototype *document, const ecs_component_desc *desc)
{
    if (!document || !desc || !desc->name || desc->derived_from ||
        find_component(document, desc->name)) {
        return -1;
    }
    prototype_component *detached = find_component_in(document->detached_components, desc->name);
    if (detached) {
        int index = (int)(detached - document->detached_components);
        apush(document->components, *detached);
        adel(document->detached_components, index);
        return 0;
    }
    prototype_component component = { .desc = desc,
                                      .data = xcalloc(1, desc->element_size) };
    const prototype_registry *registry = document->registry;
    int strings = acount(document->strings);
    for (int i = 0; i < registry->default_count; i++) {
        const prototype_default *initial = &registry->defaults[i];
        if (initial->component != desc->id) {
            continue;
        }
        if (!initial->data || initial->size != desc->element_size ||
            !desc->type || desc->type->size != desc->element_size ||
            copy_default_fields(document, component.data, initial->data, desc->type, 0)) {
            for (int j = strings; j < acount(document->strings); j++) {
                free(document->strings[j]);
            }
            asetlen(document->strings, strings);
            free(component.data);
            return -1;
        }
        break;
    }
    type_validate_ranges(desc->type, component.data);
    apush(document->components, component);
    return 0;
}

int prototype_detach_component(prototype *document, const char *component_name)
{
    if (!document || !component_name) {
        return -1;
    }
    prototype_component *component = find_component(document, component_name);
    if (!component) {
        return -1;
    }
    int index = (int)(component - document->components);
    apush(document->detached_components, *component);
    adel(document->components, index);
    return 0;
}

av_value *prototype_component_defaults_value(const prototype_registry *registry, const ecs_component_desc *desc)
{
    prototype *defaults = xcalloc(1, sizeof *defaults);
    defaults->registry = registry;
    av_value *json = prototype_add_component(defaults, desc) == 0
                       ? prototype_component_value(defaults, desc->name)
                       : NULL;
    prototype_free(defaults);
    return json;
}

int prototype_remove_component(prototype *document, const char *component_name)
{
    if (!document || !component_name) {
        return -1;
    }
    for (int i = 0; i < acount(document->components); i++) {
        prototype_component *component = &document->components[i];
        if (strcmp(component->desc->name, component_name) != 0) {
            continue;
        }
        drop_links(document->links, component->desc, -1);
        free(component->data);
        adel(document->components, i);
        return 0;
    }
    return -1;
}

int prototype_has_graph(const prototype *document, const char *asset_name)
{
    if (!document || !asset_name || !asset_name[0]) {
        return 0;
    }
    for (int i = 0; i < acount(document->graphs); i++) {
        if (strcmp(document->graphs[i], asset_name) == 0) {
            return 1;
        }
    }
    return 0;
}

int prototype_add_graph(prototype *document, const char *asset_name)
{
    if (!document || !asset_name || !asset_name[0] ||
        prototype_has_graph(document, asset_name)) {
        return -1;
    }
    apush(document->graphs, own_string(document, asset_name));
    return 0;
}

int prototype_remove_graph(prototype *document, const char *asset_name)
{
    if (!document || !asset_name) {
        return -1;
    }
    for (int i = 0; i < acount(document->graphs); i++) {
        if (strcmp(document->graphs[i], asset_name) != 0) {
            continue;
        }
        adel(document->graphs, i);
        return 0;
    }
    return -1;
}
