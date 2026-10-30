#ifndef CHIBI_SHADER_VALUES_H
#define CHIBI_SHADER_VALUES_H
#include "render/shader_catalog.h"
/* Prepared parameter identity is an opaque application key. Publication copies
 * every field; lookups require the same immutable shader generation. */
typedef struct
{
    const char *key, *scope;
    int shape, material;
    uint64_t shape_generation, material_generation;
    float shape_values[32][4], material_values[32][4];
    float bindings[MODEL_MATERIAL_SLOT_MAX + 1][4];
    uint64_t binding_generations[MODEL_MATERIAL_SLOT_MAX + 1];
    bool plain, scoped;
} shader_values;
uint64_t render_shader_generation(int index);
bool shader_values_publish(const shader_values *, shader_error *);
bool shader_values_plain(int index, const char *key, float[32][4], shader_error *);
bool shader_values_scoped(int index, const char *key, const char *scope, float[32][4], shader_error *);
bool shader_values_binding(
    int shape,
    int material,
    const char *key,
    float[32][4],
    float[32][4],
    float[MODEL_MATERIAL_SLOT_MAX + 1][4],
    shader_error *
);
/* malloc-owned field-wise versioned payload. Load is atomic and resolves shader names. */
void *shader_values_save(size_t *size);
bool shader_values_load(const void *bytes, size_t size, shader_error *);
void shader_values_clear();
#endif
