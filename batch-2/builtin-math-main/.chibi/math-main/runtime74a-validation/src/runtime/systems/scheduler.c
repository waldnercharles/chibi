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
    schedule_desc desc;
    int serial;
} entry;
typedef struct
{
    int index, result;
    bool ran;
    schedule_error error;
} batch_entry;
enum
{
    BATCH_CACHE_ENTRIES = 128
};
struct schedule
{
    ecs_world *world;
    const task_system *tasks;
    entry *entries;
    int *order;
    unsigned char *edges;
    batch_entry *scratch;
    bool dirty, parallel, scratch_busy;
    int next_serial, parallel_batches;
    bool paused;
    system_phase paused_phase;
    int resume_at;
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
    free((void *)e->desc.reduces);
    free_names(e->desc.resource_reads, e->desc.resource_read_count);
    free_names(e->desc.resource_writes, e->desc.resource_write_count);
    free_names(e->desc.after, e->desc.after_count);
}
schedule *schedule_new(ecs_world *world, const task_system *tasks)
{
    schedule *s = xcalloc(1, sizeof *s);
    s->world = world;
    assert(!tasks || (tasks->workers > 0 && tasks->enqueue && tasks->finish));
    s->tasks = tasks;
    s->parallel = true;
    return s;
}
void schedule_free(schedule *s)
{
    for (int i = 0; i < acount(s->entries); i++) {
        free_entry(&s->entries[i]);
    }
    afree(s->entries);
    afree(s->order);
    afree(s->scratch);
    free(s->edges);
    free(s);
}
static int find(const schedule *s, const char *name)
{
    for (int i = 0; i < acount(s->entries); i++) {
        if (!strcmp(s->entries[i].desc.name, name)) {
            return i;
        }
    }
    return -1;
}
static bool names_equal(const char *const *a, const char *const *b, int count)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(a[i], b[i])) {
            return false;
        }
    }
    return true;
}
static bool same(const schedule_desc *a, const schedule_desc *b)
{
    return a->phase == b->phase && a->order == b->order &&
           a->read_count == b->read_count && a->write_count == b->write_count &&
           a->reduce_count == b->reduce_count &&
           a->resource_read_count == b->resource_read_count &&
           a->resource_write_count == b->resource_write_count &&
           a->after_count == b->after_count && a->declared == b->declared &&
           a->structural == b->structural && a->host_sink == b->host_sink &&
           a->world_read == b->world_read && a->deterministic == b->deterministic &&
           a->parallel_safe == b->parallel_safe && a->identity == b->identity &&
           a->run == b->run && a->run_parallel == b->run_parallel &&
           a->prepare == b->prepare && a->finish == b->finish &&
           a->parallel_ready == b->parallel_ready && a->maintain == b->maintain &&
           a->field_changed == b->field_changed && a->user == b->user &&
           (!a->read_count ||
            !memcmp(a->reads, b->reads, (size_t)a->read_count * sizeof *a->reads)) &&
           (!a->write_count ||
            !memcmp(a->writes, b->writes, (size_t)a->write_count * sizeof *a->writes)) &&
           (!a->reduce_count ||
            !memcmp(a->reduces, b->reduces, (size_t)a->reduce_count * sizeof *a->reduces)) &&
           names_equal(a->resource_reads, b->resource_reads, a->resource_read_count) &&
           names_equal(a->resource_writes, b->resource_writes, a->resource_write_count) &&
           names_equal(a->after, b->after, a->after_count);
}
int schedule_register(schedule *s, const schedule_desc *d)
{
    if (!d || !d->name || !d->name[0] || d->phase < 0 ||
        d->phase >= SYSTEM_PHASE_COUNT || !d->run || d->read_count < 0 ||
        d->write_count < 0 || d->reduce_count < 0 || d->resource_read_count < 0 ||
        d->resource_write_count < 0 || d->after_count < 0 ||
        (d->read_count && !d->reads) || (d->write_count && !d->writes) ||
        (d->reduce_count && (!d->reduces || !d->prepare || !d->finish)) ||
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
    int index = find(s, d->name);
    /* A failed run is retried by re-registering its descriptor. Keep that
     * rebuild path, and never discard an already pending topology change. */
    if (index >= 0 && !s->error[0] && same(&s->entries[index].desc, d)) {
        return 0;
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
    e.desc.reduces = xmalloc((size_t)d->reduce_count * sizeof *d->reduces);
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
    if (d->reduce_count) {
        memcpy(
            (void *)e.desc.reduces,
            d->reduces,
            (size_t)d->reduce_count * sizeof *d->reduces
        );
    }
    if (index < 0) {
        apush(s->entries, e);
    } else {
        e.serial = s->entries[index].serial;
        free_entry(&s->entries[index]);
        s->entries[index] = e;
    }
    s->dirty = true;
    s->paused = false;
    return 0;
}
void schedule_remove(schedule *s, const char *name)
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
    s->paused = false;
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
void schedule_reset(schedule *s)
{
    assert(s && !s->scratch_busy);
    s->paused = false;
    s->resume_at = 0;
    s->dirty = true;
}
bool schedule_paused(const schedule *s)
{
    return s->paused;
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
    const schedule_desc *x = &a->desc, *y = &b->desc;
    return barrier(a) || barrier(b) ||
           ids_intersect(x->writes, x->write_count, y->writes, y->write_count) ||
           ids_intersect(x->writes, x->write_count, y->reads, y->read_count) ||
           ids_intersect(y->writes, y->write_count, x->reads, x->read_count) ||
           ids_intersect(x->reduces, x->reduce_count, y->reads, y->read_count) ||
           ids_intersect(x->reduces, x->reduce_count, y->writes, y->write_count) ||
           ids_intersect(y->reduces, y->reduce_count, x->reads, x->read_count) ||
           ids_intersect(y->reduces, y->reduce_count, x->writes, x->write_count) ||
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
bool schedule_build(schedule *s)
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
    schedule *schedule;
    batch_entry *entries;
    double time, delta;
} batch;
static void run_batch(int start, int end, uint32_t, void *user)
{
    batch *b = user;
    for (int i = start; i < end; i++) {
        batch_entry *item = &b->entries[i];
        entry *e = &b->schedule->entries[item->index];
        item->ran = true;
        item->result = e->desc
                           .run(b->schedule->world, b->time, b->delta, e->desc.user, &item->error);
        if (item->result == FLOW_PAUSED_RESULT) {
            break;
        }
    }
}
int schedule_maintain(schedule *s, double time, double delta, schedule_error *error)
{
    if (!schedule_build(s)) {
        return FLOW_ERR_COMPILE;
    }
    for (int i = 0; i < acount(s->order); i++) {
        schedule_desc *d = &s->entries[s->order[i]].desc;
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

int schedule_field_changed(
    schedule *s,
    ecs_entity entity,
    ecs_component_id component,
    size_t offset,
    pin_value value,
    schedule_error *error
)
{
    if (!schedule_build(s)) {
        return FLOW_ERR_COMPILE;
    }
    for (int i = 0; i < acount(s->order); i++) {
        schedule_desc *d = &s->entries[s->order[i]].desc;
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

int schedule_run(schedule *s, system_phase phase, double time, double delta)
{
    if (!schedule_build(s)) {
        return FLOW_ERR_COMPILE;
    }
    int count = acount(s->entries);
    bool cache = !s->scratch_busy;
    batch_entry *items = cache ? s->scratch : NULL;
    if (cache) {
        s->scratch = NULL;
        s->scratch_busy = true;
    }
    int result = 0;
    if (s->paused && s->paused_phase != phase) {
        if (cache) {
            s->scratch = items;
            s->scratch_busy = false;
        }
        return FLOW_PAUSED_RESULT;
    }
    int start = s->paused ? s->resume_at : 0;
    s->paused = false;
    for (int at = start; at < count;) {
        int first = s->order[at];
        if (s->entries[first].desc.phase != phase) {
            at++;
            continue;
        }
        aclear(items);
        apush(items, ((batch_entry){ .index = first }));
        at++;
        while (at < count && s->entries[s->order[at]].desc.phase == phase &&
               !barrier(&s->entries[first])) {
            int next = s->order[at];
            bool independent = true;
            for (int j = 0; j < acount(items); j++) {
                independent &= !s->edges[items[j].index * count + next] &&
                               !conflict(&s->entries[items[j].index], &s->entries[next]);
            }
            if (!independent) {
                break;
            }
            apush(items, ((batch_entry){ .index = next }));
            at++;
        }
        int n = acount(items);
        bool safe = true;
        for (int j = 0; j < n; j++) {
            entry *e = &s->entries[items[j].index];
            if (e->desc.prepare) {
                items[j].result = e->desc.prepare(e->desc.user, &items[j].error);
                safe &= items[j].result >= 0;
            }
        }
        if (safe) {
            batch b = { s, items, time, delta };
            const schedule_desc *single = &s->entries[first].desc;
            bool rows = n == 1 && s->parallel && s->tasks &&
                        !barrier(&s->entries[first]) && single->run_parallel &&
                        (!single->parallel_ready ||
                         single->parallel_ready(single->user));
            bool parallel = s->parallel && s->tasks && n > 1;
            for (int j = 0; j < n && parallel; j++) {
                const schedule_desc *desc = &s->entries[items[j].index].desc;
                parallel = !desc->parallel_ready || desc->parallel_ready(desc->user);
            }
            if (rows) {
                items[0].ran = true;
                items[0].result = single->run_parallel(
                    single->user,
                    s->tasks,
                    &items[0].error
                );
            } else if (parallel) {
                int result = ecs_world_iteration_begin(s->world);
                assert(result == ECS_OK);
                (void)result;
                const task_system *tasks = s->tasks;
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
        bool commit = safe;
        for (int j = 0; j < n; j++) {
            schedule_desc *desc = &s->entries[items[j].index].desc;
            bool accepted = commit && items[j].ran &&
                            (items[j].result == FLOW_OK ||
                             items[j].result == FLOW_PAUSED_RESULT);
            commit &= items[j].ran && items[j].result == FLOW_OK;
            if (desc->finish) {
                schedule_error error = { 0 };
                int finished = desc->finish(desc->user, accepted, &error);
                if (items[j].result >= 0 && finished < 0) {
                    items[j].result = finished;
                    items[j].error = error;
                    commit = false;
                }
                if (finished == FLOW_PAUSED_RESULT) {
                    commit = false;
                    if (!result) {
                        s->paused = true;
                        s->paused_phase = phase;
                        s->resume_at = at - n + j;
                        result = FLOW_PAUSED_RESULT;
                    }
                }
            }
            if (items[j].result == FLOW_PAUSED_RESULT) {
                commit = false;
                if (!result) {
                    s->paused = true;
                    s->paused_phase = phase;
                    s->resume_at = at - n + j;
                    result = FLOW_PAUSED_RESULT;
                }
            }
            if (!result && items[j].result < 0) {
                snprintf(
                    s->error,
                    sizeof s->error,
                    "System %s: %s",
                    s->entries[items[j].index].desc.name,
                    items[j].error.message[0] ? items[j].error.message : "execution failed"
                );
                result = items[j].result;
            }
        }
        if (result != FLOW_OK) {
            break;
        }
    }
    if (cache) {
        s->scratch_busy = false;
        if (acap(items) <= BATCH_CACHE_ENTRIES) {
            s->scratch = items;
            items = NULL;
        }
    }
    afree(items);
    return result;
}
const char *schedule_error_message(const schedule *s)
{
    return s->error[0] ? s->error : NULL;
}
const char *schedule_warning(const schedule *s)
{
    return s->warning[0] ? s->warning : NULL;
}
void schedule_set_parallel(schedule *s, bool parallel)
{
    s->parallel = parallel;
}
int schedule_count(const schedule *s)
{
    return acount(s->order);
}
const char *schedule_at(const schedule *s, int position)
{
    return s->entries[s->order[position]].desc.name;
}
const schedule_desc *schedule_descriptor_at(const schedule *s, int position)
{
    assert(position >= 0 && position < acount(s->order));
    return &s->entries[s->order[position]].desc;
}
int schedule_parallel_batches(const schedule *s)
{
    return s->parallel_batches;
}
size_t schedule_scratch_bytes(const schedule *s)
{
    return s->scratch ? sizeof(dyna_hdr) + (size_t)acap(s->scratch) * sizeof *s->scratch
                      : 0;
}

bool schedule_dependency(const schedule *s, const char *before_name, const char *after_name)
{
    int a = find(s, before_name), b = find(s, after_name);
    return !s->dirty && !s->error[0] && a >= 0 && b >= 0 &&
           s->edges[a * acount(s->entries) + b];
}
