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
} schedule_error;
typedef int (*schedule_run_fn)(ecs_world *, double, double, void *, schedule_error *);
typedef struct
{
    const char *name;
    system_phase phase;
    int order;
    const uint64_t *reads, *writes, *reduces;
    int read_count, write_count, reduce_count;
    const char *const *resource_reads, *const *resource_writes;
    int resource_read_count, resource_write_count;
    const char *const *after;
    int after_count;
    bool declared, structural, host_sink, world_read, deterministic, parallel_safe;
    /* Mutable execution storage may appear in only one registration. NULL
     * identifies stateless native callbacks. The scheduler borrows all hooks
     * and user storage; copied names/access declarations own their snapshots. */
    const void *identity;
    schedule_run_fn run;
    /* Optional row executor. Called only by the submitting thread for a
     * single-system batch, never from a system worker. */
    int (*run_parallel)(void *, const task_system *, schedule_error *);
    int (*prepare)(void *, schedule_error *);
    /* Called serially in schedule order after the batch has joined, before
     * the next batch prepares. commit is false after preparation/execution
     * failure, including an earlier peer's failure. Only commit=true may
     * publish successful work or retain a paused dispatch; false discards it. */
    int (*finish)(void *, bool commit, schedule_error *);
    /* Optional serial check of current instrumentation/runtime barriers. */
    bool (*parallel_ready)(void *);
    int (*maintain)(void *, double, double, schedule_error *);
    int (*field_changed)(void *, ecs_entity, ecs_component_id, size_t, pin_value, schedule_error *);
    void *user;
} schedule_desc;

typedef struct schedule schedule;
/* Borrowed executor must outlive the schedule. NULL runs serially. */
schedule *schedule_new(ecs_world *, const task_system *);
void schedule_free(schedule *);
int schedule_register(schedule *, const schedule_desc *);
void schedule_remove(schedule *, const char *);
/* Quiescent scene/reset boundary: forget an unfinished phase cursor and
 * rebuild the existing registrations on the next dispatch. */
void schedule_reset(schedule *);
/* Whether an unfinished phase still has a valid resume cursor. */
bool schedule_paused(const schedule *);
bool schedule_build(schedule *);
int schedule_maintain(schedule *, double, double, schedule_error *);
int schedule_field_changed(schedule *, ecs_entity, ecs_component_id, size_t, pin_value, schedule_error *);
int schedule_run(schedule *, system_phase, double, double);
const char *schedule_error_message(const schedule *);
const char *schedule_warning(const schedule *);
void schedule_set_parallel(schedule *, bool);
int schedule_count(const schedule *);
const char *schedule_at(const schedule *, int);
/* Borrowed copied registration until the next register/remove/free. Indexes
 * use the built schedule order returned by count/at. */
const schedule_desc *schedule_descriptor_at(const schedule *, int);
int schedule_parallel_batches(const schedule *);
/* Dispatch scratch retained between runs; excludes in-flight temporary storage.
 * At most 128 batch entries are retained; larger and recursive runs use temporary storage. */
size_t schedule_scratch_bytes(const schedule *);
bool schedule_dependency(const schedule *, const char *, const char *);
#endif
