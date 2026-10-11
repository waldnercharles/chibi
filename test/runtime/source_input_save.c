#include "authoring/application_catalog.h"
#include "authoring/language_catalog.h"
#include "authoring/source_prepare.h"
#include "ecs/ecs.h"
#include "execution/module.h"
#include "execution/status.h"
#include "runtime/engine_input.h"
#include "runtime/save.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <dirent.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static authoring_language_catalog *catalog;
static authoring_source *prepared;
static execution_program *program;
static execution_instance *instance;
static ecs_world *world;
static game_save *save;
static char save_directory[128];
static engine_input *input_service;
static void cleanup()
{
    execution_program_free(program);
    program = NULL;
    instance = NULL;
    authoring_source_free(prepared);
    prepared = NULL;
    authoring_language_catalog_free(catalog);
    catalog = NULL;
    if (save) {
        game_save_free(save);
    }
    save = NULL;
    if (save_directory[0]) {
        DIR *directory = opendir(save_directory);
        assert(directory);
        struct dirent *entry;
        while ((entry = readdir(directory))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
                continue;
            }
            char path[512];
            snprintf(path, sizeof path, "%s/%s", save_directory, entry->d_name);
            assert(!unlink(path));
        }
        closedir(directory);
        assert(!rmdir(save_directory));
        save_directory[0] = 0;
    }
    if (input_service) {
        engine_input_free(input_service);
    }
    input_service = NULL;
    ecs_world_free(world);
    world = NULL;
}
static uint64_t state_id(const char *name)
{
    for (int i = 0; i < prepared->analysis->symbol_count; i++) {
        const script_symbol *s = &prepared->analysis->symbols[i];
        if (s->kind == SCRIPT_STATE && !strcmp(s->name, name)) {
            return s->id;
        }
    }
    return 0;
}
static bool prepare(const char *text, behavior_backend backend)
{
    char error[256];
    native_capabilities *native = authoring_application_catalog_new(false, error, sizeof error);
    if (!native) {
        fprintf(stderr, "source feature catalog: %s\n", error);
        return false;
    }
    authoring_language_catalog_input input = { .native = native };
    catalog = authoring_language_catalog_new(&input, error, sizeof error);
    native_capabilities_free(native);
    if (!catalog) {
        fprintf(stderr, "source feature declarations: %s\n", error);
        return false;
    }
    script_lower_bindings bindings = authoring_language_bindings(catalog);
    prepared = authoring_source_prepare_document(
        "features.chibi",
        "test/native-features",
        text,
        authoring_language_catalog_view(catalog),
        &bindings,
        backend
    );
    if (!prepared->valid) {
        for (int i = 0; i < prepared->diagnostic_count; i++) {
            fprintf(
                stderr,
                "source feature compile: %s\n",
                prepared->diagnostics[i].message
            );
        }
        return false;
    }
    service_binding *services = NULL;
    if (save) {
        apush(services, ((service_binding){ "game.save", save }));
    }
    program = execution_program_new(prepared->module, world, services);
    afree(services);
    if (!program) {
        fprintf(stderr, "source feature program rejected required services/layout\n");
    }
    return program != NULL;
}
static void retire_producer()
{
    authoring_source_free(prepared);
    prepared = NULL;
    authoring_language_catalog_free(catalog);
    catalog = NULL;
}
static int dispatch(uint64_t event, unsigned budget)
{
    int status = execution_instance_dispatch(instance, event, NULL, 0, 0, 0, budget);
    if (status < 0) {
        const execution_program_failure *failure = execution_program_error(program);
        fprintf(
            stderr,
            "source feature dispatch: %d %s\n",
            status,
            failure ? failure->message : "no retained diagnostic"
        );
    }
    return status;
}
TEST_CASE(source_controller_connected_reads_live_slots)
{
    const char *
        text = "behavior Controllers { state slot:int=3; "
               "state connected:bool=true; "
               "on Tick { connected=controller_connected(Controller:slot); } }";
    for (int backend = 0; backend < 2; backend++) {
        world = ecs_world_new();
        REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        uint64_t tick = execution_module_event(prepared->module, "Tick"),
                 connected = state_id("connected");
        REQUIRE(tick && connected);
        instance = execution_program_attach(program, 0);
        REQUIRE(instance);
        retire_producer();
        /* The optional input service defaults to disconnected. */
        REQUIRE(dispatch(tick, 1024) >= 0);
        REQUIRE(!execution_instance_state(instance, connected)->b);
        input_service = engine_input_new(world, NULL);
        REQUIRE(execution_program_bind_service(program, GAME_INPUT_SERVICE, input_service) == 0);
        bool firing;
        REQUIRE(
            engine_input_controller(input_service, 3, (input_controller_state){ .connected = true }, &firing) ==
            FLOW_OK
        );
        REQUIRE(dispatch(tick, 1024) >= 0);
        REQUIRE(execution_instance_state(instance, connected)->b);
        REQUIRE(
            engine_input_controller(input_service, 0, (input_controller_state){ .connected = true }, &firing) ==
            FLOW_OK
        );
        REQUIRE(
            engine_input_controller(input_service, 3, (input_controller_state){ 0 }, &firing) == FLOW_OK
        );
        REQUIRE(dispatch(tick, 1024) >= 0);
        REQUIRE(!execution_instance_state(instance, connected)->b);
        cleanup();
    }
    return true;
}

TEST_CASE(source_native_saved_numbers_preserve_values_and_defaults)
{
    const char *text =
        "behavior Numbers { state input:double=0; state number:double=0; "
        "state found:bool=false; state fallback:double=0; state "
        "missing:bool=true; "
        "on Begin { absent:=read_saved_number(Key:\"absent\",Default:19.5); "
        "missing=absent.Found; fallback=absent.Number; } "
        "on Tick { await set_saved_number(Key:\"score\",Number:input); "
        "result:=read_saved_number(Key:\"score\",Default:-1.0); "
        "found=result.Found; number=result.Number; await commit_save(); "
        "await set_saved_number(Key:\"score\",Number:42.0); "
        "await discard_save_changes(); } }";
    const double samples[] = {
        0,       -0.0,    0.1,          -12345.625,
        DBL_MAX, DBL_MIN, DBL_TRUE_MIN, 9007199254740991.0
    };
    for (int backend = 0; backend < 2; backend++) {
        save = game_save_new();
        REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        uint64_t begin = execution_module_event(prepared->module, "Begin"),
                 tick = execution_module_event(prepared->module, "Tick"),
                 input = state_id("input"), number = state_id("number"),
                 found = state_id("found"), fallback = state_id("fallback"),
                 missing = state_id("missing");
        REQUIRE(begin && tick && input && number && found && fallback && missing);
        instance = execution_program_attach(program, 0);
        REQUIRE(instance);
        retire_producer();
        REQUIRE(dispatch(begin, 1024) >= 0);
        REQUIRE(!execution_instance_state(instance, missing)->b);
        REQUIRE(execution_instance_state(instance, fallback)->d == 19.5);
        /* Invalid existing text uses the same fallback as an absent key. */
        REQUIRE(game_save_set(save, "absent", "1.25oops"));
        REQUIRE(dispatch(begin, 1024) >= 0);
        REQUIRE(!execution_instance_state(instance, missing)->b);
        REQUIRE(execution_instance_state(instance, fallback)->d == 19.5);
        for (int i = 0; i < (int)(sizeof samples / sizeof *samples); i++) {
            REQUIRE(
                execution_instance_set_state(
                    instance,
                    input,
                    (pin_value){ .type = PIN_F64, .d = samples[i] }
                ) == 0
            );
            REQUIRE(dispatch(tick, 2048) >= 0);
            REQUIRE(execution_instance_pending(instance) == 0);
            REQUIRE(execution_instance_state(instance, found)->b);
            double actual = execution_instance_state(instance, number)->d;
            REQUIRE(!memcmp(&actual, &samples[i], sizeof actual));
            REQUIRE(game_save_get_number(save, "score", &actual));
            REQUIRE(!memcmp(&actual, &samples[i], sizeof actual));
        }
        uint64_t revision = game_save_revision(save);
        const double invalid[] = { NAN, INFINITY, -INFINITY };
        for (int i = 0; i < 3; i++) {
            REQUIRE(
                execution_instance_set_state(
                    instance,
                    input,
                    (pin_value){ .type = PIN_F64, .d = invalid[i] }
                ) == 0
            );
            REQUIRE(dispatch(tick, 2048) < 0);
            REQUIRE(strstr(game_save_error(save), "finite"));
            REQUIRE(game_save_revision(save) == revision);
            REQUIRE(execution_instance_pending(instance) == 0);
            execution_program_clear_error(program);
        }
        cleanup();
    }
    return true;
}

TEST_CASE(saved_numbers_reject_malformed_text_without_mutation)
{
    const char *invalid[] = { "",       "nan",   "NaN",  "Infinity", "-inf",
                              "1e9999", "1.2x",  " 1",   "1 ",       "+1",
                              "01",     "0x1p2", "null", "true",     "[]" };
    save = game_save_new();
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        REQUIRE(game_save_set(save, "number", invalid[i]));
        double number = 17.25;
        uint64_t revision = game_save_revision(save);
        REQUIRE(!game_save_get_number(save, "number", &number));
        REQUIRE(number == 17.25 && game_save_revision(save) == revision);
    }
    REQUIRE(game_save_set(save, "number", "-1.25e2"));
    double number;
    REQUIRE(game_save_get_number(save, "number", &number) && number == -125);
    return true;
}

TEST_CASE(project_saves_reopen_isolate_names_and_respect_override)
{
    snprintf(save_directory, sizeof save_directory, "/tmp/chibi-save-XXXXXX");
    REQUIRE(mkdtemp(save_directory));
    save = game_save_new();
    const char *project = "Example/../project: Î±";
    REQUIRE(game_save_open_project(save, project, save_directory, NULL));
    REQUIRE(game_save_set_number(save, "best", 0.1));
    REQUIRE(game_save_commit(save));
    REQUIRE(game_save_set_number(save, "best", 100));
    game_save_free(save);
    save = game_save_new();
    REQUIRE(game_save_open_project(save, project, save_directory, NULL));
    double number;
    REQUIRE(game_save_get_number(save, "best", &number) && number == 0.1);
    REQUIRE(game_save_open_project(save, "Example project", save_directory, NULL));
    REQUIRE(!game_save_get(save, "best"));
    REQUIRE(game_save_set_number(save, "best", 2));
    REQUIRE(game_save_commit(save));
    char override[256];
    snprintf(override, sizeof override, "%s/selected.json", save_directory);
    REQUIRE(game_save_open_project(save, project, save_directory, override));
    REQUIRE(!game_save_get(save, "best"));
    REQUIRE(game_save_set_number(save, "best", 3));
    REQUIRE(game_save_commit(save));
    game_save_free(save);
    save = game_save_new();
    REQUIRE(game_save_open_project(save, NULL, NULL, override));
    REQUIRE(game_save_get_number(save, "best", &number) && number == 3);
    REQUIRE(game_save_open_project(save, project, save_directory, NULL));
    REQUIRE(game_save_get_number(save, "best", &number) && number == 0.1);
    REQUIRE(game_save_open_project(save, "Example project", save_directory, NULL));
    REQUIRE(game_save_get_number(save, "best", &number) && number == 2);
    REQUIRE(!game_save_open_project(save, "Example project", save_directory, ""));
    REQUIRE(!game_save_open_project(save, "Example project", NULL, NULL));
    return true;
}

void test_source_input_save()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_controller_connected_reads_live_slots);
    RUN_TEST_CASE(source_native_saved_numbers_preserve_values_and_defaults);
    RUN_TEST_CASE(saved_numbers_reject_malformed_text_without_mutation);
    RUN_TEST_CASE(project_saves_reopen_isolate_names_and_respect_override);
    pu_clear_setup();
}
