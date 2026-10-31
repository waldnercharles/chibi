#pragma once
#include "runtime/behaviors/package.h"
#include "runtime/native_capabilities.h"

typedef struct authoring_graph_catalog authoring_graph_catalog;
/* The application composition chibi build compiles with: its natives, and its
 * node packs, installed in the graph registry until authoring_application_end. */
typedef struct
{
    native_capabilities *native;
    authoring_graph_catalog *graphs;
} authoring_application;
bool authoring_application_begin(authoring_application *, char *error, size_t size);
void authoring_application_end(authoring_application *);
/* Read-only project validation and static cost reporting on stdout. JSON
 * output contains only the report; neither mode executes game functions. */
int authoring_project_explain_costs(const char *project, const char *scene, bool json);
/* Deployed offline compiler command. Uses the explicit shared application
 * capability composition and writes the artifact plus resource manifest. A
 * NULL output checks the project as it is: nothing is migrated or written. */
int authoring_project_compile(const char *project, const char *scene, const char *output);
/* The same artifact assembled from `modules`, one for each of the project's
 * executable sources, as another compiler prepared them, instead of compiling
 * the sources. Zero modules assembles a resource-only project and refuses
 * any executable source. Borrows `modules`. */
int authoring_project_compile_modules(
    const char *project,
    const package_module *modules,
    int count,
    const char *output
);
/* chibi test: loads every package below root, the project: collection, with
 * the core collection at core; both are absolute directories. Prepares them
 * for the natives that need no running game and runs each `@test` function on
 * the default backend. Pure tests run on the bounded runtime worker pool;
 * CHIBI_TEST_WORKERS=1 forces serial execution (1..32 selects a hardware-capped
 * worker budget). Call on the controlling thread, outside a pool submission.
 * 0 when every test passes, 1 for a failure, invalid package or worker setting. */
int authoring_project_test(const char *root, const char *core);
/* chibi doc: writes the site root/docs/site.kdl lists into root/build/docs:
 * each page's Markdown, package reference or grammar rendered and laid out by
 * the generator packages at generator_root (markdown, reference, grammar and
 * site), whose imports of core: resolve to core. Every path is absolute. 0
 * when every page was written, 1 otherwise. */
int authoring_project_docs(const char *root, const char *generator_root, const char *core);
