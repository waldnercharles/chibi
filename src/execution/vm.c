#include "execution/machine.h"
#include "execution/runner.h"
#include "execution/status.h"
#include "support/array.h"

int behavior_function_run_interpreted(const behavior_function *f, behavior_function_context *ctx)
{
    behavior_run_record(EXECUTION_RUN_BYTECODE);
    execution_machine machine;
    execution_machine_begin(&machine, f, ctx, true);
    int instruction_count = acount(machine.instructions);
    int result = FLOW_OK;
    for (int pc = machine.pc; pc < instruction_count; pc = machine.pc) {
        result = execution_machine_step(&machine, pc);
        if (result != EXECUTION_CONTINUE) {
            break;
        }
        result = FLOW_OK;
    }
    execution_machine_end(&machine, result);
    return result;
}
