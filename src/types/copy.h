#ifndef CHIBI_TYPES_COPY_H
#define CHIBI_TYPES_COPY_H

#include <stdbool.h>
#include <stdint.h>

/* Selected managed-value C copy operations, not all runtime memory traffic.
 * Text/name bytes include NUL; record bytes include inline fields and padding.
 * Descriptor operations and borrowed descriptor-buffer bytes are separate from
 * payload. Buffer bytes also include packed fixed-width scalar array payload
 * writes, which move no element descriptors. Completed copies remain counted
 * when a later validation fails.
 * Descriptor sites: push destination, owning/heap/arena value copy, direct heap
 * union copy and layout-aware reference-root snapshot. Payload leaves include
 * their text/nominal/record copies and the owned-set name. The task-result
 * wrapper's borrowed buffer is separate. Other assignments, BF_COPY_STRING,
 * ECS row publication, serializers, allocator movement and arbitrary host
 * copies are excluded. These are C operation spans, not hardware bandwidth.
 * See docs/evidence/runtime-attribution/design.md for the exact inventory. */
typedef struct
{
    uint64_t text_bytes;
    uint64_t nominal_bytes;
    uint64_t record_bytes;
    uint64_t value_buffer_bytes;
    uint64_t value_header_moves;
    bool overflow;
} pin_copy_counts;

typedef struct pin_copy_scope
{
    pin_copy_counts counts;
    struct pin_copy_scope *previous;
    bool active;
} pin_copy_scope;

/* Caller-owned, initially zeroed, same-thread scopes. Only the current (inner)
 * scope receives events. Begin/end preserve counts; reset requires inactivity.
 * Reentry uses the current scope; workers do not inherit it. No simultaneous
 * access, cross-thread end, or nonlocal exit past end. Inactive transfer requires
 * caller synchronization. Counts saturate at UINT64_MAX with sticky overflow;
 * totals/merges need checked arithmetic, and saturated deltas are not exact. */
void pin_copy_scope_begin(pin_copy_scope *);
void pin_copy_scope_end(pin_copy_scope *);
void pin_copy_scope_reset(pin_copy_scope *);

#endif
