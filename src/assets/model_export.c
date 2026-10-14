#include "assets/model_export.h"

#include "assets/model_source.h"
#include "authoring/image_prepare.h"
#include "authoring/mesh_prepare.h"
#include "authoring/shader_prepare.h"
#include "render/shader_catalog.h"
#include "runtime/geometry/mesh_prepared.h"
#include "runtime/images/catalog.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yyjson.h>
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <fcntl.h>
#include <sys/syscall.h>
#endif

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
static bool write_json(const char *directory, const char *name, yyjson_mut_doc *doc, bool pretty)
{
    size_t length = strlen(directory) + strlen(name) + 2;
    char *path = xmalloc(length);
    snprintf(path, length, "%s/%s", directory, name);
    bool ok = yyjson_mut_write_file(path, doc, pretty ? YYJSON_WRITE_PRETTY : 0, NULL, NULL);
    free(path);
    return ok;
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
/* Writes the engine mesh after reading its bytes back, so a bundle never
 * carries a mesh the engine would refuse to load. */
static bool write_mesh(const char *directory, const render_mesh_asset *mesh, char *error, size_t size)
{
    unsigned char *bytes = NULL;
    size_t length = 0;
    render_mesh_asset check = { 0 };
    size_t budget = SIZE_MAX;
    char reason[256] = "";
    bool ok = render_mesh_asset_prepared_write(mesh, &bytes, &length, reason, sizeof reason) &&
              render_mesh_asset_prepared_read_budget(bytes, length, &budget, &check, reason, sizeof reason);
    render_mesh_asset_free(&check);
    if (!ok) {
        snprintf(error, size, "Baked engine mesh is invalid: %s.", reason);
        free(bytes);
        return false;
    }
    size_t path_size = strlen(directory) + sizeof "/model.mesh";
    char *path = xmalloc(path_size);
    snprintf(path, path_size, "%s/model.mesh", directory);
    FILE *file = fopen(path, "wb");
    free(path);
    ok = file && fwrite(bytes, 1, length, file) == length;
    if (file) {
        ok = fclose(file) == 0 && ok;
    }
    free(bytes);
    if (!ok) {
        snprintf(error, size, "Could not write staged export: %s", strerror(errno));
    }
    return ok;
}
static bool write_obj(const char *directory, const render_mesh_asset *mesh)
{
    size_t length = strlen(directory) + sizeof "/model.obj";
    char *path = xmalloc(length);
    snprintf(path, length, "%s/model.obj", directory);
    FILE *file = fopen(path, "wb");
    free(path);
    if (!file) {
        return false;
    }
    fprintf(file, "# Chibi model bake: engine XYZ, approximate dominant-normal box UVs, live materials in model.mesh and provenance only\n");
    fprintf(file, "mtllib model.mtl\n");
    for (int i = 0; i < acount(mesh->vertices); i++) {
        const render_mesh_surface_vertex *v = &mesh->vertices[i];
        fprintf(
            file,
            "v %.9g %.9g %.9g\n",
            v->position.x,
            v->position.y,
            v->position.z
        );
        fprintf(file, "vt %.9g %.9g\n", v->uv.x, v->uv.y);
        fprintf(file, "vn %.9g %.9g %.9g\n", v->normal.x, v->normal.y, v->normal.z);
    }
    int group = 0;
    for (int i = 0; i < acount(mesh->indices); i += 3) {
        if (group < acount(mesh->groups) && mesh->groups[group].first == i) {
            fprintf(file, "g part_%d\nusemtl material_%d\n", group, group);
            group++;
        }
        const int *corner = mesh->indices + i;
        fprintf(file, "f");
        for (int j = 0; j < 3; j++) {
            fprintf(file, " %d/%d/%d", corner[j] + 1, corner[j] + 1, corner[j] + 1);
        }
        fprintf(file, "\n");
    }
    bool ok = !ferror(file);
    return fclose(file) == 0 && ok;
}
static bool write_mtl(const char *directory, const render_mesh_asset *mesh)
{
    size_t length = strlen(directory) + sizeof "/model.mtl";
    char *path = xmalloc(length);
    snprintf(path, length, "%s/model.mtl", directory);
    FILE *file = fopen(path, "wb");
    free(path);
    if (!file) {
        return false;
    }
    fprintf(file, "# Neutral placeholders preserve Blender slots; live shader appearance is not baked.\n");
    for (int i = 0; i < acount(mesh->groups); i++) {
        fprintf(file, "# Live engine material reference: ");
        const char *material = mesh->groups[i].material;
        if (!*material) {
            fprintf(file, "(inherits entity material)");
        }
        for (const char *p = material; *p; p++) {
            fputc(*p == '\n' || *p == '\r' ? ' ' : *p, file);
        }
        fprintf(file, "\nnewmtl material_%d\nKa 0 0 0\nKd 0.7 0.7 0.7\nKs 0 0 0\nd 1\nillum 1\n\n", i);
    }
    bool ok = !ferror(file);
    return fclose(file) == 0 && ok;
}
static yyjson_mut_doc *provenance(
    const model_source_catalog *sources,
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
    for (int i = 0; i < acount(sources->saved); i++) {
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, saved);
        yyjson_mut_obj_add_strcpy(doc, entry, "name", sources->saved[i].asset->name);
        yyjson_mut_obj_add_strcpy(doc, entry, "path", sources->saved[i].path);
        char hash[17];
        snprintf(
            hash,
            sizeof hash,
            "%016llx",
            (unsigned long long)source_hash(sources->saved[i].text)
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
static int publish(const char *stage, const char *destination)
{
#if defined(__APPLE__)
    return renamex_np(stage, destination, RENAME_EXCL);
#elif defined(__EMSCRIPTEN__)
    /* MEMFS is operated by this editor thread; do not overwrite a completed
     * bundle. Native publication uses the atomic exclusive rename syscall. */
    struct stat st;
    if (!lstat(destination, &st)) {
        errno = EEXIST;
        return -1;
    }
    return rename(stage, destination);
#elif defined(_WIN32)
    /* The runtime's own rename never replaces; the call is parenthesized to
     * skip the replacing POSIX rename support/win32/posix.h maps it to. */
    return (rename)(stage, destination);
#elif defined(__linux__)
    return (int)syscall(SYS_renameat2, AT_FDCWD, stage, AT_FDCWD, destination, 1 /* RENAME_NOREPLACE */);
#else
    errno = ENOTSUP;
    return -1;
#endif
}
static void discard(const char *directory)
{
    const char *names[] = { "model.mesh", "model.obj", "model.mtl", "provenance.json" };
    for (int i = 0; i < 4; i++) {
        size_t length = strlen(directory) + strlen(names[i]) + 2;
        char *path = xmalloc(length);
        snprintf(path, length, "%s/%s", directory, names[i]);
        unlink(path);
        free(path);
    }
    rmdir(directory);
}
bool model_export_bundle(
    const char *root,
    const char *name,
    const char *destination,
    const model_export_options *options,
    model_export_summary *summary,
    char *error,
    size_t size
)
{
    assert(root && name && destination && options && error && size);
    error[0] = '\0';
    if (!*name || !*destination || destination[strlen(destination) - 1] == '/' ||
        options->override_count < 0 || options->override_count > SHADER_PARAMETER_MAX ||
        (options->override_count && !options->overrides)) {
        snprintf(error, size, "Invalid model export request.");
        return false;
    }
    struct stat st;
    if (!lstat(destination, &st) || errno != ENOENT) {
        snprintf(error, size, "Export destination must be a new directory.");
        return false;
    }
    model_source_catalog sources;
    bool ok = model_source_load(root, &sources, error, size);
    if (ok) {
        ok = render_image_mount_content(root, error, size) &&
             authoring_mesh_mount_content(root, error, size);
    }
    const behavior_asset *source = NULL;
    for (int i = 0; ok && i < acount(sources.saved); i++) {
        if (strcmp(sources.saved[i].asset->name, name)) {
            continue;
        }
        if (sources.saved[i].asset->domain == BEHAVIOR_DOMAIN_MODEL) {
            if (source) {
                snprintf(error, size, "Ambiguous model name '%s'.", name);
                ok = false;
                break;
            }
            source = sources.saved[i].asset;
        }
        if (sources.saved[i].asset->domain == BEHAVIOR_DOMAIN_SHAPE) {
            snprintf(error, size, "Model name collides with a raw shape '%s'.", name);
            ok = false;
        }
    }
    if (ok && !source) {
        snprintf(error, size, "Saved model '%s' not found.", name);
        ok = false;
    }
    model_program program;
    model_program_init(&program);
    shader_error diagnostic = { 0 };
    Shader binding = { .shape = (char *)name };
    if (ok) {
        ok = render_shader_binding_sources_validate(
            &binding,
            NULL,
            sources.catalog,
            acount(sources.catalog),
            &diagnostic
        );
    }
    if (ok) {
        ok = model_compile_with_catalog(source, sources.catalog, acount(sources.catalog), &program, &diagnostic);
    }
    if (ok) {
        ok = render_image_validate_volumes(&program.shader, &diagnostic);
    }
    program.resolve_image = render_image_resolve;
    float parameters[SHADER_PARAMETER_MAX][4] = { { 0 } };
    if (ok) {
        ok = model_program_parameters(&program, parameters, &diagnostic);
    }
    bool used[SHADER_PARAMETER_MAX] = { 0 };
    for (int i = 0; ok && i < options->override_count; i++) {
        const model_export_override *override = &options->overrides[i];
        const model_parameter *match = NULL;
        for (int j = 0; j < acount(program.parameters); j++) {
            const model_parameter *p = &program.parameters[j];
            if (!p->hidden && p->node == override->parameter_id && override->asset &&
                override->scope && !strcmp(p->asset, override->asset) &&
                !strcmp(p->scope, override->scope)) {
                if (match) {
                    match = NULL;
                    break;
                }
                match = p;
            }
        }
        if (!match || match->width != override->width || used[match->slot]) {
            snprintf(error, size, "Unknown, duplicate, or wrong-width public parameter override.");
            ok = false;
            break;
        }
        used[match->slot] = true;
        memcpy(parameters[match->slot], override->value, sizeof override->value);
    }
    model_bake_result bake = { 0 };
    if (ok) {
        ok = model_bake(&program, parameters, &options->bake, &bake, &diagnostic);
    }
    char *stage = NULL;
    if (ok) {
        size_t length = strlen(destination) + sizeof ".stage-XXXXXX";
        stage = xmalloc(length);
        snprintf(stage, length, "%s.stage-XXXXXX", destination);
        if (!mkdtemp(stage)) {
            snprintf(error, size, "Could not stage export: %s", strerror(errno));
            ok = false;
        }
    }
    if (ok) {
        ok = write_mesh(stage, &bake.mesh, error, size);
    }
    if (ok) {
        yyjson_mut_doc *meta = provenance(&sources, name, &program, &bake, options);
        ok = write_obj(stage, &bake.mesh) && write_mtl(stage, &bake.mesh) &&
             write_json(stage, "provenance.json", meta, true);
        yyjson_mut_doc_free(meta);
        if (!ok) {
            snprintf(error, size, "Could not write staged export: %s", strerror(errno));
        }
    }
    if (ok && publish(stage, destination)) {
        snprintf(error, size, "Could not publish export: %s", strerror(errno));
        ok = false;
    }
    if (!ok && !error[0]) {
        snprintf(
            error,
            size,
            "Node %llu: %s",
            (unsigned long long)diagnostic.node,
            diagnostic.message
        );
    }
    if (ok && summary) {
        *summary = (model_export_summary){ .triangles = acount(bake.mesh.indices) / 3,
                                           .samples = bake.samples,
                                           .work = bake.work,
                                           .cell_diagonal = bake.cell_diagonal,
                                           .max_residual = bake.max_residual,
                                           .bounds = bake.bounds };
    }
    if (!ok && stage) {
        discard(stage);
    }
    free(stage);
    model_bake_result_free(&bake);
    model_program_free(&program);
    model_source_free(&sources);
    return ok;
}
