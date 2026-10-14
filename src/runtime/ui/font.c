#include "runtime/ui/font.h"

#include "support/array.h"
#include "support/xalloc.h"
#include "types/value_wire.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FONT_BYTES (16 * 1024 * 1024)
static unsigned be16(const unsigned char *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}
static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
bool ui_font_valid(const ui_font *f)
{
    if (!f->key || !*f->key || !f->family || !*f->family ||
        strlen(f->key) > 1024 || strlen(f->family) > 255 || !f->data ||
        f->length < 12 || f->length > FONT_BYTES || f->style < 0 ||
        f->style > 1 || f->weight < 1 || f->weight > 1000) {
        return false;
    }
    uint32_t tag = be32(f->data);
    if (tag != UINT32_C(0x00010000) && tag != UINT32_C(0x4f54544f) &&
        tag != UINT32_C(0x74727565)) {
        return false;
    }
    unsigned count = be16(f->data + 4);
    if (!count || count > 4096 || count > (f->length - 12) / 16) {
        return false;
    }
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *row = f->data + 12 + i * 16;
        size_t offset = be32(row + 8), length = be32(row + 12);
        if (offset > f->length || length > f->length - offset) {
            return false;
        }
    }
    return true;
}
void ui_font_free(ui_font *f)
{
    free((void *)f->key);
    free((void *)f->family);
    free((void *)f->data);
    *f = (ui_font){ 0 };
}
static void wire(execution_wire *w, ui_font *f)
{
    uint64_t magic = UINT64_C(0x31544e4642484343), version = UI_FONT_PREPARED_VERSION;
    execution_wire_u64(w, &magic);
    execution_wire_u64(w, &version);
    if (!w->error && (magic != UINT64_C(0x31544e4642484343) ||
                      version != UI_FONT_PREPARED_VERSION)) {
        w->error = "Unknown prepared font format";
    }
    char *key = (char *)f->key, *family = (char *)f->family;
    execution_wire_owned_string(w, &key);
    execution_wire_owned_string(w, &family);
    if (w->reading) {
        f->key = key;
        f->family = family;
    }
    execution_wire_int(w, &f->style);
    execution_wire_int(w, &f->weight);
    execution_wire_bool(w, &f->fallback);
    const unsigned char *bytes = f->data;
    size_t length = f->length;
    execution_wire_blob(w, &bytes, &length);
    if (!w->error && (length > FONT_BYTES || length < 12)) {
        w->error = "Invalid font byte extent";
    }
    if (w->reading && !w->error) {
        unsigned char *owned = execution_wire_allocate_owned(w, length);
        if (owned) {
            memcpy(owned, bytes, length);
            f->data = owned;
            f->length = length;
        }
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
bool ui_font_prepared_write(const ui_font *f, unsigned char **data, size_t *length, char *error, size_t size)
{
    assert(f && data && length);
    *data = NULL;
    *length = 0;
    execution_wire w = { 0 };
    ui_font view = *f;
    if (!ui_font_valid(f)) {
        w.error = "Invalid prepared font";
    }
    if (!w.error) {
        wire(&w, &view);
    }
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
bool ui_font_prepared_read_budget(
    const void *data,
    size_t length,
    size_t *remaining,
    ui_font *result,
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
    ui_font decoded = { 0 };
    if (!data || length > EXECUTION_WIRE_MAX_BYTES) {
        w.error = "Invalid font input extent";
    }
    if (!w.error) {
        wire(&w, &decoded);
    }
    if (!w.error && (w.offset != w.size || !ui_font_valid(&decoded))) {
        w.error = "Invalid prepared font";
    }
    arena_free(&storage);
    if (!report(&w, error, size)) {
        ui_font_free(&decoded);
        return false;
    }
    *remaining -= w.owned_bytes - (EXECUTION_WIRE_MAX_BYTES - budget);
    ui_font_free(result);
    *result = decoded;
    return true;
}
