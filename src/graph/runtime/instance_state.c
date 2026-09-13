#include "graph/runtime/instance.h"
#include "graph/runtime/state.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

int behavior_instance_select_state(void *owner, const behavior_state_selection *selection, int start, int flags)
{
    behavior_instance *bi = owner;
    assert(selection->index >= 0 && selection->index < acount(bi->node_states));
    int count = selection->outputs;
    assert(count >= 0);
    pin_value *state = &bi->node_states[selection->index];
    if (flags & BEHAVIOR_STATE_RESET) {
        if (state->type == PIN_STR) {
            memset((char *)state->str, '0', (size_t)count);
        }
        return -1;
    }
    if (state->type != PIN_STR) {
        char *used = xmalloc((size_t)count + 1);
        memset(used, '0', (size_t)count);
        used[count] = '\0';
        apush(bi->strings, used);
        *state = (pin_value){ .type = PIN_STR, .str = used };
    }
    char *used = (char *)state->str;
    int remaining = 0;
    for (int i = 0; i < count; i++) {
        remaining += used[i] == '0';
    }
    if (!remaining && !(flags & BEHAVIOR_STATE_LOOP)) {
        return -1;
    }
    if (!remaining) {
        memset(used, '0', (size_t)count);
        remaining = count;
    }
    if (!remaining) {
        return -1;
    }
    bool valid = start >= 0 && start < count;
    int output = valid ? start : 0;
    int choice = flags & BEHAVIOR_STATE_RANDOM
                   ? (int)((double)rand() / ((double)RAND_MAX + 1) * remaining)
                   : 0;
    /* The forced first output still consumes the random draw above. */
    if (remaining == count && valid) {
        choice = 0;
    }
    for (int i = 0; i < count; i++) {
        if (used[output] == '0' && choice-- == 0) {
            used[output] = '1';
            return output;
        }
        if (++output == count) {
            output = 0;
        }
    }
    assert(false);
    return -1;
}
