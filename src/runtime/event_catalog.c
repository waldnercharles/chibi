#include "runtime/event_catalog.h"

#include "runtime/behaviors/module_set.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/value_wire.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    uint64_t id;
    execution_prepared_type type;
} event_type;

struct event_catalog
{
    int refs;
    arena storage;
    event_declaration *events;
    int count;
    event_type *types;
    int type_count;
};
static const uint64_t magic = UINT64_C(0x3154564553484343);
static bool fail(char *error, size_t size, const char *text)
{
    if (size) {
        snprintf(error, size, "%s", text);
    }
    return false;
}
static void *allocate(execution_wire *wire, size_t bytes)
{
    void *data = execution_wire_allocate(wire, bytes);
    if (data) {
        memset(data, 0, bytes);
    }
    return data;
}
static void name(execution_wire *wire, const char **value)
{
    execution_wire_string(wire, value);
    if (!wire->error && (!*value || !(*value)[0] || strnlen(*value, 1025) > 1024)) {
        wire->error = "Event catalog name is invalid";
    }
}
static void field(execution_wire *wire, execution_module_event_field_info *f)
{
    name(wire, &f->name);
    execution_wire_u64(wire, &f->nominal);
    execution_wire_bool(wire, &f->has_default);
    execution_wire_bool(wire, &f->default_owner);
    execution_wire_value(wire, &f->initial);
    if (!wire->error &&
        (f->initial.type <= PIN_FLOW || f->initial.type > PIN_ENUM ||
         f->initial.container > PIN_CONTAINER_VIEW ||
         (f->initial.container == PIN_CONTAINER_VIEW && f->initial.ptr) ||
         (f->default_owner && (!f->has_default || f->initial.type != PIN_HANDLE ||
                               f->initial.container != PIN_CONTAINER_SINGLE)))) {
        wire->error = "Event catalog payload storage or owner default is "
                      "invalid";
    }
}
static void declarations(execution_wire *wire, event_catalog *catalog, const execution_prepared_linker *linker)
{
    uint64_t identity = magic, version = EVENT_CATALOG_VERSION;
    execution_wire_u64(wire, &identity);
    execution_wire_u64(wire, &version);
    if (!wire->error && (identity != magic || version != EVENT_CATALOG_VERSION)) {
        wire->error = "Event catalog magic or version is unsupported";
    }
    execution_wire_count(wire, &catalog->type_count, 5);
    if (!wire->error && catalog->type_count > MODULE_SET_MAX_EVENT_FIELDS) {
        wire->error = "Event catalog type count exceeds limit";
    }
    if (wire->reading) {
        catalog->types = allocate(
            wire,
            (size_t)catalog->type_count * sizeof *catalog->types
        );
    }
    for (int i = 0; i < catalog->type_count && !wire->error; i++) {
        event_type *t = &catalog->types[i];
        uint64_t storage = t->type.storage;
        execution_wire_u64(wire, &t->id);
        name(wire, &t->type.name);
        execution_wire_u64(wire, &storage);
        execution_wire_size(wire, &t->type.size);
        execution_wire_u64(wire, &t->type.layout);
        if (wire->reading) {
            t->type.storage = (pin_type)storage;
        }
        if (!wire->error && (!t->id || storage <= PIN_FLOW || storage > PIN_ENUM)) {
            wire->error = "Event catalog type requirement is invalid";
        }
        for (int j = 0; j < i && !wire->error; j++) {
            if (catalog->types[j].id == t->id) {
                wire->error = "Event catalog type identity is duplicated";
            }
        }
        if (wire->reading && !wire->error) {
            execution_prepared_type actual = { 0 };
            if (!linker || !linker->type ||
                !linker->type(linker->user, t->id, &actual) || !actual.name ||
                strcmp(actual.name, t->type.name) || actual.storage != t->type.storage ||
                actual.size != t->type.size || actual.layout != t->type.layout) {
                wire->error = "Event catalog selected type is incompatible";
            }
        }
    }
    execution_wire_count(wire, &catalog->count, 4);
    if (!wire->error && catalog->count > MODULE_SET_MAX_EVENT_DECLARATIONS) {
        wire->error = "Event catalog declaration limit exceeded";
    }
    if (wire->reading) {
        catalog
            ->events = allocate(wire, (size_t)catalog->count * sizeof *catalog->events);
    }
    int field_count = 0;
    for (int i = 0; i < catalog->count && !wire->error; i++) {
        event_declaration *d = &catalog->events[i];
        execution_wire_u64(wire, &d->event.id);
        execution_wire_u64(wire, &d->event.abi);
        name(wire, &d->event.name);
        execution_wire_count(wire, &d->event.field_count, 5);
        if (!wire->error &&
            (!d->event.id || !d->event.abi ||
             d->event.field_count > MODULE_SET_MAX_EVENT_FIELDS - field_count)) {
            wire->error = "Event catalog identity or field count is invalid";
        }
        field_count += d->event.field_count;
        if (wire->reading) {
            d->fields = allocate(wire, (size_t)d->event.field_count * sizeof *d->fields);
        }
        if (!wire->error && d->event.field_count && !d->fields) {
            wire->error = "Event catalog fields are missing";
        }
        for (int j = 0; j < d->event.field_count && !wire->error; j++) {
            execution_module_event_field_info f = d->fields[j];
            field(wire, &f);
            if (f.nominal && !wire->error) {
                const event_type *type = NULL;
                for (int k = 0; k < catalog->type_count; k++) {
                    if (catalog->types[k].id == f.nominal) {
                        type = &catalog->types[k];
                        break;
                    }
                }
                if (!type || type->type.storage != f.initial.type || !f.initial.type_name ||
                    strcmp(type->type.name, f.initial.type_name)) {
                    wire->error = "Event catalog nominal field requirement is "
                                  "incompatible";
                }
            }
            if (wire->reading && !wire->error) {
                ((execution_module_event_field_info *)d->fields)[j] = f;
            }
            for (int k = 0; k < j && !wire->error; k++) {
                if (!strcmp(d->fields[k].name, f.name)) {
                    wire->error = "Event catalog field name is duplicated";
                }
            }
        }
        for (int j = 0; j < i && !wire->error; j++) {
            if (d->event.id == catalog->events[j].event.id ||
                !strcmp(d->event.name, catalog->events[j].event.name)) {
                wire->error = "Event catalog declaration identity is "
                              "duplicated";
            }
        }
    }
}
bool event_catalog_write(
    const event_catalog *catalog,
    unsigned char **data,
    size_t *length,
    char *error,
    size_t size
)
{
    assert(catalog && data && length && (!size || error));
    *data = NULL;
    *length = 0;
    execution_wire wire = { 0 };
    event_catalog view = *catalog;
    declarations(&wire, &view, NULL);
    if (wire.error) {
        afree(wire.data);
        return fail(error, size, wire.error);
    }
    *data = xmalloc(wire.size);
    memcpy(*data, wire.data, wire.size);
    *length = wire.size;
    afree(wire.data);
    return true;
}
event_catalog *event_catalog_read_budget(
    const void *data,
    size_t length,
    const execution_prepared_linker *types,
    size_t *remaining,
    char *error,
    size_t size
)
{
    assert(remaining && (!size || error));
    size_t budget = *remaining < EXECUTION_WIRE_MAX_BYTES ? *remaining
                                                          : EXECUTION_WIRE_MAX_BYTES;
    if (budget < sizeof(event_catalog)) {
        fail(error, size, "Event catalog retained storage budget exceeded");
        return NULL;
    }
    size_t allocation_budget = budget - sizeof(event_catalog);
    event_catalog *catalog = xcalloc(1, sizeof *catalog);
    catalog->refs = 1;
    execution_wire wire = { .data = (unsigned char *)data,
                            .size = length,
                            .reading = true,
                            .storage = &catalog->storage,
                            .owned_bytes = EXECUTION_WIRE_MAX_BYTES - budget,
                            .storage_budget = &allocation_budget };
    declarations(&wire, catalog, types);
    if (!wire.error && wire.offset != wire.size) {
        wire.error = "Event catalog has trailing bytes";
    }
    if (wire.error) {
        fail(error, size, wire.error);
        event_catalog_free(catalog);
        return NULL;
    }
    size_t capacity = arena_storage_bytes(&catalog->storage);
    if (capacity > budget || sizeof *catalog > budget - capacity) {
        fail(error, size, "Event catalog retained storage budget exceeded");
        event_catalog_free(catalog);
        return NULL;
    }
    *remaining -= capacity + sizeof *catalog;
    return catalog;
}
event_catalog *event_catalog_read(
    const void *data,
    size_t length,
    const execution_prepared_linker *types,
    char *error,
    size_t size
)
{
    size_t remaining = EXECUTION_WIRE_MAX_BYTES;
    return event_catalog_read_budget(data, length, types, &remaining, error, size);
}
event_catalog *event_catalog_new(
    const event_declaration *events,
    int count,
    const execution_prepared_linker *types,
    char *error,
    size_t size
)
{
    assert(!size || error);
    if (count < 0 || count > MODULE_SET_MAX_EVENT_DECLARATIONS || (count && !events)) {
        fail(error, size, "Event catalog declaration count is invalid");
        return NULL;
    }
    event_catalog view = { .events = (event_declaration *)events, .count = count };
    int field_total = 0;
    for (int i = 0; i < count; i++) {
        if (events[i].event.field_count < 0 ||
            events[i].event.field_count > MODULE_SET_MAX_EVENT_FIELDS - field_total ||
            (events[i].event.field_count && !events[i].fields)) {
            fail(error, size, "Event catalog fields are invalid");
            afree(view.types);
            return NULL;
        }
        field_total += events[i].event.field_count;
        for (int j = 0; j < events[i].event.field_count; j++) {
            uint64_t id = events[i].fields[j].nominal;
            bool found = !id;
            for (int k = 0; k < acount(view.types); k++) {
                found |= view.types[k].id == id;
            }
            if (found) {
                continue;
            }
            event_type requirement = { .id = id };
            if (!types || !types->type ||
                !types->type(types->user, id, &requirement.type)) {
                fail(error, size, "Event catalog nominal type is unavailable");
                afree(view.types);
                return NULL;
            }
            apush(view.types, requirement);
        }
    }
    view.type_count = acount(view.types);
    unsigned char *data;
    size_t length;
    if (!event_catalog_write(&view, &data, &length, error, size)) {
        afree(view.types);
        return NULL;
    }
    event_catalog *catalog = event_catalog_read(data, length, types, error, size);
    afree(view.types);
    free(data);
    return catalog;
}
void event_catalog_retain(event_catalog *catalog)
{
    assert(catalog && catalog->refs > 0);
    catalog->refs++;
}
void event_catalog_free(event_catalog *catalog)
{
    if (!catalog || --catalog->refs) {
        return;
    }
    arena_free(&catalog->storage);
    free(catalog);
}
size_t event_catalog_storage_bytes(const event_catalog *catalog)
{
    size_t capacity = arena_storage_bytes(&catalog->storage);
    return capacity + sizeof *catalog;
}
int event_catalog_count(const event_catalog *catalog)
{
    return catalog->count;
}
execution_module_event_info event_catalog_at(const event_catalog *catalog, int index)
{
    assert(index >= 0 && index < catalog->count);
    return catalog->events[index].event;
}
execution_module_event_field_info event_catalog_field_at(const event_catalog *catalog, int event, int index)
{
    assert(
        event >= 0 && event < catalog->count && index >= 0 &&
        index < catalog->events[event].event.field_count
    );
    return catalog->events[event].fields[index];
}
int event_catalog_find(const event_catalog *catalog, const char *name)
{
    for (int i = 0; i < catalog->count; i++) {
        if (!strcmp(catalog->events[i].event.name, name)) {
            return i;
        }
    }
    return -1;
}
static bool field_equal(execution_module_event_field_info a, execution_module_event_field_info b)
{
    execution_wire left = { 0 }, right = { 0 };
    field(&left, &a);
    field(&right, &b);
    bool same = !left.error && !right.error && left.size == right.size &&
                !memcmp(left.data, right.data, left.size);
    afree(left.data);
    afree(right.data);
    return same;
}
static bool module_events(
    const event_catalog *catalog,
    const execution_module *module,
    char *error,
    size_t size
)
{
    for (int i = 0; i < execution_module_event_count(module); i++) {
        execution_module_event_info event = execution_module_event_at(module, i);
        for (int j = 0; j < catalog->count; j++) {
            const event_declaration *d = &catalog->events[j];
            if (d->event.id != event.id && strcmp(d->event.name, event.name)) {
                continue;
            }
            if (d->event.id != event.id || d->event.abi != event.abi ||
                strcmp(d->event.name, event.name) ||
                d->event.field_count != event.field_count) {
                return fail(error, size, "Event catalog and executable declaration disagree");
            }
            for (int f = 0; f < event.field_count; f++) {
                if (!field_equal(d->fields[f], execution_module_event_field_at(module, i, f))) {
                    return fail(error, size, "Event catalog and executable payload disagree");
                }
            }
        }
    }
    return true;
}
static bool visit(
    const event_catalog *catalog,
    const execution_module *module,
    const execution_module ***seen,
    int depth,
    char *error,
    size_t size
)
{
    if (depth > MODULE_SET_MAX_IMPORT_DEPTH) {
        return fail(error, size, "Event catalog import depth exceeds limit");
    }
    for (int i = 0; i < acount(*seen); i++) {
        if (execution_module_id((*seen)[i]) == execution_module_id(module)) {
            return true;
        }
    }
    if (acount(*seen) == MODULE_SET_MAX_MODULES) {
        return fail(error, size, "Event catalog module closure exceeds limit");
    }
    apush(*seen, module);
    if (!module_events(catalog, module, error, size)) {
        return false;
    }
    for (int i = 0; i < execution_module_import_count(module); i++) {
        if (!visit(catalog, execution_module_import_at(module, i), seen, depth + 1, error, size)) {
            return false;
        }
    }
    return true;
}
bool event_catalog_validate(
    const event_catalog *catalog,
    const execution_module *const *modules,
    int count,
    char *error,
    size_t size
)
{
    assert(catalog && (!size || error));
    if (!module_set_validate(modules, count, 0, error, size)) {
        return false;
    }
    const execution_module **seen = NULL;
    bool valid = true;
    for (int i = 0; valid && i < count; i++) {
        valid = visit(catalog, modules[i], &seen, 1, error, size);
    }
    afree(seen);
    return valid;
}
