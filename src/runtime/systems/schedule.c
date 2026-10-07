#include "runtime/systems/schedule.h"

#include "runtime/task_pool.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    engine_system_desc desc;
    compile_effects effects;
    int serial;
} entry;
struct system_schedule
{
    ecs_world *world;
    game_task_pool *pool;
    entry *entries;
    int *order;
    unsigned char *edges;
    bool dirty, parallel;
    int next_serial, parallel_batches;
    char error[256], warning[256];
};

static char **copy_names(const char *const *names, int count)
{
    char **copy = NULL;
    for (int i = 0; i < count; i++) {
        apush(copy, xstrdup(names[i]));
    }
    return copy;
}
static void free_names(const char *const *names, int count)
{
    for (int i = 0; i < count; i++) {
        free((char *)names[i]);
    }
    char **array = (char **)names;
    afree(array);
}
static void free_entry(entry *e)
{
    free((char *)e->desc.name);
    ecs_query_expr_free((ecs_query_expr *)e->desc.query);
    afree(e->effects.reads);
    afree(e->effects.writes);
    free_names(e->desc.resource_reads, e->desc.resource_read_count);
    free_names(e->desc.resource_writes, e->desc.resource_write_count);
    free_names(e->desc.after, e->desc.after_count);
}
system_schedule *system_schedule_new(ecs_world *world)
{
    system_schedule *s = xcalloc(1, sizeof *s);
    s->world = world;
    s->pool = game_task_pool_acquire();
    s->parallel = true;
    return s;
}
void system_schedule_free(system_schedule *s)
{
    for (int i = 0; i < acount(s->entries); i++) {
        free_entry(&s->entries[i]);
    }
    afree(s->entries);
    afree(s->order);
    free(s->edges);
    game_task_pool_release(s->pool);
    free(s);
}
static int find(const system_schedule *s, const char *name)
{
    for (int i = 0; i < acount(s->entries); i++) {
        if (!strcmp(s->entries[i].desc.name, name)) {
            return i;
        }
    }
    return -1;
}
int system_schedule_register(system_schedule *s, const engine_system_desc *d)
{
    if (!d || !d->name || !d->name[0] || d->phase < 0 || d->phase >= SYSTEM_PHASE_COUNT ||
        (!d->graph && !d->run) || (d->graph && d->run) || d->read_count < 0 ||
        d->write_count < 0 || d->resource_read_count < 0 ||
        d->resource_write_count < 0 || d->after_count < 0 ||
        (d->read_count && !d->reads) || (d->write_count && !d->writes) ||
        (d->resource_read_count && !d->resource_reads) ||
        (d->resource_write_count && !d->resource_writes) ||
        (d->after_count && !d->after)) {
        return -1;
    }
    for (int i = 0; d->graph && i < acount(s->entries); i++) {
        if (s->entries[i].desc.graph == d->graph &&
            strcmp(s->entries[i].desc.name, d->name)) {
            return -1; /* A System owns mutable query and execution storage. */
        }
    }
    entry e = { .desc = *d, .serial = s->next_serial++ };
    e.desc.name = xstrdup(d->name);
    e.desc.query = d->query ? ecs_query_expr_clone(d->query) : NULL;
    e.desc.resource_reads = (const char *const *)
        copy_names(d->resource_reads, d->resource_read_count);
    e.desc.resource_writes = (const char *const *)
        copy_names(d->resource_writes, d->resource_write_count);
    e.desc.after = (const char *const *)copy_names(d->after, d->after_count);
    compile_effects effects = { .all_writes = !d->declared,
                                .parallel_eligible = d->parallel_safe && d->deterministic,
                                .flags = (d->structural ? NODE_EFFECT_STRUCTURAL : 0) |
                                         (d->host_sink ? NODE_EFFECT_HOST_SINK : 0) };
    if (d->graph) {
        if (d->query && game_system_set_query(d->graph, d->query) < 0) {
            free_entry(&e);
            return -1;
        }
        effects = game_system_compile(d->graph)->effects;
        if (game_system_compile(d->graph)->error_count) {
            free_entry(&e);
            return -1;
        }
        effects.parallel_eligible &= game_system_uses_bytecode(d->graph);
    }
    e.effects = effects;
    e.effects.reads = NULL;
    e.effects.writes = NULL;
    const uint64_t *reads = d->graph ? effects.reads : d->reads;
    const uint64_t *writes = d->graph ? effects.writes : d->writes;
    int nr = d->graph ? acount(effects.reads) : d->read_count,
        nw = d->graph ? acount(effects.writes) : d->write_count;
    for (int i = 0; i < nr; i++) {
        apush(e.effects.reads, reads[i]);
    }
    for (int i = 0; i < nw; i++) {
        apush(e.effects.writes, writes[i]);
    }
    e.desc.reads = e.effects.reads;
    e.desc.read_count = nr;
    e.desc.writes = e.effects.writes;
    e.desc.write_count = nw;
    int index = find(s, d->name);
    if (index < 0) {
        apush(s->entries, e);
    } else {
        e.serial = s->entries[index].serial;
        free_entry(&s->entries[index]);
        s->entries[index] = e;
    }
    s->dirty = true;
    return 0;
}
void system_schedule_remove(system_schedule *s, const char *name)
{
    int index = find(s, name);
    if (index < 0) {
        return;
    }
    free_entry(&s->entries[index]);
    for (int i = index; i + 1 < acount(s->entries); i++) {
        s->entries[i] = s->entries[i + 1];
    }
    asetlen(s->entries, acount(s->entries) - 1);
    s->dirty = true;
}
static bool names_intersect(const char *const *a, int na, const char *const *b, int nb)
{
    for (int i = 0; i < na; i++) {
        for (int j = 0; j < nb; j++) {
            if (!strcmp(a[i], b[j])) {
                return true;
            }
        }
    }
    return false;
}
static bool barrier(const entry *e)
{
    return e->effects.all_writes || !e->effects.parallel_eligible ||
           e->desc.resource_write_count ||
           (e->effects.flags & (NODE_EFFECT_STRUCTURAL | NODE_EFFECT_HOST_SINK |
                                NODE_EFFECT_WORLD_READ));
}
static bool conflict(const entry *a, const entry *b)
{
    const engine_system_desc *x = &a->desc, *y = &b->desc;
    return barrier(a) || barrier(b) ||
           compile_effects_conflict(&a->effects, &b->effects, 0) ||
           names_intersect(x->resource_writes, x->resource_write_count, y->resource_reads, y->resource_read_count) ||
           names_intersect(y->resource_writes, y->resource_write_count, x->resource_reads, x->resource_read_count) ||
           names_intersect(x->resource_writes, x->resource_write_count, y->resource_writes, y->resource_write_count);
}
static bool before(const entry *a, const entry *b)
{
    if (a->desc.phase != b->desc.phase) {
        return a->desc.phase < b->desc.phase;
    }
    if (a->desc.order != b->desc.order) {
        return a->desc.order < b->desc.order;
    }
    return a->serial < b->serial;
}
bool system_schedule_build(system_schedule *s)
{
    if (!s->dirty) {
        return !s->error[0];
    }
    s->dirty = false;
    s->error[0] = 0;
    s->warning[0] = 0;
    aclear(s->order);
    int count = acount(s->entries);
    free(s->edges);
    s->edges = xcalloc((size_t)count * count, 1);
    for (int i = 0; i < count; i++) {
        entry *e = &s->entries[i];
        if (e->effects.all_writes) {
            snprintf(
                s->warning,
                sizeof s->warning,
                "System %s has undeclared effects and runs serially",
                e->desc.name
            );
        }
        for (int j = 0; j < count; j++) {
            if (i == j) {
                continue;
            }
            entry *other = &s->entries[j];
            if (before(e, other) &&
                (e->desc.phase != other->desc.phase ||
                 e->desc.order != other->desc.order || conflict(e, other))) {
                s->edges[i * count + j] = 1;
            }
        }
        for (int j = 0; j < e->desc.after_count; j++) {
            int dependency = find(s, e->desc.after[j]);
            if (dependency < 0) {
                snprintf(
                    s->error,
                    sizeof s->error,
                    "System %s requires missing system %s",
                    e->desc.name,
                    e->desc.after[j]
                );
                return false;
            }
            s->edges[dependency * count + i] = 1;
        }
    }
    int *degree = xcalloc((size_t)count, sizeof(int));
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < count; j++) {
            degree[j] += s->edges[i * count + j];
        }
    }
    for (int n = 0; n < count; n++) {
        int chosen = -1;
        for (int i = 0; i < count; i++) {
            if (degree[i] == 0 &&
                (chosen < 0 || before(&s->entries[i], &s->entries[chosen]))) {
                chosen = i;
            }
        }
        if (chosen < 0) {
            snprintf(s->error, sizeof s->error, "System dependency cycle among remaining systems");
            free(degree);
            aclear(s->order);
            return false;
        }
        apush(s->order, chosen);
        degree[chosen] = -1;
        for (int j = 0; j < count; j++) {
            if (s->edges[chosen * count + j]) {
                degree[j]--;
            }
        }
    }
    free(degree);
    return true;
}
typedef struct
{
    system_schedule *schedule;
    int *indices;
    int *results;
    flow_error *errors;
    double time, delta;
} batch;
static void run_batch(int start, int end, uint32_t, void *user)
{
    batch *b = user;
    for (int i = start; i < end; i++) {
        entry *e = &b->schedule->entries[b->indices[i]];
        b->results[i] = e->desc.graph
                          ? game_system_run_snapshot(e->desc.graph, b->time, b->delta, &b->errors[i])
                          : e->desc.run(
                                b->schedule->world,
                                b->time,
                                b->delta,
                                e->desc.user,
                                &b->errors[i]
                            );
    }
}
int system_schedule_maintain(system_schedule *s, double time, double delta, flow_error *error)
{
    if (!system_schedule_build(s)) {
        return FLOW_ERR_COMPILE;
    }
    for (int i = 0; i < acount(s->order); i++) {
        game_system *system = s->entries[s->order[i]].desc.graph;
        if (!system) {
            continue;
        }
        int result = game_system_maintain(system, time, delta, error);
        if (result < 0) {
            return result;
        }
    }
    return FLOW_OK;
}

int system_schedule_field_changed(
    system_schedule *s,
    ecs_entity entity,
    ecs_component_id component,
    size_t offset,
    pin_value value,
    flow_error *error
)
{
    if (!system_schedule_build(s)) {
        return FLOW_ERR_COMPILE;
    }
    for (int i = 0; i < acount(s->order); i++) {
        game_system *system = s->entries[s->order[i]].desc.graph;
        if (!system) {
            continue;
        }
        int result = game_system_field_changed(system, entity, component, offset, value, error);
        if (result < 0) {
            return result;
        }
    }
    return FLOW_OK;
}

int system_schedule_run(system_schedule *s, system_phase phase, double time, double delta)
{
    if (!system_schedule_build(s)) {
        return FLOW_ERR_COMPILE;
    }
    int count = acount(s->entries);
    for (int at = 0; at < count;) {
        int first = s->order[at];
        if (s->entries[first].desc.phase != phase) {
            at++;
            continue;
        }
        int *indices = NULL;
        apush(indices, first);
        at++;
        while (at < count && s->entries[s->order[at]].desc.phase == phase &&
               !barrier(&s->entries[first])) {
            int next = s->order[at];
            bool independent = true;
            for (int j = 0; j < acount(indices); j++) {
                independent &= !s->edges[indices[j] * count + next] &&
                               !conflict(&s->entries[indices[j]], &s->entries[next]);
            }
            if (!independent) {
                break;
            }
            apush(indices, next);
            at++;
        }
        int n = acount(indices);
        int *results = xcalloc((size_t)n, sizeof(int));
        flow_error *errors = xcalloc((size_t)n, sizeof(flow_error));
        bool safe = true;
        for (int j = 0; j < n; j++) {
            entry *e = &s->entries[indices[j]];
            if (e->desc.graph) {
                results[j] = game_system_prepare_snapshot(e->desc.graph, &errors[j]);
                safe &= results[j] >= 0;
            }
        }
        if (safe) {
            batch b = { s, indices, results, errors, time, delta };
            bool parallel = s->parallel && n > 1;
            if (parallel) {
                int result = ecs_world_iteration_begin(s->world);
                assert(result == ECS_OK);
                game_task_pool_begin(s->pool, true);
                const game_task_system *tasks = game_task_pool_tasks(s->pool);
                void *task = tasks->enqueue(run_batch, n, 1, &b, tasks->user);
                if (task) {
                    tasks->finish(task, tasks->user);
                }
                game_task_pool_end(s->pool);
                ecs_world_iteration_end(s->world);
                s->parallel_batches++;
            } else {
                run_batch(0, n, 0, &b);
            }
        }
        int result = 0;
        for (int j = 0; j < n; j++) {
            if (results[j] < 0) {
                snprintf(
                    s->error,
                    sizeof s->error,
                    "System %s: %s",
                    s->entries[indices[j]].desc.name,
                    errors[j].message ? errors[j].message : "execution failed"
                );
                result = results[j];
                break;
            }
        }
        free(results);
        free(errors);
        afree(indices);
        if (result < 0) {
            return result;
        }
    }
    return 0;
}
const char *system_schedule_error(const system_schedule *s)
{
    return s->error[0] ? s->error : NULL;
}
const char *system_schedule_warning(const system_schedule *s)
{
    return s->warning[0] ? s->warning : NULL;
}
void system_schedule_set_parallel(system_schedule *s, bool parallel)
{
    s->parallel = parallel;
}
int system_schedule_count(const system_schedule *s)
{
    return acount(s->order);
}
const char *system_schedule_at(const system_schedule *s, int position)
{
    return s->entries[s->order[position]].desc.name;
}
int system_schedule_parallel_batches(const system_schedule *s)
{
    return s->parallel_batches;
}

bool system_schedule_dependency(const system_schedule *s, const char *before_name, const char *after_name)
{
    int a = find(s, before_name), b = find(s, after_name);
    return !s->dirty && !s->error[0] && a >= 0 && b >= 0 &&
           s->edges[a * acount(s->entries) + b];
}
