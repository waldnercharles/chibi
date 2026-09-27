/* Native slots: kit content inside a kit_host's authored content, rebuilt
 * when the content replaces its element; and the authored status bar, which
 * hosts one, over Studio's `editor` variable. */
#include "editor/studio/studio_internal.h"
#include "render/ui.h"
#include "support/text_overlay.h"
#include "test/suite_internal.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FRAME_SECONDS (1.0 / 60.0)

typedef struct
{
    rml_backend *backend;
    kit *k;
    kit_frame_input in;
    ui_mount *mount;
    rml_listener *listener;
    ui_data data;
    rml_element *host;
    bool slot;  /* begin the #slot slot */
    int clicks; /* of the button in it */
} slot_fixture;

static void on_event(void *user, const rml_event *event)
{
    ui_mount_listen(user, event);
}

static void on_emit(void *user, const rml_event *event, const char *name, const char *value)
{
    ui_mount_emit(user, event->current, name, value);
}

/* A Studio kit whose host shows ui asset `name` under `root`, bound to
 * `doc`. */
static void open_fixture(slot_fixture *f, const char *root, const char *name, yyjson_mut_doc *doc)
{
    f->backend = rml_backend_create(1280, 480, 1);
    f->k = test_kit_create(rml_backend_context(f->backend), "studio.rml");
    f->in = (kit_frame_input){ .dp_ratio = 1, .width = 1280, .height = 480 };
    f->mount = ui_mount_new(root, name);
    f->listener = rml_listener_create(on_event, f->mount);
    ui_data_init(&f->data, rml_backend_context(f->backend), doc, on_emit, f->mount);
}

static void close_fixture(slot_fixture *f)
{
    kit_destroy(f->k);
    ui_data_free(&f->data);
    rml_backend_destroy(f->backend);
    ui_mount_free(f->mount);
    rml_listener_destroy(f->listener);
}

static void host(slot_fixture *f, const char *key)
{
    f->host = kit_host(f->k, key, NULL);
    if (ui_mount_sync(f->mount, f->host, f->listener, f->in.time)) {
        kit_style_sheet(f->k, ui_mount_name(f->mount), ui_mount_style(f->mount));
    }
}

static void end_frame(slot_fixture *f)
{
    kit_end(f->k);
    rml_backend_update(f->backend, FRAME_SECONDS);
    f->in.time += FRAME_SECONDS;
}

/* The Bar asset with a button in #slot and a label in each item's cell. */
static void bar_frame(slot_fixture *f)
{
    kit *k = f->k;
    kit_begin(k, &f->in);
    kit_root(k, "app");
    host(f, "bar");
    if (f->slot && kit_slot_begin(k, "slot", f->host, "#slot")) {
        f->clicks += kit_button(k, "go", &(kit_button_desc){ .label = "Go" });
        kit_slot_end(k);
    }
    if (kit_slot_begin(k, "cell", f->host, ".cell")) {
        kit_label(k, "l", "native", "Native");
        kit_slot_end(k);
    }
    end_frame(f);
}

static void bar_frames(slot_fixture *f, int count)
{
    for (int i = 0; i < count; i++) {
        bar_frame(f);
    }
}

/* The shown elements `selector` matches; data-for keeps its template
   hidden, and data-if hides rather than removes. */
static int count(slot_fixture *f, const char *selector)
{
    rml_element *found[8];
    int matches = rml_element_query_selector_all(f->host, selector, found, 8),
        shown = 0;
    for (int i = 0; i < matches && i < 8; i++) {
        shown += rml_element_is_visible(found[i], true);
    }
    return shown;
}

static void click(slot_fixture *f, rml_element *el)
{
    rml_vec2 at = rml_element_absolute_offset(el, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    float x = at.x + size.x * 0.5f, y = at.y + size.y * 0.5f;
    rml_input_type types[] = { RML_INPUT_POINTER_MOVE,
                               RML_INPUT_POINTER_DOWN,
                               RML_INPUT_POINTER_UP };
    for (size_t i = 0; i < sizeof types / sizeof *types; i++) {
        rml_backend_process_input(f->backend, &(rml_input){ .type = types[i], .x = x, .y = y });
    }
}

/* Sets `items` to `size` numbers and lets data-for follow. */
static void set_items(slot_fixture *f, int size)
{
    yyjson_mut_val *items = yyjson_mut_obj_get(yyjson_mut_doc_get_root(f->data.doc), "items");
    yyjson_mut_arr_clear(items);
    for (int i = 0; i < size; i++) {
        yyjson_mut_arr_add_int(f->data.doc, items, i);
    }
    rml_data_model_dirty(f->data.model, "items");
}

/* Bar, then the same markup edited. */
static const char
    BAR[] = "rml: |\n  <div class=\"bar\"><span id=\"slot\"/><div "
            "class=\"item\" data-for=\"item : items\"><span "
            "class=\"cell\"/></div></div>\n";
static const char EDITED[] = "rml: |\n  <div class=\"bar edited\"><span "
                             "id=\"slot\"/><div class=\"item\" data-for=\"item "
                             ": items\"><span class=\"cell\"/></div></div>\n";

static void slot_checks(const char *root)
{
    char path[128];
    snprintf(path, sizeof path, "%s/ui/Bar.yaml", root);
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs(BAR, file);
    fclose(file);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *top = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, top);
    yyjson_mut_obj_add_arr(doc, top, "items");
    slot_fixture f = { .slot = true };
    open_fixture(&f, root, "Bar", doc);
    set_items(&f, 1);
    bar_frames(&f, 3);
    kit_rect rect;
    rml_element *slot = rml_element_query_selector(f.host, "#slot");
    char mark[16] = "";
    rml_element_attribute(slot, "data-kit-slot", mark, sizeof mark);
    test_check(
        !ui_mount_error(f.mount)[0] && !strcmp(mark, "slot") &&
            count(&f, "#slot .btn") == 1 && count(&f, ".cell .native") == 1 &&
            kit_find(f.k, "app/bar/slot/go", &rect),
        "a native slot adopts an element of hosted content, marks it and holds "
        "kit content"
    );

    click(&f, rml_element_query_selector(f.host, "#slot .btn"));
    bar_frame(&f);
    test_check(f.clicks == 1, "kit controls in a native slot take their events");

    /* The mount reinstalls its markup within the frame, and RmlUi's pools
       hand the new #slot the old one's address. Neither the pointer nor the
       focus is on what goes, so no event reaches the kit meanwhile. */
    rml_backend_process_input(
        f.backend,
        &(rml_input){ .type = RML_INPUT_POINTER_MOVE, .x = 1, .y = 470 }
    );
    kit_release_focus(f.k, f.host);
    bar_frame(&f);
    text_overlay_set(path, EDITED);
    bar_frame(&f);
    rml_element *button = rml_element_query_selector(f.host, "#slot .btn");
    bool rebuilt = count(&f, ".edited #slot .btn") == 1;
    if (button) {
        click(&f, button);
    }
    /* data-for makes the new items as the document updates, after the frame
       that installed them. */
    bar_frame(&f);
    test_check(
        rebuilt && f.clicks == 2 && count(&f, ".cell .native") == 1,
        "a native slot builds anew when its mount reinstalls the markup"
    );

    set_items(&f, 0);
    bar_frames(&f, 2);
    bool gone = count(&f, ".cell") == 0 && !kit_find(f.k, "app/bar/cell/l", &rect);
    set_items(&f, 1);
    bar_frames(&f, 2);
    bool back = count(&f, ".cell .native") == 1;
    /* data-for drops the item and makes another between frames, where the
       freed elements' addresses may come back, and input arrives before the
       next frame begins. */
    set_items(&f, 0);
    rml_backend_update(f.backend, FRAME_SECONDS);
    set_items(&f, 1);
    rml_backend_update(f.backend, FRAME_SECONDS);
    click(&f, rml_element_query_selector(f.host, ".item"));
    bar_frames(&f, 2);
    test_check(
        gone && back && count(&f, ".cell .native") == 1 &&
            kit_find(f.k, "app/bar/cell/l", &rect),
        "a native slot follows data-for as it drops and recreates its item"
    );

    f.slot = false;
    bar_frame(&f);
    test_check(
        count(&f, "#slot .btn") == 0 &&
            !rml_element_has_attribute(rml_element_query_selector(f.host, "#slot"), "data-kit-slot") &&
            !kit_find(f.k, "app/bar/slot/go", &rect),
        "a native slot not begun in a frame loses its content and its mark"
    );
    close_fixture(&f);
    text_overlay_set(path, NULL);
    unlink(path);
}

/* ---------- The authored status bar ---------- */

static void status_frame(slot_fixture *f)
{
    kit *k = f->k;
    kit_begin(k, &f->in);
    kit_root(k, "app");
    host(f, "status");
    kit_release_focus(k, f->host);
    if (kit_slot_begin(k, "scale", f->host, "#ui-scale")) {
        kit_button(k, "scale", &(kit_button_desc){ .label = "100%", .size = KIT_SM, .cls = "ui-scale" });
        kit_slot_end(k);
    }
    end_frame(f);
}

static void status_frames(slot_fixture *f, int count)
{
    for (int i = 0; i < count; i++) {
        status_frame(f);
    }
}

static bool text_is(rml_element *el, const char *text)
{
    char rml[128] = "";
    if (el) {
        rml_element_inner_rml(el, rml, sizeof rml);
    }
    return !strcmp(rml, text);
}

static bool state_is(rml_element *el, const char *state)
{
    char value[16] = "";
    rml_element_attribute(el, "data-state", value, sizeof value);
    return !strcmp(value, state);
}

static void status_checks()
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(doc, yyjson_mut_obj(doc));
    studio_status_add(doc);
    studio_status status = {
        .hints = { { KIT_ICON_MOUSE, NULL, "Select" }, { 0, "F", "Frame" } },
        .hint_count = 2,
        .report = "Saved",
        .reporting = true,
        .assets = true,
        .assets_key = "\xe2\x87\xa7\xe2\x8c\x98"
                      "A",
        .scene = "Sandbox",
        .selection = "Player",
        .objects = 12,
    };
    bool shown = studio_status_show(doc, &status);
    bool same = !studio_status_show(doc, &status);
    slot_fixture f = { 0 };
    open_fixture(&f, "projects/studio/content", "StatusBar", doc);
    status_frames(&f, 3);
    rml_element *hints[4], *toggles[4];
    int hint_count = rml_element_query_selector_all(f.host, "[data-ui=KeyHint]", hints, 4) -
                     1;
    int toggle_count = rml_element_query_selector_all(f.host, ".toggle-drawer", toggles, 4);
    char kbd[32] = "";
    rml_element_attribute(toggles[0], "data-kbd", kbd, sizeof kbd);
    rml_element *bar = rml_element_query_selector(f.host, ".statusbar");
    test_check(
        shown && same && !ui_mount_error(f.mount)[0] && hint_count == 2 &&
            text_is(rml_element_query_selector(hints[0], ".label"), "Select") &&
            text_is(rml_element_query_selector(hints[1], ".kbd-key"), "F") &&
            !rml_element_is_visible(rml_element_query_selector(hints[1], ".icon"), false) &&
            rml_element_is_class_set(rml_element_query_selector(f.host, ".report"), "shown") &&
            toggle_count == 3 && state_is(toggles[0], "on") &&
            state_is(toggles[1], "off") && !strcmp(kbd, status.assets_key) &&
            text_is(rml_element_query_selector(f.host, ".count"), "Objects 1/12") &&
            count(&f, "#ui-scale .ui-scale") == 1 &&
            rml_element_box_size(bar, RML_BOX_BORDER).y == 24,
        "the authored status bar shows the editor variable, with UI Scale in "
        "its native slot"
    );

    click(&f, toggles[1]);
    status_frame(&f);
    ui_activation a;
    bool emitted = ui_mount_take(f.mount, &a) &&
                   !strcmp(a.event, "studio.toggle-output") &&
                   studio_command_named(a.event + strlen("studio.")) ==
                       STUDIO_COMMAND_TOGGLE_OUTPUT;
    test_check(emitted && !kit_wants_keyboard(f.k), "a status bar toggle emits its Studio command and leaves the keyboard to Studio");

    status.hint_count = 1;
    status.reporting = false;
    status.selection = "";
    if (studio_status_show(doc, &status)) {
        rml_data_model_dirty(f.data.model, "editor");
    }
    status_frames(&f, 2);
    test_check(
        count(&f, "[data-ui=KeyHint]") == 1 &&
            !rml_element_is_class_set(rml_element_query_selector(f.host, ".report"), "shown") &&
            text_is(rml_element_query_selector(f.host, ".count"), "Objects 0/12"),
        "the status bar follows the editor variable as it changes"
    );
    close_fixture(&f);
}

void test_kit_slot()
{
    char root[] = "/tmp/chibi-kit-slot-XXXXXX", folder[64];
    assert(mkdtemp(root));
    snprintf(folder, sizeof folder, "%s/ui", root);
    assert(mkdir(folder, 0700) == 0);
    slot_checks(root);
    rmdir(folder);
    rmdir(root);
    status_checks();
}
