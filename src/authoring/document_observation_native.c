#include "authoring/document_observation.h"
#include "authoring/document_observation_definition.h"
#include "execution/function.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
static bool identity(const char *text, uint64_t *out)
{
    if (!text || !*text) {
        return false;
    }
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    errno = 0;
    char *end;
    *out = strtoull(text, &end, 10);
    return !errno && !*end;
}
static const char *query(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    char error[256] = { 0 };
    document_observation result = { 0 };
    uint64_t graph = 0, node = 0, pin = 0;
    bool valid = identity(in[2].str, &graph) && identity(in[3].str, &node) &&
                 identity(in[4].str, &pin);
    document_semantics *snapshot = NULL;
    document_semantics_service *service = host;
    if (!service || !service->documents) {
        strcpy(error, "Semantic document service is unavailable");
    } else if (valid) {
        snapshot = document_semantics_service_snapshot(host, in[0].i, in[1].i, error, sizeof error);
    } else {
        strcpy(error, "Graph, node and pin require decimal stable identities");
    }
    valid = snapshot &&
            document_observation_query(snapshot, graph, node, pin, in[5].b, &result, error, sizeof error);
    out[0].b = valid;
    char *copy = arena_alloc(context->value_storage, strlen(error) + 1);
    strcpy(copy, error);
    out[1].str = copy;
    out[2].u64 = result.module;
    out[3].u64 = result.function;
    out[4].u64 = result.occurrence;
    out[5].i = result.index;
    out[6].b = result.output;
    out[7].u64 = result.fallback_occurrence;
    out[8].i = result.fallback_index;
    out[9].b = result.fallback_output;
    out[10].b = result.has_fallback;
    document_semantics_free(snapshot);
    return NULL;
}
const node_host_call DOCUMENT_OBSERVATION_CALL = {
    .service = "document_semantics",
    .optional_service = true,
    .missing_service = "Semantic document service is unavailable",
    .context = query
};
static const char *execution_keys(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    document_semantics_service *service = host;
    char error[256] = "";
    document_execution result = { 0 };
    document_semantics *snapshot = NULL;
    uint64_t graph = 0, node = 0;
    if (!service || !service->documents) {
        strcpy(error, "Semantic document service is unavailable");
    } else if (!identity(in[2].str, &graph) || !identity(in[3].str, &node)) {
        strcpy(error, "Graph and node require decimal stable identities");
    } else {
        snapshot = document_semantics_service_snapshot(service, in[0].i, in[1].i, error, sizeof error);
    }
    out[0].b = snapshot &&
               document_execution_query(snapshot, graph, node, &result, error, sizeof error);
    out[1].str = arena_alloc(context->value_storage, strlen(error) + 1);
    strcpy((char *)out[1].str, error);
    out[2].u64 = result.module;
    out[3].u64 = result.function;
    out[4].u64 = result.occurrence;
    document_semantics_free(snapshot);
    return NULL;
}
const node_host_call DOCUMENT_EXECUTION_CALL = {
    .service = "document_semantics",
    .optional_service = true,
    .missing_service = "Semantic document service is unavailable",
    .context = execution_keys
};
const node_access DOCUMENT_OBSERVATION_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
