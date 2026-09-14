#ifndef CHIBI_BEHAVIOR_STATE_H
#define CHIBI_BEHAVIOR_STATE_H

typedef struct
{
    int index, outputs;
} behavior_state_selection;
enum
{
    BEHAVIOR_STATE_RESET = 1,
    BEHAVIOR_STATE_LOOP = 2,
    BEHAVIOR_STATE_RANDOM = 4
};
/* Instance-owned selection masks survive calls and waits. This operation never
 * evaluates a graph, retains arguments or changes component storage. */
int behavior_instance_select_state(
    void *owner,
    const behavior_state_selection *selection,
    int start,
    int flags
);
#endif
