#pragma once

#include "runtime/material_mask.h"

#include <stdint.h>

typedef struct material_destruction material_destruction;
typedef struct
{
    float x, y;
} material_point;

enum
{
    MATERIAL_DAMAGE_CONTACT,
    MATERIAL_DAMAGE_BLAST
};

/* What struck the material: the color of the source's light and its heat.
 * Zero is a cold, colorless impact. Carried through to results. */
typedef struct
{
    unsigned color;
    float heat;
} material_impact;

typedef struct
{
    uint64_t target;
    material_point point;
    float radius, energy;
    int cause;
    material_impact impact;
} material_damage;

/* Borrowed for one operation. Host columns may move when fragments are created;
 * the mask itself must remain at a stable address until destroy_source. */
typedef struct
{
    material_mask *mask;
    material_point position;
    float angle, hardness, blast_multiplier, release_fraction;
    float chip_speed, fragment_speed, blast_speed, release_speed;
    int minimum, initial_cells;
    bool anchored, retain_chips;
} material_source;

typedef struct
{
    material_mask mask; /* Ownership transfers to create_fragment. */
    material_point hit;
    float speed;
} material_fragment;

typedef enum
{
    MATERIAL_EVENT_DAMAGE,
    MATERIAL_EVENT_FRAGMENT,
    MATERIAL_EVENT_RELEASE,
    MATERIAL_EVENT_BODY_IMPACT,
} material_event_kind;

typedef struct
{
    material_event_kind kind;
    uint64_t target;
    material_point point;
    float energy;
    int cause, removed, dust;
    material_impact impact;
} material_event;

typedef struct
{
    void *user;
    bool (*read_source)(void *user, uint64_t target, material_source *source);
    void (*create_fragment)(void *user, uint64_t source, material_fragment fragment);
    void (*destroy_source)(void *user, uint64_t source);
    void (*invalidate_geometry)(void *user, uint64_t source);
    void (*result)(void *user, const material_event *event);
    /* Optional notification for each disk that removed cells, before fragment
     * callbacks. Copy the disk if needed; do not invalidate source masks here. */
    void (*cut_surface)(void *user, const material_damage *damage);
} material_host;

material_destruction *material_destruction_new();
void material_destruction_free(material_destruction *destruction);
void material_destruction_reset(material_destruction *destruction);
void material_destruction_queue_damage(material_destruction *destruction, material_damage damage);
void material_destruction_queue_release(material_destruction *destruction, uint64_t target);
/* Called outside physics integration and ECS queries. Host callbacks may enqueue
 * damage/releases. Releases queued during release callbacks run next step;
 * damage-phase releases still run this step. Recursive step, reset or free on
 * the same destruction owner during a callback is unsupported. */
void material_destruction_step(
    material_destruction *destruction,
    const material_host *host,
    int fragment_capacity
);
bool material_destruction_poll_event(material_destruction *destruction, material_event *event);
void material_destruction_emit_event(material_destruction *destruction, material_event event);
uint64_t material_destruction_hash(const material_destruction *destruction, uint64_t hash);
