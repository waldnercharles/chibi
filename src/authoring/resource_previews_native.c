#include "authoring/resource_previews.h"
#include "authoring/resource_previews_definition.h"
#include "execution/function.h"

#include <string.h>
static const char *copy(arena *a, const char *s)
{
    if (!s) {
        s = "";
    }
    char *out = arena_alloc(a, strlen(s) + 1);
    strcpy(out, s);
    return out;
}
static const char *open_call(
    const behavior_function_context *c,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    if (!host) {
        out[0].i = 0;
        out[1].str = copy(c->value_storage, "Resource previews require an active workspace graphics host");
        return NULL;
    }
    char error[256] = { 0 };
    out[0].i = resource_preview_open(host, in[0].str, in[1].i, in[2].i, error, sizeof error);
    out[1].str = copy(c->value_storage, error);
    return NULL;
}
static const char *read_call(
    const behavior_function_context *c,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    if (!host) {
        out[0].b = false;
        out[1].str = copy(c->value_storage, "unavailable");
        out[2].str = copy(c->value_storage, "");
        out[3].str = copy(c->value_storage, "Resource previews require an active workspace graphics host");
        return NULL;
    }
    resource_preview_view view = { 0 };
    bool valid = resource_preview_read(host, in[0].i, &view);
    out[0].b = valid && view.ready;
    out[1].str = copy(c->value_storage, valid ? view.status : "closed");
    out[2].str = copy(c->value_storage, view.image);
    out[3].str = copy(c->value_storage, valid ? view.error : "Resource preview is closed");
    return NULL;
}
static const char *close_call(
    const behavior_function_context *,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    out[0].b = host && resource_preview_close(host, in[0].i);
    return NULL;
}
static const node_host_call OPEN = {
    .service = "resource_previews",
    .optional_service = true,
    .missing_service = "Resource previews are unavailable",
    .context = open_call
};
static const node_host_call READ = {
    .service = "resource_previews",
    .optional_service = true,
    .missing_service = "Resource previews are unavailable",
    .context = read_call
};
static const node_host_call CLOSE = {
    .service = "resource_previews",
    .optional_service = true,
    .missing_service = "Resource previews are unavailable",
    .context = close_call
};
static const node_access ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Open Resource Preview",
      .implementation = { .call = &OPEN, .access = &ACCESS } },
    { .canonical_name = "Read Resource Preview",
      .implementation = { .call = &READ, .access = &ACCESS } },
    { .canonical_name = "Close Resource Preview",
      .implementation = { .call = &CLOSE, .access = &ACCESS } }
};
const native_definition_pack RESOURCE_PREVIEWS_DEFINITION_PACK = {
    .definitions = RESOURCE_PREVIEW_DEFINITIONS,
    .definition_count = 3,
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS)
};
