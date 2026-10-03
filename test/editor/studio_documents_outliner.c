/* The Outliner: renaming objects in place, and the Hierarchy a UI
 * document shows there. */

#include "assets/ui_events.h"
#include "editor/collab/collab.h"
#include "editor/studio/studio_designer_internal.h"
#include "editor/studio/studio_graph_internal.h"
#include "editor/studio/studio_internal.h"
#include "editor/ui/input_feel.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/editor_internal.h"
#include "editor/workspace/input.h"
#include "graph/document/serialize.h"
#include "graph/nodes/component_nodes.h"
#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "render/screenshot.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_file.h"
#include "support/text_overlay.h"
#include "test/editor/studio_documents.h"
#include "test/suite_internal.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Selecting on Canvas keeps the same widget visible in its Hierarchy. */
static void designer_selection_reveals_hierarchy()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    s->prefs.code_apply_live = false;
    char path[PATH_MAX];
    assert(realpath(BANK, path) && studio_open_ui_source(s, path, 1));
    frames(&f, 3);
    ui_buffer *buffer = studio_ui_buffer(s);
    const char *markup = "<div id=\"panel\"><button "
                         "id=\"target\">Select</button></div>\n";
    text_edit edit = { 0, (int)strlen(ui_buffer_text(buffer, UI_PART_RML)), markup };
    ui_buffer_edit(buffer, UI_PART_RML, &edit, 1);
    frames(&f, 3);
    studio_designer *d = designer_state(s, &buffer);
    assert(acount(d->rml->elements) == 2);
    designer_select(s, 0);
    assert(studio_designer_key(s, STUDIO_EDITOR_OUTLINER, SDLK_LEFT, 0));
    snprintf(d->filter, sizeof d->filter, "absent");
    d->closed = true;
    s->ui_schedule.wake = true;
    frames(&f, 2);
    bool hidden = !studio_element(s, ".designer-tree .tree-row[data-selected] .name");
    designer_select(s, 1);
    s->ui_schedule.wake = true;
    frames(&f, 3);
    rml_element *name = studio_element(s, ".designer-tree .tree-row[data-selected] .name");
    char text[32] = "";
    if (name) {
        rml_element_inner_rml(name, text, sizeof text);
    }
    if (!(hidden && name && !strcmp(text, "button") && !d->closed &&
          !d->filter[0] && !acount(d->folded))) {
        printf(
            "  Hierarchy hidden %d, row %s, selected %d, closed %d, filter "
            "'%s', folded %d\n",
            hidden,
            name ? text : "missing",
            d->selected,
            d->closed,
            d->filter,
            acount(d->folded)
        );
    }
    test_check(
        hidden && name && !strcmp(text, "button") && !d->closed &&
            !d->filter[0] && !acount(d->folded),
        "Canvas selection reveals its Hierarchy row through collapsed "
        "ancestors and an excluding filter"
    );
    snprintf(d->filter, sizeof d->filter, "target");
    designer_select(s, 1);
    test_check(!strcmp(d->filter, "target"), "selection keeps a Hierarchy filter that already includes its row");
    fixture_free(&f);
}

static void mouse_over(studio_fixture *f, studio_area_slot area)
{
    kit_rect r = f->studio->areas[area].rect;
    mouse_move(f->studio, r.x + r.w * 0.5f, r.y + r.h * 0.5f);
    frames(f, 1);
}

static bool named(const app_scene_authoring *scene, const char *name)
{
    for (int i = 0; i < acount(scene->actors); i++) {
        if (!strcmp(scene->actors[i].name, name)) {
            return true;
        }
    }
    return false;
}

/* Renames one object at a time: F2 over the Outliner edits its row, Enter
   renames as one Undo step, Escape keeps the name and a name in use is
   refused; without the Outliner, F2 edits Properties' name field. */
static void rename_objects()
{
    studio_fixture f = side_open();
    studio *s = f.studio;
    app_scene_authoring *scene = f.scene;
    studio_add_object(s, "Vault");
    select_instance(scene, 0);
    frames(&f, 2);
    mouse_over(&f, STUDIO_AREA_OUTLINER);
    tap(SDL_SCANCODE_F2);
    frames(&f, 3);
    bool field = s->rename.entity && s->rename.outliner && kit_wants_keyboard(s->kit);
    text_event("Strongroom");
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 2);
    bool renamed = named(scene, "Strongroom") && !named(scene, "Vault") &&
                   !s->rename.entity;
    studio_history(s, false);
    bool undone = named(scene, "Vault") && !named(scene, "Strongroom");
    studio_history(s, true);
    bool redone = named(scene, "Strongroom");
    frames(&f, 1);
    tap(SDL_SCANCODE_F2);
    frames(&f, 3);
    text_event("Nope");
    tap(SDL_SCANCODE_ESCAPE);
    frames(&f, 2);
    bool kept = named(scene, "Strongroom") && !named(scene, "Nope") &&
                !s->rename.entity;
    select_instance(scene, 1);
    frames(&f, 1);
    tap(SDL_SCANCODE_F2);
    frames(&f, 3);
    text_event("Strongroom");
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 2);
    bool refused = named(scene, "Vault_1") && strstr(scene->message, "Strongroom");
    studio_active_layout(s)->editors[STUDIO_AREA_OUTLINER] = STUDIO_EDITOR_ASSETS;
    frames(&f, 2);
    mouse_over(&f, STUDIO_AREA_TOP);
    tap(SDL_SCANCODE_F2);
    frames(&f, 3);
    bool properties = s->tab == STUDIO_TAB_OBJECT && kit_wants_keyboard(s->kit);
    text_event("Annex");
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 2);
    bool inspector = named(scene, "Annex") && !named(scene, "Vault_1");
    if (!(field && renamed && undone && redone && kept && refused &&
          properties && inspector)) {
        printf("  %d %d %d %d %d %d %d %d\n", field, renamed, undone, redone, kept, refused, properties, inspector);
    }
    test_check(
        field && renamed && undone && redone && kept,
        "F2 over the Outliner renames its row in place as one Undo step, "
        "and Escape keeps the name"
    );
    test_check(
        refused && properties && inspector,
        "a name in use is refused, and F2 edits Properties' name field "
        "while no area shows the Outliner"
    );
    fixture_free(&f);
}

void studio_documents_outliner_checks()
{
    designer_selection_reveals_hierarchy();
    rename_objects();
}
