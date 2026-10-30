#include "authoring/source_collection.h"
#include "authoring/source_navigation.h"
#include "backend.h"
#include "execution/status.h"
#include "support/arena.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum
{
    SOURCE_COUNT = 4,
    TRIAL_COUNT = 5
};
static char root[128], paths[SOURCE_COUNT][256];
static source_catalog_edit edits[SOURCE_COUNT];
static script_catalog_capability tick;
static script_catalog catalog;
static script_lower_bindings bindings;
static authoring_sources *previous, *current;
static execution_program *program;
static const char ORIGINAL[] = "Leaf :: library { Value :: (amount: number u = "
                               "2) -> number u { return amount; } }";
static const char EDITED[] = "Leaf :: library { Value :: (amount: number u = "
                             "2) -> number u { return amount + 7; } }";
static const char CATEGORY[] = "Lifecycle", KEYWORDS[] = "tick update",
                  DESCRIPTION[] = "Run one update.";

static void setup()
{
    strcpy(root, "/tmp/chibi-incremental-benchmark-XXXXXX");
    assert(mkdtemp(root));
    static const char *const names[SOURCE_COUNT] = {
        "libraries/Leaf.chibi",
        "libraries/Middle.chibi",
        "behaviors/Caller.chibi",
        "behaviors/Independent.chibi"
    };
    static const char *const texts[SOURCE_COUNT] = {
        ORIGINAL,
        "Middle :: library { Value :: () -> u { return Leaf.Value() + 1; } }",
        "Caller :: behavior { value: retain number = 0; on Tick :: () { value "
        "= "
        "Middle.Value(); } }",
        "Independent :: behavior { value: retain number = 0; on Tick :: () { "
        "value "
        "= 7; } }"
    };
    for (int i = 0; i < SOURCE_COUNT; i++) {
        snprintf(paths[i], sizeof paths[i], "%s/%s", root, names[i]);
        edits[i] = (source_catalog_edit){ .path = paths[i], .text = texts[i] };
    }
    tick = (script_catalog_capability){ .id = 10,
                                        .name = "Tick",
                                        .kind = SCRIPT_CAPABILITY_EVENT,
                                        .completion_outcome = -1,
                                        .category = CATEGORY,
                                        .keywords = KEYWORDS,
                                        .description = DESCRIPTION };
    catalog = (script_catalog){ .capabilities = &tick,
                                .capability_count = 1,
                                .tick_event = tick.id };
    bindings = (script_lower_bindings){ .generation = 1 };
}
static void cleanup()
{
    execution_program_free(program);
    program = NULL;
    authoring_sources_free(current);
    authoring_sources_free(previous);
    current = previous = NULL;
    rmdir(root);
}
static authoring_sources *prepare(const authoring_sources *prior)
{
    return authoring_sources_prepare_incremental(
        source_catalog_replaced(root, &catalog, edits, SOURCE_COUNT),
        &bindings,
        BEHAVIOR_BYTECODE,
        prior
    );
}
static const authoring_source *source(const authoring_sources *collection, const char *name)
{
    for (int i = 0; i < authoring_sources_count(collection); i++) {
        const authoring_source *candidate = authoring_sources_at(collection, i);
        if (candidate->analysis->name && !strcmp(candidate->analysis->name, name)) {
            return candidate;
        }
    }
    return NULL;
}
static bool value_is(const authoring_sources *collection, const char *name, double expected)
{
    const authoring_source *input = source(collection, name);
    if (!input || !input->valid) {
        return false;
    }
    uint64_t state = 0;
    for (int i = 0; i < input->analysis->symbol_count; i++) {
        const script_symbol *symbol = &input->analysis->symbols[i];
        if (symbol->kind == SCRIPT_STATE && !strcmp(symbol->name, "value")) {
            state = symbol->id;
        }
    }
    program = execution_program_new(input->module, NULL, NULL);
    if (!program) {
        return false;
    }
    execution_instance *instance = execution_program_attach(program, 1);
    bool ok = state && instance &&
              execution_instance_dispatch(instance, tick.id, NULL, 0, 0, 0, 1000) ==
                  FLOW_OK;
    const pin_value *value = ok ? execution_instance_state(instance, state) : NULL;
    ok = value && value->type == PIN_F32 && value->f == (float)expected;
    execution_program_free(program);
    program = NULL;
    return ok;
}
static bool presentation_is(
    const authoring_sources *collection,
    const char *category,
    const char *keywords,
    const char *description
)
{
    const authoring_source *caller = source(collection, "Caller");
    if (!caller) {
        return false;
    }
    authoring_document document = { .path = caller->path,
                                    .syntax = caller->syntax,
                                    .analysis = caller->analysis };
    authoring_navigation *navigation = authoring_navigation_new(
        &document,
        1,
        authoring_sources_catalog(collection)
    );
    const script_catalog_capability *event = script_catalog_capability_by_id(
        authoring_navigation_catalog(navigation),
        tick.id
    );
    bool ok = event && !strcmp(event->category, category) &&
              !strcmp(event->keywords, keywords) &&
              !strcmp(event->description, description);
    authoring_navigation_free(navigation);
    return ok;
}
static double seconds()
{
    struct timespec time;
    int status = clock_gettime(CLOCK_MONOTONIC, &time);
    assert(status == 0);
    return time.tv_sec + time.tv_nsec * 1e-9;
}
TEST_CASE(incremental_preparation_benchmark)
{
    previous = prepare(NULL);
    REQUIRE(authoring_sources_valid(previous));
    REQUIRE(authoring_sources_count(previous) == SOURCE_COUNT);
    REQUIRE(value_is(previous, "Caller", 3));
    static const char *const modes[] = { "unchanged", "body", "presentation" };
    for (int mode = 0; mode < 3; mode++) {
        edits[0].text = mode == 1 ? EDITED : ORIGINAL;
        tick.category = mode == 2 ? "Frame Events" : CATEGORY;
        tick.keywords = mode == 2 ? "simulation frame" : KEYWORDS;
        tick.description = mode == 2 ? "Execute each simulation tick." : DESCRIPTION;
        for (int trial = 0; trial < TRIAL_COUNT; trial++) {
            xalloc_stats before, after;
            xalloc_stats_get(&before);
            uint64_t blocks = arena_block_allocations();
            double start = seconds();
            current = prepare(previous);
            double elapsed = seconds() - start;
            xalloc_stats_get(&after);
            blocks = arena_block_allocations() - blocks;
            REQUIRE(authoring_sources_valid(current));
            REQUIRE(authoring_sources_count(current) == SOURCE_COUNT);
            int reused = authoring_sources_reused(current);
            if (mode == 0) {
                REQUIRE(reused == SOURCE_COUNT);
                REQUIRE(source(current, "Caller") == source(previous, "Caller"));
            } else if (mode == 1) {
                REQUIRE(reused == 1);
                REQUIRE(source(current, "Independent") == source(previous, "Independent"));
                REQUIRE(source(current, "Caller") != source(previous, "Caller"));
            }
            if (mode == 2) {
                REQUIRE(reused == SOURCE_COUNT);
                REQUIRE(source(current, "Caller") == source(previous, "Caller"));
            }
            REQUIRE(presentation_is(current, tick.category, tick.keywords, tick.description));
            REQUIRE(presentation_is(previous, CATEGORY, KEYWORDS, DESCRIPTION));
            REQUIRE(value_is(current, "Caller", mode == 1 ? 10 : 3));
            REQUIRE(value_is(current, "Independent", 7));
            REQUIRE(value_is(previous, "Caller", 3));
            printf(
                "INCREMENTAL mode=%s trial=%d sources=%d reused=%d "
                "prepare_ms=%.6f "
                "xalloc_calls=%llu requested_bytes=%llu arena_blocks=%llu\n",
                modes[mode],
                trial,
                SOURCE_COUNT,
                reused,
                elapsed * 1e3,
                (unsigned long long)(after.calls - before.calls),
                (unsigned long long)(after.requested_bytes - before.requested_bytes),
                (unsigned long long)blocks
            );
            if (mode == 2 && trial == TRIAL_COUNT - 1) {
                authoring_sources_free(previous);
                previous = NULL;
                REQUIRE(presentation_is(current, tick.category, tick.keywords, tick.description));
                REQUIRE(value_is(current, "Caller", 3));
            }
            authoring_sources_free(current);
            current = NULL;
            if (previous) {
                REQUIRE(value_is(previous, "Caller", 3));
            }
        }
    }
    return true;
}
void test_language_incremental_benchmark()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(incremental_preparation_benchmark);
    pu_clear_setup();
}
