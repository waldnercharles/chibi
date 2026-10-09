#include "runtime/systems/scheduler.h"

#include "execution/status.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    game_schedule_desc desc;
    int serial;
} entry;
struct game_schedule
{
    ecs_world *world;
    const game_task_system *tasks;
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
    free((void *)e->desc.reads);
    free((void *)e->desc.writes);
    free_names(e->desc.resource_reads, e->desc.resource_read_count);
    free_names(e->desc.resource_writes, e->desc.resource_write_count);
    free_names(e->desc.after, e->desc.after_count);
}
game_schedule *game_schedule_new(ecs_world *world, const game_task_system *tasks)
{
    game_schedule *s = xcalloc(1, sizeof *s);
    s->world = world;
    assert(!tasks || (tasks->workers > 0 && tasks->enqueue && tasks->finish));
    s->tasks = tasks;
    s->parallel = true;
    return s;
}
void game_schedule_free(game_schedule *s)
{
    for (int i = 0; i < acount(s->entries); i++) {
        free_entry(&s->entries[i]);
    }
    afree(s->entries);
    afree(s->order);
    free(s->edges);
    free(s);
}
static int find(const game_schedule *s, const char *name)
{
    for (int i = 0; i < acount(s->entries); i++) {
        if (!strcmp(s->entries[i].desc.name, name)) {
            return i;
        }
    }
    return -1;
}
int game_schedule_register(game_schedule *s, const game_schedule_desc *d)
{
    if (!d || !d->name || !d->name[0] || d->phase < 0 ||
        d->phase >= SYSTEM_PHASE_COUNT || !d->run || d->read_count < 0 ||
        d->write_count < 0 || d->resource_read_count < 0 ||
        d->resource_write_count < 0 || d->after_count < 0 ||
        (d->read_count && !d->reads) || (d->write_count && !d->writes) ||
        (d->resource_read_count && !d->resource_reads) ||
        (d->resource_write_count && !d->resource_writes) ||
        (d->after_count && !d->after)) {
        return -1;
    }
    for (int i = 0; d->identity && i < acount(s->entries); i++) {
        if (s->entries[i].desc.identity == d->identity &&
            strcmp(s->entries[i].desc.name, d->name)) {
            return -1; /* A System owns mutable query and execution storage. */
        }
    }
    entry e = { .desc = *d, .serial = s->next_serial++ };
    e.desc.name = xstrdup(d->name);
    e.desc.resource_reads = (const char *const *)
        copy_names(d->resource_reads, d->resource_read_count);
    e.desc.resource_writes = (const char *const *)
        copy_names(d->resource_writes, d->resource_write_count);
    e.desc.after = (const char *const *)copy_names(d->after, d->after_count);
    e.desc.reads = xmalloc((size_t)d->read_count * sizeof *d->reads);
    e.desc.writes = xmalloc((size_t)d->write_count * sizeof *d->writes);
    if (d->read_count) {
        memcpy((void *)e.desc.reads, d->reads, (size_t)d->read_count * sizeof *d->reads);
    }
    if (d->write_count) {
        memcpy(
            (void *)e.desc.writes,
            d->writes,
            (size_t)d->write_count * sizeof *d->writes
        );
    }
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
void game_schedule_remove(game_schedule *s, const char *name)
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
    return !e->desc.declared || !e->desc.parallel_safe ||
           !e->desc.deterministic || e->desc.resource_write_count ||
           e->desc.structural || e->desc.host_sink || e->desc.world_read;
}
static bool ids_intersect(const uint64_t *a, int na, const uint64_t *b, int nb)
{
    for (int i = 0; i < na; i++) {
        for (int j = 0; j < nb; j++) {
            if (a[i] == b[j]) {
                return true;
            }
        }
    }
    return false;
}
static bool conflict(const entry *a, const entry *b)
{
    const game_schedule_desc *x = &a->desc, *y = &b->desc;
    return barrier(a) || barrier(b) ||
           ids_intersect(x->writes, x->write_count, y->writes, y->write_count) ||
           ids_intersect(x->writes, x->write_count, y->reads, y->read_count) ||
           ids_intersect(y->writes, y->write_count, x->reads, x->read_count) ||
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
bool game_schedule_build(game_schedule *s)
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
        if (!e->desc.declared) {
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
    game_schedule *schedule;
    int *indices;
    int *results;
    game_schedule_error *errors;
    double time, delta;
} batch;
static void run_batch(int start, int end, uint32_t, void *user)
{
    batch *b = user;
    for (int i = start; i < end; i++) {
        entry *e = &b->schedule->entries[b->indices[i]];
        b->results[i] = e->desc.run(
            b->schedule->world,
            b->time,
            b->delta,
            e->desc.user,
            &b->errors[i]
        );
    }
}
int game_schedule_maintain(game_schedule *s, double time, double delta, game_schedule_error *error)
{
    if (!game_schedule_build(s)) {
        return FLOW_ERR_COMPILE;
    }
    for (int i = 0; i < acount(s->order); i++) {
        game_schedule_desc *d = &s->entries[s->order[i]].desc;
        if (!d->maintain) {
            continue;
        }
        int result = d->maintain(d->user, time, delta, error);
        if (result < 0) {
            return result;
        }
    }
    return FLOW_OK;
}

int game_schedule_field_changed(
    game_schedule *s,
    ecs_entity entity,
    ecs_component_id component,
    size_t offset,
    pin_value value,
    game_schedule_error *error
)
{
    if (!game_schedule_build(s)) {
        return FLOW_ERR_COMPILE;
    }
    for (int i = 0; i < acount(s->order); i++) {
        game_schedule_desc *d = &s->entries[s->order[i]].desc;
        if (!d->field_changed) {
            continue;
        }
        int result = d->field_changed(d->user, entity, component, offset, value, error);
        if (result < 0) {
            return result;
        }
    }
    return FLOW_OK;
}

int game_schedule_run(game_schedule *s, system_phase phase, double time, double delta)
{
    if (!game_schedule_build(s)) {
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
        game_schedule_error *errors = xcalloc((size_t)n, sizeof(game_schedule_error));
        bool safe = true;
        for (int j = 0; j < n; j++) {
            entry *e = &s->entries[indices[j]];
            if (e->desc.prepare) {
                results[j] = e->desc.prepare(e->desc.user, &errors[j]);
                safe &= results[j] >= 0;
            }
        }
        if (safe) {
            batch b = { s, indices, results, errors, time, delta };
            bool parallel = s->parallel && s->tasks && n > 1;
            for (int j = 0; j < n && parallel; j++) {
                const game_schedule_desc *desc = &s->entries[indices[j]].desc;
                parallel = !desc->parallel_ready || desc->parallel_ready(desc->user);
            }
            if (parallel) {
                int result = ecs_world_iteration_begin(s->world);
                assert(result == ECS_OK);
                (void)result;
                const game_task_system *tasks = s->tasks;
                void *task = tasks->enqueue(run_batch, n, 1, &b, tasks->user);
                if (task) {
                    tasks->finish(task, tasks->user);
                }
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
                    errors[j].message[0] ? errors[j].message : "execution failed"
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
const char *game_schedule_error_message(const game_schedule *s)
{
    return s->error[0] ? s->error : NULL;
}
const char *game_schedule_warning(const game_schedule *s)
{
    return s->warning[0] ? s->warning : NULL;
}
void game_schedule_set_parallel(game_schedule *s, bool parallel)
{
    s->parallel = parallel;
}
int game_schedule_count(const game_schedule *s)
{
    return acount(s->order);
}
const char *game_schedule_at(const game_schedule *s, int position)
{
    return s->entries[s->order[position]].desc.name;
}
const game_schedule_desc *game_schedule_descriptor_at(const game_schedule *s, int position)
{
    assert(position >= 0 && position < acount(s->order));
    return &s->entries[s->order[position]].desc;
}
int game_schedule_parallel_batches(const game_schedule *s)
{
    return s->parallel_batches;
}

bool game_schedule_dependency(const game_schedule *s, const char *before_name, const char *after_name)
{
    int a = find(s, before_name), b = find(s, after_name);
    return !s->dirty && !s->error[0] && a >= 0 && b >= 0 &&
           s->edges[a * acount(s->entries) + b];
}
