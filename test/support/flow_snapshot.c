#include "test/flow_snapshot.h"

#include "execution/scalar.h"
#include "graph/model/registry.h"
#include "graph/runtime/flow_internal.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
test_flow_snapshot test_flow_snapshot_take(flow_run *run, const behavior_instance *instance, int result)
{
    const node *node = flow_run_current_node(run);
    test_flow_snapshot s = { .state = flow_run_status(run),
                             .result = result,
                             .steps = flow_run_steps(run),
                             .wake = flow_run_wake_time(run),
                             .node = node ? node->id : 0 };
    arena_init(&s.storage, 1024);
    const flow_run *leaf = flow_run_active_leaf(run);
    s.host_wait = flow_run_waiting_for_host(run);
    s.leaf_host_wait = flow_run_waiting_for_host(leaf);
    s.error = *flow_run_error(run);
    s.leaf_root = leaf == run;
    s.leaf_depth = flow_run_call_depth(leaf);
    s.leaf_state = flow_run_status(leaf);
    s.leaf_steps = flow_run_steps(leaf);
    flow_inputs inputs = flow_run_inputs(leaf);
    for (int i = 0; i < inputs.count; i++) {
        pin_value value;
        assert(!pin_value_snapshot_to_arena(&s.storage, &value, &inputs.values[i]));
        apush(s.leaf_inputs, value);
    }
    for (int i = 0; i < acount(instance->asset->variables); i++) {
        pin_value value;
        assert(!pin_value_snapshot_to_arena(
            &s.storage,
            &value,
            &instance->execution.variables[i]
        ));
        apush(s.variables, value);
    }
    int count;
    const flow_call_frame *stack = flow_run_call_stack(run, &count);
    for (int i = 0; i < count; i++) {
        apush(s.stack, stack[i]);
    }
    stack = flow_run_call_stack(leaf, &count);
    for (int i = 0; i < count; i++) {
        apush(s.leaf_stack, stack[i]);
    }
    for (int g = 0; g < acount(instance->asset->graphs); g++) {
        const graph *graph = instance->asset->graphs[g];
        for (int i = 0; i < acount(graph->nodes); i++) {
            const struct node *n = graph->nodes[i];
            for (int output = 0; output < 2; output++) {
                int count = output ? n->output_count : n->input_count;
                for (int pin = 0; pin < count; pin++) {
                    pin_value value;
                    int status = output
                                   ? flow_run_watch_output(run, n, pin, &value)
                                   : flow_run_watch_input(run, n, pin, &value);
                    if (status) {
                        continue;
                    }
                    pin_value copy;
                    assert(!pin_value_snapshot_to_arena(&s.storage, &copy, &value));
                    apush(s.watches, ((test_flow_watch){ n->id, pin, output, copy }));
                }
            }
        }
    }
    const flow_traversal *traversals = flow_run_traversals(run, &count);
    for (int i = 0; i < count; i++) {
        apush(s.traversals, traversals[i]);
    }
    traversals = flow_run_traversals(leaf, &count);
    for (int i = 0; i < count; i++) {
        apush(s.leaf_traversals, traversals[i]);
    }
    return s;
}
void test_flow_snapshot_free(test_flow_snapshot *s)
{
    arena_free(&s->storage);
    afree(s->variables);
    afree(s->leaf_inputs);
    afree(s->leaf_stack);
    afree(s->leaf_traversals);
    afree(s->watches);
    afree(s->traversals);
    afree(s->stack);
}
static uint64_t watch_bits(pin_value value)
{
    if (value.type == PIN_F32 || value.type == PIN_F64) {
        uint64_t bits = 0;
        memcpy(&bits, &value.d, behavior_scalar_size(value.type));
        return bits;
    }
    return value.type == PIN_FLOW ? 0 : behavior_scalar_bits(value);
}
static bool value_equal(pin_value a, pin_value b)
{
    if (a.type != b.type || a.container != b.container ||
        (a.type_name == NULL) != (b.type_name == NULL) ||
        (a.type_name && strcmp(a.type_name, b.type_name))) {
        return false;
    }
    if (a.container >= PIN_CONTAINER_ARRAY) {
        const pin_array *x = a.ptr, *y = b.ptr;
        if (!x || !y) {
            return x == y;
        }
        if (x->count != y->count || x->element_type != y->element_type) {
            return false;
        }
        for (int i = 0; i < x->count; i++) {
            if (!value_equal(x->items[i], y->items[i])) {
                return false;
            }
        }
        return true;
    }
    if (a.type == PIN_STRUCT) {
        if (!a.ptr || !b.ptr) {
            return a.ptr == b.ptr;
        }
        const type_desc *type = type_catalog_find(a.type_name);
        assert(type);
        for (int i = 0; i < type->field_count; i++) {
            pin_value x = { 0 }, y = { 0 };
            pin_read_field(a.ptr, &type->fields[i], &x);
            pin_read_field(b.ptr, &type->fields[i], &y);
            if (!value_equal(x, y)) {
                return false;
            }
        }
        return true;
    }
    if (a.type == PIN_NAME || a.type == PIN_STR) {
        return (!a.str || !b.str) ? a.str == b.str : !strcmp(a.str, b.str);
    }
    return watch_bits(a) == watch_bits(b);
}
void test_flow_snapshot_same(test_flow_snapshot a, test_flow_snapshot b)
{
    assert(a.host_wait == b.host_wait && a.leaf_host_wait == b.leaf_host_wait);
    assert(a.error.code == b.error.code && a.error.pin == b.error.pin);
    assert(a.error.node == b.error.node);
    assert((a.error.message == NULL) == (b.error.message == NULL));
    assert(!a.error.message || !strcmp(a.error.message, b.error.message));
    if (a.leaf_depth != b.leaf_depth || a.leaf_state != b.leaf_state ||
        a.leaf_steps != b.leaf_steps || a.leaf_root != b.leaf_root ||
        acount(a.leaf_inputs) != acount(b.leaf_inputs)) {
        fprintf(
            stderr,
            "leaf actual root=%d depth=%d state=%d steps=%lu inputs=%d "
            "expected root=%d depth=%d state=%d steps=%lu inputs=%d\n",
            a.leaf_root,
            a.leaf_depth,
            a.leaf_state,
            a.leaf_steps,
            acount(a.leaf_inputs),
            b.leaf_root,
            b.leaf_depth,
            b.leaf_state,
            b.leaf_steps,
            acount(b.leaf_inputs)
        );
        assert(false);
    }
    for (int i = 0; i < acount(a.leaf_inputs); i++) {
        assert(value_equal(a.leaf_inputs[i], b.leaf_inputs[i]));
    }
    if (acount(a.leaf_stack) != acount(b.leaf_stack)) {
        fprintf(
            stderr,
            "leaf stack count=%d/%d state=%d node=%llu steps=%lu\n",
            acount(a.leaf_stack),
            acount(b.leaf_stack),
            a.state,
            (unsigned long long)a.node,
            a.steps
        );
        for (int i = 0; i < acount(a.leaf_stack); i++) {
            fprintf(
                stderr,
                " actual %llu:%llu\n",
                (unsigned long long)a.leaf_stack[i].graph_id,
                (unsigned long long)a.leaf_stack[i].node_id
            );
        }
        for (int i = 0; i < acount(b.leaf_stack); i++) {
            fprintf(
                stderr,
                " expected %llu:%llu\n",
                (unsigned long long)b.leaf_stack[i].graph_id,
                (unsigned long long)b.leaf_stack[i].node_id
            );
        }
        assert(false);
    }
    for (int i = 0; i < acount(a.leaf_stack); i++) {
        assert(
            a.leaf_stack[i].graph_id == b.leaf_stack[i].graph_id &&
            a.leaf_stack[i].node_id == b.leaf_stack[i].node_id
        );
    }
    assert(acount(a.leaf_traversals) == acount(b.leaf_traversals));
    for (int i = 0; i < acount(a.leaf_traversals); i++) {
        flow_traversal x = a.leaf_traversals[i], y = b.leaf_traversals[i];
        assert(
            x.from_id == y.from_id && x.from_pin_id == y.from_pin_id &&
            x.to_id == y.to_id && x.to_pin_id == y.to_pin_id && x.kind == y.kind &&
            x.timestamp == y.timestamp && x.activation == y.activation
        );
    }
    assert(acount(a.variables) == acount(b.variables));
    for (int i = 0; i < acount(a.variables); i++) {
        assert(value_equal(a.variables[i], b.variables[i]));
    }
    if (a.state != b.state || a.result != b.result || a.steps != b.steps ||
        a.node != b.node || acount(a.stack) != acount(b.stack)) {
        fprintf(
            stderr,
            "flow snapshot actual state=%d result=%d "
            "steps=%lu node=%llu stack=%d; expected state=%d result=%d "
            "steps=%lu node=%llu stack=%d\n",
            a.state,
            a.result,
            a.steps,
            (unsigned long long)a.node,
            acount(a.stack),
            b.state,
            b.result,
            b.steps,
            (unsigned long long)b.node,
            acount(b.stack)
        );
        assert(false);
    }
    if (a.wake != b.wake && !(isnan(a.wake) && isnan(b.wake))) {
        fprintf(
            stderr,
            "wake actual=%g expected=%g state=%d steps=%lu "
            "node=%llu\n",
            a.wake,
            b.wake,
            a.state,
            a.steps,
            (unsigned long long)a.node
        );
        assert(false);
    }
    for (int i = 0; i < acount(a.stack); i++) {
        assert(
            a.stack[i].node_id == b.stack[i].node_id &&
            a.stack[i].graph_id == b.stack[i].graph_id
        );
    }
    if (acount(a.watches) != acount(b.watches)) {
        fprintf(
            stderr,
            "watch count %d expected %d at node=%llu state=%d steps=%lu\n",
            acount(a.watches),
            acount(b.watches),
            (unsigned long long)a.node,
            a.state,
            a.steps
        );
        for (int i = 0; i < acount(a.watches); i++) {
            fprintf(
                stderr,
                " actual %llu:%d:%d\n",
                (unsigned long long)a.watches[i].node,
                a.watches[i].output,
                a.watches[i].pin
            );
        }
        for (int i = 0; i < acount(b.watches); i++) {
            fprintf(
                stderr,
                " expected %llu:%d:%d\n",
                (unsigned long long)b.watches[i].node,
                b.watches[i].output,
                b.watches[i].pin
            );
        }
        assert(false);
    }
    for (int i = 0; i < acount(a.watches); i++) {
        test_flow_watch x = a.watches[i], y = b.watches[i];
        assert(x.node == y.node && x.pin == y.pin && x.output == y.output);
        if (!value_equal(x.value, y.value)) {
            fprintf(
                stderr,
                "watch %llu:%d:%d type=%d/%d name=%s/%s\n",
                (unsigned long long)x.node,
                x.output,
                x.pin,
                x.value.type,
                y.value.type,
                x.value.type_name ? x.value.type_name : "NULL",
                y.value.type_name ? y.value.type_name : "NULL"
            );
            assert(false);
        }
    }
    if (acount(a.traversals) != acount(b.traversals)) {
        fprintf(
            stderr,
            "traversal count=%d expected=%d at node=%llu state=%d steps=%lu\n",
            acount(a.traversals),
            acount(b.traversals),
            (unsigned long long)a.node,
            a.state,
            a.steps
        );
        for (int i = 0; i < acount(a.traversals); i++) {
            fprintf(
                stderr,
                " actual edge %llu -> %llu kind=%d step=%lu\n",
                (unsigned long long)a.traversals[i].from_id,
                (unsigned long long)a.traversals[i].to_id,
                a.traversals[i].kind,
                a.traversals[i].activation
            );
        }
        for (int i = 0; i < acount(b.traversals); i++) {
            fprintf(
                stderr,
                " expected edge %llu -> %llu kind=%d step=%lu\n",
                (unsigned long long)b.traversals[i].from_id,
                (unsigned long long)b.traversals[i].to_id,
                b.traversals[i].kind,
                b.traversals[i].activation
            );
        }
        assert(false);
    }
    for (int i = 0; i < acount(a.traversals); i++) {
        flow_traversal x = a.traversals[i], y = b.traversals[i];
        assert(
            x.from_id == y.from_id && x.from_pin_id == y.from_pin_id &&
            x.to_id == y.to_id && x.to_pin_id == y.to_pin_id && x.kind == y.kind &&
            x.timestamp == y.timestamp && x.activation == y.activation
        );
    }
}
