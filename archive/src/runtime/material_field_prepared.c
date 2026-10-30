#include "runtime/material_field_prepared.h"

#include "runtime/fields/prepared.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static_assert(
    sizeof(int) == 4 && sizeof(float) == 4 && FLT_RADIX == 2 &&
        FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
    "Prepared field arguments require 32-bit integers and IEEE binary32"
);

static const unsigned char MAGIC[] = {
    'C', 'H', 'B', 'I', 'F', 'C', 'A', MATERIAL_FIELDS_PREPARED_VERSION
};
typedef struct
{
    unsigned char *bytes;
    size_t count, capacity;
} writer;
typedef struct
{
    const unsigned char *bytes;
    size_t length, at;
    arena storage;
    size_t budget;
} reader;
static void *allocate(reader *r, size_t bytes)
{
    return arena_alloc_bounded(&r->storage, bytes, r->budget);
}
static bool put(writer *w, const void *data, size_t count)
{
    if (count > MATERIAL_FIELDS_PREPARED_MAX_BYTES - w->count) {
        return false;
    }
    size_t needed = w->count + count;
    if (needed > w->capacity) {
        size_t capacity = w->capacity ? w->capacity * 2 : 256;
        if (capacity < needed) {
            capacity = needed;
        }
        if (capacity > MATERIAL_FIELDS_PREPARED_MAX_BYTES) {
            capacity = MATERIAL_FIELDS_PREPARED_MAX_BYTES;
        }
        w->bytes = xrealloc(w->bytes, capacity);
        w->capacity = capacity;
    }
    memcpy(w->bytes + w->count, data, count);
    w->count = needed;
    return true;
}
static bool number(writer *w, uint32_t v)
{
    unsigned char b[] = { v, v >> 8, v >> 16, v >> 24 };
    return put(w, b, 4);
}
static bool text(writer *w, const char *s, size_t max)
{
    if (!s) {
        return number(w, UINT32_MAX);
    }
    size_t n = strlen(s);
    return n <= max && number(w, (uint32_t)n) && put(w, s, n);
}
static bool get(reader *r, void *out, size_t count)
{
    if (count > r->length - r->at) {
        return false;
    }
    memcpy(out, r->bytes + r->at, count);
    r->at += count;
    return true;
}
static bool next(reader *r, uint32_t *v)
{
    unsigned char b[4];
    if (!get(r, b, 4)) {
        return false;
    }
    *v = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 |
         (uint32_t)b[3] << 24;
    return true;
}
static bool string(reader *r, const char **out, size_t max)
{
    uint32_t n;
    if (!next(r, &n)) {
        return false;
    }
    if (n == UINT32_MAX) {
        *out = NULL;
        return true;
    }
    if (n > max || n > r->length - r->at) {
        return false;
    }
    char *s = allocate(r, (size_t)n + 1);
    if (!s) {
        return false;
    }
    if (!get(r, s, n)) {
        return false;
    }
    if (memchr(s, 0, n)) {
        return false;
    }
    s[n] = 0;
    *out = s;
    return true;
}
bool material_fields_prepared_write(
    const material_fields *fields,
    unsigned char **out,
    size_t *length,
    char *error,
    size_t size
)
{
    assert(fields && out && length && (!size || error));
    *out = NULL;
    *length = 0;
    writer w = { 0 };
    int count = material_field_count(fields),
        arguments = material_field_argument_count(fields);
    if (count > 1024 || arguments > 1024 || !put(&w, MAGIC, sizeof MAGIC) ||
        !number(&w, count)) {
        goto fail;
    }
    for (int i = 0; i < count; i++) {
        const char *name = material_field_name_at(fields, i);
        unsigned char *bytes = NULL;
        size_t n;
        if (!field_prepared_write(material_field_program(fields, name), &bytes, &n, error, size)) {
            goto fail;
        }
        bool ok = text(&w, name, FIELD_PARAMETER_NAME_MAX) &&
                  number(&w, (uint32_t)n) && put(&w, bytes, n);
        free(bytes);
        if (!ok) {
            goto fail;
        }
    }
    if (!number(&w, arguments)) {
        goto fail;
    }
    for (int i = 0; i < arguments; i++) {
        material_field_arguments a = material_field_argument_at(fields, i);
        if (!text(&w, a.field, FIELD_PARAMETER_NAME_MAX) || !text(&w, a.key, 65536) ||
            !number(&w, a.context.seed) || !number(&w, a.context.parameter_count)) {
            goto fail;
        }
        for (int j = 0; j < a.context.parameter_count; j++) {
            field_parameter p = a.context.parameters[j];
            uint32_t kind, bits;
            if (p.value.type == PIN_F32) {
                kind = 2;
                memcpy(&bits, &p.value.f, 4);
            } else if (p.value.type == PIN_I32) {
                kind = 1;
                memcpy(&bits, &p.value.i, 4);
            } else if (p.value.type == PIN_U32) {
                kind = 3;
                bits = p.value.u;
            } else {
                goto fail;
            }
            if (!text(&w, p.name, FIELD_PARAMETER_NAME_MAX) ||
                !text(&w, p.value.type_name, FIELD_PARAMETER_NAME_MAX) ||
                !number(&w, kind) || !number(&w, bits)) {
                goto fail;
            }
        }
    }
    *out = w.bytes;
    *length = w.count;
    return true;
fail:
    free(w.bytes);
    snprintf(error, size, "Invalid or oversized prepared field catalog");
    return false;
}
material_fields *material_fields_prepared_read_budget(
    const void *bytes,
    size_t length,
    size_t *remaining,
    char *error,
    size_t size
)
{
    assert(remaining && (bytes || !length) && (!size || error));
    size_t budget = *remaining < MATERIAL_FIELDS_PREPARED_MAX_BYTES
                      ? *remaining
                      : MATERIAL_FIELDS_PREPARED_MAX_BYTES;
    reader r = { .bytes = bytes, .length = length, .budget = budget };
    material_fields *fields = material_fields_new();
    unsigned char magic[sizeof MAGIC];
    uint32_t count;
    if (material_fields_storage_bytes(fields) > budget ||
        length > MATERIAL_FIELDS_PREPARED_MAX_BYTES || !get(&r, magic, sizeof magic) ||
        memcmp(magic, MAGIC, sizeof magic) || !next(&r, &count) || count > 1024) {
        goto fail;
    }
    r.budget = budget - material_fields_storage_bytes(fields);
    for (uint32_t i = 0; i < count; i++) {
        const char *name;
        uint32_t n;
        if (!string(&r, &name, FIELD_PARAMETER_NAME_MAX) || !name || !name[0] ||
            material_field_program(fields, name) || !next(&r, &n) ||
            n > FIELD_PREPARED_MAX_BYTES || n > r.length - r.at) {
            goto fail;
        }
        size_t available = budget - material_fields_storage_bytes(fields);
        field_program *program = field_prepared_read_budget(r.bytes + r.at, n, &available, error, size);
        if (!program) {
            goto fail;
        }
        r.at += n;
        if (!material_field_register_budget(fields, name, program, budget)) {
            field_program_free(program);
            goto fail;
        }
        if (material_fields_storage_bytes(fields) > budget) {
            goto fail;
        }
        arena_free(&r.storage);
        r.budget = budget - material_fields_storage_bytes(fields);
    }
    if (!next(&r, &count) || count > 1024) {
        goto fail;
    }
    for (uint32_t i = 0; i < count; i++) {
        const char *field, *key;
        uint32_t seed, n;
        if (!string(&r, &field, FIELD_PARAMETER_NAME_MAX) || !field ||
            !string(&r, &key, 65536) || !key || !key[0] || !next(&r, &seed) ||
            !next(&r, &n) || n > 4096) {
            goto fail;
        }
        field_parameter *p = allocate(&r, (size_t)n * sizeof *p);
        if (!p) {
            goto fail;
        }
        for (uint32_t j = 0; j < n; j++) {
            uint32_t kind, bits;
            memset(&p[j], 0, sizeof p[j]);
            if (!string(&r, &p[j].name, FIELD_PARAMETER_NAME_MAX) || !p[j].name ||
                !string(&r, &p[j].value.type_name, FIELD_PARAMETER_NAME_MAX) ||
                !next(&r, &kind) || !next(&r, &bits)) {
                goto fail;
            }
            if (kind == 1) {
                p[j].value.type = PIN_I32;
                memcpy(&p[j].value.i, &bits, 4);
            } else if (kind == 2) {
                p[j].value.type = PIN_F32;
                memcpy(&p[j].value.f, &bits, 4);
            } else if (kind == 3) {
                p[j].value.type = PIN_U32;
                p[j].value.u = bits;
            } else {
                goto fail;
            }
        }
        int before = material_field_argument_count(fields);
        if (!material_field_register_arguments_budget(
                fields,
                field,
                key,
                (field_context){ seed, p, (int)n },
                budget,
                error,
                size
            ) ||
            material_field_argument_count(fields) == before) {
            goto fail;
        }
        if (material_fields_storage_bytes(fields) > budget) {
            goto fail;
        }
        arena_free(&r.storage);
        r.budget = budget - material_fields_storage_bytes(fields);
    }
    if (r.at != length) {
        goto fail;
    }
    arena_free(&r.storage);
    *remaining -= material_fields_storage_bytes(fields);
    return fields;
fail:
    arena_free(&r.storage);
    material_fields_free(fields);
    snprintf(error, size, "Invalid prepared field catalog");
    return NULL;
}
material_fields *material_fields_prepared_read(const void *bytes, size_t length, char *error, size_t size)
{
    size_t remaining = MATERIAL_FIELDS_PREPARED_MAX_BYTES;
    return material_fields_prepared_read_budget(bytes, length, &remaining, error, size);
}
