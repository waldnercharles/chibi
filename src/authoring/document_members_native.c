#include "authoring/document_members_definition.h"
#include "execution/function.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
static const char *copy(const behavior_function_context *c, const char *s)
{
    return c->copy_string(c->value_storage, s ? s : "");
}
static pin_array *rows(const behavior_function_context *c, document_member *values, int count)
{
    pin_array *a = pin_array_snapshot_allocate(c->value_storage, PIN_STRUCT, "DocumentMember", count);
    for (int i = 0; i < count; i++) {
        a->items[i] = (pin_value){ .type = PIN_STRUCT,
                                   .type_name = "DocumentMember",
                                   .ptr = &values[i] };
    }
    return a;
}
static const char *inspect(const behavior_function_context *c, void *host, const pin_value *in, pin_value *out)
{
    for (int i = 0; i < 3; i++) {
        out[i].ptr = rows(c, NULL, 0);
    }
    if (!host || !in) {
        out[3].str = copy(c, !host ? "Semantic document service is unavailable" : "Missing member inventory inputs");
        return NULL;
    }
    char error[256] = "";
    document_semantics
        *s = document_semantics_service_snapshot(host, in[0].i, in[1].i, error, sizeof error);
    uint64_t graph = 0;
    bool valid = in[2].str && *in[2].str;
    for (const char *p = in[2].str; p && *p; p++) {
        if (*p < '0' || *p > '9') {
            valid = false;
        }
    }
    errno = 0;
    if (valid) {
        graph = strtoull(in[2].str, NULL, 10);
        valid = !errno;
    }
    document_member *members = NULL, *pins = NULL, *types = NULL;
    int mc = 0, pc = 0, tc = 0;
    if (s && valid &&
        document_members_inspect(s, graph, c->value_storage, &members, &mc, &pins, &pc, &types, &tc, error, sizeof error)) {
        out[0].ptr = rows(c, members, mc);
        out[1].ptr = rows(c, pins, pc);
        out[2].ptr = rows(c, types, tc);
    } else if (!valid) {
        strcpy(error, "Graph requires a decimal stable identity");
    }
    out[3].str = copy(c, error);
    document_semantics_free(s);
    return NULL;
}
static const char *prepare(const behavior_function_context *c, void *host, const pin_value *in, pin_value *out)
{
    if (!host || !in) {
        out[0].b = false;
        out[1].str = copy(c, !host ? "Semantic document service is unavailable" : "Missing member edit inputs");
        out[2].str = copy(c, "");
        out[3].str = copy(c, "");
        return NULL;
    }
    char error[256] = "";
    char *text = NULL, *metadata = NULL;
    document_semantics
        *s = document_semantics_service_snapshot(host, in[0].i, in[1].i, error, sizeof error);
    out[0].b = s &&
               document_member_prepare(s, in[2].ptr, &text, &metadata, error, sizeof error);
    out[1].str = copy(c, error);
    out[2].str = copy(c, text);
    out[3].str = copy(c, metadata);
    free(text);
    free(metadata);
    document_semantics_free(s);
    return NULL;
}
const node_host_call DOCUMENT_MEMBERS_CALLS[2] = {
    { .service = "document_semantics",
      .optional_service = true,
      .missing_service = "Semantic document service is unavailable",
      .context = inspect },
    { .service = "document_semantics",
      .optional_service = true,
      .missing_service = "Semantic document service is unavailable",
      .context = prepare }
};
const node_access DOCUMENT_MEMBERS_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
