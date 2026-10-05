/* A game document's events reach its entity's behaviors in the update that
 * follows the click, with the payload the event asset declares (render/ui.h
 * ui_activation_send), so what a behavior sets shows in the same update. */
#include "graph/document/events.h"
#include "graph/document/serialize.h"
#include "render/ui.h"
#include "runtime/behaviors/live.h"
#include "runtime/generated.h"
#include "test/suite_internal.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUMPED                                                                 \
    "version 3\nname \"Bumped\"\ndomain \"event\"\nid_counter 7\n"             \
    "(array)variables {\n"                                                     \
    "    - id=1 name=\"Shift\" type=\"bool\" category=\"Payload\"\n"           \
    "    - id=2 name=\"Mod\" type=\"bool\" category=\"Payload\"\n"             \
    "    - id=3 name=\"Pointer\" type=\"struct\" type_name=\"CF_V2\" "         \
    "category=\"Payload\"\n"                                                   \
    "    - id=4 name=\"Label\" type=\"const char *\" category=\"Payload\"\n"   \
    "    - id=5 name=\"Count\" type=\"int\" category=\"Payload\"\n"            \
    "    - id=6 name=\"Ratio\" type=\"float\" category=\"Payload\"\n"          \
    "}\n(array)graphs {\n    - kind=\"event\" name=\"\"\n}\n"

/* A payload a UI event cannot carry: a list. */
#define LISTED                                                                 \
    "version 3\nname \"Listed\"\ndomain \"event\"\nid_counter 2\n"             \
    "(array)variables {\n"                                                     \
    "    - id=1 name=\"Items\" type=\"int\" container=1 "                      \
    "category=\"Payload\"\n"                                                   \
    "}\n(array)graphs {\n    - kind=\"event\" name=\"\"\n}\n"

static void click(render_ui *ui, float x, float y, unsigned modifiers)
{
    rml_input_type types[] = { RML_INPUT_POINTER_MOVE,
                               RML_INPUT_POINTER_DOWN,
                               RML_INPUT_POINTER_UP };
    for (size_t i = 0; i < sizeof types / sizeof *types; i++) {
        render_ui_input(ui, &(rml_input){ .type = types[i], .x = x, .y = y, .modifiers = modifiers });
    }
}

static void update(render_ui *ui, const ui_world *w)
{
    render_ui_update(ui, w, 1280, 720, 1.0 / 60);
}

static int output_pin(const node *n, const char *name)
{
    for (int i = 0; i < n->output_count; i++) {
        if (!strcmp(n->output_pins[i].name, name)) {
            return i;
        }
    }
    assert(!"The event node declares the payload pin");
    return -1;
}

/* Bumped's handler: it shows the event's Label in #out, then records the
   event. */
static behavior_asset *bump_handler(const behavior_asset *definition)
{
    behavior_asset *asset = behavior_asset_new("BumpHandler");
    assert(asset && behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    graph *g = asset->graph;
    node *event = graph_add_node(g, registry_node_by_name("Event"));
    assert(event && graph_set_event_name(g, event, "Bumped") == 0);
    assert(graph_event_apply(g, event, definition) == 0);
    node *text = graph_add_node(g, registry_node_by_name("Set UI Text"));
    pin_value element = { .type = PIN_STR, .type_name = "const char *", .str = "#out" };
    assert(text && graph_set_pin_default(g, text, 0, 2, &element) == 0);
    assert(graph_connect(g, event, output_pin(event, "Label"), text, 3) == 0);
    assert(graph_connect_exec(g, event, 0, text, 0, 0) == 0);
    node *record = graph_add_node(g, test_ui_probe_node("Bumped"));
    assert(record && graph_connect_exec(g, text, 0, record, 0, 0) == 0);
    return asset;
}

static bool text_is(render_ui *ui, const char *selector, const char *expected)
{
    rml_element *element = rml_element_query_selector(
        rml_context_document(render_ui_context(ui), 0),
        selector
    );
    char text[64] = "";
    return element && rml_element_inner_rml(element, text, sizeof text) &&
           !strcmp(text, expected);
}

void test_game_ui_events()
{
    char root[] = "/tmp/chibi-game-ui-events-XXXXXX", folder[64];
    assert(mkdtemp(root));
    snprintf(folder, sizeof folder, "%s/ui", root);
    assert(mkdir(folder, 0700) == 0);
    test_write_ui(
        folder,
        "Probe",
        "<div id=\"bump\" data-event-click=\"emit('Bumped', 'Gem', 3, "
        "0.5)\">Bump</div>\n"
        "<div id=\"plain\" data-event-click=\"emit('Plain')\">Plain</div>\n"
        "<div id=\"listed\" data-event-click=\"emit('Listed')\">Listed</div>\n"
        "<div id=\"out\">idle</div>\n",
        "div { display: block; width: 200dp; height: 40dp; }\n"
    );
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(world);
    const char *definitions[] = { BUMPED, LISTED };
    for (int i = 0; i < 2; i++) {
        behavior_asset *event = behavior_asset_load(definitions[i]);
        assert(
            event &&
            game_live_add_definition(live, game_live_definition_new(event->name, event)) == 0
        );
    }
    ecs_entity panel = ecs_world_create_entity(world);
    assert(GAME_GameUi_add(world, panel, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameUi_get(world, panel) = (GameUi){ .document = "Probe" };
    ui_probe bumped = { 0 }, plain = { 0 };
    const game_live_definition *declared = game_live_find_definition(live, "Bumped");
    test_ui_probe_attach(live, panel, bump_handler(declared->working), &bumped);
    const char *const plain_events[] = { "Plain" };
    test_ui_probe_events(live, panel, plain_events, 1, &plain);
    ui_world w = { .ecs = world, .model = game_behavior_ui(live->behaviors), .live = live };
    render_ui *ui = render_ui_new(root, NULL);
    update(ui, &w);
    update(ui, &w);
    bool idle = text_is(ui, "#out", "idle") && !render_ui_error(ui)[0];

    /* One update after a click with Shift held: the handler ran, with the
       payload typed as the event asset declares it, and #out shows what it
       set. */
    click(ui, 40, 20, RML_INPUT_MOD_SHIFT);
    update(ui, &w);
    bool ran = bumped.count == 1 && bumped.values == 6 &&
               !game_entities_pending_events(live->entities);
    bool flags = ran && bumped.payload[0].type == PIN_BOOL && bumped.payload[0].b &&
                 bumped.payload[1].type == PIN_BOOL && !bumped.payload[1].b;
    bool pointer = ran && bumped.payload[2].type == PIN_STRUCT &&
                   fabsf(bumped.pointer[0] - 40) <= 1 &&
                   fabsf(bumped.pointer[1] - 20) <= 1;
    bool arguments = ran && bumped.payload[3].type == PIN_STR &&
                     !strcmp(bumped.payload[3].str, "Gem") &&
                     bumped.payload[4].type == PIN_I32 && bumped.payload[4].i == 3 &&
                     bumped.payload[5].type == PIN_F32 && bumped.payload[5].f == 0.5f;
    bool shown = text_is(ui, "#out", "Gem");
    if (!(idle && ran && flags && pointer && arguments && shown)) {
        printf("  %d %d %d %d %d %d\n", idle, ran, flags, pointer, arguments, shown);
    }
    test_check(
        idle && ran && flags && pointer && arguments && shown,
        "a game document's event runs its behavior in the update after the "
        "click, with Shift, Pointer and emit's arguments typed as its event "
        "asset declares, and the document shows what it set in that update"
    );

    /* Control is Mod in a game, as on any platform whose hosts trade
       Command for it. */
    click(ui, 40, 20, RML_INPUT_MOD_CONTROL);
    update(ui, &w);
    test_check(bumped.count == 2 && !bumped.payload[0].b && bumped.payload[1].b, "a click's Control modifier is the event's Mod");

    /* An event no asset declares is local: it carries no payload. */
    click(ui, 40, 60, 0);
    update(ui, &w);
    test_check(
        plain.count == 1 && plain.values == 0 && !strcmp(plain.event, "Plain") &&
            !game_entities_pending_events(live->entities),
        "an event without an event asset reaches its behavior with no payload"
    );

    /* A payload a UI event cannot carry runs nothing and says why. */
    click(ui, 40, 100, 0);
    update(ui, &w);
    test_check(
        strstr(render_ui_error(ui), "Listed declares a payload") &&
            plain.count == 1 && bumped.count == 2,
        "an event whose payload a UI event cannot carry reports it and runs "
        "nothing"
    );
    update(ui, &w);
    test_check(!render_ui_error(ui)[0], "that report ends with the update that made it");

    render_ui_free(ui);
    game_live_world_free(live);
    test_ui_probes_free();
    ecs_world_free(world);
    test_remove_ui(folder, "Probe");
    rmdir(folder);
    rmdir(root);
}
