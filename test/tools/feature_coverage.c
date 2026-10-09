/* Feature coverage: compares the engine catalog (components, native functions
 * and events) with what one project's scripts, prototypes and scenes use, and
 * runs each of the project's scenes for a few frames through run_scenario. */
#include "assets/prototype.h"
#include "assets/scene_document.h"
#include "authoring/application_catalog.h"
#include "authoring/language_catalog.h"
#include "authoring/source_catalog.h"
#include "runtime/component_catalog.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

enum
{
    SCENARIO_FRAMES = 4,
    MAX_PATH = 4096,
    MAX_LINE = 256,
};

typedef enum
{
    ENTRY_COMPONENT,
    ENTRY_FUNCTION,
    ENTRY_EVENT,
    ENTRY_KIND_COUNT,
} entry_kind;

static const char *const KIND_NAMES[ENTRY_KIND_COUNT] = { "component", "function", "event" };

typedef struct
{
    entry_kind kind;
    const char *name; /* Borrowed from the catalog. */
    uint64_t id; /* Catalog type ID for components, capability ID otherwise. */
    bool used, allowed;
} entry;

typedef struct
{
    char *name;
    char **behaviors;
} prototype_entry;

static char project_root[PATH_MAX];
static char scratch[] = "/tmp/chibi-feature-coverage-XXXXXX";
static authoring_language_catalog *native;
static source_catalog *project;
static entry *entries;
static int problems; /* Reported failures; the coverage case fails on any. */
static prototype_entry *prototypes;
/* Names a scene reaches: its entities' prototypes and behaviors, their
 * prototypes' behaviors, and the prototypes those behaviors spawn. */
static char **reached_prototypes, **reached_behaviors;

/* Language library categories; their functions are not engine features. */
static const char *const LANGUAGE_CATEGORIES[] = { "Array", "Set", "Flow Control", "Conversion" };

static bool language_category(const char *category)
{
    for (size_t i = 0; i < sizeof LANGUAGE_CATEGORIES / sizeof *LANGUAGE_CATEGORIES;
         i++) {
        if (!strcmp(category, LANGUAGE_CATEGORIES[i])) {
            return true;
        }
    }
    return false;
}

/* Feature functions exclude generated field, struct, flow, state and operator
 * forms and the language library; those follow from the components and the
 * language themselves. */
static bool feature_function(const script_catalog_capability *capability)
{
    return (capability->kind == SCRIPT_CAPABILITY_FUNCTION ||
            capability->kind == SCRIPT_CAPABILITY_ACTION) &&
           capability->component_access == SCRIPT_COMPONENT_ACCESS_NONE &&
           capability->struct_access == SCRIPT_STRUCT_ACCESS_NONE &&
           capability->flow.kind == SCRIPT_FLOW_ACCESS_NONE &&
           capability->state_access == SCRIPT_STATE_ACCESS_NONE &&
           !capability->operator_name && capability->category &&
           !language_category(capability->category);
}

static void add_catalog(const script_catalog *engine, const script_catalog *merged)
{
    for (int i = 0; i < merged->type_count; i++) {
        const script_catalog_type *type = &merged->types[i];
        if (type->component) {
            apush(entries, ((entry){ .kind = ENTRY_COMPONENT, .name = type->name, .id = type->id }));
        }
    }
    for (int i = 0; i < engine->capability_count; i++) {
        const script_catalog_capability *capability = &engine->capabilities[i];
        entry_kind kind = capability->kind == SCRIPT_CAPABILITY_EVENT
                            ? ENTRY_EVENT
                            : ENTRY_FUNCTION;
        if (kind == ENTRY_FUNCTION && !feature_function(capability)) {
            continue;
        }
        apush(
            entries,
            ((entry){ .kind = kind,
                      .name = capability->qualified_name,
                      .id = capability->id })
        );
    }
}

static entry *find_id(entry_kind kind, uint64_t id)
{
    for (int i = 0; i < acount(entries); i++) {
        if (entries[i].kind == kind && entries[i].id == id) {
            return &entries[i];
        }
    }
    return NULL;
}

static entry *find_name(entry_kind kind, const char *name)
{
    for (int i = 0; i < acount(entries); i++) {
        if (entries[i].kind == kind && !strcmp(entries[i].name, name)) {
            return &entries[i];
        }
    }
    return NULL;
}

static void use_id(entry_kind kind, uint64_t id)
{
    entry *found = id ? find_id(kind, id) : NULL;
    if (found) {
        found->used = true;
    }
}

static void use_component_name(const char *name)
{
    const char *prefix = "GAME.";
    if (!strncmp(name, prefix, strlen(prefix))) {
        name += strlen(prefix);
    }
    entry *found = find_name(ENTRY_COMPONENT, name);
    if (found) {
        found->used = true;
    }
}

static void use_capability(const script_catalog *catalog, uint64_t id)
{
    const script_catalog_capability *capability = id ? script_catalog_capability_by_id(catalog, id)
                                                     : NULL;
    if (!capability) {
        return;
    }
    use_id(capability->kind == SCRIPT_CAPABILITY_EVENT ? ENTRY_EVENT : ENTRY_FUNCTION, id);
    if (capability->component_access != SCRIPT_COMPONENT_ACCESS_NONE) {
        use_id(ENTRY_COMPONENT, capability->component);
    }
}

static void use_analysis(const script_catalog *catalog, const script_analysis *analysis)
{
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *expression = &analysis->expressions[i];
        if (expression->kind != SCRIPT_EXPR_COMPONENT) { /* Its capability is a type ID. */
            use_capability(catalog, expression->capability);
        }
        use_id(ENTRY_COMPONENT, expression->type.nominal);
    }
    /* Every catalog name is a symbol in scope; only its uses count. */
    for (int i = 0; i < analysis->use_count; i++) {
        const script_use *use = &analysis->uses[i];
        if (use->symbol < 0) {
            use_capability(catalog, use->member); /* A handler names its event directly. */
            continue;
        }
        const script_symbol *symbol = &analysis->symbols[use->symbol];
        use_capability(catalog, symbol->capability);
        if (symbol->kind == SCRIPT_COMPONENT) {
            use_id(ENTRY_COMPONENT, symbol->type.nominal);
        }
    }
    for (int i = 0; i < analysis->function_count; i++) {
        use_id(ENTRY_EVENT, analysis->functions[i].event);
    }
    for (int i = 0; i < analysis->query_count; i++) {
        for (int j = 0; j < catalog->type_count; j++) {
            if (catalog->types[j].component &&
                catalog->types[j].component_id == analysis->queries[i].component) {
                use_id(ENTRY_COMPONENT, catalog->types[j].id);
            }
        }
    }
}

static void use_sources()
{
    const script_catalog *catalog = source_catalog_view(project);
    for (int i = 0; i < source_catalog_source_count(project); i++) {
        const source_catalog_source *source = source_catalog_source_at(project, i);
        script_analysis
            *analysis = script_analyze_document(source->syntax, catalog, source->identity);
        if (!analysis->valid) {
            fprintf(
                stderr,
                "%s: %s\n",
                source->path,
                analysis->diagnostic_count ? analysis->diagnostics[0].message : "invalid"
            );
        }
        problems += !analysis->valid;
        use_analysis(catalog, analysis);
        script_analysis_free(analysis);
    }
}

static int compare_paths(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The KDL files in content/<folder> in name order, owned by the caller. */
static char **directory_files(const char *folder)
{
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s/content/%s", project_root, folder);
    char **paths = NULL;
    DIR *directory = opendir(path);
    if (!directory) {
        return NULL;
    }
    for (struct dirent *item; (item = readdir(directory));) {
        size_t length = strlen(item->d_name);
        if (length > 4 && !strcmp(item->d_name + length - 4, ".kdl")) {
            char *file = xmalloc(MAX_PATH);
            snprintf(file, MAX_PATH, "%s/%s", path, item->d_name);
            apush(paths, file);
        }
    }
    closedir(directory);
    if (paths) {
        qsort(paths, (size_t)acount(paths), sizeof *paths, compare_paths);
    }
    return paths;
}

static void free_paths(char **paths)
{
    for (int i = 0; i < acount(paths); i++) {
        free(paths[i]);
    }
    afree(paths);
}

static bool listed(char **names, const char *name)
{
    for (int i = 0; i < acount(names); i++) {
        if (!strcmp(names[i], name)) {
            return true;
        }
    }
    return false;
}

static void reach_behavior(const char *name)
{
    if (!listed(reached_behaviors, name)) {
        apush(reached_behaviors, xstrdup(name));
    }
}

static void reach_prototype(const char *name)
{
    if (listed(reached_prototypes, name)) {
        return;
    }
    apush(reached_prototypes, xstrdup(name));
    for (int i = 0; i < acount(prototypes); i++) {
        for (int j = 0; !strcmp(prototypes[i].name, name) &&
                        j < acount(prototypes[i].behaviors);
             j++) {
            reach_behavior(prototypes[i].behaviors[j]);
        }
    }
}

static void use_prototypes()
{
    game_prototype_registry registry = {
        .components = GAME_REGISTRY_COMPONENTS,
        .component_count = GAME_REGISTRY_COMPONENT_COUNT,
    };
    char **paths = directory_files("prototypes");
    for (int i = 0; i < acount(paths); i++) {
        char *text = tool_read(paths[i]);
        char error[256];
        game_prototype *prototype = game_prototype_load(text, &registry, error, sizeof error);
        if (!prototype) {
            fprintf(stderr, "%s: %s\n", paths[i], error);
        }
        problems += !prototype;
        for (int j = 0; prototype && j < acount(prototype->components); j++) {
            use_component_name(prototype->components[j].desc->name);
        }
        if (prototype) {
            prototype_entry item = { .name = xstrdup(prototype->name) };
            for (int j = 0; j < acount(prototype->graphs); j++) {
                apush(item.behaviors, xstrdup(prototype->graphs[j]));
            }
            apush(prototypes, item);
        }
        game_prototype_free(prototype);
        free(text);
    }
    free_paths(paths);
}

static void use_scene_components(const av_value *components)
{
    for (size_t i = 0; components && i < av_obj_size(components); i++) {
        use_component_name(av_get_str(av_obj_key_at(components, i)));
    }
}

static void use_scenes()
{
    char **paths = directory_files("scenes");
    for (int i = 0; i < acount(paths); i++) {
        char *text = tool_read(paths[i]);
        char error[256];
        game_scene_document *scene = game_scene_document_load(text, error, sizeof error);
        if (!scene) {
            fprintf(stderr, "%s: %s\n", paths[i], error);
        }
        problems += !scene;
        for (int j = 0; scene && j < acount(scene->entities); j++) {
            const game_scene_entity *entity = &scene->entities[j];
            use_scene_components(entity->components);
            if (entity->prototype) {
                reach_prototype(entity->prototype);
            }
            for (int k = 0; k < acount(entity->behaviors); k++) {
                reach_behavior(entity->behaviors[k]);
            }
        }
        game_scene_document_free(scene);
        free(text);
    }
    free_paths(paths);
}

/* A behavior source's name is its file name in content/behaviors. */
static bool behavior_name(const source_catalog_source *source, char *name, size_t size)
{
    const char *folder = "behaviors/", *found = strstr(source->path, folder);
    const char *file = found ? found + strlen(folder) : NULL;
    if (!file || strchr(file, '/')) {
        return false;
    }
    snprintf(name, size, "%.*s", (int)strcspn(file, "."), file);
    return true;
}

/* Reaches each prototype that a reached behavior names in a string, as
 * spawn_prototype("Drone") does. Returns whether it reached any. */
static bool reach_spawned()
{
    bool reached = false;
    for (int i = 0; i < source_catalog_source_count(project); i++) {
        const source_catalog_source *source = source_catalog_source_at(project, i);
        char name[MAX_LINE], quoted[MAX_LINE];
        if (!behavior_name(source, name, sizeof name) ||
            !listed(reached_behaviors, name)) {
            continue;
        }
        for (int j = 0; j < acount(prototypes); j++) {
            snprintf(quoted, sizeof quoted, "\"%s\"", prototypes[j].name);
            if (!listed(reached_prototypes, prototypes[j].name) &&
                strstr(source->text, quoted)) {
                reach_prototype(prototypes[j].name);
                reached = true;
            }
        }
    }
    return reached;
}

/* Only what a scene runs counts as covered, so every prototype and behavior
 * must be reachable from a scene. */
static void check_reachable()
{
    while (reach_spawned()) {
    }
    for (int i = 0; i < acount(prototypes); i++) {
        if (!listed(reached_prototypes, prototypes[i].name)) {
            fprintf(
                stderr,
                "prototype %s: no scene uses or spawns it\n",
                prototypes[i].name
            );
            problems++;
        }
    }
    for (int i = 0; i < source_catalog_source_count(project); i++) {
        char name[MAX_LINE];
        if (behavior_name(source_catalog_source_at(project, i), name, sizeof name) &&
            !listed(reached_behaviors, name)) {
            fprintf(stderr, "behavior %s: no scene runs it\n", name);
            problems++;
        }
    }
}

static void free_prototypes()
{
    for (int i = 0; i < acount(prototypes); i++) {
        free(prototypes[i].name);
        free_paths(prototypes[i].behaviors);
    }
    afree(prototypes);
    free_paths(reached_prototypes);
    free_paths(reached_behaviors);
}

/* coverage-gaps.txt lists the catalog entries the project knowingly leaves
 * unused, one "<kind> <name>" per line. Without it the report only lists them. */
static bool read_allowed_gaps()
{
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s/coverage-gaps.txt", project_root);
    FILE *file = fopen(path, "r");
    if (!file) {
        return false;
    }
    char line[MAX_LINE];
    while (fgets(line, sizeof line, file)) {
        line[strcspn(line, "\r\n")] = 0;
        char *name = strchr(line, ' ');
        if (!line[0] || line[0] == '#') {
            continue;
        }
        if (!name) {
            fprintf(stderr, "coverage-gaps.txt: expected \"<kind> <name>\": %s\n", line);
            problems++;
            continue;
        }
        *name++ = 0;
        entry_kind kind = 0;
        while (kind < ENTRY_KIND_COUNT && strcmp(line, KIND_NAMES[kind])) {
            kind++;
        }
        entry *found = kind < ENTRY_KIND_COUNT ? find_name(kind, name) : NULL;
        if (!found) {
            fprintf(stderr, "coverage-gaps.txt: %s %s is not in the catalog; remove it\n", line, name);
        }
        problems += !found;
        if (found) {
            found->allowed = true;
        }
    }
    fclose(file);
    return true;
}

static void report(bool strict)
{
    for (entry_kind kind = 0; kind < ENTRY_KIND_COUNT; kind++) {
        int total = 0, used = 0;
        for (int i = 0; i < acount(entries); i++) {
            total += entries[i].kind == kind;
            used += entries[i].kind == kind && entries[i].used;
        }
        printf("Feature coverage: %d of %d %ss used\n", used, total, KIND_NAMES[kind]);
    }
    int unlisted = 0, stale = 0;
    for (int i = 0; i < acount(entries); i++) {
        const entry *item = &entries[i];
        if (!item->used && !item->allowed) {
            printf("gap: %s %s\n", KIND_NAMES[item->kind], item->name);
            unlisted++;
        }
        if (item->used && item->allowed) {
            fprintf(
                stderr,
                "coverage-gaps.txt: %s %s is now used; remove it\n",
                KIND_NAMES[item->kind],
                item->name
            );
            stale++;
        }
    }
    if (strict && unlisted) {
        fprintf(stderr, "Use each new gap in a feature-coverage scene, or list it in coverage-gaps.txt with a reason\n");
    }
    problems += strict && unlisted;
    problems += stale;
}

TEST_CASE(test_catalog_coverage)
{
    char error[256], content[MAX_PATH];
    native_capabilities *table = authoring_application_catalog_new(false, error, sizeof error);
    REQUIRE(table != NULL);
    const authoring_language_catalog_input input = { .native = table };
    native = authoring_language_catalog_new(&input, error, sizeof error);
    native_capabilities_free(table);
    REQUIRE(native != NULL);
    snprintf(content, sizeof content, "%s/content", project_root);
    project = source_catalog_new(content, authoring_language_catalog_view(native));
    for (int i = 0; i < source_catalog_diagnostic_count(project); i++) {
        const source_catalog_diagnostic *diagnostic = source_catalog_diagnostic_at(project, i);
        fprintf(
            stderr,
            "%s:%d: %s\n",
            diagnostic->path,
            diagnostic->range.line,
            diagnostic->message
        );
    }
    REQUIRE(!source_catalog_diagnostic_count(project));
    add_catalog(authoring_language_catalog_view(native), source_catalog_view(project));
    use_sources();
    use_prototypes();
    use_scenes();
    check_reachable();
    bool strict = read_allowed_gaps();
    report(strict);
    REQUIRE(!problems);
    return true;
}

/* Runs one scene through chibi_ctl's run_scenario and checks that it ran every
 * frame without a behavior failure. */
static bool run_scene(const char *path)
{
    const char *file = strrchr(path, '/') + 1;
    char name[MAX_LINE], request_path[MAX_PATH], log_path[MAX_PATH],
        request[2 * MAX_LINE];
    snprintf(name, sizeof name, "%.*s", (int)(strlen(file) - 4), file);
    snprintf(
        request,
        sizeof request,
        "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"scene\","
        "\"name\":\"%s\",\"frames\":%d}\n",
        name,
        SCENARIO_FRAMES
    );
    snprintf(request_path, sizeof request_path, "%s/feature-coverage-request.json", scratch);
    snprintf(log_path, sizeof log_path, "%s/feature-coverage-%s.log", scratch, name);
    tool_write(request_path, request);
    const char *argv[] = { "/bin/sh",
                           "-c",
                           "exec \"$0\" --project-root \"$1\" < \"$2\"",
                           CHIBI_CTL_PATH,
                           project_root,
                           request_path,
                           NULL };
    int status = tool_run(argv, project_root, log_path);
    char *output = tool_read(log_path);
    /* The log also holds the controller's diagnostics; the response is its own line. */
    const char *response = output ? strstr(output, "{\"contract\"") : NULL;
    yyjson_doc *doc = response ? yyjson_read(response, strcspn(response, "\n"), 0)
                               : NULL;
    yyjson_val *result = yyjson_obj_get(yyjson_doc_get_root(doc), "result");
    yyjson_val *failures = yyjson_obj_get(result, "behavior_failures");
    bool passed = !status &&
                  yyjson_get_uint(yyjson_obj_get(result, "frames")) == SCENARIO_FRAMES &&
                  yyjson_is_arr(failures) && !yyjson_arr_size(failures);
    printf("Scene %s: %s\n", name, passed ? "ran" : "failed");
    if (!passed && !status) { /* tool_run already printed a failed run's log. */
        fprintf(stderr, "%s\n", output ? output : "(no output)");
    }
    yyjson_doc_free(doc);
    free(output);
    return passed;
}

TEST_CASE(test_scenes_run)
{
    char **paths = directory_files("scenes");
    REQUIRE(acount(paths) > 0);
    REQUIRE(mkdtemp(scratch));
    int failed = 0;
    for (int i = 0; i < acount(paths); i++) {
        failed += !run_scene(paths[i]);
    }
    free_paths(paths);
    tool_remove_tree(scratch);
    REQUIRE(!failed);
    return true;
}

int run_feature_coverage(const char *root)
{
    if (!realpath(root, project_root)) {
        perror(root);
        return 1;
    }
    RUN_TEST_CASE(test_catalog_coverage);
    RUN_TEST_CASE(test_scenes_run);
    source_catalog_free(project);
    authoring_language_catalog_free(native);
    afree(entries);
    free_prototypes();
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}
