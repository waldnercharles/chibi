#include "ecs/ecs.h"
#include "ecs/ecs_internal.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/module_internal.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "execution/storage.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static execution_module_plan *plan;
static execution_module *module, *loaded;
static behavior_function_builder *builder;
static behavior_function *binding_oracle;
static unsigned char *bytes, *again;
static ecs_world *world;
static bool unsafe_link;
static char error[256];
static const type_field value_fields[] = {
    { .name = "value", .type = PIN_F64, .size = sizeof(double), .type_name = "double" },
};
static const type_desc value_type = { .name = "query.scalar",
                                      .size = sizeof(double),
                                      .fields = value_fields,
                                      .field_count = 1 };
static const uint64_t reads[] = { 802 };
static const node_access read_access = { .reads = reads,
                                         .read_count = 1,
                                         .flags = NODE_EFFECT_PURE |
                                                  NODE_EFFECT_WORLD_READ };
static const node_access unsafe_access = { .flags = NODE_EFFECT_HOST_SINK };
static const char *read_value(void *service, const pin_value *arguments, pin_value *results)
{
    (void)service;
    (void)arguments;
    results[0].b = true;
    return NULL;
}
static const node_host_call read_call = { .service = "query.read", .invoke = read_value };
static void cleanup()
{
    if (builder) {
        behavior_function_builder_free(builder);
    }
    behavior_function_free(binding_oracle);
    binding_oracle = NULL;
    execution_module_plan_free(plan);
    execution_module_free(module);
    execution_module_free(loaded);
    if (world) {
        ecs_world_free(world);
    }
    free(bytes);
    free(again);
    builder = NULL;
    plan = NULL;
    module = loaded = NULL;
    world = NULL;
    bytes = again = NULL;
    unsafe_link = false;
}
static void emit(behavior_function_instruction instruction)
{
    behavior_function_builder_emit(builder, instruction);
}
static behavior_function *finish_function()
{
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    behavior_function *function = behavior_function_builder_finish(builder, error, sizeof error);
    if (!function) {
        fprintf(stderr, "query boundary function: %s\n", error);
    }
    behavior_function_builder_free(builder);
    builder = NULL;
    return function;
}
static void begin_function(int variables)
{
    builder = behavior_function_builder_new();
    pin_value boolean = { .type = PIN_BOOL };
    behavior_function_builder_signature(builder, NULL, 0, &boolean, 1, &boolean, variables);
}
static void return_true()
{
    /* Authored return updates private invocation control before its result. */
    int context = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
    behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, context, BEHAVIOR_ADDRESS);
    emit((
        behavior_function_instruction
    ){ .op = BF_CONTEXT, .type = BF_POINTER_TYPE, .output = context, .target = 1 });
    int slot = behavior_function_builder_slot(builder, PIN_BOOL);
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_BOOL,
                                          .output = slot,
                                          .value = { .type = PIN_BOOL, .b = true } });
    emit((behavior_function_instruction){
        .op = BF_WRITE,
        .type = PIN_BOOL,
        .left = slot,
        .right = context,
        .offset = offsetof(behavior_function_context, exit_requested) });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_BOOL, .left = slot });
}
/* Keep the original intrinsic path as a differential oracle for direct loads. */
static behavior_function *query_binding_host(int variable)
{
    builder = behavior_function_builder_new();
    pin_value result = { .type = PIN_HANDLE };
    behavior_function_builder_signature(builder, NULL, 0, &result, 1, NULL, 0);
    pin_value arguments[] = { { .type = BF_POINTER_TYPE },
                              { .type = PIN_I32, .i = variable } };
    int host = behavior_function_builder_host(
        builder,
        &EXECUTION_query_variable_call,
        NULL,
        arguments,
        2,
        &result,
        1
    );
    uint64_t id = execution_intrinsic_id(&EXECUTION_query_variable_call);
    behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id));
    int first = behavior_function_builder_host_first(builder, host);
    behavior_function_builder_kind(builder, BEHAVIOR_BANK_DATA, first, BEHAVIOR_ADDRESS);
    int context = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
    int address = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
    int value = behavior_function_builder_slot(builder, PIN_HANDLE);
    behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, context, BEHAVIOR_ADDRESS);
    behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, address, BEHAVIOR_ADDRESS);
    emit((
        behavior_function_instruction
    ){ .op = BF_CONTEXT, .type = BF_POINTER_TYPE, .output = context, .target = 1 });
    emit((behavior_function_instruction){ .op = BF_HOST_SLOT,
                                          .type = BF_POINTER_TYPE,
                                          .output = address,
                                          .left = first });
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = BF_POINTER_TYPE,
                                          .left = context,
                                          .right = address,
                                          .offset = offsetof(pin_value, d) });
    int source = behavior_function_builder_origin(
        builder,
        (behavior_function_source){ .graph_id = 8310,
                                    .call_id = 8311,
                                    .parent = -1,
                                    .asset_name = "QueryBinding.chibi",
                                    .start = 12,
                                    .end = 18,
                                    .line = 2,
                                    .column = 5 }
    );
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                          .source = source,
                                          .node_id = 8312,
                                          .pin = 7 });
    int result_address = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
    behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, result_address, BEHAVIOR_ADDRESS);
    emit((behavior_function_instruction){
        .op = BF_HOST_SLOT,
        .type = BF_POINTER_TYPE,
        .output = result_address,
        .left = behavior_function_builder_host_results(builder, host) });
    emit((behavior_function_instruction){ .op = BF_LOAD,
                                          .type = PIN_HANDLE,
                                          .output = value,
                                          .left = result_address,
                                          .offset = offsetof(pin_value, d) });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_HANDLE, .left = value });
    return finish_function();
}
static int inspect_binding(void *user, const behavior_function_point *point)
{
    (void)user;
    (void)point;
    return 0;
}
static bool check_query_binding(const behavior_function *function, int variable)
{
    const uint64_t values[] = { UINT64_C(0x87654321abcdef01), 0, 37 };
    uint64_t tuple[ECS_QUERY_VARS];
    for (int i = 0; i < ECS_QUERY_VARS; i++) {
        tuple[i] = (uint64_t)i + 101;
    }
    REQUIRE(behavior_function_compile((behavior_function *)function));
    for (int backend = 0; backend < 2; backend++) {
        for (int inspected = 0; inspected < 2; inspected++) {
            /* Reuse one definition with distinct current tuples and contexts. */
            for (int sample = 0; sample < 4; sample++) {
                bool missing = sample == 2;
                uint64_t expected = values[sample == 3 ? 2 : sample];
                tuple[variable - 1] = expected;
                pin_value output = { .type = PIN_HANDLE, .u64 = UINT64_MAX };
                behavior_function_context context = {
                    .query_variables = missing ? NULL : tuple,
                    .results = &output,
                    .budget = 100,
                    .inspect = inspected ? inspect_binding : NULL,
                };
                int status = behavior_function_run(function, &context, backend == 1);
                if (status != (missing ? FLOW_ERR_INVALID_GRAPH : FLOW_OK)) {
                    fprintf(
                        stderr,
                        "query binding variable=%d backend=%d inspected=%d "
                        "sample=%d status=%d failure=%d message=%s\n",
                        variable,
                        backend,
                        inspected,
                        sample,
                        status,
                        context.failure,
                        context.error_message ? context.error_message : "none"
                    );
                }
                REQUIRE(status == (missing ? FLOW_ERR_INVALID_GRAPH : FLOW_OK));
                REQUIRE(output.type == PIN_HANDLE);
                REQUIRE(output.u64 == (missing ? UINT64_MAX : expected));
                if (!missing) {
                    REQUIRE(!context.error_message);
                    continue;
                }
                REQUIRE(
                    context.error_message &&
                    !strcmp(context.error_message, "Query variable is not bound")
                );
                REQUIRE(context.failure == BEHAVIOR_FUNCTION_HOST_DATA);
                REQUIRE(context.node_id == 8312 && context.error_pin == 7);
                REQUIRE(context.error_frame == 0 && context.error_source == 1);
                REQUIRE(context.error_stack == -1 && context.error_steps == 0);
                REQUIRE(acount(function->sources) == 2);
                const behavior_function_source
                    *source = &function->sources[context.error_source];
                REQUIRE(source->graph_id == 8310 && source->call_id == 8311);
                REQUIRE(source->start == 12 && source->end == 18);
                REQUIRE(source->line == 2 && source->column == 5);
                REQUIRE(!strcmp(source->asset_name, "QueryBinding.chibi"));
            }
        }
    }
    return true;
}
TEST_CASE(query_binding_host_preserves_tuple_and_failure_contract)
{
    const int variables[] = { 1, 2, ECS_QUERY_VARS };
    for (int i = 0; i < 3; i++) {
        cleanup();
        binding_oracle = query_binding_host(variables[i]);
        REQUIRE(binding_oracle);
        REQUIRE(acount(binding_oracle->host_calls) == 1);
        REQUIRE(acount(binding_oracle->data_values) == 3);
        REQUIRE(check_query_binding(binding_oracle, variables[i]));
    }
    return true;
}
static double binding_seconds()
{
    struct timespec now;
    int status = clock_gettime(CLOCK_MONOTONIC, &now);
    assert(status == 0);
    (void)status;
    return now.tv_sec + now.tv_nsec * 1e-9;
}
TEST_CASE(query_binding_dispatch_has_bounded_measurements)
{
    enum
    {
        TRIALS = 5,
        READS = 512
    };
    binding_oracle = query_binding_host(ECS_QUERY_VARS);
    REQUIRE(binding_oracle && behavior_function_compile(binding_oracle));
    uint64_t tuple[ECS_QUERY_VARS] = { 0 };
    tuple[ECS_QUERY_VARS - 1] = UINT64_C(0x87654321abcdef01);
    for (int backend = 0; backend < 2; backend++) {
        pin_value output = { .type = PIN_HANDLE };
        behavior_function_context warm = { .query_variables = tuple,
                                           .results = &output,
                                           .budget = 100 };
        REQUIRE(behavior_function_run(binding_oracle, &warm, backend == 1) == FLOW_OK);
        for (int trial = 0; trial < TRIALS; trial++) {
            int status = FLOW_OK;
            xalloc_stats before, after;
            xalloc_stats_get(&before);
            uint64_t blocks = arena_block_allocations();
            double start = binding_seconds();
            for (int i = 0; i < READS; i++) {
                behavior_function_context context = { .query_variables = tuple,
                                                      .results = &output,
                                                      .budget = 100 };
                status |= behavior_function_run(binding_oracle, &context, backend == 1);
            }
            double elapsed = binding_seconds() - start;
            xalloc_stats_get(&after);
            blocks = arena_block_allocations() - blocks;
            REQUIRE(status == FLOW_OK && output.u64 == tuple[ECS_QUERY_VARS - 1]);
            printf(
                "QUERY_BINDING path=production_value_host backend=%s trial=%d "
                "reads=%d us_per_read=%.6f host_calls=%d data_values=%d "
                "allocation_calls=%llu requested_bytes=%llu "
                "arena_blocks=%llu\n",
                backend ? "compiled" : "vm",
                trial,
                READS,
                elapsed * 1e6 / READS,
                acount(binding_oracle->host_calls),
                acount(binding_oracle->data_values),
                (unsigned long long)(after.calls - before.calls),
                (unsigned long long)(after.requested_bytes - before.requested_bytes),
                (unsigned long long)blocks
            );
        }
    }
    return true;
}
/* These tiny definitions bypass source diagnostics to exercise the module boundary. */
static behavior_function *function(int kind, int variables)
{
    begin_function(variables);
    if (kind == 1) {
        pin_value boolean = { .type = PIN_BOOL };
        int host = behavior_function_builder_host(builder, &read_call, &read_access, NULL, 0, &boolean, 1);
        behavior_function_builder_host_identity(builder, host, 9001, 1);
        emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                              .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                              .target = host });
    } else if (kind == 2) {
        int slot = behavior_function_builder_slot(builder, PIN_I32);
        emit((behavior_function_instruction){ .op = BF_RANDOM,
                                              .type = PIN_I32,
                                              .output = slot });
    } else if (kind == 3) {
        int slot = behavior_function_builder_slot(builder, PIN_BOOL);
        emit((
            behavior_function_instruction
        ){ .op = BF_VARIABLE, .type = PIN_BOOL, .output = slot, .left = 0 });
        emit((behavior_function_instruction){ .op = BF_RESULT,
                                              .type = PIN_BOOL,
                                              .left = slot });
        return finish_function();
    } else if (kind == 4) {
        pin_value arguments[] = { { .type = BF_POINTER_TYPE },
                                  { .type = PIN_I32, .i = 1 } };
        pin_value result = { .type = PIN_HANDLE };
        int host = behavior_function_builder_host(
            builder,
            &EXECUTION_query_variable_call,
            NULL,
            arguments,
            2,
            &result,
            1
        );
        uint64_t id = execution_intrinsic_id(&EXECUTION_query_variable_call);
        behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id));
        behavior_function_builder_kind(
            builder,
            BEHAVIOR_BANK_DATA,
            behavior_function_builder_host_first(builder, host),
            BEHAVIOR_ADDRESS
        );
        int context = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
        int address = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
        int offset = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
        int alias = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
        int recovered = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
        behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, context, BEHAVIOR_ADDRESS);
        behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, address, BEHAVIOR_ADDRESS);
        behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, alias, BEHAVIOR_ADDRESS);
        behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, recovered, BEHAVIOR_ADDRESS);
        emit((behavior_function_instruction){ .op = BF_CONTEXT,
                                              .type = BF_POINTER_TYPE,
                                              .output = context,
                                              .target = 1 });
        emit((behavior_function_instruction){
            .op = BF_HOST_SLOT,
            .type = BF_POINTER_TYPE,
            .output = address,
            .left = behavior_function_builder_host_first(builder, host) });
        emit((behavior_function_instruction){ .op = BF_WRITE,
                                              .type = BF_POINTER_TYPE,
                                              .left = context,
                                              .right = address,
                                              .offset = offsetof(pin_value, d) });
        emit((behavior_function_instruction){
            .op = BF_CONSTANT,
            .type = BF_POINTER_TYPE,
            .output = offset,
            .value = { .type = BF_POINTER_TYPE, .u64 = offsetof(pin_value, d) } });
        emit((behavior_function_instruction){ .op = BF_ADD,
                                              .type = BF_POINTER_TYPE,
                                              .output = alias,
                                              .left = address,
                                              .right = offset });
        emit((behavior_function_instruction){ .op = BF_LOAD,
                                              .type = BF_POINTER_TYPE,
                                              .output = recovered,
                                              .left = alias });
    }
    return_true();
    return finish_function();
}
static bool prepare()
{
    plan = execution_module_plan_new();
    plan->valid = true;
    plan->id = 8000;
    behavior_function *body = function(0, 0);
    REQUIRE(body);
    apush(
        plan->functions,
        ((execution_module_function){ .id = 1, .kind = EXECUTION_FUNCTION_SYSTEM, .body = body })
    );
    plan->function_count = 1;
    behavior_function *predicate = function(1, 0);
    REQUIRE(predicate);
    apush(
        plan->queries,
        ((execution_module_query){ .kind = EXECUTION_QUERY_GENERATOR,
                                   .component = 901,
                                   .target_variable = 1 })
    );
    apush(
        plan->queries,
        ((execution_module_query){ .kind = EXECUTION_QUERY_CHANGED,
                                   .component = 801,
                                   .source_variable = 1,
                                   .source_relationship = 902 })
    );
    apush(plan->queries, ((execution_module_query){ .kind = EXECUTION_QUERY_VALUE, .predicate = predicate }));
    int *children = malloc(3 * sizeof *children);
    REQUIRE(children);
    children[0] = 0;
    children[1] = 1;
    children[2] = 2;
    apush(
        plan->queries,
        ((execution_module_query){ .kind = EXECUTION_QUERY_ALL,
                                   .children = children,
                                   .child_count = 3 })
    );
    plan->query_count = acount(plan->queries);
    plan->query = 3;
    plan->query_variable_count = 1;
    return true;
}
static bool resolve_host(void *user, uint64_t id, execution_prepared_host *out)
{
    (void)user;
    if (id != 9001) {
        return false;
    }
    *out = (execution_prepared_host){ .call = &read_call,
                                      .access = unsafe_link ? &unsafe_access : &read_access,
                                      .abi = 1 };
    return true;
}
static const execution_prepared_linker linker = { .host = resolve_host };
static bool read_dependency(execution_effect_summary effects, uint64_t id)
{
    for (int i = 0; i < effects.read_count; i++) {
        if (effects.reads[i] == id) {
            return true;
        }
    }
    return false;
}
TEST_CASE(query_predicates_roundtrip_with_services_effects_and_preflight)
{
    REQUIRE(prepare());
    /* A mandatory nested conjunction must retain its generator before the
     * downstream variable-dependent leaf, including after serialization. */
    plan->queries[3].children[1] = 2;
    plan->queries[3].child_count = 2;
    int *children = malloc(2 * sizeof *children);
    REQUIRE(children);
    children[0] = 3;
    children[1] = 1;
    apush(
        plan->queries,
        ((execution_module_query){ .kind = EXECUTION_QUERY_ALL,
                                   .children = children,
                                   .child_count = 2 })
    );
    plan->query = plan->query_count++;
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module);
    execution_effect_summary effects = execution_module_effects(module);
    REQUIRE(!effects.parallel_safe);
    REQUIRE(effects.declared && !effects.structural && !effects.host_sink);
    REQUIRE(read_dependency(effects, 801) && read_dependency(effects, 802));
    REQUIRE(read_dependency(effects, 901) && read_dependency(effects, 902));
    int count = 0;
    const char *const *services = execution_module_services(module, &count);
    REQUIRE(count == 2);
    REQUIRE(!strcmp(services[0], "query.read") && !strcmp(services[1], "ecs.world"));
    REQUIRE(execution_module_costs(module).possible != 0);
    REQUIRE(
        execution_module_storage_bytes(module) >
        behavior_function_storage_bytes(module->plan.queries[2].predicate)
    );
    size_t length = 0, repeated = 0;
    REQUIRE(execution_prepared_write(module, &bytes, &length, error, sizeof error));
    loaded = execution_prepared_read(bytes, length, &linker, BEHAVIOR_BYTECODE, error, sizeof error);
    if (!loaded) {
        fprintf(stderr, "query boundary roundtrip: %s\n", error);
    }
    REQUIRE(loaded);
    REQUIRE(loaded->plan.query_variable_count == 1 && loaded->plan.queries[2].predicate);
    REQUIRE(
        loaded->plan.queries[0].target_variable == 1 &&
        loaded->plan.queries[1].source_variable == 1
    );
    REQUIRE(execution_prepared_write(loaded, &again, &repeated, error, sizeof error));
    REQUIRE(length == repeated && !memcmp(bytes, again, length));
    world = ecs_world_new();
    const ecs_relationship_desc relationship = { .id = 901,
                                                 .key = "query.targets",
                                                 .name = "Targets" };
    const ecs_relationship_desc source = { .id = 902,
                                           .key = "query.source",
                                           .name = "Source",
                                           .traits = ECS_REL_EXCLUSIVE };
    REQUIRE(ecs_world_register_relationship(world, &relationship, NULL) == ECS_OK);
    REQUIRE(ecs_world_register_relationship(world, &source, NULL) == ECS_OK);
    ecs_component_desc component = { .id = 801,
                                     .type = &value_type,
                                     .key = "query.position",
                                     .name = "Position",
                                     .element_size = sizeof(double),
                                     .layout_hash = 1 };
    REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_OK);
    REQUIRE(!execution_module_components_valid(loaded, world));
    component.id = 802;
    component.key = "query.health";
    component.name = "Health";
    REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_OK);
    REQUIRE(execution_module_components_valid(loaded, world));
    cleanup();
    REQUIRE(prepare());
    children = malloc(2 * sizeof *children);
    REQUIRE(children);
    children[0] = 1;
    children[1] = 2;
    apush(
        plan->queries,
        ((execution_module_query){ .kind = EXECUTION_QUERY_ALL,
                                   .children = children,
                                   .child_count = 2 })
    );
    plan->query_filter = ++plan->query_count;
    plan->queries[3].children[1] = 4;
    plan->queries[3].child_count = 2;
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module);
    REQUIRE(execution_prepared_write(module, &bytes, &length, error, sizeof error));
    loaded = execution_prepared_read(bytes, length, &linker, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(loaded && loaded->plan.query_filter == 5);
    return true;
}
TEST_CASE(query_binding_order_is_checked_before_publication)
{
    for (int bad = 0; bad < 11; bad++) {
        cleanup();
        REQUIRE(prepare());
        switch (bad) {
            case 0: plan->query_variable_count = 9; break;
            case 1: plan->queries[0].source_variable = 1; break;
            case 2:
                plan->query_variable_count = 2;
                plan->queries[0].target_variable = 2;
                break;
            case 3: plan->queries[3].kind = EXECUTION_QUERY_ANY; break;
            case 4:
                plan->queries[3].children[0] = 1;
                plan->queries[3].children[1] = 0;
                break;
            case 5: plan->queries[3].child_count = 2; break;
            case 6:
                plan->query_filter = 3;
                break; /* Changed outside marked where. */
            case 7:
                plan->query_filter = 4;
                break; /* Generator inside marked where. */
            case 8: plan->query_filter = 5; break; /* Out of bounds. */
            case 9: {
                int *child = malloc(sizeof *child);
                REQUIRE(child);
                *child = 2;
                apush(
                    plan->queries,
                    ((execution_module_query){ .kind = EXECUTION_QUERY_NOT,
                                               .children = child,
                                               .child_count = 1 })
                );
                plan->query_count++;
                plan->queries[1].kind = EXECUTION_QUERY_HAS;
                plan->queries[3].children[2] = 4;
                plan->query_filter = 3;
                break;
            }
            case 10: {
                int *children = realloc(plan->queries[3].children, 4 * sizeof *children);
                REQUIRE(children);
                plan->queries[3].children = children;
                children[3] = 2;
                plan->queries[3].child_count = 4;
                plan->queries[1].kind = EXECUTION_QUERY_HAS;
                plan->query_filter = 3;
                break;
            }
        }
        module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
        REQUIRE(!module && plan);
        REQUIRE(strstr(error, "query"));
    }
    return true;
}
TEST_CASE(query_predicate_purity_is_rechecked_after_native_linking)
{
    REQUIRE(prepare());
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module);
    size_t length = 0;
    REQUIRE(execution_prepared_write(module, &bytes, &length, error, sizeof error));
    unsafe_link = true;
    loaded = execution_prepared_read(bytes, length, &linker, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(!loaded);
    REQUIRE(strstr(error, "predicate"));
    return true;
}
TEST_CASE(query_predicate_purity_follows_each_outlined_callee)
{
    for (int bad = 0; bad < 3; bad++) {
        cleanup();
        plan = execution_module_plan_new();
        plan->valid = true;
        plan->id = 8100;
        int variables = bad == 2;
        if (variables) {
            apush(plan->states, ((execution_module_state){ .id = 1, .initial.type = PIN_BOOL }));
            plan->state_count = 1;
        }
        behavior_function *first = function(0, variables);
        REQUIRE(first);
        apush(
            plan->functions,
            ((execution_module_function){ .id = 1,
                                          .kind = EXECUTION_FUNCTION_AUTHORED,
                                          .body = first })
        );
        behavior_function *second = function(bad ? bad + 1 : 0, variables);
        REQUIRE(second);
        apush(
            plan->functions,
            ((execution_module_function){ .id = 2,
                                          .kind = EXECUTION_FUNCTION_AUTHORED,
                                          .body = second })
        );
        plan->function_count = 2;
        begin_function(variables);
        pin_value boolean = { .type = PIN_BOOL };
        int call = behavior_function_builder_call(builder, first, NULL, 0, &boolean, 1);
        emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
        call = behavior_function_builder_call(builder, second, NULL, 0, &boolean, 1);
        emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
        return_true();
        behavior_function *predicate = finish_function();
        REQUIRE(predicate);
        apush(
            plan->queries,
            ((execution_module_query){ .kind = EXECUTION_QUERY_VALUE, .predicate = predicate })
        );
        plan->query_count = 1;
        plan->query = 0;
        module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
        REQUIRE(bad ? !module && plan : module && !plan);
        if (bad) {
            REQUIRE(strstr(error, "predicate"));
        }
    }
    return true;
}
TEST_CASE(query_predicate_context_cannot_be_recovered_through_bank_aliases)
{
    REQUIRE(prepare());
    behavior_function_free(plan->queries[2].predicate);
    plan->queries[2].predicate = function(4, 0);
    REQUIRE(plan->queries[2].predicate);
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(!module && plan);
    REQUIRE(strstr(error, "predicate"));
    return true;
}
TEST_CASE(query_change_versions_saturate)
{
    world = ecs_world_new();
    const ecs_component_desc component = { .id = 801,
                                           .type = &value_type,
                                           .key = "query.position",
                                           .name = "Position",
                                           .element_size = sizeof(double),
                                           .layout_hash = 1 };
    REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(world);
    REQUIRE(ecs_world_record_add(world, entity, component.id, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_component_track_changes(world, component.id) == ECS_OK);
    /* Inject the boundary rather than attempting an unbounded write loop.
     * Public SDK consumer cases do not include private ECS headers. */
    find_component(world, component.id)->versions[entity.slot] = UINT64_MAX - 1;
    REQUIRE(ecs_component_modified(world, entity, component.id) == ECS_OK);
    REQUIRE(ecs_component_version(world, entity, component.id) == UINT64_MAX);
    REQUIRE(ecs_component_modified(world, entity, component.id) == ECS_OK);
    REQUIRE(ecs_component_version(world, entity, component.id) == UINT64_MAX);
    return true;
}
void test_language_query_boundary()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(query_binding_host_preserves_tuple_and_failure_contract);
    RUN_TEST_CASE(query_binding_dispatch_has_bounded_measurements);
    RUN_TEST_CASE(query_change_versions_saturate);
    RUN_TEST_CASE(query_predicates_roundtrip_with_services_effects_and_preflight);
    RUN_TEST_CASE(query_binding_order_is_checked_before_publication);
    RUN_TEST_CASE(query_predicate_purity_is_rechecked_after_native_linking);
    RUN_TEST_CASE(query_predicate_purity_follows_each_outlined_callee);
    RUN_TEST_CASE(query_predicate_context_cannot_be_recovered_through_bank_aliases);
    pu_clear_setup();
}
