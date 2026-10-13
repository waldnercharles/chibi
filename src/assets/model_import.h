#ifndef CHIBI_ASSETS_MODEL_IMPORT_H
#define CHIBI_ASSETS_MODEL_IMPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The most bytes of a model file an import reads. */
#define MODEL_IMPORT_BYTES_MAX (256 * 1024 * 1024)

typedef struct
{
    char **written;       /* content/... keys of the files written, in order */
    char **warnings;      /* what the import left out, one sentence each */
    char **warning_codes; /* parallel stable categories for warnings */
    int vertices, triangles, materials, images;
    int skins, animations, cameras, lights;
    float bounds_min[3], bounds_max[3];
    size_t source_bytes, output_bytes;
    /* glTF positions are metres; no implicit unit conversion. */
} model_import_report;

/* Copies the glTF 2.0 model at `path`, a .gltf with its files or a .glb, into
 * the content folder `content` as `name`. Every mesh node bakes into
 * meshes/<name>.mesh with one group per material, each glTF material its
 * triangles draw with becomes a Standard material at
 * materials/<name>_<material>.kdl, its images are copied under
 * textures/<name>/, and prototypes/<name>.kdl draws the mesh.
 * It writes nothing over an existing file and leaves nothing behind when it
 * fails; false with why in `error`. The report holds what it wrote and left
 * out; free it with model_import_report_free either way. */
bool model_import(
    const char *content,
    const char *path,
    const char *name,
    model_import_report *report,
    char *error,
    size_t size
);
void model_import_report_free(model_import_report *report);

typedef struct model_import_plan model_import_plan;
typedef struct
{
    char *key, *identity;
    unsigned char *data;
    size_t size;
    bool editable;
} model_import_output;
typedef struct
{
    char *path; /* canonical absolute source path */
    uint64_t hash;
    size_t size;
} model_import_dependency;
/* Read-only, bounded preparation. allow_existing permits outputs owned by a
 * recipe; declaration collisions are still checked. No directories are made. */
bool model_import_prepare(
    const char *content,
    const char *path,
    const char *name,
    bool allow_existing,
    model_import_plan **result,
    char *error,
    size_t size
);
const model_import_report *model_import_plan_report(const model_import_plan *);
const model_import_output *model_import_plan_outputs(const model_import_plan *, int *count);
const model_import_dependency *model_import_plan_dependencies(const model_import_plan *, int *count);
/* Recheck source bytes and declaration collisions before publishing a plan. */
bool model_import_plan_valid(const model_import_plan *, char *error, size_t size);
void model_import_plan_free(model_import_plan *);
uint64_t model_import_fingerprint(const void *, size_t);

#endif
