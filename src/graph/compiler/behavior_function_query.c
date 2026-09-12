#include "graph/compiler/behavior_lower_internal.h"
#include "graph/runtime/eval.h"
#include "support/array.h"

#include <stdlib.h>
#include <string.h>

void behavior_function_prepare_queries(const behavior_function *f, behavior_function_context *ctx)
{
    assert(!ctx->queries);
    for (int i = 0; i < acount(f->queries); i++) {
        const behavior_function_query *q = &f->queries[i];
        behavior_query_binding binding = {
            .read = q->read,
            .key = q->key,
            .snapshot = { .element_type = PIN_HANDLE, .type_name = "ecs.entity" }
        };
        for (int j = 0; j < acount(ctx->services); j++) {
            if (!strcmp(ctx->services[j].type_name, q->service)) {
                binding.service = ctx->services[j].value;
                break;
            }
        }
        apush(ctx->queries, binding);
    }
}
void behavior_function_release_queries(behavior_function_context *ctx)
{
    for (int i = 0; i < acount(ctx->queries); i++) {
        free(ctx->queries[i].snapshot.items);
    }
    afree(ctx->queries);
}
int behavior_lower_query(builder *b, node *n)
{
    assert(n->desc->query_service && n->input_count == 1);
    node *source;
    int pin;
    const pin_spec *key = &n->input_pins[0];
    if (graph_input_source(b->graph, n, 0, &source, &pin)) {
        if (source->desc->pure_kind != NODE_PURE_CONST ||
            !node_desc_is_pure(source->desc)) {
            return behavior_lower_unsupported(b, n, 0, false, "dynamic query component key");
        }
        key = &source->output_pins[pin];
    }
    if (key->binding || key->type != PIN_NAME || key->default_value.type != PIN_NAME ||
        n->output_count != 1 || n->output_pins[0].type != PIN_HANDLE ||
        n->output_pins[0].container != PIN_CONTAINER_ARRAY) {
        return behavior_lower_unsupported(b, n, 0, false, "query snapshot contract");
    }
    int index = acount(b->function->queries);
    apush(
        b->function->queries,
        ((behavior_function_query){ n->desc->query_service,
                                    key->default_value.str,
                                    n->desc->query })
    );
    int pointer = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_QUERY,
                                         .type = BF_POINTER_TYPE,
                                         .target = index,
                                         .node_id = n->id,
                                         .pin = 0 }
    );
    int value = behavior_lower_array_value(b, pointer, PIN_HANDLE);
    /* One buffer per lowered producer, reused only after its consumers finish.
     * Recursive loop activation is excluded. Query arrays cannot enter locals,
     * stored parameters, variables or results until those boundaries own snapshots. */
    behavior_lower_array(b, value)->temporary = true;
    return value;
}
