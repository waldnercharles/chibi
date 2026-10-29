#include "authoring/reference_index.h"

#include "assets/call_hierarchy.h"
#include "assets/prototype.h"
#include "assets/reference_adapters.h"
#include "authoring/source_references.h"
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
#include "test/support/graph_packs.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

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
static int uses(const reference_index *refs, reference_symbol symbol, reference_role role, const node *n)
{
    const reference **found = NULL;
    reference_index_find_uses(refs, symbol, &found);
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

static reference_index *refs;
static behavior_asset *healer_asset, *damaged;
static prototype *ship;

static void cleanup()
{
    reference_index_free(refs);
    behavior_asset_free(healer_asset);
    behavior_asset_free(damaged);
    prototype_free(ship);
    refs = NULL;
    healer_asset = damaged = NULL;
    ship = NULL;
    test_graph_packs_clear();
}

/* Paths and keys keep an asset one asset; names resolve what was named. */
TEST_CASE(reference_assets_keep_identity_by_path_and_key)
{
    refs = reference_index_new();
    int prototype = reference_index_add_asset(refs, REFERENCE_ASSET_PROTOTYPE, "Ship", "content/prototypes/Ship.kdl", NULL);
    int material = reference_index_add_asset(refs, REFERENCE_ASSET_MATERIAL, "Ship", "content/materials/Ship.kdl", NULL);
    REQUIRE(prototype != material);
    REQUIRE(reference_index_find_name(refs, REFERENCE_ASSET_MATERIAL, "Ship") == material);
    REQUIRE(
        reference_index_find_asset(refs, REFERENCE_ASSET_PROTOTYPE, "./content/prototypes/Ship.kdl", NULL) ==
        prototype
    );
    /* An unsaved asset keeps its key while renamed, then takes its path. */
    int scratch = reference_index_add_asset(refs, REFERENCE_ASSET_BEHAVIOR, "Scratch", NULL, "tab:1");
    REQUIRE(
        reference_index_add_asset(refs, REFERENCE_ASSET_BEHAVIOR, "Renamed", NULL, "tab:1") == scratch
    );
    REQUIRE(!strcmp(reference_index_asset_at(refs, scratch)->name, "Renamed"));
    REQUIRE(
        reference_index_add_asset(refs, REFERENCE_ASSET_BEHAVIOR, "Renamed", "content/behaviors/Renamed.chibi", NULL) ==
        scratch
    );
    REQUIRE(reference_index_asset_at(refs, scratch)->path);
    reference_index_clear(refs);
    REQUIRE(reference_index_asset_count(refs) == 0 && reference_index_count(refs) == 0);
    return true;
}

TEST_CASE(reference_relationship_sources_resolve_their_declared_asset_identity)
{
    const script_catalog_relationship relationship = { .id = 17, .name = "Targets" };
    const script_catalog catalog = { .relationships = &relationship,
                                     .relationship_count = 1 };
    const char *text = "Use :: behavior { relation: Relationship = Targets; }";
    script_syntax *syntax = script_parse(text);
    script_analysis *analysis = script_analyze(syntax, &catalog);
    REQUIRE(!syntax->diagnostic_count && analysis->valid);
    const authoring_document document = { "content/behaviors/Use.chibi", syntax, analysis };
    const source_catalog_definition definition = {
        .id = 17,
        .name = "Targets",
        .path = "content/relationships/DifferentFileName.chibi",
        .text = "Targets :: relationship {}",
        .range = { 0, 7 },
        .text_kind = SOURCE_TEXT_CHIBI,
        .relationship = true
    };
    authoring_navigation *
        navigation = authoring_navigation_new_declarations(&document, 1, &catalog, &definition, 1);
    refs = reference_index_new();
    authoring_source_references_add(refs, navigation);
    int asset = reference_index_find_name(refs, REFERENCE_ASSET_RELATIONSHIP, "Targets");
    REQUIRE(asset >= 0);
    REQUIRE(!strcmp(reference_index_asset_at(refs, asset)->path, definition.path));
    REQUIRE(reference_index_find_name(refs, REFERENCE_ASSET_EVENT, "Targets") < 0);
    reference_symbol symbol = { .kind = REFERENCE_SYMBOL_ASSET, .asset = asset };
    REQUIRE(reference_index_count_uses(refs, symbol, REFERENCE_USES) == 1);
    authoring_navigation_free(navigation);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    return true;
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
    set_default(g, h.getter, "Component", (pin_value){ .type = PIN_NAME, .str = "Health" });
    assert(graph_expose_output_fields(g, h.getter, 0) == 0);
    h.setter = add_named_node(g, "Set_Health_current");
    pin_binding maximum = { .kind = PIN_BINDING_COMPONENT_FIELD,
                            .component_key = "Health",
                            .field_name = "maximum" };
    assert(graph_set_pin_binding(g, h.setter, 0, pin_named(h.setter, 0, "Value"), &maximum) == 0);
    h.split = graph_add_node(g, &NODE_SetComponent);
    set_default(g, h.split, "Component", (pin_value){ .type = PIN_NAME, .str = "Ui" });
    assert(component_node_show_field(g, h.split, "document", true) == 0);
    set_default(
        g,
        h.split,
        "document",
        (pin_value){ .type = PIN_STR,
                     .text = &(const pin_string){ .bytes = "Hud",
                                                  .length = sizeof("Hud") - 1 } }
    );
    h.shader = add_named_node(g, "SetEntity_Shader");
    Shader glow = { .material = "Glow", .shape = "", .parameters = "" };
    set_default(
        g,
        h.shader,
        "Value",
        (pin_value){ .type = PIN_STRUCT, .type_name = "Shader", .ptr = &glow }
    );
    return h;
}

TEST_CASE(reference_graphs_index_every_kind_of_symbol)
{
    REQUIRE(test_graph_packs_set());
    healer h = healer_new();
    healer_asset = h.asset;
    refs = reference_index_new();
    int event = reference_index_add_asset(refs, REFERENCE_ASSET_EVENT, "Damaged", "content/events/Damaged.chibi", NULL);
    int source = reference_index_add_asset(refs, REFERENCE_ASSET_BEHAVIOR, "Healer", "content/behaviors/Healer.chibi", NULL);
    damaged = behavior_asset_new("Damaged");
    damaged->domain = BEHAVIOR_DOMAIN_EVENT_DEFINITION;
    reference_index_add_graphs(refs, event, damaged);
    reference_index_add_graphs(refs, source, h.asset);

    /* Functions, their parameters, variables and locals, within the asset. */
    reference_symbol heal = { .kind = REFERENCE_SYMBOL_GRAPH,
                              .asset = source,
                              .id = h.heal->graph_id };
    REQUIRE(uses(refs, heal, REFERENCE_CALL, h.call) == 1);
    REQUIRE(reference_index_definition(refs, heal)->graph_id == h.heal->graph_id);
    reference_symbol amount = { .kind = REFERENCE_SYMBOL_PARAMETER,
                                .asset = source,
                                .graph_id = h.heal->graph_id,
                                .id = h.amount };
    REQUIRE(uses(refs, amount, REFERENCE_ARGUMENT, h.call) == 1);
    REQUIRE(uses(refs, amount, REFERENCE_READ, NULL) == 1);
    REQUIRE(reference_index_definition(refs, amount)->pin_id == h.amount);
    /* Definitions carry their names, and references their pins'. */
    REQUIRE(!strcmp(reference_index_definition(refs, heal)->symbol.name, "Heal"));
    REQUIRE(!strcmp(reference_index_definition(refs, amount)->symbol.name, "Amount"));
    const reference **arguments = NULL;
    reference_index_find_uses(refs, amount, &arguments);
    REQUIRE(acount(arguments) == 2 && !strcmp(arguments[0]->pin, "Amount"));
    afree(arguments);
    reference_symbol health = { .kind = REFERENCE_SYMBOL_VARIABLE,
                                .asset = source,
                                .id = h.health->id };
    REQUIRE(uses(refs, health, REFERENCE_READ, h.get) == 1);
    REQUIRE(uses(refs, health, REFERENCE_WRITE, NULL) == 2);
    REQUIRE(reference_index_definition(refs, health));
    reference_symbol hp = { .kind = REFERENCE_SYMBOL_LOCAL,
                            .asset = source,
                            .graph_id = h.asset->graph->graph_id,
                            .id = h.local };
    REQUIRE(uses(refs, hp, REFERENCE_READ, h.call) == 1);
    REQUIRE(reference_index_definition(refs, hp)->node_id == h.get->id);
    REQUIRE(!strcmp(reference_index_definition(refs, hp)->symbol.name, "hp"));
    REQUIRE(!strcmp(reference_index_definition(refs, health)->symbol.name, "health"));

    /* Events by name: a definition's belongs to its asset; host events and
       undeclared ones to none. */
    reference_symbol damage = named(REFERENCE_SYMBOL_EVENT, "Damaged", NULL);
    REQUIRE(uses(refs, damage, REFERENCE_HANDLE, h.handle) == 1);
    REQUIRE(reference_index_definition(refs, damage)->source == event);
    REQUIRE(uses(refs, asset_symbol(event), REFERENCE_HANDLE, h.handle) == 1);
    REQUIRE(
        uses(refs, named(REFERENCE_SYMBOL_EVENT, "Tick", NULL), REFERENCE_HANDLE, h.tick) == 1
    );
    REQUIRE(!reference_index_definition(refs, named(REFERENCE_SYMBOL_EVENT, "Tick", NULL)));
    REQUIRE(
        uses(refs, named(REFERENCE_SYMBOL_EVENT, "Healed", NULL), REFERENCE_SEND, h.send) == 1
    );
    REQUIRE(reference_index_find_name(refs, REFERENCE_ASSET_EVENT, "Healed") < 0);

    /* Assets named in pins: a spawned prototype, a Set's ui document, a
       struct default's material. Each is unresolved until added. */
    int pulse = reference_index_find_name(refs, REFERENCE_ASSET_PROTOTYPE, "Pulse");
    REQUIRE(pulse >= 0 && reference_index_asset_at(refs, pulse)->unresolved);
    REQUIRE(uses(refs, asset_symbol(pulse), REFERENCE_SPAWN, h.spawn) == 1);
    int hud = reference_index_find_name(refs, REFERENCE_ASSET_UI, "Hud");
    REQUIRE(uses(refs, asset_symbol(hud), REFERENCE_BIND, h.split) == 1);
    int glow = reference_index_find_name(refs, REFERENCE_ASSET_MATERIAL, "Glow");
    REQUIRE(uses(refs, asset_symbol(glow), REFERENCE_BIND, h.shader) == 1);

    /* Components and fields through nodes and bindings. */
    reference_symbol component_health = named(REFERENCE_SYMBOL_COMPONENT, "Health", NULL);
    REQUIRE(uses(refs, component_health, REFERENCE_READ, h.getter) == 3); /* the node and its two fields */
    reference_symbol current = named(REFERENCE_SYMBOL_FIELD, "Health", "current");
    REQUIRE(uses(refs, current, REFERENCE_READ, h.getter) == 1);
    REQUIRE(uses(refs, current, REFERENCE_WRITE, h.setter) == 1);
    REQUIRE(
        uses(refs, named(REFERENCE_SYMBOL_FIELD, "Health", "maximum"), REFERENCE_READ, h.setter) == 1
    );
    REQUIRE(
        uses(refs, named(REFERENCE_SYMBOL_FIELD, "Ui", "document"), REFERENCE_WRITE, h.split) == 1
    );
    REQUIRE(
        uses(refs, named(REFERENCE_SYMBOL_COMPONENT, "Shader", NULL), REFERENCE_WRITE, h.shader) == 1
    );
    REQUIRE(!reference_index_definition(refs, component_health));

    /* Counts by role; the asset's own symbols are not uses of the asset. */
    REQUIRE(reference_index_count_uses(refs, health, REFERENCE_USES) == 3);
    REQUIRE(reference_index_count_uses(refs, health, REFERENCE_ROLE(REFERENCE_WRITE)) == 2);
    REQUIRE(reference_index_count_uses(refs, health, REFERENCE_ROLE(REFERENCE_DEFINE)) == 1);
    REQUIRE(reference_index_count_uses(refs, asset_symbol(source), REFERENCE_USES) == 0);
    return true;
}

/* Prototypes name behaviors and assets in component fields; a scene object
 * holds its prototype's unless it overrides them; ui documents use widgets
 * and send events. */
TEST_CASE(reference_documents_index_prototypes_objects_and_ui)
{
    prototype_registry registry = {
        .components = REGISTRY_COMPONENTS,
        .component_count = REGISTRY_COMPONENT_COUNT,
    };
    char error[256];
    ship = prototype_load(
        "name \"Ship\"\n"
        "(array)behaviors \"Flight\"\n"
        "components {\n"
        "    Shader material=\"Hull\"\n"
        "    Ui document=\"Hud\"\n"
        "}\n",
        &registry,
        error,
        sizeof error
    );
    REQUIRE(ship);
    refs = reference_index_new();
    int prototype = reference_index_add_asset(refs, REFERENCE_ASSET_PROTOTYPE, "Ship", "content/prototypes/Ship.kdl", NULL);
    int scene = reference_index_add_asset(refs, REFERENCE_ASSET_SCENE, "Dock", "content/scenes/Dock.kdl", NULL);
    int hud = reference_index_add_asset(refs, REFERENCE_ASSET_UI, "Hud", "content/ui/Hud.rml", NULL);
    reference_index_add_prototype(refs, prototype, ship);
    int flight = reference_index_find_name(refs, REFERENCE_ASSET_BEHAVIOR, "Flight");
    int hull = reference_index_find_name(refs, REFERENCE_ASSET_MATERIAL, "Hull");
    REQUIRE(
        reference_index_count_uses(refs, asset_symbol(flight), REFERENCE_ROLE(REFERENCE_ATTACH)) == 1
    );
    REQUIRE(
        reference_index_count_uses(refs, asset_symbol(hud), REFERENCE_ROLE(REFERENCE_BIND)) == 1
    );

    const char *behaviors[] = { "Flight" };
    av_value *overrides = test_read_asset_value("Shader material=\"Chrome\"\n");
    reference_object object = {
        .name = "player",
        .prototype = "Ship",
        .base = ship,
        .overrides = overrides,
        .behaviors = behaviors,
        .behavior_count = 1,
    };
    reference_index_add_object(refs, scene, &object);
    av_value_free(overrides);
    const reference **found = NULL;
    reference_index_find_uses(refs, asset_symbol(prototype), &found);
    REQUIRE(
        acount(found) == 1 && found[0]->role == REFERENCE_USE &&
        !strcmp(found[0]->object, "player")
    );
    asetlen(found, 0);
    int chrome = reference_index_find_name(refs, REFERENCE_ASSET_MATERIAL, "Chrome");
    reference_index_find_uses(refs, asset_symbol(chrome), &found);
    REQUIRE(
        acount(found) == 1 && found[0]->source == scene &&
        !strcmp(found[0]->component, "Shader.material")
    );
    asetlen(found, 0);
    /* The override replaces the prototype's material; its ui stays. */
    reference_index_find_uses(refs, asset_symbol(hull), &found);
    REQUIRE(acount(found) == 1 && found[0]->source == prototype);
    REQUIRE(reference_index_count_uses(refs, asset_symbol(hud), REFERENCE_USES) == 2);
    afree(found);

    reference_index_add_ui(
        refs,
        hud,
        "<use ui=\"Meter\" id=\"hull\"/>\n<div "
        "data-event-click=\"emit('Launch')\">Go</div>"
        "<div class=\"menu\" data-keys=\"Enter:Choose Down:Next\"></div>"
    );
    int meter = reference_index_find_name(refs, REFERENCE_ASSET_UI, "Meter");
    REQUIRE(
        reference_index_count_uses(refs, asset_symbol(meter), REFERENCE_ROLE(REFERENCE_USE)) == 1
    );
    const char *sent[] = { "Launch", "Choose", "Next" };
    for (int i = 0; i < 3; i++) {
        REQUIRE(
            reference_index_count_uses(
                refs,
                named(REFERENCE_SYMBOL_EVENT, sent[i], NULL),
                REFERENCE_ROLE(REFERENCE_SEND)
            ) == 1
        );
    }
    return true;
}

static const reference *handler(const reference_index *refs, const char *event)
{
    const reference **found = NULL;
    reference_index_find_uses(refs, named(REFERENCE_SYMBOL_EVENT, event, NULL), &found);
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
TEST_CASE(reference_calls_climb_handlers_and_detect_cycles)
{
    REQUIRE(test_graph_packs_set());
    healer h = healer_new();
    healer_asset = h.asset;
    graph *g = h.asset->graph;
    REQUIRE(graph_connect_exec(g, h.handle, 0, h.call, 0, 0) == 0);
    REQUIRE(graph_connect_exec(g, h.tick, 0, h.send, 0, 0) == 0);
    REQUIRE(behavior_asset_add_function_call_node_to_graph(h.asset, h.heal, h.heal));
    refs = reference_index_new();
    int event = reference_index_add_asset(refs, REFERENCE_ASSET_EVENT, "Damaged", "content/events/Damaged.chibi", NULL);
    int source = reference_index_add_asset(refs, REFERENCE_ASSET_BEHAVIOR, "Healer", "content/behaviors/Healer.chibi", NULL);
    damaged = behavior_asset_new("Damaged");
    damaged->domain = BEHAVIOR_DOMAIN_EVENT_DEFINITION;
    reference_index_add_graphs(refs, event, damaged);
    reference_index_add_graphs(refs, source, h.asset);
    const reference *on_damage = handler(refs, "Damaged");
    REQUIRE(reference_index_reaches(refs, on_damage, h.call->id));
    REQUIRE(!reference_index_reaches(refs, on_damage, h.send->id));
    REQUIRE(reference_index_reaches(refs, handler(refs, "Tick"), h.send->id));

    reference_symbol heal = { .kind = REFERENCE_SYMBOL_GRAPH,
                              .asset = source,
                              .id = h.heal->graph_id };
    call_item *callers = call_hierarchy(refs, heal, false, 2);
    int by_handler = child(callers, 0, CALL_HANDLER, "Damaged");
    int by_itself = child(callers, 0, CALL_FUNCTION, "Heal");
    REQUIRE(by_handler > 0 && callers[by_handler].node_id == h.handle->id);
    REQUIRE(
        acount(callers[by_handler].sites) == 1 &&
        callers[by_handler].sites[0]->node_id == h.call->id
    );
    int damage = child(callers, by_handler, CALL_EVENT, "Damaged");
    REQUIRE(damage > 0 && callers[damage].asset == event && !callers[damage].count);
    REQUIRE(by_itself > 0 && callers[by_itself].cycle && !callers[by_itself].count);
    call_hierarchy_free(callers);

    call_item *callees = call_hierarchy(refs, named(REFERENCE_SYMBOL_EVENT, "Tick", NULL), true, 1);
    int tick = child(callees, 0, CALL_HANDLER, "Tick");
    REQUIRE(
        callees[0].asset == -1 && tick > 0 && callees[tick].more &&
        !callees[tick].count
    );
    call_hierarchy_free(callees);
    callees = call_hierarchy(refs, named(REFERENCE_SYMBOL_EVENT, "Tick", NULL), true, 2);
    REQUIRE(child(callees, child(callees, 0, CALL_HANDLER, "Tick"), CALL_EVENT, "Healed") > 0);
    call_hierarchy_free(callees);
    return true;
}

void test_reference_index()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(reference_assets_keep_identity_by_path_and_key);
    RUN_TEST_CASE(reference_relationship_sources_resolve_their_declared_asset_identity);
    RUN_TEST_CASE(reference_graphs_index_every_kind_of_symbol);
    RUN_TEST_CASE(reference_documents_index_prototypes_objects_and_ui);
    RUN_TEST_CASE(reference_calls_climb_handlers_and_detect_cycles);
    pu_clear_setup();
}
