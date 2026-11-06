#include "render/shader_values.h"

#include "render/shader_catalog_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VALUES_LIMIT 4096
#define VALUES_STRING_LIMIT 65536
#define VALUES_BYTES_LIMIT (32u * 1024u * 1024u)
static shader_values *records;
static uint64_t bytes(uint64_t hash, const void *data, size_t count)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < count; i++) {
        hash = (hash ^ p[i]) * UINT64_C(1099511628211);
    }
    return hash;
}
uint64_t shader_values_generation(const shader_asset *assets, int count, int index)
{
    if (!index) {
        return 0;
    }
    if (index < 0 || index > count) {
        return 0;
    }
    const shader_asset *a = &assets[index - 1];
    if (!a->name[0]) {
        return 0;
    }
    uint64_t h = UINT64_C(14695981039346656037);
    h = bytes(h, a->name, strlen(a->name) + 1);
    if (a->glsl) {
        const char *text = a->glsl;
        while (*text) {
            if (!strncmp(text, "sg_asset_", 9) && text[9] >= '0' && text[9] <= '9') {
                h = bytes(h, text, 9);
                text += 9;
                while (*text >= '0' && *text <= '9') {
                    text++;
                }
                continue;
            }
            h = bytes(h, text++, 1);
        }
        h = bytes(h, "", 1);
    }
    h = bytes(h, a->widths, sizeof a->widths);
    h = bytes(h, a->defaults, sizeof a->defaults);
    for (int i = 0; i < 32; i++) {
        if (a->names[i]) {
            h = bytes(h, a->names[i], strlen(a->names[i]) + 1);
        }
    }
    for (int i = 0; i < acount(a->model.material_slots); i++) {
        const model_material_slot *m = &a->model.material_slots[i];
        h = bytes(h, m->material, strlen(m->material) + 1);
    }
    for (int i = 0; i < acount(a->model.parameters); i++) {
        const model_parameter *p = &a->model.parameters[i];
        h = bytes(h, &p->type, sizeof p->type);
        h = bytes(h, p->minimum, sizeof p->minimum);
        h = bytes(h, p->maximum, sizeof p->maximum);
    }
    return h ? h : 1;
}
uint64_t render_shader_generation(int index)
{
    return shader_values_generation(shader_catalog_assets, acount(shader_catalog_assets), index);
}
static bool generations_from(const shader_asset *assets, int count, const shader_values *v)
{
    if (v->shape < 0 || v->material < 0 ||
        (v->shape && !shader_values_generation(assets, count, v->shape)) ||
        (v->material && !shader_values_generation(assets, count, v->material))) {
        return false;
    }
    if (v->shape_generation != shader_values_generation(assets, count, v->shape) ||
        v->material_generation != shader_values_generation(assets, count, v->material)) {
        return false;
    }
    for (int i = 0; i <= MODEL_MATERIAL_SLOT_MAX; i++) {
        float id = v->bindings[i][0];
        if (!isfinite(id) || id < 0 || (double)id > INT32_MAX || (float)(int)id != id) {
            return false;
        }
        if (id && !shader_values_generation(assets, count, (int)id)) {
            return false;
        }
        for (int j = 1; j < 4; j++) {
            float x = v->bindings[i][j];
            if (!isfinite(x) || x < 0 || x > 32 || (float)(int)x != x) {
                return false;
            }
        }
        if (v->bindings[i][1] + v->bindings[i][2] > 32 || v->bindings[i][3] != 0) {
            return false;
        }
        if (v->binding_generations[i] !=
            shader_values_generation(assets, count, (int)id)) {
            return false;
        }
    }
    return true;
}
static bool generations(const shader_values *v)
{
    return generations_from(shader_catalog_assets, acount(shader_catalog_assets), v);
}
static bool failure(shader_error *e, const char *message)
{
    if (e) {
        *e = (shader_error){ 0 };
        snprintf(e->message, sizeof e->message, "%s", message);
    }
    return false;
}
bool shader_values_publish(const shader_values *v, shader_error *e)
{
    if (!v || !v->key || !*v->key || strlen(v->key) > VALUES_STRING_LIMIT ||
        (v->scope && strlen(v->scope) > VALUES_STRING_LIMIT) ||
        (v->plain && v->scoped) || !generations(v)) {
        return failure(e, "Prepared shader parameter generation mismatch.");
    }
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 4; j++) {
            if (!isfinite(v->shape_values[i][j]) ||
                !isfinite(v->material_values[i][j])) {
                return failure(e, "Prepared shader parameters must be finite.");
            }
        }
    }
    size_t retained = sizeof *v + strlen(v->key) + 1 +
                      strlen(v->scope ? v->scope : "") + 1;
    bool replacing = false;
    for (int i = 0; i < acount(records); i++) {
        const shader_values *r = &records[i];
        bool same = r->shape == v->shape && r->material == v->material &&
                    r->plain == v->plain && r->scoped == v->scoped &&
                    !strcmp(r->key, v->key) &&
                    !strcmp(r->scope, v->scope ? v->scope : "");
        replacing |= same;
        if (!same) {
            retained += sizeof *r + strlen(r->key) + strlen(r->scope) + 2;
        }
    }
    if (retained > VALUES_BYTES_LIMIT ||
        (!replacing && acount(records) == VALUES_LIMIT)) {
        return failure(e, "Prepared shader parameter table exceeds ownership bounds.");
    }
    shader_values copy = *v;
    copy.key = xstrdup(v->key);
    copy.scope = xstrdup(v->scope ? v->scope : "");
    for (int i = 0; i < acount(records); i++) {
        shader_values *r = &records[i];
        if (r->shape == v->shape && r->material == v->material &&
            r->plain == v->plain && r->scoped == v->scoped &&
            !strcmp(r->key, v->key) && !strcmp(r->scope, copy.scope)) {
            free((void *)r->key);
            free((void *)r->scope);
            *r = copy;
            shader_catalog_parameter_revision++;
            return true;
        }
    }
    apush(records, copy);
    shader_catalog_parameter_revision++;
    return true;
}
static const shader_values *find(int shape, int material, const char *key, const char *scope, bool plain, bool scoped)
{
    for (int i = 0; i < acount(records); i++) {
        const shader_values *r = &records[i];
        if (r->shape == shape && r->material == material && r->plain == plain &&
            r->scoped == scoped && !strcmp(r->key, key) &&
            !strcmp(r->scope, scope ? scope : "") && generations(r)) {
            return r;
        }
    }
    return NULL;
}
bool shader_values_plain(int index, const char *key, float values[32][4], shader_error *e)
{
    const shader_values *r = find(index, 0, key, NULL, true, false);
    if (!r) {
        return failure(e, "Shader parameter values have not been prepared for this generation.");
    }
    memcpy(values, r->shape_values, sizeof r->shape_values);
    return true;
}
bool shader_values_scoped(int index, const char *key, const char *scope, float values[32][4], shader_error *e)
{
    const shader_values *r = find(0, index, key, scope, false, true);
    if (!r) {
        return failure(e, "Scoped shader parameter values have not been prepared for this generation.");
    }
    memcpy(values, r->material_values, sizeof r->material_values);
    return true;
}
bool shader_values_binding(
    int shape,
    int material,
    const char *key,
    float a[32][4],
    float b[32][4],
    float table[MODEL_MATERIAL_SLOT_MAX + 1][4],
    shader_error *e
)
{
    const shader_values *r = find(shape, material, key, NULL, false, false);
    if (!r) {
        return failure(e, "Shader binding parameters have not been prepared for this generation.");
    }
    memcpy(a, r->shape_values, sizeof r->shape_values);
    memcpy(b, r->material_values, sizeof r->material_values);
    if (table) {
        memcpy(table, r->bindings, sizeof r->bindings);
    }
    return true;
}
void shader_values_clear()
{
    for (int i = 0; i < acount(records); i++) {
        free((void *)records[i].key);
        free((void *)records[i].scope);
    }
    afree(records);
    shader_catalog_parameter_revision++;
}

typedef struct
{
    unsigned char *data;
    size_t size, at, remaining;
    bool ok;
    const shader_asset *assets;
    int asset_count;
} value_io;
static void word(value_io *io, uint64_t *value, int width, bool write)
{
    if (!io->ok || (size_t)width > io->size - io->at) {
        io->ok = false;
        return;
    }
    if (!write) {
        *value = 0;
    }
    for (int i = 0; i < width; i++) {
        if (write) {
            io->data[io->at + i] = (unsigned char)(*value >> (8 * i));
        } else {
            *value |= (uint64_t)io->data[io->at + i] << (8 * i);
        }
    }
    io->at += (size_t)width;
}
static void real(value_io *io, float *v, bool write)
{
    uint32_t bits = 0;
    if (write) {
        memcpy(&bits, v, 4);
    }
    uint64_t n = bits;
    word(io, &n, 4, write);
    if (!write) {
        bits = (uint32_t)n;
        memcpy(v, &bits, 4);
    }
}
static void string(value_io *io, char **v, bool write)
{
    uint64_t n = write ? strlen(*v) : 0;
    word(io, &n, 4, write);
    if (!io->ok || n > VALUES_STRING_LIMIT || n > io->size - io->at) {
        io->ok = false;
        return;
    }
    if (write) {
        memcpy(io->data + io->at, *v, (size_t)n);
    } else {
        if (memchr(io->data + io->at, 0, (size_t)n)) {
            io->ok = false;
            return;
        }
        if (n + 1 > io->remaining) {
            io->ok = false;
            return;
        }
        io->remaining -= (size_t)n + 1;
        *v = xmalloc((size_t)n + 1);
        memcpy(*v, io->data + io->at, (size_t)n);
        (*v)[n] = 0;
    }
    io->at += (size_t)n;
}
static void shader_identity(value_io *io, int *index, uint64_t *generation, bool write)
{
    char *name = write ? (*index ? shader_catalog_assets[*index - 1].name : "") : NULL;
    string(io, &name, write);
    word(io, generation, 8, write);
    if (!write && io->ok) {
        *index = 0;
        for (int i = 0; *name && i < io->asset_count; i++) {
            if (!strcmp(name, io->assets[i].name)) {
                *index = i + 1;
                break;
            }
        }
        if (*name && !*index) {
            io->ok = false;
        }
    }
    if (!write) {
        if (name) {
            io->remaining += strlen(name) + 1;
        }
        free(name);
    }
}
static void record_io(value_io *io, shader_values *v, bool write)
{
    string(io, (char **)&v->key, write);
    string(io, (char **)&v->scope, write);
    uint64_t flags = write ? (unsigned)v->plain | ((unsigned)v->scoped << 1) : 0;
    word(io, &flags, 1, write);
    if (!write) {
        if (flags > 3 || flags == 3) {
            io->ok = false;
        }
        v->plain = flags & 1;
        v->scoped = flags & 2;
    }
    shader_identity(io, &v->shape, &v->shape_generation, write);
    shader_identity(io, &v->material, &v->material_generation, write);
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 4; j++) {
            real(io, &v->shape_values[i][j], write);
            real(io, &v->material_values[i][j], write);
        }
    }
    for (int i = 0; i <= MODEL_MATERIAL_SLOT_MAX; i++) {
        int index = write ? (int)v->bindings[i][0] : 0;
        shader_identity(io, &index, &v->binding_generations[i], write);
        if (!write) {
            v->bindings[i][0] = (float)index;
        }
        for (int j = 1; j < 4; j++) {
            real(io, &v->bindings[i][j], write);
        }
    }
}
void *shader_values_save(size_t *size)
{
    *size = 0;
    if (acount(records) > VALUES_LIMIT) {
        return NULL;
    }
    size_t capacity = 16;
    for (int i = 0; i < acount(records); i++) {
        const shader_values *v = &records[i];
        if (!generations(v) || strlen(v->key) > VALUES_STRING_LIMIT ||
            strlen(v->scope) > VALUES_STRING_LIMIT) {
            return NULL;
        }
        capacity += 2048 + strlen(v->key) + strlen(v->scope);
        if (v->shape) {
            capacity += strlen(shader_catalog_assets[v->shape - 1].name);
        }
        if (v->material) {
            capacity += strlen(shader_catalog_assets[v->material - 1].name);
        }
        for (int j = 0; j <= MODEL_MATERIAL_SLOT_MAX; j++) {
            if (v->bindings[j][0]) {
                capacity += strlen(
                    shader_catalog_assets[(int)v->bindings[j][0] - 1].name
                );
            }
        }
    }
    if (capacity > VALUES_BYTES_LIMIT) {
        return NULL;
    }
    value_io io = { .data = xmalloc(capacity), .size = capacity, .ok = true };
    uint64_t magic = UINT64_C(0x53564c50), version = SHADER_VALUES_PREPARED_VERSION,
             count = (uint64_t)acount(records);
    word(&io, &magic, 4, true);
    word(&io, &version, 4, true);
    word(&io, &count, 4, true);
    for (int i = 0; i < acount(records); i++) {
        record_io(&io, &records[i], true);
    }
    if (!io.ok) {
        free(io.data);
        return NULL;
    }
    *size = io.at;
    return io.data;
}
bool shader_values_prepare(
    const void *data,
    size_t size,
    const shader_asset *assets,
    int asset_count,
    shader_values **result,
    size_t *remaining,
    shader_error *error
)
{
    *result = NULL;
    if (size > VALUES_BYTES_LIMIT) {
        return failure(error, "Prepared shader parameter payload exceeds byte bounds.");
    }
    value_io io = { .data = (unsigned char *)data,
                    .size = size,
                    .ok = data != NULL,
                    .assets = assets,
                    .asset_count = asset_count,
                    .remaining = *remaining };
    uint64_t magic = 0, version = 0, count = 0;
    word(&io, &magic, 4, false);
    word(&io, &version, 4, false);
    word(&io, &count, 4, false);
    if (!io.ok || magic != UINT64_C(0x53564c50) ||
        version != SHADER_VALUES_PREPARED_VERSION || count > VALUES_LIMIT) {
        return failure(error, "Invalid prepared shader parameter header.");
    }
    shader_values *pending = NULL;
    size_t rows = count ? (size_t)(count < 16 ? 16 : count) * sizeof(shader_values) +
                              sizeof(dyna_hdr)
                        : 0;
    if (rows > io.remaining) {
        return failure(error, "Prepared parameter ownership budget exceeded.");
    }
    io.remaining -= rows;
    if (count) {
        asetcap(pending, (int)count);
    }
    size_t staged_bytes = 0;
    for (uint64_t i = 0; i < count && io.ok; i++) {
        shader_values v = { 0 };
        record_io(&io, &v, false);
        apush(pending, v);
        staged_bytes += sizeof v + (v.key ? strlen(v.key) + 1 : 0) +
                        (v.scope ? strlen(v.scope) + 1 : 0);
        if (staged_bytes > VALUES_BYTES_LIMIT) {
            io.ok = false;
        }
        if (!io.ok || !v.key || !*v.key || !generations_from(assets, asset_count, &v)) {
            io.ok = false;
            break;
        }
        for (int k = 0; k < 32; k++) {
            for (int j = 0; j < 4; j++) {
                if (!isfinite(v.shape_values[k][j]) ||
                    !isfinite(v.material_values[k][j])) {
                    io.ok = false;
                }
            }
        }
        for (int k = 0; k <= MODEL_MATERIAL_SLOT_MAX; k++) {
            for (int j = 1; j < 4; j++) {
                if (!isfinite(v.bindings[k][j]) || v.bindings[k][j] < 0 ||
                    v.bindings[k][j] > 32) {
                    io.ok = false;
                }
            }
        }
    }
    bool ok = io.ok && io.at == size;
    size_t retained = 0;
    for (int i = 0; ok && i < acount(pending); i++) {
        const shader_values *v = &pending[i];
        retained += sizeof *v + strlen(v->key) + strlen(v->scope) + 2;
        for (int k = 0; k < i; k++) {
            const shader_values *r = &pending[k];
            if (v->shape == r->shape && v->material == r->material &&
                v->plain == r->plain && v->scoped == r->scoped &&
                !strcmp(v->key, r->key) && !strcmp(v->scope, r->scope)) {
                ok = false;
            }
        }
    }
    ok = ok && retained <= VALUES_BYTES_LIMIT;
    if (ok) {
        *result = pending;
        *remaining = io.remaining;
        return true;
    }

    for (int i = 0; i < acount(pending); i++) {
        free((void *)pending[i].key);
        free((void *)pending[i].scope);
    }
    afree(pending);
    return ok ? true
              : failure(error, "Invalid or incompatible prepared shader parameter values.");
}

void shader_values_exchange(shader_values **pending)
{
    shader_values *previous = records;
    records = *pending;
    *pending = previous;
    shader_catalog_parameter_revision++;
}
void shader_values_release(shader_values *pending)
{
    for (int i = 0; i < acount(pending); i++) {
        free((void *)pending[i].key);
        free((void *)pending[i].scope);
    }
    afree(pending);
}
bool shader_values_load(const void *data, size_t size, shader_error *error)
{
    shader_values *pending = NULL;
    size_t remaining = VALUES_BYTES_LIMIT;
    if (!shader_values_prepare(
            data,
            size,
            shader_catalog_assets,
            acount(shader_catalog_assets),
            &pending,
            &remaining,
            error
        )) {
        return false;
    }
    shader_values_exchange(&pending);
    shader_values_release(pending);
    return true;
}
