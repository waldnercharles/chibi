#ifndef CHIBI_EXECUTION_TASK_H
#define CHIBI_EXECUTION_TASK_H
#include "execution/host.h"
/* Built-in task commands. Yield takes no arguments and completes on a later
 * update. Join, Cancel and Running take a task handle. Join also takes the
 * success type tag and returns T | Cancelled. CancelAfter takes a handle and
 * seconds since task start; Running returns one bool. */
#define EXECUTION_YIELD_ID UINT64_C(0x4348494900000102)
#define EXECUTION_TASK_JOIN_ID UINT64_C(0x4348494900000103)
#define EXECUTION_TASK_CANCEL_ID UINT64_C(0x4348494900000104)
#define EXECUTION_TASK_RUNNING_ID UINT64_C(0x4348494900000105)
#define EXECUTION_TASK_CANCEL_AFTER_ID UINT64_C(0x4348494900000106)
#define EXECUTION_TASK_ABI UINT64_C(2)
extern const node_command EXECUTION_Yield, EXECUTION_TaskJoin,
    EXECUTION_TaskCancel, EXECUTION_TaskRunning, EXECUTION_TaskCancelAfter;
const node_command *execution_task_command(uint64_t id, uint64_t abi);
uint64_t execution_task_command_id(const node_command *);
#endif
