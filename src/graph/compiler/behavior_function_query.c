#include "graph/compiler/behavior_lower_internal.h"
#include "graph/runtime/value_array.h"
#include "support/array.h"

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
    int name = behavior_lower_read(b, pointer, offsetof(pin_array, type_name), BF_POINTER_TYPE, true);
    int value = behavior_lower_array_value(b, pointer, name, &n->output_pins[0]);
    /* One buffer per lowered producer, reused only after its consumers finish.
     * Recursive loop activation is excluded. Escaping through locals,
     * parameters, variables or results now takes an owned snapshot. */
    behavior_lower_array(b, value)->temporary = true;
    return value;
}
