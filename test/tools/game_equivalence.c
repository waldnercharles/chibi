#include "assets/scenario_document.h"
#include "authoring/graph_catalog.h"
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
#include <unistd.h>

#define FNV_BASIS UINT64_C(14695981039346656037)
#define FNV_PRIME UINT64_C(1099511628211)
#define HARNESS "test/tools/GameEquivalence.chibi"
#define HARNESS_MILLISECONDS "50000"
#define HARNESS_ARGUMENTS 6
#define TERRAIN_LAB "test/fixtures/terrain-lab"
#define DIG_SCENARIO TERRAIN_LAB "/content/scenarios/Dig.kdl"

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

/* Plays an artifact on the default natives with the graph palette installed,
 * whose type catalog also reads the scenario's controls. */
int test_tool_game_observe(const char *artifact, const char *scenario_path, const char *trace)
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
    size_t length = 0;
    unsigned char *data = read_file(artifact, &length);
    char *text = file_read_text(scenario_path);
    scenario_document *scenario = text ? scenario_document_load(text, error, sizeof error)
                                       : NULL;
    FILE *out = data && scenario ? fopen(trace, "w") : NULL;
    if (!out) {
        fprintf(stderr, "observer cannot read %s, %s (%s) or write %s\n", artifact, scenario_path, error, trace);
    }
    bool valid = out && declarations(out, data, length) &&
                 play(native, data, length, scenario, out);
    if (out && fclose(out)) {
        valid = false;
    }
    if (out && !valid) {
        unlink(trace);
    }
    scenario_document_free(scenario);
    free(text);
    afree(data);
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

static bool observe_artifact(const char *artifact, const char *trace)
{
    char scenario[4096];
    snprintf(scenario, sizeof scenario, "%s/" DIG_SCENARIO, tool_root());
    const char *argv[] = { observer_path, "--game-observe",
                           artifact,      scenario,
                           trace,         NULL };
    return tool_run(argv, scratch, log_path) == 0;
}

/* The pluggable compilers rebuild Terrain Lab to the same declarations and
 * the same play: today both are stage zero until stage one compiles games. */
TEST_CASE(terrain_lab_plays_alike_under_both_compilers)
{
    char project[4096], scenario[4096], folder[128];
    snprintf(project, sizeof project, "%s/" TERRAIN_LAB, tool_root());
    snprintf(scenario, sizeof scenario, "%s/" DIG_SCENARIO, tool_root());
    path(folder, sizeof folder, "compare");
    const char *arguments[] = { reference_path, candidate_path, observer_path,
                                project,        scenario,       folder };
    REQUIRE(harness("Rebuild", arguments, 6) == 0);
    REQUIRE(log_has("the candidate matches the reference through frame 90"));
    return true;
}

/* A candidate whose behavior differs is named at the first frame it shows,
 * with both lines; its declarations still match. The variant stands in for a
 * candidate that miscompiles the jump. Dig.kdl presses jump from step 20, so
 * the weaker jump first shows after the 21st step. */
TEST_CASE(a_changed_behavior_is_named_at_its_first_frame)
{
    char project[4096], copy[128], explorer[256];
    snprintf(project, sizeof project, "%s/" TERRAIN_LAB, tool_root());
    path(copy, sizeof copy, "variant");
#ifdef __APPLE__
    const char *clone[] = { "cp", "-c", "-R", project, copy, NULL };
#else
    const char *clone[] = { "cp", "--reflink=auto", "-R", project, copy, NULL };
#endif
    REQUIRE(tool_run(clone, scratch, log_path) == 0);
    snprintf(explorer, sizeof explorer, "%s/content/behaviors/Explorer.chibi", copy);
    char *source = tool_read(explorer);
    REQUIRE(source);
    char *jump = strstr(source, "-175");
    if (jump) {
        memcpy(jump, "-150", 4);
        tool_write(explorer, source);
    }
    free(source);
    REQUIRE(jump);
    char reference[128], variant[128], reference_trace[128], variant_trace[128];
    path(reference, sizeof reference, "reference.chibip");
    path(variant, sizeof variant, "variant.chibip");
    path(reference_trace, sizeof reference_trace, "reference.trace");
    path(variant_trace, sizeof variant_trace, "variant.trace");
    REQUIRE(compile(project, reference) && compile(copy, variant));
    REQUIRE(
        observe_artifact(reference, reference_trace) &&
        observe_artifact(variant, variant_trace)
    );
    const char *arguments[] = { reference_trace, variant_trace };
    REQUIRE(harness("Diff", arguments, 2) == 1);
    REQUIRE(log_has("the candidate differs from the reference at frame 21"));
    REQUIRE(log_has("reference: frame 21 ") && log_has("candidate: frame 21 "));
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
    tool_write(reference, "declarations 0000000000000001\nmodule Explorer 1\nframe 0 ecs 1\n");
    tool_write(candidate, "declarations 0000000000000002\nmodule Explorer 1\nframe 0 ecs 1\n");
    const char *declared[] = { reference, candidate };
    REQUIRE(harness("Diff", declared, 2) == 1);
    REQUIRE(log_has("the candidate declares differently from the reference"));
    REQUIRE(log_has("reference: declarations 0000000000000001"));
    const char *absent[] = { missing, missing };
    REQUIRE(harness("Diff", absent, 2) == 2);
    REQUIRE(log_has("is not a trace"));
    tool_write(reference, "declarations 0000000000000001\nmodule Explorer 1\n");
    const char *unplayed[] = { reference, reference };
    REQUIRE(harness("Diff", unplayed, 2) == 2);
    REQUIRE(log_has("the traces hold no frames"));
    return true;
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
    snprintf(scratch, sizeof scratch, "/tmp/chibi-equivalence-XXXXXX");
    char *made = mkdtemp(scratch);
    assert(made);
    char *resolved = realpath(made, NULL);
    assert(resolved && strlen(resolved) < sizeof scratch);
    snprintf(scratch, sizeof scratch, "%s", resolved);
    free(resolved);
    path(log_path, sizeof log_path, "harness.log");
    RUN_TEST_CASE(terrain_lab_plays_alike_under_both_compilers);
    RUN_TEST_CASE(a_changed_behavior_is_named_at_its_first_frame);
    RUN_TEST_CASE(declarations_and_missing_traces_are_refused);
    tool_remove_tree(scratch);
}
