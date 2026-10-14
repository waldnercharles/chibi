#include "assets/model_export.h"

#include "assets/model_export_artifacts.h"
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
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <fcntl.h>
#include <sys/syscall.h>
#endif

static bool write_artifact(const char *directory, const model_export_artifact *artifact)
{
    size_t length = strlen(directory) + strlen(artifact->name) + 2;
    char *path = xmalloc(length);
    snprintf(path, length, "%s/%s", directory, artifact->name);
    FILE *file = fopen(path, "wb");
    free(path);
    if (!file) {
        return false;
    }
    bool ok = fwrite(artifact->bytes, 1, artifact->size, file) == artifact->size;
    return fclose(file) == 0 && ok;
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
    model_export_artifacts artifacts = { 0 };
    model_export_source *snapshot = NULL;
    if (ok) {
        for (int i = 0; i < acount(sources.saved); i++) {
            apush(
                snapshot,
                ((model_export_source){ sources.saved[i].asset->name,
                                        sources.saved[i].path,
                                        sources.saved[i].text })
            );
        }
        ok = model_export_artifacts_build(
            name,
            &program,
            &bake,
            options,
            snapshot,
            acount(snapshot),
            SIZE_MAX,
            &artifacts,
            error,
            size
        );
    }
    for (int i = 0; ok && i < 4; i++) {
        ok = write_artifact(stage, &artifacts.files[i]);
        if (!ok) {
            snprintf(error, size, "Could not write staged export: %s", strerror(errno));
        }
    }
    model_export_artifacts_free(&artifacts);
    afree(snapshot);
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
