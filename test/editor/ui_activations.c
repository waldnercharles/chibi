/* Activations a mounted ui asset sends (render/ui.h): emit's arguments, the
 * pointer, button and modifier keys of the event that sent it, and its
 * sending element as a popover's anchor; and contextmenu, which the backend
 * sends on a right press as browsers do. */
#include "render/ui.h"
#include "test/suite_internal.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct
{
    rml_backend *backend;
    rml_element *doc, *host;
    rml_listener *listener;
    ui_mount *mount;
    ui_data data;
} fixture;

static void on_event(void *user, const rml_event *event)
{
    ui_mount_listen(user, event);
}

static void on_emit(void *user, const rml_event *event, const char *name, int argc, const char *const *args)
{
    ui_mount_emit(user, event, name, argc, args);
}

static void update(fixture *f)
{
    ui_mount_sync(f->mount, f->host, f->listener, 0);
    rml_backend_update(f->backend, 1.0 / 60.0);
}

/* Ui asset `name` in `root` mounted in a shown document's #host. */
static fixture open_fixture(const char *root, const char *name)
{
    fixture f = { .backend = rml_backend_create(320, 240, 1) };
    rml_context *context = rml_backend_context(f.backend);
    f.mount = ui_mount_new(root, name);
    f.listener = rml_listener_create(on_event, f.mount);
    ui_data_init(&f.data, context, NULL, on_emit, f.mount);
    f.doc = rml_context_load_document_from_memory(
        context,
        "<rml><head><style>body { font-family: Inter; } div { display: block; "
        "height: 40dp; } #host { height: auto; }</style></head>"
        "<body><div id=\"host\"/></body></rml>",
        "[memory]"
    );
    assert(f.doc);
    rml_document_show(f.doc, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    f.host = rml_element_by_id(f.doc, "host");
    update(&f);
    return f;
}

static void close_fixture(fixture *f)
{
    rml_document_close(f->doc);
    rml_backend_update(f->backend, 1.0 / 60.0);
    ui_data_free(&f->data);
    ui_mount_free(f->mount);
    rml_listener_destroy(f->listener);
    rml_backend_destroy(f->backend);
}

/* Presses and releases `button` over the middle of `el` with `modifiers`
 * (RML_INPUT_MOD_*). */
static void press(fixture *f, rml_element *el, int button, unsigned modifiers)
{
    rml_vec2 at = rml_element_absolute_offset(el, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    float x = at.x + size.x * 0.5f, y = at.y + size.y * 0.5f;
    rml_input_type types[] = { RML_INPUT_POINTER_MOVE,
                               RML_INPUT_POINTER_DOWN,
                               RML_INPUT_POINTER_UP };
    for (size_t i = 0; i < sizeof types / sizeof *types; i++) {
        rml_input input = { .type = types[i],
                            .x = x,
                            .y = y,
                            .button = button,
                            .modifiers = modifiers };
        rml_backend_process_input(f->backend, &input);
    }
    update(f);
}

/* emit('Name', ...) keeps up to UI_ACTIVATION_ARGS arguments, reads the
 * first as the value when it is a number, and records the click's pointer,
 * button and modifiers; the anchor is the element whose binding sent it,
 * and reads NULL once that element is gone. */
static void emit_context(const char *root, const char *folder)
{
    test_write_ui(
        folder,
        "Rows",
        "<div id=\"row\" data-event-click=\"emit('outliner.select', 'e12', 2, "
        "'x', 'y', 'z')\">Row</div>\n"
        "<div id=\"count\" data-event-click=\"emit('Count', 3)\">3</div>\n",
        NULL
    );
    fixture f = open_fixture(root, "Rows");
    rml_element *row = rml_element_query_selector(f.host, "#row");
    press(&f, row, 0, RML_INPUT_MOD_SHIFT);
    ui_activation a;
    bool taken = ui_mount_take(f.mount, &a);
    rml_vec2 at = rml_element_absolute_offset(row, RML_BOX_BORDER);
    bool args = taken && !strcmp(a.event, "outliner.select") &&
                !strcmp(a.id, "row") && a.argc == UI_ACTIVATION_ARGS &&
                !strcmp(a.args[0], "e12") && !strcmp(a.args[1], "2") &&
                !strcmp(a.args[3], "y") && !a.has_value;
    bool context = taken && (a.modifiers & RML_KM_SHIFT) && a.button == 0 &&
                   fabsf(a.y - (at.y + 20)) <= 1;
    bool anchored = taken && rml_element_ref_get(a.anchor) == row;
    press(&f, rml_element_query_selector(f.host, "#count"), 0, 0);
    ui_activation counted;
    bool valued = ui_mount_take(f.mount, &counted) && counted.has_value &&
                  counted.value == 3 && !(counted.modifiers & RML_KM_SHIFT);
    rml_element_set_inner_rml(f.host, "");
    bool released = !rml_element_ref_get(counted.anchor);
    if (!(args && context && anchored && valued && released)) {
        printf("  emit context: %d %d %d %d %d\n", args, context, anchored, valued, released);
    }
    test_check(
        args && context && anchored && valued && released,
        "an activation keeps emit's arguments, its event's pointer, button and "
        "modifiers, "
        "and its sending element as an anchor until that element goes"
    );
    close_fixture(&f);
    test_remove_ui(folder, "Rows");
}

/* A right press sends contextmenu after its mousedown, as browsers do, so
 * data-event-contextmenu opens a menu with the press's button; a left press
 * sends none. */
static void context_menu(const char *root, const char *folder)
{
    test_write_ui(folder, "Menu", "<div id=\"target\" data-event-contextmenu=\"emit('outliner.menu', 'e3')\">Row</div>\n", NULL);
    fixture f = open_fixture(root, "Menu");
    rml_element *target = rml_element_query_selector(f.host, "#target");
    ui_activation a;
    press(&f, target, 0, 0);
    bool left = !ui_mount_take(f.mount, &a);
    press(&f, target, 1, RML_INPUT_MOD_CONTROL);
    bool right = ui_mount_take(f.mount, &a) && !strcmp(a.event, "outliner.menu") &&
                 a.button == 1 && (a.modifiers & RML_KM_CTRL) && a.argc == 1 &&
                 rml_element_ref_get(a.anchor) == target;
    test_check(left && right, "a right press sends contextmenu, which data-event-contextmenu hears with its button and modifiers");
    close_fixture(&f);
    test_remove_ui(folder, "Menu");
}

void test_ui_activations()
{
    char root[] = "/tmp/chibi-ui-activations-XXXXXX", folder[64];
    assert(mkdtemp(root));
    snprintf(folder, sizeof folder, "%s/ui", root);
    assert(mkdir(folder, 0700) == 0);
    emit_context(root, folder);
    context_menu(root, folder);
    rmdir(folder);
    rmdir(root);
}
