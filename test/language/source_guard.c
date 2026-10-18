#include "runtime/behaviors/source_guard.h"

#include "execution/status.h"
#include "language/lower.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Three fixed metadata-only cases: six bytecode module preparations total,
 * at most two registrations and two publication entries per operation. Each
 * source is below 128 bytes. No dispatch, instructions, files, clocks, frames,
 * threads or sleeps; compilation/backend execution coverage lives elsewhere. */
static const script_catalog_capability refresh = {
    .id = 10,
    .name = "Refresh",
    .kind = SCRIPT_CAPABILITY_EVENT,
    .completion_outcome = -1,
};
static const script_catalog catalog = { .capabilities = &refresh,
                                        .capability_count = 1 };
static source_registry *registry;
static source_staging *stage;
static execution_module *modules[3];
typedef struct
{
    int validations, notifications, current_count, candidate_count, published_count;
    const execution_module *current_producer, *candidate_producer, *published_producer;
    const execution_module *published_companion;
    bool permissive;
} guard_state;
static guard_state state, other;

static void cleanup()
{
    source_discard(stage);
    stage = NULL;
    if (registry) {
        source_registry_free(registry);
    }
    registry = NULL;
    for (int i = 0; i < 3; i++) {
        execution_module_free(modules[i]);
        modules[i] = NULL;
    }
    state = (guard_state){ 0 };
    other = (guard_state){ 0 };
}
static bool compile(int index, const char *text)
{
    script_syntax *syntax = script_parse(text);
    script_analysis *analysis = script_analyze(syntax, &catalog);
    const script_lower_bindings bindings = { 0 };
    execution_module_plan *plan = analysis->valid
                                    ? script_lower(analysis, &catalog, &bindings)
                                    : NULL;
    char error[192] = "invalid source";
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "source guard semantic: %s\n", analysis->diagnostics[i].message);
    }
    for (int i = 0; plan && i < plan->diagnostic_count; i++) {
        fprintf(stderr, "source guard lower: %s\n", plan->diagnostics[i].message);
    }
    modules[index] = plan && plan->valid
                       ? execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error)
                       : NULL;
    if (!modules[index]) {
        fprintf(stderr, "source guard module: %s\n", error);
    }
    execution_module_plan_free(plan);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    return modules[index] != NULL;
}
static const execution_module *named(const source_module_view *views, int count, const char *name)
{
    for (int i = 0; i < count; i++) {
        if (!strcmp(views[i].name, name)) {
            return views[i].module;
        }
    }
    return NULL;
}
static bool producer(const execution_module *module)
{
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info function = execution_module_function_at(module, i);
        if (function.kind == EXECUTION_FUNCTION_HANDLER &&
            function.event_name && !strcmp(function.event_name, "Refresh")) {
            return true;
        }
    }
    return false;
}
static bool validate(
    void *user,
    const source_module_view *current,
    int current_count,
    const source_module_view *candidate,
    int candidate_count,
    char *error,
    size_t size
)
{
    guard_state *s = user;
    assert(current_count <= 2 && candidate_count <= 2);
    s->validations++;
    s->current_count = current_count;
    s->candidate_count = candidate_count;
    s->current_producer = named(current, current_count, "Producer");
    s->candidate_producer = named(candidate, candidate_count, "Producer");
    if (s->permissive) {
        return true;
    }
    if ((s->current_producer && !s->candidate_producer) ||
        (s->candidate_producer && !producer(s->candidate_producer))) {
        if (size) {
            snprintf(error, size, "Producer must retain its Refresh handler");
        }
        return false;
    }
    return true;
}
static void published(void *user, const source_module_view *views, int count)
{
    guard_state *s = user;
    assert(count <= 2);
    s->notifications++;
    s->published_count = count;
    /* Only identity metadata escapes; the fixture separately owns the modules.
     * The borrowed view array and registration-name strings are not retained. */
    s->published_producer = named(views, count, "Producer");
    s->published_companion = named(views, count, "Companion");
}
static source_publication_guard guard(guard_state *s)
{
    return (source_publication_guard){ .user = s,
                                       .validate = validate,
                                       .published = published };
}
TEST_CASE(source_guard_install_accepts_empty_and_rejects_invalid_existing_atomically)
{
    char error[192];
    registry = source_registry_new(NULL);
    REQUIRE(source_set_publication_guard(registry, guard(&state), error, sizeof error));
    REQUIRE(state.validations == 1 && state.notifications == 1);
    REQUIRE(!state.current_count && !state.candidate_count && !state.published_count);
    REQUIRE(source_set_publication_guard(registry, guard(&state), error, sizeof error));
    REQUIRE(state.validations == 1 && state.notifications == 1);
    REQUIRE(!source_set_publication_guard(registry, guard(&other), error, sizeof error));
    REQUIRE(!other.validations && !other.notifications && error[0]);
    source_registry_free(registry);
    registry = source_registry_new(NULL);
    state = (guard_state){ 0 };
    REQUIRE(compile(0, "Absent :: library { Value :: () -> int { return 1; } }"));
    REQUIRE(source_register(registry, "Producer", modules[0], NULL) == FLOW_OK);
    execution_program *before = source_program(registry, "Producer");
    REQUIRE(before && execution_program_module(before) == modules[0]);
    REQUIRE(!source_set_publication_guard(registry, guard(&state), error, sizeof error));
    REQUIRE(state.validations == 1 && !state.notifications && error[0]);
    REQUIRE(source_program(registry, "Producer") == before);
    REQUIRE(execution_program_module(before) == modules[0]);
    other.permissive = true;
    REQUIRE(source_set_publication_guard(registry, guard(&other), error, sizeof error));
    REQUIRE(other.validations == 1 && other.notifications == 1);
    REQUIRE(other.published_producer == modules[0]);
    REQUIRE(source_program(registry, "Producer") == before);
    return true;
}
TEST_CASE(source_guard_rejects_whole_batch_and_later_producer_removal)
{
    char error[192];
    REQUIRE(compile(0, "Rows :: behavior { on Refresh {} }"));
    REQUIRE(compile(1, "Other :: library { Value :: () -> int { return 1; } }"));
    REQUIRE(compile(2, "Replacement :: library { Value :: () -> int { return 2; } }"));
    registry = source_registry_new(NULL);
    REQUIRE(source_set_publication_guard(registry, guard(&state), error, sizeof error));
    const source_publication initial[] = {
        { .name = "Producer", .module = modules[0] },
        { .name = "Companion", .module = modules[1] },
    };
    REQUIRE(source_publish(registry, initial, 2, NULL, error, sizeof error) == FLOW_OK);
    execution_program *rows = source_program(registry, "Producer");
    execution_program *companion = source_program(registry, "Companion");
    REQUIRE(rows && companion && state.notifications == 2);
    const source_publication rejected[] = {
        { .name = "Renamed", .previous_name = "Companion", .module = modules[1] },
        { .name = "Producer", .previous_name = "Producer", .module = modules[2] },
    };
    REQUIRE(source_publish(registry, rejected, 2, NULL, error, sizeof error) < 0 && error[0]);
    REQUIRE(state.current_count == 2 && state.candidate_count == 2);
    REQUIRE(state.current_producer == modules[0] && state.candidate_producer == modules[2]);
    REQUIRE(state.notifications == 2);
    REQUIRE(source_program(registry, "Producer") == rows);
    REQUIRE(source_program(registry, "Companion") == companion);
    REQUIRE(!source_registered(registry, "Renamed"));
    REQUIRE(execution_program_module(rows) == modules[0]);
    REQUIRE(execution_program_module(companion) == modules[1]);
    REQUIRE(source_remove(registry, "Producer") < 0);
    REQUIRE(state.notifications == 2 && state.candidate_count == 1);
    REQUIRE(source_program(registry, "Producer") == rows);
    REQUIRE(source_remove(registry, "Companion") == FLOW_OK);
    REQUIRE(state.notifications == 3 && state.published_count == 1);
    REQUIRE(state.published_producer == execution_program_module(rows));
    REQUIRE(!source_registered(registry, "Companion"));
    return true;
}
TEST_CASE(source_guard_reports_applied_generations_and_ignores_discard)
{
    char error[192];
    REQUIRE(compile(0, "Rows :: behavior { count:int=1; on Refresh {} }"));
    REQUIRE(compile(1, "Rows :: behavior { count:int=2; on Refresh {} }"));
    registry = source_registry_new(NULL);
    REQUIRE(source_set_publication_guard(registry, guard(&state), error, sizeof error));
    REQUIRE(source_register(registry, "Producer", modules[0], NULL) == FLOW_OK);
    execution_program *program = source_program(registry, "Producer");
    REQUIRE(program && state.notifications == 2 && state.published_producer == modules[0]);
    source_publication replacement = {
        .name = "Producer",
        .previous_name = "Producer",
        .module = modules[1],
    };
    stage = source_stage(registry, &replacement, 1, NULL, error, sizeof error);
    REQUIRE(
        stage && state.current_producer == modules[0] &&
        state.candidate_producer == modules[1]
    );
    REQUIRE(state.notifications == 2 && execution_program_module(program) == modules[0]);
    source_discard(stage);
    stage = NULL;
    REQUIRE(state.notifications == 2 && state.published_producer == modules[0]);
    REQUIRE(source_program(registry, "Producer") == program);
    REQUIRE(execution_program_module(program) == modules[0]);
    REQUIRE(source_publish(registry, &replacement, 1, NULL, error, sizeof error) == FLOW_OK);
    REQUIRE(source_program(registry, "Producer") == program);
    REQUIRE(execution_program_module(program) == modules[1]);
    REQUIRE(
        state.notifications == 3 &&
        state.published_producer == execution_program_module(program)
    );
    REQUIRE(source_publish(registry, NULL, 0, NULL, error, sizeof error) == FLOW_OK);
    REQUIRE(state.notifications == 4 && state.published_producer == modules[1]);
    REQUIRE(state.current_producer == modules[1] && state.candidate_producer == modules[1]);
    int validations = state.validations;
    REQUIRE(source_set_publication_guard(registry, guard(&state), error, sizeof error));
    REQUIRE(state.validations == validations && state.notifications == 4);
    replacement.module = modules[0];
    REQUIRE(source_publish(registry, &replacement, 1, NULL, error, sizeof error) == FLOW_OK);
    REQUIRE(state.notifications == 5 && state.published_producer == modules[0]);
    REQUIRE(
        state.published_producer ==
        execution_program_module(source_program(registry, "Producer"))
    );
    return true;
}
void test_language_source_guard()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_guard_install_accepts_empty_and_rejects_invalid_existing_atomically);
    RUN_TEST_CASE(source_guard_rejects_whole_batch_and_later_producer_removal);
    RUN_TEST_CASE(source_guard_reports_applied_generations_and_ignores_discard);
    pu_clear_setup();
}
