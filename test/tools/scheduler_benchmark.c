#include "ecs/ecs.h"
#include "execution/status.h"
#include "runtime/systems/scheduler.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum
{
    SYSTEMS = 12,
    PASSES = 128,
    TRIALS = 5
};
typedef struct
{
    int calls, weight;
    uint64_t checksum;
} counter;
static ecs_world *world;
static schedule *scheduled;
static counter counters[SYSTEMS];

static void setup()
{
    world = ecs_world_new();
    scheduled = schedule_new(world, NULL);
    memset(counters, 0, sizeof counters);
    for (int i = 0; i < SYSTEMS; i++) {
        counters[i].weight = i + 1;
    }
}
static void cleanup()
{
    schedule_free(scheduled);
    ecs_world_free(world);
    scheduled = NULL;
    world = NULL;
}
static int count(ecs_world *, double, double, void *user, schedule_error *)
{
    counter *c = user;
    c->calls++;
    c->checksum += c->weight;
    return 0;
}
static schedule_desc descriptor(const char *name, int index)
{
    return (schedule_desc){ .name = name,
                            .phase = SYSTEM_PRE_PHYSICS,
                            .declared = true,
                            .deterministic = true,
                            .parallel_safe = true,
                            .identity = &counters[index],
                            .run = count,
                            .user = &counters[index] };
}
static double now()
{
    struct timespec value;
    int result = clock_gettime(CLOCK_MONOTONIC, &value);
    assert(!result);
    (void)result;
    return (double)value.tv_sec + (double)value.tv_nsec * 1e-9;
}
static bool measure(bool refresh)
{
    char names[SYSTEMS][24];
    uint64_t writes[SYSTEMS], reads = 900;
    const char *resources[] = { "benchmark.clock" };
    schedule_desc descs[SYSTEMS];
    for (int i = 0; i < SYSTEMS; i++) {
        snprintf(names[i], sizeof names[i], "benchmark.%02d", i);
        writes[i] = 1000 + i;
        descs[i] = descriptor(names[i], i);
        descs[i].reads = &reads;
        descs[i].read_count = 1;
        descs[i].writes = &writes[i];
        descs[i].write_count = 1;
        descs[i].resource_reads = resources;
        descs[i].resource_read_count = 1;
        REQUIRE(!schedule_register(scheduled, &descs[i]));
    }
    REQUIRE(schedule_build(scheduled));
    REQUIRE(schedule_count(scheduled) == SYSTEMS);
    REQUIRE(!schedule_run(scheduled, SYSTEM_PRE_PHYSICS, 0, 0));
    for (int i = 0; i < SYSTEMS; i++) {
        REQUIRE(counters[i].calls == 1 && counters[i].checksum == (uint64_t)(i + 1));
    }
    /* Setup, warmup, checks and printing are outside measured intervals. Each
     * callback owns its counter; no engine frames or worker threads run. */
    for (int trial = 0; trial < TRIALS; trial++) {
        for (int i = 0; i < SYSTEMS; i++) {
            counters[i].calls = 0;
            counters[i].checksum = 0;
        }
        xalloc_stats before, after;
        xalloc_stats_get(&before);
        double start = now();
        int status = 0;
        for (int pass = 0; pass < PASSES; pass++) {
            if (refresh) {
                for (int i = 0; i < SYSTEMS; i++) {
                    status |= schedule_register(scheduled, &descs[i]);
                }
            }
            status |= schedule_run(scheduled, SYSTEM_PRE_PHYSICS, pass, 1);
        }
        double elapsed = now() - start;
        xalloc_stats_get(&after);
        REQUIRE(!status);
        uint64_t checksum = 0;
        for (int i = 0; i < SYSTEMS; i++) {
            REQUIRE(counters[i].calls == PASSES);
            REQUIRE(counters[i].checksum == (uint64_t)PASSES * (i + 1));
            checksum += counters[i].checksum;
        }
        REQUIRE(checksum == (uint64_t)PASSES * SYSTEMS * (SYSTEMS + 1) / 2);
        printf(
            "BENCH scheduler=%s systems=%d passes=%d trial=%d "
            "ns/pass=%.2f xalloc_calls=%llu requested_bytes=%llu "
            "checksum=%llu retained_bytes=%zu\n",
            refresh ? "refresh" : "run",
            SYSTEMS,
            PASSES,
            trial,
            elapsed * 1e9 / PASSES,
            (unsigned long long)(after.calls - before.calls),
            (unsigned long long)(after.requested_bytes - before.requested_bytes),
            (unsigned long long)checksum,
            schedule_scratch_bytes(scheduled)
        );
    }
    return true;
}
TEST_CASE(scheduler_benchmark_identical_registration)
{
    return measure(true);
}
TEST_CASE(scheduler_benchmark_warm_dispatch)
{
    return measure(false);
}
TEST_CASE(scheduler_benchmark_replacement_preserves_owned_snapshots)
{
    uint64_t writes = 11, reads = 12;
    schedule_desc a = descriptor("A", 0), b = descriptor("B", 1);
    a.writes = &writes;
    a.write_count = 1;
    b.reads = &reads;
    b.read_count = 1;
    REQUIRE(!schedule_register(scheduled, &a));
    REQUIRE(!schedule_register(scheduled, &b));
    REQUIRE(schedule_build(scheduled));
    REQUIRE(!schedule_dependency(scheduled, "A", "B"));
    writes = 12;
    REQUIRE(schedule_build(scheduled));
    REQUIRE(schedule_descriptor_at(scheduled, 0)->writes[0] == 11);
    REQUIRE(!schedule_dependency(scheduled, "A", "B"));
    REQUIRE(!schedule_register(scheduled, &a));
    REQUIRE(schedule_build(scheduled));
    REQUIRE(schedule_dependency(scheduled, "A", "B"));
    REQUIRE(schedule_descriptor_at(scheduled, 0)->writes[0] == 12);
    char dependency[16] = "A";
    const char *after[] = { dependency };
    b.after = after;
    b.after_count = 1;
    REQUIRE(!schedule_register(scheduled, &b));
    REQUIRE(schedule_build(scheduled));
    strcpy(dependency, "missing");
    REQUIRE(schedule_build(scheduled));
    REQUIRE(!strcmp(schedule_descriptor_at(scheduled, 1)->after[0], "A"));
    REQUIRE(!schedule_register(scheduled, &b));
    REQUIRE(!schedule_build(scheduled));
    REQUIRE(strstr(schedule_error_message(scheduled), "missing"));
    b.after_count = 0;
    a.phase = SYSTEM_POST_PHYSICS;
    REQUIRE(!schedule_register(scheduled, &a));
    REQUIRE(!schedule_register(scheduled, &b));
    REQUIRE(schedule_build(scheduled));
    REQUIRE(!strcmp(schedule_at(scheduled, 0), "B"));
    REQUIRE(schedule_dependency(scheduled, "B", "A"));
    a.name = "aliased identity";
    REQUIRE(schedule_register(scheduled, &a) < 0);
    REQUIRE(schedule_count(scheduled) == 2);
    REQUIRE(!schedule_run(scheduled, SYSTEM_PRE_PHYSICS, 0, 0));
    REQUIRE(counters[0].calls == 0 && counters[1].calls == 1);
    REQUIRE(!schedule_run(scheduled, SYSTEM_POST_PHYSICS, 0, 0));
    REQUIRE(counters[0].calls == 1 && counters[1].calls == 1);
    return true;
}
TEST_CASE(scheduler_unchanged_descriptors_and_small_batches_allocate_nothing)
{
    uint64_t reads = 11, writes = 12;
    const char *resources[] = { "clock" };
    schedule_desc a = descriptor("A", 0), b = descriptor("B", 1);
    a.reads = &reads;
    a.read_count = 1;
    a.writes = &writes;
    a.write_count = 1;
    a.resource_reads = resources;
    a.resource_read_count = 1;
    REQUIRE(!schedule_register(scheduled, &a));
    REQUIRE(!schedule_register(scheduled, &b));
    REQUIRE(!schedule_run(scheduled, SYSTEM_PRE_PHYSICS, 0, 0));
    size_t retained = schedule_scratch_bytes(scheduled);
    REQUIRE(retained > 0 && retained <= 32 * 1024);
    /* Equal metadata at different source addresses is still the same owned
     * descriptor. Compare its values, not caller pointer identities. */
    char name[] = "A", resource[] = "clock";
    uint64_t copied_reads = reads, copied_writes = writes;
    const char *copied_resources[] = { resource };
    a.name = name;
    a.reads = &copied_reads;
    a.writes = &copied_writes;
    a.resource_reads = copied_resources;
    xalloc_stats before, after;
    xalloc_stats_get(&before);
    REQUIRE(!schedule_register(scheduled, &a));
    REQUIRE(!schedule_register(scheduled, &b));
    REQUIRE(schedule_build(scheduled));
    REQUIRE(!schedule_run(scheduled, SYSTEM_PRE_PHYSICS, 1, 1));
    xalloc_stats_get(&after);
    REQUIRE(after.calls == before.calls && after.requested_bytes == before.requested_bytes);
    REQUIRE(schedule_scratch_bytes(scheduled) == retained);
    REQUIRE(counters[0].calls == 2 && counters[1].calls == 2);
    schedule_remove(scheduled, "B");
    REQUIRE(!schedule_register(scheduled, &a));
    REQUIRE(schedule_build(scheduled) && schedule_count(scheduled) == 1);
    REQUIRE(!schedule_run(scheduled, SYSTEM_PRE_PHYSICS, 2, 1));
    REQUIRE(counters[0].calls == 3 && counters[1].calls == 2);
    return true;
}
TEST_CASE(scheduler_effect_and_order_changes_rebuild_dependencies)
{
    schedule_desc a = descriptor("A", 0), b = descriptor("B", 1);
    REQUIRE(!schedule_register(scheduled, &b));
    REQUIRE(!schedule_register(scheduled, &a));
    for (int flag = 0; flag < 6; flag++) {
        a = descriptor("A", 0);
        REQUIRE(!schedule_register(scheduled, &a) && schedule_build(scheduled));
        REQUIRE(!schedule_dependency(scheduled, "B", "A"));
        switch (flag) {
            case 0: a.declared = false; break;
            case 1: a.deterministic = false; break;
            case 2: a.parallel_safe = false; break;
            case 3: a.structural = true; break;
            case 4: a.host_sink = true; break;
            case 5: a.world_read = true; break;
        }
        REQUIRE(!schedule_register(scheduled, &a) && schedule_build(scheduled));
        REQUIRE(schedule_dependency(scheduled, "B", "A"));
    }
    a = descriptor("A", 0);
    REQUIRE(!schedule_register(scheduled, &a) && schedule_build(scheduled));
    a.order = -1;
    REQUIRE(!schedule_register(scheduled, &a) && schedule_build(scheduled));
    REQUIRE(schedule_dependency(scheduled, "A", "B"));
    REQUIRE(!strcmp(schedule_at(scheduled, 0), "A"));
    const char *writes[] = { "audio" }, *reads[] = { "clock" };
    a.resource_writes = writes;
    a.resource_write_count = 1;
    a.resource_reads = reads;
    a.resource_read_count = 1;
    REQUIRE(!schedule_register(scheduled, &a) && schedule_build(scheduled));
    writes[0] = "input";
    REQUIRE(!schedule_register(scheduled, &a) && schedule_build(scheduled));
    const schedule_desc *copy = schedule_descriptor_at(scheduled, 0);
    REQUIRE(!strcmp(copy->resource_writes[0], "input"));
    reads[0] = "random";
    REQUIRE(!schedule_register(scheduled, &a) && schedule_build(scheduled));
    copy = schedule_descriptor_at(scheduled, 0);
    REQUIRE(!strcmp(copy->resource_reads[0], "random"));
    return true;
}
typedef struct
{
    int runs, prepares, maintains, fields;
    bool fail_prepare, fail_run;
} hooks;
static int run_hook(ecs_world *, double, double, void *user, schedule_error *)
{
    hooks *h = user;
    h->runs++;
    return h->fail_run ? FLOW_ERR_NODE : 0;
}
static int other_run(ecs_world *, double, double, void *user, schedule_error *)
{
    ((hooks *)user)->runs += 2;
    return 0;
}
static int prepare_hook(void *user, schedule_error *error)
{
    hooks *h = user;
    h->prepares++;
    if (h->fail_prepare) {
        strcpy(error->message, "prepare");
        return FLOW_ERR_ARGUMENT;
    }
    return 0;
}
static int maintain_hook(void *user, double, double, schedule_error *)
{
    ((hooks *)user)->maintains++;
    return 0;
}
static int field_hook(void *user, ecs_entity, ecs_component_id, size_t, pin_value, schedule_error *)
{
    ((hooks *)user)->fields++;
    return 0;
}
static bool not_ready(void *)
{
    return false;
}
static int enqueues;
static void *inline_enqueue(task_range *run, int count, int, void *context, void *)
{
    enqueues++;
    run(0, count, 0, context);
    return NULL;
}
static void inline_finish(void *, void *)
{
    assert(false); /* Inline enqueue has already completed and returns NULL. */
}
TEST_CASE(scheduler_callback_changes_and_retries_keep_failure_order)
{
    static const task_system tasks = { .workers = 1,
                                       .enqueue = inline_enqueue,
                                       .finish = inline_finish };
    schedule_free(scheduled);
    scheduled = schedule_new(world, &tasks);
    enqueues = 0;
    hooks first = { 0 }, second = { 0 }, replacement = { 0 };
    schedule_desc a = descriptor("A", 0), b = descriptor("B", 1);
    a.run = b.run = run_hook;
    a.identity = a.user = &first;
    b.identity = b.user = &second;
    REQUIRE(!schedule_register(scheduled, &a));
    REQUIRE(!schedule_register(scheduled, &b));
    REQUIRE(!schedule_run(scheduled, SYSTEM_PRE_PHYSICS, 0, 0));
    REQUIRE(enqueues == 1 && first.runs == 1 && second.runs == 1);
    a.prepare = prepare_hook;
    REQUIRE(!schedule_register(scheduled, &a));
    b.prepare = prepare_hook;
    REQUIRE(!schedule_register(scheduled, &b));
    first.fail_prepare = true;
    REQUIRE(schedule_run(scheduled, SYSTEM_PRE_PHYSICS, 0, 0) == FLOW_ERR_ARGUMENT);
    REQUIRE(first.prepares == 1 && second.prepares == 1);
    REQUIRE(first.runs == 1 && second.runs == 1 && enqueues == 1);
    REQUIRE(!strcmp(schedule_error_message(scheduled), "System A: prepare"));
    first.fail_prepare = false;
    REQUIRE(!schedule_register(scheduled, &a));
    REQUIRE(!schedule_run(scheduled, SYSTEM_PRE_PHYSICS, 0, 0));
    REQUIRE(first.runs == 2 && second.runs == 2 && enqueues == 2);
    first.fail_run = true;
    REQUIRE(schedule_run(scheduled, SYSTEM_PRE_PHYSICS, 0, 0) == FLOW_ERR_NODE);
    REQUIRE(first.runs == 3 && second.runs == 3 && enqueues == 3);
    REQUIRE(!strcmp(schedule_error_message(scheduled), "System A: execution failed"));
    first.fail_run = false;
    REQUIRE(!schedule_register(scheduled, &a));
    a.parallel_ready = not_ready;
    REQUIRE(!schedule_register(scheduled, &a));
    REQUIRE(!schedule_run(scheduled, SYSTEM_PRE_PHYSICS, 0, 0));
    REQUIRE(first.runs == 4 && second.runs == 4 && enqueues == 3);
    a.run = other_run;
    REQUIRE(!schedule_register(scheduled, &a));
    REQUIRE(!schedule_run(scheduled, SYSTEM_PRE_PHYSICS, 0, 0));
    REQUIRE(first.runs == 6 && second.runs == 5);
    a.user = &replacement;
    REQUIRE(!schedule_register(scheduled, &a));
    REQUIRE(!schedule_run(scheduled, SYSTEM_PRE_PHYSICS, 0, 0));
    REQUIRE(first.runs == 6 && replacement.runs == 2 && second.runs == 6);
    a.identity = &replacement;
    REQUIRE(!schedule_register(scheduled, &a));
    schedule_desc c = descriptor("C", 2);
    c.identity = &first;
    REQUIRE(!schedule_register(scheduled, &c));
    schedule_remove(scheduled, "C");
    a.maintain = maintain_hook;
    REQUIRE(!schedule_register(scheduled, &a));
    schedule_error error = { 0 };
    REQUIRE(!schedule_maintain(scheduled, 0, 0, &error));
    REQUIRE(replacement.maintains == 1);
    a.field_changed = field_hook;
    REQUIRE(!schedule_register(scheduled, &a));
    REQUIRE(
        !schedule_field_changed(scheduled, (ecs_entity){ 0 }, 1, 0, (pin_value){ 0 }, &error)
    );
    REQUIRE(replacement.fields == 1);
    a.run = run_hook;
    replacement.fail_run = true;
    b.order = 1;
    REQUIRE(!schedule_register(scheduled, &a));
    REQUIRE(!schedule_register(scheduled, &b));
    REQUIRE(schedule_run(scheduled, SYSTEM_PRE_PHYSICS, 0, 0) == FLOW_ERR_NODE);
    REQUIRE(replacement.runs == 3 && second.runs == 6);
    return true;
}
static int nested_run(ecs_world *, double, double, void *, schedule_error *error)
{
    int result = schedule_run(scheduled, SYSTEM_POST_PHYSICS, 0, 0);
    assert(!result);
    (void)result;
    strcpy(error->message, "outer failure");
    return FLOW_ERR_NODE;
}
TEST_CASE(scheduler_recursive_dispatch_keeps_outer_batch_storage)
{
    schedule_desc a = descriptor("A", 0), b = descriptor("B", 1);
    a.run = nested_run;
    b.phase = SYSTEM_POST_PHYSICS;
    REQUIRE(!schedule_register(scheduled, &a));
    REQUIRE(!schedule_register(scheduled, &b));
    REQUIRE(schedule_run(scheduled, SYSTEM_PRE_PHYSICS, 0, 0) == FLOW_ERR_NODE);
    REQUIRE(counters[1].calls == 1);
    REQUIRE(!strcmp(schedule_error_message(scheduled), "System A: outer failure"));
    REQUIRE(schedule_scratch_bytes(scheduled) > 0);
    return true;
}
TEST_CASE(scheduler_large_batches_do_not_retain_unbounded_scratch)
{
    enum
    {
        LARGE = 129
    };
    counter values[LARGE] = { 0 };
    for (int i = 0; i < LARGE; i++) {
        char name[24];
        snprintf(name, sizeof name, "large.%03d", i);
        values[i].weight = i + 1;
        schedule_desc d = descriptor(name, 0);
        d.identity = d.user = &values[i];
        REQUIRE(!schedule_register(scheduled, &d));
    }
    REQUIRE(!schedule_run(scheduled, SYSTEM_PRE_PHYSICS, 0, 0));
    REQUIRE(schedule_scratch_bytes(scheduled) == 0);
    for (int i = 0; i < LARGE; i++) {
        REQUIRE(values[i].calls == 1 && values[i].checksum == (uint64_t)(i + 1));
        if (i) {
            char name[24];
            snprintf(name, sizeof name, "large.%03d", i);
            schedule_remove(scheduled, name);
        }
    }
    REQUIRE(!schedule_run(scheduled, SYSTEM_PRE_PHYSICS, 0, 0));
    REQUIRE(values[0].calls == 2);
    REQUIRE(
        schedule_scratch_bytes(scheduled) > 0 &&
        schedule_scratch_bytes(scheduled) <= 32 * 1024
    );
    return true;
}
void test_tool_scheduler_benchmark()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(scheduler_benchmark_identical_registration);
    RUN_TEST_CASE(scheduler_benchmark_warm_dispatch);
    RUN_TEST_CASE(scheduler_benchmark_replacement_preserves_owned_snapshots);
    RUN_TEST_CASE(scheduler_unchanged_descriptors_and_small_batches_allocate_nothing);
    RUN_TEST_CASE(scheduler_effect_and_order_changes_rebuild_dependencies);
    RUN_TEST_CASE(scheduler_callback_changes_and_retries_keep_failure_order);
    RUN_TEST_CASE(scheduler_recursive_dispatch_keeps_outer_batch_storage);
    RUN_TEST_CASE(scheduler_large_batches_do_not_retain_unbounded_scratch);
    pu_clear_setup();
}
