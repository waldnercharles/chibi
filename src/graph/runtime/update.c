#include "graph/runtime/flow_internal.h"
#include "graph/runtime/profile.h"
#include "graph/runtime/update_internal.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static bool same_services(const service_binding *a, const service_binding *b)
{
    if (acount(a) != acount(b)) {
        return false;
    }
    for (int i = 0; i < acount(a); i++) {
        if (a[i].value != b[i].value || strcmp(a[i].type_name, b[i].type_name)) {
            return false;
        }
    }
    return true;
}

behavior_update_binding *behavior_update_prepare(
    behavior_update_binding ***bindings,
    behavior_instance *instance,
    const behavior_function *function,
    node *entry
)
{
    assert(behavior_function_batchable(function));
    behavior_function_context context = { .budget = FLOW_DEFAULT_STEP_BUDGET };
    if (!behavior_function_bind(
            function,
            &context,
            instance->services,
            instance->owner.u64
        )) {
        return NULL;
    }
    for (int i = 0; i < acount(*bindings); i++) {
        behavior_update_binding *binding = (*bindings)[i];
        if (binding->function == function && binding->context.world == context.world &&
            binding->context.binding_world == context.binding_world &&
            same_services(binding->services, instance->services)) {
            return binding;
        }
    }
    behavior_update_binding *binding = xcalloc(1, sizeof *binding);
    binding->index = acount(*bindings);
    binding->function = function;
    binding->asset = instance->asset;
    binding->entry = entry;
    binding->code = behavior_function_entry(function);
    binding->system_code = behavior_function_system_entry(function);
    for (int i = 0; i < acount(instance->services); i++) {
        service_binding service = instance->services[i];
        service.type_name = xstrdup(service.type_name);
        apush(binding->services, service);
    }
    apush(*bindings, binding);
    behavior_function_context *ctx = &binding->context;
    *ctx = context;
    ctx->services = binding->services;
    behavior_instance_bind_values(instance, ctx);
    if (behavior_function_uses_storage(function)) {
        behavior_function_bind_storage(ctx, &binding->storage);
    }
    ctx->activation = &binding->scratch;
    if (behavior_function_resumable(function)) {
        behavior_function_activation_init(function, &binding->scratch);
    }
    behavior_function_prepare_host(function, ctx);
    behavior_function_bind_columns(function, ctx, &binding->columns);
    return binding;
}

void behavior_update_refresh(behavior_update_binding **bindings)
{
    for (int i = 0; i < acount(bindings); i++) {
        behavior_update_binding *binding = bindings[i];
        behavior_function_bind_columns(
            binding->function,
            &binding->context,
            &binding->columns
        );
    }
}

void behavior_update_free_bindings(behavior_update_binding **bindings)
{
    for (int i = 0; i < acount(bindings); i++) {
        behavior_update_binding *binding = bindings[i];
        behavior_function_release_host(&binding->context);
        behavior_function_activation_free(&binding->scratch);
        arena_free(&binding->storage);
        afree(binding->columns);
        for (int j = 0; j < acount(binding->services); j++) {
            free((char *)binding->services[j].type_name);
        }
        afree(binding->services);
        free(binding);
    }
    afree(bindings);
}

behavior_update_row *behavior_update_run_c(
    behavior_update_row *row,
    behavior_update_row *end,
    const ecs_execution_view *world,
    int *result,
    behavior_profile_fn profile
)
{
    *result = FLOW_OK;
    for (; row != end; row++) {
        behavior_instance *bi = row->instance;
        behavior_update_binding *binding = row->binding;
        if (!binding || !bi->alive || bi->trace_enabled || acount(bi->activations) ||
            (bi->backend != BEHAVIOR_SLJIT && bi->backend != BEHAVIOR_BYTECODE) ||
            bi->asset != binding->asset ||
            bi->services_revision != row->services_revision ||
            (bi->profile && bi->profile != profile) ||
            !ecs_execution_entity(world, row->entity)) {
            break;
        }
        behavior_profile_scope scope;
        if (bi->profile) {
            behavior_profile_enter(&scope);
        }
        behavior_function_context *ctx = &binding->context;
        ctx->owner = bi->owner.u64;
        ctx->delta_seconds = bi->delta_seconds;
        ctx->steps = 0;
        behavior_instance_bind_values(bi, ctx);
        if (ctx->value_storage) {
            arena_reset(ctx->value_storage);
        }
        if (bi->profile) {
            bi->profile(
                binding->entry->reference_name,
                behavior_profile_leave(&scope),
                BEHAVIOR_PROFILE_SETUP,
                bi->profile_user
            );
            behavior_profile_enter(&scope);
        }
        *result = bi->backend == BEHAVIOR_SLJIT
                    ? ((int (*)(behavior_function_context *))binding->code)(ctx)
                    : behavior_function_run(binding->function, ctx, false);
        assert(*result <= FLOW_OK);
        if (bi->profile) {
            bi->profile(
                binding->entry->reference_name,
                behavior_profile_leave(&scope),
                BEHAVIOR_PROFILE_START,
                bi->profile_user
            );
        }
        if (*result) {
            break;
        }
    }
    return row;
}

flow_run *behavior_update_failure(behavior_update_row *row, int result)
{
    assert(result < FLOW_OK);
    behavior_update_binding *binding = row->binding;
    flow_run *failed = behavior_instance_enqueue(row->instance, binding->entry, (flow_inputs){ 0 }, NULL);
    assert(failed);
    behavior_function_context context = binding->context;
    /* The binding retains its banks. The captured activation owns separate
     * inspection storage and must not inherit this context's ownership flags. */
    context.owned_data = false;
    context.owned_host = false;
    context.columns = NULL;
    if (context.value_storage) {
        /* Failure inspection retains managed child arguments. Transfer the
         * arena before another row reuses this binding; never rerun effects. */
        arena_free(&failed->scratch);
        failed->scratch = binding->storage;
        binding->storage = (arena){ 0 };
        behavior_function_bind_storage(&context, &failed->scratch);
    }
    flow_run_finish_compiled(failed, binding->function, &context, result);
    return failed;
}
