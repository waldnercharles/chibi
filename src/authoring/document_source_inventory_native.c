#include "authoring/document_source_inventory_definition.h"
#include "execution/function.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
static const char *copy(arena *storage, const char *text)
{
    char *out = arena_alloc(storage, strlen(text) + 1);
    strcpy(out, text);
    return out;
}
static bool identity(const char *text, uint64_t *out)
{
    if (!text) {
        return false;
    }
    if (!*text) {
        *out = 0;
        return true;
    }
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    errno = 0;
    char *end;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno || *end) {
        return false;
    }
    *out = value;
    return true;
}
static const char *run(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out,
    bool bindings
)
{
    char error[256] = { 0 };
    uint64_t id;
    int count = 0, total = 0;
    arena *storage = context->value_storage;
    const char *name = bindings ? "DocumentSourceBinding" : "DocumentSourceMember";
    out[0].ptr = pin_array_snapshot_allocate(storage, PIN_STRUCT, name, 0);
    out[1].i = 0;
    if (!identity(in[2].str, &id)) {
        out[2].str = copy(storage, "Source identities require decimal stable values");
        return NULL;
    }
    document_semantics *
        snapshot = document_semantics_service_snapshot(host, in[0].i, in[1].i, error, sizeof error);
    if (snapshot) {
        document_source_member *members = NULL;
        document_source_binding *choices = NULL;
        bool ok = bindings ? document_source_bindings(
                                 snapshot,
                                 id,
                                 in[3].i,
                                 in[4].i,
                                 in[5].str,
                                 storage,
                                 &choices,
                                 &count,
                                 &total,
                                 error,
                                 sizeof error
                             )
                           : document_source_members(
                                 snapshot,
                                 id,
                                 in[3].i,
                                 in[4].i,
                                 in[5].str,
                                 storage,
                                 &members,
                                 &count,
                                 &total,
                                 error,
                                 sizeof error
                             );
        if (ok) {
            pin_array *rows = pin_array_snapshot_allocate(storage, PIN_STRUCT, name, count);
            for (int i = 0; i < count; i++) {
                rows->items[i] = (pin_value){ .type = PIN_STRUCT,
                                              .type_name = name,
                                              .ptr = bindings
                                                       ? (void *)&choices[i]
                                                       : (void *)&members[i] };
            }
            out[0].ptr = rows;
            out[1].i = total;
        }
    }
    out[2].str = copy(storage, error);
    document_semantics_free(snapshot);
    return NULL;
}
static const char *members(const behavior_function_context *c, void *host, const pin_value *in, pin_value *out)
{
    return run(c, host, in, out, false);
}
static const char *bindings(
    const behavior_function_context *c,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    return run(c, host, in, out, true);
}
const node_host_call DOCUMENT_SOURCE_INVENTORY_CALLS[2] = {
    { .service = "document_semantics",
      .optional_service = true,
      .missing_service = "Semantic document service is unavailable",
      .context = members },
    { .service = "document_semantics",
      .optional_service = true,
      .missing_service = "Semantic document service is unavailable",
      .context = bindings }
};
const node_access DOCUMENT_SOURCE_INVENTORY_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
