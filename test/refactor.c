#include "assets/refactor.h"

#include "assets/prototype.h"
#include "assets/scene_document.h"
#include "assets/ui_document.h"
#include "graph/compiler/compile.h"
#include "graph/document/events.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_file.h"
#include "support/xalloc.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_check(int ok, const char *message);

#define ARENA "projects/arena2d/content"

static const struct
{
    const char *folder;
    reference_asset_kind kind;
} FOLDERS[] = {
    { "behaviors", REFERENCE_ASSET_BEHAVIOR },
    { "events", REFERENCE_ASSET_EVENT },
    { "libraries", REFERENCE_ASSET_LIBRARY },
    { "materials", REFERENCE_ASSET_MATERIAL },
    { "shapes", REFERENCE_ASSET_SHAPE },
    { "fields", REFERENCE_ASSET_FIELD },
    { "shader-libraries", REFERENCE_ASSET_LIBRARY },
    { "systems", REFERENCE_ASSET_SYSTEM },
    { "prototypes", REFERENCE_ASSET_PROTOTYPE },
    { "scenes", REFERENCE_ASSET_SCENE },
    { "ui", REFERENCE_ASSET_UI },
};

#define FOLDER_COUNT ((int)(sizeof FOLDERS / sizeof *FOLDERS))

typedef struct
{
    reference_asset_kind kind;
    char *name;
    char *text; /* NULL once deleted */
} test_asset;

/* A project's assets in memory, and their index, as the MCP builds it. */
typedef struct
{
    test_asset *assets;
    reference_index *index;
    const char **texts; /* parallel to the index */
    game_prototype_registry registry;
} test_project;

static void add_content(test_project *p, int position, const test_asset *asset)
{
    char error[256];
    switch (asset->kind) {
        case REFERENCE_ASSET_PROTOTYPE: {
            game_prototype *prototype = game_prototype_load(asset->text, &p->registry, error, sizeof error);
            assert(prototype);
            reference_index_add_prototype(p->index, position, prototype);
            game_prototype_free(prototype);
            return;
        }
        case REFERENCE_ASSET_SCENE: {
            game_scene_document *scene = game_scene_document_load(asset->text, error, sizeof error);
            assert(scene);
            for (int i = 0; i < acount(scene->entities); i++) {
                game_scene_entity *entity = &scene->entities[i];
                reference_object object = {
                    .name = entity->name,
                    .prototype = entity->prototype,
                    .overrides = entity->components_json,
                    .behaviors = (const char *const *)entity->behaviors,
                    .behavior_count = acount(entity->behaviors),
                };
                reference_index_add_object(p->index, position, &object);
            }
            game_scene_document_free(scene);
            return;
        }
        case REFERENCE_ASSET_UI: {
            ui_document document = { 0 };
            assert(ui_document_read(asset->text, NULL, NULL, &document, error, sizeof error));
            reference_index_add_ui(p->index, position, document.rml);
            ui_document_free(&document);
            return;
        }
        default: {
            behavior_asset *graphs = behavior_asset_load_json(asset->text);
            assert(graphs);
            reference_index_add_graphs(p->index, position, graphs);
            behavior_asset_free(graphs);
            return;
        }
    }
}

/* Every asset first, then what each holds. */
static void index_project(test_project *p)
{
    reference_index_free(p->index);
    free(p->texts);
    p->index = reference_index_new();
    int *positions = xcalloc((size_t)acount(p->assets) + 1, sizeof *positions);
    for (int i = 0; i < acount(p->assets); i++) {
        char key[256];
        snprintf(key, sizeof key, "%d/%s", p->assets[i].kind, p->assets[i].name);
        positions[i] = p->assets[i].text ? reference_index_add_asset(
                                               p->index,
                                               p->assets[i].kind,
                                               p->assets[i].name,
                                               NULL,
                                               key
                                           )
                                         : -1;
    }
    for (int i = 0; i < acount(p->assets); i++) {
        if (positions[i] >= 0) {
            add_content(p, positions[i], &p->assets[i]);
        }
    }
    p->texts = xcalloc(
        (size_t)reference_index_asset_count(p->index) + 1,
        sizeof *p->texts
    );
    for (int i = 0; i < acount(p->assets); i++) {
        if (positions[i] >= 0) {
            p->texts[positions[i]] = p->assets[i].text;
        }
    }
    free(positions);
}

static test_project project_load(const char *content)
{
    test_project p = { .registry = { .components = GAME_REGISTRY_COMPONENTS,
                                     .component_count = GAME_REGISTRY_COMPONENT_COUNT } };
    for (int f = 0; f < FOLDER_COUNT; f++) {
        char folder[256];
        snprintf(folder, sizeof folder, "%s/%s", content, FOLDERS[f].folder);
        DIR *directory = opendir(folder);
        struct dirent *item;
        while (directory && (item = readdir(directory))) {
            size_t length = strlen(item->d_name);
            if (item->d_name[0] == '.' || length < 6 ||
                strcmp(item->d_name + length - 5, ".yaml")) {
                continue;
            }
            char path[512];
            snprintf(path, sizeof path, "%s/%s", folder, item->d_name);
            test_asset asset = { .kind = FOLDERS[f].kind,
                                 .name = xstrdup(item->d_name),
                                 .text = text_file_read(path) };
            asset.name[length - 5] = '\0';
            assert(asset.text);
            apush(p.assets, asset);
        }
        if (directory) {
            closedir(directory);
        }
    }
    index_project(&p);
    return p;
}

static void project_free(test_project *p)
{
    for (int i = 0; i < acount(p->assets); i++) {
        free(p->assets[i].name);
        free(p->assets[i].text);
    }
    afree(p->assets);
    reference_index_free(p->index);
    free(p->texts);
}

static refactor_project view(const test_project *p)
{
    return (refactor_project){ .index = p->index,
                               .texts = p->texts,
                               .registry = &p->registry };
}

static int asset_named(const test_project *p, reference_asset_kind kind, const char *name)
{
    return reference_index_find_name(p->index, kind, name);
}

/* The project asset at an index position. */
static test_asset *asset_of(test_project *p, int position)
{
    for (int i = 0; i < acount(p->assets); i++) {
        if (p->assets[i].text == p->texts[position]) {
            return &p->assets[i];
        }
    }
    assert(!"indexed asset");
    return NULL;
}

/* A graph asset's compile errors, linked to the project's libraries and
   events as validation links it. */
static int errors(const test_project *p, const char *text)
{
    behavior_asset *asset = behavior_asset_load_json(text);
    assert(asset);
    behavior_asset **catalog = NULL;
    for (int i = 0; i < acount(p->assets); i++) {
        const test_asset *other = &p->assets[i];
        if (other->text && (other->kind == REFERENCE_ASSET_EVENT ||
                            other->kind == REFERENCE_ASSET_LIBRARY)) {
            apush(catalog, behavior_asset_load_json(other->text));
        }
    }
    char error[256];
    behavior_asset *linked = behavior_asset_link_libraries(
        asset,
        (const behavior_asset *const *)catalog,
        acount(catalog),
        error,
        sizeof error
    );
    compile_report report;
    compile_report_init(&report);
    int count = linked ? behavior_asset_compile(linked, &report) : 1;
    compile_report_free(&report);
    behavior_asset_free(linked);
    behavior_asset_free(asset);
    for (int i = 0; i < acount(catalog); i++) {
        behavior_asset_free(catalog[i]);
    }
    afree(catalog);
    return count;
}

static bool compiled(reference_asset_kind kind)
{
    return kind == REFERENCE_ASSET_BEHAVIOR || kind == REFERENCE_ASSET_LIBRARY;
}

/* Applies a plan as a publisher would, then indexes the project again.
   Graph assets it changes compile with no more errors than before. */
static bool apply(test_project *p, refactor_plan *plan)
{
    int *before = xcalloc((size_t)acount(plan->edits) + 1, sizeof *before);
    for (int i = 0; i < acount(plan->edits); i++) {
        test_asset *asset = asset_of(p, plan->edits[i].asset);
        before[i] = compiled(asset->kind) ? errors(p, asset->text) : 0;
    }
    test_asset **changed = NULL;
    for (int i = 0; i < acount(plan->edits); i++) {
        refactor_edit *edit = &plan->edits[i];
        test_asset *asset = asset_of(p, edit->asset);
        apush(changed, asset);
        free(asset->text);
        asset->text = edit->text;
        edit->text = NULL;
        if (edit->name) {
            free(asset->name);
            asset->name = edit->name;
            edit->name = NULL;
        }
    }
    refactor_plan_free(plan);
    bool ok = true;
    for (int i = 0; i < acount(changed); i++) {
        ok &= !compiled(changed[i]->kind) || !changed[i]->text ||
              errors(p, changed[i]->text) <= before[i];
    }
    afree(changed);
    free(before);
    index_project(p);
    return ok;
}

static int asset_uses(const test_project *p, reference_asset_kind kind, const char *name)
{
    int asset = asset_named(p, kind, name);
    reference_symbol symbol = { .kind = REFERENCE_SYMBOL_ASSET, .asset = asset };
    return asset < 0 ? 0 : reference_index_count_uses(p->index, symbol, REFERENCE_USES);
}

static int event_uses(const test_project *p, const char *name)
{
    reference_symbol symbol = { .kind = REFERENCE_SYMBOL_EVENT, .asset = -1, .name = name };
    return reference_index_count_uses(p->index, symbol, REFERENCE_USES);
}

/* Whether an edit changes the asset named `name`, before the plan applies. */
static bool edits(const test_project *p, const refactor_plan *plan, const char *name)
{
    for (int i = 0; i < acount(plan->edits); i++) {
        if (!strcmp(reference_index_asset_at(p->index, plan->edits[i].asset)->name, name)) {
            return true;
        }
    }
    return false;
}

static bool rename_to(test_project *p, reference_symbol symbol, const char *name, refactor_plan *plan)
{
    char error[256];
    refactor_project project = view(p);
    bool ok = refactor_rename(&project, symbol, name, plan, error, sizeof error) == 0;
    if (!ok) {
        printf("  rename to %s: %s\n", name, error);
    }
    return ok;
}

static reference_symbol asset_symbol(const test_project *p, reference_asset_kind kind, const char *name)
{
    int asset = asset_named(p, kind, name);
    assert(asset >= 0);
    return (reference_symbol){ .kind = REFERENCE_SYMBOL_ASSET, .asset = asset };
}

/* ---------- Events ---------- */

/* An event with a definition renames the definition, its handlers and its
 * senders; the definition's variables rename the payload pins with them. */
static void events()
{
    test_project p = project_load(ARENA);
    int uses = event_uses(&p, "Damage");
    refactor_plan plan;
    reference_symbol damage = { .kind = REFERENCE_SYMBOL_EVENT,
                                .asset = asset_named(&p, REFERENCE_ASSET_EVENT, "Damage"),
                                .name = "Damage" };
    bool planned = rename_to(&p, damage, "Harm", &plan);
    bool listed = planned && edits(&p, &plan, "Damage") &&
                  edits(&p, &plan, "HullDamage") &&
                  edits(&p, &plan, "ProjectileHit") && acount(plan.edits) == 3;
    bool applied = planned && apply(&p, &plan);
    test_check(
        listed && applied && uses > 0 && event_uses(&p, "Harm") == uses &&
            event_uses(&p, "Damage") == 0 &&
            asset_named(&p, REFERENCE_ASSET_EVENT, "Harm") >= 0,
        "renaming an event renames its definition, handlers and senders"
    );

    int harm = asset_named(&p, REFERENCE_ASSET_EVENT, "Harm");
    behavior_asset *definition = behavior_asset_load_json(p.texts[harm]);
    behavior_variable *amount = behavior_asset_find_variable_by_name(definition, "Amount");
    reference_symbol variable = { .kind = REFERENCE_SYMBOL_VARIABLE,
                                  .asset = harm,
                                  .id = amount->id };
    behavior_asset_free(definition);
    planned = rename_to(&p, variable, "Points", &plan);
    listed = planned && edits(&p, &plan, "HullDamage") &&
             edits(&p, &plan, "ProjectileHit");
    applied = planned && apply(&p, &plan);
    bool payload = applied &&
                   strstr(p.texts[asset_named(&p, REFERENCE_ASSET_BEHAVIOR, "HullDamage")], "Points");
    test_check(listed && applied && payload, "renaming an event definition's variable renames the payload pins of its senders and handlers");

    /* Send Event has Exec and Entity inputs, and Event a Then output. */
    char error[256];
    refactor_project project = view(&p);
    definition = behavior_asset_load_json(p.texts[harm]);
    variable.id = behavior_asset_find_variable_by_name(definition, "Points")->id;
    reference_symbol source = {
        .kind = REFERENCE_SYMBOL_VARIABLE,
        .asset = harm,
        .id = behavior_asset_find_variable_by_name(definition, "Source")->id,
    };
    behavior_asset_free(definition);
    bool clash = refactor_rename(&project, variable, "Exec", &plan, error, sizeof error) !=
                     0 &&
                 refactor_rename(&project, variable, "Then", &plan, error, sizeof error) !=
                     0 &&
                 refactor_rename(&project, source, "Entity", &plan, error, sizeof error) !=
                     0;
    test_check(clash, "renaming a payload variable refuses the name of a sender's or handler's own pin");

    /* Ui documents and action bindings send no payload, so they leave a
       payload variable free to delete. */
    const reference **blockers = NULL;
    refactor_blockers(p.index, variable, &blockers);
    int payload_uses = acount(blockers);
    afree(blockers);
    test_asset *menu = asset_of(&p, asset_named(&p, REFERENCE_ASSET_UI, "PauseMenu"));
    char *sent = strstr(menu->text, "emit('Quit')");
    assert(sent);
    memcpy(sent, "emit('Harm')", strlen("emit('Harm')"));
    index_project(&p);
    refactor_blockers(p.index, variable, &blockers);
    test_check(
        payload_uses > 0 && acount(blockers) == payload_uses &&
            event_uses(&p, "Harm") > payload_uses,
        "a payload variable's blockers leave out ui and action sends"
    );
    afree(blockers);
    sent = strstr(menu->text, "emit('Harm')");
    memcpy(sent, "emit('Quit')", strlen("emit('Quit')"));
    index_project(&p);

    /* An event definition's file takes the event's name. */
    reference_symbol harm_event = { .kind = REFERENCE_SYMBOL_EVENT,
                                    .asset = harm,
                                    .name = "Harm" };
    project = view(&p);
    bool unsafe = refactor_rename(&project, harm_event, "../Harm", &plan, error, sizeof error) !=
                      0 &&
                  refactor_rename(&project, harm_event, "Ha:rm", &plan, error, sizeof error) !=
                      0;
    bool cased = refactor_rename(&project, harm_event, "harm", &plan, error, sizeof error) !=
                     0 &&
                 strstr(error, "only in case");
    test_check(unsafe && cased, "renaming an event refuses names that cannot name its file, and case-only renames");

    /* An event no asset defines, sent by a ui document. */
    uses = event_uses(&p, "Resume");
    reference_symbol resume = { .kind = REFERENCE_SYMBOL_EVENT,
                                .asset = -1,
                                .name = "Resume" };
    planned = rename_to(&p, resume, "Continue", &plan);
    listed = planned && acount(plan.edits) == 1 && edits(&p, &plan, "PauseMenu");
    applied = planned && apply(&p, &plan);
    test_check(
        listed && applied && uses == 1 && event_uses(&p, "Continue") == 1 &&
            event_uses(&p, "Resume") == 0,
        "renaming a ui event rewrites the emit that sends it"
    );

    project = view(&p);
    reference_symbol fire = { .kind = REFERENCE_SYMBOL_EVENT, .asset = -1, .name = "Fire" };
    bool host = refactor_rename(&project, fire, "Shoot", &plan, error, sizeof error) != 0 &&
                !acount(plan.edits);
    bool taken = refactor_rename(&project, resume, "Restart", &plan, error, sizeof error) !=
                 0;
    test_check(host && taken, "renaming refuses host events and names another event has");
    project_free(&p);
}

/* ---------- Assets ---------- */

typedef struct
{
    reference_asset_kind kind;
    const char *from, *to;
    const char *user; /* an asset whose references the rename rewrites */
    int uses;
} asset_case;

static const asset_case ASSET_CASES[] = {
    { REFERENCE_ASSET_MATERIAL, "Basalt", "Rock", "Cave", 1 },
    { REFERENCE_ASSET_FIELD, "BasaltCave", "Cavern", "Cave", 1 },
    { REFERENCE_ASSET_BEHAVIOR, "Pursuit", "Chase", "Sandbox", 4 },
    { REFERENCE_ASSET_UI, "MenuButton", "MenuItem", "PauseMenu", 3 },
    { REFERENCE_ASSET_PROTOTYPE, "TrailEmitter", "Trail", "ProjectileEffects", 1 },
    { REFERENCE_ASSET_LIBRARY, "WeaponActions", "Weapons", "HullDamage", 0 },
    { REFERENCE_ASSET_SYSTEM, "FreeMotion", "Drift", "FreeMotionSystem", 1 },
};

/* An asset's file and name follow its rename, and every asset naming it:
 * attachments, placements, spawns, component fields, widgets and calls. */
static void assets()
{
    test_project p = project_load(ARENA);
    int renamed = 0;
    for (size_t i = 0; i < sizeof ASSET_CASES / sizeof *ASSET_CASES; i++) {
        const asset_case *c = &ASSET_CASES[i];
        int uses = asset_uses(&p, c->kind, c->from);
        refactor_plan plan;
        bool planned = rename_to(&p, asset_symbol(&p, c->kind, c->from), c->to, &plan);
        bool listed = planned && edits(&p, &plan, c->from) &&
                      edits(&p, &plan, c->user);
        bool applied = planned && apply(&p, &plan);
        bool ok = listed && applied && (!c->uses || uses == c->uses) &&
                  asset_uses(&p, c->kind, c->to) == uses &&
                  asset_named(&p, c->kind, c->from) < 0;
        if (!ok) {
            printf(
                "  %s -> %s: listed %d applied %d uses %d -> %d\n",
                c->from,
                c->to,
                listed,
                applied,
                uses,
                asset_uses(&p, c->kind, c->to)
            );
        }
        renamed += ok;
    }
    test_check(
        renamed == (int)(sizeof ASSET_CASES / sizeof *ASSET_CASES),
        "renaming an asset renames its file and every reference to it"
    );

    /* A library call passes Projectile on as a name the index cannot read. */
    refactor_plan plan;
    bool planned = rename_to(&p, asset_symbol(&p, REFERENCE_ASSET_PROTOTYPE, "Projectile"), "Bolt", &plan);
    bool mentioned = planned && acount(plan.mentions) >= 2;
    refactor_plan_free(&plan);
    test_check(mentioned, "a rename lists values that still name the asset without a reference");

    char error[256];
    refactor_project project = view(&p);
    reference_symbol cave = asset_symbol(&p, REFERENCE_ASSET_PROTOTYPE, "Cave");
    bool taken = refactor_rename(&project, cave, "Player", &plan, error, sizeof error) !=
                     0 &&
                 refactor_rename(&project, cave, "player", &plan, error, sizeof error) !=
                     0;
    bool cased = refactor_rename(&project, cave, "cave", &plan, error, sizeof error) != 0 &&
                 strstr(error, "only in case");
    bool invalid = refactor_rename(&project, cave, "../Cave", &plan, error, sizeof error) !=
                       0 &&
                   refactor_rename(&project, cave, " Cave", &plan, error, sizeof error) !=
                       0;
    bool scene = refactor_rename(
                     &project,
                     asset_symbol(&p, REFERENCE_ASSET_SCENE, "Sandbox"),
                     "Arena",
                     &plan,
                     error,
                     sizeof error
                 ) != 0;
    reference_symbol health = { .kind = REFERENCE_SYMBOL_COMPONENT,
                                .asset = -1,
                                .name = "GameHealth" };
    bool component = refactor_rename(&project, health, "Vitality", &plan, error, sizeof error) !=
                     0;
    test_check(taken && cased && invalid && scene && component, "renaming refuses taken, case-only and unsafe names, scenes and components");
    project_free(&p);
}

/* Names in struct defaults, mesh parts, scene overrides, action bindings and
 * data-keys bindings. */
static void values()
{
    test_project p = project_load("content");
    int uses = asset_uses(&p, REFERENCE_ASSET_MATERIAL, "ArenaExperience");
    refactor_plan plan;
    bool planned = rename_to(
        &p,
        asset_symbol(&p, REFERENCE_ASSET_MATERIAL, "ArenaExperience"),
        "Experience",
        &plan
    );
    bool listed = planned && edits(&p, &plan, "ArenaDropBehavior");
    bool applied = planned && apply(&p, &plan);
    test_check(
        listed && applied && uses >= 2 &&
            asset_uses(&p, REFERENCE_ASSET_MATERIAL, "Experience") == uses,
        "renaming a material renames it in struct defaults such as a "
        "GameShader value"
    );
    reference_symbol flip = { .kind = REFERENCE_SYMBOL_EVENT, .asset = -1, .name = "FlipLeft" };
    uses = event_uses(&p, "FlipLeft");
    planned = rename_to(&p, flip, "FlipPort", &plan);
    listed = planned && edits(&p, &plan, "OrbitFlipper");
    applied = planned && apply(&p, &plan);
    test_check(
        listed && applied && uses >= 1 && event_uses(&p, "FlipPort") == uses,
        "renaming an event renames the action bindings that send it"
    );
    project_free(&p);

    p = project_load("projects/studio/content");
    uses = event_uses(&p, "Choose");
    reference_symbol choose = { .kind = REFERENCE_SYMBOL_EVENT,
                                .asset = -1,
                                .name = "Choose" };
    planned = rename_to(&p, choose, "Pick", &plan);
    listed = planned && edits(&p, &plan, "GraphView") && edits(&p, &plan, "MenuKeys");
    applied = planned && apply(&p, &plan);
    const char *view = p.texts[asset_named(&p, REFERENCE_ASSET_UI, "GraphView")];
    test_check(
        listed && applied && event_uses(&p, "Pick") == uses &&
            strstr(view, "Enter:Pick Space:Pick") && !strstr(view, "Choose"),
        "renaming an event renames every data-keys binding that sends it"
    );
    project_free(&p);

    /* Two parts of one mesh and an object's override name one material. */
    p = project_load(ARENA);
    apush(
        p.assets,
        ((test_asset){ REFERENCE_ASSET_PROTOTYPE,
                       xstrdup("Tile"),
                       xstrdup(
                           "{\"name\":\"Tile\",\"components\":{\"GAME."
                           "GameMeshParts\":{\"parts\":"
                           "\"[{\\\"material\\\":\\\"Basalt\\\"},{"
                           "\\\"material\\\":\\\"Basalt\\\"}]\"}}}"
                       ) })
    );
    test_asset *sandbox = asset_of(&p, asset_named(&p, REFERENCE_ASSET_SCENE, "Sandbox"));
    char error[256];
    game_scene_document *scene = game_scene_document_load(sandbox->text, error, sizeof error);
    assert(
        game_scene_document_set_entity_components(scene, 0, "{\"GameShader\":{\"material\":\"Basalt\"}}") == 0
    );
    free(sandbox->text);
    char *json = game_scene_document_save(scene);
    sandbox->text = asset_text_to_disk(json, error, sizeof error);
    free(json);
    game_scene_document_free(scene);
    index_project(&p);
    uses = asset_uses(&p, REFERENCE_ASSET_MATERIAL, "Basalt");
    planned = rename_to(&p, asset_symbol(&p, REFERENCE_ASSET_MATERIAL, "Basalt"), "Rock", &plan);
    listed = planned && edits(&p, &plan, "Tile") &&
             edits(&p, &plan, "Sandbox") && edits(&p, &plan, "Cave");
    applied = planned && apply(&p, &plan);
    const char *tile = p.texts[asset_named(&p, REFERENCE_ASSET_PROTOTYPE, "Tile")];
    const char *second = tile ? strstr(tile, "Rock") : NULL;
    test_check(
        listed && applied && uses == 4 &&
            asset_uses(&p, REFERENCE_ASSET_MATERIAL, "Rock") == 4 && second &&
            strstr(second + 4, "Rock"),
        "renaming a material renames mesh parts and scene overrides that name "
        "it"
    );
    project_free(&p);
}

/* ---------- Members ---------- */

static uint64_t graph_named(const test_project *p, int asset, const char *name)
{
    behavior_asset *graphs = behavior_asset_load_json(p->texts[asset]);
    graph *g = behavior_asset_find_graph_by_name(graphs, name);
    uint64_t id = g ? g->graph_id : 0;
    behavior_asset_free(graphs);
    return id;
}

/* A library function's calls in other assets follow its name and its
 * parameters' names. */
static void library_functions()
{
    test_project p = project_load(ARENA);
    int library = asset_named(&p, REFERENCE_ASSET_LIBRARY, "WeaponActions");
    reference_symbol spawn = { .kind = REFERENCE_SYMBOL_GRAPH,
                               .asset = library,
                               .id = graph_named(&p, library, "Spawn Prototype At") };
    int calls = reference_index_count_uses(p.index, spawn, REFERENCE_USES);
    refactor_plan plan;
    bool planned = rename_to(&p, spawn, "Spawn At", &plan);
    bool listed = planned && edits(&p, &plan, "HullDamage") &&
                  edits(&p, &plan, "SwarmerEffects");
    bool applied = planned && apply(&p, &plan);
    library = asset_named(&p, REFERENCE_ASSET_LIBRARY, "WeaponActions");
    spawn.asset = library;
    const char *hull = p.texts[asset_named(&p, REFERENCE_ASSET_BEHAVIOR, "HullDamage")];
    test_check(
        listed && applied && calls >= 3 &&
            reference_index_count_uses(p.index, spawn, REFERENCE_USES) == calls &&
            strstr(hull, "reference_name: Spawn At") &&
            !strstr(hull, "Spawn Prototype At"),
        "renaming a library function renames its calls in other assets"
    );

    behavior_asset *graphs = behavior_asset_load_json(p.texts[library]);
    graph *g = behavior_asset_find_graph(graphs, spawn.id);
    node *entry = graph_node_by_id(g, g->entry_id);
    uint64_t prototype = 0;
    for (int i = 0; i < entry->output_count; i++) {
        prototype = !strcmp(entry->output_pins[i].name, "Prototype")
                      ? entry->output_pins[i].id
                      : prototype;
    }
    behavior_asset_free(graphs);
    reference_symbol parameter = { .kind = REFERENCE_SYMBOL_PARAMETER,
                                   .asset = library,
                                   .graph_id = spawn.id,
                                   .id = prototype };
    planned = prototype && rename_to(&p, parameter, "Template", &plan);
    listed = planned && edits(&p, &plan, "HullDamage") &&
             edits(&p, &plan, "WeaponActions");
    applied = planned && apply(&p, &plan);
    hull = p.texts[asset_named(&p, REFERENCE_ASSET_BEHAVIOR, "HullDamage")];
    test_check(listed && applied && strstr(hull, "Template:") && !strstr(hull, "Prototype: {"), "renaming a library function's parameter renames the pins of its calls in other assets");
    project_free(&p);
}

/* A variable or named value stays in its asset. */
static void asset_members()
{
    test_project p = project_load(ARENA);
    int weapon = asset_named(&p, REFERENCE_ASSET_BEHAVIOR, "Weapon");
    behavior_asset *graphs = behavior_asset_load_json(p.texts[weapon]);
    assert(acount(graphs->variables));
    behavior_variable *variable = &graphs->variables[0];
    reference_symbol symbol = { .kind = REFERENCE_SYMBOL_VARIABLE,
                                .asset = weapon,
                                .id = variable->id };
    char *old = xstrdup(variable->name);
    behavior_asset_free(graphs);
    int uses = reference_index_count_uses(p.index, symbol, REFERENCE_USES);
    refactor_plan plan;
    bool planned = rename_to(&p, symbol, "Charge", &plan);
    bool listed = planned && acount(plan.edits) == 1 && edits(&p, &plan, "Weapon");
    bool applied = planned && apply(&p, &plan);
    graphs = behavior_asset_load_json(
        p.texts[asset_named(&p, REFERENCE_ASSET_BEHAVIOR, "Weapon")]
    );
    bool named = behavior_asset_find_variable_by_name(graphs, "Charge") &&
                 !behavior_asset_find_variable_by_name(graphs, old);
    behavior_asset_free(graphs);
    symbol.asset = asset_named(&p, REFERENCE_ASSET_BEHAVIOR, "Weapon");
    test_check(
        listed && applied && named &&
            reference_index_count_uses(p.index, symbol, REFERENCE_USES) == uses,
        "renaming a variable renames it and its Get and Set nodes"
    );
    free(old);
    project_free(&p);
}

/* ---------- Failure ---------- */

/* A rename that cannot rewrite every reference plans nothing. */
static void refused()
{
    test_project p = project_load(ARENA);
    int sandbox = asset_named(&p, REFERENCE_ASSET_SCENE, "Sandbox");
    const char *text = p.texts[sandbox];
    p.texts[sandbox] = NULL; /* as an unsaved asset has none */
    refactor_plan plan;
    char error[256];
    refactor_project project = view(&p);
    bool failed = refactor_rename(
                      &project,
                      asset_symbol(&p, REFERENCE_ASSET_BEHAVIOR, "Pursuit"),
                      "Chase",
                      &plan,
                      error,
                      sizeof error
                  ) != 0;
    p.texts[sandbox] = text;
    test_check(
        failed && !acount(plan.edits) && strstr(error, "Sandbox") &&
            strstr(p.texts[asset_named(&p, REFERENCE_ASSET_PROTOTYPE, "Swarmer")], "Pursuit"),
        "a rename that cannot rewrite one reference plans no edit"
    );
    project_free(&p);
}

/* ---------- Delete ---------- */

static void deletes()
{
    test_project p = project_load(ARENA);
    refactor_plan plan;
    char error[256];
    refactor_project project = view(&p);
    reference_symbol pursuit = asset_symbol(&p, REFERENCE_ASSET_BEHAVIOR, "Pursuit");
    bool blocked = refactor_delete(&project, pursuit, false, &plan, error, sizeof error) !=
                       0 &&
                   strstr(error, "more use it");
    bool forced = refactor_delete(&project, pursuit, true, &plan, error, sizeof error) ==
                      0 &&
                  acount(plan.edits) == 1 && !plan.edits[0].text &&
                  acount(plan.dangling) == 4;
    refactor_plan_free(&plan);
    test_check(blocked && forced, "deleting an asset refuses while it is used, and force lists what it leaves dangling");

    int weapon = asset_named(&p, REFERENCE_ASSET_BEHAVIOR, "Weapon");
    behavior_asset *graphs = behavior_asset_load_json(p.texts[weapon]);
    reference_symbol variable = { .kind = REFERENCE_SYMBOL_VARIABLE,
                                  .asset = weapon,
                                  .id = graphs->variables[0].id };
    behavior_asset_free(graphs);
    blocked = refactor_delete(&project, variable, false, &plan, error, sizeof error) != 0;
    forced = refactor_delete(&project, variable, true, &plan, error, sizeof error) == 0 &&
             acount(plan.edits) == 1 && plan.edits[0].text && !acount(plan.dangling);
    bool applied = forced && apply(&p, &plan);
    test_check(blocked && applied, "deleting a used variable refuses, and force removes its Get and Set nodes");

    project = view(&p);
    reference_symbol collect = { .kind = REFERENCE_SYMBOL_EVENT,
                                 .asset = -1,
                                 .name = "CollectExperience" };
    int uses = event_uses(&p, "CollectExperience");
    bool event = refactor_delete(&project, collect, uses > 0, &plan, error, sizeof error) ==
                     0 &&
                 acount(plan.edits) == 1 && !plan.edits[0].text;
    refactor_plan_free(&plan);
    reference_symbol resume = { .kind = REFERENCE_SYMBOL_EVENT,
                                .asset = -1,
                                .name = "Resume" };
    bool undefined = refactor_delete(&project, resume, true, &plan, error, sizeof error) !=
                     0;
    test_check(event && undefined, "deleting an event deletes its definition, and refuses one without a definition");
    project_free(&p);
}

void test_refactor()
{
    events();
    assets();
    values();
    library_functions();
    asset_members();
    refused();
    deletes();
}
