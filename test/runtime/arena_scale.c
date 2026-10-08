#include "arena_reference.h"
#include "assets/prototype.h"
#include "authoring/preview.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/flow.h"
#include "runtime/component_catalog.h"
#include "support/array.h"
#include "support/text_file.h"
#include "test/support/code_fixture.h"

#include <assert.h>
#include <glob.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static double seconds()
{
    struct timespec now;
    assert(!clock_gettime(CLOCK_MONOTONIC, &now));
    return now.tv_sec + now.tv_nsec * 1e-9;
}
static double peak_memory_mb()
{
    struct rusage usage;
    assert(!getrusage(RUSAGE_SELF, &usage));
#ifdef __APPLE__
    return usage.ru_maxrss / (1024.0 * 1024.0);
#else
    return usage.ru_maxrss / 1024.0;
#endif
}
static void load_definition(game_preview_world *live, const char *path)
{
    char *json = text_file_read(path);
    assert(json);
    behavior_asset *asset = test_project_asset(path, json);
    free(json);
    assert(asset);
    game_preview_definition *definition = game_preview_definition_new(asset->name, asset);
    assert(!game_preview_add_definition(live, definition));
}
static void load_catalog(game_preview_world *live, const char *pattern)
{
    glob_t paths = { 0 };
    assert(!glob(pattern, 0, NULL, &paths));
    for (size_t i = 0; i < paths.gl_pathc; i++) {
        load_definition(live, paths.gl_pathv[i]);
    }
    globfree(&paths);
}
static int compare(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Actual ArenaEnemy components and all authored attachments. Hold positions
 * fixed to isolate the world Tick dispatcher, queries and behavior execution.
 * Rendering, physics and authored wave spawning are separate acceptance paths. */
int bench_arena_scale(int count, int frames, bool portable, bool profile, int mode)
{
    bool reference = mode == 1, clocks = mode == 2;
    assert(count > 0 && count <= 20000 && frames >= 8 && frames <= 10000);
    behavior_backend backend = portable ? BEHAVIOR_BYTECODE : BEHAVIOR_SLJIT;
    behavior_backend previous = behavior_debug_backend(backend);
    srand(12345);
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(!ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL));
    }
    game_preview_world *live = game_preview_world_new(world);
    load_catalog(live, "content/events/*.kdl");
    load_catalog(live, "content/libraries/*.chibi");
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    char *json = text_file_read("content/prototypes/ArenaEnemy.kdl"), error[256];
    assert(json);
    game_prototype *prototype = game_prototype_load(json, &registry, error, sizeof error);
    free(json);
    assert(prototype);
    for (int i = 0; i < acount(prototype->graphs); i++) {
        char path[256];
        snprintf(path, sizeof path, "content/behaviors/%s.chibi", prototype->graphs[i]);
        load_definition(live, path);
    }
    game_behavior_set_profiling(live->runtime->behaviors, profile);
    game_entities_profile_queries(live->runtime->entities, profile);
    game_behavior_set_delta_seconds(live->runtime->behaviors, 1.0 / 60.0);
    ecs_entity target = ecs_world_create_entity(world);
    assert(!GAME_GameTransform_add(world, target, 0));
    assert(!GAME_GameCollider_add(world, target, 0));
    assert(!ecs_world_apply_commands(world));
    *GAME_GameTransform_get(world, target) = (GameTransform){ 0 };
    *GAME_GameCollider_get(world, target) = (GameCollider){ .category = 1 };
    ecs_entity *enemies = NULL;
    double setup = seconds(), memory_before = peak_memory_mb();
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    for (int i = 0; i < count; i++) {
        ecs_entity enemy;
        assert(!game_prototype_spawn_components(prototype, world, &enemy, error, sizeof error));
        *GAME_GameTransform_get(world, enemy) = (GameTransform){ 100 + (i % 100) * 8,
                                                                 100 + (i / 100) * 8 };
        assert(!game_behavior_set_attachments(
            live->runtime->behaviors,
            enemy,
            (const char *const *)prototype->graphs,
            acount(prototype->graphs)
        ));
        apush(enemies, enemy);
    }
    printf(
        "scale_setup backend=%s entities=%d attachments=%d setup_ms=%.3f "
        "peak_rss_mb=%.3f peak_growth_mb=%.3f\n",
        behavior_backend_name(backend),
        count,
        count * acount(prototype->graphs),
        (seconds() - setup) * 1000,
        peak_memory_mb(),
        peak_memory_mb() - memory_before
    );
    fflush(stdout);
    arena_reference_row *rows = reference ? reference_rows(live, enemies) : NULL;
    arena_reference_target *targets = NULL;
    GameVelocity *snapshot = NULL;
    double *reference_ms = NULL;
    if (reference) {
        asetlen(snapshot, count);
    }
    double *samples = NULL, *updates_ms = NULL, *pumps_ms = NULL;
    double *clock_ms = NULL, *complete_ms = NULL;
    double first_tick_ms = 0;
    game_behavior_profile_frame totals = { 0 };
    uint64_t batches = behavior_batch_dispatches(), prepared = 0, native = 0;
    for (int frame = -8; frame < frames; frame++) {
        if (!frame) {
            game_entities_profile_queries(live->runtime->entities, profile);
        }
        game_behavior_profile_begin(live->runtime->behaviors);
        double now = (frame + 8) / 60.0;
        double clock_start = clocks ? seconds() : 0;
        game_behavior_set_time(live->runtime->behaviors, now);
        if (clocks) {
            game_behavior_set_delta_seconds(live->runtime->behaviors, 1.0 / 60.0);
        }
        double clock_elapsed = clocks ? (seconds() - clock_start) * 1000 : 0;
        double reference_elapsed = 0;
        bool reference_first = (frame + 8) % 2 == 0;
        if (reference && reference_first) {
            double start = seconds();
            reference_tick(world, rows, &targets);
            reference_elapsed = (seconds() - start) * 1000;
            reference_snapshot(rows, snapshot, false);
        }
        double start = seconds();
        assert(game_behavior_update(live->runtime->behaviors, NULL) == count * 2);
        double updated = seconds();
        assert(!game_behavior_pump(live->runtime->behaviors, now));
        if (frame >= 0) {
            assert(!game_behavior_last_pump_count(live->runtime->behaviors));
        }
        game_behavior_update_stats updates = game_behavior_last_update(
            live->runtime->behaviors
        );
        assert(
            updates.entries == count * 2 && updates.prepared == count * 2 &&
            !updates.scheduled
        );
        assert(updates.native_loop == (!portable && !profile ? count * 2 : 0));
        prepared += updates.prepared;
        native += updates.native_loop;
        double finished = seconds(), elapsed = (finished - start) * 1000;
        if (reference) {
            reference_snapshot(rows, snapshot, reference_first);
            if (!reference_first) {
                double start = seconds();
                reference_tick(world, rows, &targets);
                reference_elapsed = (seconds() - start) * 1000;
                reference_snapshot(rows, snapshot, true);
            }
            if (frame >= 0) {
                apush(reference_ms, reference_elapsed);
            }
        }
        if (frame == -8) {
            first_tick_ms = elapsed;
        }
        if (frame < 0) {
            continue;
        }
        apush(samples, elapsed);
        apush(updates_ms, (updated - start) * 1000);
        apush(pumps_ms, (finished - updated) * 1000);
        if (clocks) {
            apush(clock_ms, clock_elapsed);
            apush(complete_ms, clock_elapsed + elapsed);
        }
        const game_behavior_profile_frame *p = game_behavior_profile(
            live->runtime->behaviors
        );
        if (!totals.count) {
            totals = *p;
            continue;
        }
        assert(totals.count == p->count);
        for (int i = 0; i < p->count; i++) {
            assert(!strcmp(totals.rows[i].behavior, p->rows[i].behavior));
            assert(!strcmp(totals.rows[i].event, p->rows[i].event));
            totals.rows[i].cpu_ms += p->rows[i].cpu_ms;
            totals.rows[i].setup_ms += p->rows[i].setup_ms;
            totals.rows[i].dispatches += p->rows[i].dispatches;
        }
    }
    if (profile) {
        game_entity_query_profile q = game_entities_query_profile(live->runtime->entities);
        printf(
            "scale_queries searches=%llu world_candidates=%llu "
            "matching_candidates=%llu cache_builds=%llu\n",
            (unsigned long long)q.searches,
            (unsigned long long)q.world_candidates,
            (unsigned long long)q.matching_candidates,
            (unsigned long long)q.cache_builds
        );
        assert(q.searches == (uint64_t)count * frames);
        assert(q.cache_builds == (uint64_t)frames);
        assert(q.world_candidates == (uint64_t)(count + 1) * frames);
    }
    printf(
        "scale_updates prepared=%llu native_loop=%llu\n",
        (unsigned long long)prepared,
        (unsigned long long)native
    );
    double checksum = 0;
    for (int i = 0; i < count; i++) {
        const GameTransform *position = GAME_GameTransform_get_const(world, enemies[i]);
        const GameVelocity *velocity = GAME_GameVelocity_get_const(world, enemies[i]);
        float speed = GAME_GameMovement_get_const(world, enemies[i])->speed;
        double distance = hypot(position->x, position->y);
        assert(fabs(velocity->x + speed * position->x / distance) < .001);
        assert(fabs(velocity->y + speed * position->y / distance) < .001);
        checksum += velocity->x + velocity->y;
        int attachments = game_behavior_attachment_count_for_entity(
            live->runtime->behaviors,
            enemies[i]
        );
        assert(attachments == acount(prototype->graphs));
        for (int j = 0; j < attachments; j++) {
            game_behavior_attachment_info info;
            assert(
                !game_behavior_attachment_info_at(live->runtime->behaviors, enemies[i], j, &info)
            );
            assert(behavior_instance_backend(info.instance) == backend);
        }
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    qsort(samples, (size_t)acount(samples), sizeof *samples, compare);
    qsort(updates_ms, (size_t)acount(updates_ms), sizeof *updates_ms, compare);
    qsort(pumps_ms, (size_t)acount(pumps_ms), sizeof *pumps_ms, compare);
    if (clocks) {
        qsort(clock_ms, (size_t)acount(clock_ms), sizeof *clock_ms, compare);
        qsort(complete_ms, (size_t)acount(complete_ms), sizeof *complete_ms, compare);
        printf(
            "scale_clocks clock_p50_ms=%.6f complete_p50_ms=%.6f "
            "complete_p95_ms=%.6f\n",
            clock_ms[(frames - 1) / 2],
            complete_ms[(frames - 1) / 2],
            complete_ms[(frames - 1) * 95 / 100]
        );
    }
    if (reference) {
        qsort(reference_ms, (size_t)acount(reference_ms), sizeof *reference_ms, compare);
        printf(
            "scale_reference c_p50_ms=%.6f c_p95_ms=%.6f update_c_ratio=%.3f "
            "tick_c_ratio=%.3f snapshots=%d\n",
            reference_ms[(frames - 1) / 2],
            reference_ms[(frames - 1) * 95 / 100],
            updates_ms[(frames - 1) / 2] / reference_ms[(frames - 1) / 2],
            samples[(frames - 1) / 2] / reference_ms[(frames - 1) / 2],
            count * (frames + 8)
        );
    }
    printf(
        "scale_phases update_p50_ms=%.6f pump_p50_ms=%.6f first_tick_ms=%.6f\n",
        updates_ms[(frames - 1) / 2],
        pumps_ms[(frames - 1) / 2],
        first_tick_ms
    );
    printf(
        "scale backend=%s entities=%d frames=%d profile=%d tick_p50_ms=%.6f "
        "tick_p95_ms=%.6f ns_per_entity=%.3f batches=%llu checksum=%.9f "
        "peak_rss_mb=%.3f oracle_calls=0\n",
        behavior_backend_name(backend),
        count,
        frames,
        profile,
        samples[(frames - 1) / 2],
        samples[(frames - 1) * 95 / 100],
        samples[(frames - 1) / 2] * 1e6 / count,
        (unsigned long long)(behavior_batch_dispatches() - batches + prepared),
        checksum,
        peak_memory_mb()
    );
    for (int i = 0; i < totals.count; i++) {
        printf(
            "scale_profile backend=%s behavior=%s event=%s exec_ms=%.6f "
            "setup_ms=%.6f dispatches=%llu\n",
            behavior_backend_name(backend),
            totals.rows[i].behavior,
            totals.rows[i].event,
            totals.rows[i].cpu_ms / frames,
            totals.rows[i].setup_ms / frames,
            (unsigned long long)totals.rows[i].dispatches
        );
    }
    fflush(stdout);
    afree(samples);
    afree(updates_ms);
    afree(pumps_ms);
    afree(clock_ms);
    afree(complete_ms);
    afree(reference_ms);
    afree(snapshot);
    afree(targets);
    afree(rows);
    afree(enemies);
    game_prototype_free(prototype);
    game_preview_world_free(live);
    ecs_world_free(world);
    behavior_debug_backend(previous);
    return 0;
}

int bench_neon_pursuit(int count, int frames, bool portable, bool profile)
{
    assert(count > 0 && count <= 20000 && frames >= 8 && frames <= 10000);
    behavior_backend backend = portable ? BEHAVIOR_BYTECODE : BEHAVIOR_SLJIT;
    behavior_backend previous = behavior_debug_backend(backend);
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(!ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL));
    }
    game_preview_world *live = game_preview_world_new(world);
    load_definition(live, "projects/arena2d/content/behaviors/Pursuit.chibi");
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    char *json = text_file_read("projects/arena2d/content/prototypes/Swarmer.kdl"),
         error[256];
    assert(json);
    game_prototype *prototype = game_prototype_load(json, &registry, error, sizeof error);
    free(json);
    assert(prototype);
    game_behavior_set_profiling(live->runtime->behaviors, profile);
    game_entities_profile_queries(live->runtime->entities, profile);
    game_behavior_set_delta_seconds(live->runtime->behaviors, 1.0 / 60.0);

    ecs_entity target = ecs_world_create_entity(world);
    assert(!GAME_GameTransform_add(world, target, 0));
    assert(!GAME_GameCollider_add(world, target, 0));
    assert(!ecs_world_apply_commands(world));
    *GAME_GameTransform_get(world, target) = (GameTransform){ 0 };
    *GAME_GameCollider_get(world, target) = (GameCollider){ .category = 1 };

    ecs_entity *enemies = NULL;
    double setup = seconds();
    for (int i = 0; i < count; i++) {
        ecs_entity enemy;
        assert(!game_prototype_spawn_components(prototype, world, &enemy, error, sizeof error));
        *GAME_GameTransform_get(world, enemy) = (GameTransform){
            100 + (i % 100) * 8,
            100 + (i / 100) * 8,
        };
        const char *behavior = "Pursuit";
        assert(!game_behavior_set_attachments(live->runtime->behaviors, enemy, &behavior, 1));
        apush(enemies, enemy);
    }
    printf(
        "neon_pursuit_setup backend=%s entities=%d setup_ms=%.3f\n",
        behavior_backend_name(backend),
        count,
        (seconds() - setup) * 1000
    );
    fflush(stdout);

    double *samples = NULL;
    uint64_t prepared = 0, native = 0;
    for (int frame = -8; frame < frames; frame++) {
        if (!frame) {
            game_entities_profile_queries(live->runtime->entities, profile);
        }
        game_behavior_profile_begin(live->runtime->behaviors);
        double start = seconds();
        game_behavior_set_time(live->runtime->behaviors, (frame + 8) / 60.0);
        assert(game_behavior_update(live->runtime->behaviors, NULL) == count);
        assert(!game_behavior_pump(live->runtime->behaviors, (frame + 8) / 60.0));
        game_behavior_update_stats updates = game_behavior_last_update(
            live->runtime->behaviors
        );
        assert(updates.entries == count && updates.prepared == count && !updates.scheduled);
        assert(updates.native_loop == (!portable && !profile ? count : 0));
        prepared += updates.prepared;
        native += updates.native_loop;
        if (frame >= 0) {
            apush(samples, (seconds() - start) * 1000);
        }
    }
    if (profile) {
        game_entity_query_profile q = game_entities_query_profile(live->runtime->entities);
        printf(
            "neon_pursuit_queries searches=%llu world_candidates=%llu "
            "matching_candidates=%llu cache_builds=%llu\n",
            (unsigned long long)q.searches,
            (unsigned long long)q.world_candidates,
            (unsigned long long)q.matching_candidates,
            (unsigned long long)q.cache_builds
        );
        assert(q.cache_builds == (uint64_t)frames);
        assert(q.world_candidates == (uint64_t)(count + 1) * frames);
    }
    qsort(samples, (size_t)acount(samples), sizeof *samples, compare);
    printf(
        "neon_pursuit backend=%s entities=%d frames=%d profile=%d "
        "tick_p50_ms=%.6f tick_p95_ms=%.6f prepared=%llu native_loop=%llu\n",
        behavior_backend_name(backend),
        count,
        frames,
        profile,
        samples[(frames - 1) / 2],
        samples[(frames - 1) * 95 / 100],
        (unsigned long long)prepared,
        (unsigned long long)native
    );
    fflush(stdout);
    afree(samples);
    afree(enemies);
    game_prototype_free(prototype);
    game_preview_world_free(live);
    ecs_world_free(world);
    behavior_debug_backend(previous);
    return 0;
}
