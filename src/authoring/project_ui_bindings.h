#ifndef CHIBI_PROJECT_UI_BINDINGS_H
#define CHIBI_PROJECT_UI_BINDINGS_H
#include "assets/prototype.h"
#include "authoring/source_catalog.h"
#include "authoring/ui_binding_navigation.h"
typedef struct
{
    const char *path;
    authoring_ui_binding_reference reference;
} project_ui_unresolved;
/* Registry is the same unpublished generation as catalog. Association uses
 * actual prototype expansion and entity component/behavior replacement.
 * Output rows and names belong to storage. Their path/text pointers borrow the
 * source catalog; retain that catalog while consuming these rows. */
bool project_ui_bindings(
    const source_catalog *,
    const script_analysis *const *,
    int,
    const prototype_registry *,
    arena *,
    source_catalog_reference **,
    int *,
    project_ui_unresolved **,
    int *,
    char *,
    size_t
);
#endif
