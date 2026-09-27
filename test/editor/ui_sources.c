/* Where Studio traces UI to: a shown ui asset's elements carry the line that
 * wrote them, the rules that style them report the file and line they came
 * from, and an unsaved edit shows without touching the disk. */
#include "editor/ui/kit.h"
#include "render/ui.h"
#include "runtime/generated.h"
#include "support/text_overlay.h"
#include "support/ui_style.h"
#include "test/suite_internal.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char PAGE_RML[] = "<body\n"
                               "  class=\"page\">\n"
                               "  <div id=\"frame\"><slot/></div>\n"
                               "</body>\n";
static const char
    PAGE_RCSS[] = "#frame { display: block; width: 100%; height: 100%; }\n";

static const char MENU_RML[] = "<div id=\"panel\">\n"
                               "  <use ui=\"Chip\" id=\"chip\"/>\n"
                               "</div>\n";
static const char MENU_RCSS[] = "@import \"theme.rcss\";\n"
                                "div { display: block; }\n"
                                "#panel {\n"
                                "  width: 300dp;\n"
                                "  height: 100dp;\n"
                                "  pointer-events: auto;\n"
                                "}\n";

static const char CHIP_RCSS[] = ".chip {\n"
                                "  width: 40dp;\n"
                                "  height: 30dp;\n"
                                "  pointer-events: auto;\n"
                                "}\n";

static void write_file(const char *root, const char *name, const char *text)
{
    char path[256];
    snprintf(path, sizeof path, "%s/ui/%s", root, name);
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs(text, file);
    fclose(file);
}

/* Whether the file on disk holds `text`, whatever overlay it has. */
static bool disk_holds(const char *path, const char *text)
{
    char buffer[1024];
    FILE *file = fopen(path, "rb");
    size_t length = file ? fread(buffer, 1, sizeof buffer - 1, file) : 0;
    if (file) {
        fclose(file);
    }
    buffer[length] = '\0';
    return file && !strcmp(buffer, text);
}

static void remove_file(const char *root, const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "%s/ui/%s", root, name);
    unlink(path);
}

/* Whether the rule that set `property` on `element` came from the file
 * ending in `file` at line `line`. */
static bool styled_from(rml_element *element, const char *property, const char *file, int line)
{
    char sheet[64], path[1024];
    int reported, source;
    if (!element ||
        !rml_element_property_source(element, property, sheet, sizeof sheet, &reported) ||
        !ui_style_origin(sheet, reported, path, sizeof path, &source)) {
        return false;
    }
    size_t length = strlen(path), suffix = strlen(file);
    return length >= suffix && !strcmp(path + length - suffix, file) && source == line;
}

/* Whether a host's built-in style, which no file holds, set `property`. */
static bool built_in(rml_element *element, const char *property)
{
    char sheet[64], path[1024];
    int reported, source;
    return element &&
           rml_element_property_source(element, property, sheet, sizeof sheet, &reported) &&
           !ui_style_origin(sheet, reported, path, sizeof path, &source);
}

static bool written_at(rml_element *element, const char *source)
{
    char value[64] = "";
    return element &&
           rml_element_attribute(element, "data-src", value, sizeof value) &&
           !strcmp(value, source);
}

/* The element under the pointer at (x, y). */
static rml_element *pick(render_ui *ui, float x, float y)
{
    render_ui_input(ui, &(rml_input){ .type = RML_INPUT_POINTER_MOVE, .x = x, .y = y });
    return rml_context_hover_element(render_ui_context(ui));
}

static void update(render_ui *ui, const ui_world *w)
{
    render_ui_update(ui, w, 1280, 720, 1.0 / 60);
}

/* A game UI host names its sheet, and each element and rule traces back to
 * the page, the asset, its widget or an import. */
static void game_ui_sources(render_ui *ui, const ui_world *w)
{
    update(ui, w);
    rml_element *chip = pick(ui, 10, 10), *panel = pick(ui, 200, 50);
    rml_element *wrapper = chip ? rml_element_parent(chip) : NULL, *frame = panel;
    while (frame && strcmp(rml_element_id(frame), "frame") != 0) {
        frame = rml_element_parent(frame);
    }
    test_check(
        !render_ui_error(ui)[0] && written_at(panel, "Menu:1") &&
            written_at(wrapper, "Menu:2") && written_at(chip, "Chip:1") &&
            written_at(frame, "Page:3"),
        "each shown element carries the asset and .rml line that wrote it"
    );
    test_check(
        styled_from(panel, "width", "/ui/Menu.rcss", 3) &&
            styled_from(chip, "width", "/ui/Chip.rcss", 1) &&
            styled_from(chip, "height", "/ui/theme.rcss", 3) &&
            styled_from(frame, "display", "/ui/Page.rcss", 1) &&
            built_in(frame, "font-family"),
        "each rule traces to its asset's .rcss line or its import's line"
    );
}

/* An unsaved edit remounts at once, from text no file holds. */
static void overlay_remount(render_ui *ui, const ui_world *w, const char *root)
{
    char menu[256], badge[256], badge_styles[256];
    snprintf(menu, sizeof menu, "%s/ui/Menu.rml", root);
    snprintf(badge, sizeof badge, "%s/ui/Badge.rml", root);
    snprintf(badge_styles, sizeof badge_styles, "%s/ui/Badge.rcss", root);
    text_overlay_set(badge, "<div id=\"badge\"/>\n");
    text_overlay_set(
        badge_styles,
        "#badge {\n"
        "  display: block; width: 50dp; height: 50dp; pointer-events: auto;\n"
        "}\n"
    );
    text_overlay_set(menu, "<use ui=\"Badge\"/>\n");
    update(ui, w);
    rml_element *shown = pick(ui, 10, 10);
    struct stat info;
    bool untouched = stat(badge, &info) != 0 && disk_holds(menu, MENU_RML);
    test_check(
        written_at(shown, "Badge:1") &&
            styled_from(shown, "width", "/ui/Badge.rcss", 1) && untouched,
        "an unsaved edit to a ui asset remounts it without touching the disk"
    );
    text_overlay_set(menu, NULL);
    text_overlay_set(badge, NULL);
    text_overlay_set(badge_styles, NULL);
    update(ui, w);
    test_check(written_at(pick(ui, 10, 10), "Chip:1"), "removing the edit shows the saved asset again");
}

static void on_event(void *user, const rml_event *event)
{
    (void)user;
    (void)event;
}

/* A Studio kit names its sheets, and its theme and a panel's rules trace
 * back to their files. */
static void kit_sources(const char *root)
{
    rml_backend *backend = rml_backend_create(640, 480, 1);
    kit *k = kit_create(rml_backend_context(backend), "studio.rml");
    ui_page page = { 0 };
    assert(ui_page_sync(&page, "projects/studio/content", "StudioShell", 0));
    kit_theme(k, page.document.rcss);
    ui_mount *mount = ui_mount_new(root, "Menu");
    rml_listener *listener = rml_listener_create(on_event, NULL);
    kit_frame_input in = { .dp_ratio = 1, .width = 640, .height = 480 };
    rml_element *host = NULL;
    for (int frame = 0; frame < 2; frame++) {
        kit_begin(k, &in);
        kit_root(k, "app");
        host = kit_host(k, "panel", NULL);
        if (ui_mount_sync(mount, host, listener, in.time)) {
            kit_style_sheet(k, "Menu", ui_mount_style(mount));
        }
        kit_end(k);
        rml_backend_update(backend, 1.0 / 60);
        in.time += 1.0 / 60;
    }
    rml_element *doc = rml_element_owner_document(host);
    test_check(
        styled_from(doc, "font-size", "/theme/base.rcss", 4) &&
            styled_from(rml_element_by_id(doc, "app"), "position", "/studio.rcss", 6) &&
            styled_from(rml_element_query_selector(host, "#panel"), "width", "/ui/Menu.rcss", 3),
        "a kit's theme, resources and panel sheets trace to their files"
    );
    kit_destroy(k);
    rml_backend_destroy(backend);
    ui_mount_free(mount);
    rml_listener_destroy(listener);
    ui_page_free(&page);
}

void test_ui_sources()
{
    char root[] = "/tmp/chibi-ui-sources-XXXXXX", folder[64];
    assert(mkdtemp(root));
    snprintf(folder, sizeof folder, "%s/ui", root);
    assert(mkdir(folder, 0700) == 0);
    test_write_ui(folder, "Page", PAGE_RML, PAGE_RCSS);
    test_write_ui(folder, "Menu", MENU_RML, MENU_RCSS);
    test_write_ui(folder, "Chip", "<div class=\"chip\" attrs/>", CHIP_RCSS);
    write_file(root, "theme.rcss", "// tokens\n\n.chip { height: 20dp; }\n");
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    ecs_entity entity = ecs_world_create_entity(world);
    assert(GAME_GameUi_add(world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameUi_get(world, entity) = (GameUi){ .document = "Menu" };
    ui_world w = { .ecs = world, .page = "Page" };
    render_ui *ui = render_ui_new(root);
    game_ui_sources(ui, &w);
    overlay_remount(ui, &w, root);
    render_ui_free(ui);
    ecs_world_free(world);
    kit_sources(root);
    static const char *const ASSETS[] = { "Page", "Menu", "Chip" };
    for (size_t i = 0; i < sizeof ASSETS / sizeof *ASSETS; i++) {
        test_remove_ui(folder, ASSETS[i]);
    }
    remove_file(root, "theme.rcss");
    rmdir(folder);
    rmdir(root);
}
