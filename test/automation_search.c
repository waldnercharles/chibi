#include "assets/project.h"
#include "automation/controller.h"
#include "graph/model/registry.h"
#include "runtime/systems/system.h"
#include "suite_internal.h"
#include "support/array.h"
#include "support/text_search.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* An agent's query and the node it means: the type, the component, struct or
 * event it names, and for a Set the field it writes. */
typedef struct
{
    const char *query;
    const char *domain;
    const char *type;
    const char *name;
    const char *field;
} intent;

static const intent INTENTS[] = {
    { "set health current", "behavior", "Set Component", "GameHealth", "current" },
    { "set health", "behavior", "Set Component", "GameHealth", NULL },
    { "get transform x", "behavior", "Get Component", "GameTransform", NULL },
    { "has aim", "behavior", "Has Component", "GameAim", NULL },
    { "add health", "behavior", "Add Component", "GameHealth", NULL },
    { "remove velocity", "behavior", "Remove Component", "GameVelocity", NULL },
    { "make transform", "behavior", "Make Struct", "GameTransform", NULL },
    { "break velocity", "behavior", "Break Struct", "GameVelocity", NULL },
    { "branch", "behavior", "Branch", NULL, NULL },
    { "random range", "behavior", "Random Range", NULL, NULL },
    { "destroy entity", "behavior", "Destroy Entity", NULL, NULL },
    { "spawn prototype", "behavior", "Spawn Prototype", NULL, NULL },
    { "play sound", "behavior", "Play Sound", NULL, NULL },
    { "clamp", "behavior", "Clamp", NULL, NULL },
    { "delay", "behavior", "Delay", NULL, NULL },
    { "for each", "behavior", "ForEach", NULL, NULL },
    { "sequence", "behavior", "Sequence", NULL, NULL },
    { "delta time", "behavior", "Delta Time", NULL, NULL },
    { "base color", "shader", "Shader Surface Output", NULL, NULL },
    { "get velocity", "system", "Get_GameVelocity", NULL, NULL },
    { "on hit", "behavior", "Event", "Contact Begin", NULL },
    { "every frame", "behavior", "Event", "Tick", NULL },
    { "on start", "behavior", "Event", "Begin", NULL },
    { "send damaged", "behavior", "Send Event", "Damaged", NULL },
    { "wait", "behavior", "Delay", NULL, NULL },
    { "if", "behavior", "Branch", NULL, NULL },
    { "kill", "behavior", "Destroy Entity", NULL, NULL },
    { "instantiate", "behavior", "Spawn Prototype", NULL, NULL },
    { "closest", "behavior", "Find Nearest Entity", NULL, NULL },
    { "owner", "behavior", "Self", NULL, NULL },
    { "sqrt", "behavior", "Square Root", NULL, NULL },
    { "ln", "behavior", "Logarithm", NULL, NULL },
    { "vec2", "behavior", "Make Struct", "CF_V2", NULL },
    { "button", "behavior", "Click UI Element", NULL, NULL },
    { "click", "behavior", "Click UI Element", NULL, NULL },
    { "menu", "behavior", "Set UI Visible", NULL, NULL },
    { "hud", "behavior", "Set UI Text", NULL, NULL },
    { "label", "behavior", "Set UI Text", NULL, NULL },
    { "slider", "behavior", "Set UI Value", NULL, NULL },
};

static char *search_in(const char *root, const char *arguments, size_t *bytes)
{
    char request[512];
    snprintf(request, sizeof request, "{\"contract\":1,\"operation\":\"search_nodes\",%s}", arguments);
    int exit_code = 0;
    char *response = chibi_automation_handle(request, root, &exit_code);
    *bytes = response ? strlen(response) : 0;
    return response;
}

static char *search(const char *arguments, size_t *bytes)
{
    return search_in(".", arguments, bytes);
}

static bool same(const char *text, const char *expected)
{
    return expected ? text && !strcmp(text, expected) : !text;
}

/* A result names a component or struct in its setting, or an event. */
static bool means(yyjson_val *node, const intent *wanted)
{
    yyjson_val *inputs = yyjson_obj_get(node, "inputs");
    const char *name = yyjson_get_str(yyjson_obj_get(inputs, "Component"));
    name = name ? name : yyjson_get_str(yyjson_obj_get(inputs, "Type"));
    name = name ? name : yyjson_get_str(yyjson_obj_get(node, "event_name"));
    name = name ? name : yyjson_get_str(yyjson_obj_get(node, "reference_name"));
    return same(yyjson_get_str(yyjson_obj_get(node, "type")), wanted->type) &&
           same(name, wanted->name) &&
           same(yyjson_get_str(yyjson_obj_get(node, "field")), wanted->field);
}

/* Each intent's node is among the first three results. */
static void intents_rank()
{
    int found = 0;
    for (size_t i = 0; i < sizeof INTENTS / sizeof *INTENTS; i++) {
        const intent *wanted = &INTENTS[i];
        char arguments[256];
        snprintf(
            arguments,
            sizeof arguments,
            "\"query\":\"%s\",\"domain\":\"%s\",\"limit\":3,\"include_pins\":"
            "false",
            wanted->query,
            wanted->domain
        );
        size_t bytes;
        char *response = search(arguments, &bytes);
        yyjson_doc *doc = yyjson_read(response, bytes, 0);
        yyjson_val *nodes = yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(doc), "result"), "nodes");
        bool hit = false;
        size_t at, max;
        yyjson_val *node;
        yyjson_arr_foreach(nodes, at, max, node)
        {
            hit |= means(node, wanted);
        }
        if (!hit) {
            printf("  \"%s\" missed %s: %s\n", wanted->query, wanted->type, response);
        }
        found += hit;
        yyjson_doc_free(doc);
        free(response);
    }
    test_check(found == (int)(sizeof INTENTS / sizeof *INTENTS), "searchNodes ranks the node an agent's query means in its first three results");
}

/* Behavior search offers component nodes, not the generated nodes they lower
 * to; System graphs name generated nodes directly. */
static void behavior_vocabulary()
{
    size_t bytes;
    char *behavior = search("\"query\":\"velocity\",\"domain\":\"behavior\",\"limit\":50", &bytes);
    char *system = search("\"query\":\"velocity\",\"domain\":\"system\",\"limit\":50", &bytes);
    test_check(
        behavior && !strstr(behavior, "\"Get_GameVelocity\"") &&
            strstr(behavior, "\"Get Component\"") && system &&
            strstr(system, "\"Get_GameVelocity\"") &&
            !strstr(system, "\"Get Component\""),
        "searchNodes offers component nodes to behaviors and generated nodes "
        "to systems"
    );
    free(behavior);
    free(system);
}

/* A result shows how Behavior Expression writes its node. */
static void expression_forms()
{
    size_t bytes;
    char *clamp = search("\"query\":\"clamp\",\"domain\":\"behavior\",\"limit\":1", &bytes);
    char *smooth = search("\"query\":\"smoothstep\",\"domain\":\"shader\",\"limit\":1", &bytes);
    test_check(
        clamp && strstr(clamp, "\"expression\":\"clamp(Value, Min, Max)\"") && smooth &&
            strstr(smooth, "\"expression\":\"smoothstep(Min, Max, Value)\""),
        "searchNodes gives the expression that writes a node, in argument order"
    );
    free(clamp);
    free(smooth);
}

/* A wire's type filters and ranks nodes by the pin that takes it, like a
 * dropped wire in Studio: exact types first, then conversions, then
 * wildcards. */
static void wire_fits()
{
    size_t bytes;
    char *destroy = search("\"query\":\"destroy\",\"wire_from\":\"ecs.entity\",\"include_pins\":false", &bytes);
    char *health = search("\"wire_to\":\"GameHealth\",\"domain\":\"behavior\",\"limit\":1,\"include_pins\":false", &bytes);
    char *after = search("\"query\":\"delay\",\"wire_from\":\"exec\",\"limit\":1,\"include_pins\":false", &bytes);
    char *real = search("\"wire_from\":\"int\",\"wire_to\":\"float\",\"limit\":1,\"include_pins\":false", &bytes);
    char *unknown = search("\"wire_from\":\"GameNothing\"", &bytes);
    test_check(
        destroy && strstr(destroy, "\"type\":\"Destroy Entity\"") &&
            strstr(destroy, "\"wire_input\":\"Entity\"") && health &&
            strstr(health, "\"wire_output\":\"GameHealth\"") && after &&
            strstr(after, "\"type\":\"Delay\"") &&
            strstr(after, "\"wire_input\":\"Exec\"") && real &&
            strstr(real, "\"type\":\"To Real\"") && unknown &&
            strstr(unknown, "\"invalid_request\""),
        "searchNodes ranks nodes by the pin that takes a wire of a given type"
    );
    free(destroy);
    free(health);
    free(after);
    free(real);
    free(unknown);
}

/* The names searchNodes returns: result node types, or outline names. */
static const char **listed_names(const char *response)
{
    yyjson_doc *doc = yyjson_read(response, strlen(response), 0);
    yyjson_val *result = yyjson_obj_get(yyjson_doc_get_root(doc), "result");
    const char **names = NULL;
    size_t at, max;
    yyjson_val *item, *category;
    yyjson_arr_foreach(yyjson_obj_get(result, "nodes"), at, max, item)
    {
        apush(
            names,
            registry_node_by_name(yyjson_get_str(yyjson_obj_get(item, "type")))->name
        );
    }
    yyjson_obj_iter categories = yyjson_obj_iter_with(yyjson_obj_get(result, "categories"));
    while ((category = yyjson_obj_iter_next(&categories))) {
        yyjson_arr_foreach(yyjson_obj_iter_get_val(category), at, max, item)
        {
            apush(names, registry_node_by_name(yyjson_get_str(item))->name);
        }
    }
    yyjson_doc_free(doc);
    return names;
}

/* Every node the outline offers, in every domain, and every node System
 * search finds describes itself. */
static void descriptions_complete()
{
    const char *const requests[] = {
        "\"include_pins\":false",
        "\"domain\":\"system\"",
        "\"query\":\"health\",\"domain\":\"system\",\"limit\":50,\"include_"
        "pins\":false",
        "\"query\":\"transform\",\"domain\":\"system\",\"limit\":50,\"include_"
        "pins\":false",
    };
    int missing = 0;
    for (size_t r = 0; r < sizeof requests / sizeof *requests; r++) {
        size_t bytes;
        char *response = search(requests[r], &bytes);
        const char **names = listed_names(response);
        for (int i = 0; i < acount(names); i++) {
            const node_desc *desc = registry_node_by_name(names[i]);
            if (!desc->description || !desc->description[0]) {
                printf("  %s has no description\n", desc->name);
                missing++;
            }
        }
        afree(names);
        free(response);
    }
    test_check(!missing, "every node searchNodes lists has a description");
}

/* System search offers what the System compiler lowers: Get_ and Set_ of a
 * whole component, Make_ and Break_, but not field or entity setters, Has_
 * or Add_. */
static void system_offers()
{
    size_t bytes;
    char *outline = search("\"domain\":\"system\"", &bytes);
    char *health = search("\"query\":\"health\",\"domain\":\"system\",\"limit\":50,\"include_pins\":false", &bytes);
    const char **names = listed_names(outline);
    const char **found = listed_names(health);
    int unsupported = 0;
    for (int i = 0; i < acount(names); i++) {
        unsupported += !game_system_node_supported(registry_node_by_name(names[i]));
    }
    for (int i = 0; i < acount(found); i++) {
        unsupported += !game_system_node_supported(registry_node_by_name(found[i]));
    }
    test_check(
        !unsupported && acount(names) && strstr(health, "\"Get_GameHealth\"") &&
            strstr(health, "\"Set_GameHealth\"") &&
            strstr(health, "\"Make_GameHealth\"") &&
            strstr(health, "\"Break_GameHealth\"") &&
            !strstr(health, "Set_GameHealth_current") &&
            !strstr(health, "GetEntity_GameHealth") &&
            !strstr(health, "Has_GameHealth") &&
            !strstr(health, "Add_GameHealth") && !strstr(outline, "\"Branch\""),
        "searchNodes offers System graphs only the nodes their compiler lowers"
    );
    afree(names);
    afree(found);
    free(outline);
    free(health);
}

/* inspectGraph lists each node's pins with the types wildcards resolved to,
 * which the serialized nodes omit. */
static void inspected_pins()
{
    char root[] = "/tmp/chibi-inspect-XXXXXX";
    assert(mkdtemp(root));
    char manifest[128], content[128], behaviors[128], asset[160];
    snprintf(manifest, sizeof manifest, "%s/" CHIBI_PROJECT_MANIFEST, root);
    snprintf(content, sizeof content, "%s/content", root);
    snprintf(behaviors, sizeof behaviors, "%s/content/behaviors", root);
    snprintf(asset, sizeof asset, "%s/Pins.yaml", behaviors);
    FILE *file = fopen(manifest, "w");
    assert(file);
    fprintf(file, "version: %d\ncontent: content\n", CHIBI_PROJECT_VERSION);
    fclose(file);
    assert(!mkdir(content, 0755) && !mkdir(behaviors, 0755));
    char request[1024];
    snprintf(
        request,
        sizeof request,
        "{\"contract\":1,\"operation\":\"edit_graph\",\"project_root\":\"%s\","
        "\"kind\":\"behavior\",\"name\":\"Pins\",\"create\":true,"
        "\"operations\":["
        "{\"op\":\"add_node\",\"ref\":\"two\",\"type\":\"Real\",\"inputs\":{"
        "\"Value\":2}},"
        "{\"op\":\"add_node\",\"ref\":\"sum\",\"type\":\"Add\"},"
        "{\"op\":\"connect\",\"from\":\"two\",\"from_pin\":\"Value\",\"to\":"
        "\"sum\",\"to_pin\":\"A\"}]}",
        root
    );
    int exit_code = 0;
    char *edited = chibi_automation_handle(request, ".", &exit_code);
    snprintf(
        request,
        sizeof request,
        "{\"contract\":1,\"operation\":\"inspect_graph\",\"project_root\":\"%"
        "s\","
        "\"kind\":\"behavior\",\"name\":\"Pins\"}",
        root
    );
    char *inspected = chibi_automation_handle(request, ".", &exit_code);
    test_check(
        edited && strstr(edited, "\"ok\":true") && inspected &&
            strstr(inspected, "\"type\":\"Add\"") &&
            strstr(inspected, "\"in\":[\"A: double\",") &&
            strstr(inspected, "\"out\":[\"Result: double\"]"),
        "inspectGraph lists each node's pins with their resolved types"
    );
    free(edited);
    free(inspected);
    unlink(asset);
    rmdir(behaviors);
    rmdir(content);
    unlink(manifest);
    rmdir(root);
}

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    assert(file);
    fputs(text, file);
    fclose(file);
}

/* Elements whose handlers emit events, or with data-keys, in a ui document
 * offer their events to behaviors, saying when each is sent. The player shell
 * takes the pause menu's own. */
static void ui_events()
{
    char root[] = "/tmp/chibi-ui-events-XXXXXX";
    assert(mkdtemp(root));
    char manifest[128], content[128], ui[128], hud[160], pause[160];
    snprintf(manifest, sizeof manifest, "%s/" CHIBI_PROJECT_MANIFEST, root);
    snprintf(content, sizeof content, "%s/content", root);
    snprintf(ui, sizeof ui, "%s/content/ui", root);
    snprintf(hud, sizeof hud, "%s/Hud.rml", ui);
    snprintf(pause, sizeof pause, "%s/PauseMenu.rml", ui);
    assert(!mkdir(content, 0755) && !mkdir(ui, 0755));
    char version[64];
    snprintf(version, sizeof version, "version: %d\ncontent: content\n", CHIBI_PROJECT_VERSION);
    write_text(manifest, version);
    write_text(
        hud,
        "<div class=\"button\" id=\"go\" "
        "data-event-click=\"emit('Go')\">Go</div>"
        "<input type=\"range\" id=\"volume\" "
        "data-event-change=\"emit('Volume')\"/>"
        "<div class=\"menu wide\" data-keys=\"Enter:Choose Space:Choose\"/>"
        "<div data-event-click=\"emit('Tick')\"/>\n"
    );
    write_text(pause, "<div data-event-click=\"emit('Resume')\">Resume</div>\n");
    size_t bytes;
    char *outline = search_in(root, "\"domain\":\"behavior\"", &bytes);
    char *go = search_in(root, "\"query\":\"go\",\"limit\":1", &bytes);
    char *volume = search_in(root, "\"query\":\"volume\",\"limit\":1", &bytes);
    char *choose = search_in(root, "\"query\":\"choose\",\"limit\":1", &bytes);
    char *button = search_in(root, "\"query\":\"button\",\"limit\":3", &bytes);
    char *resume = search_in(root, "\"query\":\"resume\"", &bytes);
    test_check(
        outline &&
            strstr(outline, "\"Contact Begin\",\"Go\",\"Volume\",\"Choose\"]") && go &&
            strstr(go, "\"type\":\"Event\",\"event_name\":\"Go\",\"description\":\"Sent when the player clicks #go in Hud.\"") &&
            volume &&
            strstr(volume, "\"description\":\"Sent when the player changes #volume in Hud.\"") &&
            choose &&
            strstr(choose, "\"description\":\"Sent when the player presses Enter or Space inside div.menu in Hud.\"") &&
            button && strstr(button, "\"event_name\":\"Go\"") && resume &&
            !strstr(resume, "\"Resume\""),
        "searchNodes offers the events ui documents send behaviors, saying when"
    );
    free(outline);
    free(go);
    free(volume);
    free(choose);
    free(button);
    free(resume);
    unlink(hud);
    unlink(pause);
    rmdir(ui);
    rmdir(content);
    unlink(manifest);
    rmdir(root);
}

/* Given the behavior being edited, searchNodes offers only the events of
 * the ui documents on what attaches it, which are the ones that reach it. */
static void behavior_ui_events()
{
    char root[] = "/tmp/chibi-behavior-events-XXXXXX";
    assert(mkdtemp(root));
    static const char *const FOLDERS[] = { "content",
                                           "content/ui",
                                           "content/prototypes",
                                           "content/behaviors" };
    static const char *const FILES[][2] = {
        { "content/ui/Hud.rml", "<div data-event-click=\"emit('Go')\">Go</div>\n" },
        { "content/ui/Shop.rml", "<div data-event-click=\"emit('Buy')\">Buy</div>\n" },
        { "content/prototypes/Hero.yaml", "name: Hero\ncomponents:\n  GAME.GameUi: {document: Hud}\nbehaviors: [Player]\n" },
        { "content/behaviors/Player.yaml", "version: 3\nname: Player\nid_counter: 1\nowner_type: ecs.entity\ngraphs: []\n" },
    };
    char path[256], version[64];
    snprintf(path, sizeof path, "%s/" CHIBI_PROJECT_MANIFEST, root);
    snprintf(version, sizeof version, "version: %d\ncontent: content\n", CHIBI_PROJECT_VERSION);
    write_text(path, version);
    for (size_t i = 0; i < sizeof FOLDERS / sizeof *FOLDERS; i++) {
        snprintf(path, sizeof path, "%s/%s", root, FOLDERS[i]);
        assert(!mkdir(path, 0755));
    }
    for (size_t i = 0; i < sizeof FILES / sizeof *FILES; i++) {
        snprintf(path, sizeof path, "%s/%s", root, FILES[i][0]);
        write_text(path, FILES[i][1]);
    }
    size_t bytes;
    char *go = search_in(root, "\"query\":\"go\",\"behavior\":\"Player\"", &bytes);
    char *buy = search_in(root, "\"query\":\"buy\",\"behavior\":\"Player\"", &bytes);
    char *every = search_in(root, "\"query\":\"buy\"", &bytes);
    char *missing = search_in(root, "\"query\":\"go\",\"behavior\":\"Nobody\"", &bytes);
    test_check(
        go && strstr(go, "\"event_name\":\"Go\"") && buy &&
            !strstr(buy, "\"event_name\":\"Buy\"") && every &&
            strstr(every, "\"event_name\":\"Buy\"") && missing &&
            strstr(missing, "no behavior named Nobody"),
        "searchNodes offers a behavior only the UI events of the documents on "
        "what attaches it"
    );
    free(go);
    free(buy);
    free(every);
    free(missing);
    for (int i = (int)(sizeof FILES / sizeof *FILES) - 1; i >= 0; i--) {
        snprintf(path, sizeof path, "%s/%s", root, FILES[i][0]);
        unlink(path);
    }
    for (int i = (int)(sizeof FOLDERS / sizeof *FOLDERS) - 1; i >= 0; i--) {
        snprintf(path, sizeof path, "%s/%s", root, FOLDERS[i]);
        rmdir(path);
    }
    snprintf(path, sizeof path, "%s/" CHIBI_PROJECT_MANIFEST, root);
    unlink(path);
    rmdir(root);
}

/* Responses stay small enough to read whole: the vocabulary outline, and a
 * page of results with pins. */
static void response_budgets()
{
    size_t outline, page;
    char *listed = search("\"domain\":\"behavior\"", &outline);
    char *found = search("\"query\":\"health\"", &page);
    test_check(
        listed && strstr(listed, "\"categories\"") &&
            strstr(listed, "\"GameHealth\"") && strstr(listed, "\"Contact Begin\"") &&
            outline < 6144 && found && page < 2560,
        "searchNodes lists the behavior vocabulary in under 6 KB and a page of "
        "pins in under 2.5 KB"
    );
    free(listed);
    free(found);
}

/* Word search skips letters scattered through a name, but keeps typos; ranking
 * prefers whole names, then prefixes, then words inside names. */
static void text_search_scores()
{
    assert(text_search_score("  ", "Player", NULL) == 0);
    assert(text_search_score("hit", "Switch on Int", NULL) >= 0);
    assert(text_search_word_score("hit", "Switch on Int", NULL) < 0);
    assert(text_search_word_score("playre", "Player", NULL) >= 0);
    assert(text_search_word_score("gate", "GameHealth", NULL) < 0);
    assert(text_search_word_score("if", "lifetime", NULL) < 0);
    assert(text_search_word_score("aevb", "ArenaEnemyVisualBehavior", NULL) < 0);
    assert(
        text_search_score(" \tplayer  ", "Player", NULL) ==
        text_search_score("player", "Player", NULL)
    );
    assert(
        text_search_score("player", "Player", NULL) >
        text_search_score("player", "PlayerMovement", NULL)
    );
    assert(
        text_search_score("player", "PlayerMovement", NULL) >
        text_search_score("player", "ArenaPlayer", NULL)
    );
    assert(
        text_search_score("player", "ArenaPlayer", NULL) >
        text_search_score("player", "PlaybackRenderer", NULL)
    );
    assert(
        text_search_score("player", "Player", NULL) >
        text_search_score("player", "Spawn", "player")
    );
    assert(text_search_score("lerp", "Linear Interpolate", "blend lerp") >= 0);
    assert(text_search_score("math blend", "Linear Interpolate", "Math blend lerp") >= 0);
    assert(
        text_search_score("aevb", "ArenaEnemyVisualBehavior", NULL) >
        text_search_score("aevb", "ArenaEveryVerb", NULL)
    );
    assert(text_search_score("playr", "Player", NULL) >= 0);
    assert(text_search_score("playyer", "Player", NULL) >= 0);
    assert(text_search_score("playar", "Player", NULL) >= 0);
    assert(text_search_score("plsyr", "Player", NULL) < 0);
    assert(
        text_search_score("player arena", "PlayerContactBehavior", "content/behaviors/PlayerContactBehavior.yaml") <
        0
    );
    assert(text_search_score("cat", "Cast", NULL) >= 0);
    assert(text_search_score("cot", "Cat", NULL) < 0);
}

/* Asset search ranks as Studio's palette does: a name that is the query
   first, then names that start with it, then matches in paths. */
static void assets_rank()
{
    int exit_code = 0;
    char *response = chibi_automation_handle(
        "{\"contract\":1,\"operation\":\"search_assets\",\"query\":\"health "
        "behavior\","
        "\"summary\":true}",
        ".",
        &exit_code
    );
    yyjson_doc *doc = yyjson_read(response, strlen(response), 0);
    yyjson_val *assets = yyjson_ptr_get(yyjson_doc_get_root(doc), "/result/assets");
    const char *first = yyjson_get_str(yyjson_obj_get(yyjson_arr_get(assets, 0), "name"));
    bool ranked = exit_code == 0 && first && !strcmp(first, "HealthBehavior");
    if (!ranked) {
        printf("  assets: first %s of %zu\n", first ? first : "(none)", yyjson_arr_size(assets));
    }
    test_check(ranked, "searchAssets ranks the asset its name matches first, as Studio's palette does");
    yyjson_doc_free(doc);
    free(response);
}

void test_automation_search()
{
    text_search_scores();
    assets_rank();
    intents_rank();
    behavior_vocabulary();
    expression_forms();
    wire_fits();
    descriptions_complete();
    system_offers();
    inspected_pins();
    ui_events();
    behavior_ui_events();
    response_budgets();
}
