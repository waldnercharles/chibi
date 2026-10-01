#include "editor/collab/collab.h"
#include "editor/studio/studio_graph_internal.h"
#include "editor/studio/studio_internal.h"
#include "editor/workspace/editor_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

static yyjson_mut_val *number(yyjson_mut_doc *doc, double value)
{
    return yyjson_mut_real(doc, isfinite(value) ? value : 0);
}

static void add_layout(yyjson_mut_doc *doc, yyjson_mut_val *array, const studio_layout *layout)
{
    yyjson_mut_val *value = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_bool(doc, value, "side_open", layout->side_open);
    yyjson_mut_obj_add_val(doc, value, "side_width", number(doc, layout->side_width));
    yyjson_mut_obj_add_val(doc, value, "main_split", number(doc, layout->main_split));
    yyjson_mut_obj_add_val(doc, value, "side_split", number(doc, layout->side_split));
    yyjson_mut_obj_add_bool(doc, value, "drawer_open", layout->drawer_open);
    yyjson_mut_obj_add_val(doc, value, "drawer_height", number(doc, layout->drawer_height));
    yyjson_mut_val *editors = yyjson_mut_obj_add_arr(doc, value, "editors");
    yyjson_mut_val *panels = yyjson_mut_obj_add_arr(doc, value, "panels");
    for (int i = 0; i < STUDIO_AREA_COUNT; i++) {
        yyjson_mut_arr_add_int(doc, editors, layout->editors[i]);
        yyjson_mut_arr_add_strcpy(doc, panels, layout->panels[i]);
    }
}

static char *relative_content_path(const studio *s, const char *path)
{
    if (!path || !s->scene->content_root) {
        return NULL;
    }
    size_t root = strlen(s->scene->content_root);
    if (strncmp(path, s->scene->content_root, root) || path[root] != '/') {
        return NULL;
    }
    return xstrdup(path + root + 1);
}

static char *capture(studio *s, bool pointer)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "family", "presentation");
    yyjson_mut_obj_add_int(doc, root, "workspace", s->prefs.workspace);
    yyjson_mut_val *layouts = yyjson_mut_obj_add_arr(doc, root, "layouts");
    for (int i = 0; i < STUDIO_WORKSPACE_COUNT; i++) {
        add_layout(doc, layouts, &s->prefs.layouts[i]);
    }
    yyjson_mut_obj_add_int(doc, root, "focus_area", s->focus_area);
    yyjson_mut_obj_add_int(doc, root, "maximized", s->maximized);
    if (s->scene->active_tab >= 0 && s->scene->active_tab < acount(s->scene->tabs)) {
        yyjson_mut_obj_add_strcpy(
            doc,
            root,
            "tab",
            s->scene->tabs[s->scene->active_tab].key
        );
    } else {
        yyjson_mut_obj_add_null(doc, root, "tab");
    }
    engine_viewport_view camera;
    engine_viewport_get_view(s->viewport, &camera);
    double camera_values[] = { camera.center_x,
                               camera.center_y,
                               camera.width,
                               camera.height };
    yyjson_mut_obj_add_val(doc, root, "scene_camera", yyjson_mut_arr_with_real(doc, camera_values, 4));
    yyjson_mut_val *graph = yyjson_mut_obj_add_obj(doc, root, "graph_camera");
    yyjson_mut_obj_add_uint(doc, graph, "id", s->graph.graph_id);
    yyjson_mut_obj_add_val(doc, graph, "zoom", number(doc, sg_zoom(s)));
    yyjson_mut_obj_add_val(doc, graph, "x", number(doc, s->graph.x));
    yyjson_mut_obj_add_val(doc, graph, "y", number(doc, s->graph.y));

    yyjson_mut_val *selection = yyjson_mut_obj_add_obj(doc, root, "selection");
    uint64_t actor = 0;
    if (s->scene->selected >= 0 && s->scene->selected < acount(s->scene->actors)) {
        actor = s->scene->actors[s->scene->selected].identity;
    }
    yyjson_mut_obj_add_uint(doc, selection, "actor", actor);
    yyjson_mut_val *nodes = yyjson_mut_obj_add_arr(doc, selection, "nodes");
    editor *e = s->scene->graph_editor;
    if (e) {
        for (int i = 0; i < acount(e->selected); i++) {
            yyjson_mut_arr_add_uint(doc, nodes, e->selected[i]->id);
        }
    }
    yyjson_mut_obj_add_uint(doc, selection, "graph", s->graph.graph_id);

    yyjson_mut_val *text = yyjson_mut_obj_add_obj(doc, root, "text");
    char *path = relative_content_path(s, s->code.editor_path);
    if (path) {
        yyjson_mut_obj_add_strcpy(doc, text, "path", path);
        free(path);
        yyjson_mut_obj_add_int(doc, text, "part", s->code.editor_part);
        yyjson_mut_obj_add_int(
            doc,
            text,
            "caret",
            s->code.editor ? kit_code_cursor(s->code.editor) : 0
        );
    }
    if (pointer && s->hover_area >= 0 && s->hover_area < STUDIO_AREA_COUNT) {
        kit_rect area = s->areas[s->hover_area].rect;
        if (area.w > 0 && area.h > 0) {
            yyjson_mut_val *point = yyjson_mut_obj_add_obj(doc, root, "pointer");
            yyjson_mut_obj_add_int(doc, point, "area", s->hover_area);
            yyjson_mut_obj_add_val(doc, point, "x", number(doc, (s->in.mouse_x - area.x) / area.w));
            yyjson_mut_obj_add_val(doc, point, "y", number(doc, (s->in.mouse_y - area.y) / area.h));
        }
    }
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}

char *studio_collab_capture(studio *s)
{
    return capture(s, true);
}

static bool read_num(yyjson_val *object, const char *key, double *out)
{
    yyjson_val *value = yyjson_obj_get(object, key);
    if (!yyjson_is_num(value)) {
        return false;
    }
    *out = yyjson_get_num(value);
    return isfinite(*out);
}

static bool read_layout(yyjson_val *value, studio_layout *layout)
{
    if (!yyjson_is_obj(value)) {
        return false;
    }
    double side_width, main_split, side_split, drawer_height;
    yyjson_val *editors = yyjson_obj_get(value, "editors");
    yyjson_val *panels = yyjson_obj_get(value, "panels");
    if (!read_num(value, "side_width", &side_width) ||
        !read_num(value, "main_split", &main_split) ||
        !read_num(value, "side_split", &side_split) ||
        !read_num(value, "drawer_height", &drawer_height) ||
        !yyjson_is_arr(editors) || yyjson_arr_size(editors) != STUDIO_AREA_COUNT ||
        !yyjson_is_arr(panels) || yyjson_arr_size(panels) != STUDIO_AREA_COUNT) {
        return false;
    }
    layout->side_open = yyjson_get_bool(yyjson_obj_get(value, "side_open"));
    layout->drawer_open = yyjson_get_bool(yyjson_obj_get(value, "drawer_open"));
    layout->side_width = (float)fmax(180, fmin(side_width, 1200));
    layout->main_split = (float)fmax(0.15, fmin(main_split, 0.85));
    layout->side_split = (float)fmax(0.15, fmin(side_split, 0.85));
    layout->drawer_height = (float)fmax(80, fmin(drawer_height, 1600));
    for (int i = 0; i < STUDIO_AREA_COUNT; i++) {
        yyjson_val *editor_value = yyjson_arr_get(editors, i);
        yyjson_val *panel_value = yyjson_arr_get(panels, i);
        int editor_id = (int)yyjson_get_int(editor_value);
        const char *panel = yyjson_get_str(panel_value);
        if (!yyjson_is_int(editor_value) || editor_id < 0 ||
            editor_id >= STUDIO_EDITOR_COUNT || !panel ||
            strlen(panel) >= sizeof layout->panels[i]) {
            return false;
        }
        layout->editors[i] = (studio_editor)editor_id;
        snprintf(layout->panels[i], sizeof layout->panels[i], "%s", panel);
    }
    return true;
}

static int tab_by_key(const app_scene_authoring *scene, const char *key)
{
    for (int i = 0; i < acount(scene->tabs); i++) {
        if (!strcmp(scene->tabs[i].key, key)) {
            return i;
        }
    }
    return -1;
}

bool studio_collab_restore(studio *s, const char *json)
{
    yyjson_doc *doc = json ? yyjson_read(json, strlen(json), 0) : NULL;
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    yyjson_val *family = yyjson_obj_get(root, "family");
    yyjson_val *layouts = yyjson_obj_get(root, "layouts");
    yyjson_val *camera_array = yyjson_obj_get(root, "scene_camera");
    if (!yyjson_is_obj(root) || !yyjson_is_str(family) ||
        strcmp(yyjson_get_str(family), "presentation") || !yyjson_is_arr(layouts) ||
        yyjson_arr_size(layouts) != STUDIO_WORKSPACE_COUNT ||
        !yyjson_is_arr(camera_array) || yyjson_arr_size(camera_array) != 4) {
        yyjson_doc_free(doc);
        return false;
    }
    studio_layout parsed[STUDIO_WORKSPACE_COUNT];
    for (int i = 0; i < STUDIO_WORKSPACE_COUNT; i++) {
        parsed[i] = s->prefs.layouts[i];
        if (!read_layout(yyjson_arr_get(layouts, i), &parsed[i])) {
            yyjson_doc_free(doc);
            return false;
        }
    }
    double camera_values[4];
    for (int i = 0; i < 4; i++) {
        yyjson_val *value = yyjson_arr_get(camera_array, i);
        if (!yyjson_is_num(value) || !isfinite(yyjson_get_num(value))) {
            yyjson_doc_free(doc);
            return false;
        }
        camera_values[i] = yyjson_get_num(value);
    }
    int workspace = (int)yyjson_get_int(yyjson_obj_get(root, "workspace"));
    if (workspace < 0 || workspace >= STUDIO_WORKSPACE_COUNT) {
        yyjson_doc_free(doc);
        return false;
    }
    if (!s->collaboration.saved_prefs) {
        s->collaboration.local_prefs = s->prefs;
        s->collaboration.saved_prefs = true;
    }
    memcpy(s->prefs.layouts, parsed, sizeof parsed);
    s->prefs.workspace = (studio_workspace)workspace;
    int focus = (int)yyjson_get_int(yyjson_obj_get(root, "focus_area"));
    int maximized = (int)yyjson_get_int(yyjson_obj_get(root, "maximized"));
    s->focus_area = focus >= 0 && focus < STUDIO_AREA_COUNT ? focus : -1;
    s->maximized = maximized >= 0 && maximized < STUDIO_AREA_COUNT ? maximized : -1;
    yyjson_val *tab = yyjson_obj_get(root, "tab");
    const char *key = yyjson_get_str(tab);
    if (key) {
        int index = tab_by_key(s->scene, key);
        if (index >= 0 && index != s->scene->active_tab) {
            scene_tabs_activate(s->scene, index);
        }
    }
    engine_viewport_view camera = { (float)camera_values[0],
                                    (float)camera_values[1],
                                    (float)camera_values[2],
                                    (float)camera_values[3] };
    if (camera.width > 0 && camera.height > 0) {
        engine_viewport_set_view(s->viewport, &camera);
        s->scene->camera.view = camera;
        s->scene->camera.saved = true;
    }
    yyjson_val *graph = yyjson_obj_get(root, "graph_camera");
    double zoom, gx, gy;
    if (yyjson_is_obj(graph) && read_num(graph, "zoom", &zoom) &&
        read_num(graph, "x", &gx) && read_num(graph, "y", &gy)) {
        s->graph.graph_id = yyjson_get_uint(yyjson_obj_get(graph, "id"));
        float local_zoom = (float)(zoom * 12.0 / s->prefs.standard_font_size);
        s->graph.zoom = fminf(fmaxf(local_zoom, 0.1f), 2.0f);
        s->graph.x = (float)gx;
        s->graph.y = (float)gy;
    }
    yyjson_val *selection = yyjson_obj_get(root, "selection");
    uint64_t actor = yyjson_get_uint(yyjson_obj_get(selection, "actor"));
    if (actor) {
        for (int i = 0; i < acount(s->scene->actors); i++) {
            if (s->scene->actors[i].identity == actor) {
                select_instance(s->scene, i);
                break;
            }
        }
    }
    editor *e = s->scene->graph_editor;
    yyjson_val *nodes = yyjson_obj_get(selection, "nodes");
    uint64_t graph_id = yyjson_get_uint(yyjson_obj_get(selection, "graph"));
    if (e && e->active_graph && graph_id == s->graph.graph_id && yyjson_is_arr(nodes)) {
        afree(e->selected);
        size_t i, count;
        yyjson_val *node_id;
        yyjson_arr_foreach(nodes, i, count, node_id)
        {
            node *selected = yyjson_is_uint(node_id)
                               ? graph_node_by_id(e->active_graph, yyjson_get_uint(node_id))
                               : NULL;
            if (selected) {
                apush(e->selected, selected);
            }
        }
    }
    s->collaboration.restoring_prefs = true;
    yyjson_doc_free(doc);
    return true;
}

static studio_collab_peer *peer_find(studio *s, const char *client)
{
    for (int i = 0; i < acount(s->collaboration.peers); i++) {
        if (!strcmp(s->collaboration.peers[i].client, client)) {
            return &s->collaboration.peers[i];
        }
    }
    return NULL;
}

void studio_collab_receive(studio *s, const char *client, const char *name, uint32_t color, const char *json)
{
    if (!client || !strcmp(client, collab_client())) {
        return;
    }
    studio_collab_peer *peer = peer_find(s, client);
    if (!json) {
        if (peer) {
            int index = (int)(peer - s->collaboration.peers);
            free(peer->client);
            free(peer->name);
            free(peer->presentation);
            adel(s->collaboration.peers, index);
            if (!strcmp(s->collaboration.follow_client, client)) {
                studio_collab_unfollow(s);
            }
        }
        return;
    }
    if (!peer) {
        studio_collab_peer created = { .client = xstrdup(client) };
        apush(s->collaboration.peers, created);
        peer = &alast(s->collaboration.peers);
    }
    free(peer->name);
    free(peer->presentation);
    peer->name = xstrdup(name && name[0] ? name : client);
    peer->presentation = xstrdup(json);
    peer->color = color;
    peer->pointer_valid = false;
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *pointer = doc ? yyjson_obj_get(yyjson_doc_get_root(doc), "pointer")
                              : NULL;
    double x, y;
    if (yyjson_is_obj(pointer) && read_num(pointer, "x", &x) &&
        read_num(pointer, "y", &y)) {
        peer->pointer_area = (int)yyjson_get_int(yyjson_obj_get(pointer, "area"));
        peer->pointer_x = (float)fmax(0, fmin(x, 1));
        peer->pointer_y = (float)fmax(0, fmin(y, 1));
        peer->pointer_valid = peer->pointer_area >= 0 &&
                              peer->pointer_area < STUDIO_AREA_COUNT;
    }
    yyjson_doc_free(doc);
    if (s->collaboration.following && !strcmp(s->collaboration.follow_client, client)) {
        studio_collab_restore(s, json);
    }
}

static void color_text(char *out, size_t size, uint32_t color)
{
    snprintf(out, size, "#%08x", color ? color : 0x6ea8feffu);
}

void studio_collab_emit(studio *s)
{
    for (int i = 0; i < acount(s->collaboration.peers); i++) {
        studio_collab_peer *peer = &s->collaboration.peers[i];
        if (!peer->pointer_valid) {
            continue;
        }
        kit_rect area = s->areas[peer->pointer_area].rect;
        if (area.w <= 0 || area.h <= 0) {
            continue;
        }
        char key[80], left[32], top[32], color[16];
        snprintf(key, sizeof key, "peer-%s", peer->client);
        snprintf(left, sizeof left, "%.1fdp", area.x + area.w * peer->pointer_x);
        snprintf(top, sizeof top, "%.1fdp", area.y + area.h * peer->pointer_y);
        color_text(color, sizeof color, peer->color);
        kit_push(s->kit, "presence", NULL, "collab-overlay");
        kit_push(s->kit, key, NULL, "collab-cursor");
        kit_style(s->kit, "left", left);
        kit_style(s->kit, "top", top);
        kit_style(s->kit, "border-color", color);
        kit_style(s->kit, "background-color", color);
        kit_icon(s->kit, "arrow", "icon", KIT_ICON_MOUSE);
        kit_push(s->kit, "label", NULL, "collab-cursor-label");
        kit_style(s->kit, "background-color", color);
        kit_label(s->kit, "name", NULL, peer->name);
        kit_pop(s->kit);
        kit_pop(s->kit);
        kit_pop(s->kit);
    }
}

static bool enter_pressed(const studio *s)
{
    for (int i = 0; i < acount(s->keys); i++) {
        if ((s->keys[i].key == SDLK_RETURN || s->keys[i].key == SDLK_KP_ENTER) &&
            !s->keys[i].repeat) {
            return true;
        }
    }
    return false;
}

static const char *role_name(collab_role role)
{
    switch (role) {
        case COLLAB_ROLE_HOST: return "Host";
        case COLLAB_ROLE_EDITOR: return "Editor";
        case COLLAB_ROLE_VIEWER: return "Viewer";
        case COLLAB_ROLE_NONE: break;
    }
    return "Offline";
}

static bool collab_busy()
{
    const char *feedback = collab_feedback_text();
    return !strncmp(feedback, "Creating room", 13) ||
           !strncmp(feedback, "Joining room", 12) ||
           !strncmp(feedback, "Connecting to room", 18);
}

static void collab_stack_begin(kit *k, const char *key)
{
    kit_push(k, key, NULL, NULL);
    kit_style(k, "display", "flex");
    kit_style(k, "flex-direction", "column");
    kit_style(k, "gap", "6dp");
    kit_style(k, "width", "100%");
}

static void collab_status_line(kit *k, const char *key, const char *message, bool error)
{
    kit_label(k, key, "collab-status", message);
    kit_style(k, "display", "block");
    kit_style(k, "color", error ? "var(--destructive)" : "var(--muted-foreground)");
    kit_style(k, "white-space", "normal");
    kit_style(k, "word-break", "break-word");
    kit_attr(k, "role", "status");
    kit_attr(k, "aria-live", "polite");
}

static void collab_control_secret(const char *command, char *argument, size_t size)
{
    collab_control(command, argument);
    memset(argument, 0, size);
}

static void collab_join(studio *s)
{
    collab_control_secret(
        "join",
        s->collaboration.invite,
        sizeof s->collaboration.invite
    );
}

static void collab_host(studio *s)
{
    collab_control_secret(
        "host",
        s->collaboration.admin_token,
        sizeof s->collaboration.admin_token
    );
}

static void collab_button_row(kit *k, const char *key)
{
    kit_push(k, key, NULL, "row");
    kit_style(k, "width", "100%");
}

static bool collab_button(
    kit *k,
    const char *key,
    const char *label,
    uint32_t icon,
    kit_variant variant,
    kit_size size,
    unsigned flags
)
{
    kit_button_desc d = { .label = label,
                          .icon = icon,
                          .variant = variant,
                          .size = size,
                          .flags = flags };
    return kit_button(k, key, &d);
}

static void collab_participants(studio *s, bool *close_popup)
{
    kit *k = s->kit;
    int count = acount(s->collaboration.peers);
    kit_label(k, "participants-title", "collab-section-title", count ? "Participants" : "Just you");
    if (!count) {
        return;
    }
    kit_push(k, "participants-list", NULL, "collab-participants");
    kit_style(k, "max-height", "180dp");
    kit_style(k, "overflow-y", "auto");
    for (int i = 0; i < count; i++) {
        studio_collab_peer *peer = &s->collaboration.peers[i];
        char key[32], color[16];
        snprintf(key, sizeof key, "peer-%d", i);
        color_text(color, sizeof color, peer->color);
        kit_push(k, key, NULL, "row collab-peer");
        kit_push(k, "avatar", NULL, "collab-avatar");
        kit_style(k, "width", "var(--studio-height-18, 18dp)");
        kit_style(k, "height", "var(--studio-height-18, 18dp)");
        kit_style(k, "border-radius", "9dp");
        kit_style(k, "background-color", color);
        kit_pop(k);
        kit_label(k, "name", "collab-peer-name", peer->name);
        kit_style(k, "flex", "1 1 0dp");
        kit_style(k, "overflow", "hidden");
        kit_style(k, "text-overflow", "ellipsis");
        kit_style(k, "white-space", "nowrap");
        bool following = s->collaboration.following &&
                         !strcmp(s->collaboration.follow_client, peer->client);
        if (collab_button(k, "follow", following ? "Following" : "Follow", KIT_ICON_EYE, KIT_GHOST, KIT_SM, 0)) {
            if (following) {
                studio_collab_unfollow(s);
            } else {
                studio_collab_follow(s, i);
            }
            *close_popup = true;
        }
        kit_pop(k);
    }
    kit_pop(k);
}

void studio_collab_ui(studio *s)
{
    kit *k = s->kit;
    const bool active = collab_active();
    bool open = kit_popup_is_open(k, "collaboration");
    kit_button_desc trigger = { .label = "Collab",
                                .icon = KIT_ICON_PEOPLE,
                                .variant = KIT_GHOST,
                                .size = KIT_MD,
                                .flags = (active || open) ? KIT_ON : 0,
                                .tip = "Collaboration" };
    if (kit_button(k, "collaboration-trigger", &trigger)) {
        if (open) {
            kit_popup_close(k, "collaboration");
        } else {
            kit_popup_open(k, "collaboration");
        }
    }
    if (!kit_popup_begin(k, "collaboration", "popover collab-popup")) {
        return;
    }
    bool close_popup = false;
    collab_stack_begin(k, "content");
    kit_label(k, "title", "collab-title", "Collaboration");
    kit_style(k, "font-weight", "600");
    char state[320];
    const char *feedback = collab_feedback_text();
    snprintf(state, sizeof state, "%s · %s", role_name(collab_role_current()), collab_status());
    collab_status_line(k, "state", active ? state : collab_status(), false);
    if (feedback && feedback[0] && strcmp(feedback, collab_status())) {
        collab_status_line(k, "feedback", feedback, collab_feedback_error());
    }
    if (!active && !collab_rooms_available()) {
        collab_status_line(k, "unavailable", "Open the web editor to join or create a room.", false);
        kit_pop(k);
        kit_popup_end(k);
        return;
    }
    kit_menu_separator(k, "status-separator");
    if (!active) {
        kit_label(k, "invite-label", "collab-section-title", "Join a room");
        collab_button_row(k, "join-row");
        bool invite_committed = kit_text_field(
            k,
            "invite",
            s->collaboration.invite,
            sizeof s->collaboration.invite,
            "Paste an invite link",
            0
        );
        kit_attr(k, "aria-label", "Invite link");
        kit_style(k, "flex", "1 1 0dp");
        kit_style(k, "min-width", "0");
        unsigned busy = collab_busy() ? KIT_DISABLED : 0;
        bool submitted = !busy && invite_committed && enter_pressed(s) &&
                         s->collaboration.invite[0];
        if (collab_button(k, "join", "Join room", 0, KIT_PRIMARY, KIT_MD, busy)) {
            submitted = s->collaboration.invite[0] != '\0';
        }
        kit_pop(k);
        if (submitted) {
            collab_join(s);
        }
        kit_menu_separator(k, "host-separator");
        kit_label(k, "host-label", "collab-section-title", "Create a room");
        collab_button_row(k, "host-row");
        bool host = collab_button(k, "host", "Create room", 0, KIT_PRIMARY, KIT_MD, busy);
        kit_pop(k);
        if (s->collaboration.admin_token_expanded) {
            kit_label(k, "token-label", "collab-field-label", "Admin token · optional");
            bool token_committed = kit_text_field(
                k,
                "admin-token",
                s->collaboration.admin_token,
                sizeof s->collaboration.admin_token,
                "Enter token if required",
                KIT_SECRET
            );
            kit_attr(k, "aria-label", "Admin token");
            kit_style(k, "width", "100%");
            host = host || (!busy && token_committed && enter_pressed(s));
        }
        if (collab_button(
                k,
                "admin-token-toggle",
                s->collaboration.admin_token_expanded ? "Hide admin token" : "Use admin token",
                0,
                KIT_GHOST,
                KIT_SM,
                0
            )) {
            s->collaboration.admin_token_expanded = !s->collaboration.admin_token_expanded;
        }
        if (host) {
            collab_host(s);
            s->collaboration.admin_token_expanded = false;
        }
    } else {
        collab_participants(s, &close_popup);
        if (collab_is_host()) {
            kit_menu_separator(k, "invite-separator");
            kit_label(k, "invite-title", "collab-section-title", "Invite people");
            collab_button_row(k, "invite-row");
            if (collab_button(k, "editor-invite", "Editor link", KIT_ICON_EDIT, KIT_GHOST, KIT_SM, 0)) {
                collab_control("invite_editor", "");
            }
            if (collab_button(k, "viewer-invite", "Viewer link", KIT_ICON_EYE, KIT_GHOST, KIT_SM, 0)) {
                collab_control("invite_viewer", "");
            }
            kit_pop(k);
        }
        kit_menu_separator(k, "leave-separator");
        collab_button_row(k, "leave-row");
        if (collab_button(k, "leave", "Leave room", KIT_ICON_CLOSE, KIT_DANGER, KIT_MD, 0)) {
            collab_control("leave", "");
            studio_collab_unfollow(s);
        }
        kit_pop(k);
    }
    kit_pop(k);
    kit_popup_end(k);
    if (close_popup) {
        kit_popup_close(k, "collaboration");
    }
}

bool studio_collab_follow(studio *s, int peer_index)
{
    if (peer_index < 0 || peer_index >= acount(s->collaboration.peers)) {
        return false;
    }
    studio_collab_peer *peer = &s->collaboration.peers[peer_index];
    if (!s->collaboration.saved_prefs) {
        s->collaboration.local_prefs = s->prefs;
        s->collaboration.saved_prefs = true;
    }
    snprintf(s->collaboration.follow_client, sizeof s->collaboration.follow_client, "%s", peer->client);
    s->collaboration.following = true;
    return studio_collab_restore(s, peer->presentation);
}

void studio_collab_unfollow(studio *s)
{
    s->collaboration.following = false;
    s->collaboration.follow_client[0] = '\0';
    if (!s->collaboration.saved_prefs) {
        return;
    }
    s->prefs = s->collaboration.local_prefs;
    s->collaboration.saved_prefs = false;
    s->collaboration.restoring_prefs = true;
    s->prefs_changed_at = 0;
    s->maximized = -1;
    memset(s->areas, 0, sizeof s->areas);
}

void studio_collab_free(studio *s)
{
    for (int i = 0; i < acount(s->collaboration.peers); i++) {
        free(s->collaboration.peers[i].client);
        free(s->collaboration.peers[i].name);
        free(s->collaboration.peers[i].presentation);
    }
    afree(s->collaboration.peers);
    memset(s->collaboration.invite, 0, sizeof s->collaboration.invite);
    memset(s->collaboration.admin_token, 0, sizeof s->collaboration.admin_token);
}

void studio_collab_after_frame(studio *s)
{
    if (!collab_active() || s->time - s->collaboration.presence_sent < 0.08) {
        return;
    }
    char *json = studio_collab_capture(s);
    collab_presence(json);
    free(json);
    s->collaboration.presence_sent = s->time;
}

static bool command_is_view(studio_command command)
{
    switch (command) {
        case STUDIO_COMMAND_SELECT_TAB:
        case STUDIO_COMMAND_MAXIMIZE:
        case STUDIO_COMMAND_RESET_LAYOUT:
        case STUDIO_COMMAND_WORKSPACE_VERTICAL_SPLIT:
        case STUDIO_COMMAND_WORKSPACE_HORIZONTAL_SPLIT:
        case STUDIO_COMMAND_TOGGLE_SIDE_PANE:
        case STUDIO_COMMAND_TOGGLE_ASSETS:
        case STUDIO_COMMAND_TOGGLE_OUTPUT:
        case STUDIO_COMMAND_TOGGLE_PERFORMANCE:
        case STUDIO_COMMAND_CLOSE_DRAWER:
        case STUDIO_COMMAND_FRAME:
        case STUDIO_COMMAND_RESET_CAMERA: return true;
        default: return false;
    }
}

static char *command_action(studio *s, studio_command command, int argument)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "family", "command");
    yyjson_mut_obj_add_str(doc, root, "id", studio_command_id(command));
    if (command == STUDIO_COMMAND_SELECT_TAB) {
        if (argument < 0 || argument >= acount(s->scene->tabs)) {
            yyjson_mut_obj_add_null(doc, root, "argument");
        } else {
            yyjson_mut_obj_add_strcpy(
                doc,
                root,
                "argument",
                s->scene->tabs[argument].key
            );
        }
    } else if (command == STUDIO_COMMAND_MAXIMIZE) {
        yyjson_mut_obj_add_int(doc, root, "argument", argument >= 0 ? argument : s->focus_area);
    } else {
        yyjson_mut_obj_add_null(doc, root, "argument");
    }
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}

bool studio_collab_apply(studio *s, const char *json, char *error, size_t size)
{
    yyjson_doc *doc = json ? yyjson_read(json, strlen(json), 0) : NULL;
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    const char *family = yyjson_get_str(yyjson_obj_get(root, "family"));
    bool applied = false;
    if (family && !strcmp(family, "presentation")) {
        applied = studio_collab_restore(s, json);
    } else if (family && !strcmp(family, "command")) {
        const char *id = yyjson_get_str(yyjson_obj_get(root, "id"));
        int command = id ? studio_command_named(id) : -1;
        if (command == STUDIO_COMMAND_UNDO || command == STUDIO_COMMAND_REDO) {
            if (collab_is_host()) {
                studio_command_run(s, (studio_command)command);
                applied = true;
            } else {
                snprintf(error, size, "Undo requires a fresh checkpoint");
            }
        } else if (command >= 0 && command_is_view((studio_command)command)) {
            yyjson_val *argument = yyjson_obj_get(root, "argument");
            int index = -1;
            if (command == STUDIO_COMMAND_SELECT_TAB) {
                const char *key = yyjson_get_str(argument);
                index = key ? tab_by_key(s->scene, key) : -1;
            } else if (yyjson_is_int(argument)) {
                index = (int)yyjson_get_int(argument);
            }
            if (command != STUDIO_COMMAND_SELECT_TAB || index >= 0) {
                studio_command_run_with(s, (studio_command)command, index);
                applied = true;
            }
        }
    } else if (family && !strcmp(family, "text")) {
        snprintf(error, size, "Text collaboration is still initializing");
    }
    yyjson_doc_free(doc);
    if (!applied && error && size && !error[0]) {
        snprintf(error, size, "Unsupported Studio collaboration action");
    }
    return applied;
}
