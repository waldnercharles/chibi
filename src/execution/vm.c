#include "execution/machine.h"
#include "execution/runner.h"
#include "execution/status.h"
#include "support/array.h"

static bool pure_operation(behavior_function_op op)
{
    switch (op) {
        case BF_VARIABLE:
        case BF_DELTA:
        case BF_OWNER:
        case BF_WORLD:
        case BF_QUERY_VARIABLE:
        case BF_QUERY:
        case BF_HOST_SERVICE:
        case BF_COMMAND_SERVICE:
        case BF_COMMAND_RESULT:
        case BF_STATE_SELECT:
        case BF_RANDOM:
        case BF_RANDOM_RANGE:
        case BF_ENTITY:
        case BF_COMPONENT:
        case BF_COMPONENT_READ:
        case BF_COLUMN:
        case BF_COLUMN_TRY:
        case BF_ROW:
        case BF_RETRIGGER:
        case BF_STORE:
        case BF_STORE_VALUE:
        case BF_STORE_HEADER:
        case BF_STORE_TYPE_NAME:
        case BF_COMPONENT_WRITE:
        case BF_COMMAND_ARGUMENT:
        case BF_COMMAND_OUTPUT:
        case BF_COMMAND:
        case BF_COMMAND_BEGIN:
        case BF_SUSPEND:
        case BF_DELAY:
        case BF_INTERN_STRING: return false;
        default: return true;
    }
}

int behavior_function_run_interpreted(const behavior_function *f, behavior_function_context *ctx)
{
    behavior_run_record(EXECUTION_RUN_BYTECODE);
    execution_machine machine;
    execution_machine_begin(&machine, f, ctx, true);
    int instruction_count = acount(machine.instructions);
    int result = FLOW_OK;
    for (int pc = machine.pc; pc < instruction_count; pc = machine.pc) {
        const char *failure = behavior_execution_scope_step();
        if (!failure && behavior_execution_scope_pure() &&
            !pure_operation(machine.instructions[pc].op)) {
            failure = "operation is unavailable during pure evaluation";
        }
        if (failure) {
            result = execution_machine_fail(&machine, failure);
            break;
        }
        result = execution_machine_step(&machine, pc);
        if (result != EXECUTION_CONTINUE) {
            break;
        }
        result = FLOW_OK;
    }
    execution_machine_end(&machine, result);
    return result;
}
