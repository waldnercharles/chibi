#include "runtime/behaviors/package.h"

#include "runtime/behaviors/module_set.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const package_module *records;
    int count, depth;
    const execution_prepared_linker *native;
    const event_catalog *events;
    execution_prepared_linker linker;
    execution_module **modules;
    execution_module **imports;
    uint64_t *import_fingerprints;
    unsigned char *state;
    behavior_backend backend;
    char *error;
    size_t size, bytes;
    size_t *remaining;
    const execution_module *charged[PACKAGE_MAX_MODULES];
    int charged_count;
} package_reader;

static bool fail(package_reader *r, const char *message)
{
    if (r->size && !r->error[0]) {
        snprintf(r->error, r->size, "%s", message);
    }
    return false;
}
static bool host(void *user, uint64_t id, execution_prepared_host *out)
{
    package_reader *r = user;
    return r->native && r->native->host && r->native->host(r->native->user, id, out);
}
static bool command(void *user, uint64_t id, const char *reference, execution_prepared_command *out)
{
    package_reader *r = user;
    return r->native && r->native->command &&
           r->native->command(r->native->user, id, reference, out);
}
static bool query(void *user, uint64_t id, execution_prepared_query *out)
{
    package_reader *r = user;
    return r->native && r->native->query && r->native->query(r->native->user, id, out);
}
static bool event(void *user, uint64_t id, uint64_t abi)
{
    package_reader *r = user;
    for (int i = 0; r->events && i < event_catalog_count(r->events); i++) {
        execution_module_event_info declaration = event_catalog_at(r->events, i);
        if (declaration.id == id) {
            return declaration.abi == abi;
        }
    }
    return r->native && r->native->event && r->native->event(r->native->user, id, abi);
}
static bool type(void *user, uint64_t id, execution_prepared_type *out)
{
    package_reader *r = user;
    return r->native && r->native->type && r->native->type(r->native->user, id, out);
}
static execution_module *read_module(package_reader *, int);
static bool charged(const package_reader *r, const execution_module *module)
{
    for (int i = 0; i < r->charged_count; i++) {
        if (r->charged[i] == module) {
            return true;
        }
    }
    return false;
}
static bool charge_import(package_reader *r, const execution_module *module, int depth)
{
    if (depth > PACKAGE_MAX_IMPORT_DEPTH) {
        return fail(r, "prepared package import depth exceeds limit");
    }
    if (charged(r, module)) {
        return true;
    }
    if (r->charged_count == PACKAGE_MAX_MODULES) {
        return fail(r, "prepared package retained generation count exceeds limit");
    }
    size_t bytes = execution_module_storage_bytes(module);
    if (bytes > *r->remaining) {
        return fail(r, "prepared package canonical storage exceeds aggregate budget");
    }
    *r->remaining -= bytes;
    r->charged[r->charged_count++] = module;
    for (int i = 0; i < execution_module_import_count(module); i++) {
        if (!charge_import(r, execution_module_import_at(module, i), depth + 1)) {
            return false;
        }
    }
    return true;
}
static execution_module *import(void *user, uint64_t id, uint64_t fingerprint)
{
    package_reader *r = user;
    for (int i = 0; i < r->count; i++) {
        if (r->records[i].id != id) {
            continue;
        }
        execution_module *module = read_module(r, i);
        if (module) {
            execution_module_retain(module);
        }
        return module;
    }
    if (r->native && r->native->import) {
        for (int i = 0; i < acount(r->imports); i++) {
            if (execution_module_id(r->imports[i]) != id) {
                continue;
            }
            if (r->import_fingerprints[i] != fingerprint) {
                fail(r, "prepared package import identity has conflicting generations");
                return NULL;
            }
            execution_module_retain(r->imports[i]);
            return r->imports[i];
        }
        if (r->count + acount(r->imports) == PACKAGE_MAX_MODULES) {
            fail(r, "prepared package imported module count exceeds limit");
            return NULL;
        }
        execution_module *module = r->native->import(r->native->user, id, fingerprint);
        if (!module) {
            return NULL;
        }
        if (execution_module_id(module) != id) {
            execution_module_free(module);
            fail(r, "prepared package import identity differs");
            return NULL;
        }
        if (!charge_import(r, module, r->depth + 1)) {
            execution_module_free(module);
            return NULL;
        }
        execution_module_retain(module);
        apush(r->imports, module);
        apush(r->import_fingerprints, fingerprint);
        return module;
    }
    fail(r, "prepared package import is missing");
    return NULL;
}
static execution_module *read_module(package_reader *r, int index)
{
    if (r->state[index] == 2) {
        return r->modules[index];
    }
    if (r->state[index] == 1) {
        fail(r, "prepared package import cycle");
        return NULL;
    }
    if (r->depth == PACKAGE_MAX_IMPORT_DEPTH) {
        fail(r, "prepared package import depth exceeds limit");
        return NULL;
    }
    r->depth++;
    r->state[index] = 1;
    const package_module *record = &r->records[index];
    char error[256] = { 0 };
    execution_module *module = execution_prepared_read_budget(
        record->data,
        record->length,
        &r->linker,
        r->backend,
        r->remaining,
        error,
        sizeof error
    );
    r->depth--;
    if (!module) {
        char diagnostic[512];
        snprintf(
            diagnostic,
            sizeof diagnostic,
            "Prepared module %.128s: %.256s",
            record->name,
            error
        );
        fail(r, diagnostic);
        return NULL;
    }
    if (execution_module_id(module) != record->id) {
        execution_module_free(module);
        fail(r, "prepared package module identity differs");
        return NULL;
    }
    if (r->charged_count == PACKAGE_MAX_MODULES) {
        execution_module_free(module);
        fail(r, "prepared package retained generation count exceeds limit");
        return NULL;
    }
    r->charged[r->charged_count++] = module;
    r->modules[index] = module;
    r->state[index] = 2;
    return module;
}

typedef struct
{
    char *name;
    execution_module *module;
} package_item;
struct package_collection
{
    package_item *items;
    int count;
    size_t metadata;
};
void package_collection_free(package_collection *collection)
{
    assert(collection);
    for (int i = 0; i < collection->count; i++) {
        free(collection->items[i].name);
        execution_module_free(collection->items[i].module);
    }
    free(collection->items);
    free(collection);
}
int package_collection_count(const package_collection *collection)
{
    assert(collection);
    return collection->count;
}
const char *package_collection_name(const package_collection *collection, int index)
{
    assert(collection && index >= 0 && index < collection->count);
    return collection->items[index].name;
}
execution_module *package_collection_module(const package_collection *collection, int index)
{
    assert(collection && index >= 0 && index < collection->count);
    return collection->items[index].module;
}
package_collection *package_read_budget(
    const package_module *records,
    int count,
    const execution_prepared_linker *native,
    const event_catalog *events,
    behavior_backend backend,
    size_t *remaining,
    char *error,
    size_t size
)
{
    assert(remaining && count >= 0 && (!count || records) && (!size || error));
    if (size) {
        error[0] = '\0';
    }
    size_t budget = *remaining;
    package_reader r = {
        .records = records,
        .count = count,
        .native = native,
        .events = events,
        .backend = backend,
        .error = error,
        .size = size,
        .remaining = &budget,
    };
    if (count > PACKAGE_MAX_MODULES) {
        fail(&r, "prepared package module count exceeds limit");
        return NULL;
    }
    size_t bytes = 0;
    size_t metadata = sizeof(package_collection) + (size_t)count * sizeof(package_item);
    for (int i = 0; i < count; i++) {
        if (!records[i].name || !records[i].name[0] ||
            strnlen(records[i].name, 1025) > 1024 || !records[i].id ||
            !records[i].data || !records[i].length) {
            fail(&r, "prepared package record is incomplete");
            return NULL;
        }
        if (records[i].length > PACKAGE_MAX_BYTES - bytes) {
            fail(&r, "prepared package bytes exceed limit");
            return NULL;
        }
        bytes += records[i].length;
        metadata += strlen(records[i].name) + 1;
        for (int j = 0; j < i; j++) {
            if (records[i].id == records[j].id ||
                !strcmp(records[i].name, records[j].name)) {
                fail(&r, "prepared package module identity or name is duplicated");
                return NULL;
            }
        }
    }
    if (metadata > budget) {
        fail(&r, "prepared package metadata exceeds aggregate budget");
        return NULL;
    }
    budget -= metadata;
    r.bytes = bytes;
    r.modules = xcalloc((size_t)count, sizeof *r.modules);
    r.state = xcalloc((size_t)count, sizeof *r.state);
    r.linker = (execution_prepared_linker){
        .user = &r,
        .host = host,
        .command = command,
        .query = query,
        .event = r.events || (native && native->event) ? event : NULL,
        .type = type,
        .import = import,
        .decoded_bytes = native ? native->decoded_bytes : 0,
        .decoded_records = native ? native->decoded_records : 0,
        .layout_work = native ? native->layout_work : 0,
    };
    package_collection *collection = NULL;
    for (int i = 0; i < count; i++) {
        if (!read_module(&r, i)) {
            goto done;
        }
    }
    if (!module_set_validate((const execution_module *const *)r.modules, count, bytes, error, size)) {
        goto done;
    }
    if (events &&
        !event_catalog_validate(events, (const execution_module *const *)r.modules, count, error, size)) {
        goto done;
    }
    collection = xcalloc(1, sizeof *collection);
    collection->count = count;
    collection->metadata = metadata;
    collection->items = xcalloc((size_t)count, sizeof *collection->items);
    for (int i = 0; i < count; i++) {
        collection->items[i] = (package_item){ xstrdup(records[i].name),
                                               r.modules[i] };
        r.modules[i] = NULL;
    }
done:
    for (int i = 0; i < count; i++) {
        if (r.modules[i]) {
            execution_module_free(r.modules[i]);
        }
    }
    for (int i = 0; i < acount(r.imports); i++) {
        execution_module_free(r.imports[i]);
    }
    afree(r.imports);
    afree(r.import_fingerprints);
    free(r.modules);
    free(r.state);
    if (collection) {
        *remaining = budget;
    }
    return collection;
}
