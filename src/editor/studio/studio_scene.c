/* Studio editors for the authored scene: viewport and Outliner. */
#include "editor/studio/studio_internal.h"
#include "support/array.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>

bool studio_editable(const studio *s)
{
    return scene_can_move(s->scene) && !s->scene->runtime_snapshot_valid;
}

bool studio_matches(const char *query, const char *text)
{
    if (!query[0]) {
        return true;
    }
    for (const char *h = text; *h; h++) {
        size_t i = 0;
        while (query[i] && h[i] &&
               tolower((unsigned char)query[i]) == tolower((unsigned char)h[i])) {
            i++;
        }
        if (!query[i]) {
            return true;
        }
    }
    return false;
}

/* ---------- Scene viewport ---------- */

void studio_scene_header(studio *s, int area)
{
    kit *k = s->kit;
    char key[24];
    snprintf(key, sizeof key, "view:%d", area);
    kit_button_desc view = { .label = "View", .cls = "menu-btn" };
    if (kit_button(k, "view", &view)) {
        kit_popup_open(k, key);
    }
    if (kit_popup_begin(k, key, "menu")) {
        kit_menu_item_desc frame = { .label = "Frame Selected",
                                     .icon = KIT_ICON_FRAME,
                                     .shortcut = "F",
                                     .checked = -1,
                                     .disabled = !selected_actor(s->scene) };
        if (kit_menu_item(k, "frame", &frame)) {
            studio_frame_selection(s);
        }
        kit_menu_item_desc reset = { .label = "Reset Camera", .checked = -1 };
        if (kit_menu_item(k, "reset", &reset)) {
            engine_viewport_reset_camera(s->viewport);
        }
        kit_popup_end(k);
    }
    kit_leaf(k, "spacer", NULL, "spacer");
    kit_button_desc frame = { .icon = KIT_ICON_FRAME,
                              .label = "Frame",
                              .tip = "Frame selected",
                              .shortcut = "F" };
    if (kit_button(k, "frame", &frame)) {
        studio_frame_selection(s);
    }
}

void studio_scene_body(studio *s, int area)
{
    (void)area;
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    kit_push(k, "viewport", NULL, "viewport");
    kit_surface(k, "surface", "surface", "viewport", &s->surface);
    /* A prototype dropped here is placed under the pointer by the viewport. */
    kit_drop_target(k, "prototype", s->dropped_prototype, sizeof s->dropped_prototype);
    kit_push(k, "tools", NULL, "toolshelf");
    kit_button_desc select = { .icon = KIT_ICON_SELECT, .tip = "Select", .flags = KIT_ON };
    kit_button(k, "select", &select);
    kit_tip(k, "Select", NULL, NULL);
    kit_attr(k, "data-tip-side", "right");
    kit_button_desc frame = { .icon = KIT_ICON_FRAME,
                              .tip = "Frame selected",
                              .shortcut = "F" };
    if (kit_button(k, "frame", &frame)) {
        studio_frame_selection(s);
    }
    kit_attr(k, "data-tip-side", "right");
    kit_pop(k);
    kit_push(k, "label", NULL, "view-label");
    kit_label(k, "mode", NULL, "Top orthographic");
    char title[128], context[256];
    scene_actor *actor = selected_actor(scene);
    snprintf(
        context,
        sizeof context,
        "(%s) %s",
        studio_scene_title(scene, title, sizeof title),
        actor ? actor->name : "-"
    );
    kit_label(k, "context", "dim", context);
    kit_pop(k);
    engine_viewport_view view;
    engine_viewport_get_view(s->viewport, &view);
    /* Only once the surface is laid out: a placeholder value would roll on startup. */
    if (s->surface.rect.w > 0) {
        double zoom = roundf(s->surface.rect.w / fmaxf(view.width, 1) * 100);
        kit_number_flow(
            k,
            "zoom",
            "readout",
            zoom,
            &(kit_number_flow_desc){ .grouping = true, .suffix = "%" }
        );
    }
    if (runtime_session(scene)) {
        kit_label(k, "live", "badge accent live-badge", "Live");
    }
    const char *error = app_scene_renderer_error(scene->renderer);
    if (error && error[0]) {
        kit_label(k, "error", "notice", error);
    }
    kit_pop(k);
}

/* ---------- Outliner ---------- */

void studio_outliner_header(studio *s, int area)
{
    kit *k = s->kit;
    kit_search(k, "filter", s->outliner_filter, sizeof s->outliner_filter, "Filter");
    char key[24];
    snprintf(key, sizeof key, "add:%d", area);
    kit_button_desc add = { .icon = KIT_ICON_ADD,
                            .tip = "Add object",
                            .flags = studio_editable(s) ? 0 : KIT_DISABLED };
    if (kit_button(k, "add", &add)) {
        kit_popup_open(k, key);
    }
    if (kit_popup_begin(k, key, "menu")) {
        kit_menu_group(k, "group", "Add");
        for (int i = 0; i < acount(s->scene->prototypes); i++) {
            const game_prototype *prototype = s->scene->prototypes[i].prototype;
            if (!prototype || !prototype->name) {
                continue;
            }
            char item_key[24];
            snprintf(item_key, sizeof item_key, "p%d", i);
            kit_menu_item_desc d = { .label = prototype->name,
                                     .icon = studio_prototype_icon(s, prototype->name),
                                     .checked = -1 };
            if (kit_menu_item(k, item_key, &d)) {
                studio_add_object(s, prototype->name);
            }
        }
        kit_popup_end(k);
    }
}

/* Actors shown by the Outliner filter, in scene order. The caller frees the array. */
int *studio_outliner_rows(studio *s)
{
    int *rows = NULL;
    for (int i = 0; i < acount(s->scene->actors); i++) {
        const scene_actor *actor = &s->scene->actors[i];
        if (studio_matches(s->outliner_filter, actor->name) ||
            studio_matches(s->outliner_filter, actor->prototype_name)) {
            apush(rows, i);
        }
    }
    return rows;
}

void studio_outliner_body(studio *s, int area)
{
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    int *rows = studio_outliner_rows(s);
    int shown = acount(rows);
    int reveal = -1;
    for (int r = 0; r < shown; r++) {
        reveal = rows[r] == scene->selected ? r + 1 : reveal;
    }
    kit_list_desc list = {
        .count = 1 + (s->outliner_collapsed ? 0 : shown),
        .row_height = s->prefs.comfortable ? 26 : 22,
        .reveal = s->outliner_reveal ? reveal : -1,
    };
    int first, last;
    kit_list_begin(k, "tree", "outliner", &list, &first, &last);
    char dropped[256];
    if (kit_drop_target(k, "prototype", dropped, sizeof dropped)) {
        studio_add_object(s, dropped);
    }
    char title[128], count[16];
    snprintf(count, sizeof count, "%d", shown);
    if (first == 0) {
        kit_row_desc root = {
            .label = studio_scene_title(scene, title, sizeof title),
            .icon = KIT_ICON_SCENE,
            .twisty = !s->outliner_collapsed,
            .meta = count,
        };
        if (kit_row_begin(k, "root", &root) & (KIT_ROW_TWISTY | KIT_ROW_DOUBLE_CLICK)) {
            s->outliner_collapsed = !s->outliner_collapsed;
        }
        kit_row_end(k);
    }
    char menu_key[24];
    snprintf(menu_key, sizeof menu_key, "actor:%d", area);
    for (int r = first > 0 ? first - 1 : 0;
         !s->outliner_collapsed && r < shown && r + 1 <= last;
         r++) {
        int i = rows[r];
        scene_actor *actor = &scene->actors[i];
        char key[32], behaviors[16] = "";
        snprintf(
            key,
            sizeof key,
            "e%llu",
            (unsigned long long)ecs_entity_to_u64(actor->entity)
        );
        char **names = instance_behavior_names(scene, actor->entity);
        if (acount(names)) {
            snprintf(behaviors, sizeof behaviors, "%d", acount(names));
        }
        free_behavior_names(names);
        bool selected = scene->selected == i;
        kit_row_desc row = {
            .label = actor->name,
            .icon = studio_prototype_icon(s, actor->prototype_name),
            .depth = 1,
            .twisty = -1,
            .flags = selected ? KIT_ON | KIT_ACTIVE : 0,
        };
        unsigned events = kit_row_begin(k, key, &row);
        if (behaviors[0]) {
            kit_push(k, "behaviors", NULL, "inline row");
            kit_icon(k, "icon", NULL, KIT_ICON_GRAPH);
            kit_label(k, "count", "meta", behaviors);
            kit_pop(k);
        }
        kit_row_end(k);
        if (kit_drop_target(k, "behavior", dropped, sizeof dropped) &&
            studio_editable(s)) {
            finish_transform_edit(scene, false);
            select_instance(scene, i);
            if (app_scene_authoring_set_instance_behavior(scene, dropped, true) == 0) {
                char message[320];
                snprintf(
                    message,
                    sizeof message,
                    "Attached %s to %s",
                    dropped,
                    actor->name
                );
                studio_report(s, message);
            }
        }
        if (events & (KIT_ROW_CLICK | KIT_ROW_CONTEXT)) {
            finish_transform_edit(scene, false);
            select_instance(scene, i);
        }
        if (events & KIT_ROW_DOUBLE_CLICK) {
            studio_frame_selection(s);
        }
        if (events & KIT_ROW_CONTEXT) {
            kit_popup_open_at(k, menu_key, s->in.mouse_x, s->in.mouse_y);
        }
    }
    kit_list_end(k);
    afree(rows);
    s->outliner_reveal = false;
    if (!shown && s->outliner_filter[0]) {
        kit_label(k, "empty", "outliner-empty", "No matches");
    }
    if (kit_popup_begin(k, menu_key, "menu")) {
        scene_actor *actor = selected_actor(scene);
        kit_menu_group(k, "group", actor ? actor->name : "");
        kit_menu_item_desc frame = { .label = "Frame",
                                     .icon = KIT_ICON_FRAME,
                                     .shortcut = "F",
                                     .checked = -1 };
        if (kit_menu_item(k, "frame", &frame)) {
            studio_frame_selection(s);
        }
        kit_menu_item_desc duplicate = { .label = "Duplicate",
                                         .icon = KIT_ICON_COPY,
                                         .shortcut = "Shift+D",
                                         .checked = -1,
                                         .disabled = !studio_editable(s) };
        if (kit_menu_item(k, "duplicate", &duplicate)) {
            studio_duplicate(s);
        }
        kit_menu_item_desc prototype = { .label = "Open Prototype",
                                         .icon = KIT_ICON_PROTOTYPE,
                                         .checked = -1,
                                         .disabled = !actor };
        if (kit_menu_item(k, "prototype", &prototype)) {
            app_scene_authoring_open_prototype(scene, actor->prototype_name);
        }
        kit_menu_separator(k, "s1");
        kit_menu_item_desc remove = { .label = "Delete",
                                      .icon = KIT_ICON_TRASH,
                                      .shortcut = "X",
                                      .checked = -1,
                                      .disabled = !studio_editable(s) };
        if (kit_menu_item(k, "delete", &remove)) {
            studio_delete(s);
        }
        kit_popup_end(k);
    }
}
