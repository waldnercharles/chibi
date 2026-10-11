#include "runtime/behaviors/module_set.h"

#include "execution/prepared.h"
#include "support/array.h"
#include "types/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const execution_module *const *modules;
    int count;
    size_t bytes, root_bytes;
    char *error;
    size_t size;
} module_set_reader;
static bool fail(module_set_reader *r, const char *message)
{
    if (r->size && !r->error[0]) {
        snprintf(r->error, r->size, "%s", message);
    }
    return false;
}
typedef struct
{
    const execution_module *module;
    int index;
    execution_module_event_info info;
} package_event;

static int event_id_compare(const void *left, const void *right)
{
    const package_event *a = left, *b = right;
    return (a->info.id > b->info.id) - (a->info.id < b->info.id);
}
static int event_name_compare(const void *left, const void *right)
{
    const package_event *a = left, *b = right;
    return strcmp(a->info.name, b->info.name);
}
static bool same_text(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}
static bool same_event(const package_event *a, const package_event *b)
{
    if (a->info.id != b->info.id || a->info.abi != b->info.abi ||
        !same_text(a->info.name, b->info.name) ||
        a->info.field_count != b->info.field_count) {
        return false;
    }
    for (int i = 0; i < a->info.field_count; i++) {
        execution_module_event_field_info
            x = execution_module_event_field_at(a->module, a->index, i);
        execution_module_event_field_info
            y = execution_module_event_field_at(b->module, b->index, i);
        if (!same_text(x.name, y.name) || x.nominal != y.nominal ||
            x.initial.type != y.initial.type ||
            x.initial.container != y.initial.container ||
            !same_text(x.initial.type_name, y.initial.type_name) ||
            x.has_default != y.has_default || x.default_owner != y.default_owner ||
            (x.has_default && !pin_value_equal(&x.initial, &y.initial))) {
            return false;
        }
    }
    return true;
}
static bool validate_events(module_set_reader *r, const execution_module *const *modules)
{
    package_event *events = NULL;
    int fields = 0;
    bool valid = false;
    for (int i = 0; i < acount(modules); i++) {
        const execution_module *module = modules[i];
        for (int j = 0; j < execution_module_event_count(module); j++) {
            execution_module_event_info info = execution_module_event_at(module, j);
            if (acount(events) == MODULE_SET_MAX_EVENT_DECLARATIONS ||
                info.field_count > MODULE_SET_MAX_EVENT_FIELDS - fields) {
                fail(r, "module set event declarations exceed limit");
                goto done;
            }
            fields += info.field_count;
            apush(events, ((package_event){ module, j, info }));
        }
    }
    if (acount(events) > 1) {
        asort(events, event_id_compare);
        for (int i = 1; i < acount(events); i++) {
            if (events[i - 1].info.id == events[i].info.id &&
                !same_event(&events[i - 1], &events[i])) {
                fail(r, "module set event identity has conflicting declarations");
                goto done;
            }
        }
        asort(events, event_name_compare);
        for (int i = 1; i < acount(events); i++) {
            if (!strcmp(events[i - 1].info.name, events[i].info.name) &&
                events[i - 1].info.id != events[i].info.id) {
                fail(r, "module set event name has conflicting identities");
                goto done;
            }
        }
    }
    valid = true;
done:
    afree(events);
    return valid;
}

typedef struct
{
    const execution_module *module;
    bool active;
} package_dependency;

static bool same_generation(module_set_reader *r, const execution_module *a, const execution_module *b)
{
    unsigned char *left = NULL, *right = NULL;
    size_t left_size = 0, right_size = 0;
    char error[256] = { 0 };
    bool equal = false;
    if (!execution_prepared_write(a, &left, &left_size, error, sizeof error)) {
        fail(r, error);
        goto done;
    }
    if (left_size > MODULE_SET_MAX_BYTES - r->bytes) {
        fail(r, "module set dependency bytes exceed limit");
        goto done;
    }
    r->bytes += left_size;
    if (!execution_prepared_write(b, &right, &right_size, error, sizeof error)) {
        fail(r, error);
        goto done;
    }
    if (right_size > MODULE_SET_MAX_BYTES - r->bytes) {
        fail(r, "module set dependency bytes exceed limit");
        goto done;
    }
    r->bytes += right_size;
    equal = left_size == right_size && !memcmp(left, right, left_size);
    if (!equal) {
        fail(r, "module set dependency identity has conflicting generations");
    }
done:
    free(left);
    free(right);
    return equal;
}
static bool account_dependency(module_set_reader *r, const execution_module *module)
{
    for (int i = 0; r->root_bytes && i < r->count; i++) {
        if (r->modules[i] == module) {
            return true;
        }
    }
    unsigned char *bytes = NULL;
    size_t length = 0;
    char error[256] = { 0 };
    if (!execution_prepared_write(module, &bytes, &length, error, sizeof error)) {
        return fail(r, error);
    }
    free(bytes);
    if (length > MODULE_SET_MAX_BYTES - r->bytes) {
        return fail(r, "module set dependency bytes exceed limit");
    }
    r->bytes += length;
    return true;
}
static bool collect_dependencies(
    module_set_reader *r,
    const execution_module *module,
    int depth,
    package_dependency **dependencies
)
{
    if (depth > MODULE_SET_MAX_IMPORT_DEPTH) {
        return fail(r, "module set dependency depth exceeds limit");
    }
    for (int i = 0; i < acount(*dependencies); i++) {
        const execution_module *previous = (*dependencies)[i].module;
        if (previous == module) {
            return !(*dependencies)[i].active ||
                   fail(r, "module set dependency cycle");
        }
        if (execution_module_id(previous) == execution_module_id(module)) {
            if ((*dependencies)[i].active) {
                return fail(r, "module set dependency identity cycle");
            }
            return same_generation(r, previous, module);
        }
    }
    if (acount(*dependencies) == MODULE_SET_MAX_MODULES) {
        return fail(r, "module set dependency count exceeds limit");
    }
    if (!account_dependency(r, module)) {
        return false;
    }
    int index = acount(*dependencies);
    apush(*dependencies, ((package_dependency){ module, true }));
    for (int i = 0; i < execution_module_import_count(module); i++) {
        if (!collect_dependencies(r, execution_module_import_at(module, i), depth + 1, dependencies)) {
            return false;
        }
    }
    (*dependencies)[index].active = false;
    return true;
}
static bool validate_modules(module_set_reader *r)
{
    package_dependency *dependencies = NULL;
    const execution_module **modules = NULL;
    bool valid = false;
    for (int i = 0; i < r->count; i++) {
        if (!collect_dependencies(r, r->modules[i], 1, &dependencies)) {
            goto done;
        }
    }
    for (int i = 0; i < acount(dependencies); i++) {
        apush(modules, dependencies[i].module);
    }
    valid = validate_events(r, modules);
done:
    afree(modules);
    afree(dependencies);
    return valid;
}

bool module_set_validate(
    const execution_module *const *modules,
    int count,
    size_t root_bytes,
    char *error,
    size_t size
)
{
    assert(count >= 0 && (!count || modules) && (!size || error));
    if (size) {
        error[0] = '\0';
    }
    module_set_reader r = {
        .modules = modules,
        .count = count,
        .bytes = root_bytes,
        .root_bytes = root_bytes,
        .error = error,
        .size = size,
    };
    if (count > MODULE_SET_MAX_MODULES || root_bytes > MODULE_SET_MAX_BYTES) {
        return fail(&r, "module set exceeds its count or byte limit");
    }
    for (int i = 0; i < count; i++) {
        assert(modules[i]);
    }
    return validate_modules(&r);
}
