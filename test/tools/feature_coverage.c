/* Feature coverage: compares the engine catalog (components, native functions
 * and events) with what one project's scripts, prototypes and scenes use, and
 * runs each of the project's scenes for a few frames through run_scenario. */
#include "assets/project.h"
#include "assets/prototype.h"
#include "assets/scene_document.h"
#include "authoring/application_catalog.h"
#include "authoring/component_declarations.h"
#include "authoring/language_catalog.h"
#include "authoring/project_prepare_internal.h"
#include "authoring/source_catalog.h"
#include "authoring/source_prototype_flow.h"
#include "authoring/studio_project.h"
#include "runtime/behaviors/entities_definition.h"
#include "runtime/component_catalog.h"
#include "runtime/components.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <yyjson.h>

enum
{
    SCENARIO_FRAMES = 4,
    MAX_PATH = 4096,
};

typedef enum
{
    ENTRY_COMPONENT,
    ENTRY_FUNCTION,
    ENTRY_EVENT,
    ENTRY_KIND_COUNT,
} entry_kind;

static const char *const KIND_NAMES[ENTRY_KIND_COUNT] = { "component", "function", "event" };
static const char *const PROGRAM_KINDS[] = {
    [SCRIPT_PROGRAM_BEHAVIOR] = "behavior",
    [SCRIPT_PROGRAM_LIBRARY] = "library",
    [SCRIPT_PROGRAM_SYSTEM] = "system",
};

typedef struct
{
    entry_kind kind;
    const char *name; /* Borrowed from the catalog. */
    uint64_t id; /* Catalog type ID for components, capability ID otherwise. */
    bool used, allowed;
} entry;

typedef struct
{
    const game_prototype *prototype; /* Merged over its includes; values owns it. */
    char **includes;
} prototype_entry;

static char project_root[PATH_MAX], content_root[PATH_MAX];
static char scratch[] = "/tmp/chibi-feature-coverage-XXXXXX";
static authoring_language_catalog *native;
static source_catalog *project;
static entry *entries;
static int problems; /* Reported failures; the coverage case fails on any. */
static prototype_entry *prototypes;
static script_analysis **analyses; /* One per project source, in catalog order. */
/* Constants that reach a prototype parameter through values, by analysis. */
static authoring_prototype_name *prototype_names;
/* The project's declared components and loaded prototypes; scene
 * placements merge over the prototypes. */
static declared_components *declared;
static game_prototype_registry registry;
static project_prototype *values;
/* Names a scene reaches: its entities' prototypes, behaviors and systems, the
 * prototypes that running sources spawn by reference or through values, the
 * libraries they call, and the behaviors of spawned prototypes. */
static char **reached_prototypes, **spawned_prototypes, **reached_behaviors,
    **reached_libraries, **reached_systems;

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
 * language themselves. Generic component functions such as add_component name
 * no component of their own, so they are features. */
static bool feature_function(const script_catalog_capability *capability)
{
    return (capability->kind == SCRIPT_CAPABILITY_FUNCTION ||
            capability->kind == SCRIPT_CAPABILITY_ACTION) &&
           !capability->component &&
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

/* A generic component call, such as add_component("GAME.Heat"), names its
 * component through a selector argument whose capability is the type ID. */
static void use_component_selector(
    const script_catalog *catalog,
    const script_analysis *analysis,
    const script_expression *call
)
{
    const script_catalog_capability
        *capability = call->capability
                        ? script_catalog_capability_by_id(catalog, call->capability)
                        : NULL;
    if (!capability || capability->component_access == SCRIPT_COMPONENT_ACCESS_NONE ||
        capability->component) {
        return;
    }
    for (int i = 0; i < acount(call->arguments); i++) {
        const script_argument *argument = &call->arguments[i];
        if (argument->parameter >= 0 && argument->parameter < capability->parameter_count &&
            capability->parameters[argument->parameter].reference_kind ==
                SCRIPT_REFERENCE_COMPONENT) {
            use_id(ENTRY_COMPONENT, analysis->expressions[argument->value].capability);
        }
    }
}

static void use_analysis(const script_catalog *catalog, const script_analysis *analysis)
{
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *expression = &analysis->expressions[i];
        if (expression->kind != SCRIPT_EXPR_COMPONENT) { /* Its capability is a type ID. */
            use_capability(catalog, expression->capability);
            use_component_selector(catalog, analysis, expression);
        }
        /* A field such as input.Fire.held reads through a native getter. */
        use_capability(catalog, expression->read);
        use_capability(catalog, expression->write);
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
}

static void analyze_sources()
{
    const script_catalog *catalog = source_catalog_view(project);
    authoring_document *documents = NULL;
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
        apush(analyses, analysis);
        apush(documents, ((authoring_document){ source->path, source->syntax, analysis }));
    }
    prototype_names = authoring_prototype_names(documents, acount(documents), catalog);
    afree(documents);
}

static int compare_paths(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The KDL files in content/<folder> in name order, owned by the caller. */
static char **directory_files(const char *folder)
{
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s/%s", content_root, folder);
    char **paths = NULL;
    DIR *directory = opendir(path);
    if (!directory) {
        return NULL;
    }
    /* The files chibi_ctl enumerates as assets: visible, regular .kdl files. */
    for (struct dirent *item; (item = readdir(directory));) {
        const char *name = item->d_name;
        size_t length = strlen(name);
        if (name[0] == '.' || strchr(name, '\\') || strstr(name, "..") ||
            length <= 4 || strcmp(name + length - 4, ".kdl")) {
            continue;
        }
        char *file = xmalloc(MAX_PATH);
        snprintf(file, MAX_PATH, "%s/%s", path, name);
        struct stat info;
        if (lstat(file, &info) || !S_ISREG(info.st_mode)) {
            free(file);
            continue;
        }
        apush(paths, file);
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

/* The engine runs a system only while an entity's EngineSystem names it. */
static void reach_system(const char *name)
{
    if (!listed(reached_systems, name)) {
        apush(reached_systems, xstrdup(name));
    }
}

/* An instance a scene runs: its merged components count as used, and its
 * behaviors and the systems its EngineSystem names run. */
static void use_instance(const game_prototype *instance)
{
    for (int i = 0; i < acount(instance->graphs); i++) {
        reach_behavior(instance->graphs[i]);
    }
    for (int i = 0; i < acount(instance->components); i++) {
        const game_prototype_component *component = &instance->components[i];
        use_component_name(component->desc->name);
        const EngineSystem *system = component->data;
        if (component->desc->id == GAME_COMPONENT_EngineSystem.id && system->asset) {
            reach_system(system->asset);
        }
    }
}

/* Reaches the named prototype and the prototypes it includes. A spawn also
 * uses its instance, which is merged over its includes. A scene placement
 * uses the entity's instance instead (place_prototype). Returns whether it
 * reached anything new. */
static bool reach_prototype(const char *name, bool spawned)
{
    for (int i = 0; i < acount(prototypes); i++) {
        const prototype_entry *item = &prototypes[i];
        const game_prototype *prototype = item->prototype;
        if (strcmp(prototype->name, name)) {
            continue;
        }
        bool reached = !listed(reached_prototypes, name);
        if (reached) {
            apush(reached_prototypes, xstrdup(name));
            for (int j = 0; j < acount(item->includes); j++) {
                reach_prototype(item->includes[j], false);
            }
        }
        if (spawned && !listed(spawned_prototypes, name)) {
            apush(spawned_prototypes, xstrdup(name));
            use_instance(prototype);
            reached = true;
        }
        return reached;
    }
    return false;
}

/* The names in a prototype document's includes, owned by the caller. */
static char **prototype_includes(const char *text)
{
    char error[256];
    av_doc *document = av_doc_read(text, error, sizeof error);
    assert(document); /* project_prepare_prototypes already read it. */
    const av_value *includes = av_obj_get(av_doc_root(document), "includes");
    char **names = NULL;
    for (size_t i = 0; includes && i < av_arr_size(includes); i++) {
        apush(names, xstrdup(av_get_str(av_arr_get(includes, i))));
    }
    av_doc_free(document);
    return names;
}

/* Loads the prototypes as project preparation does, so includes resolve. */
static void use_prototypes()
{
    ecs_component_catalog components = project_components();
    registry = (game_prototype_registry){
        .components = components.components,
        .component_count = components.component_count,
    };
    char error[256];
    if (!project_prepare_prototypes(content_root, &registry, &values, error, sizeof error)) {
        fprintf(stderr, "%s/prototypes: %s\n", content_root, error);
        problems++;
    }
    for (int i = 0; i < acount(values); i++) {
        if (!values[i].prototype) { /* Loading stopped at an earlier failure. */
            continue;
        }
        prototype_entry item = { .prototype = values[i].prototype,
                                 .includes = prototype_includes(values[i].text) };
        apush(prototypes, item);
    }
}

/* A placed entity's instance is its prototype with the scene's component
 * overrides merged over it and its own behavior list, if it has one, in
 * place of the prototype's, as at runtime. */
static void place_prototype(const game_scene_entity *entity)
{
    reach_prototype(entity->prototype, false);
    char error[256];
    game_prototype *object = project_prepare_object(entity, values, error, sizeof error);
    if (!object) {
        fprintf(stderr, "scene entity %s: %s\n", entity->name, error);
        problems++;
        return;
    }
    use_instance(object);
    game_prototype_free(object);
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
            place_prototype(entity);
        }
        game_scene_document_free(scene);
        free(text);
    }
    free_paths(paths);
}

/* A source runs once a scene reaches it. An invalid source never runs;
 * analyze_sources reported it. */
static bool runs(const script_analysis *analysis)
{
    if (!analysis->valid) {
        return false;
    }
    switch (analysis->kind) {
        case SCRIPT_PROGRAM_BEHAVIOR:
            return listed(reached_behaviors, analysis->name);
        case SCRIPT_PROGRAM_LIBRARY:
            return listed(reached_libraries, analysis->name);
        case SCRIPT_PROGRAM_SYSTEM:
            return listed(reached_systems, analysis->name);
    }
    return false;
}

/* The library that declares the function with this capability ID, or NULL. */
static const script_analysis *library_of(uint64_t id)
{
    for (int i = 0; id && i < acount(analyses); i++) {
        const script_analysis *library = analyses[i];
        if (!library->valid || library->kind != SCRIPT_PROGRAM_LIBRARY) {
            continue;
        }
        for (int j = 0; j < library->function_count; j++) {
            int symbol = library->functions[j].symbol;
            if (symbol >= 0 && library->symbols[symbol].id == id) {
                return library;
            }
        }
    }
    return NULL;
}

static bool reach_library(uint64_t id)
{
    const script_analysis *library = library_of(id);
    if (!library || listed(reached_libraries, library->name)) {
        return false;
    }
    apush(reached_libraries, xstrdup(library->name));
    return true;
}

/* Reaches the prototype a spawn call names by reference, as
 * spawn_prototype("Drone") and spawn_prototype(Drone) do. */
static bool reach_spawned(const script_analysis *analysis, const script_expression *call)
{
    bool reached = false;
    for (int i = 0; i < call->argument_count; i++) {
        int value = call->arguments[i].value;
        const script_expression *argument = value < 0
                                              ? NULL
                                              : &analysis->expressions[value];
        if (argument && argument->kind == SCRIPT_EXPR_CONSTANT && argument->capability) {
            reached |= reach_prototype(argument->constant.str, true);
        }
    }
    return reached;
}

/* Reaches what one running source spawns and the libraries it calls. Returns
 * whether it reached anything new. */
static bool reach_from(int document)
{
    const script_analysis *analysis = analyses[document];
    const script_catalog *catalog = source_catalog_view(project);
    bool reached = false;
    for (int i = 0; i < acount(prototype_names); i++) {
        if (prototype_names[i].document == document) {
            uint64_t id = prototype_names[i].prototype;
            reached |= reach_prototype(
                script_catalog_capability_by_id(catalog, id)->name,
                true
            );
        }
    }
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *expression = &analysis->expressions[i];
        if (expression->kind != SCRIPT_EXPR_CALL) {
            continue;
        }
        const script_catalog_capability
            *capability = script_catalog_capability_by_id(catalog, expression->capability);
        reached |= reach_library(expression->capability);
        if (capability && !strcmp(capability->name, GAME_ENTITY_SPAWN_SOURCE)) {
            reached |= reach_spawned(analysis, expression);
        }
    }
    for (int i = 0; i < analysis->use_count; i++) {
        int symbol = analysis->uses[i].symbol;
        reached |= symbol >= 0 && reach_library(analysis->symbols[symbol].capability);
    }
    return reached;
}

/* Only what a scene runs counts as covered, so every prototype, behavior and
 * library must be reachable from a scene, and only running sources are used. */
static void use_reachable()
{
    for (bool reached = true; reached;) {
        reached = false;
        for (int i = 0; i < acount(analyses); i++) {
            if (runs(analyses[i])) {
                reached |= reach_from(i);
            }
        }
    }
    for (int i = 0; i < acount(prototypes); i++) {
        const char *name = prototypes[i].prototype->name;
        if (!listed(reached_prototypes, name)) {
            fprintf(stderr, "prototype %s: no scene uses or spawns it\n", name);
            problems++;
        }
    }
    const script_catalog *catalog = source_catalog_view(project);
    for (int i = 0; i < acount(analyses); i++) {
        const script_analysis *analysis = analyses[i];
        if (runs(analysis)) {
            use_analysis(catalog, analysis);
            continue;
        }
        if (analysis->valid) {
            fprintf(
                stderr,
                "%s %s: no scene runs it\n",
                PROGRAM_KINDS[analysis->kind],
                analysis->name
            );
            problems++;
        }
    }
}

static void free_reach()
{
    for (int i = 0; i < acount(prototypes); i++) {
        free_paths(prototypes[i].includes);
    }
    afree(prototypes);
    project_prepare_prototypes_free(values);
    free_paths(reached_prototypes);
    free_paths(spawned_prototypes);
    free_paths(reached_behaviors);
    free_paths(reached_libraries);
    free_paths(reached_systems);
    for (int i = 0; i < acount(analyses); i++) {
        script_analysis_free(analyses[i]);
    }
    afree(analyses);
    afree(prototype_names);
}

/* coverage-gaps.txt lists the catalog entries the project knowingly leaves
 * unused, one "<kind> <name>" per line, each in a group of lines under a
 * comment that gives the reason. Without it, every unused entry is a gap. */
static void read_allowed_gaps()
{
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s/coverage-gaps.txt", project_root);
    FILE *file = fopen(path, "r");
    if (!file) {
        return;
    }
    char *line = NULL;
    size_t size = 0;
    bool reasoned = false; /* A comment gives the reason for the entries below it. */
    while (getline(&line, &size, file) >= 0) {
        line[strcspn(line, "\r\n")] = 0;
        char *name = strchr(line, ' ');
        if (!line[0]) {
            reasoned = false;
            continue;
        }
        if (line[0] == '#') { /* A bare "#" spaces a reason; it gives none. */
            reasoned |= line[1 + strspn(line + 1, " \t")] != 0;
            continue;
        }
        if (!reasoned) {
            fprintf(stderr, "coverage-gaps.txt: give %s a reason in a comment above it\n", line);
            problems++;
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
    free(line);
    fclose(file);
}

static void report()
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
    if (unlisted) {
        fprintf(stderr, "Use each new gap in a feature-coverage scene, or list it in coverage-gaps.txt with a reason\n");
    }
    problems += unlisted > 0;
    problems += stale;
}

TEST_CASE(test_catalog_coverage)
{
    char error[256];
    /* The Studio project sees the editor's capabilities, as chibi_ctl gives it. */
    native_capabilities *table = authoring_application_catalog_new(
        studio_project_is(project_root),
        error,
        sizeof error
    );
    REQUIRE(table != NULL);
    /* The live declared set joins the language catalog and project_components. */
    if (!authoring_component_declarations_load(
            content_root,
            native_capabilities_catalog(table),
            &declared,
            error,
            sizeof error
        )) {
        fprintf(stderr, "%s/components: %s\n", content_root, error);
    }
    REQUIRE(declared != NULL);
    const authoring_language_catalog_input input = { .native = table };
    native = authoring_language_catalog_new(&input, error, sizeof error);
    native_capabilities_free(table);
    REQUIRE(native != NULL);
    project = source_catalog_new(content_root, authoring_language_catalog_view(native));
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
    analyze_sources();
    use_prototypes();
    use_scenes();
    use_reachable();
    read_allowed_gaps();
    report();
    REQUIRE(!problems);
    return true;
}

/* Runs one scene through chibi_ctl's run_scenario and checks that it ran every
 * frame without a behavior failure. */
static bool run_scene(const char *path)
{
    /* A scene's name is its file name without ".kdl"; the scene runs under it,
     * as current_scene() reports. */
    const char *file = strrchr(path, '/') + 1;
    char name[MAX_PATH], request_path[MAX_PATH], log_path[MAX_PATH];
    snprintf(name, sizeof name, "%.*s", (int)(strlen(file) - strlen(".kdl")), file);
    yyjson_mut_doc *message = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(message);
    yyjson_mut_doc_set_root(message, root);
    yyjson_mut_obj_add_int(message, root, "contract", 1);
    yyjson_mut_obj_add_str(message, root, "operation", "run_scenario");
    yyjson_mut_obj_add_str(message, root, "kind", "scene");
    yyjson_mut_obj_add_str(message, root, "name", name);
    yyjson_mut_obj_add_int(message, root, "frames", SCENARIO_FRAMES);
    size_t length;
    char *json = yyjson_mut_write(message, 0, &length);
    yyjson_mut_doc_free(message);
    char *request = xmalloc(length + 2); /* The controller reads one line. */
    memcpy(request, json, length);
    memcpy(request + length, "\n", 2);
    free(json);
    snprintf(request_path, sizeof request_path, "%s/feature-coverage-request.json", scratch);
    snprintf(log_path, sizeof log_path, "%s/feature-coverage-%s.log", scratch, name);
    tool_write(request_path, request);
    free(request);
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
    char error[256];
    chibi_project manifest = { 0 };
    if (!realpath(root, project_root)) {
        perror(root);
        return 1;
    }
    if (chibi_project_open(&manifest, project_root, error, sizeof error)) {
        fprintf(stderr, "%s: %s\n", root, error);
        return 1;
    }
    bool resolved = realpath(manifest.content, content_root);
    if (!resolved) {
        perror(manifest.content);
    }
    chibi_project_free(&manifest);
    if (!resolved) {
        return 1;
    }
    /* chibi_ctl's run_scenario reads scenes only from <root>/content. */
    char expected[PATH_MAX];
    snprintf(expected, sizeof expected, "%s/content", project_root);
    if (strcmp(content_root, expected)) {
        fprintf(stderr, "%s: feature coverage needs the content folder at %s\n", root, expected);
        return 1;
    }
    RUN_TEST_CASE(test_catalog_coverage);
    RUN_TEST_CASE(test_scenes_run);
    source_catalog_free(project);
    authoring_language_catalog_free(native);
    afree(entries);
    free_reach();
    declared_components_free(declared);
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}
