#ifndef CHIBI_EXECUTION_INSPECTION_H
#define CHIBI_EXECUTION_INSPECTION_H
#include "ecs/ecs.h"
#include "execution/module.h"

typedef struct execution_inspector execution_inspector;
typedef enum
{
    EXECUTION_DEBUG_RESUME,
    EXECUTION_DEBUG_INTO,
    EXECUTION_DEBUG_OVER,
    EXECUTION_DEBUG_OUT
} execution_debug_control;
typedef enum
{
    EXECUTION_WATCH_AVAILABLE,
    EXECUTION_WATCH_UNCONSUMED_REFERENCE,
    EXECUTION_WATCH_BUDGET,
    EXECUTION_WATCH_UNSUPPORTED
} execution_watch_availability;
typedef struct
{
    uint64_t generation, run, step, module, function, owner;
    uint64_t query_variables[ECS_QUERY_VARS];
    behavior_source_point point;
    int depth, status;
    bool retired;
} execution_debug_cursor;
typedef struct
{
    uint64_t generation, run, module, function, owner;
    uint64_t query_variables[ECS_QUERY_VARS];
    behavior_source_watch source;
    pin_value value;
    execution_watch_availability availability;
} execution_debug_watch;
typedef struct
{
    uint64_t module, function;
    int depth;
    behavior_source_point point;
} execution_debug_frame;
typedef struct
{
    uint64_t generation, owner, module, function, occurrence;
    uint64_t query_variables[ECS_QUERY_VARS];
    uint64_t from_module, from_function, from_occurrence;
    uint64_t count;
    double time;
} execution_debug_activity;
/* Enables recording for subsequent evaluations and returns an owned handle.
 * Snapshots remain valid on a retained handle after replacement/destruction.
 * Each handle retains at most 512 values and 4 MiB of snapshot allocation. */
execution_inspector *execution_program_inspector(execution_program *);
/* Borrowed existing handle; does not enable recording. */
execution_inspector *execution_program_inspection(const execution_program *);
/* Recording controls activity/value snapshots without clearing retained rows
 * or changing breakpoints. Owner zero records all owners when enabled. */
void execution_inspector_recording(execution_inspector *, bool enabled, uint64_t owner);
bool execution_inspector_records(const execution_inspector *, uint64_t owner);
bool execution_program_paused(const execution_program *);
int execution_program_owner_count(const execution_program *);
uint64_t execution_program_owner_at(const execution_program *, int);
int behavior_function_source_point_count(const behavior_function *);
behavior_source_point behavior_function_source_point_at(const behavior_function *, int);
int behavior_function_source_watch_count(const behavior_function *);
behavior_source_watch behavior_function_source_watch_at(const behavior_function *, int);
execution_inspector *execution_inspector_retain(execution_inspector *);
void execution_inspector_free(execution_inspector *);
execution_debug_cursor execution_inspector_cursor(const execution_inspector *);
/* Keeps the selected owner's last cursor/stack while siblings run or retire.
 * Multi-target owners expose their last tuple in cursor.query_variables;
 * watch and activity rows retain distinct tuples. Pause selects the next
 * matching owner activation and subsequent controls address that exact run.
 * At most 64 owner snapshots are retained; selection pins its cursor/stack and
 * existing watch rows against eviction by sibling owners.
 * Owner zero clears selection. Controls address the sole paused activation. */
bool execution_inspector_select_owner(execution_inspector *, uint64_t);
bool execution_inspector_owner_cursor(const execution_inspector *, uint64_t, execution_debug_cursor *);
int execution_inspector_owner_frame_count(const execution_inspector *, uint64_t);
execution_debug_frame execution_inspector_owner_frame_at(const execution_inspector *, uint64_t, int);
int execution_inspector_watch_count(const execution_inspector *);
const execution_debug_watch *execution_inspector_watch_at(const execution_inspector *, int);
int execution_inspector_frame_count(const execution_inspector *);
execution_debug_frame execution_inspector_frame_at(const execution_inspector *, int);
int execution_inspector_activity_count(const execution_inspector *);
execution_debug_activity execution_inspector_activity_at(const execution_inspector *, int);
uint64_t execution_inspector_activity_dropped(const execution_inspector *);
/* Enumerates concrete owners, including automatic query instances. */
int execution_inspector_owner_count(const execution_inspector *);
uint64_t execution_inspector_owner_at(const execution_inspector *, int);
bool execution_inspector_pause(execution_inspector *, uint64_t owner);
int execution_inspector_control(execution_inspector *, execution_debug_control);
bool execution_inspector_breakpoint(
    execution_inspector *,
    uint64_t module,
    uint64_t function,
    uint64_t occurrence,
    bool enabled
);
#endif
