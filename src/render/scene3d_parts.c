#include "render/scene3d_parts.h"

#include "render/scene3d_parts_table.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/value_wire.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct
{
    char *key;
    scene3d_parts parts;
} prepared_parts;
static prepared_parts *records;
bool render_scene3d_parts_publish(const char *key, const scene3d_parts *parts, char *error, size_t size)
{
    if (!key || !*key) {
        return parts && !parts->count;
    }
    if (!parts || parts->count < 0 || parts->count > SCENE3D_PART_LIMIT) {
        snprintf(error, size, "Invalid prepared mesh part count.");
        return false;
    }
    for (int i = 0; i < parts->count; i++) {
        const scene3d_part *p = &parts->parts[i];
        if (p->primitive < PRIMITIVE_BOX || p->primitive >= PRIMITIVE_COUNT ||
            p->dimensions.x <= 0 || p->dimensions.y <= 0 || p->dimensions.z <= 0 ||
            !isfinite(
                p->dimensions.x + p->dimensions.y + p->dimensions.z +
                p->position.x + p->position.y + p->position.z + p->rotation.x +
                p->rotation.y + p->rotation.z
            ) ||
            !memchr(p->material, 0, sizeof p->material)) {
            snprintf(error, size, "Invalid prepared mesh part.");
            return false;
        }
    }
    for (int i = 0; i < acount(records); i++) {
        if (!strcmp(records[i].key, key)) {
            records[i].parts = *parts;
            return true;
        }
    }
    apush(records, ((prepared_parts){ xstrdup(key), *parts }));
    return true;
}
bool render_scene3d_parts_read(const char *key, scene3d_parts *parts, char *error, size_t size)
{
    *parts = (scene3d_parts){ 0 };
    if (!key || !*key) {
        return true;
    }
    for (int i = 0; i < acount(records); i++) {
        if (!strcmp(records[i].key, key)) {
            *parts = records[i].parts;
            return true;
        }
    }
    snprintf(error, size, "Mesh parts have not been prepared.");
    return false;
}
void render_scene3d_parts_clear()
{
    for (int i = 0; i < acount(records); i++) {
        free(records[i].key);
    }
    afree(records);
}

struct render_parts_table
{
    prepared_parts *records;
};
static bool parts_valid(const scene3d_parts *parts)
{
    if (parts->count < 0 || parts->count > SCENE3D_PART_LIMIT) {
        return false;
    }
    for (int i = 0; i < parts->count; i++) {
        const scene3d_part *p = &parts->parts[i];
        if (p->primitive < PRIMITIVE_BOX || p->primitive >= PRIMITIVE_COUNT ||
            p->dimensions.x <= 0 || p->dimensions.y <= 0 || p->dimensions.z <= 0 ||
            !isfinite(
                p->dimensions.x + p->dimensions.y + p->dimensions.z +
                p->position.x + p->position.y + p->position.z + p->rotation.x +
                p->rotation.y + p->rotation.z
            ) ||
            !memchr(p->material, 0, sizeof p->material)) {
            return false;
        }
    }
    return true;
}
static void parts_wire(execution_wire *w, prepared_parts *r)
{
    execution_wire_owned_string(w, &r->key);
    if (!w->error && (!r->key || !r->key[0] || strlen(r->key) > 65536)) {
        w->error = "Prepared part key is invalid";
    }
    execution_wire_count(w, &r->parts.count, 1);
    if (!w->error && r->parts.count > SCENE3D_PART_LIMIT) {
        w->error = "Prepared part count exceeds limit";
    }
    for (int i = 0; i < r->parts.count && !w->error; i++) {
        scene3d_part *p = &r->parts.parts[i];
        execution_wire_int(w, &p->primitive);
        float *values[] = { &p->dimensions.x, &p->dimensions.y,
                            &p->dimensions.z, &p->position.x,
                            &p->position.y,   &p->position.z,
                            &p->rotation.x,   &p->rotation.y,
                            &p->rotation.z };
        for (int j = 0; j < 9; j++) {
            execution_wire_float(w, values[j]);
        }
        const char *material = p->material;
        execution_wire_string(w, &material);
        if (w->reading && !w->error) {
            if (!material || strlen(material) >= sizeof p->material) {
                w->error = "Prepared part material is invalid";
            } else {
                memcpy(p->material, material, strlen(material) + 1);
            }
        }
    }
    if (!w->error && !parts_valid(&r->parts)) {
        w->error = "Prepared mesh part is invalid";
    }
}
static void parts_header(execution_wire *w)
{
    uint64_t magic = UINT64_C(0x3153545241504843),
             version = RENDER_PARTS_PREPARED_VERSION;
    execution_wire_u64(w, &magic);
    execution_wire_u64(w, &version);
    if (!w->error && (magic != UINT64_C(0x3153545241504843) ||
                      version != RENDER_PARTS_PREPARED_VERSION)) {
        w->error = "Prepared part table version is unsupported";
    }
}
bool render_scene3d_parts_save(unsigned char **data, size_t *length, char *error, size_t size)
{
    *data = NULL;
    *length = 0;
    execution_wire w = { 0 };
    parts_header(&w);
    int count = acount(records);
    execution_wire_count(&w, &count, 1);
    if (count > 4096) {
        w.error = "Prepared part table exceeds count limit";
    }
    for (int i = 0; i < count && !w.error; i++) {
        parts_wire(&w, &records[i]);
    }
    if (w.error) {
        if (size) {
            snprintf(error, size, "%s", w.error);
        }
        afree(w.data);
        return false;
    }
    *data = xmalloc(w.size);
    memcpy(*data, w.data, w.size);
    *length = w.size;
    afree(w.data);
    return true;
}
render_parts_table *render_parts_prepare(
    const void *data,
    size_t length,
    size_t *remaining,
    char *error,
    size_t size
)
{
    arena storage = { 0 };
    size_t budget = *remaining < EXECUTION_WIRE_MAX_BYTES ? *remaining
                                                          : EXECUTION_WIRE_MAX_BYTES;
    execution_wire w = { .data = (unsigned char *)data,
                         .size = length,
                         .reading = true,
                         .storage = &storage,
                         .owned_bytes = EXECUTION_WIRE_MAX_BYTES - budget };
    render_parts_table *table = xcalloc(1, sizeof *table);
    parts_header(&w);
    int count = 0;
    execution_wire_count(&w, &count, 1);
    if (!w.error && count > 4096) {
        w.error = "Prepared part table exceeds count limit";
    }
    size_t rows = count ? (size_t)(count < 16 ? 16 : count) * sizeof(prepared_parts) +
                              sizeof(dyna_hdr)
                        : 0;
    if (!w.error && execution_wire_reserve(&w, rows) && count) {
        asetcap(table->records, count);
    }
    for (int i = 0; i < count && !w.error; i++) {
        prepared_parts r = { 0 };
        parts_wire(&w, &r);
        for (int j = 0; j < acount(table->records) && !w.error; j++) {
            if (!strcmp(r.key, table->records[j].key)) {
                w.error = "Prepared part keys are duplicated";
            }
        }
        if (!w.error) {
            asetcap(table->records, acount(table->records) + 1);
            apush(table->records, r);
        } else {
            free(r.key);
        }
    }
    if (!w.error && w.offset != length) {
        w.error = "Prepared part table has trailing bytes";
    }
    if (w.error) {
        if (size) {
            snprintf(error, size, "%s", w.error);
        }
        render_parts_free(table);
        table = NULL;
    } else {
        *remaining -= w.owned_bytes - (EXECUTION_WIRE_MAX_BYTES - budget);
    }
    arena_free(&storage);
    return table;
}
void render_parts_exchange(render_parts_table *table)
{
    prepared_parts *old = records;
    records = table->records;
    table->records = old;
}
void render_parts_free(render_parts_table *table)
{
    if (!table) {
        return;
    }
    for (int i = 0; i < acount(table->records); i++) {
        free(table->records[i].key);
    }
    afree(table->records);
    free(table);
}

size_t render_parts_header_bytes()
{
    return sizeof(render_parts_table);
}
render_parts_table *render_parts_new()
{
    return xcalloc(1, sizeof(render_parts_table));
}
int render_parts_count(const render_parts_table *table)
{
    return acount(table->records);
}
const scene3d_parts *render_parts_at(const render_parts_table *table, int index)
{
    return &table->records[index].parts;
}

const char *render_parts_key(const render_parts_table *table, int index)
{
    return table->records[index].key;
}
