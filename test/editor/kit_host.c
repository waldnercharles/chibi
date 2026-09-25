/* A ui asset mounted in a Studio kit element: the same mount a game uses,
 * with its RCSS scoped inside the kit's document. */
#include "editor/ui/kit.h"
#include "render/ui.h"
#include "test/suite_internal.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct
{
    rml_backend *backend;
    kit *k;
    kit_frame_input in;
    rml_listener *listener;
    ui_mount *mount;
    const char *host_key;
    rml_element *host;
} host_fixture;

static void on_event(void *user, const rml_event *event)
{
    ui_mount_listen(user, event);
}

static void frame(host_fixture *f)
{
    kit *k = f->k;
    kit_begin(k, &f->in);
    kit_root(k, "app");
    kit_leaf(k, "own", NULL, "button");
    kit_style(k, "display", "block");
    kit_style(k, "height", "20dp");
    f->host = kit_host(k, f->host_key, NULL);
    kit_style(k, "display", "block");
    kit_style(k, "height", "200dp");
    if (ui_mount_sync(f->mount, f->host, f->listener, f->in.time)) {
        kit_style_sheet(k, "Panel", ui_mount_style(f->mount));
    }
    kit_end(k);
    rml_backend_update(f->backend, 1.0 / 60.0);
    f->in.time += 1.0 / 60.0;
}

static float height_of(rml_element *root, const char *selector)
{
    rml_element *el = rml_element_query_selector(root, selector);
    return el ? rml_element_box_size(el, RML_BOX_BORDER).y : -1;
}

static void click(host_fixture *f, rml_element *el)
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

void test_kit_host()
{
    char root[] = "/tmp/chibi-kit-host-XXXXXX", folder[64], path[96];
    assert(mkdtemp(root));
    snprintf(folder, sizeof folder, "%s/ui", root);
    assert(mkdir(folder, 0700) == 0);
    snprintf(path, sizeof path, "%s/Panel.yaml", folder);
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs(
        "rml: <div class=\"button\" id=\"go\" data-event=\"Go\">Go</div>\n"
        "rcss: '.button { display: block; height: 50dp; }'\n",
        file
    );
    fclose(file);

    host_fixture f = { .host_key = "panel" };
    f.backend = rml_backend_create(640, 480, 1);
    f.k = kit_create(rml_backend_context(f.backend), "studio.rml");
    f.in = (kit_frame_input){ .dp_ratio = 1, .width = 640, .height = 480 };
    f.mount = ui_mount_new(root, "Panel");
    f.listener = rml_listener_create(on_event, f.mount);
    for (int i = 0; i < 3; i++) {
        frame(&f);
    }
    kit_rect own;
    test_check(
        !ui_mount_error(f.mount)[0] && height_of(f.host, "#go") == 50 &&
            kit_find(f.k, "app/own", &own) && own.h == 20,
        "a kit host shows a ui asset whose RCSS applies only inside it"
    );

    click(&f, rml_element_query_selector(f.host, "#go"));
    frame(&f);
    ui_activation a;
    bool taken = ui_mount_take(f.mount, &a) && !strcmp(a.event, "Go") &&
                 !strcmp(a.id, "go");
    test_check(taken, "a data-event in hosted content reaches its mount");

    f.host_key = "moved";
    for (int i = 0; i < 3; i++) {
        frame(&f);
    }
    test_check(height_of(f.host, "#go") == 50, "a mount follows its host to a new element");

    kit_destroy(f.k);
    rml_backend_destroy(f.backend);
    ui_mount_free(f.mount);
    rml_listener_destroy(f.listener);
    unlink(path);
    rmdir(folder);
    rmdir(root);
}
