#include "assets/model_export_artifacts.h"

#include "runtime/geometry/mesh_prepared.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/value_wire.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

typedef struct
{
    unsigned char *bytes;
    size_t size, capacity, limit;
    bool failed;
} text_buffer;
static void append(text_buffer *b, const char *format, ...)
{
    if (b->failed) {
        return;
    }
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int n = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (n < 0 || (size_t)n > b->limit - b->size) {
        b->failed = true;
        va_end(args);
        return;
    }
    size_t required = b->size + (size_t)n + 1;
    if (required > b->capacity) {
        size_t capacity = b->capacity ? b->capacity : 256;
        while (capacity < required && capacity <= SIZE_MAX / 2) {
            capacity *= 2;
        }
        if (capacity < required) {
            capacity = required;
        }
        if (b->limit < SIZE_MAX && capacity > b->limit + 1) {
            capacity = b->limit + 1;
        }
        b->bytes = xrealloc(b->bytes, capacity);
        b->capacity = capacity;
    }
    vsnprintf((char *)b->bytes + b->size, (size_t)n + 1, format, args);
    b->size += (size_t)n;
    va_end(args);
}
static uint64_t source_hash(const char *text)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        hash ^= *p;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}
static yyjson_mut_val *numbers(yyjson_mut_doc *doc, const float *values, int count)
{
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    for (int i = 0; i < count; i++) {
        yyjson_mut_arr_add_real(doc, array, values[i]);
    }
    return array;
}
static yyjson_mut_val *mesh_groups(yyjson_mut_doc *doc, const render_mesh_asset *mesh)
{
    yyjson_mut_val *groups = yyjson_mut_arr(doc);
    for (int i = 0; i < acount(mesh->groups); i++) {
        const render_mesh_group *g = &mesh->groups[i];
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, groups);
        yyjson_mut_obj_add_int(doc, entry, "first", g->first);
        yyjson_mut_obj_add_int(doc, entry, "count", g->count);
        yyjson_mut_obj_add_int(doc, entry, "part", g->part);
        yyjson_mut_obj_add_uint(doc, entry, "node", g->node);
        yyjson_mut_obj_add_strcpy(doc, entry, "asset", g->asset);
        yyjson_mut_obj_add_strcpy(doc, entry, "name", g->name);
        yyjson_mut_obj_add_strcpy(doc, entry, "path", g->path);
        yyjson_mut_obj_add_strcpy(doc, entry, "material", g->material);
        yyjson_mut_obj_add_strcpy(doc, entry, "material_scope", g->material_scope);
        char label[32];
        snprintf(label, sizeof label, "part_%d", i);
        yyjson_mut_obj_add_strcpy(doc, entry, "obj_group", label);
        snprintf(label, sizeof label, "material_%d", i);
        yyjson_mut_obj_add_strcpy(doc, entry, "obj_material", label);
    }
    return groups;
}
static void write_obj(text_buffer *file, const render_mesh_asset *mesh)
{
    append(file, "# Chibi model bake: engine XYZ, approximate dominant-normal box UVs, live materials in model.mesh and provenance only\n");
    append(file, "mtllib model.mtl\n");
    for (int i = 0; i < acount(mesh->vertices); i++) {
        const render_mesh_surface_vertex *v = &mesh->vertices[i];
        append(
            file,
            "v %.9g %.9g %.9g\n",
            v->position.x,
            v->position.y,
            v->position.z
        );
        append(file, "vt %.9g %.9g\n", v->uv.x, v->uv.y);
        append(file, "vn %.9g %.9g %.9g\n", v->normal.x, v->normal.y, v->normal.z);
    }
    int group = 0;
    for (int i = 0; i < acount(mesh->indices); i += 3) {
        if (group < acount(mesh->groups) && mesh->groups[group].first == i) {
            append(file, "g part_%d\nusemtl material_%d\n", group, group);
            group++;
        }
        const int *corner = mesh->indices + i;
        append(file, "f");
        for (int j = 0; j < 3; j++) {
            append(file, " %d/%d/%d", corner[j] + 1, corner[j] + 1, corner[j] + 1);
        }
        append(file, "\n");
    }
}
static void write_mtl(text_buffer *file, const render_mesh_asset *mesh)
{
    append(file, "# Neutral placeholders preserve Blender slots; live shader appearance is not baked.\n");
    for (int i = 0; i < acount(mesh->groups); i++) {
        append(file, "# Live engine material reference: ");
        const char *material = mesh->groups[i].material;
        if (!*material) {
            append(file, "(inherits entity material)");
        }
        for (const char *p = material; *p; p++) {
            append(file, "%c", *p == '\n' || *p == '\r' ? ' ' : *p);
        }
        append(file, "\nnewmtl material_%d\nKa 0 0 0\nKd 0.7 0.7 0.7\nKs 0 0 0\nd 1\nillum 1\n\n", i);
    }
}
static yyjson_mut_doc *provenance(
    const model_export_source *sources,
    int source_count,
    const char *name,
    const model_program *program,
    const model_bake_result *bake,
    const model_export_options *options
)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "version", 1);
    yyjson_mut_obj_add_strcpy(doc, root, "model", name);
    yyjson_mut_obj_add_str(doc, root, "coordinates", "OBJ uses engine XYZ. Blender import with Y-up/-Z-forward maps engine coordinates to (x,-z,y); Blender-export conversion back to engine is (x,z,-y). OBJ UVs approximate dominant-normal box mapping, not an unwrap. Engine uv_bounds retains source box UV projection per fragment. Part frames map object differentials to part-local coordinates.");
    yyjson_mut_obj_add_str(doc, root, "appearance", "Live material references retained in mesh groups. OBJ/MTL contains geometry, normals, approximate box UVs and neutral placeholder material slots. No texture or procedural appearance bake.");
    yyjson_mut_obj_add_str(doc, root, "approximation", "Marching tetrahedra sampled surface. Thin features below cell resolution can be lost. Ownership/material boundaries are chosen per triangle centroid. Residual is a sampled diagnostic, not a Hausdorff error bound.");
    yyjson_mut_obj_add_str(doc, root, "source_hash_algorithm", "fnv1a64");
    yyjson_mut_val *images = yyjson_mut_obj_add_arr(doc, root, "image_dependencies");
    for (int i = 0; i < acount(program->shader.images); i++) {
        yyjson_mut_arr_add_strcpy(doc, images, program->shader.images[i].key);
    }
    yyjson_mut_val *saved = yyjson_mut_obj_add_arr(doc, root, "saved_catalog");
    for (int i = 0; i < source_count; i++) {
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, saved);
        yyjson_mut_obj_add_strcpy(doc, entry, "name", sources[i].name);
        yyjson_mut_obj_add_strcpy(doc, entry, "path", sources[i].path);
        char hash[17];
        snprintf(
            hash,
            sizeof hash,
            "%016llx",
            (unsigned long long)source_hash(sources[i].text)
        );
        yyjson_mut_obj_add_strcpy(doc, entry, "hash", hash);
    }
    yyjson_mut_val *parameters = yyjson_mut_obj_add_arr(doc, root, "parameters");
    for (int i = 0; i < acount(program->parameters); i++) {
        const model_parameter *p = &program->parameters[i];
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, parameters);
        yyjson_mut_obj_add_strcpy(doc, entry, "asset", p->asset);
        yyjson_mut_obj_add_strcpy(doc, entry, "scope", p->scope);
        yyjson_mut_obj_add_strcpy(doc, entry, "name", p->name);
        yyjson_mut_obj_add_uint(doc, entry, "node", p->node);
        yyjson_mut_obj_add_int(doc, entry, "slot", p->slot);
        yyjson_mut_obj_add_bool(doc, entry, "derived", p->hidden);
        yyjson_mut_obj_add_val(doc, entry, "value", numbers(doc, bake->parameters[p->slot], p->width));
    }
    yyjson_mut_obj_add_val(doc, root, "parts", mesh_groups(doc, &bake->mesh));
    yyjson_mut_obj_add_val(doc, root, "bounds_min", numbers(doc, bake->bounds.min, 3));
    yyjson_mut_obj_add_val(doc, root, "bounds_max", numbers(doc, bake->bounds.max, 3));
    yyjson_mut_obj_add_real(doc, root, "cell_size", bake->cell_size);
    yyjson_mut_obj_add_int(doc, root, "boundary_triangles", bake->boundary_triangles);
    yyjson_mut_val *cells = yyjson_mut_obj_add_arr(doc, root, "cells");
    for (int i = 0; i < 3; i++) {
        yyjson_mut_arr_add_int(doc, cells, bake->cells[i]);
    }
    yyjson_mut_obj_add_real(doc, root, "cell_diagonal", bake->cell_diagonal);
    yyjson_mut_obj_add_real(doc, root, "max_sampled_residual", bake->max_residual);
    yyjson_mut_obj_add_uint(doc, root, "samples", bake->samples);
    yyjson_mut_obj_add_uint(doc, root, "work", bake->work);
    yyjson_mut_obj_add_str(doc, root, "work_scope", "Conservative node-work budget estimate including hit/frame overhead; not a GPU instruction count.");
    yyjson_mut_val *budget = yyjson_mut_obj_add_obj(doc, root, "options");
    yyjson_mut_obj_add_real(doc, budget, "cell_size", options->bake.cell_size);
    yyjson_mut_obj_add_int(doc, budget, "max_axis_cells", options->bake.max_axis_cells);
    yyjson_mut_obj_add_int(doc, budget, "max_triangles", options->bake.max_triangles);
    yyjson_mut_obj_add_uint(doc, budget, "max_samples", options->bake.max_samples);
    yyjson_mut_obj_add_uint(doc, budget, "max_work", options->bake.max_work);
    yyjson_mut_obj_add_int(doc, budget, "edge_iterations", options->bake.edge_iterations);
    return doc;
}
static bool charge_string(const char *text, size_t *remaining)
{
    if (!text) {
        return false;
    }
    size_t length = strnlen(text, *remaining);
    if (length == *remaining) {
        return false;
    }
    *remaining -= length + 1;
    return true;
}
static bool metadata_valid(
    const char *name,
    const model_program *program,
    const model_bake_result *bake,
    const model_export_options *options,
    const model_export_source *sources,
    int source_count,
    size_t budget
)
{
    if (!*name || source_count < 0 || source_count > EXECUTION_WIRE_MAX_RECORDS ||
        (source_count && !sources) || !budget || (size_t)source_count > budget / 128 ||
        (size_t)acount(program->shader.images) > budget / 64 ||
        acount(bake->mesh.groups) > RENDER_MESH_PREPARED_GROUP_MAX ||
        acount(program->parameters) > SHADER_PARAMETER_MAX ||
        acount(program->shader.images) > EXECUTION_WIRE_MAX_RECORDS ||
        !isfinite(options->bake.cell_size) || !isfinite(bake->cell_size) ||
        !isfinite(bake->cell_diagonal) || !isfinite(bake->max_residual)) {
        return false;
    }
    if (!charge_string(name, &budget)) {
        return false;
    }
    for (int i = 0; i < source_count; i++) {
        if (!charge_string(sources[i].name, &budget) ||
            !charge_string(sources[i].path, &budget) ||
            !charge_string(sources[i].text, &budget)) {
            return false;
        }
    }
    for (int i = 0; i < 3; i++) {
        if (!isfinite(bake->bounds.min[i]) || !isfinite(bake->bounds.max[i]) ||
            bake->bounds.min[i] > bake->bounds.max[i]) {
            return false;
        }
    }
    for (int i = 0; i < acount(program->shader.images); i++) {
        if (!charge_string(program->shader.images[i].key, &budget)) {
            return false;
        }
    }
    for (int i = 0; i < acount(program->parameters); i++) {
        const model_parameter *p = &program->parameters[i];
        if (p->slot < 0 || p->slot >= SHADER_PARAMETER_MAX || p->width < 1 ||
            p->width > 4 || !charge_string(p->asset, &budget) ||
            !charge_string(p->scope, &budget) || !charge_string(p->name, &budget)) {
            return false;
        }
        for (int j = 0; j < p->width; j++) {
            if (!isfinite(bake->parameters[p->slot][j])) {
                return false;
            }
        }
    }
    for (int i = 0; i < acount(bake->mesh.groups); i++) {
        const render_mesh_group *g = &bake->mesh.groups[i];
        if (!charge_string(g->asset, &budget) || !charge_string(g->name, &budget) ||
            !charge_string(g->path, &budget) || !charge_string(g->material, &budget) ||
            !charge_string(g->material_scope, &budget)) {
            return false;
        }
    }
    return true;
}
void model_export_artifacts_free(model_export_artifacts *artifacts)
{
    for (int i = 0; i < 4; i++) {
        free(artifacts->files[i].bytes);
    }
    *artifacts = (model_export_artifacts){ 0 };
}
bool model_export_artifacts_build(
    const char *name,
    const model_program *program,
    const model_bake_result *bake,
    const model_export_options *options,
    const model_export_source *sources,
    int source_count,
    size_t max_bytes,
    model_export_artifacts *result,
    char *error,
    size_t size
)
{
    assert(name && program && bake && options && result && error && size);
    error[0] = '\0';
    /* Bound intermediate metadata even when the filesystem CLI has no total
     * bundle limit. The prepared mesh has its own wire/storage limits. */
    size_t metadata_budget = max_bytes < EXECUTION_WIRE_MAX_BYTES
                               ? max_bytes
                               : EXECUTION_WIRE_MAX_BYTES;
    if (!metadata_valid(name, program, bake, options, sources, source_count, metadata_budget)) {
        snprintf(error, size, "Invalid or oversized model export metadata.");
        return false;
    }
    model_export_artifacts out = { .files = { { .name = "model.mesh" },
                                              { .name = "model.obj" },
                                              { .name = "model.mtl" },
                                              { .name = "provenance.json" } } };
    bool ok = render_mesh_asset_prepared_write(
        &bake->mesh,
        &out.files[0].bytes,
        &out.files[0].size,
        error,
        size
    );
    render_mesh_asset check = { 0 };
    size_t remaining = EXECUTION_WIRE_MAX_BYTES;
    if (ok) {
        ok = out.files[0].size <= max_bytes && render_mesh_asset_prepared_read_budget(
                                                   out.files[0].bytes,
                                                   out.files[0].size,
                                                   &remaining,
                                                   &check,
                                                   error,
                                                   size
                                               );
    }
    render_mesh_asset_free(&check);
    out.size = out.files[0].size;
    for (int i = 1; ok && i < 3; i++) {
        text_buffer text = { .limit = max_bytes - out.size };
        if (i == 1) {
            write_obj(&text, &bake->mesh);
        } else {
            write_mtl(&text, &bake->mesh);
        }
        out.files[i].bytes = text.bytes;
        out.files[i].size = text.size;
        out.size += text.size;
        ok = !text.failed;
    }
    if (ok) {
        yyjson_mut_doc *doc = provenance(sources, source_count, name, program, bake, options);
        out.files[3].bytes = (unsigned char *)
            yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, &out.files[3].size);
        yyjson_mut_doc_free(doc);
        ok = out.files[3].bytes && out.files[3].size <= max_bytes - out.size;
        if (ok) {
            out.size += out.files[3].size;
        }
    }
    if (!ok) {
        if (!error[0]) {
            snprintf(error, size, "Model export exceeds its byte budget or contains invalid data.");
        }
        model_export_artifacts_free(&out);
        return false;
    }
    *result = out;
    return true;
}
