#include "authoring/workspace_css.h"
#include "authoring/workspace_runtime.h"
#include "authoring/workspace_saved.h"
#include "authoring/workspace_tools_definition.h"
#include "execution/function.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *own(arena *a, const char *s)
{
    char *c = arena_alloc(a, strlen(s) + 1);
    strcpy(c, s);
    return c;
}
static pin_array *rows(arena *a, const char *type, void *data, size_t size, int count)
{
    pin_array *out = pin_array_snapshot_allocate(a, PIN_STRUCT, type, count);
    for (int i = 0; i < count; i++) {
        out->items[i] = (pin_value){ .type = PIN_STRUCT,
                                     .type_name = type,
                                     .ptr = (char *)data + (size_t)i * size };
    }
    return out;
}
static const char *query_save(
    const behavior_function_context *c,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    arena *a = c->value_storage;
    char error[256] = "";
    workspace_saved_row *values = NULL;
    int total = 0, n = 0;
    uint64_t revision = 0;
    save *store = workspace_runtime_save(host, in[0].i, false, error, sizeof error);
    out[0].b = store &&
               workspace_saved_query(store, in[1].i, in[2].i, a, &values, &total, &n, &revision, error, sizeof error);
    out[1].ptr = rows(a, "WorkspaceSavedRow", values, sizeof *values, n);
    out[2].i = total;
    char token[32];
    snprintf(token, sizeof token, "%" PRIu64, revision);
    out[3].str = own(a, token);
    out[4].str = own(a, error);
    return NULL;
}
static bool revision_of(const char *s, uint64_t *out)
{
    if (!s || !*s) {
        return false;
    }
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    errno = 0;
    char *end;
    *out = strtoull(s, &end, 10);
    return !errno && !*end;
}
static const char *edit_save(
    const behavior_function_context *c,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    char error[256] = "";
    uint64_t revision = 0;
    save *store = workspace_runtime_save(host, in[0].i, true, error, sizeof error);
    if (!revision_of(in[1].str, &revision)) {
        snprintf(error, sizeof error, "Saved data revision is invalid");
    }
    out[0].b = store && !error[0] &&
               workspace_saved_write(store, revision, in[2].str, in[3].str, error, sizeof error);
    out[1].str = own(c->value_storage, error);
    return NULL;
}
static const char *query_css(
    const behavior_function_context *c,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    workspace_css_service *s = host;
    workspace_css_row *values = NULL;
    int n = 0;
    char error[256] = "";
    out[0].b = workspace_css_document_query(s, in[0].i, in[1].i, c->value_storage, &values, &n, error, sizeof error);
    out[1].ptr = rows(c->value_storage, "WorkspaceCssRow", values, sizeof *values, n);
    out[2].str = own(c->value_storage, error);
    return NULL;
}
static const char *edit_css(
    const behavior_function_context *c,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    workspace_css_service *s = host;
    char error[256] = "";
    out[0].b = workspace_css_edit(
        s->documents,
        in[0].str,
        in[1].str,
        in[2].i,
        in[3].i,
        in[4].str,
        in[5].str,
        &out[1].i,
        &out[2].i,
        error,
        sizeof error
    );
    out[3].str = own(c->value_storage, error);
    return NULL;
}
const node_host_call WORKSPACE_TOOLS_CALLS[4] = {
    { .service = "workspace_runtime",
      .context = query_save,
      .optional_service = true,
      .missing_service = "Workspace tool host is unavailable" },
    { .service = "workspace_runtime",
      .context = edit_save,
      .optional_service = true,
      .missing_service = "Workspace tool host is unavailable" },
    { .service = "workspace_css",
      .context = query_css,
      .optional_service = true,
      .missing_service = "Workspace tool host is unavailable" },
    { .service = "workspace_css",
      .context = edit_css,
      .optional_service = true,
      .missing_service = "Workspace tool host is unavailable" }
};
const node_access WORKSPACE_TOOLS_READ = { .flags = NODE_EFFECT_WORLD_READ },
                  WORKSPACE_TOOLS_WRITE = { .flags = NODE_EFFECT_HOST_SINK };
