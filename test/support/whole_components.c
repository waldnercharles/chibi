#include "ecs/ecs.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "support/array.h"
#include "test/support/report.h"
#include "test/whole_aggregate_fixture.h"

#include <string.h>

static const ecs_component_desc record_component = {
    .id = UINT64_C(0xbadc0ffee),
    .name = "WholeRecord",
    .key = "test.WholeRecord",
    .element_size = sizeof(whole_record),
    .type = &WHOLE_RECORD_TYPE,
};
static const char *read_record(const eval_context *ctx, arena *a, pin_value *out)
{
    ecs_world *world = eval_context_service(ctx, "ecs.world");
    if (!world || !ctx->instance) {
        return "component Get requires an entity and ecs.world";
    }
    const void *value = ecs_component_data_const(
        world,
        ecs_entity_from_u64(behavior_instance_owner(ctx->instance).u64),
        record_component.id
    );
    if (!value) {
        return "entity lacks component";
    }
    *out = (pin_value){ .type = PIN_STRUCT,
                        .type_name = "WholeRecord",
                        .ptr = arena_alloc(a, sizeof(whole_record)) };
    memcpy(out->ptr, value, sizeof(whole_record));
    return NULL;
}
static int set_record(node *, flow_context *ctx)
{
    ecs_world *world = flow_context_service(ctx, "ecs.world");
    if (!world) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "component field Set requires ecs.world");
    }
    pin_value target, value;
    int result = flow_context_eval_data(ctx, 2, &target);
    if (result) {
        return result;
    }
    ecs_entity entity = ecs_entity_from_u64(target.u64);
    if (!ecs_entity_is_alive(world, entity)) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "component field Set requires a live Target");
    }
    result = flow_context_eval_data(ctx, 1, &value);
    if (result) {
        return result;
    }
    whole_record *record = ecs_component_data(world, entity, record_component.id);
    if (!record) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Target lacks component");
    }
    record->weight = value.d;
    pin_value *out = flow_context_output_value(ctx, 1);
    *out = (pin_value){ .type = PIN_STRUCT,
                        .type_name = "WholeRecord",
                        .ptr = flow_context_scratch_alloc(ctx, sizeof *record) };
    memcpy(out->ptr, record, sizeof *record);
    return flow_context_continue(ctx, 0);
}
static pin_spec get_outputs[] = {
    { .name = "Record", .type = PIN_STRUCT, .type_name = "WholeRecord" }
};
static pin_spec set_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Weight", PIN_F64, "double" },
    { .name = "Target", .type = PIN_HANDLE, .type_name = "ecs.entity", .flags = PIN_FLAG_DEFAULT_SELF },
};
static pin_spec set_outputs[] = {
    { "Then", PIN_FLOW, NULL, PIN_EXEC },
    { "Record", PIN_STRUCT, "WholeRecord" },
};
node_desc WHOLE_NODE_GetRecord = { .name = "Get WholeRecord",
                                   .outputs = get_outputs,
                                   .output_count = 1,
                                   .pure_kind = NODE_PURE_READ,
                                   .read = read_record,
                                   .type = &WHOLE_RECORD_TYPE,
                                   .component_kind = NODE_COMPONENT_GET_SELF,
                                   .component = &record_component };
node_desc WHOLE_NODE_SetWeight = { .name = "Set WholeRecord Weight",
                                   .inputs = set_inputs,
                                   .input_count = 3,
                                   .outputs = set_outputs,
                                   .output_count = 2,
                                   .flow_kind = NODE_FLOW_CUSTOM,
                                   .flow = set_record,
                                   .type = &WHOLE_RECORD_TYPE,
                                   .component_kind = NODE_COMPONENT_SET_FIELD,
                                   .component = &record_component,
                                   .component_field = 2 };

void whole_component_smoke()
{
    behavior_asset *a = behavior_asset_new("Portable components");
    assert(!behavior_asset_set_owner_type(a, "ecs.entity"));
    graph *g = behavior_asset_add_function(a, "Update", 0);
    node *entry = g->nodes[0], *ret = g->nodes[1];
    node *set = graph_add_node(g, &WHOLE_NODE_SetWeight);
    node *get = graph_add_node(g, &WHOLE_NODE_GetRecord);
    assert(!graph_expose_output_fields(g, get, 0));
    int weight = -1;
    for (int i = 1; i < get->output_count; i++) {
        if (!strcmp(get->output_pins[i].field_name, "weight")) {
            weight = i;
        }
    }
    assert(weight > 0);
    assert(graph_add_pin(g, entry, 1, &set_inputs[2]) == 1);
    assert(graph_add_pin(g, ret, 0, &get_outputs[0]) == 1);
    assert(!graph_connect_data(g, get, weight, set, 1));
    assert(!graph_connect_data(g, entry, 1, set, 2));
    assert(!graph_connect_data(g, set, 1, ret, 1));
    assert(!graph_connect_exec(g, entry, 0, set, 0, 0));
    assert(!graph_connect_exec(g, set, 0, ret, 0, 0));
    ecs_world *world = ecs_world_new();
    assert(!ecs_world_register_component(world, &record_component, NULL));
    ecs_entity owner = ecs_world_create_entity(world),
               other = ecs_world_create_entity(world);
    assert(!ecs_world_record_add(world, owner, record_component.id, NULL, 0));
    assert(!ecs_world_record_add(world, other, record_component.id, NULL, 0));
    assert(!ecs_world_apply_commands(world));
    whole_record *source = ecs_component_data(world, owner, record_component.id);
    whole_record *target = ecs_component_data(world, other, record_component.id);
    *source = (whole_record){ .weight = 3.25,
                              .position = { 1, -2 },
                              .stamp = INT64_MIN + 13 };
    *target = (whole_record){ .position = { -9, 7 }, .stamp = INT64_MAX - 4, .tag = 251 };
    pin_value handle = { .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(owner) };
    behavior_instance *bi = behavior_instance_create(a, &handle, NULL, NULL);
    behavior_instance_set_trace_enabled(bi, 0);
    assert(!behavior_instance_bind_service(bi, "ecs.world", world));
    handle.u64 = ecs_entity_to_u64(other);
    flow_options options = { .instance = bi };
    flow_error error = { 0 };
    uint64_t oracle = eval_oracle_calls();
    flow_run *run = flow_run_start(g, entry, (flow_inputs){ &handle, 1 }, &options, &error);
    int status = run ? flow_run_resume(run) : error.code;
    assert(run && !status && run->whole_function_executed);
    assert(oracle == eval_oracle_calls());
    assert(target->weight == source->weight && target->stamp == INT64_MAX - 4);
    assert(target->position.x == -9 && target->position.y == 7 && target->tag == 251);
    const whole_record *snapshot = flow_run_input_value(run, ret, 1)->ptr;
    target->weight = 8;
    assert(snapshot->weight == 3.25 && snapshot->stamp == INT64_MAX - 4);
    flow_run_free(run);
    const behavior_program *program = a->program;
    ecs_entity high = other;
    for (int i = 2; i < 20001; i++) {
        high = ecs_world_record_create(world, (uint64_t)i);
        assert(high.slot);
    }
    assert(!ecs_world_apply_commands(world));
    /* Presence checks must reject a high slot before payload storage exists. */
    handle.u64 = ecs_entity_to_u64(high);
    run = flow_run_start(g, entry, (flow_inputs){ &handle, 1 }, &options, &error);
    assert(run && flow_run_resume(run) < 0 && oracle == eval_oracle_calls());
    flow_run_free(run);
    assert(!ecs_world_record_add(world, high, record_component.id, NULL, 0));
    assert(!ecs_world_apply_commands(world));
    source = ecs_component_data(world, owner, record_component.id);
    target = ecs_component_data(world, high, record_component.id);
    *target = (whole_record){ .stamp = INT64_MAX - 4 };
    handle.u64 = ecs_entity_to_u64(high);
    run = flow_run_start(g, entry, (flow_inputs){ &handle, 1 }, &options, &error);
    assert(run && !flow_run_resume(run) && run->whole_function_executed);
    assert(a->program == program && target->weight == source->weight && target->weight == 3.25);
    assert(target->stamp == INT64_MAX - 4);
    assert(oracle == eval_oracle_calls());
    flow_run_free(run);
    /* Rebind the same compiled program to read the high row and write a low row. */
    target->weight = -6.5;
    assert(!behavior_instance_rebind(bi, &handle));
    handle.u64 = ecs_entity_to_u64(other);
    run = flow_run_start(g, entry, (flow_inputs){ &handle, 1 }, &options, &error);
    assert(run && !flow_run_resume(run) && run->whole_function_executed);
    assert(a->program == program && oracle == eval_oracle_calls());
    assert(
        ((whole_record *)ecs_component_data(world, other, record_component.id))->weight == -6.5
    );
    flow_run_free(run);
    /* Removing the owner's component rejects the next read. */
    assert(!ecs_world_record_remove(world, high, record_component.id, 0));
    assert(!ecs_world_apply_commands(world));
    run = flow_run_start(g, entry, (flow_inputs){ &handle, 1 }, &options, &error);
    assert(run && flow_run_resume(run) < 0 && oracle == eval_oracle_calls());
    test_note(
        "%s: whole component access across 20,001 entities, nested snapshots; "
        "zero "
        "oracle calls\n",
        behavior_backend_name(behavior_instance_backend(bi))
    );
    flow_run_free(run);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
    ecs_world_free(world);
}
