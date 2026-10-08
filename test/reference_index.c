#include "authoring/reference_index.h"

#include "assets/call_hierarchy.h"
#include "assets/prototype.h"
#include "assets/reference_adapters.h"
#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/authoring/local_edit.h"
#include "graph/document/graph_signature.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/component_nodes.h"
#include "runtime/component_catalog.h"
#include "support/array.h"
#include "test/asset_fixture.h"

#include <assert.h>
#include <string.h>

static int pin_named(const node *n, int output, const char *name)
{
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    for (int i = 0; i < (output ? n->output_count : n->input_count); i++) {
        if (!strcmp(pins[i].name, name)) {
            return i;
        }
    }
    assert(!"pin exists");
    return -1;
}

static node *add_named_node(graph *g, const char *type)
{
    node *n = graph_add_node(g, registry_node_by_name(type));
    assert(n);
    return n;
}

static node *add_event_node(graph *g, const char *type, const char *event)
{
    node *n = add_named_node(g, type);
    assert(graph_set_node_reference(g, n, 0, event) == 0);
    return n;
}

static void set_default(graph *g, node *n, const char *pin, pin_value value)
{
    assert(graph_set_pin_default(g, n, 0, pin_named(n, 0, pin), &value) == 0);
}

/* The uses of symbol in role, each checked to sit on node. */
static int uses(const reference_index *index, reference_symbol symbol, reference_role role, const node *n)
{
    const reference **found = NULL;
    reference_index_find_uses(index, symbol, &found);
    int count = 0;
    for (int i = 0; i < acount(found); i++) {
        if (found[i]->role == role && (!n || found[i]->node_id == n->id)) {
            count++;
        }
    }
    afree(found);
    return count;
}

static reference_symbol named(reference_symbol_kind kind, const char *name, const char *field)
{
    return (reference_symbol){ .kind = kind, .asset = -1, .name = name, .field = field };
}

static reference_symbol asset_symbol(int asset)
{
    return (reference_symbol){ .kind = REFERENCE_SYMBOL_ASSET, .asset = asset };
}

/* Paths and keys keep an asset one asset; names resolve what was named. */
static void test_assets()
{
    reference_index *index = reference_index_new();
    int prototype = reference_index_add_asset(
        index,
        REFERENCE_ASSET_PROTOTYPE,
        "Ship",
        "content/prototypes/Ship.kdl",
        NULL
    );
    int material = reference_index_add_asset(index, REFERENCE_ASSET_MATERIAL, "Ship", "content/materials/Ship.kdl", NULL);
    assert(prototype != material);
    assert(reference_index_find_name(index, REFERENCE_ASSET_MATERIAL, "Ship") == material);
    assert(
        reference_index_find_asset(index, REFERENCE_ASSET_PROTOTYPE, "./content/prototypes/Ship.kdl", NULL) ==
        prototype
    );
    /* An unsaved asset keeps its key while renamed, then takes its path. */
    int scratch = reference_index_add_asset(index, REFERENCE_ASSET_BEHAVIOR, "Scratch", NULL, "tab:1");
    assert(
        reference_index_add_asset(index, REFERENCE_ASSET_BEHAVIOR, "Renamed", NULL, "tab:1") == scratch
    );
    assert(!strcmp(reference_index_asset_at(index, scratch)->name, "Renamed"));
    assert(
        reference_index_add_asset(index, REFERENCE_ASSET_BEHAVIOR, "Renamed", "content/behaviors/Renamed.chibi", NULL) ==
        scratch
    );
    assert(reference_index_asset_at(index, scratch)->path);
    reference_index_clear(index);
    assert(reference_index_asset_count(index) == 0 && reference_index_count(index) == 0);
    reference_index_free(index);
}

typedef struct
{
    behavior_asset *asset;
    graph *heal;
    behavior_variable *health;
    node *call, *get, *set, *handle, *tick, *send, *spawn, *getter, *setter,
        *split, *shader;
    uint64_t amount; /* Heal's parameter */
    uint64_t local;
} healer;

/* A behavior that uses one of every kind of symbol. */
static healer healer_new()
{
    healer h = { .asset = behavior_asset_new("Healer") };
    graph *g = h.asset->graph;
    h.heal = behavior_asset_add_function(h.asset, "Heal", false);
    pin_spec amount = { .name = "Amount", .type = PIN_I32, .type_name = "int" };
    assert(graph_signature_add(h.asset, h.heal, false, &amount) == 0);
    node *entry = graph_node_by_id(h.heal, h.heal->entry_id);
    h.amount = entry->output_pins[pin_named(entry, 1, "Amount")].id;
    h.health = behavior_asset_add_variable(h.asset, "health", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, NULL);
    node *store = behavior_asset_add_set_node_to_graph(h.asset, h.heal, h.health->id);
    assert(
        graph_connect_data(h.heal, entry, pin_named(entry, 1, "Amount"), store, pin_named(store, 0, "Value")) ==
        0
    );

    h.call = behavior_asset_add_function_call_node(h.asset, h.heal);
    h.get = behavior_asset_add_get_node(h.asset, h.health->id);
    h.set = behavior_asset_add_set_node(h.asset, h.health->id);
    assert(graph_connect_data(g, h.get, 0, h.call, pin_named(h.call, 0, "Amount")) == 0);
    h.local = graph_create_local(g, h.get, 0, "hp");
    assert(h.local);
    h.handle = add_event_node(g, "Event", "Damaged");
    h.tick = add_event_node(g, "Event", "Tick");
    h.send = add_event_node(g, "Send Event", "Healed");
    h.spawn = add_named_node(g, "Spawn Prototype");
    set_default(g, h.spawn, "Prototype", (pin_value){ .type = PIN_NAME, .str = "Pulse" });

    h.getter = graph_add_node(g, &NODE_GetComponent);
    set_default(g, h.getter, "Component", (pin_value){ .type = PIN_NAME, .str = "GameHealth" });
    assert(graph_expose_output_fields(g, h.getter, 0) == 0);
    h.setter = add_named_node(g, "Set_GameHealth_current");
    pin_binding maximum = { .kind = PIN_BINDING_COMPONENT_FIELD,
                            .component_key = "GAME.GameHealth",
                            .field_name = "maximum" };
    assert(graph_set_pin_binding(g, h.setter, 0, pin_named(h.setter, 0, "Value"), &maximum) == 0);
    h.split = graph_add_node(g, &NODE_SetComponent);
    set_default(g, h.split, "Component", (pin_value){ .type = PIN_NAME, .str = "GameUi" });
    assert(component_node_show_field(g, h.split, "document", true) == 0);
    set_default(g, h.split, "document", (pin_value){ .type = PIN_STR, .str = "Hud" });
    h.shader = add_named_node(g, "SetEntity_GameShader");
    GameShader glow = { .material = "Glow", .shape = "", .parameters = "" };
    set_default(
        g,
        h.shader,
        "Value",
        (pin_value){ .type = PIN_STRUCT, .type_name = "GameShader", .ptr = &glow }
    );
    return h;
}

static void test_graphs()
{
    healer h = healer_new();
    reference_index *index = reference_index_new();
    int event = reference_index_add_asset(index, REFERENCE_ASSET_EVENT, "Damaged", "content/events/Damaged.kdl", NULL);
    int source = reference_index_add_asset(
        index,
        REFERENCE_ASSET_BEHAVIOR,
        "Healer",
        "content/behaviors/Healer.chibi",
        NULL
    );
    behavior_asset *damaged = behavior_asset_new("Damaged");
    damaged->domain = BEHAVIOR_DOMAIN_EVENT_DEFINITION;
    reference_index_add_graphs(index, event, damaged);
    reference_index_add_graphs(index, source, h.asset);

    /* Functions, their parameters, variables and locals, within the asset. */
    reference_symbol heal = { .kind = REFERENCE_SYMBOL_GRAPH,
                              .asset = source,
                              .id = h.heal->graph_id };
    assert(uses(index, heal, REFERENCE_CALL, h.call) == 1);
    assert(reference_index_definition(index, heal)->graph_id == h.heal->graph_id);
    reference_symbol amount = { .kind = REFERENCE_SYMBOL_PARAMETER,
                                .asset = source,
                                .graph_id = h.heal->graph_id,
                                .id = h.amount };
    assert(uses(index, amount, REFERENCE_ARGUMENT, h.call) == 1);
    assert(uses(index, amount, REFERENCE_READ, NULL) == 1);
    assert(reference_index_definition(index, amount)->pin_id == h.amount);
    /* Definitions carry their names, and references their pins'. */
    assert(!strcmp(reference_index_definition(index, heal)->symbol.name, "Heal"));
    assert(!strcmp(reference_index_definition(index, amount)->symbol.name, "Amount"));
    const reference **arguments = NULL;
    reference_index_find_uses(index, amount, &arguments);
    assert(acount(arguments) == 2 && !strcmp(arguments[0]->pin, "Amount"));
    afree(arguments);
    reference_symbol health = { .kind = REFERENCE_SYMBOL_VARIABLE,
                                .asset = source,
                                .id = h.health->id };
    assert(uses(index, health, REFERENCE_READ, h.get) == 1);
    assert(uses(index, health, REFERENCE_WRITE, NULL) == 2);
    assert(reference_index_definition(index, health));
    reference_symbol hp = { .kind = REFERENCE_SYMBOL_LOCAL,
                            .asset = source,
                            .graph_id = h.asset->graph->graph_id,
                            .id = h.local };
    assert(uses(index, hp, REFERENCE_READ, h.call) == 1);
    assert(reference_index_definition(index, hp)->node_id == h.get->id);
    assert(!strcmp(reference_index_definition(index, hp)->symbol.name, "hp"));
    assert(!strcmp(reference_index_definition(index, health)->symbol.name, "health"));

    /* Events by name: a definition's belongs to its asset; host events and
       undeclared ones to none. */
    reference_symbol damage = named(REFERENCE_SYMBOL_EVENT, "Damaged", NULL);
    assert(uses(index, damage, REFERENCE_HANDLE, h.handle) == 1);
    assert(reference_index_definition(index, damage)->source == event);
    assert(uses(index, asset_symbol(event), REFERENCE_HANDLE, h.handle) == 1);
    assert(
        uses(index, named(REFERENCE_SYMBOL_EVENT, "Tick", NULL), REFERENCE_HANDLE, h.tick) == 1
    );
    assert(!reference_index_definition(index, named(REFERENCE_SYMBOL_EVENT, "Tick", NULL)));
    assert(
        uses(index, named(REFERENCE_SYMBOL_EVENT, "Healed", NULL), REFERENCE_SEND, h.send) == 1
    );
    assert(reference_index_find_name(index, REFERENCE_ASSET_EVENT, "Healed") < 0);

    /* Assets named in pins: a spawned prototype, a Set's ui document, a
       struct default's material. Each is unresolved until added. */
    int pulse = reference_index_find_name(index, REFERENCE_ASSET_PROTOTYPE, "Pulse");
    assert(pulse >= 0 && reference_index_asset_at(index, pulse)->unresolved);
    assert(uses(index, asset_symbol(pulse), REFERENCE_SPAWN, h.spawn) == 1);
    int hud = reference_index_find_name(index, REFERENCE_ASSET_UI, "Hud");
    assert(uses(index, asset_symbol(hud), REFERENCE_BIND, h.split) == 1);
    int glow = reference_index_find_name(index, REFERENCE_ASSET_MATERIAL, "Glow");
    assert(uses(index, asset_symbol(glow), REFERENCE_BIND, h.shader) == 1);

    /* Components and fields through nodes and bindings. */
    reference_symbol game_health = named(REFERENCE_SYMBOL_COMPONENT, "GameHealth", NULL);
    assert(uses(index, game_health, REFERENCE_READ, h.getter) == 3); /* the node and its two fields */
    reference_symbol current = named(REFERENCE_SYMBOL_FIELD, "GameHealth", "current");
    assert(uses(index, current, REFERENCE_READ, h.getter) == 1);
    assert(uses(index, current, REFERENCE_WRITE, h.setter) == 1);
    assert(
        uses(index, named(REFERENCE_SYMBOL_FIELD, "GameHealth", "maximum"), REFERENCE_READ, h.setter) == 1
    );
    assert(
        uses(index, named(REFERENCE_SYMBOL_FIELD, "GameUi", "document"), REFERENCE_WRITE, h.split) == 1
    );
    assert(
        uses(index, named(REFERENCE_SYMBOL_COMPONENT, "GameShader", NULL), REFERENCE_WRITE, h.shader) == 1
    );
    assert(!reference_index_definition(index, game_health));

    /* Counts by role; the asset's own symbols are not uses of the asset. */
    assert(reference_index_count_uses(index, health, REFERENCE_USES) == 3);
    assert(reference_index_count_uses(index, health, REFERENCE_ROLE(REFERENCE_WRITE)) == 2);
    assert(reference_index_count_uses(index, health, REFERENCE_ROLE(REFERENCE_DEFINE)) == 1);
    assert(reference_index_count_uses(index, asset_symbol(source), REFERENCE_USES) == 0);

    reference_index_free(index);
    behavior_asset_free(damaged);
    behavior_asset_free(h.asset);
}

/* Prototypes name behaviors and assets in component fields; a scene object
 * holds its prototype's unless it overrides them; ui documents use widgets
 * and send events. */
static void test_documents()
{
    game_prototype_registry registry = {
        .components = GAME_REGISTRY_COMPONENTS,
        .component_count = GAME_REGISTRY_COMPONENT_COUNT,
    };
    char error[256];
    game_prototype *ship = game_prototype_load(
        "name \"Ship\"\n"
        "(array)behaviors \"Flight\"\n"
        "components {\n"
        "    GameShader material=\"Hull\"\n"
        "    GameUi document=\"Hud\"\n"
        "    GameActionBinding action=\"Fire\"\n"
        "}\n",
        &registry,
        error,
        sizeof error
    );
    assert(ship);
    reference_index *index = reference_index_new();
    int prototype = reference_index_add_asset(
        index,
        REFERENCE_ASSET_PROTOTYPE,
        "Ship",
        "content/prototypes/Ship.kdl",
        NULL
    );
    int scene = reference_index_add_asset(index, REFERENCE_ASSET_SCENE, "Dock", "content/scenes/Dock.kdl", NULL);
    int hud = reference_index_add_asset(index, REFERENCE_ASSET_UI, "Hud", "content/ui/Hud.rml", NULL);
    reference_index_add_prototype(index, prototype, ship);
    int flight = reference_index_find_name(index, REFERENCE_ASSET_BEHAVIOR, "Flight");
    int hull = reference_index_find_name(index, REFERENCE_ASSET_MATERIAL, "Hull");
    assert(
        reference_index_count_uses(index, asset_symbol(flight), REFERENCE_ROLE(REFERENCE_ATTACH)) == 1
    );
    assert(reference_index_count_uses(index, asset_symbol(hud), REFERENCE_ROLE(REFERENCE_BIND)) == 1);
    assert(
        reference_index_count_uses(
            index,
            named(REFERENCE_SYMBOL_EVENT, "Fire", NULL),
            REFERENCE_ROLE(REFERENCE_SEND)
        ) == 1
    );

    const char *behaviors[] = { "Flight" };
    av_value *overrides = test_read_asset_value("GAME.GameShader material=\"Chrome\"\n");
    reference_object object = {
        .name = "player",
        .prototype = "Ship",
        .base = ship,
        .overrides = overrides,
        .behaviors = behaviors,
        .behavior_count = 1,
    };
    reference_index_add_object(index, scene, &object);
    av_value_free(overrides);
    const reference **found = NULL;
    reference_index_find_uses(index, asset_symbol(prototype), &found);
    assert(
        acount(found) == 1 && found[0]->role == REFERENCE_USE &&
        !strcmp(found[0]->object, "player")
    );
    asetlen(found, 0);
    int chrome = reference_index_find_name(index, REFERENCE_ASSET_MATERIAL, "Chrome");
    reference_index_find_uses(index, asset_symbol(chrome), &found);
    assert(
        acount(found) == 1 && found[0]->source == scene &&
        !strcmp(found[0]->component, "GameShader.material")
    );
    asetlen(found, 0);
    /* The override replaces the prototype's material; its ui stays. */
    reference_index_find_uses(index, asset_symbol(hull), &found);
    assert(acount(found) == 1 && found[0]->source == prototype);
    assert(reference_index_count_uses(index, asset_symbol(hud), REFERENCE_USES) == 2);
    afree(found);

    reference_index_add_ui(
        index,
        hud,
        "<use ui=\"Meter\" id=\"hull\"/>\n<div "
        "data-event-click=\"emit('Launch')\">Go</div>"
        "<div class=\"menu\" data-keys=\"Enter:Choose Down:Next\"></div>"
    );
    int meter = reference_index_find_name(index, REFERENCE_ASSET_UI, "Meter");
    assert(
        reference_index_count_uses(index, asset_symbol(meter), REFERENCE_ROLE(REFERENCE_USE)) == 1
    );
    const char *sent[] = { "Launch", "Choose", "Next" };
    for (int i = 0; i < 3; i++) {
        assert(
            reference_index_count_uses(
                index,
                named(REFERENCE_SYMBOL_EVENT, sent[i], NULL),
                REFERENCE_ROLE(REFERENCE_SEND)
            ) == 1
        );
    }
    reference_index_free(index);
    game_prototype_free(ship);
}

static const reference *handler(const reference_index *index, const char *event)
{
    const reference **found = NULL;
    reference_index_find_uses(index, named(REFERENCE_SYMBOL_EVENT, event, NULL), &found);
    const reference *handle = acount(found) ? found[0] : NULL;
    afree(found);
    assert(handle && handle->role == REFERENCE_HANDLE);
    return handle;
}

/* The child of item `at` of kind with this name, or -1. */
static int child(const call_item *items, int at, call_kind kind, const char *name)
{
    for (int i = items[at].first; i < items[at].first + items[at].count; i++) {
        if (items[i].kind == kind && items[i].name && !strcmp(items[i].name, name)) {
            return i;
        }
    }
    return -1;
}

/* A handler's run reaches what exec wires lead to from its Event node. A
 * function's callers climb through the handler whose run calls it to its
 * event; a host event's callees are its handlers and what they send, and a
 * function calling itself is a cycle. */
static void test_calls()
{
    healer h = healer_new();
    graph *g = h.asset->graph;
    assert(graph_connect_exec(g, h.handle, 0, h.call, 0, 0) == 0);
    assert(graph_connect_exec(g, h.tick, 0, h.send, 0, 0) == 0);
    assert(behavior_asset_add_function_call_node_to_graph(h.asset, h.heal, h.heal));
    reference_index *index = reference_index_new();
    int event = reference_index_add_asset(index, REFERENCE_ASSET_EVENT, "Damaged", "content/events/Damaged.kdl", NULL);
    int source = reference_index_add_asset(
        index,
        REFERENCE_ASSET_BEHAVIOR,
        "Healer",
        "content/behaviors/Healer.chibi",
        NULL
    );
    behavior_asset *damaged = behavior_asset_new("Damaged");
    damaged->domain = BEHAVIOR_DOMAIN_EVENT_DEFINITION;
    reference_index_add_graphs(index, event, damaged);
    reference_index_add_graphs(index, source, h.asset);
    const reference *on_damage = handler(index, "Damaged");
    assert(reference_index_reaches(index, on_damage, h.call->id));
    assert(!reference_index_reaches(index, on_damage, h.send->id));
    assert(reference_index_reaches(index, handler(index, "Tick"), h.send->id));

    reference_symbol heal = { .kind = REFERENCE_SYMBOL_GRAPH,
                              .asset = source,
                              .id = h.heal->graph_id };
    call_item *callers = call_hierarchy(index, heal, false, 2);
    int by_handler = child(callers, 0, CALL_HANDLER, "Damaged");
    int by_itself = child(callers, 0, CALL_FUNCTION, "Heal");
    assert(by_handler > 0 && callers[by_handler].node_id == h.handle->id);
    assert(
        acount(callers[by_handler].sites) == 1 &&
        callers[by_handler].sites[0]->node_id == h.call->id
    );
    int damage = child(callers, by_handler, CALL_EVENT, "Damaged");
    assert(damage > 0 && callers[damage].asset == event && !callers[damage].count);
    assert(by_itself > 0 && callers[by_itself].cycle && !callers[by_itself].count);
    call_hierarchy_free(callers);

    call_item *callees = call_hierarchy(index, named(REFERENCE_SYMBOL_EVENT, "Tick", NULL), true, 1);
    int tick = child(callees, 0, CALL_HANDLER, "Tick");
    assert(
        callees[0].asset == -1 && tick > 0 && callees[tick].more &&
        !callees[tick].count
    );
    call_hierarchy_free(callees);
    callees = call_hierarchy(index, named(REFERENCE_SYMBOL_EVENT, "Tick", NULL), true, 2);
    assert(child(callees, child(callees, 0, CALL_HANDLER, "Tick"), CALL_EVENT, "Healed") > 0);
    call_hierarchy_free(callees);

    reference_index_free(index);
    behavior_asset_free(damaged);
    behavior_asset_free(h.asset);
}

void test_reference_index()
{
    test_assets();
    test_graphs();
    test_documents();
    test_calls();
}
