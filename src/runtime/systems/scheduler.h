#ifndef CHIBI_RUNTIME_SCHEDULER_H
#define CHIBI_RUNTIME_SCHEDULER_H

#include "ecs/ecs.h"
#include "runtime/tasks.h"
#include "types/value.h"

/* Names/order are shared by native and source programs. */
typedef enum
{
    SYSTEM_PRE_PHYSICS,
    SYSTEM_POST_PHYSICS,
    SYSTEM_POST_EVENTS,
    SYSTEM_PRESENTATION,
    SYSTEM_PHASE_COUNT
} system_phase;

typedef struct
{
    int status;
    char message[192];
} game_schedule_error;
typedef int (*game_schedule_run_fn)(ecs_world *, double, double, void *, game_schedule_error *);
typedef struct
{
    const char *name;
    system_phase phase;
    int order;
    const uint64_t *reads, *writes;
    int read_count, write_count;
    const char *const *resource_reads, *const *resource_writes;
    int resource_read_count, resource_write_count;
    const char *const *after;
    int after_count;
    bool declared, structural, host_sink, world_read, deterministic, parallel_safe;
    /* Mutable execution storage may appear in only one registration. NULL
     * identifies stateless native callbacks. The scheduler borrows all hooks
     * and user storage; copied names/access declarations own their snapshots. */
    const void *identity;
    game_schedule_run_fn run;
    int (*prepare)(void *, game_schedule_error *);
    /* Optional serial check of current instrumentation/runtime barriers. */
    bool (*parallel_ready)(void *);
    int (*maintain)(void *, double, double, game_schedule_error *);
    int (*field_changed)(void *, ecs_entity, ecs_component_id, size_t, pin_value, game_schedule_error *);
    void *user;
} game_schedule_desc;

typedef struct game_schedule game_schedule;
/* Borrowed executor must outlive the schedule. NULL runs serially. */
game_schedule *game_schedule_new(ecs_world *, const game_task_system *);
void game_schedule_free(game_schedule *);
int game_schedule_register(game_schedule *, const game_schedule_desc *);
void game_schedule_remove(game_schedule *, const char *);
bool game_schedule_build(game_schedule *);
int game_schedule_maintain(game_schedule *, double, double, game_schedule_error *);
int game_schedule_field_changed(game_schedule *, ecs_entity, ecs_component_id, size_t, pin_value, game_schedule_error *);
int game_schedule_run(game_schedule *, system_phase, double, double);
const char *game_schedule_error_message(const game_schedule *);
const char *game_schedule_warning(const game_schedule *);
void game_schedule_set_parallel(game_schedule *, bool);
int game_schedule_count(const game_schedule *);
const char *game_schedule_at(const game_schedule *, int);
/* Borrowed copied registration until the next register/remove/free. Indexes
 * use the built schedule order returned by count/at. */
const game_schedule_desc *game_schedule_descriptor_at(const game_schedule *, int);
int game_schedule_parallel_batches(const game_schedule *);
bool game_schedule_dependency(const game_schedule *, const char *, const char *);
#endif
