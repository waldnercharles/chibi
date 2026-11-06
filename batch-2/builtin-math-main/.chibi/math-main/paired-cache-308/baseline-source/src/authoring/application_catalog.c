#include "authoring/application_catalog.h"

#include "authoring/renderer_catalog.h"
#include "authoring/studio_catalog.h"
#include "render/shader_parameters.h"

native_capabilities *authoring_application_catalog_new(bool studio, char *error, size_t size)
{
    if (studio) {
        return authoring_studio_native_catalog_new(&RENDER_SHADER_PARAMETER_DEFINITION_PACK, 1, error, size);
    }
    return authoring_renderer_catalog_new(error, size);
}
