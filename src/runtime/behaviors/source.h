#ifndef CHIBI_SOURCE_H
#define CHIBI_SOURCE_H

#include "ecs/ecs.h"
#include "execution/module.h"
#include "execution/native.h"
#include "runtime/event_catalog.h"

typedef struct source_registry source_registry;
typedef struct source_staging source_staging;
typedef struct
{
    const char *name;
    /* NULL registers a new name; otherwise names an existing registration.
     * A different name renames that registration. NULL module removes it. */
    const char *previous_name;
    execution_module *module;
    bool restart;
} source_publication;
source_registry *source_registry_new(ecs_world *);
ecs_world *source_world(const source_registry *);
/* True only when a program has a suspended debugger activation. */
bool source_paused(const source_registry *);
void source_registry_free(source_registry *);
/* Retains a standalone original declaration generation at a quiescent boundary.
 * Incompatible live executable declarations reject without changing either.
 * Later publications validate against this same generation before commit. */
int source_set_events(source_registry *, event_catalog *);
const event_catalog *source_events(const source_registry *);
/* Services are NULL or a support/array.h vector (apush/acount). Rows are copied
 * into new programs; the vector and names may retire after return. Host values
 * remain borrowed for the resulting program lifetimes.
 * Explicit rows override registry defaults; NULL uses defaults alone. */
int source_register(source_registry *, const char *, execution_module *, const service_binding *);
int source_replace(source_registry *, const char *, execution_module *, bool restart);
/* One boundary: preflight the complete final module/import/event/service set,
 * stage compatible state, then commit without dispatch or Begin. Failure leaves
 * names, attachments, active work and state untouched. Entries borrow modules.
 * Removing and swapping names is supported within one complete batch. Modules
 * require persistent capability identities/ABIs: canonical prepared records
 * establish generation equality and bound the complete retained closure. */
/* A successful stage owns copied names and unpublished programs. Exactly one
 * stage may exist per registry; execution, attachments, services and publication
 * remain locked until consumed. Registry/ECS and borrowed service objects must
 * stay quiescent and alive. Prepare every involved world before committing any.
 * Commit performs no rejecting operation or dispatch; discard preserves the
 * registered generations, state and pending work. Services are NULL or a
 * support/array.h vector, never fixed/sentinel arrays. New registrations copy
 * rows and names, and borrow host values. New programs use registry defaults
 * then explicit overrides by name. Replacements retain current bindings, fill
 * absent names from defaults, then apply explicit overrides. The input vector
 * and names may retire after stage returns, while borrowed values outlive the
 * staged and committed programs. */
source_staging *source_stage(
    source_registry *,
    const source_publication *,
    int count,
    const service_binding *,
    char *error,
    size_t size
);
void source_commit(source_staging *);
void source_discard(source_staging *);
int source_publish(
    source_registry *,
    const source_publication *,
    int count,
    const service_binding *,
    char *error,
    size_t size
);
/* Binds each native library manifest to the registered module that defines
 * its functions, and makes every registered program call the bound entries.
 * Call once, after the last publication and before any attachment; the
 * manifests outlive the registry. A manifest that no module defines, or that
 * does not bind, fails with why and leaves the programs in the VM. */
int source_bind_natives(
    source_registry *,
    const execution_native_manifest *const *,
    int count,
    char *error,
    size_t size
);
int source_remove(source_registry *, const char *);
bool source_registered(const source_registry *, const char *);
/* Whether a registered source declares an event usable by current attachments
 * or an active system. Unattached behavior/library declarations do not count. */
bool source_has_event(const source_registry *, const char *);
/* Referenced typed event declarations from one published generation. Returned
 * names/defaults borrow that generation until the next publication/removal.
 * Unhandled standalone events belong to the prepared project's event catalog. */
bool source_event(const source_registry *, const char *, execution_module_event_info *);
bool source_event_field(const source_registry *, const char *, int, execution_module_event_field_info *);
int source_attach(source_registry *, ecs_entity, const char *, bool deferred, double now, double delta);
int source_set_attachments(
    source_registry *,
    ecs_entity,
    const char *const *,
    int,
    bool deferred,
    double now,
    double delta
);
int source_detach(source_registry *, ecs_entity, const char *);
int source_detach_entity(source_registry *, ecs_entity);
int source_begin(source_registry *, double now, double delta);
int source_dispatch(source_registry *, ecs_entity, const char *, const pin_value *, int, double now, double delta);
/* Snapshot/deliver an exact component edge to qualified observers. Generic
 * event dispatch remains independent; nested notifications defer safely. */
int source_field_changed(
    source_registry *,
    ecs_entity,
    ecs_component_id,
    size_t offset,
    pin_value,
    double now,
    double delta
);
int source_dispatch_all(source_registry *, const char *, const pin_value *, int, double now, double delta);
/* Default stepping runs every registered system. An explicit engine selection
 * instead runs systems only through run_system at their authored phases. */
bool source_is_system(const source_registry *, const char *);
int source_select_systems(source_registry *, const char *const *names, int count);
int source_run_system(source_registry *, const char *, double now, double delta);
/* The phase cohort holds registrations and services fixed. Prepare each index
 * serially just before its scheduler batch, after preceding batches commit;
 * this discovers current membership and evaluates current value filters.
 * Distinct prepared indices may run concurrently only when their verified
 * effects are independent and parallel(index) is true. Instrumentation forces
 * serial execution. Finish each index serially in schedule order after joining
 * all workers, committing only successful predecessors and the current success.
 * Passing true to finish_index also retains its own paused dispatch without
 * publishing it; false cancels the dispatch even when it paused.
 * Finish the cohort at a quiescent boundary; remaining work is discarded. */
typedef struct source_system_batch source_system_batch;
#define SOURCE_SYSTEM_BATCH_MAX 1024
source_system_batch *source_systems_prepare(
    source_registry *,
    const char *const *,
    int count,
    double now,
    double delta
);
int source_systems_prepare_index(source_system_batch *, int index, char *error, size_t size);
bool source_systems_parallel(const source_system_batch *, int index);
int source_systems_run(source_system_batch *, int index, char *error, size_t size);
int source_systems_run_parallel(
    source_system_batch *,
    int index,
    const task_system *,
    char *error,
    size_t size
);
int source_systems_finish_index(source_system_batch *, int index, bool commit, char *error, size_t size);
int source_systems_finish(source_system_batch *);
/* Quiescent scene/reset boundary; discard retained paused system dispatches. */
void source_cancel_systems(source_registry *);

int source_maintain(source_registry *, double now, double delta);
int source_step(source_registry *, double now, double delta);
int source_pump(source_registry *, double now, double delta);
execution_program *source_program(source_registry *, const char *);
/* Name is consumed during the call; host values remain borrowed. Active
 * callbacks keep the old value until each program reaches its safe boundary.
 * This mutation is serial; it cannot run from a parallel-safe system cohort. */
/* Owns a copied name and borrows the host value until rebound or registry_free.
 * Defaults apply to future publication; explicit publication bindings override
 * them by name. Existing programs rebind at their safe execution boundary.
 * A prior value used by an active callback must survive until that boundary.
 * Rejection leaves all current bindings and defaults unchanged. */
int source_bind_service(source_registry *, const char *, void *);
const char *source_error(const source_registry *);
/* Explicit behavior attachments, distinct from automatic system membership.
 * Info and its borrowed pointers end at any registry/attachment mutation.
 * Sequence stays stable across ordering, publication and declaration rename. */
typedef struct
{
    ecs_entity entity;
    uint64_t sequence;
    const char *name;
    const execution_module *module;
    execution_program *program;
    execution_instance *instance;
    int begin; /* 0 pending, 1 started, -1 failed */
} source_attachment_info;
int source_attachment_count(const source_registry *);
source_attachment_info source_attachment_at(const source_registry *, int);
#define SOURCE_PROFILE_ROWS_MAX 256
/* Function-origin samples use inclusive process CPU time: child intervals
 * overlap their parents. Rows own a retained module generation and copied
 * registration name. Borrowed row/text pointers survive publication/removal,
 * but end at reset, enable(true), or registry free. Missing origin is NULL.
 * Entry/resume counts refer to actual activation execution, excluding wait
 * polling. Outlined synchronous calls are included in their parent sample.
 * Internal evaluators have internal=true/function_id=0; kind is then unused.
 * Profiling defaults off; disabled execution never reads the profile clock.
 * Mutation requires a quiescent registry with no outstanding publication. */
typedef struct
{
    uint64_t module_id, function_id, owner, entries, resumes;
    const char *name, *source;
    int start, end, line, column, last_status;
    execution_function_kind kind;
    double cpu_seconds;
    bool cpu_available, internal;
} source_profile_row;
void source_profile_enable(source_registry *, bool enabled);
void source_profile_reset(source_registry *);
int source_profile_count(const source_registry *);
const source_profile_row *source_profile_at(const source_registry *, int index);
uint64_t source_profile_dropped(const source_registry *);

#endif
