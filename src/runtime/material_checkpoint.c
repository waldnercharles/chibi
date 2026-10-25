#include "runtime/material_checkpoint.h"

#include "runtime/fields/prepared.h"
#include "runtime/material_terrain.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static_assert(
    sizeof(int) == 4 && sizeof(float) == 4 && FLT_RADIX == 2 &&
        FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
    "Terrain checkpoints require IEEE binary32"
);
static const unsigned char magic[8] = {
    'C', 'H', 'B', 'I', 'T', 'C', 'P', MATERIAL_CHECKPOINT_VERSION
};
static uint64_t hash_bytes(uint64_t h, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) {
        h = (h ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return h;
}
static void put32(unsigned char *p, uint32_t n)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (unsigned char)(n >> (8 * i));
    }
}
static uint32_t get32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}
static void put64(unsigned char *p, uint64_t n)
{
    put32(p, (uint32_t)n);
    put32(p + 4, (uint32_t)(n >> 32));
}
static uint64_t get64(const unsigned char *p)
{
    return get32(p) | (uint64_t)get32(p + 4) << 32;
}
static uint32_t float_bits(float value)
{
    uint32_t bits;
    /* Signed zero does not change generation. */
    if (value == 0) {
        value = 0;
    }
    memcpy(&bits, &value, 4);
    return bits;
}
static float get_float(const unsigned char *p)
{
    uint32_t bits = get32(p);
    float result;
    memcpy(&result, &bits, 4);
    return result;
}
static uint64_t hash_number(uint64_t h, uint32_t n)
{
    unsigned char bytes[4];
    put32(bytes, n);
    return hash_bytes(h, bytes, 4);
}
bool material_checkpoint_fingerprint(material_checkpoint_recipe recipe, uint64_t *out, char *error, size_t size)
{
    const field_program *program = material_field_program(recipe.fields, recipe.field);
    if (!program) {
        snprintf(error, size, "Terrain field is unavailable");
        return false;
    }
    unsigned char *bytes;
    size_t length;
    if (!field_prepared_write(program, &bytes, &length, error, size)) {
        return false;
    }
    /* Lowered field programs contain their resolved dependency code. Catalog
     * revisions and unrelated fields never enter this durable identity. */
    uint64_t hash = hash_bytes(UINT64_C(14695981039346656037), bytes, length);
    free(bytes);
    hash = hash_number(hash, recipe.context.seed);
    for (int i = 0; i < field_program_parameter_count(program); i++) {
        field_parameter parameter = field_program_parameter_at(program, i);
        for (int j = 0; j < recipe.context.parameter_count; j++) {
            if (!strcmp(parameter.name, recipe.context.parameters[j].name)) {
                parameter.value = recipe.context.parameters[j].value;
                break;
            }
        }
        uint32_t kind, bits;
        switch (parameter.value.type) {
            case PIN_F32:
                if (!isfinite(parameter.value.f)) {
                    snprintf(
                        error,
                        size,
                        "Terrain field parameter %s is not finite",
                        parameter.name
                    );
                    return false;
                }
                kind = 1;
                bits = float_bits(parameter.value.f);
                break;
            case PIN_I32:
                kind = 2;
                bits = (uint32_t)parameter.value.i;
                break;
            case PIN_U32:
                kind = 3;
                bits = parameter.value.u;
                break;
            default:
                snprintf(
                    error,
                    size,
                    "Terrain field parameter %s has an unsupported type",
                    parameter.name
                );
                return false;
        }
        hash = hash_number(hash_number(hash, kind), bits);
    }
    const uint32_t settings[] = { recipe.grid.width,
                                  recipe.grid.height,
                                  float_bits(recipe.grid.cell_size),
                                  float_bits(recipe.grid.center_x),
                                  float_bits(recipe.grid.center_y),
                                  recipe.connectivity,
                                  recipe.border_cells,
                                  recipe.island_cells,
                                  recipe.streamed,
                                  MATERIAL_TERRAIN_CHUNK_CELLS,
                                  1 };
    for (size_t i = 0; i < sizeof settings / sizeof *settings; i++) {
        hash = hash_number(hash, settings[i]);
    }
    *out = hash;
    return true;
}

material_mask *material_checkpoint_copy_mask(const material_mask *source, field_grid grid)
{
    size_t count = (size_t)grid.width * grid.height;
    unsigned char *empty = NULL;
    if (!source || !source->width) {
        empty = xcalloc(count, 1);
    }
    material_mask *mask = xmalloc(sizeof *mask);
    *mask = material_mask_copy_cells(
        grid.width,
        grid.height,
        grid.cell_size,
        empty ? empty : source->cells
    );
    free(empty);
    mask->damage = xcalloc(count, sizeof(float));
    if (source && source->width) {
        assert(source->width == grid.width && source->height == grid.height);
        memcpy(mask->cells, source->cells, count);
        memcpy(mask->distance, source->distance, count * sizeof(float));
        if (source->damage) {
            memcpy(mask->damage, source->damage, count * sizeof(float));
        }
        mask->count = source->count;
    }
    return mask;
}
void material_checkpoint_free(material_checkpoint *checkpoint)
{
    for (int i = 0; i < acount(checkpoint->tiles); i++) {
        material_mask_free(checkpoint->tiles[i].mask);
        free(checkpoint->tiles[i].mask);
    }
    afree(checkpoint->tiles);
    *checkpoint = (material_checkpoint){ 0 };
}
static bool valid_grid(field_grid g)
{
    return g.width > 0 && g.width <= MATERIAL_TERRAIN_CHUNK_CELLS && g.height > 0 &&
           g.height <= MATERIAL_TERRAIN_CHUNK_CELLS && isfinite(g.cell_size) &&
           g.cell_size > 0 && isfinite(g.center_x) && isfinite(g.center_y);
}
bool material_checkpoint_write(
    const material_checkpoint *checkpoint,
    const char *identity,
    uint64_t recipe,
    unsigned char **out,
    size_t *length,
    char *error,
    size_t size
)
{
    *out = NULL;
    *length = 0;
    size_t id_length = identity ? strlen(identity) : 0;
    int count = acount(checkpoint->tiles);
    if (!id_length || id_length > MATERIAL_CHECKPOINT_ID_MAX ||
        count > MATERIAL_CHECKPOINT_MAX_TILES) {
        goto fail;
    }
    size_t total = 32 + id_length;
    for (int i = 0; i < count; i++) {
        field_grid g = checkpoint->tiles[i].grid;
        material_mask *mask = checkpoint->tiles[i].mask;
        if (!valid_grid(g) || !mask ||
            (mask->width && (mask->width != g.width || mask->height != g.height ||
                             !mask->cells || !mask->distance))) {
            goto fail;
        }
        total += 28 + (size_t)g.width * g.height * 9;
    }
    if (total > MATERIAL_CHECKPOINT_MAX_BYTES) {
        goto fail;
    }
    unsigned char *bytes = xmalloc(total);
    memcpy(bytes, magic, 8);
    put64(bytes + 8, recipe);
    put32(bytes + 16, (uint32_t)id_length);
    put32(bytes + 20, (uint32_t)count);
    memcpy(bytes + 24, identity, id_length);
    size_t at = 24 + id_length;
    for (int i = 0; i < count; i++) {
        material_stream_tile tile = checkpoint->tiles[i];
        field_grid g = tile.grid;
        material_mask *m = tile.mask;
        put64(bytes + at, tile.key);
        put32(bytes + at + 8, g.width);
        put32(bytes + at + 12, g.height);
        put32(bytes + at + 16, float_bits(g.cell_size));
        put32(bytes + at + 20, float_bits(g.center_x));
        put32(bytes + at + 24, float_bits(g.center_y));
        at += 28;
        for (int j = 0; j < g.width * g.height; j++) {
            float distance = m->width ? m->distance[j] : g.cell_size * .5f;
            float damage = m->width && m->damage ? m->damage[j] : 0;
            unsigned char cell = m->width ? m->cells[j] : 0;
            if (cell > 1 || !isfinite(distance) || !isfinite(damage) || damage < 0) {
                free(bytes);
                goto fail;
            }
            bytes[at] = cell;
            put32(bytes + at + 1, float_bits(distance));
            put32(bytes + at + 5, float_bits(damage));
            at += 9;
        }
    }
    put64(bytes + at, hash_bytes(UINT64_C(14695981039346656037), bytes, at));
    *out = bytes;
    *length = total;
    return true;
fail:
    snprintf(error, size, "Invalid terrain checkpoint or snapshot exceeds limits");
    return false;
}
bool material_checkpoint_read(
    const void *input,
    size_t length,
    const char *identity,
    uint64_t recipe,
    material_checkpoint *out,
    char *error,
    size_t size
)
{
    material_checkpoint result = { 0 };
    const unsigned char *bytes = input;
    if (!bytes || !identity || length < 32 || length > MATERIAL_CHECKPOINT_MAX_BYTES ||
        memcmp(bytes, magic, 8) || get64(bytes + 8) != recipe ||
        get64(bytes + length - 8) !=
            hash_bytes(UINT64_C(14695981039346656037), bytes, length - 8)) {
        goto fail;
    }
    uint32_t id_length = get32(bytes + 16), count = get32(bytes + 20);
    if (!id_length || id_length > MATERIAL_CHECKPOINT_ID_MAX ||
        id_length > length - 32 || strlen(identity) != id_length ||
        memcmp(bytes + 24, identity, id_length) ||
        count > MATERIAL_CHECKPOINT_MAX_TILES) {
        goto fail;
    }
    size_t at = 24 + id_length;
    for (uint32_t i = 0; i < count; i++) {
        if (length - 8 - at < 28) {
            goto fail;
        }
        uint64_t key = get64(bytes + at);
        field_grid g = { get32(bytes + at + 8),      get32(bytes + at + 12),
                         get_float(bytes + at + 16), get_float(bytes + at + 20),
                         get_float(bytes + at + 24), 0 };
        if (!valid_grid(g)) {
            goto fail;
        }
        for (int j = 0; j < acount(result.tiles); j++) {
            if (result.tiles[j].key == key) {
                goto fail;
            }
        }
        at += 28;
        size_t cells = (size_t)g.width * g.height;
        if (cells * 9 > length - 8 - at) {
            goto fail;
        }
        material_mask *m = material_checkpoint_copy_mask(NULL, g);
        apush(result.tiles, ((material_stream_tile){ key, g, m }));
        for (size_t j = 0; j < cells; j++) {
            m->cells[j] = bytes[at];
            m->distance[j] = get_float(bytes + at + 1);
            m->damage[j] = get_float(bytes + at + 5);
            if (m->cells[j] > 1 || !isfinite(m->distance[j]) ||
                !isfinite(m->damage[j]) || m->damage[j] < 0) {
                goto fail;
            }
            m->count += m->cells[j];
            at += 9;
        }
    }
    if (at != length - 8) {
        goto fail;
    }
    *out = result;
    return true;
fail:
    material_checkpoint_free(&result);
    snprintf(error, size, "Terrain checkpoint is corrupt, incompatible, or exceeds limits");
    return false;
}
