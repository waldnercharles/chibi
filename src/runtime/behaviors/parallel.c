#include "runtime/behaviors/parallel.h"

#include "diagnostics/tracy.h"
#include "graph/runtime/update_internal.h"
#include "runtime/task_pool.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum
{
    PARALLEL_MIN_ROWS = 1024,
    PARALLEL_MIN_CHUNK = 256,
    PARALLEL_MIN_WORK = 131072,
    PARALLEL_MIN_VALIDATE_ROWS = 8192
};
typedef struct
{
    behavior_update_row *rows;
    behavior_update_binding **bindings;
    int stop, result, system, prefix;
    unsigned worker;
    bool ready;
} update_chunk;
typedef struct
{
    update_chunk *chunks;
    void *code;
    int rows;
    unsigned query_categories;
} update_plan;
struct game_behavior_parallel
{
    game_task_pool *pool;
    update_plan *plans;
    bool invalidated;
};
static void clear_plans_from(game_behavior_parallel *parallel, int first)
{
    assert(first >= 0 && first <= acount(parallel->plans));
    for (int i = first; i < acount(parallel->plans); i++) {
        update_plan *plan = &parallel->plans[i];
        for (int j = 0; j < acount(plan->chunks); j++) {
            behavior_update_free_bindings(plan->chunks[j].bindings);
            afree(plan->chunks[j].rows);
        }
        afree(plan->chunks);
    }
    asetlen(parallel->plans, first);
}
static void clear_plans(game_behavior_parallel *parallel)
{
    clear_plans_from(parallel, 0);
}
game_behavior_parallel *game_behavior_parallel_new()
{
    game_behavior_parallel *p = xcalloc(1, sizeof *p);
    p->pool = game_task_pool_acquire();
    return p;
}
void game_behavior_parallel_free(game_behavior_parallel *p)
{
    clear_plans(p);
    afree(p->plans);
    game_task_pool_release(p->pool);
    free(p);
}
void game_behavior_parallel_invalidate(game_behavior_parallel *p)
{
    p->invalidated = true;
}
static bool compatible(const behavior_update_row *rows, behavior_update_span span)
{
    for (int i = 0; i < span.period; i++) {
        const behavior_function *a = rows[span.begin + i].binding->function;
        for (int j = 0; j <= i; j++) {
            if (!behavior_function_parallel_with(
                    a,
                    rows[span.begin + j].binding->function
                )) {
                return false;
            }
        }
    }
    uint64_t work = 0;
    for (int i = 0; i < span.period; i++) {
        work += behavior_function_parallel_work(rows[span.begin + i].binding->function);
    }
    return work * (uint64_t)(span.end - span.begin) / (unsigned)span.period >=
           PARALLEL_MIN_WORK;
}
void game_behavior_parallel_prepare(
    game_behavior_parallel *p,
    const behavior_update_row *rows,
    const behavior_update_span *spans,
    uint32_t slots
)
{
    game_behavior_parallel_prepare_incremental(p, rows, spans, slots, 0);
}

void game_behavior_parallel_prepare_incremental(
    game_behavior_parallel *p,
    const behavior_update_row *rows,
    const behavior_update_span *spans,
    uint32_t slots,
    int retained_spans
)
{
    assert(
        retained_spans >= 0 && retained_spans <= acount(p->plans) &&
        retained_spans <= acount(spans)
    );
    clear_plans_from(p, retained_spans);
    p->invalidated = false;
    const game_task_system *tasks = game_task_pool_tasks(p->pool);
    for (int i = retained_spans; i < acount(spans); i++) {
        apush(
            p->plans,
            ((update_plan){ .code = spans[i].code,
                            .rows = spans[i].end - spans[i].begin })
        );
    }
    if (tasks->workers == 1 || retained_spans == acount(spans)) {
        return;
    }
    int binding_count = 0;
    for (int s = retained_spans; s < acount(spans); s++) {
        behavior_update_span span = spans[s];
        for (int i = span.begin; i < span.end; i++) {
            if (rows[i].binding && rows[i].binding->index >= binding_count) {
                binding_count = rows[i].binding->index + 1;
            }
        }
    }
    behavior_update_binding **copies = xcalloc((size_t)binding_count, sizeof *copies);
    int *owners = xmalloc((size_t)slots * sizeof *owners);
    for (int s = retained_spans; s < acount(spans); s++) {
        behavior_update_span span = spans[s];
        update_plan *plan = &p->plans[s];
        int count = span.end - span.begin;
        if (count < PARALLEL_MIN_ROWS || !compatible(rows, span)) {
            continue;
        }
        for (int i = 0; i < span.period; i++) {
            plan->query_categories |= game_entities_query_categories(
                rows[span.begin + i].binding->function
            );
        }
        for (uint32_t i = 0; i < slots; i++) {
            owners[i] = -1;
        }
        int chunk_size = (count + tasks->workers - 1) / tasks->workers;
        if (chunk_size < PARALLEL_MIN_CHUNK) {
            chunk_size = PARALLEL_MIN_CHUNK;
        }
        chunk_size = (chunk_size + span.period - 1) / span.period * span.period;
        bool unique = true;
        for (int begin = span.begin; begin < span.end;) {
            int end = begin + chunk_size;
            while (end < span.end && rows[end - 1].entity == rows[end].entity) {
                end++;
            }
            if (end > span.end) {
                end = span.end;
            }
            memset(copies, 0, (size_t)binding_count * sizeof *copies);
            int chunk_index = acount(plan->chunks);
            apush(plan->chunks, ((update_chunk){ 0 }));
            update_chunk *chunk = &alast(plan->chunks);
            int phase = (begin - span.begin) % span.period;
            chunk->prefix = phase ? span.period - phase : 0;
            if (chunk->prefix > end - begin) {
                chunk->prefix = end - begin;
            }
            for (int i = begin; i < end; i++) {
                uint32_t slot = (uint32_t)rows[i].entity;
                assert(slot < slots);
                unique &= owners[slot] < 0 || owners[slot] == chunk_index;
                owners[slot] = chunk_index;
                behavior_update_row row = rows[i];
                int index = row.binding->index;
                if (!copies[index]) {
                    copies[index] = behavior_update_prepare(
                        &chunk->bindings,
                        row.instance,
                        row.binding->function,
                        row.binding->entry
                    );
                    assert(copies[index]);
                }
                row.binding = copies[index];
                apush(chunk->rows, row);
            }
            begin = end;
        }
        if (!unique) {
            for (int i = 0; i < acount(plan->chunks); i++) {
                behavior_update_free_bindings(plan->chunks[i].bindings);
                afree(plan->chunks[i].rows);
            }
            aclear(plan->chunks);
        }
    }
    free(copies);
    free(owners);
}

typedef struct
{
    update_plan *plan;
    const ecs_execution_view *world;
} update_context;
static void run_chunks(int start, int end, uint32_t worker, void *user)
{
    update_context *ctx = user;
    CHIBI_TRACE_ZONE_BEGIN(task_zone, "Behavior task");
    for (int i = start; i < end; i++) {
        CHIBI_TRACE_ZONE_BEGIN(chunk_zone, "Behavior chunk");
        update_chunk *chunk = &ctx->plan->chunks[i];
        chunk->worker = worker;
        behavior_update_row *row = chunk->rows, *end = row + acount(chunk->rows);
        behavior_update_row *stop = row;
        chunk->result = FLOW_OK;
        chunk->system = 0;
        if (chunk->prefix) {
            stop = behavior_update_run_c(
                row,
                row + chunk->prefix,
                ctx->world,
                &chunk->result,
                NULL
            );
        }
        if (!chunk->result && stop == row + chunk->prefix) {
            row = stop;
            stop = ((behavior_update_fn)
                        ctx->plan->code)(row, end, ctx->world, &chunk->result);
            chunk->system = (int)(stop - row) + (chunk->result < 0);
            if (stop == row && !chunk->result) {
                stop = behavior_update_run_c(row, end, ctx->world, &chunk->result, NULL);
            }
        }
        chunk->stop = (int)(stop - chunk->rows);
        CHIBI_TRACE_ZONE_END(chunk_zone);
    }
    CHIBI_TRACE_ZONE_END(task_zone);
}
static bool ready(const behavior_update_row *row, const ecs_execution_view *world)
{
    const behavior_instance *bi = row->instance;
    const behavior_update_binding *binding = row->binding;
    return bi->alive && !bi->trace_enabled && !bi->profile &&
           !acount(bi->activations) && bi->backend == BEHAVIOR_SLJIT &&
           bi->asset == binding->asset &&
           bi->services_revision == row->services_revision &&
           (!binding->context.world || binding->context.world == world) &&
           (!binding->context.binding_world || binding->context.binding_world == world) &&
           ecs_execution_entity(world, row->entity);
}

static void validate_chunks(int start, int end, uint32_t, void *user)
{
    update_context *ctx = user;
    CHIBI_TRACE_ZONE_BEGIN(zone, "Validate behavior chunk");
    for (int c = start; c < end; c++) {
        update_chunk *chunk = &ctx->plan->chunks[c];
        chunk->ready = true;
        for (int i = 0; i < acount(chunk->rows); i++) {
            if (!ready(&chunk->rows[i], ctx->world)) {
                chunk->ready = false;
                break;
            }
        }
        if (chunk->ready) {
            behavior_update_refresh(chunk->bindings);
        }
    }
    CHIBI_TRACE_ZONE_END(zone);
}

bool game_behavior_parallel_run(
    game_behavior_parallel *p,
    int span,
    const ecs_execution_view *world,
    game_entities *entities,
    game_behavior_update_stats *stats
)
{
    if (p->invalidated) {
        return false;
    }
    update_plan *plan = &p->plans[span];
    if (acount(plan->chunks) < 2) {
        return false;
    }
    const game_task_system *tasks = game_task_pool_tasks(p->pool);
    update_context ctx = { plan, world };
    game_task_pool_begin(p->pool, true);
    CHIBI_TRACE_ZONE_BEGIN(validation_zone, "Validate parallel Tick rows");
    if (plan->rows < PARALLEL_MIN_VALIDATE_ROWS) {
        validate_chunks(0, acount(plan->chunks), 0, &ctx);
    } else {
        void *task = tasks->enqueue(
            validate_chunks,
            acount(plan->chunks),
            1,
            &ctx,
            tasks->user
        );
        tasks->finish(task, tasks->user);
    }
    CHIBI_TRACE_ZONE_END(validation_zone);
    /* Join every read-only check before any effects. A blocked row rejects
     * the whole span, so the serial retry cannot repeat another chunk's work.
     * Each chunk owns its refreshed bindings; the world is stable throughout. */
    for (int c = 0; c < acount(plan->chunks); c++) {
        if (!plan->chunks[c].ready) {
            game_task_pool_end(p->pool);
            return false;
        }
    }
    CHIBI_TRACE_ZONE_BEGIN(query_zone, "Prepare parallel behavior queries");
    bool queries_ready = game_entities_parallel_begin(entities, plan->query_categories);
    CHIBI_TRACE_ZONE_END(query_zone);
    if (!queries_ready) {
        game_task_pool_end(p->pool);
        return false;
    }
    CHIBI_TRACE_ZONE_BEGIN(parallel_zone, "Parallel behavior execution");
    void *task = tasks->enqueue(run_chunks, acount(plan->chunks), 1, &ctx, tasks->user);
    tasks->finish(task, tasks->user);
    game_task_pool_end(p->pool);
    CHIBI_TRACE_ZONE_END(parallel_zone);
    unsigned workers = 0;
    for (int c = 0; c < acount(plan->chunks); c++) {
        update_chunk *chunk = &plan->chunks[c];
        workers |= 1u << chunk->worker;
        stats->parallel += chunk->stop + (chunk->result < 0);
        stats->system_loop += chunk->system;
        int count = acount(chunk->rows);
        while (chunk->stop < count) {
            behavior_update_row *row = &chunk->rows[chunk->stop];
            /* Match the serial scheduler: retain this failure once, then
             * continue subsequent entries. Worker scratch is still untouched. */
            if (chunk->result < 0) {
                flow_run *failed = behavior_update_failure(row, chunk->result);
                behavior_instance_release_activation(row->instance, failed);
                chunk->stop++;
            }
            behavior_update_row *stop = behavior_update_run_c(
                chunk->rows + chunk->stop,
                chunk->rows + count,
                world,
                &chunk->result,
                NULL
            );
            assert(stop > chunk->rows + chunk->stop || chunk->result < 0 || chunk->stop == count);
            chunk->stop = (int)(stop - chunk->rows);
        }
        stats->prepared += count;
        stats->native_loop += count;
    }
    game_entities_parallel_end(entities);
    stats->parallel_tasks += acount(plan->chunks);
    int used = __builtin_popcount(workers);
    if (used > stats->workers) {
        stats->workers = used;
    }
    return true;
}
