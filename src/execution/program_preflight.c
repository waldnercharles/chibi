#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/program_internal.h"
#include "support/array.h"
#include "types/record.h"

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
    if (value.type == PIN_POINTER || value.type == PIN_QUERY || depth >= 64) {
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
static bool function_valid(const behavior_function *f)
{
    if (acount(f->components)) {
        return false;
    }
    for (int i = 0; i < acount(f->instructions); i++) {
        switch (f->instructions[i].op) {
            case BF_COMPONENT:
            case BF_COLUMN:
            case BF_COLUMN_TRY:
            case BF_COMPONENT_READ:
            case BF_COMPONENT_WRITE: return false;
            default: break;
        }
    }
    for (int i = 0; i < acount(f->host_calls); i++) {
        const behavior_function_host_call *host = &f->host_calls[i];
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
                         host->call->action == canonical->action &&
                         host->call->service == canonical->service &&
                         host->call->missing_service == canonical->missing_service &&
                         host->call->optional_service == canonical->optional_service &&
                         host->call->variadic == canonical->variadic &&
                         host->call->reusable == canonical->reusable;
        if (!intrinsic && !host->call->borrows) {
            for (int bank = host->first; bank < host->result_first + host->result_count;
                 bank++) {
                if (managed_shape(f->data_values[bank].initial, 0)) {
                    return false;
                }
            }
        }
    }
    return true;
}
bool program_preflight(const execution_module *module)
{
    const execution_module_plan *plan = &module->plan;
    const behavior_function **functions = NULL;
    for (int i = 0; i < plan->function_count; i++) {
        add_function(&functions, plan->functions[i].body);
        add_function(&functions, plan->functions[i].subscription);
    }
    bool valid = true;
    for (int i = 0; i < acount(functions); i++) {
        const behavior_function *function = functions[i];
        if (!function_valid(function)) {
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
