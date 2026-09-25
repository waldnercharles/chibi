#include "editor/documents/component_presentation.h"

#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const char *const COMPONENT_TAB_NAMES[COMPONENT_TAB_COUNT] = {
    "Object",
    "Physics",
    "Material",
    "Gameplay",
};

static const char *const ICON_NAMES[COMPONENT_ICON_COUNT] = {
    "camera",  "system",   "light",      "emitter", "water",   "terrain",
    "gamepad", "keyboard", "projectile", "health",  "diamond", "shape",
};

static const char *const DOCUMENT_KEYS[] = { "components" };
static const char *const COMPONENT_KEYS[] = { "tab", "icon", "fields" };
static const char *const FIELD_KEYS[] = { "range", "unit" };

static bool fail(char *error, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(error, size, format, args);
    va_end(args);
    return false;
}

static int name_index(const char *name, const char *const *names, int count)
{
    for (int i = 0; name && i < count; i++) {
        if (strcmp(name, names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

/* The first key of `object` outside `keys`, or NULL. */
static const char *unknown_key(yyjson_val *object, const char *const *keys, int count)
{
    yyjson_obj_iter iterator = yyjson_obj_iter_with(object);
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&iterator))) {
        if (name_index(yyjson_get_str(key), keys, count) < 0) {
            return yyjson_get_str(key);
        }
    }
    return NULL;
}

static const presented_component *find_component(const component_presentation *p, uint64_t id)
{
    for (int i = 0; i < acount(p->components); i++) {
        if (p->components[i].id == id) {
            return &p->components[i];
        }
    }
    return NULL;
}

static const presented_field *find_field(const component_presentation *p, uint64_t component, int field)
{
    for (int i = 0; i < acount(p->fields); i++) {
        if (p->fields[i].component == component && p->fields[i].field == field) {
            return &p->fields[i];
        }
    }
    return NULL;
}

static int field_index(const type_desc *type, const char *name)
{
    for (int i = 0; i < type->field_count; i++) {
        if (strcmp(type->fields[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static bool numeric(pin_type type)
{
    return type >= PIN_I8 && type <= PIN_F64;
}

static bool read_field(
    component_presentation *out,
    const ecs_component_desc *desc,
    const char *component,
    yyjson_val *key,
    char *error,
    size_t size
)
{
    const char *name = yyjson_get_str(key);
    yyjson_val *value = yyjson_obj_iter_get_val(key);
    int index = field_index(desc->type, name);
    if (index < 0) {
        return fail(error, size, "%s: unknown field %s", component, name);
    }
    if (find_field(out, desc->id, index)) {
        return fail(error, size, "%s.%s is listed twice", component, name);
    }
    if (!yyjson_is_obj(value)) {
        return fail(error, size, "%s.%s must be a mapping", component, name);
    }
    const char *extra = unknown_key(value, FIELD_KEYS, (int)(sizeof FIELD_KEYS / sizeof *FIELD_KEYS));
    if (extra) {
        return fail(error, size, "%s.%s: unknown key %s", component, name, extra);
    }
    if (!numeric(desc->type->fields[index].type)) {
        return fail(error, size, "%s.%s: range and unit need a numeric field", component, name);
    }
    presented_field field = { .component = desc->id, .field = index };
    yyjson_val *range = yyjson_obj_get(value, "range");
    if (range) {
        yyjson_val *min = yyjson_arr_get(range, 0), *max = yyjson_arr_get(range, 1);
        if (yyjson_arr_size(range) != 2 || !yyjson_is_num(min) ||
            !yyjson_is_num(max) || !(yyjson_get_num(min) < yyjson_get_num(max))) {
            return fail(error, size, "%s.%s: range needs a minimum below its maximum", component, name);
        }
        field.min = yyjson_get_num(min);
        field.max = yyjson_get_num(max);
    }
    yyjson_val *unit = yyjson_obj_get(value, "unit");
    if (unit && (!yyjson_is_str(unit) || !yyjson_get_len(unit))) {
        return fail(error, size, "%s.%s: unit must not be empty", component, name);
    }
    field.unit = unit ? xstrdup(yyjson_get_str(unit)) : NULL;
    apush(out->fields, field);
    return true;
}

static bool read_component(
    component_presentation *out,
    const game_prototype_registry *registry,
    yyjson_val *key,
    char *error,
    size_t size
)
{
    const char *name = yyjson_get_str(key);
    yyjson_val *value = yyjson_obj_iter_get_val(key);
    const ecs_component_desc *desc = game_prototype_registry_find(registry, name);
    if (!desc) {
        return fail(error, size, "unknown component %s", name);
    }
    if (find_component(out, desc->id)) {
        return fail(error, size, "component %s is listed twice", name);
    }
    if (!yyjson_is_obj(value)) {
        return fail(error, size, "%s must be a mapping", name);
    }
    const char *extra = unknown_key(
        value,
        COMPONENT_KEYS,
        (int)(sizeof COMPONENT_KEYS / sizeof *COMPONENT_KEYS)
    );
    if (extra) {
        return fail(error, size, "%s: unknown key %s", name, extra);
    }
    presented_component component = { .id = desc->id, .icon = COMPONENT_ICON_NONE };
    yyjson_val *tab = yyjson_obj_get(value, "tab");
    if (tab) {
        /* Object is where untabbed components go, not a tab to name. */
        int index = name_index(yyjson_get_str(tab), COMPONENT_TAB_NAMES + 1, COMPONENT_TAB_COUNT - 1);
        if (index < 0) {
            return fail(
                error,
                size,
                "%s: unknown tab %s",
                name,
                yyjson_is_str(tab) ? yyjson_get_str(tab) : ""
            );
        }
        component.tab = (component_tab)(index + 1);
    }
    yyjson_val *icon = yyjson_obj_get(value, "icon");
    if (icon) {
        int index = name_index(yyjson_get_str(icon), ICON_NAMES, COMPONENT_ICON_COUNT);
        if (index < 0) {
            return fail(
                error,
                size,
                "%s: unknown icon %s",
                name,
                yyjson_is_str(icon) ? yyjson_get_str(icon) : ""
            );
        }
        component.icon = (component_icon)index;
    }
    apush(out->components, component);
    yyjson_val *fields = yyjson_obj_get(value, "fields");
    if (fields && !yyjson_is_obj(fields)) {
        return fail(error, size, "%s: fields must be a mapping", name);
    }
    yyjson_obj_iter iterator = yyjson_obj_iter_with(fields);
    yyjson_val *field;
    while ((field = yyjson_obj_iter_next(&iterator))) {
        if (!read_field(out, desc, name, field, error, size)) {
            return false;
        }
    }
    return true;
}

static bool read_document(
    component_presentation *out,
    const game_prototype_registry *registry,
    yyjson_val *root,
    char *error,
    size_t size
)
{
    if (!yyjson_is_obj(root)) {
        return fail(error, size, "the document must be a mapping");
    }
    const char *extra = unknown_key(root, DOCUMENT_KEYS, (int)(sizeof DOCUMENT_KEYS / sizeof *DOCUMENT_KEYS));
    if (extra) {
        return fail(error, size, "unknown key %s", extra);
    }
    yyjson_val *components = yyjson_obj_get(root, "components");
    if (components && !yyjson_is_obj(components)) {
        return fail(error, size, "components must be a mapping");
    }
    yyjson_obj_iter iterator = yyjson_obj_iter_with(components);
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&iterator))) {
        if (!read_component(out, registry, key, error, size)) {
            return false;
        }
    }
    return true;
}

bool component_presentation_read(
    const char *text,
    const game_prototype_registry *registry,
    component_presentation *out,
    char *error,
    size_t error_size
)
{
    assert(text && registry && out);
    yyjson_doc *doc = asset_text_read(text, error, error_size);
    if (!doc) {
        return false;
    }
    component_presentation next = { 0 };
    bool ok = read_document(&next, registry, yyjson_doc_get_root(doc), error, error_size);
    yyjson_doc_free(doc);
    if (!ok) {
        component_presentation_free(&next);
        return false;
    }
    component_presentation_free(out);
    *out = next;
    return true;
}

void component_presentation_free(component_presentation *presentation)
{
    for (int i = 0; i < acount(presentation->fields); i++) {
        free(presentation->fields[i].unit);
    }
    afree(presentation->fields);
    afree(presentation->components);
    *presentation = (component_presentation){ 0 };
}

component_tab component_presentation_tab(
    const component_presentation *presentation,
    const ecs_component_desc *desc
)
{
    const presented_component *component = find_component(presentation, desc->id);
    return component ? component->tab : COMPONENT_TAB_OBJECT;
}

component_icon component_presentation_icon(
    const component_presentation *presentation,
    const ecs_component_desc *desc
)
{
    const presented_component *component = find_component(presentation, desc->id);
    return component ? component->icon : COMPONENT_ICON_NONE;
}

const presented_field *component_presentation_field(
    const component_presentation *presentation,
    const ecs_component_desc *desc,
    int field
)
{
    return find_field(presentation, desc->id, field);
}
