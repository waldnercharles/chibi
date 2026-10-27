#include "ecs/ecs.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/program_internal.h"
#include "support/array.h"
#include "types/record.h"

static bool installed(const ecs_world *world, uint64_t id)
{
    return !id || (world && ecs_world_component(world, id));
}
static bool source_installed(const ecs_world *world, uint64_t id, int traversal)
{
    if (!id) {
        return traversal == ECS_SELF;
    }
    const ecs_relationship_desc *relationship = world
                                                  ? ecs_world_relationship(world, id)
                                                  : NULL;
    unsigned traits = ECS_REL_EXCLUSIVE | (traversal ? ECS_REL_TRAVERSABLE : 0);
    return traversal >= ECS_SELF && traversal <= ECS_SELF_UP && relationship &&
           (relationship->traits & traits) == traits;
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
/* Native services do not own the program's managed heap. A nominal record
 * cannot hide a pointer field when it crosses a service boundary. */
static bool managed_shape(pin_value value, int depth)
{
    if (value.type == PIN_POINTER || depth >= 64) {
        return true;
    }
    if (value.type != PIN_STRUCT) {
        return false;
    }
    value.container = PIN_CONTAINER_SINGLE;
    const type_desc *type = pin_value_type(&value);
    if (!type) {
        return false;
    }
    if (type->alias_of) {
        return managed_shape((pin_value){ .type = PIN_STRUCT, .type_name = type->alias_of }, depth + 1);
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (managed_shape((pin_value){ .type = field->type, .type_name = field->type_name }, depth + 1)) {
            return true;
        }
    }
    return false;
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
            instruction->op != BF_COLUMN_TRY && instruction->op != BF_COMPONENT_READ &&
            instruction->op != BF_COMPONENT_WRITE) {
            continue;
        }
        if (!installed(world, instruction->value.u64) ||
            ((instruction->type != PIN_BOOL || instruction->op == BF_COMPONENT_READ ||
              instruction->op == BF_COMPONENT_WRITE) &&
             !layout_matches(
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
        /* Only canonical intrinsics and borrowing kernels have a managed-heap
         * lifetime contract. Host services must opt into a distinct contract
         * before accepting managed pointer inputs or publishing such outputs. */
        const node_host_call *canonical = execution_intrinsic_resolve(host->capability_id);
        bool intrinsic = canonical &&
                         host->capability_abi ==
                             execution_intrinsic_abi(host->capability_id) &&
                         host->call->invoke == canonical->invoke &&
                         host->call->value == canonical->value &&
                         host->call->context == canonical->context &&
                         host->call->action == canonical->action;
        if (!intrinsic && !host->call->borrows) {
            for (int bank = host->first; bank < host->result_first + host->result_count;
                 bank++) {
                if (managed_shape(f->data_values[bank].initial, 0)) {
                    return false;
                }
            }
        }
        if (host->call != &EXECUTION_query_source_call &&
            host->call != &EXECUTION_query_target_call &&
            host->call != &EXECUTION_reference_make_call &&
            host->call != &EXECUTION_component_has_call &&
            host->call != &EXECUTION_component_membership_call) {
            continue;
        }
        const behavior_function_data_value *values = f->data_values + host->first;
        if (host->call == &EXECUTION_query_source_call &&
            (!installed(world, values[2].initial.u64) ||
             !source_installed(world, values[3].initial.u64, values[4].initial.i))) {
            return false;
        }
        if (host->call == &EXECUTION_query_target_call &&
            (!world || !ecs_world_relationship(world, values[2].initial.u64))) {
            return false;
        }
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
        if ((plan->queries[i].kind == EXECUTION_QUERY_RELATION ||
             plan->queries[i].kind == EXECUTION_QUERY_GENERATOR) &&
            (!world || !ecs_world_relationship(world, plan->queries[i].component))) {
            return false;
        }
        if ((plan->queries[i].kind == EXECUTION_QUERY_HAS ||
             plan->queries[i].kind == EXECUTION_QUERY_CHANGED) &&
            (!installed(world, plan->queries[i].component) ||
             !source_installed(
                 world,
                 plan->queries[i].source_relationship,
                 plan->queries[i].source_traversal
             ))) {
            return false;
        }
    }
    const behavior_function **functions = NULL;
    for (int i = 0; i < plan->query_count; i++) {
        add_function(&functions, plan->queries[i].predicate);
    }
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
