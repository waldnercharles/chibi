#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/compiler/behavior_function_internal.h"
#include "graph/compiler/graph_jit_sljit.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "graph/nodes/set_map_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/update.h"
#include "test/whole_array_op_fixture.h"
#include "test/whole_managed_fixture.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static pin_type element_type(int kind)
{
    return (pin_type[]){ PIN_I32, PIN_STR, PIN_NAME, PIN_STRUCT }[kind];
}
static const char *type_name(int kind)
{
    return (const char *[]){ "int", "const char *", "name", "ManagedRecord" }[kind];
}
void whole_array_op_inputs(int kind, pin_value arguments[5], arena *storage)
{
    const char *names[] = { "one", "two", "three", "four" };
    whole_managed records[4];
    pin_value items[4];
    for (int i = 0; i < 4; i++) {
        records[i] = (whole_managed){ names[i], { names[i], i + .25f }, i + 1 };
        items[i] = (pin_value){ .type = element_type(kind),
                                .type_name = type_name(kind) };
        if (kind == ARRAY_OP_INTS) {
            items[i].i = i + 1;
        } else if (kind == ARRAY_OP_STRUCTS) {
            items[i].ptr = &records[i];
        } else {
            items[i].str = names[i];
        }
    }
    pin_array arrays[] = {
        { .element_type = items[0].type,
          .type_name = items[0].type_name,
          .items = items,
          .count = 3,
          .capacity = 3 },
        { .element_type = items[0].type,
          .type_name = items[0].type_name,
          .items = items + 2,
          .count = 2,
          .capacity = 2 },
    };
    for (int i = 0; i < 2; i++) {
        pin_value value = { .type = items[0].type,
                            .type_name = items[0].type_name,
                            .container = PIN_CONTAINER_ARRAY,
                            .ptr = &arrays[i] };
        assert(!pin_value_snapshot_to_arena(storage, &arguments[i], &value));
    }
    assert(!pin_value_snapshot_to_arena(storage, &arguments[2], &items[1]));
    arguments[3] = (pin_value){ .type = PIN_I32, .i = 1 };
    arguments[4] = (pin_value){ .type = PIN_I32, .i = 2 };
}
static node *variable(behavior_asset *a, graph *g, int index, bool set)
{
    const behavior_variable *v = &a->variables[index];
    node *n = graph_add_node(g, set ? &NODE_SetVariable : &NODE_GetVariable);
    assert(!graph_set_node_reference(g, n, v->id, v->name));
    assert(!graph_set_pin_type(g, n, true, set ? 1 : 0, v->type, v->type_name));
    assert(!graph_set_pin_container(g, n, true, set ? 1 : 0, v->container));
    if (set) {
        assert(!graph_set_pin_type(g, n, false, 1, v->type, v->type_name));
        assert(!graph_set_pin_container(g, n, false, 1, v->container));
    }
    return n;
}
void whole_array_op_arguments(int kind, int operation, pin_value arguments[5], arena *storage)
{
    whole_array_op_inputs(kind, arguments, storage);
    if (operation >= ARRAY_OP_MAKE_SET) {
        arguments[0].container = arguments[1].container = PIN_CONTAINER_SET;
    }
}
behavior_asset *whole_array_op_fixture(int kind, int operation, bool waits)
{
    const char *operations[] = { "Make Array",     "Get",        "Identical",
                                 "Array Add",      "Add Unique", "Append",
                                 "Set Array Elem", "Insert",     "Remove Index",
                                 "Remove Item",    "Clear",      "Reverse",
                                 "Swap",           "Resize",     "Contains",
                                 "Find",           "Make Set",   "Set Add",
                                 "Set Contains",   "Set Length" };
    behavior_asset *a = behavior_asset_new("Array operations");
    pin_spec pins[] = {
        { .name = "Array",
          .type = element_type(kind),
          .type_name = type_name(kind),
          .container = PIN_CONTAINER_ARRAY },
        { .name = "Other",
          .type = element_type(kind),
          .type_name = type_name(kind),
          .container = PIN_CONTAINER_ARRAY },
        { .name = "Item", .type = element_type(kind), .type_name = type_name(kind) },
        { .name = "Index", .type = PIN_I32, .type_name = "int" },
        { .name = "Second", .type = PIN_I32, .type_name = "int" },
    };
    if (operation >= ARRAY_OP_MAKE_SET) {
        pins[0].container = pins[1].container = PIN_CONTAINER_SET;
    }
    for (int i = 0; i < 5; i++) {
        pins[i].default_value = (pin_value){ .type = pins[i].type,
                                             .type_name = pins[i].type_name,
                                             .container = pins[i].container };
    }
    pin_spec output = pins[0];
    output.name = "Result";
    if (operation == ARRAY_OP_GET) {
        output = pins[2];
    } else if (operation == ARRAY_OP_IDENTICAL || operation == ARRAY_OP_CONTAINS || operation == ARRAY_OP_SET_CONTAINS) {
        output = (pin_spec){ .name = "Result", .type = PIN_BOOL, .type_name = "bool" };
    } else if (operation == ARRAY_OP_FIND || operation == ARRAY_OP_SET_LENGTH) {
        output = pins[3];
    }
    assert(behavior_asset_add_variable(
        a,
        "Source",
        pins[0].type,
        pins[0].type_name,
        pins[0].container,
        NULL,
        ""
    ));
    assert(behavior_asset_add_variable(
        a,
        "Saved",
        output.type,
        output.type_name,
        output.container,
        NULL,
        ""
    ));
    assert(behavior_asset_add_variable(
        a,
        "Final",
        output.type,
        output.type_name,
        output.container,
        NULL,
        ""
    ));
    graph *g = behavior_asset_add_function(a, "Transform", false);
    node *input = g->nodes[0], *ret = g->nodes[1];
    for (int i = 0; i < 5; i++) {
        assert(graph_add_pin(g, input, true, &pins[i]) == i + 1);
    }
    assert(graph_add_pin(g, ret, false, &output) == 1);
    node *op = graph_add_node(g, registry_node_by_name(operations[operation]));
    assert(op);
    if (operation == ARRAY_OP_MAKE || operation == ARRAY_OP_MAKE_SET) {
        assert(graph_add_pin(g, op, false, &pins[2]) == 1);
        assert(graph_add_pin(g, op, false, &pins[2]) == 2);
    }
    int sources[3] = { 0, 3, 2 };
    if (operation == ARRAY_OP_MAKE || operation == ARRAY_OP_MAKE_SET) {
        sources[0] = sources[1] = sources[2] = 2;
    } else if (operation == ARRAY_OP_IDENTICAL || operation == ARRAY_OP_APPEND) {
        sources[1] = 1;
    } else if (
        operation == ARRAY_OP_ADD || operation == ARRAY_OP_UNIQUE ||
        operation == ARRAY_OP_REMOVE_ITEM || operation == ARRAY_OP_CONTAINS ||
        operation == ARRAY_OP_FIND || operation == ARRAY_OP_SET_ADD ||
        operation == ARRAY_OP_SET_CONTAINS
    ) {
        sources[1] = 2;
    } else if (operation == ARRAY_OP_SWAP) {
        sources[2] = 4;
    }
    for (int i = 0; i < op->input_count; i++) {
        assert(!graph_connect_data(g, input, sources[i] + 1, op, i));
    }
    node *save = variable(a, g, 1, true);
    assert(!graph_connect_data(g, op, 0, save, 1));
    assert(!graph_connect_data(g, save, 1, ret, 1));
    assert(!graph_connect_exec(g, input, 0, save, 0, 0));
    if (waits) {
        node *delay = graph_add_node(g, &NODE_Delay);
        assert(
            !graph_set_pin_default(g, delay, false, 1, &(pin_value){ .type = PIN_F64, .d = .25 })
        );
        assert(!graph_connect_exec(g, save, 0, delay, 0, 0));
        assert(!graph_connect_exec(g, delay, 0, ret, 0, 0));
    } else {
        assert(!graph_connect_exec(g, save, 0, ret, 0, 0));
    }
    graph *root = a->graph;
    node *event = graph_add_node(root, &NODE_Event);
    assert(!graph_set_event_name(root, event, "Run"));
    for (int i = 0; i < 5; i++) {
        assert(graph_add_pin(root, event, true, &pins[i]) == i + 1);
    }
    node *source = variable(a, root, 0, true), *get = variable(a, root, 0, false);
    node *call = behavior_asset_add_function_call_node(a, g),
         *final = variable(a, root, 2, true);
    assert(!graph_connect_data(root, event, 1, source, 1));
    assert(!graph_connect_data(root, get, 0, call, 1));
    for (int i = 2; i <= 5; i++) {
        assert(!graph_connect_data(root, event, i, call, i));
    }
    assert(!graph_connect_data(root, call, 1, final, 1));
    assert(!graph_connect_exec(root, event, 0, source, 0, 0));
    assert(!graph_connect_exec(root, source, 0, call, 0, 0));
    assert(!graph_connect_exec(root, call, 0, final, 0, 0));
    return a;
}
static behavior_asset *owned_tick_fixture(int kind, int operation, bool failure)
{
    behavior_asset *a = whole_array_op_fixture(kind, operation, false);
    assert(!behavior_asset_set_owner_type(a, "ecs.entity"));
    graph *g = a->graph;
    node *event = g->nodes[0], *source = g->nodes[1], *get = g->nodes[2],
         *call = g->nodes[3];
    arena storage = { 0 };
    pin_value args[5];
    whole_array_op_arguments(kind, operation, args, &storage);
    if (operation == ARRAY_OP_SET_ADD) {
        args[2] = ((pin_array *)args[1].ptr)->items[1];
    }
    assert(!behavior_asset_set_variable_default(a, a->variables[0].id, &args[0]));
    assert(!graph_set_event_name(g, event, "Tick"));
    assert(!graph_remove_edge(g, event, 1, source, 1));
    assert(!graph_connect_data(g, get, 0, source, 1));
    for (int i = 2; i <= 5; i++) {
        assert(!graph_remove_edge(g, event, i, call, i));
        assert(!graph_set_pin_default(g, call, false, i, &args[i - 1]));
    }
    for (int i = 5; i > 0; i--) {
        assert(!graph_remove_pin(g, event, true, i));
    }
    arena_free(&storage);
    assert(behavior_asset_add_variable(a, "Runs", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, ""));
    node *get_runs = variable(a, g, 3, false),
         *set_runs = variable(a, g, 3, true), *add = graph_add_node(g, &NODE_Add);
    for (int i = 0; i < 2; i++) {
        assert(!graph_set_pin_type(g, add, false, i, PIN_I32, "int"));
    }
    assert(!graph_set_pin_type(g, add, true, 0, PIN_I32, "int"));
    assert(!graph_set_pin_default(g, add, false, 1, &(pin_value){ .type = PIN_I32, .i = 1 }));
    assert(!graph_connect_data(g, get_runs, 0, add, 0));
    assert(!graph_connect_data(g, add, 0, set_runs, 1));
    assert(!graph_remove_edge(g, event, 0, source, 0));
    assert(!graph_connect_exec(g, event, 0, set_runs, 0, 0));
    assert(!graph_connect_exec(g, set_runs, 0, source, 0, 0));
    if (failure) {
        graph *child = a->graphs[1];
        assert(!graph_remove_edge(child, child->nodes[3], 0, child->nodes[1], 0));
    }
    return a;
}
behavior_asset *whole_owned_tick_fixture(int kind, bool failure)
{
    return owned_tick_fixture(kind, ARRAY_OP_APPEND, failure);
}
static void owned_tick_arrays()
{
    ecs_world *ecs = ecs_world_new();
    ecs_entity entity = ecs_world_create_entity(ecs);
    bool native = behavior_default_backend() == BEHAVIOR_SLJIT;
    void *code = native ? behavior_update_compile() : NULL;
    assert(!native || code);
    for (int variant = 0; variant < ARRAY_OP_KINDS * 2; variant++) {
        bool set = variant >= ARRAY_OP_KINDS;
        behavior_asset *a = owned_tick_fixture(
            variant % ARRAY_OP_KINDS,
            set ? ARRAY_OP_SET_ADD : ARRAY_OP_APPEND,
            false
        );
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, false);
        behavior_batch *batch = NULL;
        flow_run *failed;
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls(),
                 stores = runtime_store_creations();
        uint64_t allocations = 0;
        for (int i = 0; i < 16; i++) {
            if (i == 8) {
                allocations = arena_block_allocations();
            }
            assert(behavior_instance_dispatch_batch(bi, "Tick", &batch, &failed) && !failed);
        }
        assert(allocations == arena_block_allocations());
        node *entry;
        const behavior_function
            *f = behavior_program_batch_event(behavior_program_cached(a), "Tick", &entry);
        behavior_update_binding **bindings = NULL;
        behavior_update_row row = behavior_update_row_make(
            bi,
            behavior_update_prepare(&bindings, bi, f, entry),
            ecs_entity_to_u64(entity),
            0
        );
        assert(row.binding);
        for (int i = 0; i < 16; i++) {
            if (i == 8) {
                allocations = arena_block_allocations();
            }
            int result;
            behavior_update_row *stopped = native
                                             ? ((behavior_update_fn)code)(
                                                   &row,
                                                   &row + 1,
                                                   (const ecs_execution_view *)ecs,
                                                   &result
                                               )
                                             : behavior_update_run_c(
                                                   &row,
                                                   &row + 1,
                                                   (const ecs_execution_view *)ecs,
                                                   &result,
                                                   NULL
                                               );
            assert(stopped == &row + 1 && result == FLOW_OK);
        }
        assert(allocations == arena_block_allocations());
        assert(
            bi->execution.variables[3].i == 32 &&
            ((pin_array *)bi->execution.variables[2].ptr)->count == (set ? 4 : 5)
        );
        assert(
            bi->execution.variables[2].container ==
            (set ? PIN_CONTAINER_SET : PIN_CONTAINER_ARRAY)
        );
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        assert(stores == runtime_store_creations() && !acount(bi->activations));
        behavior_update_free_bindings(bindings);
        behavior_batch_free(batch);
        behavior_instance_destroy(bi);
        behavior_asset_free(a);
    }
    behavior_update_free_code(code);
    ecs_world_free(ecs);
    puts("PASS: owned arrays, sets and managed records in Tick batches and prepared updates; no backing-block growth, activation stores or oracle calls");
}
static void owned_tick()
{
    behavior_asset *a = behavior_asset_new("Owned Tick values");
    assert(behavior_asset_add_variable(a, "Count", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, ""));
    graph *g = a->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(!graph_set_event_name(g, event, "Tick"));
    node *make = graph_add_node(g, registry_node_by_name("Make Array"));
    assert(!graph_set_pin_type(g, make, false, 0, PIN_I32, "int"));
    assert(!graph_set_pin_default(g, make, false, 0, &(pin_value){ .type = PIN_I32, .i = 7 }));
    node *length = graph_add_node(g, registry_node_by_name("Length"));
    node *set = variable(a, g, 0, true);
    assert(!graph_connect_data(g, make, 0, length, 0));
    assert(!graph_connect_data(g, length, 0, set, 1));
    assert(!graph_connect_exec(g, event, 0, set, 0, 0));
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(bi, false);
    behavior_program *program = behavior_program_publish(a);
    const behavior_function
        *f = behavior_program_function(behavior_program_graph(program, g), event);
    assert(f && behavior_function_batchable(f));
    behavior_batch *batch = NULL;
    flow_run *failed = NULL;
    assert(behavior_instance_dispatch_batch(bi, "Tick", &batch, &failed));
    assert(batch && !failed && bi->execution.variables[0].i == 1);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    flow_run *run = behavior_instance_dispatch(bi, "Tick", (flow_inputs){ 0 }, NULL);
    assert(run && run->whole_function_executed && flow_run_status(run) == FLOW_COMPLETED);
    assert(bi->execution.variables[0].i == 1);
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    behavior_batch_free(batch);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
}
static void set_semantics()
{
    for (int kind = 0; kind < ARRAY_OP_KINDS; kind++) {
        arena input = { 0 }, output = { 0 };
        pin_value args[5];
        whole_array_op_arguments(kind, ARRAY_OP_MAKE_SET, args, &input);
        const pin_array *source = args[0].ptr, *other = args[1].ptr;
        pin_value duplicate;
        assert(!pin_value_snapshot_to_arena(&input, &duplicate, &args[2]));
        pin_value items[] = { args[2], duplicate, source->items[0] };
        pin_value made = registry_node_by_name("Make Set")->fn(items, 3, &output);
        const pin_array *set = made.ptr;
        assert(made.container == PIN_CONTAINER_SET && set && set->count == 2);
        assert(pin_value_equal(&set->items[0], &items[0]));
        assert(pin_value_equal(&set->items[1], &items[2]));
        pin_value operands[] = { args[0], other->items[1] };
        pin_value added = registry_node_by_name("Set Add")->fn(operands, 2, &output);
        const pin_array *extended = added.ptr;
        assert(extended && extended->count == 4 && source->count == 3);
        assert(pin_value_equal(&extended->items[3], &operands[1]));
        if (kind == ARRAY_OP_STRUCTS) {
            const whole_managed *record = extended->items[3].ptr;
            assert(record->count == 4 && !strcmp(record->label, "four"));
            assert(!strcmp(record->nested.text, "four") && record->nested.weight == 3.25f);
        } else if (kind == ARRAY_OP_INTS) {
            assert(extended->items[3].i == 4);
        } else {
            assert(!strcmp(extended->items[3].str, "four"));
        }
        assert(!strcmp(added.type_name, extended->type_name));
        pin_value remove_present[] = { args[0], args[2] };
        pin_value removed = registry_node_by_name("Set Remove")->fn(remove_present, 2, &output);
        const pin_array *shortened = removed.ptr;
        assert(removed.container == PIN_CONTAINER_SET && shortened && shortened->count == 2);
        assert(pin_value_equal(&shortened->items[0], &source->items[0]));
        assert(pin_value_equal(&shortened->items[1], &source->items[2]));
        assert(source->count == 3 && pin_value_equal(&source->items[1], &args[2]));
        pin_value remove_absent[] = { args[0], other->items[1] };
        pin_value unchanged = registry_node_by_name("Set Remove")->fn(remove_absent, 2, &output);
        const pin_array *same = unchanged.ptr;
        assert(same && same->count == 3);
        for (int i = 0; i < same->count; i++) {
            assert(pin_value_equal(&same->items[i], &source->items[i]));
        }
        arena_free(&input);
        arena_free(&output);
    }
}

static void owned_set_semantics()
{
    enum
    {
        ITEMS = 256
    };
    arena storage = { 0 }, snapshots = { 0 };
    pin_array *set = pin_array_snapshot_allocate(&storage, PIN_I32, "int", 0);
    for (int i = 0; i < ITEMS; i++) {
        pin_value item = { .type = PIN_I32, .type_name = "int", .i = i };
        assert(pin_set_add_owned(&storage, &set, &item) == 1);
    }
    assert(set->count == ITEMS && set->capacity >= ITEMS && set->buckets);
    pin_array *snapshot = pin_array_snapshot_to_arena(&snapshots, set);
    pin_array_index(&snapshots, snapshot);
    for (int i = 0; i < ITEMS; i++) {
        pin_value item = { .type = PIN_I32, .type_name = "int", .i = i };
        assert(pin_set_add_owned(&storage, &set, &item) == 0);
        assert(pin_array_find(set, &item) == i);
    }
    pin_value next = { .type = PIN_I32, .type_name = "int", .i = ITEMS };
    assert(pin_set_add_owned(&storage, &set, &next) == 1);
    assert(set->count == ITEMS + 1 && set->items[ITEMS].i == ITEMS);
    assert(snapshot->count == ITEMS && snapshot->items[ITEMS - 1].i == ITEMS - 1);

    char text[16] = "owned";
    pin_array *strings = pin_array_snapshot_allocate(&storage, PIN_STR, "string", 0);
    pin_value string = { .type = PIN_STR, .type_name = "string", .str = text };
    assert(pin_set_add_owned(&storage, &strings, &string) == 1);
    strcpy(text, "mutated");
    assert(!strcmp(strings->items[0].str, "owned"));
    pin_array *string_snapshot = pin_array_snapshot_to_arena(&snapshots, strings);
    pin_value same = { .type = PIN_STR, .type_name = "string", .str = "owned" };
    assert(pin_set_add_owned(&storage, &strings, &same) == 0);
    assert(strings->count == 1 && string_snapshot->count == 1);

    pin_array *entities = pin_array_snapshot_allocate(&storage, PIN_HANDLE, "ecs.entity", 0);
    pin_value wrong_entity = { .type = PIN_HANDLE, .type_name = "other", .u64 = 17 };
    pin_value entity = { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 17 };
    assert(pin_set_add_owned(&storage, &entities, &wrong_entity) < 0);
    assert(!entities->count && pin_set_add_owned(&storage, &entities, &entity) == 1);
    assert(pin_array_find(entities, &entity) == 0);
    arena_free(&snapshots);
    arena_free(&storage);
}

static void entity_set_semantics()
{
    arena output = { 0 };
    pin_value first = { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 17 };
    pin_value second = { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 29 };
    pin_value third = { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 41 };
    pin_value fourth = { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 53 };
    pin_value values[] = { first, second, first };
    pin_value made = registry_node_by_name("Make Set")->fn(values, 3, &output);
    const pin_array *set = made.ptr;
    assert(
        made.type == PIN_HANDLE && made.container == PIN_CONTAINER_SET && set &&
        set->count == 2
    );
    assert(pin_value_equal(&set->items[0], &first) && pin_value_equal(&set->items[1], &second));

    pin_value contains_args[] = { made, second };
    assert(registry_node_by_name("Set Contains")->fn(contains_args, 2, &output).b);
    contains_args[1] = fourth;
    assert(!registry_node_by_name("Set Contains")->fn(contains_args, 2, &output).b);

    pin_value duplicate_args[] = { made, first };
    pin_value duplicate = registry_node_by_name("Set Add")->fn(duplicate_args, 2, &output);
    assert(((const pin_array *)duplicate.ptr)->count == 2);
    pin_value add_args[] = { made, third };
    pin_value added = registry_node_by_name("Set Add")->fn(add_args, 2, &output);
    const pin_array *extended = added.ptr;
    assert(extended && extended->count == 3 && set->count == 2);
    assert(pin_value_equal(&extended->items[2], &third));

    pin_value remove_args[] = { added, second };
    pin_value removed = registry_node_by_name("Set Remove")->fn(remove_args, 2, &output);
    const pin_array *shortened = removed.ptr;
    assert(shortened && shortened->count == 2 && set->count == 2 && extended->count == 3);
    assert(pin_value_equal(&shortened->items[0], &first));
    assert(pin_value_equal(&shortened->items[1], &third));

    remove_args[1] = fourth;
    pin_value unchanged = registry_node_by_name("Set Remove")->fn(remove_args, 2, &output);
    const pin_array *same = unchanged.ptr;
    assert(same && same->count == 3);
    for (int i = 0; i < same->count; i++) {
        assert(pin_value_equal(&same->items[i], &extended->items[i]));
    }
    arena_free(&output);
}

static void set_remove_flow()
{
    pin_value values[] = {
        { .type = PIN_I32, .type_name = "int", .i = 1 },
        { .type = PIN_I32, .type_name = "int", .i = 2 },
        { .type = PIN_I32, .type_name = "int", .i = 3 },
    };
    pin_array initial = { .element_type = PIN_I32,
                          .type_name = "int",
                          .count = 3,
                          .capacity = 3,
                          .items = values };
    pin_value default_value = { .type = PIN_I32,
                                .type_name = "int",
                                .container = PIN_CONTAINER_SET,
                                .ptr = &initial };
    behavior_asset *asset = behavior_asset_new("Set Remove flow");
    assert(
        behavior_asset_add_variable(asset, "Seen", PIN_I32, "int", PIN_CONTAINER_SET, &default_value, "")
    );
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(!graph_set_event_name(g, event, "Tick"));
    node *get = variable(asset, g, 0, false);
    node *remove = graph_add_node(g, registry_node_by_name("Set Remove"));
    assert(remove);
    assert(!graph_set_pin_type(g, remove, false, 1, PIN_I32, "int"));
    assert(!graph_set_pin_default(g, remove, false, 1, &(pin_value){ .type = PIN_I32, .i = 2 }));
    node *set = variable(asset, g, 0, true);
    assert(!graph_connect_data(g, get, 0, remove, 0));
    assert(!graph_connect_data(g, remove, 0, set, 1));
    assert(!graph_connect_exec(g, event, 0, set, 0, 0));

    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, false);
    flow_run *run = behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, NULL);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    const pin_array *result = instance->execution.variables[0].ptr;
    assert(
        instance->execution.variables[0].container == PIN_CONTAINER_SET &&
        result && result->count == 2
    );
    assert(result->items[0].i == 1 && result->items[1].i == 3);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
}

static void owned_set_mutation()
{
    enum
    {
        INITIAL = 63
    };
    pin_value initial_items[INITIAL];
    for (int i = 0; i < INITIAL; i++) {
        initial_items[i] = (pin_value){ .type = PIN_I32, .type_name = "int", .i = i };
    }
    pin_array initial = { .element_type = PIN_I32,
                          .type_name = "int",
                          .count = INITIAL,
                          .capacity = INITIAL,
                          .items = initial_items };
    pin_value default_value = { .type = PIN_I32,
                                .type_name = "int",
                                .container = PIN_CONTAINER_SET,
                                .ptr = &initial };
    behavior_asset *asset = behavior_asset_new("Owned set append");
    assert(
        behavior_asset_add_variable(asset, "Seen", PIN_I32, "int", PIN_CONTAINER_SET, &default_value, "")
    );
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(!graph_set_event_name(g, event, "Tick"));
    node *get = variable(asset, g, 0, false);
    node *add = graph_add_node(g, registry_node_by_name("Set Add"));
    assert(add);
    assert(!graph_set_pin_type(g, add, false, 0, PIN_I32, "int"));
    assert(!graph_set_pin_container(g, add, false, 0, PIN_CONTAINER_SET));
    assert(!graph_set_pin_type(g, add, false, 1, PIN_I32, "int"));
    assert(
        !graph_set_pin_default(g, add, false, 1, &(pin_value){ .type = PIN_I32, .i = INITIAL })
    );
    assert(!graph_set_pin_type(g, add, true, 0, PIN_I32, "int"));
    assert(!graph_set_pin_container(g, add, true, 0, PIN_CONTAINER_SET));
    node *set = variable(asset, g, 0, true);
    assert(!graph_connect_data(g, get, 0, add, 0));
    assert(!graph_connect_data(g, add, 0, set, 1));
    assert(!graph_connect_exec(g, event, 0, set, 0, 0));

    compile_report report;
    compile_report_init(&report);
    behavior_asset_query_diagnostics(asset, &report);
    bool owned_append_note = false;
    for (int i = 0; i < acount(report.items); i++) {
        const compile_diagnostic *diagnostic = &report.items[i];
        if (diagnostic->node_id == add->id && diagnostic->rule == CMPL_RULE_CONTAINER) {
            owned_append_note = diagnostic->severity == CMPL_NOTE &&
                                strstr(diagnostic->message, "geometric capacity");
        }
    }
    assert(owned_append_note && report.warning_count == 0);
    compile_report_free(&report);

    behavior_program *program = behavior_program_publish(asset);
    const behavior_function
        *function = behavior_program_function(behavior_program_graph(program, g), event);
    assert(function);
    bool optimized = false;
    for (int i = 0; i < acount(function->instructions); i++) {
        const behavior_function_instruction *instruction = &function->instructions[i];
        optimized |= instruction->op == BF_STORE_VALUE &&
                     instruction->offset == BF_STORE_VALUE_SET_ADD;
    }
    assert(optimized);

    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, false);
    arena snapshot_storage = { 0 };
    pin_value snapshot;
    assert(
        !behavior_instance_copy_variable(instance, asset->variables[0].id, &snapshot_storage, &snapshot)
    );
    flow_run *run = behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, NULL);
    assert(run && run->whole_function_executed && flow_run_status(run) == FLOW_COMPLETED);
    const pin_array *current = instance->execution.variables[0].ptr;
    const pin_array *old = snapshot.ptr;
    assert(current && current->count == INITIAL + 1 && old && old->count == INITIAL);
    for (int i = 0; i < INITIAL; i++) {
        assert(current->items[i].i == i && old->items[i].i == i);
    }
    assert(current->items[INITIAL].i == INITIAL);
    run = behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, NULL);
    assert(run && run->whole_function_executed && flow_run_status(run) == FLOW_COMPLETED);
    assert(((const pin_array *)instance->execution.variables[0].ptr)->count == INITIAL + 1);
    arena_free(&snapshot_storage);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
}

static void owned_struct_set_mutation()
{
    whole_managed record = { "item", { "nested", 3.25f }, 7 };
    pin_array empty = { .element_type = PIN_STRUCT, .type_name = "ManagedRecord" };
    pin_value default_value = { .type = PIN_STRUCT,
                                .type_name = "ManagedRecord",
                                .container = PIN_CONTAINER_SET,
                                .ptr = &empty };
    behavior_asset *asset = behavior_asset_new("Owned struct set append");
    assert(
        behavior_asset_add_variable(asset, "Records", PIN_STRUCT, "ManagedRecord", PIN_CONTAINER_SET, &default_value, "")
    );
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(!graph_set_event_name(g, event, "Tick"));
    node *get = variable(asset, g, 0, false);
    node *add = graph_add_node(g, &NODE_SetAdd);
    assert(add);
    assert(!graph_set_pin_type(g, add, false, 0, PIN_STRUCT, "ManagedRecord"));
    assert(!graph_set_pin_container(g, add, false, 0, PIN_CONTAINER_SET));
    assert(!graph_set_pin_type(g, add, false, 1, PIN_STRUCT, "ManagedRecord"));
    assert(!graph_set_pin_default(
        g,
        add,
        false,
        1,
        &(pin_value){
            .type = PIN_STRUCT,
            .type_name = "ManagedRecord",
            .ptr = &record,
        }
    ));
    assert(!graph_set_pin_type(g, add, true, 0, PIN_STRUCT, "ManagedRecord"));
    assert(!graph_set_pin_container(g, add, true, 0, PIN_CONTAINER_SET));
    node *set = variable(asset, g, 0, true);
    assert(!graph_connect_data(g, get, 0, add, 0));
    assert(!graph_connect_data(g, add, 0, set, 1));
    assert(!graph_connect_exec(g, event, 0, set, 0, 0));

    behavior_program *program = behavior_program_publish(asset);
    const behavior_function
        *function = behavior_program_function(behavior_program_graph(program, g), event);
    assert(function);
    bool optimized = false;
    for (int i = 0; i < acount(function->instructions); i++) {
        const behavior_function_instruction *instruction = &function->instructions[i];
        optimized |= instruction->op == BF_STORE_VALUE &&
                     instruction->offset == BF_STORE_VALUE_SET_ADD;
    }
    assert(!optimized);

    const behavior_backend backends[] = { BEHAVIOR_SLJIT, BEHAVIOR_BYTECODE };
    for (int backend = 0; backend < 2; backend++) {
        if (backends[backend] == BEHAVIOR_SLJIT && !graph_sljit_available()) {
            continue;
        }
        behavior_backend previous = behavior_debug_backend(backends[backend]);
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(instance, false);
        flow_run *run = behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, NULL);
        assert(run && run->whole_function_executed && flow_run_status(run) == FLOW_COMPLETED);
        const pin_array *records = instance->execution.variables[0].ptr;
        assert(records && records->count == 1 && records->items[0].type == PIN_STRUCT);
        const whole_managed *actual = records->items[0].ptr;
        assert(
            actual && !strcmp(actual->label, "item") &&
            !strcmp(actual->nested.text, "nested")
        );
        assert(actual->nested.weight == 3.25f && actual->count == 7);
        behavior_instance_destroy(instance);
        behavior_debug_backend(previous);
    }
    behavior_asset_free(asset);
}

static void disabled_owned_set_nodes_use_general_lowering(bool disable_source)
{
    pin_value items[] = { { .type = PIN_I32, .type_name = "int", .i = 101 } };
    pin_array initial = { .element_type = PIN_I32,
                          .type_name = "int",
                          .count = 1,
                          .capacity = 1,
                          .items = items };
    pin_value default_value = { .type = PIN_I32,
                                .type_name = "int",
                                .container = PIN_CONTAINER_SET,
                                .ptr = &initial };
    behavior_asset *asset = behavior_asset_new("Disabled owned set append");
    assert(
        behavior_asset_add_variable(asset, "Seen", PIN_I32, "int", PIN_CONTAINER_SET, &default_value, "")
    );
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(!graph_set_event_name(g, event, "Tick"));
    node *get = variable(asset, g, 0, false);
    node *add = graph_add_node(g, &NODE_SetAdd);
    assert(add);
    assert(!graph_set_pin_type(g, add, false, 0, PIN_I32, "int"));
    assert(!graph_set_pin_container(g, add, false, 0, PIN_CONTAINER_SET));
    assert(!graph_set_pin_type(g, add, false, 1, PIN_I32, "int"));
    assert(!graph_set_pin_default(g, add, false, 1, &(pin_value){ .type = PIN_I32, .i = 5 }));
    assert(!graph_set_pin_type(g, add, true, 0, PIN_I32, "int"));
    assert(!graph_set_pin_container(g, add, true, 0, PIN_CONTAINER_SET));
    node *set = variable(asset, g, 0, true);
    assert(!graph_connect_data(g, get, 0, add, 0));
    assert(!graph_connect_data(g, add, 0, set, 1));
    assert(!graph_connect_exec(g, event, 0, set, 0, 0));
    assert(!graph_set_node_disabled(g, disable_source ? get : add, true));
    assert(!behavior_function_owned_set_add_candidate(asset, g, add));

    behavior_program *program = behavior_program_publish(asset);
    const behavior_function
        *function = behavior_program_function(behavior_program_graph(program, g), event);
    if (function) {
        for (int i = 0; i < acount(function->instructions); i++) {
            const behavior_function_instruction *instruction = &function->instructions[i];
            assert(instruction->op != BF_STORE_VALUE || instruction->offset != BF_STORE_VALUE_SET_ADD);
        }
    }
    const behavior_backend backends[] = { BEHAVIOR_SLJIT, BEHAVIOR_BYTECODE };
    for (int backend = 0; backend < 2; backend++) {
        if (backends[backend] == BEHAVIOR_SLJIT && !graph_sljit_available()) {
            continue;
        }
        behavior_backend previous = behavior_debug_backend(backends[backend]);
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(instance, false);
        flow_run *run = behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, NULL);
        assert(run && flow_run_status(run) == FLOW_COMPLETED);
        const pin_array *after = instance->execution.variables[0].ptr;
        assert(
            after && after->count == 2 && after->items[0].i == 101 &&
            after->items[1].i == 5
        );
        behavior_instance_destroy(instance);
        behavior_debug_backend(previous);
    }
    behavior_asset_free(asset);
}

static void owned_entity_set_mutation()
{
    pin_array empty = { .element_type = PIN_HANDLE, .type_name = "ecs.entity" };
    pin_value default_value = { .type = PIN_HANDLE,
                                .type_name = "ecs.entity",
                                .container = PIN_CONTAINER_SET,
                                .ptr = &empty };
    behavior_asset *asset = behavior_asset_new("Owned entity set append");
    assert(
        behavior_asset_add_variable(asset, "Contacts", PIN_HANDLE, "ecs.entity", PIN_CONTAINER_SET, &default_value, "")
    );
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(!graph_set_event_name(g, event, "Tick"));
    node *get = variable(asset, g, 0, false);
    node *add = graph_add_node(g, &NODE_SetAdd);
    assert(add);
    assert(!graph_set_pin_type(g, add, false, 0, PIN_HANDLE, "ecs.entity"));
    assert(!graph_set_pin_container(g, add, false, 0, PIN_CONTAINER_SET));
    assert(!graph_set_pin_type(g, add, false, 1, PIN_HANDLE, "ecs.entity"));
    assert(!graph_set_pin_default(
        g,
        add,
        false,
        1,
        &(pin_value){ .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 41 }
    ));
    assert(!graph_set_pin_type(g, add, true, 0, PIN_HANDLE, "ecs.entity"));
    assert(!graph_set_pin_container(g, add, true, 0, PIN_CONTAINER_SET));
    node *set = variable(asset, g, 0, true);
    assert(!graph_connect_data(g, get, 0, add, 0));
    assert(!graph_connect_data(g, add, 0, set, 1));
    assert(!graph_connect_exec(g, event, 0, set, 0, 0));

    behavior_program *program = behavior_program_publish(asset);
    const behavior_function
        *function = behavior_program_function(behavior_program_graph(program, g), event);
    assert(function);
    bool optimized = false;
    for (int i = 0; i < acount(function->instructions); i++) {
        const behavior_function_instruction *instruction = &function->instructions[i];
        optimized |= instruction->op == BF_STORE_VALUE &&
                     instruction->offset == BF_STORE_VALUE_SET_ADD;
    }
    assert(optimized);

    const behavior_backend backends[] = { BEHAVIOR_SLJIT, BEHAVIOR_BYTECODE };
    for (int backend = 0; backend < 2; backend++) {
        if (backends[backend] == BEHAVIOR_SLJIT && !graph_sljit_available()) {
            continue;
        }
        behavior_backend previous = behavior_debug_backend(backends[backend]);
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(instance, false);
        arena snapshot_storage = { 0 };
        pin_value snapshot;
        assert(
            !behavior_instance_copy_variable(instance, asset->variables[0].id, &snapshot_storage, &snapshot)
        );
        for (int i = 0; i < 2; i++) {
            flow_run *run = behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, NULL);
            assert(run && run->whole_function_executed && flow_run_status(run) == FLOW_COMPLETED);
        }
        const pin_array *contacts = instance->execution.variables[0].ptr;
        assert(contacts && contacts->count == 1 && contacts->items[0].u64 == 41);
        assert(!strcmp(contacts->items[0].type_name, "ecs.entity"));
        const pin_array *old = snapshot.ptr;
        assert(old && old->count == 0);
        pin_value wrong_type = { .type = PIN_HANDLE, .type_name = "ecs.other", .u64 = 41 };
        assert(
            behavior_instance_add_set_variable(instance, asset->variables[0].id, &wrong_type) < 0
        );
        assert(((const pin_array *)instance->execution.variables[0].ptr)->count == 1);

        arena_free(&snapshot_storage);
        behavior_instance_destroy(instance);
        behavior_debug_backend(previous);
    }
    behavior_asset_free(asset);
}

void whole_array_op_smoke()
{
    extern void test_value_index();
    test_value_index();
    set_semantics();
    owned_set_semantics();
    entity_set_semantics();
    set_remove_flow();
    owned_set_mutation();
    owned_struct_set_mutation();
    disabled_owned_set_nodes_use_general_lowering(false);
    disabled_owned_set_nodes_use_general_lowering(true);
    owned_entity_set_mutation();
    owned_tick();
    owned_tick_arrays();
    for (int operation = 0; operation < ARRAY_OP_COUNT; operation++) {
        behavior_asset *a = whole_array_op_fixture(ARRAY_OP_STRINGS, operation, true);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, false);
        arena input = { 0 };
        pin_value args[5];
        whole_array_op_arguments(ARRAY_OP_STRINGS, operation, args, &input);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ args, 5 }, NULL);
        assert(run && run->whole_function_executed && flow_run_status(run) == FLOW_SUSPENDED);
        arena_free(&input);
        assert(!flow_run_tick_at(run, .25));
        assert(flow_run_status(run) == FLOW_COMPLETED);
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        behavior_instance_destroy(bi);
        behavior_asset_free(a);
    }
    puts("PASS: whole array/set construction, mutation, membership, length and direct Get through nested waits; zero oracle calls");
}
