/* Graph chrome (workstream G1) in Studio: the canvas's context menus
 * (GraphMenu.rml over the `graph_menu` source), each item's action one Undo
 * step, written once and run by key. */

#include "editor/studio/studio_graph_internal.h"
#include "graph/model/registry.h"
#include "support/array.h"
#include "test/editor/studio_documents.h"
#include "test/suite_internal.h"
#include "ui/popover.h"

#include <SDL3/SDL_keyboard.h>
#include <stdio.h>
#include <string.h>

/* The Vault with Teller open in the Behavior area below the Scene and
   Properties beside them. */
static studio_fixture graph_open()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    s->maximized = -1;
    studio_layout *layout = studio_active_layout(s);
    layout->editors[STUDIO_AREA_TOP] = STUDIO_EDITOR_SCENE;
    layout->editors[STUDIO_AREA_BOTTOM] = STUDIO_EDITOR_BEHAVIOR;
    layout->editors[STUDIO_AREA_PROPERTIES] = STUDIO_EDITOR_PROPERTIES;
    frames(&f, 6);
    return f;
}

static bool shows_text(rml_element *el, const char *expected)
{
    char text[256] = "";
    if (el) {
        rml_element_inner_rml(el, text, sizeof text);
    }
    return el && !strcmp(text, expected);
}

/* The items of `menu` shown, data-when's templates left out. */
static int shown_items(rml_element *menu, const char *selector, rml_element **out, int max)
{
    rml_element *all[64];
    int count = menu ? rml_element_query_selector_all(menu, selector, all, 64) : 0,
        shown = 0;
    for (int i = 0; i < count && i < 64; i++) {
        if (rml_element_is_visible(all[i], true) && shown < max) {
            out[shown++] = all[i];
        }
    }
    return shown;
}

/* The item of `menu` whose label is `label`, or NULL. */
static rml_element *item_labeled(rml_element *menu, const char *label)
{
    rml_element *items[64];
    int count = shown_items(menu, ".menu-item", items, 64);
    for (int i = 0; i < count; i++) {
        if (shows_text(rml_element_query_selector(items[i], ".label"), label)) {
            return items[i];
        }
    }
    return NULL;
}

static bool disabled(rml_element *item)
{
    return item && rml_element_has_attribute(item, "data-disabled");
}

/* Opens the canvas menu on a node, one of its pins, a comment frame or a
   wire, at a point over the graph, as a right click there does; the open
   menu or NULL. */
static rml_element *open_menu(studio_fixture *f, uint64_t node, uint64_t pin, uint64_t comment, int edge)
{
    studio_graph *g = &f->studio->graph;
    g->menu_node = node;
    g->menu_pin = pin;
    g->menu_comment = comment;
    g->menu_edge = edge;
    g->menu_x = g->surface.rect.x + 80;
    g->menu_y = g->surface.rect.y + 40;
    g->menu_request = true;
    frames(f, 3);
    rml_element *menu = studio_element(f->studio, "#graph-menu");
    return menu && popover_open(menu) ? menu : NULL;
}

/* Clicks a submenu's row, which opens it beside the row; its menu. */
static rml_element *open_submenu(studio_fixture *f, rml_element *row)
{
    if (!row) {
        return NULL;
    }
    press(f->studio, row);
    frames(f, 2);
    rml_element *sub = rml_element_parent(row);
    sub = sub ? rml_element_query_selector(sub, ".popup.menu") : NULL;
    return sub && popover_open(sub) ? sub : NULL;
}

static bool menu_closed(studio_fixture *f)
{
    frames(f, 2);
    return !f->studio->graph.menu_open && !studio_element(f->studio, "#graph-menu");
}

/* The node menu names its node, lists its actions with their keys and
   opens Select beside its row; Disable runs as one Undo step and closes
   the menu, and Escape closes it. */
static void node_menu()
{
    studio_fixture f = graph_open();
    studio *s = f.studio;
    node *branch = editor_spawn(f.graph, registry_node_by_name("Branch"), 120, 80);
    uint64_t id = branch->id;
    frames(&f, 3);
    int undo = acount(f.graph->undo);
    rml_element *menu = open_menu(&f, id, 0, 0, -1);
    rml_element *heading = menu ? rml_element_query_selector(menu, ".menu-group")
                                : NULL;
    rml_element *remove = item_labeled(menu, "Delete");
    bool shown = menu && shows_text(heading, "Branch") &&
                 item_labeled(menu, "Duplicate") && remove &&
                 shows_text(rml_element_query_selector(remove, ".kbd"), "X");
    rml_element *select = open_submenu(&f, item_labeled(menu, "Select"));
    rml_element *items[8];
    int linked = shown_items(select, ".menu-item", items, 8);
    bool submenu = select && linked == 4 && item_labeled(select, "All Output Nodes");
    tap(SDL_SCANCODE_ESCAPE);
    tap(SDL_SCANCODE_ESCAPE);
    bool escaped = menu_closed(&f);
    menu = open_menu(&f, id, 0, 0, -1);
    press(s, item_labeled(menu, "Disable"));
    bool closed = menu_closed(&f);
    branch = graph_node_by_id(sg_active_graph(s), id);
    bool ran = branch && branch->disabled && acount(f.graph->undo) == undo + 1;
    menu = open_menu(&f, id, 0, 0, -1);
    bool enable = item_labeled(menu, "Enable") != NULL;
    if (!(shown && submenu && escaped && ran && closed && enable)) {
        printf(
            "  node menu: shown %d submenu %d (%d items) escaped %d ran %d "
            "closed %d enable %d\n",
            shown,
            submenu,
            linked,
            escaped,
            ran,
            closed,
            enable
        );
    }
    test_check(
        shown && submenu && escaped && ran && closed && enable,
        "the graph's node menu names the node, shows its actions' keys, opens "
        "Select beside its row, closes on Escape and runs Disable as one Undo "
        "step, closing"
    );
    fixture_free(&f);
}

/* A pin's menu: Break Link stays disabled without a wire and its click does
   nothing; Reset to Default puts a changed input back as one Undo step. */
static void pin_menu()
{
    studio_fixture f = graph_open();
    studio *s = f.studio;
    node *branch = editor_spawn(f.graph, registry_node_by_name("Branch"), 120, 80);
    uint64_t id = branch->id, condition = branch->input_pins[1].id;
    pin_value on = { .type = PIN_BOOL, .b = true };
    assert(graph_set_pin_default(sg_active_graph(s), branch, 0, 1, &on) == 0);
    frames(&f, 3);
    int undo = acount(f.graph->undo);
    rml_element *menu = open_menu(&f, id, condition, 0, -1);
    rml_element *heading = menu ? rml_element_query_selector(menu, ".menu-group")
                                : NULL;
    rml_element *unlink = item_labeled(menu, "Break Link");
    bool shown = menu && shows_text(heading, "Condition") && disabled(unlink) &&
                 item_labeled(menu, "Reset to Default") &&
                 !disabled(item_labeled(menu, "Reset to Default"));
    press(s, unlink);
    frames(&f, 2);
    bool inert = popover_open(menu) && acount(f.graph->undo) == undo;
    press(s, item_labeled(menu, "Reset to Default"));
    bool closed = menu_closed(&f);
    branch = graph_node_by_id(sg_active_graph(s), id);
    bool reset = branch && !branch->input_pins[1].default_value.b &&
                 acount(f.graph->undo) == undo + 1;
    test_check(
        shown && inert && reset && closed,
        "a pin's menu names the pin, keeps Break Link disabled without a wire "
        "and runs Reset to Default as one Undo step"
    );
    fixture_free(&f);
}

/* A wire's menu deletes it; a comment frame's Color submenu shows each
   color's dot, the frame's own current, and sets another as one step. */
static void wire_and_comment_menus()
{
    studio_fixture f = graph_open();
    studio *s = f.studio;
    graph *gr = sg_active_graph(s);
    node *add = editor_spawn(f.graph, registry_node_by_name("Add"), 80, 80);
    node *atan = editor_spawn(f.graph, registry_node_by_name("Atan2"), 360, 80);
    assert(graph_connect_data(gr, add, 0, atan, 0) == 0);
    graph_comment_box *box = graph_add_comment_box(gr, "Aim", 40, 260, 240, 120);
    uint64_t comment = box->id;
    frames(&f, 3);
    int undo = acount(f.graph->undo), edges = acount(gr->edges);
    rml_element *menu = open_menu(&f, 0, 0, 0, 0);
    bool wire = item_labeled(menu, "Straighten Connection") &&
                item_labeled(menu, "Delete Wire");
    press(s, item_labeled(menu, "Delete Wire"));
    bool deleted = menu_closed(&f) && acount(gr->edges) == edges - 1 &&
                   acount(f.graph->undo) == undo + 1;
    menu = open_menu(&f, 0, 0, comment, -1);
    rml_element *colors = open_submenu(&f, item_labeled(menu, "Color"));
    rml_element *dots[16];
    int count = shown_items(colors, ".menu-item .lead", dots, 16);
    char tint[32] = "";
    rml_element *red = item_labeled(colors, "Red");
    rml_element *dot = red ? rml_element_query_selector(red, ".lead") : NULL;
    if (dot) {
        rml_element_property(dot, "color", tint, sizeof tint);
    }
    bool listed = count == 9 &&
                  rml_element_has_attribute(item_labeled(colors, "Default"), "data-current") &&
                  tint[0];
    press(s, red);
    bool colored = menu_closed(&f) && sg_comment_by_id(s, comment) &&
                   sg_comment_by_id(s, comment)->color == 0xe5534bffu &&
                   acount(f.graph->undo) == undo + 2;
    if (!(wire && deleted && listed && colored)) {
        printf(
            "  wire %d deleted %d listed %d (%d dots, tint %s) colored %d\n",
            wire,
            deleted,
            listed,
            count,
            tint,
            colored
        );
    }
    test_check(
        wire && deleted && listed && colored,
        "a wire's menu deletes it and a comment frame's Color submenu shows "
        "each color as a tinted dot, the frame's own current, each choice one "
        "Undo step"
    );
    fixture_free(&f);
}

void studio_documents_graph_chrome_checks()
{
    node_menu();
    pin_menu();
    wire_and_comment_menus();
}
