#include "authoring/language_catalog.h"
#include "authoring/source_prepare.h"
#include "execution/inspection.h"
#include "execution/status.h"
#include "host/native_default.h"
#include "runtime/behaviors/source.h"
#include "runtime/component_catalog.h"
#include "runtime/declared_components.h"
#include "runtime/engine.h"
#include "runtime/task_pool.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum
{
    TOTAL,
    SINGLE,
    COUNT,
    BOUNDED,
    LIMITED,
    MINIMUM,
    MAXIMUM
};
enum
{
    TARGET,
    ALTERNATE,
    AMOUNT,
    CANDIDATE
};
enum
{
    SEEN_TOTAL,
    SEEN_COUNT,
    SEEN_HITS
};
enum
{
    OWNER_MAX = 256,
    SYSTEM_MAX = 3,
    BENCH_PASSES = 8,
    BENCH_TRIALS = 5
};

typedef struct
{
    ecs_world *world;
    declared_components *declared;
    const ecs_component_desc *totals, *contribution, *seen;
    authoring_language_catalog *catalog;
    source_registry *sources;
    engine *engine;
    entities *entities;
    physics *physics;
    execution_inspector *inspector;
    execution_program *pending_program;
    ecs_entity owners[OWNER_MAX];
    behavior_backend backend;
} fixture;
static fixture f;
static void cleanup_driver();

static double now()
{
    struct timespec value;
    int status = clock_gettime(CLOCK_MONOTONIC, &value);
    assert(!status);
    (void)status;
    return value.tv_sec + value.tv_nsec * 1e-9;
}
static void cleanup()
{
    cleanup_driver();
    if (f.pending_program) {
        execution_program_discard_reductions(f.pending_program);
        execution_program_finish_system(f.pending_program);
    }
    engine_free(f.engine);
    execution_inspector_free(f.inspector);
    if (f.physics) {
        physics_free(f.physics);
    }
    if (f.entities) {
        entities_free(f.entities);
    }
    if (f.sources) {
        source_registry_free(f.sources);
    }
    authoring_language_catalog_free(f.catalog);
    ecs_world_free(f.world);
    declared_components_free(f.declared);
    memset(&f, 0, sizeof f);
}
static bool create(int owners, behavior_backend backend)
{
    assert(owners <= OWNER_MAX);
    const declared_field totals[] = {
        { .name = "total", .type = "f64" },
        { .name = "single", .type = "number" },
        { .name = "count", .type = "int" },
        { .name = "bounded", .type = "f64", .min = 0, .max = 10 },
        { .name = "limited", .type = "int", .min = 0, .max = 10 },
        { .name = "minimum", .type = "f64" },
        { .name = "maximum", .type = "f64" },
    };
    const declared_field contribution[] = {
        { .name = "target", .type = "entity" },
        { .name = "alternate", .type = "entity" },
        { .name = "amount", .type = "f64" },
        { .name = "candidate", .type = "f64" },
    };
    const declared_field seen[] = {
        { .name = "total", .type = "f64" },
        { .name = "count", .type = "int" },
        { .name = "hits", .type = "int" },
    };
    const declared_component components[] = {
        { .name = "ReductionTotals", .fields = totals, .field_count = 7 },
        { .name = "ReductionContribution", .fields = contribution, .field_count = 4 },
        { .name = "ReductionSeen", .fields = seen, .field_count = 3 },
    };
    char error[256];
    f.declared = declared_components_new(components, 3, error, sizeof error);
    REQUIRE(f.declared);
    ecs_component_catalog declarations = declared_components_ecs(f.declared);
    f.totals = declarations.components[0];
    f.contribution = declarations.components[1];
    f.seen = declarations.components[2];
    f.world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(!ecs_world_register_component(f.world, REGISTRY_COMPONENTS[i], NULL));
    }
    for (int i = 0; i < declarations.component_count; i++) {
        REQUIRE(!ecs_world_register_component(f.world, declarations.components[i], NULL));
    }
    native_capabilities *native = host_native_default_catalog_new(NULL, 0, NULL, 0, error, sizeof error);
    REQUIRE(native);
    authoring_language_catalog_input input = { .native = native };
    f.catalog = authoring_language_catalog_new(&input, error, sizeof error);
    native_capabilities_free(native);
    REQUIRE(f.catalog);
    f.sources = source_registry_new(f.world);
    f.backend = backend;
    for (int i = 0; i < owners; i++) {
        f.owners[i] = ecs_world_create_entity(f.world);
        REQUIRE(!ecs_world_record_add(f.world, f.owners[i], f.totals->id, NULL, 0));
        REQUIRE(!ecs_world_record_add(f.world, f.owners[i], f.seen->id, NULL, 0));
    }
    REQUIRE(!ecs_world_apply_commands(f.world));
    REQUIRE(!ecs_component_track_changes(f.world, f.totals->id));
    return true;
}
static void write_field(ecs_entity owner, const ecs_component_desc *desc, int field, pin_value value)
{
    int status = ecs_component_write_field(f.world, owner, desc->id, field, value);
    assert(!status);
    (void)status;
}
static pin_value read_field(ecs_entity owner, const ecs_component_desc *desc, int field)
{
    pin_value value = { 0 };
    int status = ecs_component_read_field(NULL, f.world, owner, desc->id, field, &value);
    assert(!status);
    (void)status;
    return value;
}
static bool publish(const char *name, const char *text)
{
    script_lower_bindings bindings = authoring_language_bindings(f.catalog);
    authoring_source *source = authoring_source_prepare_document(
        name,
        name,
        text,
        authoring_language_catalog_view(f.catalog),
        &bindings,
        f.backend
    );
    if (!source->valid) {
        for (int i = 0; i < source->diagnostic_count; i++) {
            fprintf(stderr, "%s: %s\n", name, source->diagnostics[i].message);
        }
    }
    bool valid = source->valid;
    int status = valid ? source_register(f.sources, name, source->module, NULL) : -1;
    authoring_source_free(source);
    REQUIRE(valid && !status);
    return true;
}
static bool bind_engine(const char *const *names, int count, bool parallel)
{
    for (int i = 0; i < count; i++) {
        ecs_entity binding = ecs_world_create_entity(f.world);
        EngineSystem value = { names[i], SYSTEM_PRE_PHYSICS, 0 };
        REQUIRE(!ecs_world_record_add(f.world, binding, COMPONENT_EngineSystem.id, &value, 0));
    }
    REQUIRE(!ecs_world_apply_commands(f.world));
    engine_modules modules = { .sources = f.sources };
    f.engine = engine_new(f.world, &modules);
    REQUIRE(f.engine);
    schedule_set_parallel((schedule *)engine_system_schedule(f.engine), parallel);
    REQUIRE(!engine_frame(f.engine, 0));
    return true;
}
static int frame(int step)
{
    int status = engine_frame(f.engine, step * ENGINE_FIXED_SECONDS);
    if (status < 0) {
        fprintf(stderr, "reduction frame: %s\n", engine_error(f.engine));
    }
    return status;
}
static bool effects(const char *name)
{
    execution_effect_summary summary = execution_module_effects(
        execution_program_module(source_program(f.sources, name))
    );
    REQUIRE(summary.parallel_safe && summary.deterministic);
    REQUIRE(summary.reduce_count == 1 && summary.reduces[0] == f.totals->id);
    REQUIRE(summary.read_count == 0 && summary.write_count == 0);
    return true;
}
static const char *numeric_a = "ReduceA :: system(t:^ReductionTotals){reduce "
                               "t.total += 10000000000000000; reduce t.total "
                               "+= -10000000000000000;reduce t.single += "
                               "16777216; reduce t.single += -16777216;reduce "
                               "t.count += 1;reduce t.bounded += 100; reduce "
                               "t.bounded += -1;reduce t.limited += 100; "
                               "reduce t.limited += -1;reduce "
                               "min(t.minimum,-4); reduce max(t.maximum,4);}";
static const char *numeric_b = "ReduceB :: system(t:^ReductionTotals){reduce "
                               "t.total += 1; reduce t.single += 1; reduce "
                               "t.count += 1;reduce t.bounded += 100; reduce "
                               "t.bounded += -1;reduce t.limited += 100; "
                               "reduce t.limited += -1;reduce "
                               "min(t.minimum,-6); reduce max(t.maximum,6);}";

TEST_CASE(source_reductions_publish_before_dependent_filtered_query)
{
    for (int backend = 0; backend < 2; backend++) {
        for (int parallel = 0; parallel < 2; parallel++) {
            REQUIRE(create(4, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
            REQUIRE(publish("ReduceA", numeric_a));
            REQUIRE(publish("ReduceB", numeric_b));
            REQUIRE(publish(
                "Observe",
                "Observe :: system( t:^ReductionTotals, s:^ReductionSeen , if "
                "t.total == 1 and changed(t)) "
                "{s.total=t.total; s.count=t.count; s.hits+=1;}"
            ));
            REQUIRE(effects("ReduceA") && effects("ReduceB"));
            uint64_t versions[4];
            for (int i = 0; i < 4; i++) {
                write_field(f.owners[i], f.totals, TOTAL, (pin_value){ .type = PIN_F64, .d = 0 });
                write_field(f.owners[i], f.totals, SINGLE, (pin_value){ .type = PIN_F32, .f = 1 });
                write_field(f.owners[i], f.totals, COUNT, (pin_value){ .type = PIN_I32, .i = INT_MAX });
                versions[i] = ecs_component_version(
                    f.world,
                    f.owners[i],
                    f.totals->id
                );
            }
            const char *names[] = { "ReduceA", "ReduceB", "Observe" };
            REQUIRE(bind_engine(names, 3, parallel));
            for (int step = 1; step <= 2; step++) {
                REQUIRE(!frame(step));
                for (int i = 0; i < 4; i++) {
                    ecs_entity owner = f.owners[i];
                    REQUIRE(read_field(owner, f.totals, TOTAL).d == 1);
                    REQUIRE(read_field(owner, f.totals, SINGLE).f == 1);
                    REQUIRE(read_field(owner, f.totals, COUNT).i == INT_MIN + 2 * step - 1);
                    REQUIRE(read_field(owner, f.totals, BOUNDED).d == 9);
                    REQUIRE(read_field(owner, f.totals, LIMITED).i == 9);
                    REQUIRE(read_field(owner, f.totals, MINIMUM).d == -6);
                    REQUIRE(read_field(owner, f.totals, MAXIMUM).d == 6);
                    REQUIRE(read_field(owner, f.seen, SEEN_TOTAL).d == 1);
                    REQUIRE(read_field(owner, f.seen, SEEN_COUNT).i == INT_MIN + 2 * step - 1);
                    REQUIRE(read_field(owner, f.seen, SEEN_HITS).i == step);
                    REQUIRE(
                        ecs_component_version(f.world, owner, f.totals->id) ==
                        versions[i] + 20 * step
                    );
                }
            }
            const schedule *s = engine_system_schedule(f.engine);
            REQUIRE(!schedule_dependency(s, "ReduceA", "ReduceB"));
            REQUIRE(schedule_dependency(s, "ReduceA", "Observe"));
            REQUIRE(schedule_dependency(s, "ReduceB", "Observe"));
            REQUIRE(schedule_parallel_batches(s) == (parallel ? 2 : 0));
            cleanup();
        }
    }
    return true;
}

static ecs_entity contribution(ecs_entity target, ecs_entity alternate, double amount, double candidate)
{
    ecs_entity owner = ecs_world_create_entity(f.world);
    int status = ecs_world_record_add(f.world, owner, f.contribution->id, NULL, 0);
    assert(!status && !ecs_world_apply_commands(f.world));
    (void)status;
    write_field(
        owner,
        f.contribution,
        TARGET,
        (pin_value){ .type = PIN_HANDLE,
                     .type_name = "ecs.entity",
                     .u64 = ecs_entity_to_u64(target) }
    );
    write_field(
        owner,
        f.contribution,
        ALTERNATE,
        (pin_value){ .type = PIN_HANDLE,
                     .type_name = "ecs.entity",
                     .u64 = ecs_entity_to_u64(alternate) }
    );
    write_field(owner, f.contribution, AMOUNT, (pin_value){ .type = PIN_F64, .d = amount });
    write_field(owner, f.contribution, CANDIDATE, (pin_value){ .type = PIN_F64, .d = candidate });
    return owner;
}
TEST_CASE(source_reductions_keep_row_order_and_comparison_select_values)
{
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(create(1, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        REQUIRE(publish(
            "Ordered",
            "Ordered :: system(c:^ReductionContribution){reduce "
            "c.target.ReductionTotals.total += "
            "c.amount;reduce "
            "min(c.target.ReductionTotals.minimum,c.candidate);reduce "
            "max(c.target.ReductionTotals.maximum,c.candidate);}"
        ));
        ecs_entity target = f.owners[0];
        contribution(target, target, 1e16, NAN);
        contribution(target, target, -1e16, 4);
        ecs_entity last = contribution(target, target, 1, -0.0);
        REQUIRE(source_run_system(f.sources, "Ordered", 0, .01) >= 0);
        REQUIRE(read_field(target, f.totals, TOTAL).d == 1);
        REQUIRE(signbit(read_field(target, f.totals, MINIMUM).d));
        REQUIRE(read_field(target, f.totals, MAXIMUM).d == 4);
        /* Equality selects the incoming zero; unordered selects incoming NaN. */
        write_field(last, f.contribution, CANDIDATE, (pin_value){ .type = PIN_F64, .d = NAN });
        REQUIRE(source_run_system(f.sources, "Ordered", 1, .01) >= 0);
        REQUIRE(isnan(read_field(target, f.totals, MINIMUM).d));
        REQUIRE(isnan(read_field(target, f.totals, MAXIMUM).d));
        cleanup();
        REQUIRE(create(1, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        REQUIRE(
            publish("Zero", "Zero :: system(t:^ReductionTotals){reduce min(t.minimum,-0.0);reduce max(t.maximum,-0.0);}")
        );
        REQUIRE(source_run_system(f.sources, "Zero", 0, .01) >= 0);
        REQUIRE(signbit(read_field(f.owners[0], f.totals, MINIMUM).d));
        REQUIRE(signbit(read_field(f.owners[0], f.totals, MAXIMUM).d));
        cleanup();
    }
    return true;
}

TEST_CASE(source_reductions_discard_failed_system_and_later_peers)
{
    for (int backend = 0; backend < 2; backend++) {
        for (int parallel = 0; parallel < 2; parallel++) {
            REQUIRE(create(1, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
            REQUIRE(
                publish("Earlier", "Earlier :: system(c:^ReductionContribution){reduce c.target.ReductionTotals.total += 1;}")
            );
            REQUIRE(publish(
                "Failed",
                "Failed :: system(c:^ReductionContribution){reduce "
                "c.alternate.ReductionTotals.total += "
                "10;}"
            ));
            REQUIRE(
                publish("Later", "Later :: system(c:^ReductionContribution){reduce c.target.ReductionTotals.total += 100;}")
            );
            ecs_entity target = f.owners[0],
                       missing = ecs_world_create_entity(f.world);
            REQUIRE(!ecs_world_apply_commands(f.world));
            contribution(target, target, 0, 0);
            contribution(target, missing, 0, 0);
            uint64_t version = ecs_component_version(f.world, target, f.totals->id);
            const char *names[] = { "Earlier", "Failed", "Later" };
            REQUIRE(bind_engine(names, 3, parallel));
            REQUIRE(frame(1) < 0);
            REQUIRE(strstr(engine_error(f.engine), "Failed"));
            REQUIRE(read_field(target, f.totals, TOTAL).d == 2);
            REQUIRE(ecs_component_version(f.world, target, f.totals->id) == version + 2);
            cleanup();
        }
    }
    return true;
}

TEST_CASE(source_unordered_reductions_regroup_only_when_requested)
{
    for (int backend = 0; backend < 2; backend++) {
        for (int unordered = 0; unordered < 2; unordered++) {
            REQUIRE(create(1, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
            char source[512];
            snprintf(
                source,
                sizeof source,
                "Sum :: system(c:^ReductionContribution){"
                "reduce %sc.target.ReductionTotals.total += c.amount;"
                "reduce %sc.target.ReductionTotals.count += 1;}",
                unordered ? "unordered " : "",
                unordered ? "unordered " : ""
            );
            REQUIRE(publish("Sum", source));
            ecs_entity target = f.owners[0];
            write_field(target, f.totals, TOTAL, (pin_value){ .type = PIN_F64, .d = 1e16 });
            write_field(target, f.totals, COUNT, (pin_value){ .type = PIN_I32, .i = INT_MAX });
            contribution(target, target, -1e16, 0);
            contribution(target, target, 1, 0);
            uint64_t version = ecs_component_version(f.world, target, f.totals->id);
            REQUIRE(source_run_system(f.sources, "Sum", 0, .01) >= 0);
            REQUIRE(read_field(target, f.totals, TOTAL).d == (unordered ? 0 : 1));
            REQUIRE(read_field(target, f.totals, COUNT).i == INT_MIN + 1);
            REQUIRE(
                ecs_component_version(f.world, target, f.totals->id) ==
                version + (unordered ? 2 : 4)
            );
            cleanup();
        }
    }
    return true;
}

typedef struct
{
    int index;
    execution_program *program;
    size_t log_bytes, peak_bytes, retained_bytes, row_bytes;
    uint64_t contributions, records;
    int runs, parallel_runs, finishes;
} source_row;
typedef struct
{
    task_pool *pool;
    const task_system *delegate;
    task_system tasks;
    schedule *schedule;
    source_system_batch *batch;
    task_range *run;
    void *context;
    source_row rows[SYSTEM_MAX];
    const char *names[SYSTEM_MAX];
    int count, prepare_failure;
    atomic_uint workers;
    atomic_int arrived;
    atomic_bool gate_failed;
    bool parallel, gate, row_gate, open;
} source_driver;
static source_driver driver;
static void cleanup_driver()
{
    if (driver.batch) {
        source_systems_finish(driver.batch);
    }
    if (driver.open) {
        task_pool_end(driver.pool);
    }
    if (driver.schedule) {
        schedule_free(driver.schedule);
    }
    if (driver.pool) {
        task_pool_release(driver.pool);
    }
    memset(&driver, 0, sizeof driver);
}
static void observed_range(int start, int end, uint32_t worker, void *context)
{
    source_driver *d = context;
    atomic_fetch_or(&d->workers, 1u << worker);
    if (d->row_gate) {
        atomic_fetch_add(&d->arrived, 1);
        double deadline = now() + 2;
        while (atomic_load(&d->arrived) < 2 && now() < deadline) {
        }
        if (atomic_load(&d->arrived) < 2) {
            atomic_store(&d->gate_failed, true);
        }
    }
    d->run(start, end, worker, d->context);
}
static void *enqueue(task_range *run, int count, int minimum, void *context, void *user)
{
    source_driver *d = user;
    d->run = run;
    d->context = context;
    return d->delegate->enqueue(observed_range, count, minimum, d, d->delegate->user);
}
static void finish_task(void *task, void *user)
{
    source_driver *d = user;
    d->delegate->finish(task, d->delegate->user);
}
static int prepare_row(void *user, schedule_error *error)
{
    source_row *row = user;
    int status = source_systems_prepare_index(
        driver.batch,
        row->index,
        error->message,
        sizeof error->message
    );
    if (status >= 0 && driver.prepare_failure == row->index) {
        snprintf(error->message, sizeof error->message, "injected prepare failure");
        return FLOW_ERR_STATE;
    }
    return status;
}
static bool parallel_row(void *user)
{
    source_row *row = user;
    return source_systems_parallel(driver.batch, row->index);
}
static int run_row(ecs_world *, double, double, void *user, schedule_error *error)
{
    source_row *row = user;
    row->runs++;
    if (driver.gate) {
        /* This one regression gate proves two distinct source systems enter
         * before either executes rows. A broken/serial executor times out and
         * fails cleanly instead of deadlocking the process. Not used in timings. */
        atomic_fetch_add(&driver.arrived, 1);
        double deadline = now() + 2;
        while (atomic_load(&driver.arrived) < 2 && now() < deadline) {
        }
        if (atomic_load(&driver.arrived) < 2) {
            atomic_store(&driver.gate_failed, true);
        }
    }
    return source_systems_run(
        driver.batch,
        row->index,
        error->message,
        sizeof error->message
    );
}
static int finish_row(void *user, bool commit, schedule_error *error)
{
    source_row *row = user;
    row->finishes++;
    execution_memory_usage memory = execution_program_memory_usage(row->program);
    row->log_bytes = memory.reduction_bytes;
    row->peak_bytes = memory.reduction_peak_bytes;
    row->contributions = memory.reduction_contributions;
    row->records = memory.reduction_records;
    int status = source_systems_finish_index(
        driver.batch,
        row->index,
        commit,
        error->message,
        sizeof error->message
    );
    memory = execution_program_memory_usage(row->program);
    row->retained_bytes = memory.reduction_capacity;
    row->row_bytes = memory.system_row_capacity;
    return status;
}
static int run_row_parallel(void *user, const task_system *tasks, schedule_error *error)
{
    source_row *row = user;
    row->runs++;
    row->parallel_runs++;
    return source_systems_run_parallel(
        driver.batch,
        row->index,
        tasks,
        error->message,
        sizeof error->message
    );
}
static bool create_driver(const char *const *names, int count, bool parallel)
{
    REQUIRE(count <= SYSTEM_MAX);
    REQUIRE(!source_select_systems(f.sources, names, count));
    driver.pool = task_pool_acquire_workers(2);
    driver.delegate = task_pool_tasks(driver.pool);
    driver.tasks = (task_system){ driver.delegate->workers, &driver, enqueue, finish_task };
    driver.schedule = schedule_new(f.world, &driver.tasks);
    driver.parallel = parallel;
    driver.count = count;
    driver.prepare_failure = -1;
    atomic_init(&driver.workers, 0);
    atomic_init(&driver.arrived, 0);
    atomic_init(&driver.gate_failed, false);
    schedule_set_parallel(driver.schedule, parallel);
    for (int i = 0; i < count; i++) {
        driver.names[i] = names[i];
        source_row *row = &driver.rows[i];
        row->index = i;
        row->program = source_program(f.sources, names[i]);
        execution_effect_summary e = execution_module_effects(
            execution_program_module(row->program)
        );
        schedule_desc desc = { .name = names[i],
                               .phase = SYSTEM_PRE_PHYSICS,
                               .reads = e.reads,
                               .read_count = e.read_count,
                               .writes = e.writes,
                               .write_count = e.write_count,
                               .reduces = e.reduces,
                               .reduce_count = e.reduce_count,
                               .declared = e.declared,
                               .structural = e.structural,
                               .host_sink = e.host_sink,
                               .world_read = e.world_read,
                               .deterministic = e.deterministic,
                               .parallel_safe = e.parallel_safe,
                               .identity = row->program,
                               .run = run_row,
                               .prepare = prepare_row,
                               .parallel_ready = parallel_row,
                               .finish = finish_row,
                               .user = row };
        REQUIRE(!schedule_register(driver.schedule, &desc));
    }
    REQUIRE(schedule_build(driver.schedule));
    return true;
}
static int run_driver(int pass)
{
    driver.batch = source_systems_prepare(f.sources, driver.names, driver.count, pass, .01);
    if (!driver.batch) {
        fprintf(stderr, "reduction prepare: %s\n", source_error(f.sources));
        return -1;
    }
    task_pool_begin(driver.pool, driver.parallel);
    driver.open = true;
    int status = schedule_run(driver.schedule, SYSTEM_PRE_PHYSICS, pass, .01);
    task_pool_end(driver.pool);
    driver.open = false;
    int finished = source_systems_finish(driver.batch);
    driver.batch = NULL;
    if (status < 0) {
        fprintf(
            stderr,
            "reduction schedule: %s\n",
            schedule_error_message(driver.schedule)
        );
    }
    return status < 0 ? status : finished;
}
static bool simple_sources(int mode)
{
    const char *prefix = mode == 2 ? ""
                       : mode == 1 ? "reduce unordered "
                                   : "reduce ";
    char source[512];
    for (int i = 0; i < 2; i++) {
        snprintf(
            source,
            sizeof source,
            "%s :: system(c:^ReductionContribution){"
            "%sc.target.ReductionTotals.total += %s;"
            "%sc.target.ReductionTotals.count += %d;}",
            i ? "ReduceB" : "ReduceA",
            prefix,
            i ? "0.5" : "0.25",
            prefix,
            i + 1
        );
        REQUIRE(publish(i ? "ReduceB" : "ReduceA", source));
    }
    return true;
}
TEST_CASE(source_reductions_run_on_distinct_worker_threads)
{
    REQUIRE(create(1, BEHAVIOR_SLJIT));
    REQUIRE(simple_sources(0));
    for (int i = 0; i < 4; i++) {
        contribution(f.owners[0], f.owners[0], 0, 0);
    }
    const char *names[] = { "ReduceA", "ReduceB" };
    REQUIRE(create_driver(names, 2, true));
    driver.gate = driver.delegate->workers > 1;
    REQUIRE(!run_driver(0));
    REQUIRE(read_field(f.owners[0], f.totals, TOTAL).d == 3);
    REQUIRE(read_field(f.owners[0], f.totals, COUNT).i == 12);
    REQUIRE(driver.rows[0].runs == 1 && driver.rows[1].runs == 1);
    REQUIRE(driver.rows[0].finishes == 1 && driver.rows[1].finishes == 1);
    REQUIRE(schedule_parallel_batches(driver.schedule) == 1);
    for (int i = 0; i < 2; i++) {
        execution_memory_usage memory = execution_program_memory_usage(
            driver.rows[i].program
        );
        REQUIRE(memory.instance_count == 0 && memory.instance_bytes == 0);
        REQUIRE(memory.query_rows == 4 && memory.system_row_bytes == 0);
        REQUIRE(memory.system_row_capacity > 0 && memory.system_row_capacity <= 64 * 1024);
    }
    if (driver.delegate->workers > 1) {
        unsigned workers = atomic_load(&driver.workers);
        REQUIRE(!atomic_load(&driver.gate_failed));
        REQUIRE(atomic_load(&driver.arrived) == 2);
        REQUIRE(workers && (workers & (workers - 1)));
        REQUIRE(task_pool_started(driver.pool));
    } else {
        printf("COVERAGE source-reductions workers=1; real worker overlap unavailable\n");
    }
    return true;
}
TEST_CASE(source_reductions_discard_prepared_cohort_after_prepare_failure)
{
    REQUIRE(create(1, BEHAVIOR_SLJIT));
    REQUIRE(simple_sources(0));
    contribution(f.owners[0], f.owners[0], 0, 0);
    const char *names[] = { "ReduceA", "ReduceB" };
    REQUIRE(create_driver(names, 2, true));
    driver.prepare_failure = 1;
    uint64_t version = ecs_component_version(f.world, f.owners[0], f.totals->id);
    REQUIRE(run_driver(0) < 0);
    REQUIRE(driver.rows[0].runs == 0 && driver.rows[1].runs == 0);
    REQUIRE(driver.rows[0].finishes == 1 && driver.rows[1].finishes == 1);
    REQUIRE(read_field(f.owners[0], f.totals, TOTAL).d == 0);
    REQUIRE(ecs_component_version(f.world, f.owners[0], f.totals->id) == version);
    driver.prepare_failure = -1;
    /* Scheduler failures remain latched until the owning descriptor is
     * registered again. Retry the same programs after clearing that boundary. */
    schedule_desc retry = *schedule_descriptor_at(driver.schedule, 1);
    REQUIRE(!strcmp(retry.name, "ReduceB"));
    REQUIRE(!schedule_register(driver.schedule, &retry));
    REQUIRE(!run_driver(1));
    REQUIRE(driver.rows[0].runs == 1 && driver.rows[1].runs == 1);
    REQUIRE(driver.rows[0].finishes == 2 && driver.rows[1].finishes == 2);
    REQUIRE(read_field(f.owners[0], f.totals, TOTAL).d == .75);
    REQUIRE(read_field(f.owners[0], f.totals, COUNT).i == 3);
    REQUIRE(ecs_component_version(f.world, f.owners[0], f.totals->id) == version + 4);
    return true;
}

TEST_CASE(source_reductions_preflight_all_records_before_publication)
{
    for (int backend = 0; backend < 2; backend++) {
        for (int reused = 0; reused < 2; reused++) {
            REQUIRE(create(2, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
            REQUIRE(
                publish("Preflight", "Preflight :: system(t:^ReductionTotals){reduce t.total += 1;}")
            );
            execution_program *program = source_program(f.sources, "Preflight");
            REQUIRE(execution_program_prepare_system(program, 0, .01, 10000) == FLOW_OK);
            f.pending_program = program;
            REQUIRE(execution_program_run_system(program) == FLOW_OK);
            REQUIRE(execution_program_memory_usage(program).reduction_records == 2);
            uint64_t
                version = ecs_component_version(f.world, f.owners[0], f.totals->id);
            /* Deliberately invalidate a later record after collection. Callers
             * ordinarily keep membership fixed; finish must still reject the
             * whole log before touching its earlier, valid destination. */
            ecs_entity replacement = { 0 };
            uint64_t replacement_version = 0;
            if (reused) {
                /* Grow the world's column array after the repeated field lookup
                 * warmed metadata, then reuse the later destination's slot. */
                int columns = ecs_world_component_count(f.world);
                REQUIRE(columns <= 512);
                for (int i = 0; i <= columns; i++) {
                    char name[64];
                    snprintf(name, sizeof name, "PreflightExtra%d", i);
                    ecs_component_desc extra = *f.seen;
                    extra.id = 0;
                    extra.key = extra.name = name;
                    REQUIRE(!ecs_world_register_component(f.world, &extra, NULL));
                }
                REQUIRE(!ecs_world_record_destroy(f.world, f.owners[1], 0));
                REQUIRE(!ecs_world_apply_commands(f.world));
                replacement = ecs_world_create_entity(f.world);
                REQUIRE(replacement.slot == f.owners[1].slot);
                REQUIRE(replacement.generation != f.owners[1].generation);
                REQUIRE(!ecs_world_record_add(f.world, replacement, f.totals->id, NULL, 0));
            } else {
                REQUIRE(!ecs_world_record_remove(f.world, f.owners[1], f.totals->id, 0));
            }
            REQUIRE(!ecs_world_apply_commands(f.world));
            if (reused) {
                write_field(replacement, f.totals, TOTAL, (pin_value){ .type = PIN_F64, .d = 17 });
                replacement_version = ecs_component_version(
                    f.world,
                    replacement,
                    f.totals->id
                );
            }
            int status = execution_program_finish_system(program);
            f.pending_program = NULL;
            REQUIRE(status < 0);
            REQUIRE(read_field(f.owners[0], f.totals, TOTAL).d == 0);
            REQUIRE(ecs_component_version(f.world, f.owners[0], f.totals->id) == version);
            REQUIRE(execution_program_memory_usage(program).reduction_bytes == 0);
            if (reused) {
                REQUIRE(read_field(replacement, f.totals, TOTAL).d == 17);
                REQUIRE(
                    ecs_component_version(f.world, replacement, f.totals->id) == replacement_version
                );
            }
            cleanup();
        }
    }
    return true;
}

static bool benchmark(int mode, bool parallel)
{
    REQUIRE(create(1, BEHAVIOR_SLJIT));
    REQUIRE(simple_sources(mode));
    for (int i = 0; i < OWNER_MAX; i++) {
        contribution(f.owners[0], f.owners[0], 0, 0);
    }
    const char *names[] = { "ReduceA", "ReduceB" };
    REQUIRE(create_driver(names, 2, parallel));
    REQUIRE(!run_driver(0));
    REQUIRE(read_field(f.owners[0], f.totals, TOTAL).d == OWNER_MAX * .75);
    REQUIRE(read_field(f.owners[0], f.totals, COUNT).i == OWNER_MAX * 3);
    for (int trial = 0; trial < BENCH_TRIALS; trial++) {
        write_field(f.owners[0], f.totals, TOTAL, (pin_value){ .type = PIN_F64, .d = 0 });
        write_field(f.owners[0], f.totals, COUNT, (pin_value){ .type = PIN_I32, .i = 0 });
        uint64_t version = ecs_component_version(f.world, f.owners[0], f.totals->id);
        int batches = schedule_parallel_batches(driver.schedule);
        xalloc_stats before, after;
        xalloc_stats_get(&before);
        double start = now();
        int status = 0;
        for (int pass = 0; pass < BENCH_PASSES && status >= 0; pass++) {
            status = run_driver(1 + trial * BENCH_PASSES + pass);
        }
        double elapsed = now() - start;
        xalloc_stats_get(&after);
        REQUIRE(status >= 0);
        REQUIRE(read_field(f.owners[0], f.totals, TOTAL).d == OWNER_MAX * .75 * BENCH_PASSES);
        REQUIRE(read_field(f.owners[0], f.totals, COUNT).i == OWNER_MAX * 3 * BENCH_PASSES);
        uint64_t writes = ecs_component_version(f.world, f.owners[0], f.totals->id) -
                          version;
        REQUIRE(writes == (uint64_t)4 * BENCH_PASSES * (mode == 1 ? 1 : OWNER_MAX));
        REQUIRE(
            schedule_parallel_batches(driver.schedule) - batches ==
            (parallel ? BENCH_PASSES : 0)
        );
        size_t logged = driver.rows[0].log_bytes + driver.rows[1].log_bytes;
        size_t peak = driver.rows[0].peak_bytes + driver.rows[1].peak_bytes;
        size_t retained = driver.rows[0].retained_bytes + driver.rows[1].retained_bytes;
        size_t rows = driver.rows[0].row_bytes + driver.rows[1].row_bytes;
        uint64_t contributions = driver.rows[0].contributions +
                                 driver.rows[1].contributions;
        uint64_t records = driver.rows[0].records + driver.rows[1].records;
        REQUIRE(mode == 2 ? !logged && !peak && !retained : logged > 0 && peak >= logged);
        REQUIRE(contributions == (mode == 2 ? 0 : 4 * OWNER_MAX));
        REQUIRE(records == (mode == 2 ? 0 : mode == 1 ? 4 : 4 * OWNER_MAX));
        int instances = 0;
        size_t instance_bytes = 0, activation_cache_bytes = 0;
        for (int i = 0; i < 2; i++) {
            execution_memory_usage memory = execution_program_memory_usage(
                driver.rows[i].program
            );
            instances += memory.instance_count;
            instance_bytes += memory.instance_bytes;
            activation_cache_bytes += memory.activation_cache_bytes;
            if (mode != 2) {
                REQUIRE(memory.instance_count == 0 && memory.instance_bytes == 0);
                REQUIRE(memory.query_rows == OWNER_MAX);
                REQUIRE(memory.system_row_bytes == 0);
                REQUIRE(memory.system_row_capacity > 0 && memory.system_row_capacity <= 64 * 1024);
                REQUIRE(memory.reduction_capacity <= 64 * 1024);
                REQUIRE(memory.activation_cache_bytes <= 64 * 1024);
            }
        }
        unsigned workers = atomic_load(&driver.workers);
        unsigned allowed_workers = driver.delegate->workers == TASK_WORKER_MAX
                                     ? UINT32_MAX
                                     : (1u << driver.delegate->workers) - 1;
        REQUIRE(!(workers & ~allowed_workers));
        printf(
            "BENCH source-reductions mode=%s execution=%s rows=%d systems=2 "
            "passes=%d trial=%d ns/pass=%.2f published_writes=%llu "
            "contributions=%llu records=%llu log_bytes=%zu "
            "reduction_peak_bytes=%zu "
            "reduction_retained_bytes=%zu row_retained_bytes=%zu "
            "instances=%d instance_bytes=%zu activation_cache_bytes=%zu "
            "xalloc_calls=%llu requested_bytes=%llu "
            "parallel_batches=%d worker_budget=%d worker_mask=%u total=%.0f "
            "count=%d\n",
            mode == 2   ? "direct"
            : mode == 1 ? "unordered"
                        : "ordered",
            parallel ? "parallel" : "serial",
            OWNER_MAX,
            BENCH_PASSES,
            trial,
            elapsed * 1e9 / BENCH_PASSES,
            (unsigned long long)writes,
            (unsigned long long)contributions,
            (unsigned long long)records,
            logged,
            peak,
            retained,
            rows,
            instances,
            instance_bytes,
            activation_cache_bytes,
            (unsigned long long)(after.calls - before.calls),
            (unsigned long long)(after.requested_bytes - before.requested_bytes),
            schedule_parallel_batches(driver.schedule) - batches,
            driver.delegate->workers,
            workers,
            read_field(f.owners[0], f.totals, TOTAL).d,
            read_field(f.owners[0], f.totals, COUNT).i
        );
    }
    cleanup();
    return true;
}
TEST_CASE(source_reductions_benchmark_same_authored_workload)
{
    REQUIRE(benchmark(0, false));
    REQUIRE(benchmark(0, true));
    REQUIRE(benchmark(1, false));
    REQUIRE(benchmark(1, true));
    REQUIRE(benchmark(2, false));
    return true;
}

static bool paused(void *context)
{
    return source_paused(context);
}
static int count_stage(ecs_world *, double, double, void *user, schedule_error *)
{
    (*(int *)user)++;
    return FLOW_OK;
}
static bool statement_breakpoint(execution_program *program, int line, uint64_t *function, uint64_t *occurrence)
{
    const execution_module *module = execution_program_module(program);
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info info = execution_module_function_at(module, i);
        if (info.kind != EXECUTION_FUNCTION_SYSTEM) {
            continue;
        }
        for (int j = 0; j < behavior_function_source_point_count(info.body); j++) {
            behavior_source_point point = behavior_function_source_point_at(info.body, j);
            if (point.kind == BEHAVIOR_SOURCE_STATEMENT && point.line == line &&
                !point.completion) {
                *function = info.id;
                *occurrence = point.occurrence;
                return true;
            }
        }
    }
    return false;
}
TEST_CASE(source_reductions_cancel_later_paused_peer_after_earlier_failure)
{
    REQUIRE(create(1, BEHAVIOR_SLJIT));
    REQUIRE(publish(
        "Failed",
        "Failed :: system(c:^ReductionContribution){reduce "
        "c.alternate.ReductionTotals.total += "
        "10;}"
    ));
    REQUIRE(publish(
        "Later",
        "Later :: system(c:^ReductionContribution){\n"
        "reduce c.target.ReductionTotals.total += 1;\n"
        "reduce c.target.ReductionTotals.total += 2;\n"
        "}"
    ));
    ecs_entity missing = ecs_world_create_entity(f.world);
    REQUIRE(!ecs_world_apply_commands(f.world));
    contribution(f.owners[0], missing, 0, 0);
    execution_program *later = source_program(f.sources, "Later");
    const execution_module *module = execution_program_module(later);
    f.inspector = execution_program_inspector(later);
    REQUIRE(f.inspector);
    uint64_t function = 0, occurrence = 0;
    REQUIRE(statement_breakpoint(later, 3, &function, &occurrence));
    REQUIRE(
        execution_inspector_breakpoint(f.inspector, execution_module_id(module), function, occurrence, true)
    );
    const char *names[] = { "Failed", "Later" };
    REQUIRE(create_driver(names, 2, false));
    REQUIRE(!schedule_dependency(driver.schedule, "Failed", "Later"));
    uint64_t version = ecs_component_version(f.world, f.owners[0], f.totals->id);
    REQUIRE(run_driver(0) < 0);
    REQUIRE(driver.rows[0].runs == 1 && driver.rows[1].runs == 1);
    REQUIRE(driver.rows[0].finishes == 1 && driver.rows[1].finishes == 1);
    /* The later peer reached its breakpoint with one private contribution;
     * the earlier failure denies publication and must cancel that suspension. */
    REQUIRE(driver.rows[1].contributions == 1 && driver.rows[1].log_bytes > 0);
    REQUIRE(!execution_program_paused(later) && !source_paused(f.sources));
    REQUIRE(execution_program_memory_usage(later).reduction_bytes == 0);
    REQUIRE(read_field(f.owners[0], f.totals, TOTAL).d == 0);
    REQUIRE(ecs_component_version(f.world, f.owners[0], f.totals->id) == version);
    return true;
}
TEST_CASE(source_reductions_resume_post_physics_without_replaying_prior_stages)
{
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(create(2, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        REQUIRE(publish(
            "Paused",
            "Paused :: system(t:^ReductionTotals){\n"
            "reduce t.total += 1;\n"
            "reduce t.total += 2;\n"
            "}"
        ));
        REQUIRE(
            publish("Consumer", "Consumer :: system(t:^ReductionTotals,s:^ReductionSeen){s.total=t.total;s.hits+=1;}")
        );
        const char *names[] = { "Paused", "Consumer" };
        for (int i = 0; i < 2; i++) {
            ecs_entity binding = ecs_world_create_entity(f.world);
            EngineSystem value = { names[i], SYSTEM_POST_PHYSICS, i };
            REQUIRE(
                !ecs_world_record_add(f.world, binding, COMPONENT_EngineSystem.id, &value, 0)
            );
        }
        ecs_entity mover = ecs_world_create_entity(f.world);
        REQUIRE(
            !ecs_world_record_add(f.world, mover, COMPONENT_Transform.id, &(Transform){ 0 }, 0)
        );
        REQUIRE(
            !ecs_world_record_add(f.world, mover, COMPONENT_Velocity.id, &(Velocity){ 60, 0 }, 0)
        );
        REQUIRE(
            !ecs_world_record_add(f.world, mover, COMPONENT_Body.id, &(Body){ .type = BODY_KINEMATIC }, 0)
        );
        REQUIRE(!ecs_world_record_add(
            f.world,
            mover,
            COMPONENT_Collider.id,
            &(Collider){ .radius = 1, .category = 1 },
            0
        ));
        REQUIRE(!ecs_world_apply_commands(f.world));
        f.entities = entities_new(f.world, NULL);
        REQUIRE(f.entities);
        f.physics = physics_new(f.world, f.entities);
        REQUIRE(f.physics);
        engine_modules modules = { .sources = f.sources,
                                   .entities = f.entities,
                                   .physics = f.physics,
                                   .paused = paused,
                                   .context = f.sources };
        f.engine = engine_new(f.world, &modules);
        REQUIRE(f.engine);
        int before_physics = 0, before_reducer = 0;
        REQUIRE(!engine_register_system(
            f.engine,
            &(schedule_desc){ .name = "PrePhysics counter",
                              .phase = SYSTEM_PRE_PHYSICS,
                              .run = count_stage,
                              .user = &before_physics }
        ));
        REQUIRE(!engine_register_system(
            f.engine,
            &(schedule_desc){ .name = "PostPhysics predecessor",
                              .phase = SYSTEM_POST_PHYSICS,
                              .order = -1,
                              .run = count_stage,
                              .user = &before_reducer }
        ));
        execution_program *program = source_program(f.sources, "Paused");
        const execution_module *module = execution_program_module(program);
        f.inspector = execution_program_inspector(program);
        REQUIRE(f.inspector);
        uint64_t function = 0, occurrence = 0;
        REQUIRE(statement_breakpoint(program, 3, &function, &occurrence));
        REQUIRE(
            execution_inspector_breakpoint(f.inspector, execution_module_id(module), function, occurrence, true)
        );
        REQUIRE(!engine_frame(f.engine, 0));
        REQUIRE(!frame(1));
        REQUIRE(execution_program_paused(program));
        REQUIRE(execution_inspector_cursor(f.inspector).status == FLOW_PAUSED_RESULT);
        REQUIRE(execution_program_memory_usage(program).reduction_contributions == 1);
        REQUIRE(execution_program_memory_usage(program).instance_count == 2);
        REQUIRE(before_physics == 1 && before_reducer == 1);
        REQUIRE(fabsf(Transform_get_const(f.world, mover)->x - 1) < .0001f);
        REQUIRE(engine_last_timing(f.engine)->simulation_steps == 1);
        for (int i = 0; i < 2; i++) {
            REQUIRE(read_field(f.owners[i], f.totals, TOTAL).d == 0);
            REQUIRE(read_field(f.owners[i], f.seen, SEEN_HITS).i == 0);
        }
        REQUIRE(!frame(1));
        REQUIRE(before_physics == 1 && before_reducer == 1);
        REQUIRE(
            execution_inspector_breakpoint(f.inspector, execution_module_id(module), function, occurrence, false)
        );
        REQUIRE(execution_inspector_control(f.inspector, EXECUTION_DEBUG_RESUME) == FLOW_OK);
        REQUIRE(!frame(1));
        REQUIRE(!execution_program_paused(program));
        REQUIRE(before_physics == 1 && before_reducer == 1);
        REQUIRE(fabsf(Transform_get_const(f.world, mover)->x - 1) < .0001f);
        REQUIRE(engine_last_timing(f.engine)->simulation_steps == 1);
        for (int i = 0; i < 2; i++) {
            REQUIRE(read_field(f.owners[i], f.totals, TOTAL).d == 3);
            REQUIRE(read_field(f.owners[i], f.seen, SEEN_TOTAL).d == 3);
            REQUIRE(read_field(f.owners[i], f.seen, SEEN_HITS).i == 1);
        }
        REQUIRE(!frame(1));
        REQUIRE(before_physics == 1 && before_reducer == 1);
        /* Reset a second paused step while it owns a contribution and a
         * scheduler continuation. Replacing all query owners also makes any
         * stale destination or prepared row visible on the next fresh tick. */
        REQUIRE(
            execution_inspector_breakpoint(f.inspector, execution_module_id(module), function, occurrence, true)
        );
        REQUIRE(!frame(2));
        REQUIRE(execution_program_paused(program));
        REQUIRE(execution_program_memory_usage(program).reduction_contributions == 1);
        REQUIRE(before_physics == 2 && before_reducer == 2);
        REQUIRE(fabsf(Transform_get_const(f.world, mover)->x - 2) < .0001f);
        for (int i = 0; i < 2; i++) {
            REQUIRE(read_field(f.owners[i], f.totals, TOTAL).d == 3);
            REQUIRE(read_field(f.owners[i], f.seen, SEEN_HITS).i == 1);
        }
        engine_reset_scene(f.engine);
        REQUIRE(!execution_program_paused(program) && !source_paused(f.sources));
        REQUIRE(execution_program_memory_usage(program).reduction_bytes == 0);
        REQUIRE(
            execution_inspector_breakpoint(f.inspector, execution_module_id(module), function, occurrence, false)
        );
        execution_inspector_free(f.inspector);
        f.inspector = NULL;
        for (int i = 0; i < 2; i++) {
            REQUIRE(read_field(f.owners[i], f.totals, TOTAL).d == 3);
            REQUIRE(!ecs_world_record_destroy(f.world, f.owners[i], 0));
        }
        REQUIRE(!ecs_world_apply_commands(f.world));
        for (int i = 0; i < 2; i++) {
            f.owners[i] = ecs_world_create_entity(f.world);
            REQUIRE(!ecs_world_record_add(f.world, f.owners[i], f.totals->id, NULL, 0));
            REQUIRE(!ecs_world_record_add(f.world, f.owners[i], f.seen->id, NULL, 0));
        }
        REQUIRE(!ecs_world_apply_commands(f.world));
        for (int i = 0; i < 2; i++) {
            write_field(f.owners[i], f.totals, TOTAL, (pin_value){ .type = PIN_F64, .d = 10 });
        }
        /* The first unpaused frame rebases the debug clock without advancing. */
        REQUIRE(!frame(2));
        REQUIRE(before_physics == 2 && before_reducer == 2);
        REQUIRE(!frame(3));
        REQUIRE(before_physics == 3 && before_reducer == 3);
        REQUIRE(fabsf(Transform_get_const(f.world, mover)->x - 3) < .0001f);
        REQUIRE(engine_last_timing(f.engine)->simulation_steps == 3);
        for (int i = 0; i < 2; i++) {
            REQUIRE(read_field(f.owners[i], f.totals, TOTAL).d == 13);
            REQUIRE(read_field(f.owners[i], f.seen, SEEN_TOTAL).d == 13);
            REQUIRE(read_field(f.owners[i], f.seen, SEEN_HITS).i == 1);
        }
        REQUIRE(!frame(3));
        REQUIRE(before_physics == 3 && before_reducer == 3);
        cleanup();
    }
    return true;
}

typedef struct
{
    ecs_entity contribution;
    int calls;
} phase_producer;
static int produce_amount(ecs_world *, double, double, void *user, schedule_error *)
{
    phase_producer *producer = user;
    producer->calls++;
    write_field(
        producer->contribution,
        f.contribution,
        AMOUNT,
        (pin_value){ .type = PIN_F64, .d = producer->calls }
    );
    return FLOW_OK;
}
TEST_CASE(source_reductions_discard_paused_log_when_descriptor_edit_restarts_phase)
{
    REQUIRE(create(1, BEHAVIOR_SLJIT));
    REQUIRE(publish(
        "B",
        "B :: system(c:^ReductionContribution){\n"
        "reduce c.target.ReductionTotals.total += c.amount;\n"
        "reduce c.target.ReductionTotals.total += c.amount;\n"
        "}"
    ));
    phase_producer producer = {
        .contribution = contribution(f.owners[0], f.owners[0], 0, 0)
    };
    ecs_entity binding = ecs_world_create_entity(f.world);
    EngineSystem binding_value = { "B", SYSTEM_POST_PHYSICS, 0 };
    REQUIRE(!ecs_world_record_add(f.world, binding, COMPONENT_EngineSystem.id, &binding_value, 0));
    REQUIRE(!ecs_world_apply_commands(f.world));
    engine_modules modules = { .sources = f.sources, .paused = paused, .context = f.sources };
    f.engine = engine_new(f.world, &modules);
    REQUIRE(f.engine);
    int pre = 0, after = 0;
    uint64_t totals = f.totals->id, seen = f.seen->id;
    REQUIRE(!engine_register_system(
        f.engine,
        &(schedule_desc){ .name = "Earlier phase",
                          .phase = SYSTEM_PRE_PHYSICS,
                          .run = count_stage,
                          .user = &pre }
    ));
    REQUIRE(!engine_register_system(
        f.engine,
        &(schedule_desc){ .name = "A",
                          .phase = SYSTEM_POST_PHYSICS,
                          .order = -1,
                          .run = produce_amount,
                          .user = &producer }
    ));
    schedule_desc c = { .name = "C",
                        .phase = SYSTEM_POST_PHYSICS,
                        .order = 1,
                        .declared = true,
                        .deterministic = true,
                        .parallel_safe = true,
                        .reads = &totals,
                        .read_count = 1,
                        .run = count_stage,
                        .user = &after };
    REQUIRE(!engine_register_system(f.engine, &c));
    execution_program *program = source_program(f.sources, "B");
    const execution_module *module = execution_program_module(program);
    f.inspector = execution_program_inspector(program);
    REQUIRE(f.inspector);
    uint64_t function = 0, occurrence = 0;
    REQUIRE(statement_breakpoint(program, 3, &function, &occurrence));
    REQUIRE(
        execution_inspector_breakpoint(f.inspector, execution_module_id(module), function, occurrence, true)
    );
    REQUIRE(!engine_frame(f.engine, 0));
    REQUIRE(!frame(1));
    REQUIRE(execution_program_paused(program));
    REQUIRE(execution_program_memory_usage(program).reduction_contributions == 1);
    REQUIRE(pre == 1 && producer.calls == 1 && after == 0);
    REQUIRE(read_field(f.owners[0], f.totals, TOTAL).d == 0);
    /* A real declaration change restarts the current phase. Its committed A
     * runs again, while B must discard the log made from A's previous value. */
    c.writes = &seen;
    c.write_count = 1;
    REQUIRE(!engine_register_system(f.engine, &c));
    REQUIRE(
        execution_inspector_breakpoint(f.inspector, execution_module_id(module), function, occurrence, false)
    );
    REQUIRE(execution_inspector_control(f.inspector, EXECUTION_DEBUG_RESUME) == FLOW_OK);
    REQUIRE(read_field(f.owners[0], f.totals, TOTAL).d == 0);
    REQUIRE(!frame(1));
    REQUIRE(!execution_program_paused(program));
    REQUIRE(pre == 1 && producer.calls == 2 && after == 1);
    REQUIRE(read_field(f.owners[0], f.totals, TOTAL).d == 4);
    REQUIRE(engine_last_timing(f.engine)->simulation_steps == 1);
    REQUIRE(!frame(1));
    REQUIRE(pre == 1 && producer.calls == 2 && after == 1);
    return true;
}

enum
{
    ROW_BENCH_PASSES = 4,
    ROW_BENCH_LARGE = 1024,
    ROW_TEST_COUNT = 256
};

static void create_row_executor()
{
    driver.pool = task_pool_acquire_workers(2);
    driver.delegate = task_pool_tasks(driver.pool);
    driver.tasks = (task_system){ driver.delegate->workers, &driver, enqueue, finish_task };
    atomic_init(&driver.workers, 0);
    atomic_init(&driver.arrived, 0);
    atomic_init(&driver.gate_failed, false);
}

/* This unit-level runner deliberately measures one program: no system-level
 * scheduler job can account for the row executor's work counters. */
static int run_rows(execution_program *program, int pass, bool parallel)
{
    task_pool_begin(driver.pool, parallel);
    driver.open = true;
    int status = execution_program_prepare_system(program, pass, .01, 100000);
    if (status < 0) {
        task_pool_end(driver.pool);
        driver.open = false;
        return status;
    }
    f.pending_program = program;
    status = parallel ? execution_program_run_system_parallel(program, &driver.tasks)
                      : execution_program_run_system(program);
    if (status < 0) {
        execution_program_discard_reductions(program);
    }
    int finished = execution_program_finish_system(program);
    f.pending_program = NULL;
    task_pool_end(driver.pool);
    driver.open = false;
    return status < 0 ? status : finished;
}

static int32_t mix_reference(int seed, int iterations)
{
    uint32_t bits = (uint32_t)seed;
    for (int i = 0; i < iterations; i++) {
        bits = bits * UINT32_C(1664525) + UINT32_C(1013904223);
    }
    int32_t value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static bool row_benchmark(int rows, int iterations, bool parallel, bool unordered)
{
    REQUIRE(create(1, BEHAVIOR_SLJIT));
    char source[1024];
    snprintf(
        source,
        sizeof source,
        "Work :: system(c:^ReductionContribution){"
        "Mix :: (seed:int)->int { value:=seed; i:int=0;"
        "while i < %d {value=value*1664525+1013904223; i+=1;} return value;}"
        "if true {value:=Mix(int(c.amount));"
        "reduce %sc.target.ReductionTotals.count += value;"
        "reduce %sc.target.ReductionTotals.total += f64(value);}}",
        iterations,
        unordered ? "unordered " : "",
        unordered ? "unordered " : ""
    );
    REQUIRE(publish("Work", source));
    execution_program *program = source_program(f.sources, "Work");
    ecs_entity target = f.owners[0];
    uint32_t expected_bits = 0;
    double expected_total = 0;
    for (int i = 0; i < rows; i++) {
        int seed = i % 16 + 1;
        contribution(target, target, seed, 0);
        int32_t value = mix_reference(seed, iterations);
        expected_bits += (uint32_t)value;
        expected_total += value;
    }
    create_row_executor();
    REQUIRE(run_rows(program, 0, parallel) == FLOW_OK);
    REQUIRE(read_field(target, f.totals, TOTAL).d == expected_total);
    REQUIRE((uint32_t)read_field(target, f.totals, COUNT).i == expected_bits);
    bool eligible = parallel && rows >= 128 && driver.delegate->workers > 1;
    for (int trial = 0; trial < BENCH_TRIALS; trial++) {
        write_field(target, f.totals, TOTAL, (pin_value){ .type = PIN_F64, .d = 0 });
        write_field(target, f.totals, COUNT, (pin_value){ .type = PIN_I32, .i = 0 });
        uint64_t version = ecs_component_version(f.world, target, f.totals->id);
        atomic_store(&driver.workers, 0);
        xalloc_stats before, after;
        xalloc_stats_get(&before);
        double start = now();
        int status = FLOW_OK;
        for (int pass = 0; pass < ROW_BENCH_PASSES && status >= 0; pass++) {
            status = run_rows(program, 1 + trial * ROW_BENCH_PASSES + pass, parallel);
        }
        double elapsed = now() - start;
        xalloc_stats_get(&after);
        REQUIRE(status == FLOW_OK);
        execution_memory_usage memory = execution_program_memory_usage(program);
        unsigned worker_mask = atomic_load(&driver.workers);
        REQUIRE(read_field(target, f.totals, TOTAL).d == expected_total * ROW_BENCH_PASSES);
        REQUIRE((uint32_t)read_field(target, f.totals, COUNT).i == expected_bits * ROW_BENCH_PASSES);
        uint64_t writes = ecs_component_version(f.world, target, f.totals->id) - version;
        int records = 2 * (unordered ? 1 : rows);
        REQUIRE(writes == (uint64_t)records * ROW_BENCH_PASSES);
        REQUIRE(memory.reduction_contributions == (uint64_t)2 * rows);
        REQUIRE(memory.reduction_records == (uint64_t)records);
        REQUIRE(memory.instance_count == 0 && memory.instance_bytes == 0);
        REQUIRE(memory.system_row_bytes == 0 && memory.reduction_bytes == 0);
        REQUIRE(memory.system_row_capacity <= 64 * 1024);
        REQUIRE(memory.reduction_capacity <= 64 * 1024);
        REQUIRE(memory.activation_cache_bytes <= 64 * 1024);
        REQUIRE(memory.row_worker_capacity <= 1024 * 1024);
        REQUIRE(memory.query_rows == (uint64_t)rows);
        REQUIRE(eligible ? memory.row_parallel_jobs > 1 : memory.row_parallel_jobs == 0);
        REQUIRE(memory.row_parallel_jobs <= driver.delegate->workers);
        REQUIRE(memory.row_parallel_workers <= memory.row_parallel_jobs);
        REQUIRE(eligible ? worker_mask != 0 : worker_mask == 0);
        unsigned allowed = driver.delegate->workers == TASK_WORKER_MAX
                             ? UINT32_MAX
                             : (1u << driver.delegate->workers) - 1;
        REQUIRE(!(worker_mask & ~allowed));
        printf(
            "BENCH source-execution rows=%d iterations=%d execution=%s mode=%s "
            "passes=%d trial=%d ns/pass=%.2f published_writes=%llu "
            "contributions=%llu records=%llu row_jobs=%d row_workers=%d "
            "worker_budget=%d worker_mask=%u xalloc_calls=%llu "
            "requested_bytes=%llu "
            "activation_cache_bytes=%zu row_retained_bytes=%zu "
            "reduction_retained_bytes=%zu worker_retained_bytes=%zu "
            "total=%.0f count=%d\n",
            rows,
            iterations,
            parallel ? "parallel-requested" : "serial",
            unordered ? "unordered" : "ordered",
            ROW_BENCH_PASSES,
            trial,
            elapsed * 1e9 / ROW_BENCH_PASSES,
            (unsigned long long)writes,
            (unsigned long long)memory.reduction_contributions,
            (unsigned long long)memory.reduction_records,
            memory.row_parallel_jobs,
            memory.row_parallel_workers,
            driver.delegate->workers,
            worker_mask,
            (unsigned long long)(after.calls - before.calls),
            (unsigned long long)(after.requested_bytes - before.requested_bytes),
            memory.activation_cache_bytes,
            memory.system_row_capacity,
            memory.reduction_capacity,
            memory.row_worker_capacity,
            read_field(target, f.totals, TOTAL).d,
            read_field(target, f.totals, COUNT).i
        );
    }
    cleanup();
    return true;
}

TEST_CASE(source_execution_benchmark_tiny_light_and_heavy_rows)
{
    for (int parallel = 0; parallel < 2; parallel++) {
        REQUIRE(row_benchmark(16, 1, parallel, true));
        REQUIRE(row_benchmark(ROW_BENCH_LARGE, 1, parallel, true));
        REQUIRE(row_benchmark(ROW_BENCH_LARGE, 64, parallel, true));
        REQUIRE(row_benchmark(ROW_BENCH_LARGE, 64, parallel, false));
    }
    return true;
}

TEST_CASE(source_parallel_rows_keep_order_and_discard_failed_dispatch)
{
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(create(1, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        REQUIRE(publish(
            "Rows",
            "Rows :: system(c:^ReductionContribution){reduce "
            "c.target.ReductionTotals.total += "
            "c.amount;reduce c.target.ReductionTotals.count += 1;}"
        ));
        ecs_entity target = f.owners[0], first_bad = { 0 }, last = { 0 };
        for (int i = 0; i < ROW_TEST_COUNT; i++) {
            /* The later chunk's cancellation loses the first chunk's 1 when
             * folded in row order. Combining chunk sums would preserve it. */
            double amount = i == 0 ? 1 : i == 129 ? 1e16 : i == 130 ? -1e16 : 0;
            last = contribution(target, target, amount, 0);
            if (i == 64) {
                first_bad = last;
            }
        }
        double expected = 0;
        for (int i = 0; i < ROW_TEST_COUNT; i++) {
            expected += i == 0 ? 1 : i == 129 ? 1e16 : i == 130 ? -1e16 : 0;
        }
        execution_program *program = source_program(f.sources, "Rows");
        create_row_executor();
        driver.row_gate = backend && driver.delegate->workers > 1;
        uint64_t version = ecs_component_version(f.world, target, f.totals->id);
        REQUIRE(run_rows(program, 0, true) == FLOW_OK);
        REQUIRE(!atomic_load(&driver.gate_failed));
        driver.row_gate = false;
        REQUIRE(read_field(target, f.totals, TOTAL).d == expected);
        REQUIRE(read_field(target, f.totals, COUNT).i == ROW_TEST_COUNT);
        REQUIRE(ecs_component_version(f.world, target, f.totals->id) == version + 2 * ROW_TEST_COUNT);
        execution_memory_usage memory = execution_program_memory_usage(program);
        REQUIRE(
            driver.delegate->workers > 1 ? memory.row_parallel_jobs > 1
                                         : memory.row_parallel_jobs == 0
        );
        REQUIRE(memory.row_parallel_jobs <= driver.delegate->workers);
        if (backend && driver.delegate->workers > 1) {
            REQUIRE(memory.row_parallel_workers >= 2);
            unsigned workers = atomic_load(&driver.workers);
            REQUIRE(workers && (workers & (workers - 1)));
        }
        ecs_entity missing = ecs_world_create_entity(f.world);
        REQUIRE(!ecs_world_apply_commands(f.world));
        write_field(
            last,
            f.contribution,
            TARGET,
            (pin_value){ .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(missing) }
        );
        write_field(
            first_bad,
            f.contribution,
            TARGET,
            (pin_value){ .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(missing) }
        );
        version = ecs_component_version(f.world, target, f.totals->id);
        REQUIRE(run_rows(program, 1, true) < 0);
        REQUIRE(execution_program_error(program)->owner == ecs_entity_to_u64(first_bad));
        REQUIRE(read_field(target, f.totals, TOTAL).d == expected);
        REQUIRE(read_field(target, f.totals, COUNT).i == ROW_TEST_COUNT);
        REQUIRE(ecs_component_version(f.world, target, f.totals->id) == version);
        memory = execution_program_memory_usage(program);
        REQUIRE(memory.reduction_bytes == 0 && memory.system_row_bytes == 0);
        REQUIRE(memory.row_worker_capacity <= 1024 * 1024);
        cleanup();
    }
    return true;
}

static void count_profile(void *user, const execution_profile_sample *)
{
    (*(int *)user)++;
}
TEST_CASE(source_parallel_rows_publish_through_scheduler_hook)
{
    REQUIRE(create(1, BEHAVIOR_SLJIT));
    REQUIRE(publish(
        "Rows",
        "Rows :: system(c:^ReductionContribution){reduce unordered "
        "c.target.ReductionTotals.total "
        "+= c.amount;}"
    ));
    REQUIRE(publish(
        "Observe",
        "Observe :: system( t:^ReductionTotals, s:^ReductionSeen , if t.total "
        "== 256) "
        "{s.total=t.total; s.hits+=1;}"
    ));
    for (int i = 0; i < ROW_TEST_COUNT; i++) {
        contribution(f.owners[0], f.owners[0], 1, 0);
    }
    const char *names[] = { "Rows", "Observe" };
    REQUIRE(create_driver(names, 2, true));
    const schedule_desc *found = NULL;
    for (int i = 0; i < schedule_count(driver.schedule); i++) {
        const schedule_desc *desc = schedule_descriptor_at(driver.schedule, i);
        if (!strcmp(desc->name, "Rows")) {
            found = desc;
            break;
        }
    }
    REQUIRE(found);
    schedule_desc rows = *found;
    rows.run_parallel = run_row_parallel;
    REQUIRE(!schedule_register(driver.schedule, &rows));
    uint64_t version = ecs_component_version(f.world, f.owners[0], f.totals->id);
    REQUIRE(!run_driver(0));
    REQUIRE(driver.rows[0].parallel_runs == 1);
    REQUIRE(driver.rows[0].finishes == 1 && driver.rows[1].finishes == 1);
    REQUIRE(schedule_parallel_batches(driver.schedule) == 0);
    execution_memory_usage memory = execution_program_memory_usage(
        driver.rows[0].program
    );
    REQUIRE(
        driver.delegate->workers > 1 ? memory.row_parallel_jobs > 1
                                     : memory.row_parallel_jobs == 0
    );
    REQUIRE(read_field(f.owners[0], f.totals, TOTAL).d == ROW_TEST_COUNT);
    REQUIRE(read_field(f.owners[0], f.seen, SEEN_TOTAL).d == ROW_TEST_COUNT);
    REQUIRE(read_field(f.owners[0], f.seen, SEEN_HITS).i == 1);
    REQUIRE(ecs_component_version(f.world, f.owners[0], f.totals->id) == version + 1);
    return true;
}

TEST_CASE(source_parallel_rows_fall_back_for_inspection_and_profiling)
{
    for (int observed = 0; observed < 2; observed++) {
        REQUIRE(create(1, BEHAVIOR_SLJIT));
        REQUIRE(publish(
            "Rows",
            "Rows :: system(c:^ReductionContribution){reduce unordered "
            "c.target.ReductionTotals.total "
            "+= c.amount;}"
        ));
        for (int i = 0; i < ROW_TEST_COUNT; i++) {
            contribution(f.owners[0], f.owners[0], 1, 0);
        }
        execution_program *program = source_program(f.sources, "Rows");
        int samples = 0;
        if (observed) {
            execution_program_observe(program, count_profile, &samples);
        } else {
            f.inspector = execution_program_inspector(program);
            REQUIRE(f.inspector);
        }
        create_row_executor();
        REQUIRE(run_rows(program, 0, true) == FLOW_OK);
        REQUIRE(read_field(f.owners[0], f.totals, TOTAL).d == ROW_TEST_COUNT);
        execution_memory_usage memory = execution_program_memory_usage(program);
        REQUIRE(memory.row_parallel_jobs == 0 && memory.row_parallel_workers == 0);
        REQUIRE(atomic_load(&driver.workers) == 0);
        if (observed) {
            REQUIRE(samples == ROW_TEST_COUNT);
            execution_program_observe(program, NULL, NULL);
        } else {
            REQUIRE(memory.instance_count == ROW_TEST_COUNT);
        }
        cleanup();
    }
    return true;
}

void test_source_execution_benchmark()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_execution_benchmark_tiny_light_and_heavy_rows);
    pu_clear_setup();
}

void test_source_reduction_benchmark()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_reductions_benchmark_same_authored_workload);
    pu_clear_setup();
}

void test_source_reductions()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_reductions_publish_before_dependent_filtered_query);
    RUN_TEST_CASE(source_reductions_keep_row_order_and_comparison_select_values);
    RUN_TEST_CASE(source_reductions_discard_failed_system_and_later_peers);
    RUN_TEST_CASE(source_unordered_reductions_regroup_only_when_requested);
    RUN_TEST_CASE(source_reductions_run_on_distinct_worker_threads);
    RUN_TEST_CASE(source_reductions_discard_prepared_cohort_after_prepare_failure);
    RUN_TEST_CASE(source_reductions_preflight_all_records_before_publication);
    RUN_TEST_CASE(source_parallel_rows_keep_order_and_discard_failed_dispatch);
    RUN_TEST_CASE(source_parallel_rows_publish_through_scheduler_hook);
    RUN_TEST_CASE(source_parallel_rows_fall_back_for_inspection_and_profiling);
    RUN_TEST_CASE(source_reductions_cancel_later_paused_peer_after_earlier_failure);
    RUN_TEST_CASE(source_reductions_resume_post_physics_without_replaying_prior_stages);
    RUN_TEST_CASE(source_reductions_discard_paused_log_when_descriptor_edit_restarts_phase);
    pu_clear_setup();
}
