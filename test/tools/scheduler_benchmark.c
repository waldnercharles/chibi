#include "ecs/ecs.h"
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
            "checksum=%llu\n",
            refresh ? "refresh" : "run",
            SYSTEMS,
            PASSES,
            trial,
            elapsed * 1e9 / PASSES,
            (unsigned long long)(after.calls - before.calls),
            (unsigned long long)(after.requested_bytes - before.requested_bytes),
            (unsigned long long)checksum
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
void test_tool_scheduler_benchmark()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(scheduler_benchmark_identical_registration);
    RUN_TEST_CASE(scheduler_benchmark_warm_dispatch);
    RUN_TEST_CASE(scheduler_benchmark_replacement_preserves_owned_snapshots);
    pu_clear_setup();
}
