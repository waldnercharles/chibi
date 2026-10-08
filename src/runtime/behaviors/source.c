#include "execution/inspection.h"
#include "execution/status.h"
#include "runtime/behaviors/module_set.h"
#include "source_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void reap_attachments(game_source_registry *);

static void begin_boundary(game_source_registry *r)
{
    assert(!r->staging);
    if (r->executing) {
        return;
    }
    r->error[0] = '\0';
    for (int i = 0; i < acount(r->records); i++) {
        execution_program_clear_error(r->records[i].program);
    }
}
static int program_status(game_source_registry *r, execution_program *program, int status)
{
    if (status >= 0 || r->error[0]) {
        return status;
    }
    const execution_program_failure *failure = execution_program_error(program);
    if (!failure) {
        snprintf(r->error, sizeof r->error, "Source execution failed with status %d", status);
        return status;
    }
    if (failure->line > 0) {
        snprintf(
            r->error,
            sizeof r->error,
            "%s:%d:%d: %s",
            failure->asset_name,
            failure->line,
            failure->column,
            failure->message
        );
    } else {
        snprintf(
            r->error,
            sizeof r->error,
            "%s: %s",
            failure->asset_name,
            failure->message
        );
    }
    return status;
}

static bool same_entity(ecs_entity a, ecs_entity b)
{
    return a.slot == b.slot && a.generation == b.generation;
}
static uint64_t event_id(const source_record *record, const char *name)
{
    return execution_module_event(record->module, name);
}
ecs_world *game_source_world(const game_source_registry *r)
{
    return r->world;
}
game_source_registry *game_source_registry_new(ecs_world *world)
{
    game_source_registry *r = xcalloc(1, sizeof *r);
    r->world = world;
    return r;
}
void game_source_registry_free(game_source_registry *r)
{
    assert(!r->staging);
    game_source_profile_reset(r);
    afree(r->profiles);
    for (int i = 0; i < acount(r->records); i++) {
        execution_program_free(r->records[i].program);
        free(r->records[i].name);
    }
    afree(r->records);
    afree(r->attachments);
    for (int i = 0; i < acount(r->services); i++) {
        free((void *)r->services[i].type_name);
    }
    afree(r->services);
    game_event_catalog_free(r->events);
    free(r);
}
int game_source_set_events(game_source_registry *r, game_event_catalog *events)
{
    assert(r && !r->staging && !r->executing);
    const execution_module **modules = NULL;
    for (int i = 0; i < acount(r->records); i++) {
        apush(modules, r->records[i].module);
    }
    bool valid = !events || game_event_catalog_validate(
                                events,
                                modules,
                                acount(modules),
                                r->error,
                                sizeof r->error
                            );
    afree(modules);
    if (!valid) {
        return -1;
    }
    if (events) {
        game_event_catalog_retain(events);
    }
    game_event_catalog_free(r->events);
    r->events = events;
    return 0;
}
const game_event_catalog *game_source_events(const game_source_registry *r)
{
    return r->events;
}
bool game_source_registered(const game_source_registry *r, const char *name)
{
    return record_index(r, name) >= 0;
}
bool game_source_has_event(const game_source_registry *r, const char *name)
{
    for (int i = 0; i < acount(r->records); i++) {
        const source_record *record = &r->records[i];
        uint64_t event = event_id(record, name);
        bool handler = false;
        for (int j = 0; event && j < execution_module_function_count(record->module);
             j++) {
            execution_module_function_info
                function = execution_module_function_at(record->module, j);
            if (function.event == event && !function.action && function.body &&
                function.kind != EXECUTION_FUNCTION_SYSTEM) {
                handler = true;
                break;
            }
        }
        if (!handler) {
            continue;
        }
        if (record->system && source_record_active(r, record)) {
            return true;
        }
        for (int j = 0; j < acount(r->attachments); j++) {
            if (r->attachments[j].program == record->program) {
                return true;
            }
        }
    }
    return false;
}
static const execution_module *event_module(const game_source_registry *r, const char *name, int *index)
{
    for (int i = 0; i < acount(r->records); i++) {
        const execution_module *module = r->records[i].module;
        for (int j = 0; j < execution_module_event_count(module); j++) {
            execution_module_event_info event = execution_module_event_at(module, j);
            if (!strcmp(event.name, name)) {
                *index = j;
                return module;
            }
        }
    }
    return NULL;
}
bool game_source_event(const game_source_registry *r, const char *name, execution_module_event_info *out)
{
    assert(r && name && out);
    int declaration = r->events ? game_event_catalog_find(r->events, name) : -1;
    if (declaration >= 0) {
        *out = game_event_catalog_at(r->events, declaration);
        return true;
    }
    int index;
    const execution_module *module = event_module(r, name, &index);
    if (!module) {
        return false;
    }
    *out = execution_module_event_at(module, index);
    return true;
}
bool game_source_event_field(
    const game_source_registry *r,
    const char *name,
    int field,
    execution_module_event_field_info *out
)
{
    assert(r && name && out);
    int declaration = r->events ? game_event_catalog_find(r->events, name) : -1;
    if (declaration >= 0) {
        if (field < 0 ||
            field >= game_event_catalog_at(r->events, declaration).field_count) {
            return false;
        }
        *out = game_event_catalog_field_at(r->events, declaration, field);
        return true;
    }
    int index;
    const execution_module *module = event_module(r, name, &index);
    if (!module || field < 0 ||
        field >= execution_module_event_at(module, index).field_count) {
        return false;
    }
    *out = execution_module_event_field_at(module, index, field);
    return true;
}
bool game_source_paused(const game_source_registry *r)
{
    for (int i = 0; i < acount(r->records); i++) {
        if (execution_program_paused(r->records[i].program)) {
            return true;
        }
    }
    return false;
}
execution_program *game_source_program(game_source_registry *r, const char *name)
{
    int i = record_index(r, name);
    return i < 0 ? NULL : r->records[i].program;
}
int game_source_register(
    game_source_registry *r,
    const char *name,
    execution_module *module,
    const service_binding *services
)
{
    assert(module);
    game_source_publication entry = { .name = name, .module = module };
    return game_source_publish(r, &entry, 1, services, r->error, sizeof r->error);
}
int game_source_replace(game_source_registry *r, const char *name, execution_module *module, bool restart)
{
    assert(module);
    game_source_publication entry = {
        .name = name,
        .previous_name = name,
        .module = module,
        .restart = restart,
    };
    return game_source_publish(r, &entry, 1, NULL, r->error, sizeof r->error);
}
int game_source_remove(game_source_registry *r, const char *name)
{
    game_source_publication entry = { .previous_name = name };
    return game_source_publish(r, &entry, 1, NULL, r->error, sizeof r->error);
}
static int begin_attachment(game_source_registry *r, int index, double now, double delta)
{
    source_attachment a = r->attachments[index];
    if (a.begin) {
        return a.begin < 0 ? -1 : 0;
    }
    uint64_t event = 0;
    for (int i = 0; i < acount(r->records); i++) {
        if (r->records[i].program == a.program) {
            event = event_id(&r->records[i], "Begin");
            break;
        }
    }
    r->attachments[index].begin = 1;
    execution_instance *instance = execution_program_instance(a.program, ecs_entity_to_u64(a.entity));
    assert(instance);
    begin_boundary(r);
    r->executing++;
    int status = event
                   ? execution_instance_dispatch(instance, event, NULL, 0, now, delta, 10000)
                   : 0;
    r->executing--;
    if (status == FLOW_ERR_UNKNOWN_EVENT) {
        return 0;
    }
    if (status < 0) {
        for (int i = 0; i < acount(r->attachments); i++) {
            if (r->attachments[i].program == a.program &&
                same_entity(r->attachments[i].entity, a.entity)) {
                r->attachments[i].begin = -1;
            }
        }
    }
    return program_status(r, a.program, status < 0 ? status : 0);
}
int game_source_attach(
    game_source_registry *r,
    ecs_entity entity,
    const char *name,
    bool deferred,
    double now,
    double delta
)
{
    assert(!r->staging);
    execution_program *p = game_source_program(r, name);
    if (!p || !ecs_entity_is_alive(r->world, entity) ||
        execution_program_instance(p, ecs_entity_to_u64(entity))) {
        return -1;
    }
    if (!execution_program_attach(p, ecs_entity_to_u64(entity))) {
        return -1;
    }
    assert(r->attachment_sequence < UINT64_MAX);
    apush(
        r->attachments,
        ((source_attachment){ .entity = entity, .program = p, .sequence = ++r->attachment_sequence })
    );
    int status = deferred
                   ? 0
                   : begin_attachment(r, acount(r->attachments) - 1, now, delta);
    if (status < 0) {
        game_source_detach(r, entity, name);
    }
    return status;
}
int game_source_detach(game_source_registry *r, ecs_entity entity, const char *name)
{
    assert(!r->staging);
    execution_program *p = game_source_program(r, name);
    for (int i = 0; p && i < acount(r->attachments); i++) {
        if (r->attachments[i].program == p &&
            same_entity(r->attachments[i].entity, entity)) {
            execution_program_detach(p, ecs_entity_to_u64(entity));
            adel(r->attachments, i);
            return 1;
        }
    }
    return 0;
}
int game_source_set_attachments(
    game_source_registry *r,
    ecs_entity entity,
    const char *const *names,
    int count,
    bool deferred,
    double now,
    double delta
)
{
    assert(!r->staging);
    if (count < 0 || count > GAME_MODULE_SET_MAX_MODULES || (count && !names) ||
        !ecs_entity_is_alive(r->world, entity)) {
        snprintf(r->error, sizeof r->error, "Invalid source attachment owner or count");
        return -1;
    }
    for (int i = 0; i < count; i++) {
        if (!names[i] || !names[i][0] || !game_source_registered(r, names[i])) {
            snprintf(r->error, sizeof r->error, "Source attachment name is unregistered");
            return -1;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(names[i], names[j])) {
                snprintf(r->error, sizeof r->error, "Source attachment name is duplicated");
                return -1;
            }
        }
    }
    const char **created = NULL;
    for (int i = 0; i < count; i++) {
        execution_program *p = game_source_program(r, names[i]);
        if (!p || execution_program_instance(p, ecs_entity_to_u64(entity))) {
            continue;
        }
        if (game_source_attach(r, entity, names[i], true, now, delta)) {
            for (int j = 0; j < acount(created); j++) {
                game_source_detach(r, entity, created[j]);
            }
            afree(created);
            return -1;
        }
        apush(created, names[i]);
    }
    for (int i = 0; !deferred && i < acount(r->attachments); i++) {
        if (same_entity(r->attachments[i].entity, entity) &&
            begin_attachment(r, i, now, delta) < 0) {
            for (int j = 0; j < acount(created); j++) {
                game_source_detach(r, entity, created[j]);
            }
            afree(created);
            return -1;
        }
    }
    afree(created);
    for (int i = acount(r->attachments) - 1; i >= 0; i--) {
        source_attachment a = r->attachments[i];
        if (!same_entity(a.entity, entity)) {
            continue;
        }
        bool keep = false;
        for (int j = 0; j < count; j++) {
            keep |= game_source_program(r, names[j]) == a.program;
        }
        if (!keep) {
            execution_program_detach(a.program, ecs_entity_to_u64(entity));
            adel(r->attachments, i);
        }
    }
    source_attachment *ordered = NULL;
    bool inserted = false;
    for (int i = 0; i < acount(r->attachments); i++) {
        if (!same_entity(r->attachments[i].entity, entity)) {
            apush(ordered, r->attachments[i]);
            continue;
        }
        if (inserted) {
            continue;
        }
        for (int j = 0; j < count; j++) {
            execution_program *p = game_source_program(r, names[j]);
            for (int k = 0; p && k < acount(r->attachments); k++) {
                if (r->attachments[k].program == p &&
                    same_entity(r->attachments[k].entity, entity)) {
                    apush(ordered, r->attachments[k]);
                    break;
                }
            }
        }
        inserted = true;
    }
    afree(r->attachments);
    r->attachments = ordered;
    return 0;
}
int game_source_detach_entity(game_source_registry *r, ecs_entity entity)
{
    assert(!r->staging);
    int count = 0;
    for (int i = acount(r->attachments) - 1; i >= 0; i--) {
        if (same_entity(r->attachments[i].entity, entity)) {
            execution_program_detach(r->attachments[i].program, ecs_entity_to_u64(entity));
            adel(r->attachments, i);
            count++;
        }
    }
    return count;
}
static int source_begin(game_source_registry *r, double now, double delta)
{
    reap_attachments(r);
    for (int i = 0; i < acount(r->attachments); i++) {
        int status = begin_attachment(r, i, now, delta);
        if (status < 0) {
            return status;
        }
    }
    return 0;
}
static int dispatch_event(
    game_source_registry *r,
    uint64_t owner,
    const char *name,
    const pin_value *arguments,
    int count,
    double now,
    double delta
)
{
    begin_boundary(r);
    r->executing++;
    int dispatched = 0;
    for (int i = 0; i < acount(r->records); i++) {
        source_record record = r->records[i];
        uint64_t event = event_id(&record, name);
        if (!event) {
            continue;
        }
        int status = execution_program_dispatch(record.program, event, owner, arguments, count, now, delta, 10000);
        if (status < 0) {
            r->executing--;
            return program_status(r, record.program, status);
        }
        dispatched++;
    }
    r->executing--;
    return dispatched;
}
int game_source_dispatch(
    game_source_registry *r,
    ecs_entity entity,
    const char *name,
    const pin_value *arguments,
    int count,
    double now,
    double delta
)
{
    return dispatch_event(r, ecs_entity_to_u64(entity), name, arguments, count, now, delta);
}
int game_source_field_changed(
    game_source_registry *r,
    ecs_entity entity,
    ecs_component_id component,
    size_t offset,
    pin_value value,
    double now,
    double delta
)
{
    begin_boundary(r);
    r->executing++;
    for (int i = 0; i < acount(r->records); i++) {
        execution_program *program = r->records[i].program;
        int status = execution_program_field_changed(
            program,
            ecs_entity_to_u64(entity),
            component,
            offset,
            value,
            now,
            delta,
            10000
        );
        if (status < 0) {
            r->executing--;
            return program_status(r, program, status);
        }
    }
    r->executing--;
    return FLOW_OK;
}
int game_source_dispatch_all(
    game_source_registry *r,
    const char *name,
    const pin_value *arguments,
    int count,
    double now,
    double delta
)
{
    return dispatch_event(r, 0, name, arguments, count, now, delta);
}
int game_source_bind_service(game_source_registry *r, const char *name, void *value)
{
    assert(r && name && *name && !r->staging);
    for (int i = 0; i < acount(r->records); i++) {
        if (!execution_program_service_bind_valid(r->records[i].program, name, value)) {
            return FLOW_ERR_STATE;
        }
    }
    for (int i = 0; i < acount(r->records); i++) {
        int status = execution_program_bind_service(r->records[i].program, name, value);
        assert(status >= 0);
        (void)status;
    }
    for (int i = 0; i < acount(r->services); i++) {
        if (!strcmp(r->services[i].type_name, name)) {
            r->services[i].value = value;
            return 0;
        }
    }
    apush(r->services, ((service_binding){ xstrdup(name), value }));
    return 0;
}
static void reap_attachments(game_source_registry *r)
{
    for (int i = acount(r->attachments) - 1; i >= 0; i--) {
        if (!ecs_entity_is_alive(r->world, r->attachments[i].entity)) {
            execution_program_detach(
                r->attachments[i].program,
                ecs_entity_to_u64(r->attachments[i].entity)
            );
            adel(r->attachments, i);
        }
    }
}
static int maintain(game_source_registry *r, double now, double delta)
{
    execution_program **programs = NULL;
    for (int i = 0; i < acount(r->records); i++) {
        apush(programs, r->records[i].program);
    }
    int status = execution_program_bindings_all(
        programs,
        acount(programs),
        now,
        delta,
        10000,
        r->error,
        sizeof r->error
    );
    afree(programs);
    return status;
}
static int source_step(game_source_registry *r, double now, double delta)
{
    reap_attachments(r);
    for (int i = 0; i < acount(r->records); i++) {
        if (r->explicit_systems && r->records[i].system) {
            continue;
        }
        int status = execution_program_update(r->records[i].program, now, delta, 10000);
        if (status < 0) {
            return program_status(r, r->records[i].program, status);
        }
    }
    return maintain(r, now, delta);
}
bool game_source_is_system(const game_source_registry *r, const char *name)
{
    int index = record_index(r, name);
    return index >= 0 && r->records[index].system;
}
int game_source_select_systems(game_source_registry *r, const char *const *names, int count)
{
    assert(r && !r->staging && !r->executing && count >= 0 && (!count || names));
    r->error[0] = '\0';
    bool *selected = xcalloc((size_t)acount(r->records) + 1, sizeof *selected);
    for (int i = 0; i < count; i++) {
        int index = names[i] ? record_index(r, names[i]) : -1;
        if (index < 0 || !r->records[index].system || selected[index]) {
            snprintf(r->error, sizeof r->error, "source system selection contains an unknown, non-system or duplicate name");
            free(selected);
            return -1;
        }
        selected[index] = true;
    }
    r->explicit_systems = true;
    for (int i = 0; i < acount(r->records); i++) {
        r->records[i].selected = selected[i];
        if (r->records[i].system) {
            execution_program_set_enabled(r->records[i].program, selected[i]);
        }
    }
    free(selected);
    return 0;
}
int game_source_run_system(game_source_registry *r, const char *name, double now, double delta)
{
    assert(r && name && !r->staging);
    int index = record_index(r, name);
    if (index < 0 || !r->records[index].system ||
        !source_record_active(r, &r->records[index])) {
        snprintf(r->error, sizeof r->error, "source system is not selected");
        return -1;
    }
    begin_boundary(r);
    r->executing++;
    int status = execution_program_update(r->records[index].program, now, delta, 10000);
    status = program_status(r, r->records[index].program, status);
    r->executing--;
    return status;
}
typedef struct
{
    execution_program *program;
    int status;
    bool ran, prepared;
} system_result;
struct game_source_system_batch
{
    game_source_registry *registry;
    system_result *results;
    int count;
    bool parallel;
    double now, delta;
};
game_source_system_batch *game_source_systems_prepare(
    game_source_registry *r,
    const char *const *names,
    int count,
    double now,
    double delta,
    bool allow_prepare
)
{
    assert(r && !r->staging && !r->executing);
    if (count < 0 || count > GAME_SOURCE_SYSTEM_BATCH_MAX || (count && !names)) {
        snprintf(r->error, sizeof r->error, "Invalid source system cohort");
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        int index = names[i] ? record_index(r, names[i]) : -1;
        if (index < 0 || !r->records[index].system ||
            !source_record_active(r, &r->records[index])) {
            snprintf(r->error, sizeof r->error, "Source system cohort contains an unselected system");
            return NULL;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(names[i], names[j])) {
                snprintf(r->error, sizeof r->error, "Source system cohort contains a duplicate");
                return NULL;
            }
        }
    }
    game_source_system_batch *batch = xcalloc(1, sizeof *batch);
    batch->registry = r;
    batch->count = count;
    batch->now = now;
    batch->delta = delta;
    batch->parallel = allow_prepare && !r->profiling;
    batch->results = xcalloc((size_t)count, sizeof *batch->results);
    for (int i = 0; i < count; i++) {
        batch->results[i].program = r->records[record_index(r, names[i])].program;
        execution_program *program = batch->results[i].program;
        execution_effect_summary effects = execution_module_effects(
            execution_program_module(program)
        );
        batch->parallel &= effects.parallel_safe &&
                           !execution_program_inspection(program) &&
                           !execution_program_observed(program);
    }
    begin_boundary(r);
    r->executing++;
    if (batch->parallel) {
        for (int i = 0; i < count; i++) {
            system_result *result = &batch->results[i];
            result->status = execution_program_prepare_system(result->program, now, delta, 10000);
            result->prepared = result->status >= 0;
        }
        for (int i = 0; i < count; i++) {
            batch->parallel &= batch->results[i].prepared;
        }
    }
    return batch;
}
bool game_source_systems_parallel(const game_source_system_batch *batch)
{
    assert(batch);
    return batch->parallel;
}
int game_source_systems_run(game_source_system_batch *batch, int index, char *error, size_t size)
{
    assert(batch && index >= 0 && index < batch->count && (!size || error));
    system_result *result = &batch->results[index];
    assert(!result->ran);
    result->ran = true;
    if (result->status >= 0) {
        result->status = result->prepared
                           ? execution_program_run_system(result->program)
                           : execution_program_update(
                                 result->program,
                                 batch->now,
                                 batch->delta,
                                 10000
                             );
    }
    if (size) {
        error[0] = 0;
        if (result->status < 0) {
            const execution_program_failure *failure = execution_program_error(
                result->program
            );
            if (failure) {
                snprintf(
                    error,
                    size,
                    "%s:%d:%d: %s",
                    failure->asset_name,
                    failure->line,
                    failure->column,
                    failure->message
                );
            } else {
                snprintf(
                    error,
                    size,
                    "Source execution failed with status %d",
                    result->status
                );
            }
        }
    }
    return result->status;
}
int game_source_systems_finish(game_source_system_batch *batch)
{
    assert(batch && batch->registry->executing == 1);
    game_source_registry *r = batch->registry;
    int status = 0;
    for (int i = 0; i < batch->count; i++) {
        system_result *result = &batch->results[i];
        if (result->prepared) {
            int finished = execution_program_finish_system(result->program);
            if (result->status >= 0) {
                result->status = finished;
            }
        }
        if (result->status < 0 && status >= 0) {
            status = program_status(r, result->program, result->status);
        }
    }
    r->executing--;
    free(batch->results);
    free(batch);
    return status;
}
int game_source_maintain(game_source_registry *r, double now, double delta)
{
    begin_boundary(r);
    reap_attachments(r);
    r->executing++;
    int status = maintain(r, now, delta);
    r->executing--;
    return status;
}
const char *game_source_error(const game_source_registry *r)
{
    return r->error;
}
static int source_pump(game_source_registry *r, double now, double delta)
{
    reap_attachments(r);
    for (int i = 0; i < acount(r->records); i++) {
        int status = execution_program_pump(r->records[i].program, now, delta, 10000);
        if (status < 0) {
            return program_status(r, r->records[i].program, status);
        }
    }
    return 0;
}

int game_source_begin(game_source_registry *r, double now, double delta)
{
    begin_boundary(r);
    r->executing++;
    int status = source_begin(r, now, delta);
    r->executing--;
    return status;
}

int game_source_step(game_source_registry *r, double now, double delta)
{
    begin_boundary(r);
    r->executing++;
    int status = source_step(r, now, delta);
    r->executing--;
    return status;
}

int game_source_pump(game_source_registry *r, double now, double delta)
{
    begin_boundary(r);
    r->executing++;
    int status = source_pump(r, now, delta);
    r->executing--;
    return status;
}

int game_source_attachment_count(const game_source_registry *r)
{
    return acount(r->attachments);
}
game_source_attachment_info game_source_attachment_at(const game_source_registry *r, int index)
{
    assert(index >= 0 && index < acount(r->attachments));
    source_attachment a = r->attachments[index];
    for (int i = 0; i < acount(r->records); i++) {
        const source_record *record = &r->records[i];
        if (record->program != a.program) {
            continue;
        }
        return (
            game_source_attachment_info
        ){ .entity = a.entity,
           .sequence = a.sequence,
           .name = record->name,
           .module = record->module,
           .program = a.program,
           .instance = execution_program_instance(a.program, ecs_entity_to_u64(a.entity)),
           .begin = a.begin };
    }
    assert(false);
    return (game_source_attachment_info){ 0 };
}
