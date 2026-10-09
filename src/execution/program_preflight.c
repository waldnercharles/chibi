#include "ecs/ecs.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/program_internal.h"
#include "support/array.h"

static bool installed(const ecs_world *world, uint64_t id)
{
    return !id || (world && ecs_world_component(world, id));
}
static bool layout_matches(const ecs_world *world, uint64_t id, uint64_t layout, size_t size)
{
    const ecs_component_desc *component = world ? ecs_world_component(world, id) : NULL;
    return component && component->layout_hash == layout &&
           component->element_size == size;
}
static bool access_valid(const ecs_world *world, const node_access *access)
{
    if (!access) {
        return true;
    }
    for (int i = 0; i < access->read_count; i++) {
        if (!installed(world, access->reads[i])) {
            return false;
        }
    }
    for (int i = 0; i < access->write_count; i++) {
        if (!installed(world, access->writes[i])) {
            return false;
        }
    }
    if (access->read_refs) {
        for (int i = 0; access->read_refs[i]; i++) {
            if (!installed(world, *access->read_refs[i])) {
                return false;
            }
        }
    }
    return true;
}
static void add_function(const behavior_function ***functions, const behavior_function *function)
{
    if (!function) {
        return;
    }
    for (int i = 0; i < acount(*functions); i++) {
        if ((*functions)[i] == function) {
            return;
        }
    }
    apush(*functions, function);
}
static bool function_valid(const behavior_function *f, const ecs_world *world)
{
    for (int i = 0; i < acount(f->components); i++) {
        if (!installed(world, f->components[i].id)) {
            return false;
        }
    }
    for (int i = 0; i < acount(f->instructions); i++) {
        const behavior_function_instruction *instruction = &f->instructions[i];
        if (instruction->op != BF_COMPONENT && instruction->op != BF_COLUMN &&
            instruction->op != BF_COLUMN_TRY) {
            continue;
        }
        if (!installed(world, instruction->value.u64) ||
            (instruction->type != PIN_BOOL && !layout_matches(
                                                  world,
                                                  instruction->value.u64,
                                                  instruction->layout,
                                                  instruction->offset
                                              ))) {
            return false;
        }
    }
    for (int i = 0; i < acount(f->host_calls); i++) {
        const behavior_function_host_call *host = &f->host_calls[i];
        if (!access_valid(world, host->access)) {
            return false;
        }
        const behavior_function_data_value *values = f->data_values + host->first;
        if (host->call == &EXECUTION_reference_make_call &&
            values[2].initial.i == LANGUAGE_REFERENCE_COMPONENT &&
            !layout_matches(
                world,
                values[6].initial.u64,
                values[7].initial.u64,
                values[8].initial.u64
            )) {
            return false;
        }
        if ((host->call == &EXECUTION_component_has_call ||
             host->call == &EXECUTION_component_membership_call) &&
            !installed(world, values[2].initial.u64)) {
            return false;
        }
    }
    return true;
}
bool program_preflight(const execution_module *module, const ecs_world *world)
{
    const execution_module_plan *plan = &module->plan;
    for (int i = 0; i < plan->query_count; i++) {
        if (plan->queries[i].kind == EXECUTION_QUERY_HAS &&
            !installed(world, plan->queries[i].component)) {
            return false;
        }
    }
    const behavior_function **functions = NULL;
    for (int i = 0; i < plan->binding_count; i++) {
        const execution_module_binding *binding = &plan->bindings[i];
        const ecs_component_desc
            *component = world ? ecs_world_component(world, binding->component) : NULL;
        if (!component || component->layout_hash != binding->layout ||
            !ecs_component_accepts_value(
                world,
                binding->component,
                binding->offset,
                binding->target_type
            )) {
            afree(functions);
            return false;
        }
        add_function(&functions, binding->body);
        add_function(&functions, binding->dependencies);
    }
    for (int i = 0; i < plan->function_count; i++) {
        const execution_module_function *function = &plan->functions[i];
        const execution_field_observer *observer = &function->observer;
        if (observer->component) {
            const ecs_component_desc
                *component = world ? ecs_world_component(world, observer->component)
                                   : NULL;
            if (!component || component->layout_hash != observer->layout ||
                !ecs_component_accepts_value(
                    world,
                    observer->component,
                    observer->offset,
                    observer->type
                )) {
                afree(functions);
                return false;
            }
        }
        add_function(&functions, function->body);
        add_function(&functions, function->subscription);
    }
    bool valid = true;
    for (int i = 0; i < acount(functions); i++) {
        const behavior_function *function = functions[i];
        if (!function_valid(function, world)) {
            valid = false;
            break;
        }
        add_function(&functions, function->fast);
        for (int j = 0; j < acount(function->calls); j++) {
            add_function(&functions, function->calls[j].function);
        }
        for (int j = 0; j < acount(function->definitions); j++) {
            add_function(&functions, function->definitions[j]);
        }
    }
    afree(functions);
    return valid;
}

#define COMPONENT_PREFLIGHT_MODULES_MAX 65536

bool execution_module_components_valid(const execution_module *module, const ecs_world *world)
{
    const execution_module **modules = NULL;
    apush(modules, module);
    bool valid = true;
    for (int i = 0; i < acount(modules) && valid; i++) {
        const execution_module *current = modules[i];
        if (!program_preflight(current, world)) {
            valid = false;
            break;
        }
        for (int j = 0; j < current->plan.import_count; j++) {
            const execution_module *import = current->plan.imports[j];
            bool found = false;
            for (int k = 0; k < acount(modules); k++) {
                if (modules[k] == import) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                if (acount(modules) == COMPONENT_PREFLIGHT_MODULES_MAX) {
                    valid = false;
                    break;
                }
                apush(modules, import);
            }
        }
    }
    afree(modules);
    return valid;
}
