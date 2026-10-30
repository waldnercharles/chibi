#ifndef CHIBI_AUTHORING_PROJECT_PREPARE_H
#define CHIBI_AUTHORING_PROJECT_PREPARE_H
#include "assets/project.h"
#include "authoring/source_costs.h"
#include "runtime/behaviors/package.h"
#include "runtime/native_capabilities.h"
#include "runtime/player_prepared.h"

typedef struct
{
    const char *project, *scene;
    native_capabilities *native;
    ecs_component_catalog components;
    unsigned modules;
    /* Optional reporting of the finalized source facts; no game code runs. */
    authoring_cost_report_mode cost_report;
} authoring_project_options;
typedef struct
{
    unsigned char *data;
    size_t size;
    char *manifest;    /* Owned JSON resource manifest; no executable source. */
    char *cost_report; /* Owned optional report, separate from runtime bytes. */
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
/* Prepares the same artifact from modules compiled elsewhere, such as by the
 * stage-one compiler, instead of compiling the project's sources: one
 * prepared module per executable content source, identified as stage zero
 * identifies its own, script_catalog_id("module", the source's content
 * path). Assembly reads the declarations, scenes, prototypes and resources
 * itself and links the modules from their bytes. The native field/shader
 * backend independently analyzes library sources for their graph projection;
 * this never compiles replacement executable modules. Borrows `modules`. */
bool authoring_project_assemble(
    const authoring_project_options *,
    const package_module *modules,
    int count,
    authoring_project_artifact *,
    char *error,
    size_t size
);
void authoring_project_artifact_free(authoring_project_artifact *);
/* The declarations a project's modules link against, as preparation reads
 * them: its natives, with the components the project declares. */
typedef struct authoring_project_catalog authoring_project_catalog;
typedef struct source_catalog source_catalog;
authoring_project_catalog *authoring_project_catalog_open(
    const authoring_project_options *,
    char *error,
    size_t size
);
/* The project's executable sources, each with its identity and text, and
 * the catalog they compile against: source_catalog_view. */
const source_catalog *authoring_project_catalog_sources(const authoring_project_catalog *);
void authoring_project_catalog_free(authoring_project_catalog *);
/* The manifest's application settings as preparation reads them: a title
 * that fits, and an icon that decodes as a PNG. */
bool authoring_project_application_valid(const chibi_project *, char *error, size_t size);
#endif
