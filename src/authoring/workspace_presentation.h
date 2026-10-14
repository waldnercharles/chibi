#ifndef CHIBI_WORKSPACE_PRESENTATION_H
#define CHIBI_WORKSPACE_PRESENTATION_H
#include "authoring/component_presentation.h"
#include "authoring/workspace_documents.h"
/* Optional project policy, including unsaved document overlays. An absent
 * policy restores engine defaults; invalid input preserves the prior policy. */
bool workspace_presentation_load(
    const workspace_documents *,
    ecs_component_catalog,
    component_presentation *,
    char *,
    size_t
);
#endif
