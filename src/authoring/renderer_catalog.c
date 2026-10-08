#include "authoring/renderer_catalog.h"

#include "host/native_default.h"
#include "render/shader_parameters.h"

native_capabilities *authoring_renderer_catalog_new(char *error, size_t size)
{
    return host_native_default_catalog_new(NULL, 0, &RENDER_SHADER_PARAMETER_DEFINITION_PACK, 1, error, size);
}
