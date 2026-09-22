#pragma once
#include "runtime/systems/system.h"

typedef enum
{
    SYSTEM_PRE_PHYSICS,
    SYSTEM_POST_PHYSICS,
    SYSTEM_POST_EVENTS,
    SYSTEM_PRESENTATION,
    SYSTEM_PHASE_COUNT
} system_phase;
typedef int (*engine_system_fn)(ecs_world *, double time, double delta, void *, flow_error *);

typedef struct
{
    const char *name;
    system_phase phase;
    int order;
    const ecs_query_expr *query;
    const uint64_t *reads, *writes;
    int read_count, write_count;
    const char *const *resource_reads, *const *resource_writes;
    int resource_read_count, resource_write_count;
    const char *const *after;
    int after_count;
    bool declared, structural, host_sink, deterministic, parallel_safe;
    game_system *graph; /* Borrowed. Effects are inferred from its compiler. */
    engine_system_fn run;
    void *user;
} engine_system_desc;

typedef struct system_schedule system_schedule;
system_schedule *system_schedule_new(ecs_world *world);
void system_schedule_free(system_schedule *schedule);
/* Replace by name. Descriptors, query, and effect declarations are copied.
 * Each graph must have one registration; re-register after graph edits.
 * Native callbacks own query execution. Parallel callbacks must use stable
 * prebound data without opening ECS queries or mutating membership. */
int system_schedule_register(system_schedule *schedule, const engine_system_desc *desc);
void system_schedule_remove(system_schedule *schedule, const char *name);
bool system_schedule_build(system_schedule *schedule);
int system_schedule_run(system_schedule *schedule, system_phase phase, double time, double delta);
const char *system_schedule_error(const system_schedule *schedule);
const char *system_schedule_warning(const system_schedule *schedule);
void system_schedule_set_parallel(system_schedule *schedule, bool parallel);
int system_schedule_count(const system_schedule *schedule);
const char *system_schedule_at(const system_schedule *schedule, int position);
int system_schedule_parallel_batches(const system_schedule *schedule);

bool system_schedule_dependency(const system_schedule *schedule, const char *before, const char *after);
