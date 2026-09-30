#include "ecs/execution.h"
#include "test/suite_internal.h"

#include <dlfcn.h>
#include <time.h>

static const behavior_function *body(behavior_asset *a)
{
    behavior_program_publish(a);
    const behavior_function *f = behavior_program_function(
        behavior_program_graph(a->program, a->graphs[1]),
        a->graphs[1]->nodes[0]
    );
    assert(f && behavior_function_entry(f));
    return f;
}
void emit_component_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int mode = 0; mode < 3; mode++) {
        char name[40];
        snprintf(name, sizeof name, "generated_component%d", mode);
        behavior_asset *a = whole_component_fixture(mode);
        behavior_function_write_c(body(a), out, name);
        behavior_asset_free(a);
    }
    assert(!fclose(out));
}
static double seconds()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec * 1e-9;
}
int bench_component(const char *path, int count, int rotation)
{
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    ecs_world *world = ecs_world_new();
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL));
    ecs_entity owner = ecs_world_create_entity(world);
    assert(!GAME_GameHealth_add(world, owner, 0));
    assert(!ecs_world_apply_commands(world));
    GameHealth *health = GAME_GameHealth_get(world, owner);
    for (int fixture = 0; fixture < 3; fixture++) {
        char name[40];
        snprintf(name, sizeof name, "generated_component%d", fixture);
        behavior_asset *a = whole_component_fixture(fixture);
        const behavior_function *f = body(a);
        int (*entries[])(behavior_function_context *) = {
            (int (*)(behavior_function_context *))behavior_function_entry(f),
            (int (*)(behavior_function_context *))dlsym(library, name)
        };
        assert(entries[0] && entries[1]);
        arena storage = { 0 };
        pin_value results[3] = { 0 };
        behavior_function_prepare_results(f, results, &storage);
        GameHealth input = { .current = 4, .maximum = 56 };
        pin_value args[] = {
            { .type = PIN_HANDLE, .u64 = ecs_entity_to_u64(owner) },
            { .type = PIN_STRUCT, .type_name = "GameHealth", .ptr = &input }
        };
        for (int scenario = 0; scenario < 5; scenario++) {
            int status = 0;
            behavior_function_context expected = { 0 };
            GameHealth reference[2] = { 0 };
            for (int mode = 0; mode < 3; mode++) {
                *health = (GameHealth){ .current = 12, .maximum = 70 };
                behavior_function_context ctx = {
                    .results = results,
                    .budget = scenario == 4 ? 2 : 100,
                    .arguments = args,
                    .argument_count = 2,
                    .world = scenario == 1 ? NULL : (const ecs_execution_view *)world,
                    .owner = scenario == 2 ? UINT64_MAX : ecs_entity_to_u64(owner)
                };
                args[0].u64 = ctx.owner;
                args[1].ptr = scenario == 3 ? NULL : &input;
                int result = mode ? entries[mode - 1](&ctx)
                                  : behavior_function_run(f, &ctx, false);
                if (!mode) {
                    expected = ctx;
                    status = result;
                    if (!result) {
                        reference[0] = *(const GameHealth *)results[1].ptr;
                        reference[1] = *(const GameHealth *)results[2].ptr;
                    }
                } else {
                    assert(result == status && ctx.steps == expected.steps);
                    if (result) {
                        assert(
                            ctx.node_id == expected.node_id &&
                            ctx.error_pin == expected.error_pin
                        );
                        assert(
                            ctx.error_source == expected.error_source &&
                            ctx.failure == expected.failure
                        );
                        assert((ctx.error_message != NULL) == (expected.error_message != NULL));
                        if (ctx.error_message) {
                            assert(!strcmp(ctx.error_message, expected.error_message));
                        }
                    } else {
                        for (int i = 0; i < 2; i++) {
                            const GameHealth *value = results[i + 1].ptr;
                            assert(
                                value->current == reference[i].current &&
                                value->maximum == reference[i].maximum
                            );
                        }
                        assert(
                            health->current == reference[1].current &&
                            health->maximum == reference[1].maximum
                        );
                    }
                }
            }
        }
        if (fixture == 2) {
            uint64_t reference = 0;
            for (int order = 0; order < 3; order++) {
                int mode = (order + rotation) % 3;
                *health = (GameHealth){ .current = 12, .maximum = 70 };
                behavior_function_context ctx = {
                    .results = results,
                    .budget = 100,
                    .arguments = args,
                    .argument_count = 2,
                    .world = (const ecs_execution_view *)world,
                    .owner = ecs_entity_to_u64(owner)
                };
                uint64_t checksum = 0;
                double start = seconds();
                for (int i = 0; i < count; i++) {
                    ctx.steps = 0;
                    int result = mode < 2 ? entries[mode](&ctx)
                                          : behavior_function_run(f, &ctx, false);
                    assert(!result);
                    checksum += (uint64_t)((const GameHealth *)results[1].ptr)->current;
                }
                double elapsed = seconds() - start;
                if (!order) {
                    reference = checksum;
                }
                assert(checksum == reference);
                printf(
                    "%s component_body ns=%.3f checksum=%llu\n",
                    (const char *[]){ "SLJIT", "GENERATED_C", "BYTECODE" }[mode],
                    elapsed * 1e9 / count,
                    (unsigned long long)checksum
                );
            }
        }
        arena_free(&storage);
        behavior_asset_free(a);
    }
    test_note("PASS: generated C, SLJIT and bytecode component state, snapshots and error parity\n");
    ecs_world_free(world);
    assert(!dlclose(library));
    return 0;
}
