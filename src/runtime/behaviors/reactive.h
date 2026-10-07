#ifndef CHIBI_REACTIVE_H
#define CHIBI_REACTIVE_H
#include "ecs/ecs.h"
#include "graph/runtime/instance.h"

typedef struct game_reactive game_reactive;
typedef struct
{
    int evaluations, writes, events, dormant;
} game_reactive_stats;

/* Group every manager in an engine safe point to diagnose competing owners
 * across attached behaviors and separate systems. Calls must be paired. */
void game_reactive_world_begin(ecs_world *world);
void game_reactive_world_end(ecs_world *world);
game_reactive *game_reactive_new(ecs_world *world);
void game_reactive_free(game_reactive *reactive);
/* One deterministic safe point. Enumerate live instances, then prune removed
 * registrations. Components are reacquired; no column pointers are retained. */
void game_reactive_begin(game_reactive *reactive);
int game_reactive_apply(game_reactive *reactive, behavior_instance *instance, flow_error *error);
/* A suspended query invocation drops change callbacks while still updating
 * relationship values and subscription snapshots; it accumulates no backlog. */
int game_reactive_apply_quiet(game_reactive *reactive, behavior_instance *instance, flow_error *error);
/* Input supplies each materialized edge, including queued focus releases;
 * qualified subscriptions consume it without resampling away an intermediate edge. */
int game_reactive_changed(
    game_reactive *,
    behavior_instance *,
    ecs_entity,
    ecs_component_id,
    size_t,
    pin_value,
    flow_error *
);
void game_reactive_end(game_reactive *reactive);
game_reactive_stats game_reactive_last_update(const game_reactive *reactive);
#endif
