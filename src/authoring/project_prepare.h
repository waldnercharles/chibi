#ifndef CHIBI_AUTHORING_PROJECT_PREPARE_H
#define CHIBI_AUTHORING_PROJECT_PREPARE_H
#include "assets/project.h"
#include "runtime/native_capabilities.h"
#include "runtime/player_prepared.h"

typedef struct
{
    const char *project, *scene;
    native_capabilities *native;
    ecs_component_catalog components;
    unsigned modules;
} authoring_project_options;
typedef struct
{
    unsigned char *data;
    size_t size;
    char *manifest; /* Owned JSON resource manifest; no executable source. */
} authoring_project_artifact;
/* Offline operation owns and clears the process's authoring visual catalogs;
 * call only while previews are quiescent. Compile a bounded disk snapshot
 * without attaching entities or dispatching Begin. The result owns runtime
 * bytes and retains no authoring source. The result must initially be empty. Native
 * descriptors and kernels remain supplied by the eventual player host. */
bool authoring_project_prepare(
    const authoring_project_options *,
    authoring_project_artifact *,
    char *error,
    size_t size
);
void authoring_project_artifact_free(authoring_project_artifact *);
/* The manifest's application settings as preparation reads them: a title
 * that fits, and an icon that decodes as a PNG. */
bool authoring_project_application_valid(const chibi_project *, char *error, size_t size);
#endif
