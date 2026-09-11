#include "graph/model/registry.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/value_array.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>

static const type_desc *watch_type(const flow_run *run, const node *n, int output, int pin)
{
    if (!n || !n->desc) {
        return NULL;
    }
    const type_desc *type = n->desc->type;
    if (!output) {
        node *source = NULL;
        int source_pin = -1;
        if (graph_input_source(run->graph, n, pin, &source, &source_pin) &&
            source && source->desc) {
            type = source->desc->type;
        }
    }
    if (!type) {
        const pin_spec *spec = output ? n->output_pins + pin : n->input_pins + pin;
        if (spec && spec->type_name) {
            return registry_type_by_name(spec->type_name);
        }
    }
    return type;
}

static unsigned watch_hash(uint64_t node_id, uint64_t pin_id, int output)
{
    uint64_t hash = node_id * UINT64_C(0x9e3779b97f4a7c15) ^ pin_id ^ (uint64_t)output;
    hash ^= hash >> 33;
    hash *= UINT64_C(0xff51afd7ed558ccd);
    return (unsigned)(hash ^ (hash >> 32));
}

static int watch_slot(const flow_run *run, uint64_t node_id, uint64_t pin_id, int output)
{
    int slot = (int)(watch_hash(node_id, pin_id, output) &
                     (unsigned)(run->watch_capacity - 1));
    while (run->watch_index[slot]) {
        const flow_watch *watch = &run->watches[run->watch_index[slot] - 1];
        if (watch->node_id == node_id && watch->pin_id == pin_id &&
            watch->output == output) {
            break;
        }
        slot = (slot + 1) & (run->watch_capacity - 1);
    }
    return slot;
}

static void watch_reserve(flow_run *run)
{
    if ((acount(run->watches) + 1) * 2 <= run->watch_capacity) {
        return;
    }
    free(run->watch_index);
    run->watch_capacity = run->watch_capacity ? run->watch_capacity * 2 : 32;
    run->watch_index = xcalloc((size_t)run->watch_capacity, sizeof *run->watch_index);
    for (int i = 0; i < acount(run->watches); i++) {
        const flow_watch *watch = &run->watches[i];
        run->watch_index[watch_slot(run, watch->node_id, watch->pin_id, watch->output)] = i + 1;
    }
}

static const flow_watch *watch_find(const flow_run *run, const node *n, int output, int pin)
{
    if (!n || pin < 0 || !run->watch_capacity) {
        return NULL;
    }
    int count = output ? n->output_count : n->input_count;
    if (pin >= count) {
        return NULL;
    }
    uint64_t pin_id = output ? n->output_pins[pin].id : n->input_pins[pin].id;
    int index = run->watch_index[watch_slot(run, n->id, pin_id, output)];
    return index ? &run->watches[index - 1] : NULL;
}

static void watch_release(flow_watch *watch)
{
    if (!watch || !watch->owned) {
        return;
    }
    if (watch->value.type == PIN_STRUCT &&
        watch->value.container == PIN_CONTAINER_SINGLE) {
        free(watch->owned);
        watch->value = (pin_value){ 0 };
    } else {
        pin_value_destroy(&watch->value);
    }
    watch->owned = NULL;
    watch->owned_size = 0;
}

int flow_watch_store(flow_run *run, const node *n, int output, int pin, const pin_value *value)
{
    if (!run || !n || !value) {
        return -1;
    }
    flow_watch *watch = (flow_watch *)watch_find(run, n, output, pin);
    if (!watch) {
        uint64_t pin_id = output ? n->output_pins[pin].id : n->input_pins[pin].id;
        watch_reserve(run);
        apush(
            run->watches,
            (flow_watch){
                .node_id = n->id,
                .pin_id = pin_id,
                .output = output,
                .value = { .type = PIN_FLOW },
                .owned = NULL,
                .owned_size = 0,
            }
        );
        watch = &run->watches[acount(run->watches) - 1];
        run->watch_index[watch_slot(run, n->id, pin_id, output)] = acount(run->watches);
    }
    if (value->type == PIN_STRUCT && value->container == PIN_CONTAINER_SINGLE &&
        value->ptr) {
        const type_desc *type = watch_type(run, n, output, pin);
        if (type && watch->owned && watch->value.type == PIN_STRUCT &&
            watch->value.container == PIN_CONTAINER_SINGLE &&
            watch->owned_size == type->size) {
            memmove(watch->owned, value->ptr, type->size);
            watch->value = *value;
            watch->value.ptr = watch->owned;
            return 0;
        }
    }
    watch_release(watch);
    watch->value = *value;

    if (value->container >= PIN_CONTAINER_ARRAY && value->ptr) {
        if (pin_value_copy(&watch->value, value)) {
            watch->value = (pin_value){ .type = PIN_FLOW };
            return -1;
        }
        watch->owned = watch->value.ptr;
    } else if ((value->type == PIN_STR || value->type == PIN_NAME) && value->str) {
        if (pin_value_copy(&watch->value, value)) {
            watch->value = (pin_value){ .type = PIN_FLOW };
            return -1;
        }
        watch->owned = (void *)watch->value.str;
    } else if (value->type == PIN_STRUCT && value->ptr) {
        const type_desc *type = watch_type(run, n, output, pin);
        if (!type || !type->size) {
            watch->value = (pin_value){ .type = PIN_FLOW };
            return -1;
        }
        watch->owned = xmalloc(type->size);
        memcpy(watch->owned, value->ptr, type->size);
        watch->owned_size = type->size;
        watch->value.ptr = watch->owned;
    }
    return 0;
}

void flow_watch_capture(flow_run *run, node *n)
{
    if (run->instance && !run->instance->trace_enabled) {
        return;
    }
    if (!run || !n || !n->desc) {
        return;
    }
    const runtime_node *state = runtime_store_node(run->runtime, n);
    const pin_value *inputs = runtime_node_inputs_const(state);
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].kind == PIN_DATA) {
            (void)flow_watch_store(run, n, 0, i, &inputs[i]);
        }
    }
    flow_watch_capture_outputs(run, n);
}

void flow_watch_capture_outputs(flow_run *run, node *n)
{
    if (run->instance && !run->instance->trace_enabled) {
        return;
    }
    const runtime_node *state = runtime_store_node(run->runtime, n);
    const pin_value *outputs = runtime_node_outputs_const(state);
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].kind != PIN_DATA) {
            continue;
        }
        if (n->output_pins[i].field_source || n->output_pins[i].field_name) {
            int source = -1;
            const type_field *field = graph_output_field(n, i, &source);
            if (!field || outputs[source].type != PIN_STRUCT || !outputs[source].ptr) {
                continue;
            }
            pin_value value = { 0 };
            pin_read_field(outputs[source].ptr, field, &value);
            (void)flow_watch_store(run, n, 1, i, &value);
            continue;
        }
        (void)flow_watch_store(run, n, 1, i, &outputs[i]);
    }
}

void flow_watch_clear(flow_run *run)
{
    for (int i = 0; i < acount(run->watches); i++) {
        watch_release(&run->watches[i]);
    }
    afree(run->watches);
    free(run->watch_index);
    run->watch_index = NULL;
    run->watch_capacity = 0;
}

static int flow_run_watch_get(const flow_run *run, const node *n, int output, int pin, pin_value *out)
{
    if (!run || !n || !out || pin < 0) {
        return -1;
    }
    const flow_watch *w = watch_find(run, n, output, pin);
    if (!w) {
        for (int i = acount(run->children) - 1; i >= 0; i--) {
            if (flow_run_watch_get(run->children[i], n, output, pin, out) == 0) {
                return 0;
            }
        }
        return -1;
    }
    *out = w->value;
    return 0;
}

int flow_run_watch_input(const flow_run *run, const node *n, int pin, pin_value *out)
{
    return flow_run_watch_get(run, n, 0, pin, out);
}

int flow_run_watch_output(const flow_run *run, const node *n, int pin, pin_value *out)
{
    return flow_run_watch_get(run, n, 1, pin, out);
}

int flow_run_watch_copy_output(
    const flow_run *run,
    const node *n,
    int pin,
    pin_value *out,
    void *struct_storage,
    size_t struct_storage_size
)
{
    if (!run || !n || !out || pin < 0) {
        return -1;
    }
    const flow_watch *w = watch_find(run, n, 1, pin);
    if (!w) {
        return -1;
    }
    *out = w->value;
    if (out->type == PIN_STRUCT) {
        if (!w->owned || !struct_storage || struct_storage_size < w->owned_size) {
            return -1;
        }
        memcpy(struct_storage, w->owned, w->owned_size);
        out->ptr = struct_storage;
    }
    return 0;
}
