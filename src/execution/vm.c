#include "execution/definition.h"
#include "execution/machine.h"
#include "execution/runner.h"
#include "execution/status.h"
#include "support/array.h"

static bool pure_operation(const behavior_function *f, const behavior_function_instruction *i)
{
    switch (i->op) {
        case BF_INVOKE: return !f->calls[i->left].task;
        case BF_VARIABLE:
        case BF_DELTA:
        case BF_OWNER:
        case BF_HOST_SERVICE:
        case BF_COMMAND_SERVICE:
        case BF_COMMAND_RESULT:
        case BF_RANDOM:
        case BF_RANDOM_RANGE:
        case BF_STORE:
        case BF_STORE_VALUE:
        case BF_STORE_HEADER:
        case BF_STORE_TYPE_NAME:
        case BF_COMMAND_ARGUMENT:
        case BF_COMMAND_OUTPUT:
        case BF_COMMAND:
        case BF_COMMAND_BEGIN:
        case BF_SUSPEND:
        case BF_DELAY: return false;
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
            !pure_operation(machine.function, &machine.instructions[pc])) {
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
