#include "assets/scenario_document.h"
#include "authoring/graph_catalog.h"
#include "authoring/project_compile.h"
#include "authoring/project_prepare.h"
#include "execution/module_internal.h"
#include "execution/prepared.h"
#include "host/native_default.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "runtime/native_prepared.h"
#include "runtime/player_prepared.h"
#include "support/array.h"
#include "support/file_io.h"
#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yyjson.h>

#define FNV_BASIS UINT64_C(14695981039346656037)
#define FNV_PRIME UINT64_C(1099511628211)
#define HARNESS "test/tools/GameEquivalence.chibi"
#define HARNESS_MILLISECONDS "50000"
#define STORIES "test/stories"
#define SCENARIO "/content/scenarios/Story.kdl"
#define STORY_COUNT 5
#define HARNESS_ARGUMENTS (5 + 2 * STORY_COUNT)
#define STAGE_ONE "tools/toolchain/StageOne.chibi"
/* The stories stage one compiles so far, from the first. */
#define STAGE_ONE_STORIES 4

/* The stories, in the order the stage-one increments reach them. */
static const char *const story_names[STORY_COUNT] = { "Counter",
                                                      "Motion",
                                                      "Controls",
                                                      "Results",
                                                      "Follow" };

static const char *observer_path, *build_host_path, *reference_path, *candidate_path;
static char scratch[64], log_path[128];

static uint64_t digest(uint64_t hash, const void *data, size_t length)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < length; i++) {
        hash = (hash ^ bytes[i]) * FNV_PRIME;
    }
    return hash;
}

static unsigned char *read_file(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    unsigned char *data = NULL;
    unsigned char chunk[65536];
    size_t count;
    while ((count = fread(chunk, 1, sizeof chunk, file))) {
        int at = acount(data);
        asetlen(data, at + (int)count);
        memcpy(data + at, chunk, count);
    }
    fclose(file);
    *length = (size_t)acount(data);
    return data;
}

/* What an artifact declares apart from its code: every byte outside its
 * module records, and each module's name and ID in order. */
static bool declarations(FILE *out, const unsigned char *data, size_t length)
{
    arena names = { 0 };
    package_module *modules = NULL;
    size_t first, end;
    char error[256];
    bool valid = player_prepared_modules(data, length, &names, &modules, &first, &end, error, sizeof error);
    if (!valid) {
        fprintf(stderr, "declarations: %s\n", error);
    } else {
        uint64_t rest = digest(digest(FNV_BASIS, data, first), data + end, length - end);
        fprintf(out, "declarations %016" PRIx64 "\n", rest);
        for (int i = 0; i < acount(modules); i++) {
            fprintf(
                out,
                "module %s %016" PRIx64 "\n",
                modules[i].name,
                modules[i].id
            );
        }
    }
    afree(modules);
    arena_free(&names);
    return valid;
}

/* The authored UI text, which hashes of the world do not see. */
static uint64_t ui_text(ui *ui)
{
    uint64_t hash = FNV_BASIS;
    for (int i = 0; ui && i < ui_element_count(ui); i++) {
        const ui_element *element = ui_element_at(ui, i);
        hash = digest(hash, element->selector, strlen(element->selector) + 1);
        if (element->text) {
            hash = digest(hash, element->text, strlen(element->text) + 1);
        }
    }
    return hash;
}

/* One line of what a frame left observable to the player and its saves. */
static void observe(FILE *out, int frame, world *world)
{
    const engine_modules *modules = world_modules(world);
    engine *engine = world_engine(world);
    char *saved = modules->save ? save_export(modules->save) : xstrdup("");
    physics_hashes physics = modules->physics ? physics_hash(modules->physics)
                                              : (physics_hashes){ 0 };
    uint64_t materials = materials_enabled(engine_materials(engine))
                           ? materials_hash_parts(engine_materials(engine)).hash
                           : 0;
    fprintf(
        out,
        "frame %d ecs %016" PRIx64 " saved %016" PRIx64 " materials %016" PRIx64
        " bodies %016" PRIx64 " contacts %016" PRIx64 " events %016" PRIx64
        " ui %016" PRIx64 "\n",
        frame,
        ecs_world_hash(world_ecs(world)),
        digest(FNV_BASIS, saved, strlen(saved)),
        materials,
        physics.body_state,
        physics.contact_pairs,
        physics.contact_events,
        ui_text(modules->ui)
    );
    free(saved);
}

/* The CPU host keeps no graphics handles; headless worlds keep UI state. */
static void *stage_cpu_resources(void *, const player_asset *, int, size_t *, char *, size_t)
{
    static int resources;
    return &resources;
}
static bool cpu_scene(void *, void *, const scene *, char *, size_t)
{
    return true;
}
static void commit_cpu_resources(void *, void *, world *) {}
static void release_cpu_resources(void *, void *) {}

/* Plays the artifact from its start with the scenario's input. Frame N is
 * the world after N fixed steps, so input keyed at step K first shows in
 * frame K + 1. */
static bool play(
    native_capabilities *native,
    const unsigned char *data,
    size_t length,
    const scenario_document *scenario,
    FILE *out
)
{
    char error[512] = { 0 };
    native_prepared_context context = { .native = native };
    execution_prepared_linker linker = native_capabilities_prepared_linker(&context);
    player_host host = {
        .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT },
        .linker = &linker,
        .resources = { .stage = stage_cpu_resources,
                       .scene = cpu_scene,
                       .commit = commit_cpu_resources,
                       .discard = release_cpu_resources,
                       .retire = release_cpu_resources },
    };
    player *runtime = player_prepared_read(data, length, &host, behavior_default_backend(), error, sizeof error);
    bool valid = runtime != NULL;
    const char *start = runtime
                          ? scene_name(scene_instance_scene(player_scene(runtime)))
                          : NULL;
    if (valid && strcmp(start, scenario->scene)) {
        valid = player_load_scene(runtime, scenario->scene, error, sizeof error);
    }
    engine *engine = runtime ? world_engine(player_world(runtime)) : NULL;
    valid = valid && engine_set_focus(engine, ENGINE_FOCUS_VIEWPORT) == 0 &&
            engine_set_playing(engine, true) == 0;
    if (valid) {
        engine_replay_input(
            engine,
            scenario->keys,
            acount(scenario->keys),
            scenario->seed
        );
    }
    for (int frame = 0; valid && frame <= scenario->frames; frame++) {
        valid = engine_frame(engine, (frame + .5) * ENGINE_FIXED_SECONDS) == 0;
        if (valid) {
            observe(out, frame, player_world(runtime));
        }
    }
    if (!valid) {
        fprintf(stderr, "observer: %s\n", engine && engine_error(engine) ? engine_error(engine) : error);
    }
    if (runtime) {
        player_free(runtime);
    }
    return valid;
}

/* The name a trace gives a played artifact: its file name without folder or
 * extension, so artifacts of one story from two compilers share it. */
static void story_name(const char *artifact, char *out, size_t size)
{
    const char *slash = strrchr(artifact, '/');
    const char *name = slash ? slash + 1 : artifact;
    const char *dot = strrchr(name, '.');
    size_t length = dot ? (size_t)(dot - name) : strlen(name);
    snprintf(out, size, "%.*s", (int)length, name);
}

/* Writes one story's section of the trace: its name, what its artifact
 * declares, then one line per fixed step of its scenario. */
static bool observe_story(
    native_capabilities *native,
    const char *artifact,
    const char *scenario_path,
    FILE *out
)
{
    char error[512] = { 0 }, name[256];
    size_t length = 0;
    unsigned char *data = read_file(artifact, &length);
    char *text = file_read_text(scenario_path);
    scenario_document *scenario = text ? scenario_document_load(text, error, sizeof error)
                                       : NULL;
    if (!data || !scenario) {
        fprintf(stderr, "observer cannot read %s or %s (%s)\n", artifact, scenario_path, error);
    }
    story_name(artifact, name, sizeof name);
    bool valid = data && scenario && fprintf(out, "play %s\n", name) > 0 &&
                 declarations(out, data, length) &&
                 play(native, data, length, scenario, out);
    scenario_document_free(scenario);
    free(text);
    afree(data);
    return valid;
}

/* Plays each artifact under its scenario on the default natives, with the
 * graph palette installed so the type catalog reads the scenarios' controls.
 * The trace is removed unless every story played. */
int test_tool_game_observe(const char *trace, const char *const *stories, int count)
{
    char error[512] = { 0 };
    native_capabilities *native = host_native_default_catalog_new(NULL, 0, NULL, 0, error, sizeof error);
    authoring_graph_catalog *palette = native
                                         ? authoring_graph_catalog_new(
                                               (authoring_graph_catalog_config){
                                                   .native = native,
                                                   .types = TYPE_PACK,
                                                   .components = { REGISTRY_COMPONENTS,
                                                                   REGISTRY_COMPONENT_COUNT },
                                               },
                                               error,
                                               sizeof error
                                           )
                                         : NULL;
    if (!palette) {
        fprintf(stderr, "observer catalog: %s\n", error);
        native_capabilities_free(native);
        return 1;
    }
    int pack_count;
    const registry_pack *const *packs = authoring_graph_catalog_packs(palette, &pack_count);
    registry_set_packs(packs, pack_count);
    FILE *out = fopen(trace, "w");
    if (!out) {
        fprintf(stderr, "observer cannot write %s\n", trace);
    }
    bool valid = out != NULL;
    for (int i = 0; valid && i < count; i++) {
        valid = observe_story(native, stories[2 * i], stories[2 * i + 1], out);
    }
    if (out && fclose(out)) {
        valid = false;
    }
    if (out && !valid) {
        unlink(trace);
    }
    registry_set_packs(NULL, 0);
    authoring_graph_catalog_free(palette);
    native_capabilities_free(native);
    return valid ? 0 : 1;
}

static void path(char *out, size_t size, const char *name)
{
    int n = snprintf(out, size, "%s/%s", scratch, name);
    assert(n > 0 && (size_t)n < size);
}

static void story_path(char *out, size_t size, const char *story, const char *file)
{
    int n = snprintf(out, size, "%s/" STORIES "/%s%s", tool_root(), story, file);
    assert(n > 0 && (size_t)n < size);
}

static bool log_has(const char *text)
{
    char *output = tool_read(log_path);
    bool found = output && strstr(output, text);
    if (!found) {
        fprintf(stderr, "expected \"%s\" in:\n%s\n", text, output ? output : "(no log)");
    }
    free(output);
    return found;
}

/* Runs one entry of the Chibi harness with the arguments after it. */
static int harness(const char *entry, const char *const *arguments, int count)
{
    char root[4096], source[4096];
    snprintf(root, sizeof root, "%s", tool_root());
    snprintf(source, sizeof source, "%s/" HARNESS, root);
    const char *argv[HARNESS_ARGUMENTS + 11] = {
        build_host_path, "--root", root,           "--source",           source,
        "--entry",       entry,    "--timeout-ms", HARNESS_MILLISECONDS, "--"
    };
    assert(count <= HARNESS_ARGUMENTS);
    int at = 10;
    for (int i = 0; i < count; i++) {
        argv[at++] = arguments[i];
    }
    argv[at] = NULL;
    return tool_run_within(argv, root, log_path, 55);
}

static bool compile(const char *project, const char *output)
{
    const char *argv[] = { reference_path, "--project-root", project,
                           "--output",     output,           NULL };
    return tool_run(argv, scratch, log_path) == 0;
}

static bool observe_motion(const char *artifact, const char *trace)
{
    char scenario[4096];
    story_path(scenario, sizeof scenario, "Motion", SCENARIO);
    const char *argv[] = { observer_path, "--game-observe", trace,
                           artifact,      scenario,         NULL };
    return tool_run(argv, scratch, log_path) == 0;
}

/* The pluggable compilers rebuild every story to the same declarations and
 * the same play: today both are stage zero until stage one compiles them. */
TEST_CASE(stories_play_alike_under_both_compilers)
{
    char folder[128], projects[STORY_COUNT][4096], scenarios[STORY_COUNT][4096];
    path(folder, sizeof folder, "compare");
    const char *arguments[HARNESS_ARGUMENTS] = { build_host_path,
                                                 reference_path,
                                                 candidate_path,
                                                 observer_path,
                                                 folder };
    int count = 5;
    for (int i = 0; i < STORY_COUNT; i++) {
        story_path(projects[i], sizeof projects[i], story_names[i], "");
        story_path(scenarios[i], sizeof scenarios[i], story_names[i], SCENARIO);
        arguments[count++] = projects[i];
        arguments[count++] = scenarios[i];
    }
    REQUIRE(harness("Rebuild", arguments, count) == 0);
    REQUIRE(
        log_has("the candidate matches the reference in Counter, Motion, Controls, Results, Follow")
    );
    return true;
}

/* A candidate whose behavior differs is named in its story at the first
 * frame it shows, with both lines; its declarations still match. The variant
 * stands in for a candidate that miscompiles Motion's drift, which first
 * shows after the first step. */
TEST_CASE(a_changed_behavior_is_named_at_its_first_frame)
{
    char project[4096], copy[128], drift[256];
    story_path(project, sizeof project, "Motion", "");
    path(copy, sizeof copy, "Motion");
#ifdef __APPLE__
    const char *clone[] = { "cp", "-c", "-R", project, copy, NULL };
#else
    const char *clone[] = { "cp", "--reflink=auto", "-R", project, copy, NULL };
#endif
    REQUIRE(tool_run(clone, scratch, log_path) == 0);
    snprintf(drift, sizeof drift, "%s/content/behaviors/Drift.chibi", copy);
    char *source = tool_read(drift);
    REQUIRE(source);
    char *step = strstr(source, "* dt");
    char *edited = NULL;
    if (step) {
        edited = xmalloc(strlen(source) + 5);
        snprintf(edited, strlen(source) + 5, "%.*s* dt * 2%s", (int)(step - source), source, step + 4);
        tool_write(drift, edited);
    }
    free(edited);
    free(source);
    REQUIRE(step);
    const char *folders[] = { "reference", "variant" };
    char artifacts[2][128], traces[2][128];
    for (int i = 0; i < 2; i++) {
        char folder[128];
        path(folder, sizeof folder, folders[i]);
        REQUIRE(!mkdir(folder, 0700));
        snprintf(artifacts[i], sizeof artifacts[i], "%s/Motion.chibip", folder);
        snprintf(traces[i], sizeof traces[i], "%s.trace", folder);
    }
    REQUIRE(compile(project, artifacts[0]) && compile(copy, artifacts[1]));
    REQUIRE(
        observe_motion(artifacts[0], traces[0]) &&
        observe_motion(artifacts[1], traces[1])
    );
    const char *arguments[] = { traces[0], traces[1] };
    REQUIRE(harness("Diff", arguments, 2) == 1);
    REQUIRE(log_has("the candidate differs from the reference in Motion at frame 1"));
    REQUIRE(log_has("reference: frame 1 ") && log_has("candidate: frame 1 "));
    return true;
}

/* Declarations are compared before play. A missing trace, or traces with
 * no frames, are refused rather than matching each other. */
TEST_CASE(declarations_and_missing_traces_are_refused)
{
    char reference[128], candidate[128], missing[128];
    path(reference, sizeof reference, "declared.trace");
    path(candidate, sizeof candidate, "redeclared.trace");
    path(missing, sizeof missing, "missing.trace");
    tool_write(reference, "play Motion\ndeclarations 0000000000000001\nmodule Drift 1\nframe 0 ecs 1\n");
    tool_write(candidate, "play Motion\ndeclarations 0000000000000002\nmodule Drift 1\nframe 0 ecs 1\n");
    const char *declared[] = { reference, candidate };
    REQUIRE(harness("Diff", declared, 2) == 1);
    REQUIRE(log_has("the candidate declares Motion differently from the reference"));
    REQUIRE(log_has("reference: declarations 0000000000000001"));
    const char *absent[] = { missing, missing };
    REQUIRE(harness("Diff", absent, 2) == 2);
    REQUIRE(log_has("is not a trace"));
    tool_write(reference, "play Motion\ndeclarations 0000000000000001\nmodule Drift 1\n");
    const char *unplayed[] = { reference, reference };
    REQUIRE(harness("Diff", unplayed, 2) == 2);
    REQUIRE(log_has("the traces hold no frames for some story"));
    return true;
}

/* Assembling a story from the modules of its own artifact, as a stage-one
 * route will supply modules it compiled, writes that artifact again; a source
 * without its module, or a module of no source, is refused. */
static bool assembles_from_its_own_modules(const char *story)
{
    char project[4096], error[512] = { 0 };
    story_path(project, sizeof project, story, "");
    authoring_application application;
    REQUIRE(authoring_application_begin(&application, error, sizeof error));
    authoring_project_options options = { .project = project,
                                          .native = application.native,
                                          .components = { REGISTRY_COMPONENTS,
                                                          REGISTRY_COMPONENT_COUNT },
                                          .modules = WORLD_MODULES_ALL,
                                          .read_only = true };
    authoring_project_artifact prepared = { 0 }, assembled = { 0 }, refused = { 0 };
    bool valid = authoring_project_prepare(&options, &prepared, error, sizeof error);
    arena names = { 0 };
    package_module *modules = NULL;
    size_t first, end;
    valid = valid && player_prepared_modules(
                         prepared.data,
                         prepared.size,
                         &names,
                         &modules,
                         &first,
                         &end,
                         error,
                         sizeof error
                     );
    int count = acount(modules);
    valid = valid && count > 0 &&
            authoring_project_assemble(&options, modules, count, &assembled, error, sizeof error);
    if (!valid) {
        fprintf(stderr, "%s assembly: %s\n", story, error);
    }
    bool same = valid && assembled.size == prepared.size &&
                !memcmp(assembled.data, prepared.data, prepared.size) &&
                !strcmp(assembled.manifest, prepared.manifest);
    bool missing = valid &&
                   !authoring_project_assemble(&options, modules, count - 1, &refused, error, sizeof error) &&
                   strstr(error, "no prepared module was supplied");
    package_module stray = modules ? modules[0] : (package_module){ 0 };
    stray.id ^= 1;
    if (valid) {
        apush(modules, stray);
    }
    bool extra = valid &&
                 !authoring_project_assemble(&options, modules, count + 1, &refused, error, sizeof error) &&
                 strstr(error, "belongs to no project source");
    afree(modules);
    arena_free(&names);
    authoring_project_artifact_free(&prepared);
    authoring_project_artifact_free(&assembled);
    authoring_application_end(&application);
    REQUIRE(same && missing && extra && !refused.data);
    return true;
}
/* Positions of CompileBehaviors' arguments in the exported input: the
 * behaviors' identities and sources, the six target arguments, then the
 * game's. */
enum
{
    INPUT_IDENTITIES = 0,
    INPUT_SOURCES,
    INPUT_NATIVES = 5,
    INPUT_EVENTS = 8,
    INPUT_EVENT_FIELDS,
    INPUT_TYPES,
    INPUT_TYPE_FIELDS,
    INPUT_ENTITY,
    INPUT_OPERATORS,
    INPUT_GLOBALS,
    INPUT_ARGUMENTS
};
/* Whether the exported input holds the story's behavior and lists the type
 * requirements and events of the module stage zero prepared for it, in
 * order, as a stage-one module must, the binary operators of the natives
 * stage zero calls for records, + among them, and the globals, `self` of the
 * owner's type among them. */
static bool target_matches(yyjson_val *root, const execution_module *module, const char *source)
{
    const execution_module_plan *plan = &module->plan;
    REQUIRE(yyjson_arr_size(root) == INPUT_ARGUMENTS);
    yyjson_val *identities = yyjson_arr_get(root, INPUT_IDENTITIES);
    yyjson_val *sources = yyjson_arr_get(root, INPUT_SOURCES);
    REQUIRE(yyjson_arr_size(identities) == 1 && yyjson_arr_size(sources) == 1);
    REQUIRE(!strcmp(yyjson_get_str(yyjson_arr_get(identities, 0)), "behaviors/Counter.chibi"));
    REQUIRE(source && !strcmp(yyjson_get_str(yyjson_arr_get(sources, 0)), source));
    yyjson_val *types = yyjson_arr_get(root, INPUT_TYPES);
    REQUIRE(yyjson_arr_size(types) == (size_t)plan->type_count);
    for (int i = 0; i < plan->type_count; i++) {
        yyjson_val *type = yyjson_arr_get(types, (size_t)i);
        const execution_type_requirement *expected = &plan->types[i];
        REQUIRE(yyjson_get_uint(yyjson_obj_get(type, "id")) == expected->id);
        REQUIRE(yyjson_get_uint(yyjson_obj_get(type, "layout")) == expected->layout);
        REQUIRE(yyjson_get_int(yyjson_obj_get(type, "storage")) == (int)expected->storage);
        REQUIRE(yyjson_get_uint(yyjson_obj_get(type, "size")) == expected->size);
        REQUIRE(!strcmp(yyjson_get_str(yyjson_obj_get(type, "name")), expected->name));
    }
    yyjson_val *events = yyjson_arr_get(root, INPUT_EVENTS);
    yyjson_val *fields = yyjson_arr_get(root, INPUT_EVENT_FIELDS);
    for (int i = 0; i < plan->event_count; i++) {
        const execution_module_event_declaration *expected = &plan->events[i];
        yyjson_val *event = NULL;
        for (size_t j = 0; !event && j < yyjson_arr_size(events); j++) {
            yyjson_val *candidate = yyjson_arr_get(events, j);
            event = yyjson_get_uint(yyjson_obj_get(candidate, "id")) == expected->id
                      ? candidate
                      : NULL;
        }
        REQUIRE(event);
        REQUIRE(yyjson_get_uint(yyjson_obj_get(event, "abi")) == expected->abi);
        REQUIRE(!strcmp(yyjson_get_str(yyjson_obj_get(event, "name")), expected->name));
        REQUIRE(yyjson_get_int(yyjson_obj_get(event, "count")) == expected->field_count);
        size_t first = yyjson_get_uint(yyjson_obj_get(event, "first"));
        for (int j = 0; j < expected->field_count; j++) {
            yyjson_val *field = yyjson_arr_get(fields, first + (size_t)j);
            REQUIRE(!strcmp(
                yyjson_get_str(yyjson_obj_get(field, "name")),
                expected->fields[j].name
            ));
            REQUIRE(
                yyjson_get_int(yyjson_obj_get(field, "storage")) ==
                (int)expected->fields[j].initial.type
            );
            REQUIRE(
                yyjson_get_uint(yyjson_obj_get(field, "nominal")) ==
                expected->fields[j].nominal
            );
        }
    }
    REQUIRE(yyjson_get_uint(yyjson_arr_get(root, INPUT_ENTITY)) == plan->owner_type);
    yyjson_val *natives = yyjson_arr_get(root, INPUT_NATIVES);
    yyjson_val *operators = yyjson_arr_get(root, INPUT_OPERATORS);
    bool add = false;
    for (size_t i = 0; i < yyjson_arr_size(operators); i++) {
        yyjson_val *row = yyjson_arr_get(operators, i);
        yyjson_val *native = yyjson_arr_get(natives, yyjson_get_uint(yyjson_obj_get(row, "native")));
        REQUIRE(native && yyjson_get_int(yyjson_obj_get(native, "parameters")) == 2);
        REQUIRE(yyjson_get_int(yyjson_obj_get(native, "results")) == 1);
        add |= !strcmp(yyjson_get_str(yyjson_obj_get(row, "operator")), "+") &&
               !strcmp(yyjson_get_str(yyjson_obj_get(native, "name")), "add");
    }
    REQUIRE(add);
    yyjson_val *globals = yyjson_arr_get(root, INPUT_GLOBALS);
    bool owner = false;
    for (size_t i = 0; i < yyjson_arr_size(globals); i++) {
        yyjson_val *global = yyjson_arr_get(globals, i);
        yyjson_val *type = yyjson_arr_get(types, yyjson_get_uint(yyjson_obj_get(global, "record")));
        owner |= !strcmp(yyjson_get_str(yyjson_obj_get(global, "name")), "self") &&
                 type &&
                 yyjson_get_uint(yyjson_obj_get(type, "id")) == plan->owner_type;
    }
    REQUIRE(owner);
    return true;
}
/* chibi-compile --stage-one-input exports stage one's input for a story: its
 * behavior's identity and source, and what the behavior links against, the
 * same type requirements, events and entity type that stage zero writes into
 * the behavior's module. */
TEST_CASE(stage_one_input_lists_what_stage_zero_modules_require)
{
    char project[4096], target[4096], behavior[4096], error[512] = { 0 };
    story_path(project, sizeof project, "Counter", "");
    story_path(behavior, sizeof behavior, "Counter", "/content/behaviors/Counter.chibi");
    path(target, sizeof target, "counter-input.json");
    const char *argv[] = { reference_path,      "--project-root", project,
                           "--stage-one-input", target,           NULL };
    REQUIRE(tool_run(argv, scratch, log_path) == 0);
    yyjson_doc *doc = yyjson_read_file(target, 0, NULL, NULL);
    REQUIRE(doc);
    authoring_application application;
    REQUIRE(authoring_application_begin(&application, error, sizeof error));
    authoring_project_options options = { .project = project,
                                          .native = application.native,
                                          .components = { REGISTRY_COMPONENTS,
                                                          REGISTRY_COMPONENT_COUNT },
                                          .modules = WORLD_MODULES_ALL,
                                          .read_only = true };
    authoring_project_artifact artifact = { 0 };
    arena names = { 0 };
    package_module *modules = NULL;
    size_t first, end;
    bool prepared = authoring_project_prepare(&options, &artifact, error, sizeof error) &&
                    player_prepared_modules(
                        artifact.data,
                        artifact.size,
                        &names,
                        &modules,
                        &first,
                        &end,
                        error,
                        sizeof error
                    ) &&
                    acount(modules) == 1;
    native_prepared_context context = { .native = application.native };
    execution_prepared_linker linker = native_capabilities_prepared_linker(&context);
    execution_module *module = prepared ? execution_prepared_read(
                                              modules[0].data,
                                              modules[0].length,
                                              &linker,
                                              BEHAVIOR_BYTECODE,
                                              error,
                                              sizeof error
                                          )
                                        : NULL;
    if (!module) {
        fprintf(stderr, "stage-one input: %s\n", error);
    }
    char *source = tool_read(behavior);
    bool matches = module && target_matches(yyjson_doc_get_root(doc), module, source);
    free(source);
    execution_module_free(module);
    afree(modules);
    arena_free(&names);
    authoring_project_artifact_free(&artifact);
    authoring_application_end(&application);
    yyjson_doc_free(doc);
    REQUIRE(matches);
    return true;
}
/* chibi-compile --modules refuses a file that is no closure of modules and
 * writes no artifact. */
TEST_CASE(assembly_refuses_what_is_no_closure)
{
    char project[4096], closure[128], output[128];
    story_path(project, sizeof project, "Counter", "");
    path(closure, sizeof closure, "no-closure.chibip");
    path(output, sizeof output, "refused.chibip");
    tool_write(closure, "not a closure of modules");
    const char *argv[] = { reference_path, "--project-root",
                           project,        "--modules",
                           closure,        "--output",
                           output,         NULL };
    REQUIRE(tool_run(argv, scratch, log_path) == 1);
    REQUIRE(log_has("invalid compiler artifact header") && access(output, F_OK) != 0);
    return true;
}
TEST_CASE(stories_assemble_from_their_own_modules)
{
    for (int i = 0; i < STORY_COUNT; i++) {
        REQUIRE(assembles_from_its_own_modules(story_names[i]));
    }
    return true;
}
/* Stage one's route rebuilds the first STAGE_ONE_STORIES stories as stage zero
 * does: it recovers stage one from the bootstrap artifact and the compiler
 * sources, compiles each story's behaviors with it, and has chibi-compile
 * assemble the artifact from those modules. Recovery takes one or two compiles
 * of the compiler the first time its sources change, and none once its
 * closure is kept. */
TEST_CASE(stage_one_rebuilds_stories_as_stage_zero_does)
{
    char folder[128], route[4096], projects[STAGE_ONE_STORIES][4096],
        scenarios[STAGE_ONE_STORIES][4096];
    path(folder, sizeof folder, "stage-one");
    snprintf(route, sizeof route, "%s/" STAGE_ONE, tool_root());
    const char *arguments[5 + 2 * STAGE_ONE_STORIES] = { build_host_path,
                                                         reference_path,
                                                         route,
                                                         observer_path,
                                                         folder };
    for (int i = 0; i < STAGE_ONE_STORIES; i++) {
        story_path(projects[i], sizeof projects[i], story_names[i], "");
        story_path(scenarios[i], sizeof scenarios[i], story_names[i], SCENARIO);
        arguments[5 + 2 * i] = projects[i];
        arguments[6 + 2 * i] = scenarios[i];
    }
    REQUIRE(harness("Rebuild", arguments, 5 + 2 * STAGE_ONE_STORIES) == 0);
    REQUIRE(log_has("the candidate matches the reference in Counter, Motion, Controls, Results"));
    return true;
}
static void prepare_scratch()
{
    snprintf(scratch, sizeof scratch, "/tmp/chibi-equivalence-XXXXXX");
    char *made = mkdtemp(scratch);
    assert(made);
    char *resolved = realpath(made, NULL);
    assert(resolved && strlen(resolved) < sizeof scratch);
    snprintf(scratch, sizeof scratch, "%s", resolved);
    free(resolved);
    path(log_path, sizeof log_path, "harness.log");
}
void test_tool_game_equivalence_stage_one(
    const char *observer,
    const char *build_host,
    const char *reference,
    const char *stage_one_host
)
{
    observer_path = observer;
    build_host_path = build_host;
    reference_path = reference;
    setenv("CHIBI_COMPILE", reference, 1);
    setenv("CHIBI_STAGE_ONE_HOST", stage_one_host, 1);
    prepare_scratch();
    RUN_TEST_CASE(stage_one_rebuilds_stories_as_stage_zero_does);
    tool_remove_tree(scratch);
}
void test_tool_game_equivalence(
    const char *observer,
    const char *build_host,
    const char *reference,
    const char *candidate
)
{
    observer_path = observer;
    build_host_path = build_host;
    reference_path = reference;
    candidate_path = candidate;
    prepare_scratch();
    RUN_TEST_CASE(stories_assemble_from_their_own_modules);
    RUN_TEST_CASE(assembly_refuses_what_is_no_closure);
    RUN_TEST_CASE(stage_one_input_lists_what_stage_zero_modules_require);
    RUN_TEST_CASE(stories_play_alike_under_both_compilers);
    RUN_TEST_CASE(a_changed_behavior_is_named_at_its_first_frame);
    RUN_TEST_CASE(declarations_and_missing_traces_are_refused);
    tool_remove_tree(scratch);
}
