#ifndef CHIBI_GAME_SOURCE_H
#define CHIBI_GAME_SOURCE_H

#include "ecs/ecs.h"
#include "execution/module.h"
#include "runtime/event_catalog.h"

typedef struct game_source_registry game_source_registry;
typedef struct game_source_staging game_source_staging;
typedef struct
{
    const char *name;
    /* NULL registers a new name; otherwise names an existing registration.
     * A different name renames that registration. NULL module removes it. */
    const char *previous_name;
    execution_module *module;
    bool restart;
} game_source_publication;
game_source_registry *game_source_registry_new(ecs_world *);
ecs_world *game_source_world(const game_source_registry *);
/* True only when a program has a suspended debugger activation. */
bool game_source_paused(const game_source_registry *);
void game_source_registry_free(game_source_registry *);
/* Retains a standalone original declaration generation at a quiescent boundary.
 * Incompatible live executable declarations reject without changing either.
 * Later publications validate against this same generation before commit. */
int game_source_set_events(game_source_registry *, game_event_catalog *);
const game_event_catalog *game_source_events(const game_source_registry *);
/* Services are NULL or a support/array.h vector (apush/acount). Rows are copied
 * into new programs; the vector and names may retire after return. Host values
 * remain borrowed for the resulting program lifetimes.
 * Explicit rows override registry defaults; NULL uses defaults alone. */
int game_source_register(game_source_registry *, const char *, execution_module *, const service_binding *);
int game_source_replace(game_source_registry *, const char *, execution_module *, bool restart);
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
game_source_staging *game_source_stage(
    game_source_registry *,
    const game_source_publication *,
    int count,
    const service_binding *,
    char *error,
    size_t size
);
void game_source_commit(game_source_staging *);
void game_source_discard(game_source_staging *);
int game_source_publish(
    game_source_registry *,
    const game_source_publication *,
    int count,
    const service_binding *,
    char *error,
    size_t size
);
int game_source_remove(game_source_registry *, const char *);
bool game_source_registered(const game_source_registry *, const char *);
/* Whether a registered source declares an event usable by current attachments
 * or an active system. Unattached behavior/library declarations do not count. */
bool game_source_has_event(const game_source_registry *, const char *);
/* Referenced typed event declarations from one published generation. Returned
 * names/defaults borrow that generation until the next publication/removal.
 * Unhandled standalone events belong to the prepared project's event catalog. */
bool game_source_event(const game_source_registry *, const char *, execution_module_event_info *);
bool game_source_event_field(const game_source_registry *, const char *, int, execution_module_event_field_info *);
int game_source_attach(game_source_registry *, ecs_entity, const char *, bool deferred, double now, double delta);
int game_source_set_attachments(
    game_source_registry *,
    ecs_entity,
    const char *const *,
    int,
    bool deferred,
    double now,
    double delta
);
int game_source_detach(game_source_registry *, ecs_entity, const char *);
int game_source_detach_entity(game_source_registry *, ecs_entity);
int game_source_begin(game_source_registry *, double now, double delta);
int game_source_dispatch(
    game_source_registry *,
    ecs_entity,
    const char *,
    const pin_value *,
    int,
    double now,
    double delta
);
/* Snapshot/deliver an exact component edge to qualified observers. Generic
 * event dispatch remains independent; nested notifications defer safely. */
int game_source_field_changed(
    game_source_registry *,
    ecs_entity,
    ecs_component_id,
    size_t offset,
    pin_value,
    double now,
    double delta
);
int game_source_dispatch_all(
    game_source_registry *,
    const char *,
    const pin_value *,
    int,
    double now,
    double delta
);
/* Default stepping runs every registered system. An explicit engine selection
 * instead runs systems only through run_system at their authored phases. */
bool game_source_is_system(const game_source_registry *, const char *);
int game_source_select_systems(game_source_registry *, const char *const *names, int count);
int game_source_run_system(game_source_registry *, const char *, double now, double delta);
/* Prepare discovers query membership and creates Begin/system work serially
 * only when the whole cohort is verified synchronous and owner-disjoint.
 * Other cohorts retain serial update order. allow_prepare must be false when
 * an external/native callback in the same phase can run before these systems. Prepare/finish are quiescent
 * registry boundaries. Between them the registry's
 * registrations and services remain fixed. Distinct indices may run once in
 * parallel only when verified module effects permit it and parallel() is true.
 * Profiling, any attached inspector, or any program observer forces serial
 * execution. Run writes only its program/result and caller-local diagnostics. Finish joins
 * no workers: the caller must finish its executor before calling it. */
typedef struct game_source_system_batch game_source_system_batch;
#define GAME_SOURCE_SYSTEM_BATCH_MAX 1024
game_source_system_batch *game_source_systems_prepare(
    game_source_registry *,
    const char *const *,
    int count,
    double now,
    double delta,
    bool allow_prepare
);
bool game_source_systems_parallel(const game_source_system_batch *);
int game_source_systems_run(game_source_system_batch *, int index, char *error, size_t size);
int game_source_systems_finish(game_source_system_batch *);

int game_source_maintain(game_source_registry *, double now, double delta);
int game_source_step(game_source_registry *, double now, double delta);
int game_source_pump(game_source_registry *, double now, double delta);
execution_program *game_source_program(game_source_registry *, const char *);
/* Name is consumed during the call; host values remain borrowed. Active
 * callbacks keep the old value until each program reaches its safe boundary.
 * This mutation is serial; it cannot run from a parallel-safe system cohort. */
/* Owns a copied name and borrows the host value until rebound or registry_free.
 * Defaults apply to future publication; explicit publication bindings override
 * them by name. Existing programs rebind at their safe execution boundary.
 * A prior value used by an active callback must survive until that boundary.
 * Rejection leaves all current bindings and defaults unchanged. */
int game_source_bind_service(game_source_registry *, const char *, void *);
const char *game_source_error(const game_source_registry *);
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
} game_source_attachment_info;
int game_source_attachment_count(const game_source_registry *);
game_source_attachment_info game_source_attachment_at(const game_source_registry *, int);
#define GAME_SOURCE_PROFILE_ROWS_MAX 256
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
} game_source_profile_row;
void game_source_profile_enable(game_source_registry *, bool enabled);
void game_source_profile_reset(game_source_registry *);
int game_source_profile_count(const game_source_registry *);
const game_source_profile_row *game_source_profile_at(const game_source_registry *, int index);
uint64_t game_source_profile_dropped(const game_source_registry *);

#endif
