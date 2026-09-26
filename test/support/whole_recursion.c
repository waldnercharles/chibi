#include "graph/compiler/behavior_function_internal.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/nodes/compare_nodes.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/flow.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/whole_async_fixture.h"
#include "test/whole_managed_fixture.h"
#include "test/whole_recursion_fixture.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void signature(graph *g, int kind)
{
    pin_spec spec = { .name = "N", .type = PIN_I32, .type_name = "int" };
    assert(graph_add_pin(g, g->nodes[0], true, &spec) == 1);
    spec.name = "Sum";
    assert(graph_add_pin(g, g->nodes[1], false, &spec) == 1);
    if (!(kind & WHOLE_RECURSION_VALUES)) {
        return;
    }
    const pin_spec values[] = {
        { .name = "Text", .type = PIN_STR, .type_name = "const char *" },
        { .name = "Record", .type = PIN_STRUCT, .type_name = "ManagedRecord" },
        { .name = "Records",
          .type = PIN_STRUCT,
          .type_name = "ManagedRecord",
          .container = PIN_CONTAINER_ARRAY },
        { .name = "Names", .type = PIN_NAME, .type_name = "name", .container = PIN_CONTAINER_SET }
    };
    for (int i = 0; i < 4; i++) {
        assert(graph_add_pin(g, g->nodes[0], true, &values[i]) == i + 2);
        assert(graph_add_pin(g, g->nodes[1], false, &values[i]) == i + 2);
    }
    pin_value member = { .type = PIN_NAME, .type_name = "name", .str = "default member" };
    pin_array set = { .element_type = PIN_NAME,
                      .type_name = "name",
                      .count = 1,
                      .capacity = 1,
                      .items = &member };
    pin_value initial = { .type = PIN_NAME,
                          .type_name = "name",
                          .container = PIN_CONTAINER_SET,
                          .ptr = &set };
    assert(!graph_set_pin_default(g, g->nodes[0], true, 5, &initial));
}
static void recursive_body(behavior_asset *asset, graph *g, graph *target, int kind)
{
    node *entry = g->nodes[0], *ret = g->nodes[1];
    node *call = behavior_asset_add_function_call_node_to_graph(asset, g, target);
    node *branch = graph_add_node(g, &NODE_Branch);
    node *condition = graph_add_node(g, &NODE_Integer_Greater);
    node *subtract = graph_add_node(g, &NODE_Subtract),
         *add = graph_add_node(g, &NODE_Add);
    node *arithmetic[] = { subtract, add };
    for (int j = 0; j < 2; j++) {
        assert(!graph_set_pin_type(g, arithmetic[j], false, 0, PIN_I32, "int"));
        assert(!graph_set_pin_type(g, arithmetic[j], false, 1, PIN_I32, "int"));
        assert(!graph_set_pin_type(g, arithmetic[j], true, 0, PIN_I32, "int"));
    }
    assert(
        !graph_set_pin_default(g, subtract, false, 1, &(pin_value){ .type = PIN_I32, .i = 1 })
    );
    assert(!graph_connect_data(g, entry, 1, condition, 0));
    assert(!graph_connect_data(g, condition, 0, branch, 1));
    assert(!graph_connect_data(g, entry, 1, subtract, 0));
    assert(!graph_connect_data(g, subtract, 0, call, 1));
    assert(!graph_connect_data(g, entry, 1, add, 0));
    assert(!graph_connect_data(g, call, 1, add, 1));
    assert(!graph_connect_data(g, add, 0, ret, 1));
    assert(!graph_connect_exec(g, entry, 0, branch, 0, 0));
    node *next = ret;
    if (kind & WHOLE_RECURSION_WAIT) {
        next = graph_add_node(g, &NODE_Delay);
        assert(
            !graph_set_pin_default(g, next, false, 1, &(pin_value){ .type = PIN_F64, .d = .125 })
        );
        assert(!graph_connect_exec(g, next, 0, ret, 0, 0));
    }
    assert(!graph_connect_exec(g, branch, 1, next, 0, 0));
    if (kind & WHOLE_RECURSION_LOOP) {
        node *loop = graph_add_node(g, &NODE_ForLoop);
        node *first = graph_add_node(g, &NODE_Integer_Equal);
        node *choose = graph_add_node(g, &NODE_Branch);
        assert(
            !graph_set_pin_default(g, loop, false, 2, &(pin_value){ .type = PIN_I32, .i = 1 })
        );
        assert(!graph_connect_exec(g, branch, 0, loop, 0, 0));
        assert(!graph_connect_exec(g, loop, 0, choose, 0, 0));
        assert(!graph_connect_exec(g, choose, 0, call, 0, 0));
        assert(!graph_connect_data(g, loop, 1, first, 0));
        assert(!graph_connect_data(g, first, 0, choose, 1));
        assert(!graph_connect_exec(g, loop, 2, next, 0, 0));
    } else {
        assert(!graph_connect_exec(g, branch, 0, call, 0, 0));
        assert(!graph_connect_exec(g, call, 0, next, 0, 0));
    }
    for (int i = 2; i < ret->input_count; i++) {
        assert(!graph_connect_data(g, entry, i, call, i));
        assert(!graph_connect_data(g, entry, i, ret, i));
    }
}
graph *whole_recursion_root(behavior_asset *asset, int kind)
{
    return asset->graphs
        [(kind & WHOLE_RECURSION_OUTER) && !(kind & WHOLE_RECURSION_LIBRARY)
             ? acount(asset->graphs) - 1
             : 1];
}

void whole_recursion_add_host(behavior_asset *asset)
{
    for (int i = 1; i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        int count = acount(g->nodes);
        for (int j = 0; j < count; j++) {
            node *delay = g->nodes[j];
            if (delay->desc != &NODE_Delay) {
                continue;
            }
            node *ret = g->nodes[1];
            assert(!graph_remove_edge(g, delay, 0, ret, 0));
            node *request = whole_async_node(g);
            assert(!graph_connect_exec(g, delay, 0, request, 0, 0));
            assert(!graph_connect_exec(g, request, 0, ret, 0, 0));
            assert(!graph_connect_data(g, g->nodes[0], 1, request, 2));
            assert(
                !graph_set_pin_default(g, request, false, 1, &(pin_value){ .type = PIN_NAME, .str = "recursive request" })
            );
        }
    }
}

void whole_recursion_add_event(behavior_asset *asset, const graph *function)
{
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(!graph_set_event_name(g, event, "Run"));
    assert(graph_add_pin(g, event, true, &function->nodes[0]->output_pins[1]) == 1);
    node *call = behavior_asset_add_function_call_node(asset, (graph *)function);
    uint64_t id = behavior_asset_add_variable(asset, "Result", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "")
                      ->id;
    node *set = behavior_asset_add_set_node(asset, id);
    assert(!graph_connect_exec(g, event, 0, call, 0, 0));
    assert(!graph_connect_exec(g, call, 0, set, 0, 0));
    assert(!graph_connect_data(g, event, 1, call, 1));
    assert(!graph_connect_data(g, call, 1, set, 1));
}

static behavior_asset *linked_graph(int kind)
{
    behavior_asset *library = whole_recursion_graph(kind & ~WHOLE_RECURSION_LIBRARY);
    library->domain = BEHAVIOR_DOMAIN_LIBRARY;
    graph *target = whole_recursion_root(library, kind & ~WHOLE_RECURSION_LIBRARY);
    behavior_asset *source = behavior_asset_new("Recursive library consumer");
    graph *g = behavior_asset_add_function(source, "Forward", false);
    signature(g, kind);
    node *call = behavior_asset_add_function_call_node_to_graph(source, g, target);
    assert(!graph_connect_exec(g, g->nodes[0], 0, call, 0, 0));
    assert(!graph_connect_exec(g, call, 0, g->nodes[1], 0, 0));
    for (int i = 1; i < g->nodes[1]->input_count; i++) {
        assert(!graph_connect_data(g, g->nodes[0], i, call, i));
        assert(!graph_connect_data(g, call, i, g->nodes[1], i));
    }
    const behavior_asset *catalog[] = { library };
    char error[256];
    behavior_asset *linked = behavior_asset_link_libraries(source, catalog, 1, error, sizeof error);
    if (!linked) {
        fprintf(stderr, "recursive library: %s / %s\n", error, graph_last_error());
    }
    assert(linked && acount(linked->graphs) == acount(library->graphs) + 1);
    for (int i = 2; i < acount(linked->graphs); i++) {
        const graph *g = linked->graphs[i];
        assert(
            g->source_graph_id && g->source_asset &&
            !strcmp(g->source_asset, library->name)
        );
        if (kind & WHOLE_RECURSION_VALUES) {
            const pin_value *value = &g->nodes[0]->output_pins[5].default_value;
            const pin_array *set = value->ptr;
            assert(value->container == PIN_CONTAINER_SET && set && set->count == 1);
            assert(!strcmp(set->items[0].str, "default member"));
            value = &g->nodes[1]->input_pins[5].default_value;
            set = value->ptr;
            assert(value->container == PIN_CONTAINER_SET && (!set || !set->count));
        }
    }
    behavior_asset_free(source);
    behavior_asset_free(library);
    return linked;
}

behavior_asset *whole_recursion_graph(int kind)
{
    if (kind & WHOLE_RECURSION_LIBRARY) {
        return linked_graph(kind);
    }
    behavior_asset *asset = behavior_asset_new("Recursive sum");
    graph *g = behavior_asset_add_function(asset, "Sum", false);
    signature(g, kind);
    graph *other = g;
    if (kind & WHOLE_RECURSION_MUTUAL) {
        other = behavior_asset_add_function(asset, "Other Sum", false);
        signature(other, kind);
    }
    recursive_body(asset, g, other, kind);
    if (other != g) {
        recursive_body(asset, other, g, kind);
    }
    if (kind & WHOLE_RECURSION_OUTER) {
        graph *outer = behavior_asset_add_function(asset, "Outer", false);
        signature(outer, kind);
        node *call = behavior_asset_add_function_call_node_to_graph(asset, outer, g);
        assert(!graph_connect_exec(outer, outer->nodes[0], 0, call, 0, 0));
        assert(!graph_connect_exec(outer, call, 0, outer->nodes[1], 0, 0));
        assert(!graph_connect_data(outer, outer->nodes[0], 1, call, 1));
        assert(!graph_connect_data(outer, call, 1, outer->nodes[1], 1));
        for (int i = 2; i < outer->nodes[1]->input_count; i++) {
            assert(!graph_connect_data(outer, outer->nodes[0], i, call, i));
            assert(!graph_connect_data(outer, call, i, outer->nodes[1], i));
        }
    }
    return asset;
}

static int emit(behavior_function *f, behavior_function_instruction i)
{
    if (i.op == BF_CONSTANT || i.op == BF_ARGUMENT) {
        i.value.type = i.type;
    }
    if (i.op < BF_STORE) {
        i.output = acount(f->slots);
        apush(f->slots, i.type);
    }
    apush(f->instructions, i);
    return i.op < BF_STORE ? i.output : acount(f->instructions) - 1;
}

/* sum(n) = n + sum(n - 1), sum(0) = 0. The caller's n is live across
 * INVOKE. A separately owned string travels through every argument/return. */
behavior_function *whole_recursion_ir(bool waits, int limit)
{
    behavior_function *f = xcalloc(1, sizeof *f);
    f->waits = waits;
    apush(f->frames, ((behavior_function_frame){ .source.parent = -1 }));
    apush(f->calls, ((behavior_function_invocation){ f, 0, 2, 2, 3 }));
    pin_type types[] = { PIN_I32, PIN_STR, PIN_BOOL, PIN_I32, PIN_STR };
    for (int j = 0; j < 5; j++) {
        apush(f->data_values, ((behavior_function_data_value){ .initial = { .type = types[j] } }));
    }
    emit(f, (behavior_function_instruction){ .op = BF_CHECK, .node_id = 1 });
    int n = emit(f, (behavior_function_instruction){ .op = BF_ARGUMENT, .type = PIN_I32, .left = 0 });
    int zero = emit(f, (behavior_function_instruction){ .op = BF_CONSTANT, .type = PIN_I32 });
    int base = emit(
        f,
        (behavior_function_instruction){ .op = BF_EQ, .type = PIN_BOOL, .left = n, .right = zero }
    );
    int branch = emit(f, (behavior_function_instruction){ .op = BF_BRANCH, .left = base });
    emit(f, (behavior_function_instruction){ .op = BF_CALL_CHECK, .left = limit, .node_id = 2 });
    int one = emit(
        f,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_I32,
                                         .value = { .type = PIN_I32, .i = 1 } }
    );
    int argument = emit(
        f,
        (behavior_function_instruction){ .op = BF_SUB, .type = PIN_I32, .left = n, .right = one }
    );
    int bank = emit(
        f,
        (behavior_function_instruction){ .op = BF_HOST_SLOT,
                                         .type = BF_POINTER_TYPE,
                                         .left = 0 }
    );
    emit(
        f,
        (behavior_function_instruction){ .op = BF_WRITE,
                                         .type = PIN_I32,
                                         .left = argument,
                                         .right = bank,
                                         .offset = offsetof(pin_value, d) }
    );
    int text = emit(f, (behavior_function_instruction){ .op = BF_ARGUMENT, .type = PIN_STR, .left = 1 });
    bank = emit(f, (behavior_function_instruction){ .op = BF_HOST_SLOT, .type = BF_POINTER_TYPE, .left = 1 });
    emit(
        f,
        (behavior_function_instruction){ .op = BF_WRITE,
                                         .type = PIN_STR,
                                         .left = text,
                                         .right = bank,
                                         .offset = offsetof(pin_value, d) }
    );
    emit(f, (behavior_function_instruction){ .op = BF_INVOKE, .left = 0, .node_id = 2 });
    emit(f, (behavior_function_instruction){ .op = BF_CHECK, .node_id = 3 });
    bank = emit(f, (behavior_function_instruction){ .op = BF_HOST_SLOT, .type = BF_POINTER_TYPE, .left = 3 });
    int child = emit(
        f,
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = PIN_I32,
                                         .left = bank,
                                         .offset = offsetof(pin_value, d) }
    );
    int sum = emit(
        f,
        (behavior_function_instruction){ .op = BF_ADD, .type = PIN_I32, .left = n, .right = child }
    );
    emit(f, (behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = sum, .target = 1 });
    bank = emit(f, (behavior_function_instruction){ .op = BF_HOST_SLOT, .type = BF_POINTER_TYPE, .left = 4 });
    int returned_text = emit(
        f,
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = PIN_STR,
                                         .left = bank,
                                         .offset = offsetof(pin_value, d) }
    );
    emit(
        f,
        (
            behavior_function_instruction
        ){ .op = BF_RESULT, .type = PIN_STR, .left = returned_text, .target = 2 }
    );
    int tail = emit(f, (behavior_function_instruction){ .op = BF_JUMP });
    f->instructions[branch].target = acount(f->instructions);
    emit(f, (behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = zero, .target = 1 });
    text = emit(f, (behavior_function_instruction){ .op = BF_ARGUMENT, .type = PIN_STR, .left = 1 });
    int copy = emit(
        f,
        (behavior_function_instruction){ .op = BF_COPY_STRING, .type = PIN_STR, .left = text }
    );
    emit(f, (behavior_function_instruction){ .op = BF_RESULT, .type = PIN_STR, .left = copy, .target = 2 });
    f->instructions[tail].target = acount(f->instructions);
    if (waits) {
        int duration = emit(
            f,
            (behavior_function_instruction){ .op = BF_CONSTANT,
                                             .type = PIN_F64,
                                             .value = { .type = PIN_F64, .d = .25 } }
        );
        emit(
            f,
            (
                behavior_function_instruction
            ){ .op = BF_DELAY, .type = PIN_F64, .left = duration, .node_id = 4 }
        );
    }
    emit(f, (behavior_function_instruction){ .op = BF_RETURN, .node_id = 5 });
    emit(f, (behavior_function_instruction){ .op = BF_END });
    behavior_function_constant_locals(f);
    behavior_function_reuse_accesses(f);
    behavior_function_prune_values(f);
    behavior_function_optimize(f);
    behavior_function_finalize(f);
    return f;
}

void whole_recursion_case(behavior_function *f, int mode, int n, int budget, int depth, bool cancel)
{
    behavior_function_activation activation = { 0 };
    behavior_function_activation_init(f, &activation);
    char input[] = "retained call argument";
    pin_value arguments[] = { { .type = PIN_I32, .i = n },
                              { .type = PIN_STR, .str = input } };
    pin_value results[] = { { 0 }, { .type = PIN_I32 }, { .type = PIN_STR } };
    arena storage = { 0 };
    behavior_function_context context = { .activation = &activation,
                                          .arguments = arguments,
                                          .argument_count = 2,
                                          .results = results,
                                          .budget = (uintptr_t)budget,
                                          .call_depth = (unsigned)depth };
    behavior_function_bind_storage(&context, &storage);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    int status, waits = 0;
    do {
        status = behavior_function_run(f, &context, mode == 1 || (mode == 2 && waits % 2 == 0));
        const behavior_function *active = f;
        behavior_function_context *leaf = behavior_function_active_context(&context, &active);
        if (status == FLOW_SUSPENDED_RESULT) {
            assert(f->waits && ++waits <= n + 1);
            assert(leaf->arguments[0].i == waits - 1);
            assert(!strcmp(leaf->arguments[1].str, input));
            assert(!leaf->parent || leaf->arguments[1].str != input);
            assert(leaf->call_depth == (unsigned)(depth + n - waits + 1));
            int parents = 0;
            for (behavior_function_context *p = leaf; p->parent; p = p->parent) {
                assert(++parents <= n && p->arguments != p->parent->arguments);
            }
            assert(parents == n - waits + 1);
            assert(context.wake_time == .25 * waits);
            if (cancel) {
                break;
            }
            context.now = context.wake_time;
        }
        if (status < 0) {
            assert(status == FLOW_ERR_STEP_BUDGET);
            assert(leaf->failure == (budget < 2 ? BEHAVIOR_FUNCTION_BUDGET : BEHAVIOR_FUNCTION_DEPTH));
            assert(leaf->node_id == (budget == 0 ? 1u : budget == 1 ? 3u : 2u));
            assert(behavior_function_run(f, &context, mode == 1) == status);
        }
    } while (status == FLOW_SUSPENDED_RESULT);
    assert(
        status == (cancel                ? FLOW_SUSPENDED_RESULT
                   : budget < 2 || depth ? FLOW_ERR_STEP_BUDGET
                                         : FLOW_OK)
    );
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    if (status == 0) {
        assert(results[1].i == n * (n + 1) / 2);
        assert(!strcmp(results[2].str, input) && results[2].str != input);
        assert(waits == (f->waits ? n + 1 : 0));
        assert(context.steps == (n ? 2u : 1u));
    }
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    memset(input, 'x', sizeof input - 1);
    if (!status) {
        assert(!strcmp(results[2].str, "retained call argument"));
    }
    arena_free(&storage);
}

static void check_record(const whole_managed *record)
{
    assert(record && !strcmp(record->label, "call label"));
    assert(!strcmp(record->nested.text, "call text"));
    assert(record->nested.weight == 3.25f && record->count == 42);
}

void whole_recursion_values(behavior_function *f, int mode, bool empty)
{
    char label[] = "call label", text[] = "call text", name[] = "ManagedRecord";
    whole_managed record = { label, { text, 3.25f }, 42 };
    pin_value item = { .type = PIN_STRUCT, .type_name = name, .ptr = &record };
    pin_array records = { .element_type = PIN_STRUCT,
                          .type_name = name,
                          .count = 1,
                          .capacity = 1,
                          .items = &item };
    pin_value member = { .type = PIN_NAME, .type_name = "name", .str = label };
    pin_array names = { .element_type = PIN_NAME,
                        .type_name = "name",
                        .count = 1,
                        .capacity = 1,
                        .items = &member };
    pin_value arguments[] = {
        { .type = PIN_I32, .i = 7 },
        { .type = PIN_STR, .type_name = "const char *", .str = empty ? NULL : text },
        { .type = PIN_STRUCT, .type_name = name, .ptr = empty ? NULL : &record },
        { .type = PIN_STRUCT,
          .type_name = name,
          .container = PIN_CONTAINER_ARRAY,
          .ptr = empty ? NULL : &records },
        { .type = PIN_NAME,
          .type_name = "name",
          .container = PIN_CONTAINER_SET,
          .ptr = empty ? NULL : &names }
    };
    pin_value results[6] = { 0 };
    for (int i = 0; i < 5; i++) {
        results[i + 1] = arguments[i];
        results[i + 1].ptr = NULL;
    }
    behavior_function_activation activation = { 0 };
    behavior_function_activation_init(f, &activation);
    behavior_function_context context = { .activation = &activation,
                                          .arguments = arguments,
                                          .argument_count = 5,
                                          .results = results,
                                          .budget = 100 };
    arena storage = { 0 };
    behavior_function_prepare_results(f, results, &storage);
    behavior_function_bind_storage(&context, &storage);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    int status, stops = 0;
    do {
        status = behavior_function_run(f, &context, mode == 1 || (mode == 2 && stops % 2 == 0));
        context.now = context.wake_time;
        assert(++stops < 20);
    } while (status == FLOW_SUSPENDED_RESULT);
    assert(!status && results[1].i == 28);
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    memset(label, 'x', sizeof label - 1);
    memset(text, 'y', sizeof text - 1);
    memset(name, 'z', sizeof name - 1);
    memset(&record, 0, sizeof record);
    memset(&item, 0, sizeof item);
    memset(&member, 0, sizeof member);
    memset(&records, 0, sizeof records);
    memset(&names, 0, sizeof names);
    assert(results[2].type == PIN_STR && results[3].type == PIN_STRUCT);
    assert(!strcmp(results[3].type_name, "ManagedRecord"));
    assert(results[4].container == PIN_CONTAINER_ARRAY && results[5].container == PIN_CONTAINER_SET);
    assert(
        !strcmp(results[4].type_name, "ManagedRecord") &&
        !strcmp(results[5].type_name, "name")
    );
    if (empty) {
        for (int i = 2; i < 6; i++) {
            assert(!results[i].ptr);
        }
        arena_free(&storage);
        return;
    }
    assert(!strcmp(results[2].str, "call text"));
    check_record(results[3].ptr);
    const pin_array *actual_records = results[4].ptr, *actual_names = results[5].ptr;
    assert(actual_records && actual_records->count == 1 && actual_records->element_type == PIN_STRUCT);
    assert(!strcmp(actual_records->type_name, "ManagedRecord"));
    assert(!strcmp(actual_records->items[0].type_name, "ManagedRecord"));
    check_record(actual_records->items[0].ptr);
    assert(actual_names && actual_names->count == 1 && actual_names->element_type == PIN_NAME);
    assert(!strcmp(actual_names->items[0].str, "call label"));
    arena_free(&storage);
}

static behavior_asset *runtime_asset()
{
    int kind = WHOLE_RECURSION_MUTUAL | WHOLE_RECURSION_WAIT |
               WHOLE_RECURSION_OUTER | WHOLE_RECURSION_LOOP;
    behavior_asset *asset = whole_recursion_graph(kind);
    whole_recursion_add_host(asset);
    whole_recursion_add_event(asset, whole_recursion_root(asset, kind));
    return asset;
}

static void runtime_smoke()
{
    behavior_asset *asset = runtime_asset(), *next = runtime_asset();
    behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(!behavior_instance_set_activation_pool(bi, true));
    whole_async_capture capture = { 0 };
    assert(!behavior_instance_bind_service(bi, "test.async", &capture));
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls(), stores = 0;
    flow_run *pooled = NULL;
    for (int iteration = 0; iteration < 7; iteration++) {
        if (iteration == 6) {
            assert(behavior_instance_can_update(bi, next) == INSTANCE_UPDATE_READY);
            behavior_instance_update(bi, next);
            behavior_asset_free(asset);
            asset = next;
            pooled = NULL;
        }
        behavior_instance_set_trace_enabled(bi, iteration % 2);
        pin_value input = { .type = PIN_I32, .i = 3 };
        flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ &input, 1 }, NULL);
        assert(run && (!pooled || pooled == run) && run->whole_function_executed);
        assert(behavior_instance_can_update(bi, next) == INSTANCE_UPDATE_BUSY);
        int advances = 0;
        bool cancelled = false;
        while (flow_run_status(run) == FLOW_SUSPENDED ||
               flow_run_status(run) == FLOW_PAUSED) {
            assert(++advances < 200);
            if (flow_run_status(run) == FLOW_PAUSED) {
                assert(flow_run_step(run) >= 0);
                continue;
            }
            if (!flow_run_waiting_for_host(run)) {
                assert(behavior_instance_tick(bi, flow_run_wake_time(run)) >= 0);
                continue;
            }
            int index = acount(capture.requests) - 1;
            assert(index >= 0 && capture.arguments[index] == index);
            assert(flow_run_waiting_for_host(flow_run_active_leaf(run)));
            flow_host_request *request = capture.requests[index];
            assert(flow_host_run(request) == run);
            if (iteration < 6 && iteration % 3 == 0 && index == 1) {
                flow_host_cancel(request);
                assert(flow_run_status(run) == FLOW_IDLE && !flow_host_run(request));
                assert(!flow_host_complete(request, -1, (pin_value){ 0 }));
                cancelled = true;
                break;
            }
            if (iteration % 2) {
                assert(!flow_run_pause(run));
            }
            assert(flow_host_complete(
                request,
                1,
                (pin_value){ .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 100 + index }
            ));
            assert(flow_host_resume(request) >= 0);
        }
        if (!cancelled) {
            assert(flow_run_status(run) == FLOW_COMPLETED && bi->variables[0].i == 6);
            assert(capture.calls == 4);
        }
        whole_async_clear(&capture);
        assert(!behavior_instance_release_activation(bi, run));
        if (!iteration) {
            stores = runtime_store_creations();
        }
        if (iteration > 0 && iteration < 6) {
            assert(runtime_store_creations() == stores);
        }
        pooled = run;
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    behavior_instance_destroy(bi);
    behavior_asset_free(asset);
    printf(
        "PASS: %s recursive behavior dispatch, paused host waits, "
        "cancellation, pooling and generation replacement; zero oracle calls\n",
        behavior_backend_name(behavior_default_backend())
    );
}

void whole_recursion_smoke()
{
    runtime_smoke();
    bool native = behavior_default_backend() == BEHAVIOR_SLJIT;
    for (int waits = 0; waits < 2; waits++) {
        behavior_function *f = whole_recursion_ir(waits, 4096);
        behavior_function *other = whole_recursion_ir(waits, 4096);
        if (native) {
            assert(behavior_function_compile(f) && behavior_function_compile(other));
        }
        f->calls[0].function = other;
        other->calls[0].function = f;
        for (int mode = 0; mode < (native ? 3 : 1); mode++) {
            whole_recursion_case(f, mode, 7, 10, 0, false);
            whole_recursion_case(f, mode, 7, 1, 0, false);
            whole_recursion_case(f, mode, 7, 10, 4094, false);
            if (waits) {
                whole_recursion_case(f, mode, 7, 10, 0, true);
            }
        }
        if (!waits) {
            whole_recursion_case(f, native, 2048, 10, 0, false);
        }
        behavior_function_free(f);
        behavior_function_free(other);
    }
    for (int kind = WHOLE_RECURSION_VALUES; kind < WHOLE_RECURSION_COUNT; kind++) {
        if (!(kind & WHOLE_RECURSION_VALUES)) {
            continue;
        }
        behavior_asset *asset = whole_recursion_graph(kind);
        graph *g = whole_recursion_root(asset, kind);
        behavior_function_diagnostic diagnostic;
        behavior_function *f = behavior_function_lower(asset, g, g->nodes[0], &diagnostic);
        assert(f && behavior_function_has_invocations(f));
        if (native) {
            assert(behavior_function_compile(f));
        }
        for (int mode = 0; mode < (native ? 3 : 1); mode++) {
            whole_recursion_values(f, mode, false);
            whole_recursion_values(f, mode, true);
        }
        behavior_function_free(f);
        behavior_asset_free(asset);
    }
    printf(
        "PASS: %s authored/IR recursion, retained frames/owned returns, waits "
        "and "
        "cancellation; zero oracle calls\n",
        behavior_backend_name(behavior_default_backend())
    );
}
