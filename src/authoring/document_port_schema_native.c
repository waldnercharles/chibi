#include "authoring/document_port_schema_definition.h"
#include "execution/function.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
static const char *own(arena *a, const char *s)
{
    char *v = arena_alloc(a, strlen(s) + 1);
    strcpy(v, s);
    return v;
}
static bool id(const char *s, uint64_t *out)
{
    if (!s || !*s) {
        return false;
    }
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    char *end;
    errno = 0;
    *out = strtoull(s, &end, 10);
    return !errno && !*end;
}
static pin_array *array(arena *a, const char *name, void *rows, int count, size_t size)
{
    pin_array *v = pin_array_snapshot_allocate(a, PIN_STRUCT, name, count);
    for (int i = 0; i < count; i++) {
        v->items[i] = (pin_value){ .type = PIN_STRUCT,
                                   .type_name = name,
                                   .ptr = (char *)rows + (size_t)i * size };
    }
    return v;
}
static const char *run(
    const behavior_function_context *c,
    void *host,
    const pin_value *in,
    pin_value *out,
    bool prepare
)
{
    document_semantics_service *service = host;
    arena *a = c->value_storage;
    char error[256] = { 0 };
    uint64_t graph, node, pin;
    if (prepare) {
        out[0].b = false;
        out[2].str = own(a, "");
    } else {
        out[0].ptr = array(a, "DocumentPortField", NULL, 0, 0);
        out[1].ptr = array(a, "DocumentPortChoice", NULL, 0, 0);
        out[2].i = 0;
    }
    document_semantics *snapshot = NULL;
    if (!service || !service->documents) {
        strcpy(error, "Semantic document service is unavailable");
    } else if (!id(in[2].str, &graph) || !id(in[3].str, &node) || !id(in[4].str, &pin)) {
        strcpy(error, "Graph, Node and Pin require decimal stable identities");
    } else {
        snapshot = document_semantics_service_snapshot(service, in[0].i, in[1].i, error, sizeof error);
    }
    if (snapshot) {
        if (prepare) {
            char *text = NULL;
            out[0].b = document_port_prepare_field(
                service->documents,
                in[0].i,
                snapshot,
                graph,
                node,
                pin,
                in[5].b,
                in[6].str,
                in[7].str,
                &text,
                error,
                sizeof error
            );
            if (text) {
                out[2].str = own(a, text);
                free(text);
            }
        } else {
            document_port_field *fields = NULL;
            document_port_choice *choices = NULL;
            int fc = 0, cc = 0, total = 0;
            if (document_port_schema(
                    snapshot,
                    graph,
                    node,
                    pin,
                    in[5].b,
                    in[6].i,
                    in[7].i,
                    a,
                    &fields,
                    &fc,
                    &choices,
                    &cc,
                    &total,
                    error,
                    sizeof error
                )) {
                out[0].ptr = array(a, "DocumentPortField", fields, fc, sizeof *fields);
                out[1].ptr = array(a, "DocumentPortChoice", choices, cc, sizeof *choices);
                out[2].i = total;
            }
        }
    }
    out[prepare ? 1 : 3].str = own(a, error);
    document_semantics_free(snapshot);
    return NULL;
}
static const char *schema(const behavior_function_context *c, void *h, const pin_value *in, pin_value *out)
{
    return run(c, h, in, out, false);
}
static const char *prepare(const behavior_function_context *c, void *h, const pin_value *in, pin_value *out)
{
    return run(c, h, in, out, true);
}
const node_host_call DOCUMENT_PORT_SCHEMA_CALLS[2] = {
    { .service = "document_semantics",
      .optional_service = true,
      .missing_service = "Semantic document service is unavailable",
      .context = schema },
    { .service = "document_semantics",
      .optional_service = true,
      .missing_service = "Semantic document service is unavailable",
      .context = prepare }
};
const node_access DOCUMENT_PORT_SCHEMA_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
