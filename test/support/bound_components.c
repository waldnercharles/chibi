#include "ecs/ecs.h"
#include "graph/model/registry.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/update.h"
#include "test/whole_aggregate_fixture.h"
#include "test/whole_batch_fixture.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    ecs_world *world;
    behavior_update_binding **bindings;
    behavior_update_row row;
    void *code;
} bound_case;

static void step(bound_case *test, const char *error)
{
    behavior_update_refresh(test->bindings);
    int result;
    behavior_update_row *row = &test->row;
    behavior_update_row *end = test->code
                                 ? ((behavior_update_fn)test->code)(
                                       row,
                                       row + 1,
                                       (const ecs_execution_view *)test->world,
                                       &result
                                   )
                                 : behavior_update_run_c(
                                       row,
                                       row + 1,
                                       (const ecs_execution_view *)test->world,
                                       &result,
                                       NULL
                                   );
    assert(end == row + (error == NULL));
    if (!error) {
        assert(!result);
        return;
    }
    assert(result < 0);
    flow_run *failed = behavior_update_failure(row, result);
    assert(strstr(flow_run_error(failed)->message, error));
    assert(flow_run_error(failed)->node && flow_run_error(failed)->steps > 0);
    flow_run_free(failed);
}

static void storage_changes(behavior_backend backend)
{
    behavior_asset *asset = whole_batch_fixture(true);
    graph *g = asset->graph;
    node *tick = g->nodes[1], *call = g->nodes[acount(g->nodes) - 1];
    assert(tick->reference_name && !strcmp(tick->reference_name, "Tick"));
    behavior_variable *variable = behavior_asset_add_variable(
        asset,
        "Enabled",
        PIN_BOOL,
        "bool",
        PIN_CONTAINER_SINGLE,
        NULL,
        ""
    );
    assert(variable);
    uint64_t enabled = variable->id;
    node *branch = graph_add_node(g, &NODE_Branch),
         *get = behavior_asset_add_get_node(asset, enabled);
    assert(!graph_remove_edge(g, tick, 0, call, 0));
    assert(!graph_connect_exec(g, tick, 0, branch, 0, 0));
    assert(!graph_connect_data(g, get, 0, branch, 1));
    assert(!graph_connect_exec(g, branch, 0, call, 0, 0));

    bound_case test = { .world = ecs_world_new() };
    ecs_entity owner = ecs_world_create_entity(test.world);
    pin_value handle = { .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(owner) };
    behavior_instance *bi = behavior_instance_create(asset, &handle, NULL, NULL);
    assert(!behavior_instance_set_backend(bi, backend));
    behavior_instance_set_trace_enabled(bi, false);
    behavior_instance_set_delta_seconds(bi, .125);
    assert(!behavior_instance_bind_service(bi, "ecs.world", test.world));
    assert(!behavior_instance_set_variable(bi, asset->variables[1].id, &handle));
    flow_run *begin = behavior_instance_dispatch(bi, "Begin", (flow_inputs){ 0 }, NULL);
    assert(begin && flow_run_status(begin) == FLOW_COMPLETED);
    flow_run_free(begin);
    const behavior_function *function = behavior_program_batch_event(asset->program, "Tick", &tick);
    assert(function);
    test.row = (behavior_update_row){
        .instance = bi,
        .binding = behavior_update_prepare(&test.bindings, bi, function, tick),
        .services_revision = bi->services_revision,
        .entity = handle.u64
    };
    assert(test.row.binding);
    if (backend == BEHAVIOR_SLJIT) {
        test.code = behavior_update_compile();
        assert(test.code);
    }
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    /* Resolving a missing column cannot fail an untaken branch. */
    step(&test, NULL);
    assert(bi->variables[0].d == 10.);
    pin_value on = { .type = PIN_BOOL, .b = true }, off = { .type = PIN_BOOL };
    assert(!behavior_instance_set_variable(bi, enabled, &on));
    step(&test, "entity lacks component");
    assert(bi->variables[0].d == 10.125);

    const ecs_component_desc *component = WHOLE_NODE_GetRecord.component;
    assert(!ecs_world_register_component(test.world, component, NULL));
    step(&test, "entity lacks component");
    whole_record record = { .weight = 12 };
    assert(!ecs_world_record_add(test.world, owner, component->id, &record, sizeof record));
    assert(!ecs_world_apply_commands(test.world));
    step(&test, NULL);
    assert(
        ((whole_record *)ecs_component_data(test.world, owner, component->id))->weight == 12.125
    );

    /* Grow the descriptor array and hash table after binding, then relocate
     * component storage to a high row without publishing another program. */
    for (int i = 0; i < 40; i++) {
        char name[32];
        snprintf(name, sizeof name, "bound.extra.%d", i);
        ecs_component_desc extra = *component;
        extra.id = (uint64_t)i + 1;
        extra.key = extra.name = name;
        assert(!ecs_world_register_component(test.world, &extra, NULL));
    }
    ecs_entity high = owner;
    for (int i = 0; i < 20000; i++) {
        high = ecs_world_create_entity(test.world);
    }
    assert(!ecs_world_record_add(test.world, high, component->id, NULL, 0));
    assert(!ecs_world_apply_commands(test.world));
    handle.u64 = ecs_entity_to_u64(high);
    assert(!behavior_instance_set_variable(bi, asset->variables[1].id, &handle));
    step(&test, NULL);
    assert(((whole_record *)ecs_component_data(test.world, high, component->id))->weight == 12.25);

    assert(!ecs_world_record_remove(test.world, owner, component->id, 0));
    assert(!ecs_world_apply_commands(test.world));
    assert(!behavior_instance_set_variable(bi, enabled, &off));
    step(&test, NULL);
    assert(!behavior_instance_set_variable(bi, enabled, &on));
    step(&test, "entity lacks component");
    assert(!ecs_world_record_add(test.world, owner, component->id, &record, sizeof record));
    assert(!ecs_world_apply_commands(test.world));

    type_field *fields = NULL;
    for (int i = 0; i < WHOLE_RECORD_TYPE.field_count; i++) {
        apush(fields, WHOLE_RECORD_TYPE.fields[i]);
    }
    fields[2].name = "renamed_weight";
    type_desc changed = WHOLE_RECORD_TYPE;
    changed.fields = fields;
    ecs_component_desc replacement = *component;
    replacement.type = &changed;
    replacement.layout_hash = 0;
    assert(!ecs_world_migrate_component(test.world, &replacement, NULL));
    assert(!behavior_instance_set_variable(bi, enabled, &off));
    step(&test, NULL);
    assert(!behavior_instance_set_variable(bi, enabled, &on));
    step(&test, "compiled component layout changed");
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    behavior_update_free_code(test.code);
    behavior_update_free_bindings(test.bindings);
    behavior_instance_destroy(bi);
    behavior_asset_free(asset);
    ecs_world_free(test.world);
    afree(fields);
    printf(
        "PASS: %s bound columns, untaken branches, late registration, "
        "relocation, presence and layout diagnostics\n",
        behavior_backend_name(backend)
    );
}

static void separate_views(behavior_backend backend)
{
    behavior_asset *asset = whole_batch_fixture(true);
    graph *g = asset->graphs[1];
    const ecs_component_desc *component = WHOLE_NODE_GetRecord.component;
    bool bound = false;
    for (int i = 0; i < acount(g->nodes); i++) {
        node *add = g->nodes[i], *source, *delta;
        int pin, delta_pin;
        if (add->desc != &NODE_Add || !graph_input_source(g, add, 0, &source, &pin) ||
            source->desc != &WHOLE_NODE_GetRecord) {
            continue;
        }
        assert(graph_input_source(g, add, 1, &delta, &delta_pin));
        assert(!graph_remove_edge(g, delta, delta_pin, add, 1));
        assert(!graph_set_pin_type(g, add, 0, 1, PIN_F64, "double"));
        pin_binding binding = { .kind = PIN_BINDING_COMPONENT_FIELD,
                                .component_key = (char *)component->key,
                                .field_name = "weight" };
        assert(!graph_set_pin_binding(g, add, 0, 1, &binding));
        bound = true;
    }
    assert(bound);
    bound_case test = { .world = ecs_world_new() };
    ecs_world *other = ecs_world_new();
    ecs_world *worlds[] = { test.world, other };
    ecs_entity owner = { 0 };
    for (int i = 0; i < 2; i++) {
        assert(!ecs_world_register_component(worlds[i], component, NULL));
        owner = ecs_world_create_entity(worlds[i]);
        whole_record record = { .weight = i ? 100 : 12 };
        assert(!ecs_world_record_add(worlds[i], owner, component->id, &record, sizeof record));
        assert(!ecs_world_apply_commands(worlds[i]));
    }
    pin_value handle = { .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(owner) };
    behavior_instance *bi = behavior_instance_create(asset, &handle, NULL, NULL);
    assert(!behavior_instance_set_backend(bi, backend));
    behavior_instance_set_trace_enabled(bi, false);
    eval_component_reader reader;
    ecs_eval_component_reader_init(&reader, other);
    assert(!behavior_instance_bind_service(bi, "ecs.world", test.world));
    assert(!behavior_instance_bind_service(bi, EVAL_COMPONENT_READER_SERVICE, &reader));
    assert(!behavior_instance_set_variable(bi, asset->variables[1].id, &handle));
    node *entry;
    const behavior_function *function = behavior_program_batch_event(asset->program, "Tick", &entry);
    assert(function);
    test.row = (behavior_update_row){
        .instance = bi,
        .binding = behavior_update_prepare(&test.bindings, bi, function, entry),
        .services_revision = bi->services_revision,
        .entity = handle.u64
    };
    assert(test.row.binding);
    if (backend == BEHAVIOR_SLJIT) {
        test.code = behavior_update_compile();
        assert(test.code);
    }
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    step(&test, NULL);
    assert(
        ((whole_record *)ecs_component_data(test.world, owner, component->id))->weight == 112
    );
    assert(((whole_record *)ecs_component_data(other, owner, component->id))->weight == 100);
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    behavior_update_free_code(test.code);
    behavior_update_free_bindings(test.bindings);
    behavior_instance_destroy(bi);
    behavior_asset_free(asset);
    ecs_world_free(test.world);
    ecs_world_free(other);
    printf("PASS: %s bound component ID uses independent world and reader columns\n", behavior_backend_name(backend));
}

void whole_bound_components_smoke()
{
    storage_changes(behavior_default_backend());
    separate_views(behavior_default_backend());
    if (behavior_default_backend() != BEHAVIOR_BYTECODE) {
        storage_changes(BEHAVIOR_BYTECODE);
        separate_views(BEHAVIOR_BYTECODE);
    }
}
