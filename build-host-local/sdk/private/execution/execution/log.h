#ifndef CHIBI_EXECUTION_LOG_H
#define CHIBI_EXECUTION_LOG_H
#include "execution/function.h"

#define EXECUTION_LOG_SERVICE "execution.log"
typedef struct
{
    uint64_t owner, node_id;
    double now;
    const char *program_name, *text;
    size_t text_length;
} execution_log_message;
/* Messages and context metadata are borrowed for the synchronous callback.
 * A host that retains text must copy it. Logging never suspends execution. */
typedef struct
{
    void (*emit)(void *user, const execution_log_message *message);
    void *user;
} execution_log_service;
const char *execution_print_string(const behavior_function_context *, void *, const pin_value *, pin_value *);
extern const node_host_call EXECUTION_PrintString;
#endif
