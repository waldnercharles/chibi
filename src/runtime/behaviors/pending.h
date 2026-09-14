#ifndef CHIBI_BEHAVIOR_PENDING_H
#define CHIBI_BEHAVIOR_PENDING_H
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/* Ordered attachment membership. Different instances may update bits in the
 * same word during parallel pumping. Resize/reset only between executions. */
typedef struct
{
    _Atomic uint64_t *words;
    int word_count;
    _Atomic int count;
} behavior_pending;
void behavior_pending_reserve(behavior_pending *pending, int attachments);
void behavior_pending_reset(behavior_pending *pending);
void behavior_pending_set(behavior_pending *pending, int index, bool present);
int behavior_pending_next(const behavior_pending *pending, int start);
#endif
