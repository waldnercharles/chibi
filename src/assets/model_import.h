#ifndef CHIBI_ASSETS_MODEL_IMPORT_H
#define CHIBI_ASSETS_MODEL_IMPORT_H

#include <stdbool.h>
#include <stddef.h>

/* The most bytes of a model file an import reads. */
#define MODEL_IMPORT_BYTES_MAX (256 * 1024 * 1024)

typedef struct
{
    char **written;  /* content/... keys of the files written, in order */
    char **warnings; /* what the import left out, one sentence each */
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

#endif
