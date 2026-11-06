#include "graph/authoring/constant.h"

#include "graph/model/pin_conv.h"
#include "support/arena.h"

#include <assert.h>

bool graph_constant_value(const graph *g, const node *n, arena *storage, pin_value *value)
{
    assert(g && n && storage && value);
    const node_desc *d = n->desc;
    if (graph_node_by_id(g, n->id) != n || !d->foldable ||
        d->pure_kind != NODE_PURE_CALL || !d->fn || d->read || d->read_inputs ||
        n->input_count != d->input_count || n->output_count != 1 ||
        d->output_count != 1 || n->output_pins[0].kind != PIN_DATA) {
        return false;
    }
    pin_value *inputs = arena_alloc(storage, sizeof(*inputs) * (size_t)n->input_count);
    for (int i = 0; i < n->input_count; i++) {
        const pin_spec *pin = &n->input_pins[i];
        node *source = NULL;
        int source_pin = -1;
        if (pin->kind != PIN_DATA || pin->binding ||
            (pin->flags & PIN_FLAG_DEFAULT_SELF) ||
            graph_input_source(g, n, i, &source, &source_pin) ||
            pin->type != d->inputs[i].type ||
            !pin_types_compatible(pin, &d->inputs[i])) {
            return false;
        }
        inputs[i] = pin->default_value;
        if (pin->type == PIN_STRUCT && pin->container == PIN_CONTAINER_SINGLE &&
            !inputs[i].ptr) {
            return false;
        }
    }
    pin_value result = d->fn(inputs, n->input_count, storage);
    if (result.type == PIN_FLOW ||
        (n->output_pins[0].type == PIN_STRUCT && !result.ptr)) {
        return false;
    }
    *value = result;
    return true;
}
