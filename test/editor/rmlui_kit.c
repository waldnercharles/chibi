/* Studio kit contracts without a GPU: reconciliation, number fields, menus, command. */
#include "editor/ui/highlight.h"
#include "editor/ui/kit.h"
#include "test/suite_internal.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    rml_backend *backend;
    kit *k;
    kit_frame_input in;
    int clicks;
    double value, second;
    int changes, commits;
    bool editing;
    int menu_b;
    int chosen;
    const char *order[3];
    int order_count;
    bool open_command;
    int context;
    double flow;
    const char *morph;
    int reveal, rows_emitted, hovered_row;
    char dropped[32];
    char note[64];
    int note_commits;
    bool focus_note;
    bool open_rename;             /* a popover with a name field and one item */
    bool focus_rename, b_renames; /* item B opens the popover focused */
    bool rename_empty;
    char rename[32];
    int rename_commits, rename_applied;
    int panel_menus; /* the panel's menu asked for */
} kit_fixture;

static const kit_command_item COMMANDS[] = {
    { "Alpha", "Commands", NULL, NULL, NULL, 0 },
    { "Beta", "Commands", NULL, NULL, "second", 0 },
    { "Frame Selected", "Commands", NULL, "F", "focus", 0, "Centers the view on the selection." },
};

static const char
    TIP_ABOUT[] = "Fires True when Condition is true, otherwise False, "
                  "and continues from whichever output fired.";

static void emit(kit_fixture *f)
{
    kit *k = f->k;
    kit_root(k, "app");
    kit_push(k, "bar", NULL, "topbar");
    if (kit_menubar_trigger(k, "file", "File")) {
        kit_menu_item_desc a = { .label = "A", .checked = -1 };
        kit_menu_item(k, "a", &a);
        kit_menu_item_desc b = { .label = "B", .checked = -1 };
        if (kit_menu_item(k, "b", &b)) {
            f->menu_b++;
            f->open_rename = f->focus_rename = f->b_renames;
        }
        kit_popup_end(k);
    }
    kit_button_desc button = { .label = "Press" };
    if (kit_button(k, "button", &button)) {
        f->clicks++;
    }
    kit_button_desc rename = { .label = "Rename" };
    if (kit_button(k, "rename-trigger", &rename)) {
        if (kit_popup_is_open(k, "rename")) {
            kit_popup_close(k, "rename");
        } else {
            kit_popup_open(k, "rename");
        }
    }
    kit_leaf(k, "about", NULL, NULL);
    kit_style(k, "width", "40dp");
    kit_style(k, "height", "20dp");
    kit_tip(k, "Branch", NULL, TIP_ABOUT);
    kit_pop(k);
    kit_push(k, "fields", NULL, "prop-scroll");
    kit_style(k, "width", "300dp");
    kit_prop_begin(k, "first", "First");
    kit_number_desc d = { .step = 1, .precision = 2 };
    if (kit_number(k, "value", &f->value, &d)) {
        f->changes++;
    }
    f->commits += kit_item_committed(k);
    f->editing = kit_item_active(k);
    f->context += kit_item_context_clicked(k);
    kit_prop_end(k);
    kit_prop_begin(k, "second", "Second");
    kit_number(k, "value", &f->second, &d);
    kit_prop_end(k);
    kit_prop_begin(k, "note", "Note");
    f->note_commits += kit_text_area(k, "value", f->note, sizeof f->note, NULL, 0);
    if (f->focus_note) {
        kit_focus(k);
        f->focus_note = false;
    }
    kit_prop_end(k);
    kit_panel_begin(k, "panel", "Panel", NULL, true);
    kit_panel_end(k);
    f->panel_menus += kit_panel_menu(k);
    kit_pop(k);
    kit_push(k, "list", NULL, NULL);
    for (int i = 0; i < f->order_count; i++) {
        kit_label(k, f->order[i], "row", f->order[i]);
        kit_style(k, "display", "block");
    }
    kit_pop(k);
    kit_push(k, "motion", NULL, NULL);
    const kit_number_flow_desc plain = { 0 };
    kit_number_flow(k, "flow", NULL, f->flow, &plain);
    kit_number_flow(k, "flow_ref", NULL, 10, &plain);
    kit_morph(k, "morph", NULL, f->morph);
    kit_morph(k, "morph_ref", NULL, "Pause");
    kit_pop(k);
    kit_list_desc list = { .count = 1000, .row_height = 20, .reveal = f->reveal };
    int first, last;
    kit_list_begin(k, "big", NULL, &list, &first, &last);
    f->hovered_row = -1;
    kit_style(k, "height", "200dp");
    kit_style(k, "flex", "0 0 auto");
    for (int i = first; i <= last; i++) {
        char row[16];
        snprintf(row, sizeof row, "r%d", i);
        kit_label(k, row, NULL, row);
        kit_style(k, "display", "block");
        kit_style(k, "height", "20dp");
        if (kit_item_hovered(k)) {
            f->hovered_row = i;
        }
    }
    kit_list_end(k);
    f->rows_emitted = last - first + 1;
    kit_push(k, "dnd", NULL, NULL);
    kit_style(k, "display", "flex");
    kit_style(k, "flex", "0 0 auto");
    kit_leaf(k, "source", NULL, NULL);
    kit_style(k, "width", "60dp");
    kit_style(k, "height", "20dp");
    kit_drag_source(k, &(kit_drag_desc){ .type = "thing", .payload = "apple" });
    kit_leaf(k, "target", NULL, NULL);
    kit_style(k, "width", "60dp");
    kit_style(k, "height", "20dp");
    kit_style(k, "margin-left", "40dp");
    kit_drop_target(k, "thing", f->dropped, sizeof f->dropped);
    kit_pop(k);
    if (f->open_rename) {
        f->open_rename = false;
        kit_popup_open_at(k, "rename", 100, 100);
    }
    if (kit_popup_begin(k, "rename", "popover")) {
        if (f->rename_empty) {
            kit_label(k, "message", NULL, "No controls");
        } else {
            f->rename_commits += kit_text_field(k, "name", f->rename, sizeof f->rename, NULL, 0);
            if (f->focus_rename) {
                kit_focus(k);
                f->focus_rename = false;
            }
            kit_button_desc apply = { .label = "Apply" };
            f->rename_applied += kit_button(k, "apply", &apply);
        }
        kit_popup_end(k);
    }
    if (f->open_command) {
        f->open_command = false;
        kit_command_open(k, "Search");
    }
    int chosen = kit_command(k, COMMANDS, (int)(sizeof COMMANDS / sizeof *COMMANDS));
    if (chosen >= 0) {
        f->chosen = chosen;
    }
}

static void frame(kit_fixture *f)
{
    kit_begin(f->k, &f->in);
    emit(f);
    kit_end(f->k);
    rml_backend_update(f->backend, 1.0 / 60.0);
    f->in.time += 1.0 / 60.0;
    for (int b = 0; b < 3; b++) {
        f->in.pressed[b] = f->in.released[b] = false;
    }
}

static void settle(kit_fixture *f)
{
    for (int i = 0; i < 4; i++) {
        frame(f);
    }
}

static void pointer(kit_fixture *f, rml_input_type type, float x, float y)
{
    f->in.mouse_x = x;
    f->in.mouse_y = y;
    rml_input input = { .type = type, .x = x, .y = y };
    rml_backend_process_input(f->backend, &input);
}

static bool center(kit_fixture *f, const char *path, float *x, float *y)
{
    kit_rect r;
    if (!kit_find(f->k, path, &r)) {
        return false;
    }
    *x = r.x + r.w * 0.5f;
    *y = r.y + r.h * 0.5f;
    return true;
}

static bool click(kit_fixture *f, const char *path)
{
    float x, y;
    if (!center(f, path, &x, &y)) {
        return false;
    }
    pointer(f, RML_INPUT_POINTER_MOVE, x, y);
    pointer(f, RML_INPUT_POINTER_DOWN, x, y);
    pointer(f, RML_INPUT_POINTER_UP, x, y);
    frame(f);
    return true;
}

static void key(kit_fixture *f, rml_input_key k, unsigned modifiers)
{
    rml_input down = { .type = RML_INPUT_KEY_PRESS, .key = k, .modifiers = modifiers };
    rml_backend_process_input(f->backend, &down);
    rml_input up = { .type = RML_INPUT_KEY_RELEASE, .key = k, .modifiers = modifiers };
    rml_backend_process_input(f->backend, &up);
}

static void type(kit_fixture *f, const char *text)
{
    rml_input input = { .type = RML_INPUT_TEXT_INPUT,
                        .text = text,
                        .text_size = strlen(text) };
    rml_backend_process_input(f->backend, &input);
}

/* The first palette row's label as RML with its tags removed. */
static void first_row_text(kit_fixture *f, char *text, size_t size)
{
    rml_element *input = rml_context_focus_element(rml_backend_context(f->backend));
    rml_element *label = input
                           ? rml_element_query_selector(rml_element_owner_document(input), ".cmd-item .label")
                           : NULL;
    char rml[256] = "";
    if (label) {
        rml_element_inner_rml(label, rml, sizeof rml);
    }
    size_t n = 0;
    bool tag = false;
    for (const char *c = rml; *c && n + 1 < size; c++) {
        tag = *c == '<' || (tag && *c != '>');
        if (!tag && *c != '>') {
            text[n++] = *c;
        }
    }
    text[n] = '\0';
}

/* The shown tooltip's size, with its description's text. */
static rml_vec2 tip_shown(kit_fixture *f, const char *path, char *about, size_t size)
{
    float x, y;
    center(f, path, &x, &y);
    pointer(f, RML_INPUT_POINTER_MOVE, 1, 1);
    for (int i = 0; i < 10; i++) {
        frame(f);
    }
    pointer(f, RML_INPUT_POINTER_MOVE, x, y);
    for (int i = 0; i < 45; i++) {
        frame(f);
    }
    rml_element *hovered = rml_context_hover_element(rml_backend_context(f->backend));
    rml_element *document = rml_element_owner_document(hovered);
    rml_element *text = rml_element_query_selector(document, ".tooltip .tip-about");
    about[0] = '\0';
    if (text) {
        rml_element_inner_rml(text, about, size);
    }
    return rml_element_box_size(rml_element_query_selector(document, ".tooltip"), RML_BOX_BORDER);
}

static bool check(bool ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "  kit: %s\n", what);
    }
    return ok;
}

bool test_rmlui_kit()
{
    kit_fixture f = { .value = 10,
                      .second = 3,
                      .chosen = -1,
                      .order = { "a", "b", "c" },
                      .order_count = 3,
                      .flow = 9,
                      .morph = "Play",
                      .reveal = -1 };
    f.backend = rml_backend_create(1024, 640, 1);
    f.k = test_kit_create(rml_backend_context(f.backend), "studio.rml");
    f.in = (kit_frame_input){ .dp_ratio = 1, .width = 1024, .height = 640 };
    settle(&f);
    bool ok = true;
    ok &= check(rml_font_face("noto sans", RML_FONT_NORMAL, 400, 12) != 0, "Noto Sans regular face");
    ok &= check(rml_font_face("noto sans", RML_FONT_NORMAL, 600, 12) != 0, "Noto Sans semibold face");
    ok &= check(rml_font_face("noto sans", RML_FONT_NORMAL, 700, 12) != 0, "Noto Sans bold face");
    ok &= check(rml_font_face("inconsolata", RML_FONT_NORMAL, 400, 12) != 0, "Inconsolata regular face");

    /* Highlighting: JSON keys are names, values keep their token; markup is escaped. */
    char markup[512];
    highlight_markup(HIGHLIGHT_JSON, "{\"x\": -1.5e3, \"on\": true, \"s\": \"a<b\"}", markup, sizeof markup);
    ok &= check(
        strstr(markup, "<span class=\"tok-name\">&quot;x&quot;</span>") &&
            strstr(markup, "tok-number\">-1.5e3<") &&
            strstr(markup, "tok-keyword\">true<") &&
            strstr(markup, "tok-string\">&quot;a&lt;b&quot;<"),
        "json tokens"
    );
    highlight_markup(HIGHLIGHT_LOG, "Could not open content/scenes/Sandbox.json at 42", markup, sizeof markup);
    ok &= check(
        strstr(markup, "tok-error\">Could<") &&
            strstr(markup, "tok-path\">content/scenes/Sandbox.json<") &&
            strstr(markup, "tok-number\">42<"),
        "log tokens"
    );
    /* Markup cut to fit never splits a tag or entity. */
    highlight_markup(HIGHLIGHT_LOG, "x < 1234567", markup, 30);
    ok &= check(strcmp(markup, "x ") == 0, "markup stops before a run whose tags do not fit");
    highlight_markup(HIGHLIGHT_LOG, "x < 1234567", markup, 74);
    const char *partial = "x <span class=\"tok-punct\">&lt;</span> "
                          "<span class=\"tok-number\">123</span>";
    ok &= check(strcmp(markup, partial) == 0, "markup keeps the characters of a run that fit");
    highlight_markup(HIGHLIGHT_LOG, "<<", markup, 36);
    ok &= check(strcmp(markup, "<span class=\"tok-punct\">&lt;</span>") == 0, "markup never cuts an entity");

    ok &= check(click(&f, "app/bar/button") && f.clicks == 1, "button click reports once");
    frame(&f);
    ok &= check(f.clicks == 1, "button click does not repeat");

    /* Scrub: press, move beyond the threshold, release. One commit, no text edit. */
    float x, y;
    ok &= check(center(&f, "app/fields/first/@control/value", &x, &y), "number field is laid out");
    pointer(&f, RML_INPUT_POINTER_MOVE, x, y);
    pointer(&f, RML_INPUT_POINTER_DOWN, x, y);
    frame(&f);
    for (int step = 1; step <= 5; step++) {
        pointer(&f, RML_INPUT_POINTER_MOVE, x + step * 8.0f, y);
        frame(&f);
    }
    pointer(&f, RML_INPUT_POINTER_UP, x + 40, y);
    frame(&f);
    frame(&f);
    ok &= check(f.value > 10 && f.changes > 0, "scrub changes the value");
    ok &= check(f.commits == 1, "scrub commits once");
    ok &= check(!kit_wants_text(f.k, NULL), "releasing a scrub does not start editing");

    /* Click without movement edits; arithmetic relative to the value commits once. */
    double before = f.value;
    f.commits = 0;
    ok &= check(click(&f, "app/fields/first/@control/value"), "number field click");
    ok &= check(kit_wants_text(f.k, NULL), "click enters text editing");
    key(&f, RML_INPUT_KEY_END, 0);
    type(&f, "*2");
    key(&f, RML_INPUT_KEY_ENTER, 0);
    frame(&f);
    frame(&f);
    ok &= check(fabs(f.value - before * 2) < 0.01, "expression doubles the value");
    ok &= check(f.commits == 1, "text entry commits once");
    ok &= check(!kit_wants_text(f.k, NULL), "Enter leaves text editing");

    /* Escape restores the value while editing. */
    before = f.value;
    ok &= check(click(&f, "app/fields/first/@control/value"), "second edit");
    type(&f, "99");
    key(&f, RML_INPUT_KEY_ESCAPE, 0);
    frame(&f);
    ok &= check(f.value == before, "Escape restores the value");

    /* Invalid input keeps editing and does not change the model. */
    ok &= check(click(&f, "app/fields/first/@control/value"), "third edit");
    key(&f, RML_INPUT_KEY_BACKSPACE, 0);
    type(&f, "abc");
    key(&f, RML_INPUT_KEY_ENTER, 0);
    frame(&f);
    ok &= check(kit_wants_text(f.k, NULL) && f.value == before, "invalid input stays in editing");
    key(&f, RML_INPUT_KEY_ESCAPE, 0);
    frame(&f);

    /* Keyboard steps: Up +1, Shift+Up +10. */
    before = f.value;
    key(&f, RML_INPUT_KEY_UP, 0);
    frame(&f);
    key(&f, RML_INPUT_KEY_UP, RML_INPUT_MOD_SHIFT);
    frame(&f);
    ok &= check(fabs(f.value - (before + 11)) < 1e-6, "arrow steps with Shift for coarse");

    /* Right press reports a context click without editing. */
    ok &= check(center(&f, "app/fields/first/@control/value", &x, &y), "field for context");
    pointer(&f, RML_INPUT_POINTER_MOVE, x, y);
    rml_input right = { .type = RML_INPUT_POINTER_DOWN, .x = x, .y = y, .button = 1 };
    rml_backend_process_input(f.backend, &right);
    right.type = RML_INPUT_POINTER_UP;
    rml_backend_process_input(f.backend, &right);
    frame(&f);
    ok &= check(f.context == 1 && !kit_wants_text(f.k, NULL), "right press is a context click");

    /* A panel's menu button asks for its menu without folding the panel, as a
       right press on its header does. */
    rml_element *doc = rml_context_document(rml_backend_context(f.backend), 0);
    rml_element *more = rml_element_query_selector(doc, ".panel[data-menu] .panel-head .menu");
    ok &= check(more != NULL, "the panel's header holds its menu button");
    if (more) {
        rml_vec2 at = rml_element_absolute_offset(more, RML_BOX_BORDER);
        rml_vec2 size = rml_element_box_size(more, RML_BOX_BORDER);
        x = at.x + size.x * 0.5f;
        y = at.y + size.y * 0.5f;
        pointer(&f, RML_INPUT_POINTER_MOVE, x, y);
        pointer(&f, RML_INPUT_POINTER_DOWN, x, y);
        pointer(&f, RML_INPUT_POINTER_UP, x, y);
        frame(&f);
        rml_element *panel = rml_element_query_selector(doc, ".panel[data-menu]");
        ok &= check(
            f.panel_menus == 1 && rml_element_has_attribute(panel, "data-open"),
            "the menu button asks for the menu and leaves the panel open"
        );
        right = (rml_input){ .type = RML_INPUT_POINTER_DOWN, .x = x - 60, .y = y, .button = 1 };
        rml_backend_process_input(f.backend, &right);
        right.type = RML_INPUT_POINTER_UP;
        rml_backend_process_input(f.backend, &right);
        frame(&f);
        ok &= check(f.panel_menus == 2, "a right press on the header asks for it too");
    }

    /* A text area: Shift+Enter starts a line, Enter commits both once. */
    f.focus_note = true;
    frame(&f);
    ok &= check(kit_wants_text(f.k, NULL), "a text area takes focus");
    type(&f, "a");
    key(&f, RML_INPUT_KEY_ENTER, RML_INPUT_MOD_SHIFT);
    type(&f, "b");
    frame(&f);
    ok &= check(f.note_commits == 0 && kit_wants_text(f.k, NULL), "Shift+Enter keeps editing");
    kit_rect one, two;
    ok &= check(kit_find(f.k, "app/fields/note/@control/value", &two), "text area is laid out");
    key(&f, RML_INPUT_KEY_ENTER, 0);
    frame(&f);
    frame(&f);
    ok &= check(strcmp(f.note, "a\nb") == 0 && f.note_commits == 1, "Enter commits the lines once");
    ok &= check(!kit_wants_text(f.k, NULL), "Enter leaves the text area");
    f.focus_note = true;
    settle(&f);
    key(&f, RML_INPUT_KEY_ENTER, 0);
    frame(&f);
    ok &= check(f.note_commits == 2, "Enter commits an unchanged optional text value");
    snprintf(f.note, sizeof f.note, "a");
    settle(&f);
    ok &= check(
        kit_find(f.k, "app/fields/note/@control/value", &one) && two.h > one.h,
        "the text area grows with its lines"
    );

    /* Menubar: press opens, item activation reports once and closes. */
    ok &= check(click(&f, "app/bar/file"), "menu trigger");
    settle(&f);
    ok &= check(kit_popup_is_open(f.k, "menu:app/bar/file"), "trigger opens its menu");
    ok &= check(click(&f, "layer/@menu:app/bar/file/b"), "menu item is laid out");
    frame(&f);
    ok &= check(f.menu_b == 1 && !kit_popup_is_open(f.k, "menu:app/bar/file"), "item activates and closes the menu");
    click(&f, "app/bar/file");
    settle(&f);
    key(&f, RML_INPUT_KEY_TAB, 0);
    frame(&f);
    ok &= check(!kit_popup_is_open(f.k, "menu:app/bar/file"), "Tab still dismisses a menu");

    /* A field in a popup takes typing and Enter; the popup keeps Escape. */
    click(&f, "app/bar/rename-trigger");
    settle(&f);
    ok &= check(kit_wants_text(f.k, NULL), "opening a popover focuses its first tabbable control");
    click(&f, "app/bar/rename-trigger");
    frame(&f);
    ok &= check(!kit_popup_is_open(f.k, "rename"), "pressing a popover's ordinary button trigger again closes it");
    click(&f, "app/bar/rename-trigger");
    settle(&f);
    ok &= check(click(&f, "layer/@rename/name"), "popover field is laid out");
    key(&f, RML_INPUT_KEY_A, 0);
    type(&f, "ab");
    key(&f, RML_INPUT_KEY_ENTER, 0);
    frame(&f);
    ok &= check(
        f.rename_commits == 1 && !strcmp(f.rename, "ab") && !f.rename_applied &&
            kit_popup_is_open(f.k, "rename"),
        "a popover's field takes letters and Enter instead of the menu's "
        "typeahead"
    );
    ok &= check(click(&f, "layer/@rename/name"), "popover field refocuses");
    key(&f, RML_INPUT_KEY_TAB, 0);
    frame(&f);
    rml_element *popover_button = rml_context_focus_element(rml_backend_context(f.backend));
    ok &= check(
        popover_button && !strcmp(rml_element_tag_name(popover_button), "button") &&
            kit_popup_is_open(f.k, "rename"),
        "Tab moves from a popover field to its button without closing"
    );
    key(&f, RML_INPUT_KEY_ENTER, 0);
    frame(&f);
    ok &= check(f.rename_applied == 1 && kit_popup_is_open(f.k, "rename"), "Enter activates a focused popover button");
    key(&f, RML_INPUT_KEY_TAB, RML_KM_SHIFT);
    frame(&f);
    ok &= check(
        kit_wants_text(f.k, NULL) && kit_popup_is_open(f.k, "rename"),
        "Shift+Tab moves from a popover button back to its field"
    );
    key(&f, RML_INPUT_KEY_ESCAPE, 0);
    frame(&f);
    ok &= check(!kit_popup_is_open(f.k, "rename"), "Escape in a popover's field closes the popover");
    rml_element *restored = rml_context_focus_element(rml_backend_context(f.backend));
    char restored_label[32] = "";
    if (restored) {
        rml_element *label = rml_element_query_selector(restored, ".label");
        if (label) {
            rml_element_inner_rml(label, restored_label, sizeof restored_label);
        }
    }
    ok &= check(!strcmp(restored_label, "Rename"), "Escape restores focus to the popover's trigger");
    click(&f, "app/bar/rename-trigger");
    settle(&f);
    key(&f, RML_INPUT_KEY_TAB, RML_KM_SHIFT);
    frame(&f);
    ok &= check(!kit_popup_is_open(f.k, "rename") && !kit_wants_text(f.k, NULL), "Shift+Tab from the first popover control exits to the trigger and closes");
    click(&f, "app/bar/rename-trigger");
    settle(&f);
    key(&f, RML_INPUT_KEY_TAB, 0);
    key(&f, RML_INPUT_KEY_TAB, 0);
    frame(&f);
    ok &= check(!kit_popup_is_open(f.k, "rename"), "Tab from the last popover control exits to the document and closes");
    click(&f, "app/bar/rename-trigger");
    settle(&f);
    rml_element *document = rml_context_document(rml_backend_context(f.backend), 0);
    rml_element *outside = rml_element_query_selector(document, ".topbar > button");
    ok &= check(outside && rml_element_focus(outside, true), "outside document controls remain focusable while a popover is open");
    frame(&f);
    ok &= check(!kit_popup_is_open(f.k, "rename"), "moving focus to an outside control dismisses a nonmodal popover");
    f.rename_empty = true;
    click(&f, "app/bar/rename-trigger");
    settle(&f);
    rml_element *empty_focus = rml_context_focus_element(rml_backend_context(f.backend));
    ok &= check(empty_focus && rml_element_is_class_set(empty_focus, "popover"), "an informational popover focuses itself when it has no tabbable controls");
    rml_element_focus(outside, true);
    frame(&f);
    ok &= check(!kit_popup_is_open(f.k, "rename"), "an informational popover also closes on outside focus");
    f.rename_empty = false;
    f.b_renames = true;
    ok &= check(click(&f, "app/bar/file"), "menu trigger reopens");
    settle(&f);
    ok &= check(click(&f, "layer/@menu:app/bar/file/b"), "menu item is laid out again");
    settle(&f);
    ok &= check(
        kit_popup_is_open(f.k, "rename") && kit_wants_text(f.k, NULL),
        "a popover a menu item opens keeps its field's focus as the menu closes"
    );
    type(&f, "c");
    key(&f, RML_INPUT_KEY_ENTER, 0);
    frame(&f);
    ok &= check(!strcmp(f.rename, "c"), "typing replaces the selection the move kept");
    kit_popup_close(f.k, "rename");
    frame(&f);
    ok &= check(!kit_popup_is_open(f.k, "rename"), "close closes a popup at once");

    /* Command palette: ranking and keyboard selection. */
    f.open_command = true;
    settle(&f);
    ok &= check(kit_command_is_open(f.k), "command opens");
    type(&f, "fs");
    frame(&f);
    key(&f, RML_INPUT_KEY_ENTER, 0);
    frame(&f);
    ok &= check(f.chosen == 2, "the palette ranks Frame Selected by its initials, \"fs\"");
    ok &= check(!kit_command_is_open(f.k), "choosing closes the command");

    /* Rows a new query brings under a still pointer leave the selection on
       the best match (cmdk): only a pointer that moves chooses a row. */
    f.open_command = true;
    settle(&f);
    rml_element *rows[4];
    rml_element *input = rml_context_focus_element(rml_backend_context(f.backend));
    int row_count = rml_element_query_selector_all(rml_element_owner_document(input), ".cmd-item", rows, 4);
    rml_vec2 at = row_count > 1 ? rml_element_absolute_offset(rows[1], RML_BOX_BORDER)
                                : (rml_vec2){ 0 };
    pointer(&f, RML_INPUT_POINTER_MOVE, at.x + 4, at.y + 4);
    frame(&f);
    /* "l" drops Beta, so Frame Selected's row comes under the pointer. */
    f.chosen = -1;
    type(&f, "l");
    settle(&f);
    key(&f, RML_INPUT_KEY_ENTER, 0);
    frame(&f);
    ok &= check(row_count > 1 && f.chosen == 0, "a still pointer leaves Enter on the best match");

    /* Match highlighting keeps the spaces RmlUi would drop as blank text:
       one inside a bold run and one alone between two. */
    char matched_space[64], gap_space[64];
    f.open_command = true;
    settle(&f);
    type(&f, "frame sel");
    frame(&f);
    first_row_text(&f, matched_space, sizeof matched_space);
    key(&f, RML_INPUT_KEY_ESCAPE, 0);
    frame(&f);
    f.open_command = true;
    settle(&f);
    type(&f, "es");
    frame(&f);
    first_row_text(&f, gap_space, sizeof gap_space);
    key(&f, RML_INPUT_KEY_ESCAPE, 0);
    frame(&f);
    ok &= check(strcmp(matched_space, "Frame Selected") == 0, "highlighting keeps a matched space");
    ok &= check(strcmp(gap_space, "Frame Selected") == 0, "highlighting keeps a space between matches");

    /* A described command takes a second line; others keep one. */
    kit_rect plain, described;
    f.open_command = true;
    settle(&f);
    ok &= check(
        kit_find(f.k, "layer/@command/i0", &plain) &&
            kit_find(f.k, "layer/@command/i2", &described) &&
            described.h > plain.h,
        "a command's description is a second line"
    );
    key(&f, RML_INPUT_KEY_ESCAPE, 0);
    settle(&f);

    /* Tooltips: the description wraps under the label, and a zoomed canvas
       keeps the tooltip's size on screen. */
    char about[256];
    rml_vec2 tip = tip_shown(&f, "app/bar/about", about, sizeof about);
    ok &= check(!strcmp(about, TIP_ABOUT), "a tooltip shows its description");
    ok &= check(tip.x < 300 && tip.y > 40, "a tooltip's description wraps");
    f.in.zoom = 2;
    rml_vec2 zoomed = tip_shown(&f, "app/bar/about", about, sizeof about);
    f.in.zoom = 0;
    ok &= check(zoomed.x < tip.x * 0.6f, "a zoomed canvas's tooltip undoes the zoom");

    /* Keyed children reorder and prune without recreating survivors. */
    float ay, cy;
    kit_rect removed;
    settle(&f);
    ok &= check(
        center(&f, "app/list/a", &x, &ay) && center(&f, "app/list/c", &x, &cy) && ay < cy,
        "initial order"
    );
    f.order[0] = "c";
    f.order[1] = "a";
    f.order_count = 2;
    settle(&f);
    ok &= check(center(&f, "app/list/a", &x, &ay) && center(&f, "app/list/c", &x, &cy) && cy < ay, "reordered");
    ok &= check(!kit_find(f.k, "app/list/b", &removed), "unemitted child is removed");

    /* Virtualized list: a window of rows, and reveal scrolls a far row into existence. */
    ok &= check(f.rows_emitted > 0 && f.rows_emitted < 40, "list emits only the visible window");
    ok &= check(!kit_find(f.k, "app/big/r999", &removed), "far rows are not emitted");
    f.reveal = 999;
    settle(&f);
    ok &= check(kit_find(f.k, "app/big/r999", &removed) && f.rows_emitted < 40, "reveal scrolls to the row");

    /* Hover finds the row under the pointer, never an overscan row laid out
       past the list's edge and clipped. */
    kit_rect big;
    ok &= check(kit_find(f.k, "app/big", &big), "list is laid out");
    pointer(&f, RML_INPUT_POINTER_MOVE, big.x + 5, big.y + 5);
    settle(&f);
    ok &= check(f.hovered_row >= 0, "the row under the pointer is hovered");
    pointer(&f, RML_INPUT_POINTER_MOVE, big.x + 5, big.y - 5);
    settle(&f);
    ok &= check(f.hovered_row < 0, "an overscan row outside the list is not hovered");

    /* Drag and drop: picked up after 5 dp, dropped on the target under the pointer. */
    float sx = 0, sy = 0, tx = 0, ty = 0;
    ok &= check(
        center(&f, "app/dnd/source", &sx, &sy) &&
            center(&f, "app/dnd/target", &tx, &ty),
        "drag parts are laid out"
    );
    pointer(&f, RML_INPUT_POINTER_MOVE, sx, sy);
    pointer(&f, RML_INPUT_POINTER_DOWN, sx, sy);
    f.in.down[0] = true;
    frame(&f);
    pointer(&f, RML_INPUT_POINTER_MOVE, sx + 3, sy);
    frame(&f);
    ok &= check(!kit_dragging(f.k, "thing"), "no drag under 5 dp");
    pointer(&f, RML_INPUT_POINTER_MOVE, tx, ty);
    frame(&f);
    ok &= check(kit_dragging(f.k, "thing"), "drag starts past 5 dp");
    pointer(&f, RML_INPUT_POINTER_UP, tx, ty);
    f.in.down[0] = false;
    frame(&f);
    ok &= check(strcmp(f.dropped, "apple") == 0 && !kit_dragging(f.k, NULL), "drop lands on the target");

    /* NumberFlow and Torph settle to exactly what a fresh render of the new value lays out. */
    f.flow = 10;
    f.morph = "Pause";
    frame(&f);
    kit_rect flow, flow_ref, morph, morph_ref;
    ok &= check(kit_find(f.k, "app/motion/morph", &morph) && morph.w > 0, "morph lays out while moving");
    for (int i = 0; i < 90; i++) {
        frame(&f);
    }
    ok &= check(
        kit_find(f.k, "app/motion/flow", &flow) &&
            kit_find(f.k, "app/motion/flow_ref", &flow_ref) && flow.w > 0 &&
            fabsf(flow.w - flow_ref.w) < 0.5f,
        "number flow gains a place and settles"
    );
    ok &= check(
        kit_find(f.k, "app/motion/morph", &morph) &&
            kit_find(f.k, "app/motion/morph_ref", &morph_ref) &&
            fabsf(morph.w - morph_ref.w) < 0.5f,
        "morph settles to the new text"
    );

    /* Toasts: an action reports once and dismisses; an untouched toast expires after four seconds. */
    int undo = kit_toast(f.k, 0, &(kit_toast_desc){ .title = "Deleted", .action = "Undo" });
    for (int i = 0; i < 60; i++) {
        frame(&f);
    }
    ok &= check(click(&f, "layer/@toaster/t1/action"), "toast action is laid out");
    frame(&f);
    ok &= check(kit_toast_action(f.k, undo) && !kit_toast_action(f.k, undo), "toast action reports once");
    pointer(&f, RML_INPUT_POINTER_MOVE, 10, 10);
    kit_toast(f.k, 0, &(kit_toast_desc){ .title = "Saved" });
    for (int i = 0; i < 60; i++) {
        frame(&f);
    }
    ok &= check(kit_find(f.k, "layer/@toaster/t2", &removed), "toast shows");
    for (int i = 0; i < 60 * 4; i++) {
        frame(&f);
    }
    ok &= check(!kit_find(f.k, "layer/@toaster/t2", &removed), "toast expires");

    kit_toast(f.k, 0, &(kit_toast_desc){ .title = "Swipe" });
    for (int i = 0; i < 60; i++) {
        frame(&f);
    }
    float toast_x = 0, toast_y = 0;
    ok &= check(center(&f, "layer/@toaster/t3", &toast_x, &toast_y), "swipe toast is laid out");
    pointer(&f, RML_INPUT_POINTER_MOVE, toast_x, toast_y);
    pointer(&f, RML_INPUT_POINTER_DOWN, toast_x, toast_y);
    f.in.down[0] = true;
    frame(&f);
    pointer(&f, RML_INPUT_POINTER_MOVE, toast_x + 20, toast_y);
    frame(&f);
    pointer(&f, RML_INPUT_POINTER_UP, toast_x + 20, toast_y);
    f.in.down[0] = false;
    frame(&f);
    char toast_transform[96] = "";
    float swipe_x = 0;
    rml_element *swipe_toast = rml_element_query_selector(doc, ".toast");
    bool has_swipe_velocity = swipe_toast &&
                              rml_element_property(swipe_toast, "transform", toast_transform, sizeof toast_transform) <
                                  sizeof toast_transform &&
                              sscanf(toast_transform, "translate(%fdp, %*fdp) scale(%*f)", &swipe_x) ==
                                  1;
    ok &= check(has_swipe_velocity && swipe_x > 35, "toast dismissal carries a quick swipe's velocity into its spring");

    /* Highlighting fills at most the runs it is given, none included. */
    ok &= check(highlight_runs(HIGHLIGHT_EXPRESSION, "a + 1", 5, NULL, 0) == 0, "highlighting without capacity writes nothing");

    /* Text measures in dp, as drawn at a pixel ratio. */
    float hello = kit_text_width(f.k, "Hello", 12, 400, 1);
    ok &= check(hello > 20 && hello < 40, "text measures in dp");
    float large = kit_text_width(f.k, "Hello", 24, 400, 1);
    ok &= check(fabsf(large / (2 * hello) - 1) < 0.1f, "text width follows size");
    ok &= check(kit_text_width(f.k, "Hello", 12, 400, 2) == large / 2, "text measures as drawn at a ratio");
    ok &= check(kit_text_width(f.k, "Hello", 12, 700, 1) > hello, "bold text is wider");

    /* A second context, like the zoomed graph canvas, comes and goes while the
       first keeps working. */
    rml_backend *second = rml_backend_create(200, 100, 2);
    kit *canvas = second ? test_kit_create(rml_backend_context(second), "graph.rml")
                         : NULL;
    ok &= check(canvas != NULL, "a second context loads its document");
    if (canvas) {
        kit_frame_input in = { .dp_ratio = 2, .width = 200, .height = 100 };
        kit_begin(canvas, &in);
        kit_root(canvas, "world");
        kit_label(canvas, "node", NULL, "Node");
        kit_end(canvas);
        rml_backend_update(second, 1.0 / 60);
        kit_rect node;
        ok &= check(kit_find(canvas, "world/node", &node) && node.w > 0, "second context lays out");
        kit_destroy(canvas);
    }
    rml_backend_destroy(second);
    ok &= check(click(&f, "app/bar/button"), "first context works after the second closes");

    kit_destroy(f.k);
    rml_backend_destroy(f.backend);
    return ok;
}
