#include "execution/task.h"

const node_command EXECUTION_Yield = { .intrinsic = NODE_COMMAND_YIELD };
const node_command EXECUTION_TaskJoin = { .intrinsic = NODE_COMMAND_TASK_JOIN };
const node_command EXECUTION_TaskCancel = { .intrinsic = NODE_COMMAND_TASK_CANCEL };
const node_command EXECUTION_TaskRunning = { .intrinsic = NODE_COMMAND_TASK_RUNNING };
const node_command EXECUTION_TaskCancelAfter = { .intrinsic = NODE_COMMAND_TASK_CANCEL_AFTER };

static const struct
{
    uint64_t id;
    const node_command *command;
} commands[] = {
    { EXECUTION_YIELD_ID, &EXECUTION_Yield },
    { EXECUTION_TASK_JOIN_ID, &EXECUTION_TaskJoin },
    { EXECUTION_TASK_CANCEL_ID, &EXECUTION_TaskCancel },
    { EXECUTION_TASK_RUNNING_ID, &EXECUTION_TaskRunning },
    { EXECUTION_TASK_CANCEL_AFTER_ID, &EXECUTION_TaskCancelAfter },
};

const node_command *execution_task_command(uint64_t id, uint64_t abi)
{
    for (size_t i = 0;
         abi == EXECUTION_TASK_ABI && i < sizeof commands / sizeof *commands;
         i++) {
        if (commands[i].id == id) {
            return commands[i].command;
        }
    }
    return NULL;
}
uint64_t execution_task_command_id(const node_command *command)
{
    for (size_t i = 0; i < sizeof commands / sizeof *commands; i++) {
        if (commands[i].command == command) {
            return commands[i].id;
        }
    }
    return 0;
}
