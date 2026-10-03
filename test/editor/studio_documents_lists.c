/* Lists and drag (workstream F4) in Studio: the widget gallery's authored
 * List shows only a window of the project's assets and reuses its rows as it
 * scrolls, and one drag crosses between authored and native lists in other
 * area documents: a prototype from the authored List onto the native
 * Outliner adds an object, an asset from native Assets lands in the authored
 * Collected list, one from the List lands before a Collected row, and the
 * Scene's viewport, a C surface, places a prototype dropped on it. */

#include "test/editor/studio_documents.h"
#include "test/suite_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The Widgets panel in the top area, scrolled to the List. */
static void show_gallery(studio_fixture *f)
{
    studio *s = f->studio;
    s->maximized = -1;
    studio_layout *layout = studio_active_layout(s);
    layout->editors[STUDIO_AREA_TOP] = STUDIO_EDITOR_PANEL;
    snprintf(layout->panels[STUDIO_AREA_TOP], sizeof layout->panels[0], "Widgets");
    frames(f, 8);
    rml_element *list = studio_element(s, "#gallery-assets");
    if (list) {
        rml_element_scroll_into_view(list, RML_SCROLL_START, RML_SCROLL_NEAREST);
    }
    frames(f, 4);
}

/* The center of `el` in dp, as the pointer reaches it, `dy` of its height
   down from its middle. */
static void center(studio *s, rml_element *el, float dy, float *x, float *y)
{
    rml_vec2 at = rml_element_absolute_offset(el, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    float ratio = studio_pixel_ratio(s);
    *x = (at.x + size.x * 0.5f) / ratio;
    *y = (at.y + size.y * (0.5f + dy)) / ratio;
}

/* The authored row (a TreeRow's use) in `selector` whose name is `name`. */
static rml_element *row_named(studio *s, const char *selector, const char *name)
{
    rml_element *rows[256];
    rml_context *context = rml_backend_context(s->backend);
    for (int d = 0; d < rml_context_document_count(context); d++) {
        int count = rml_element_query_selector_all(rml_context_document(context, d), selector, rows, 256);
        for (int i = 0; i < count && i < 256; i++) {
            char payload[128] = "";
            rml_element_attribute(rows[i], "data-drag-payload", payload, sizeof payload);
            rml_element *label = rml_element_query_selector(rows[i], ".name");
            char text[128] = "";
            if (label) {
                rml_element_inner_rml(label, text, sizeof text);
            }
            if (!strcmp(payload, name) || !strcmp(text, name)) {
                return rows[i];
            }
        }
    }
    return NULL;
}

/* Presses `from`, carries it past the drag threshold onto `to`, `dy` of its
   height from its middle, and releases there; whether `to` was marked over
   before the release. */
static bool carry(studio_fixture *f, rml_element *from, rml_element *to, float dy)
{
    studio *s = f->studio;
    float x, y, tx, ty;
    center(s, from, 0, &x, &y);
    center(s, to, dy, &tx, &ty);
    mouse_move(s, x, y);
    frames(f, 1);
    graph_drag_button(s, x, y, true);
    frames(f, 1);
    mouse_move(s, x + 12, y);
    frames(f, 2);
    mouse_move(s, tx, ty);
    frames(f, 3);
    bool over = rml_element_has_attribute(to, "data-drop-over");
    graph_drag_button(s, tx, ty, false);
    frames(f, 4);
    return over;
}

/* The first row data-for made in `spacer`, past its template. */
static rml_element *first_row(rml_element *spacer)
{
    for (int i = 0; spacer && i < rml_element_child_count(spacer); i++) {
        rml_element *row = rml_element_child(spacer, i);
        if (!rml_element_has_attribute(row, "data-for")) {
            return row;
        }
    }
    return NULL;
}

/* The authored List over the Studio project's assets shows a window of
   them that keeps its length and its elements as it scrolls. */
static void list_windows_assets()
{
    studio_fixture f = studio_project_open();
    studio *s = f.studio;
    show_gallery(&f);
    const reference_index *index = app_scene_authoring_asset_index(s->scene);
    int count = reference_index_asset_count(index);
    rml_element *list = studio_element(s, "#gallery-assets");
    rml_element *spacer = studio_element(s, "#gallery-assets > .list-spacer");
    char first[1024] = "", later[1024] = "";
    int rows = list_rows(s, "#gallery-assets > .list-spacer", first, sizeof first);
    rml_element *kept = first_row(spacer);
    float row = 0;
    char height[32] = "";
    if (spacer) {
        rml_element_property(spacer, "--row", height, sizeof height);
        row = (float)atof(height);
        rml_element_set_scroll_top(list, row * 40 * studio_pixel_ratio(s));
    }
    frames(&f, 4);
    int scrolled = list_rows(s, "#gallery-assets > .list-spacer", later, sizeof later);
    bool windowed = list && count > 2 * rows && rows > 4 && scrolled == rows;
    bool moved = strcmp(first, later) && kept && first_row(spacer) == kept;
    if (!(windowed && moved)) {
        printf("  gallery list: %d assets, %d then %d rows, reused %d\n", count, rows, scrolled, moved);
    }
    test_check(
        windowed && moved,
        "an authored List shows a window of the project's assets that keeps "
        "its length and its row elements as it scrolls"
    );
    fixture_free(&f);
}

/* A prototype from the authored List onto the native Outliner adds an
   object; an asset from native Assets lands in the authored Collected
   list; one from the List lands before a Collected row. */
static void drag_crosses_lists()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    s->prefs.assets_tile = STUDIO_ASSETS_LIST;
    s->applied = s->prefs;
    snprintf(s->assets_filter, sizeof s->assets_filter, "Bank");
    studio_active_layout(s)->editors[STUDIO_AREA_BOTTOM] = STUDIO_EDITOR_ASSETS;
    show_gallery(&f);
    int objects = acount(f.scene->actors);
    rml_element *binding = row_named(s, "#gallery-assets [data-drag]", "Binding");
    rml_element *outliner = studio_element(s, ".vlist.outliner");
    bool over_outliner = binding && outliner && carry(&f, binding, outliner, 0.3f);
    bool added = acount(f.scene->actors) == objects + 1 &&
                 !strcmp(f.scene->actors[objects].prototype_name, "Binding");
    if (!(over_outliner && added)) {
        printf(
            "  to the Outliner: row %d list %d over %d, %d objects\n",
            binding != NULL,
            outliner != NULL,
            over_outliner,
            acount(f.scene->actors)
        );
    }
    test_check(
        over_outliner && added,
        "a prototype dragged from an authored List onto the native Outliner "
        "in another area adds an object of it"
    );

    rml_element *bank = row_named(s, ".assets-list .tree-row", "Bank");
    rml_element *bin = studio_element(s, ".bin");
    bool over_bin = bank && bin && carry(&f, bank, bin, 0);
    bool collected = acount(s->gallery.bin) == 1 &&
                     !strcmp(s->gallery.bin[0].name, "Bank") &&
                     row_named(s, ".bin [data-ui=TreeRow]", "Bank") &&
                     studio_element(s, ".tiles [data-ui=Tile] .tile");
    if (!(over_bin && collected)) {
        printf(
            "  to Collected: row %d bin %d over %d, %d collected\n",
            bank != NULL,
            bin != NULL,
            over_bin,
            acount(s->gallery.bin)
        );
    }
    test_check(
        over_bin && collected,
        "an asset dragged from native Assets lands in an authored list in "
        "another area, shown as a TreeRow and a Tile"
    );

    rml_element *teller = row_named(s, "#gallery-assets [data-drag]", "Teller");
    rml_element *held = row_named(s, ".bin [data-ui=TreeRow]", "Bank");
    bool before = teller && held && carry(&f, teller, held, -0.3f);
    bool placed = acount(s->gallery.bin) == 2 &&
                  !strcmp(s->gallery.bin[0].name, "Teller") &&
                  !strcmp(s->gallery.bin[1].name, "Bank");
    if (!(before && placed)) {
        printf(
            "  before a row: over %d, %d collected\n",
            before,
            acount(s->gallery.bin)
        );
    }
    test_check(
        before && placed,
        "a drop on the top half of a list-zoned TreeRow lands before it, "
        "once, though it bubbles to the list"
    );
    fixture_free(&f);
}

/* The Scene's viewport is a C surface: it takes a prototype dragged from
   the authored List with ui_drag_dropped, placing it under the pointer. */
static void scene_takes_drop()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    studio_active_layout(s)->editors[STUDIO_AREA_BOTTOM] = STUDIO_EDITOR_SCENE;
    show_gallery(&f);
    int objects = acount(f.scene->actors);
    rml_element *binding = row_named(s, "#gallery-assets [data-drag]", "Binding");
    rml_element *viewport = studio_element(s, "img[src=kit:viewport]");
    bool over = binding && viewport && carry(&f, binding, viewport, 0);
    frames(&f, 2);
    bool placed = acount(f.scene->actors) == objects + 1 &&
                  !strcmp(f.scene->actors[objects].prototype_name, "Binding");
    if (!(over && placed)) {
        printf(
            "  to the Scene: row %d viewport %d over %d, %d objects\n",
            binding != NULL,
            viewport != NULL,
            over,
            acount(f.scene->actors)
        );
    }
    test_check(
        over && placed,
        "a prototype dragged from an authored List onto the Scene's viewport "
        "places an object there"
    );
    fixture_free(&f);
}

void studio_documents_list_checks()
{
    list_windows_assets();
    drag_crosses_lists();
    scene_takes_drop();
}
