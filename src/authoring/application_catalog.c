#include "authoring/application_catalog.h"

#include "authoring/renderer_catalog.h"
#include "authoring/workspace_catalog.h"

native_capabilities *authoring_application_catalog_new(bool workspace, char *error, size_t size)
{
    return workspace ? authoring_workspace_catalog_new(NULL, 0, error, size)
                     : authoring_renderer_catalog_new(error, size);
}
