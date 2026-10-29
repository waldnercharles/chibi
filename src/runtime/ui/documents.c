#include "runtime/ui/documents.h"

#include "support/array.h"
#include "support/xalloc.h"
#include "types/value_wire.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define UI_DOCUMENT_LIMIT 1024
struct ui_documents
{
    prepared_ui_document **items;
    bool active;
};
static ui_documents resident;
static uint64_t revision;
void prepared_ui_document_free(prepared_ui_document *d)
{
    free(d->name);
    free(d->markup);
    free(d->style);
    free(d->body);
    for (int i = 0; i < acount(d->resources); i++) {
        free(d->resources[i].key);
    }
    afree(d->resources);
    *d = (prepared_ui_document){ 0 };
}
static bool valid(const prepared_ui_document *d)
{
    if (!d->name || !*d->name || strlen(d->name) > 127 || !d->markup ||
        !d->style || acount(d->resources) > UI_DOCUMENT_LIMIT) {
        return false;
    }
    for (int i = 0; i < acount(d->resources); i++) {
        const ui_resource *r = &d->resources[i];
        if (r->kind < UI_RESOURCE_IMAGE || r->kind > UI_RESOURCE_FONT ||
            !r->key || !*r->key || strstr(r->key, "..") || strchr(r->key, '\\')) {
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (d->resources[j].kind == r->kind &&
                !strcmp(d->resources[j].key, r->key)) {
                return false;
            }
        }
    }
    return true;
}
static void wire(execution_wire *w, prepared_ui_document *d)
{
    uint64_t magic = UINT64_C(0x3149554942484343), version = UI_DOCUMENT_VERSION;
    execution_wire_u64(w, &magic);
    execution_wire_u64(w, &version);
    if (!w->error &&
        (magic != UINT64_C(0x3149554942484343) || version != UI_DOCUMENT_VERSION)) {
        w->error = "Unknown prepared UI document format";
    }
    execution_wire_owned_string(w, &d->name);
    execution_wire_owned_string(w, &d->markup);
    execution_wire_owned_string(w, &d->style);
    execution_wire_owned_string(w, &d->body);
    int count = w->reading ? 0 : acount(d->resources);
    execution_wire_count(w, &count, 2);
    if (!w->error && count > UI_DOCUMENT_LIMIT) {
        w->error = "Prepared UI resource limit";
    }
    if (w->reading && count && !w->error) {
        int capacity = count < 16 ? 16 : count;
        if (!execution_wire_reserve(w, sizeof(dyna_hdr) + (size_t)capacity * sizeof(ui_resource))) {
            return;
        }
        asetlen(d->resources, count);
        memset(d->resources, 0, (size_t)count * sizeof *d->resources);
    }
    for (int i = 0; i < count && !w->error; i++) {
        ui_resource *r = &d->resources[i];
        int kind = r->kind;
        execution_wire_int(w, &kind);
        if (w->reading) {
            r->kind = kind;
        }
        execution_wire_owned_string(w, &r->key);
    }
}
static bool report(execution_wire *w, char *error, size_t size)
{
    if (!w->error) {
        return true;
    }
    if (size) {
        snprintf(error, size, "%s", w->error);
    }
    return false;
}
bool ui_document_prepared_write(
    const prepared_ui_document *d,
    unsigned char **data,
    size_t *length,
    char *error,
    size_t size
)
{
    assert(d && data && length);
    *data = NULL;
    *length = 0;
    if (!valid(d)) {
        if (size) {
            snprintf(error, size, "Invalid prepared UI document");
        }
        return false;
    }
    execution_wire w = { 0 };
    prepared_ui_document view = *d;
    wire(&w, &view);
    if (!report(&w, error, size)) {
        afree(w.data);
        return false;
    }
    *data = xmalloc(w.size);
    memcpy(*data, w.data, w.size);
    *length = w.size;
    afree(w.data);
    return true;
}
bool ui_document_prepared_read_budget(
    const void *data,
    size_t length,
    size_t *remaining,
    prepared_ui_document *result,
    char *error,
    size_t size
)
{
    assert(remaining && result);
    size_t budget = *remaining < EXECUTION_WIRE_MAX_BYTES ? *remaining
                                                          : EXECUTION_WIRE_MAX_BYTES;
    size_t allocation_budget = budget;
    arena storage = { 0 };
    execution_wire w = { .data = (unsigned char *)data,
                         .size = length,
                         .reading = true,
                         .storage = &storage,
                         .storage_budget = &allocation_budget,
                         .owned_bytes = EXECUTION_WIRE_MAX_BYTES - budget };
    prepared_ui_document decoded = { 0 };
    if (!data || length > EXECUTION_WIRE_MAX_BYTES) {
        w.error = "Invalid prepared UI input extent";
    }
    if (!w.error) {
        wire(&w, &decoded);
    }
    if (!w.error && w.offset != w.size) {
        w.error = "Trailing prepared UI document bytes";
    }
    if (!w.error && !valid(&decoded)) {
        w.error = "Invalid prepared UI document";
    }
    arena_free(&storage);
    if (!report(&w, error, size)) {
        prepared_ui_document_free(&decoded);
        return false;
    }
    size_t used = w.owned_bytes - (EXECUTION_WIRE_MAX_BYTES - budget);
    prepared_ui_document_free(result);
    *result = decoded;
    *remaining -= used;
    return true;
}
ui_documents *ui_documents_new_budget(size_t *remaining, bool active)
{
    assert(remaining);
    if (*remaining < sizeof(ui_documents)) {
        return NULL;
    }
    *remaining -= sizeof(ui_documents);
    ui_documents *d = xcalloc(1, sizeof *d);
    d->active = active;
    return d;
}
bool ui_documents_add_owned_budget(ui_documents *table, prepared_ui_document *d, size_t *remaining)
{
    assert(table && d && remaining);
    if (acount(table->items) >= UI_DOCUMENT_LIMIT || !valid(d) ||
        ui_documents_find(table, d->name)) {
        return false;
    }
    int cap = acap(table->items), next = cap;
    if (acount(table->items) == cap) {
        next = cap ? cap * 2 : 16;
    }
    size_t charge = sizeof(prepared_ui_document) +
                    (size_t)(next - cap) * sizeof *table->items +
                    (cap ? 0 : sizeof(dyna_hdr));
    if (*remaining < charge) {
        return false;
    }
    *remaining -= charge;
    prepared_ui_document *owned = xmalloc(sizeof *owned);
    *owned = *d;
    *d = (prepared_ui_document){ 0 };
    apush(table->items, owned);
    return true;
}
void ui_documents_exchange(ui_documents *table)
{
    assert(table && table != &resident);
    ui_documents old = resident;
    resident = *table;
    *table = old;
    revision++;
}
void ui_documents_free(ui_documents *table)
{
    if (!table) {
        return;
    }
    assert(table != &resident);
    for (int i = 0; i < acount(table->items); i++) {
        prepared_ui_document_free(table->items[i]);
        free(table->items[i]);
    }
    afree(table->items);
    free(table);
}
const ui_documents *ui_documents_current()
{
    return &resident;
}
const prepared_ui_document *ui_documents_find(const ui_documents *table, const char *name)
{
    assert(table && name);
    for (int i = 0; i < acount(table->items); i++) {
        if (!strcmp(table->items[i]->name, name)) {
            return table->items[i];
        }
    }
    return NULL;
}
int ui_documents_count(const ui_documents *table)
{
    assert(table);
    return acount(table->items);
}
const prepared_ui_document *ui_documents_at(const ui_documents *table, int i)
{
    assert(table && i >= 0 && i < acount(table->items));
    return table->items[i];
}
bool ui_documents_active()
{
    return resident.active;
}
uint64_t ui_documents_revision()
{
    return revision;
}
