/* The editor's authored UI: the Studio project (STUDIO_PROJECT) run as a
 * small world whose GameUi entities are panels. A panel's document mounts in
 * a kit host the way a game mounts it over its canvas, so its widgets,
 * behaviors and data bindings work as they do in games. Studio's preferences
 * are that world's saved keys. */
#include "assets/behavior_loader.h"
#include "assets/project.h"
#include "assets/scene_document.h"
#include "editor/collab/collab.h"
#include "editor/studio/studio_internal.h"
#include "graph/document/serialize.h"
#include "render/ui.h"
#include "runtime/generated.h"
#include "runtime/scenes/prototype_preview.h"
#include "support/array.h"
#include "support/text_file.h"
#include "support/tree_watch.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

/* A scene entity with a GameUi document. */
typedef struct panel
{
    char *name;
    int index; /* in the launched scene */
    ui_mount *mount;
    char error[256]; /* the last error reported */
} panel;

struct studio_ui
{
    char *content_root;
    char *default_page; /* the Studio project's default root document, or NULL */
    char *page;         /* the Studio scene's root document, or NULL */
    game_prototype_registry registry;
    game_prototype_preview *world;
    game_prototype **prototypes;
    panel *panels;
    tree_watch files; /* the project's assets, reloaded when edited */
    rml_listener *listener;
    ui_data data; /* what the kit's hosted panels bind */
    studio_sources sources;
    ui_mount *areas[STUDIO_AREA_COUNT]; /* each area's panel, when it shows one */
    ui_mount *headers[STUDIO_AREA_COUNT]; /* each area's header */
    uint64_t prefs_seen; /* the settings revision Studio's preferences match */
};

/* The panel or area mount holding `element`, or NULL. */
static ui_mount *mount_holding(studio_ui *ui, rml_element *element)
{
    for (int i = 0; i < acount(ui->panels); i++) {
        if (ui->panels[i].mount && ui_mount_holds(ui->panels[i].mount, element)) {
            return ui->panels[i].mount;
        }
    }
    for (int i = 0; i < STUDIO_AREA_COUNT; i++) {
        if (ui->areas[i] && ui_mount_holds(ui->areas[i], element)) {
            return ui->areas[i];
        }
        if (ui->headers[i] && ui_mount_holds(ui->headers[i], element)) {
            return ui->headers[i];
        }
    }
    return NULL;
}

static void on_event(void *user, const rml_event *event)
{
    ui_mount *m = mount_holding(user, event->target);
    if (m) {
        ui_mount_listen(m, event);
    }
}

static void on_emit(void *user, const rml_event *event, const char *name, const char *value)
{
    ui_mount *m = mount_holding(user, event->current);
    if (m) {
        ui_mount_emit(m, event->current, name, value);
    }
}

static game_prototype *load_prototype(const studio_ui *ui, const char *name, char *error, size_t size)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/prototypes/%s.yaml", ui->content_root, name);
    char *text = text_file_read(path);
    game_prototype *prototype = text
                                  ? game_prototype_load(text, &ui->registry, error, size)
                                  : NULL;
    if (!text) {
        snprintf(error, size, "prototypes/%s.yaml is missing", name);
    }
    free(text);
    return prototype;
}

/* Launches the Studio scene; each entity becomes a panel by name. */
static bool launch(studio_ui *ui, char *error, size_t size)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/scenes/Studio.yaml", ui->content_root);
    char *text = text_file_read(path);
    snprintf(error, size, "scenes/Studio.yaml is missing");
    game_scene_document *scene = text ? game_scene_document_load(text, error, size)
                                      : NULL;
    free(text);
    if (!scene) {
        return false;
    }
    free(ui->page);
    const char *page = scene->ui ? scene->ui : ui->default_page;
    ui->page = page ? xstrdup(page) : NULL;
    game_prototype_preview_scene_entity *entities = NULL;
    bool ok = true;
    for (int i = 0; ok && i < acount(scene->entities); i++) {
        game_scene_entity *e = &scene->entities[i];
        game_prototype *prototype = load_prototype(ui, e->prototype, error, size);
        ok = prototype != NULL;
        if (!ok) {
            break;
        }
        apush(ui->prototypes, prototype);
        game_prototype_preview_register_prototype(ui->world, prototype);
        apush(
            entities,
            ((game_prototype_preview_scene_entity){
                .prototype = prototype,
                .components_json = e->components_json,
                .behaviors = (const char *const *)e->behaviors,
                .behavior_count = acount(e->behaviors) })
        );
        apush(ui->panels, ((panel){ .name = xstrdup(e->name), .index = i }));
    }
    ok = ok &&
         game_prototype_preview_launch_scene(ui->world, entities, acount(entities), error, size) ==
             0;
    afree(entities);
    game_scene_document_free(scene);
    return ok;
}

static void report_error(studio *s, const char *path, const char *message)
{
    char error[1024];
    snprintf(error, sizeof error, "%s %s", path, message);
    kit_toast(s->kit, 0, &(kit_toast_desc){ .type = KIT_TOAST_ERROR, .title = "Studio UI", .description = error });
    studio_record(s, KIT_TOAST_ERROR, error);
}

/* Recompiles an edited panel behavior into the running world, keeping its
 * state; a behavior that fails keeps its last valid version. */
static void reload_behavior(studio *s, const char *path)
{
    char *text = text_file_read(path);
    if (!text) {
        return;
    }
    behavior_asset *asset = behavior_asset_load_json(text);
    free(text);
    char message[256] = "is not a behavior";
    if (!asset ||
        game_prototype_preview_update_behavior(s->ui->world, asset->name, asset, false, message, sizeof message) <
            0) {
        report_error(s, path, message);
    }
    behavior_asset_free(asset);
}

static void free_world(studio_ui *ui)
{
    for (int i = 0; i < acount(ui->panels); i++) {
        free(ui->panels[i].name);
        ui_mount_free(ui->panels[i].mount);
    }
    for (int i = 0; i < STUDIO_AREA_COUNT; i++) {
        ui_mount_free(ui->areas[i]);
        ui_mount_free(ui->headers[i]);
        ui->areas[i] = ui->headers[i] = NULL;
    }
    afree(ui->panels);
    game_prototype_preview_free(ui->world);
    for (int i = 0; i < acount(ui->prototypes); i++) {
        game_prototype_free(ui->prototypes[i]);
    }
    afree(ui->prototypes);
    ui->panels = NULL;
    ui->prototypes = NULL;
}

/* A new world for the Studio scene; panels mount again in their hosts. */
static bool open_world(studio_ui *ui, char *error, size_t size)
{
    ui->world = game_prototype_preview_new(&ui->registry);
    app_behavior_fixtures_load(game_prototype_preview_live(ui->world), NULL, ui->content_root);
    return launch(ui, error, size);
}

static bool in_folder(const studio_ui *ui, const char *path, const char *folder)
{
    size_t root = strlen(ui->content_root), length = strlen(folder);
    return !strncmp(path + root, folder, length) && path[root + length] == '/';
}

/* Behaviors reload in place; a scene or prototype edit relaunches the world.
 * UI documents follow their files as they mount. */
static void follow_disk(studio *s)
{
    studio_ui *ui = s->ui;
    char **changed = tree_watch_poll(&ui->files, ui->content_root, ".yaml", s->time);
    bool relaunch = false;
    for (int i = 0; i < acount(changed); i++) {
        if (in_folder(ui, changed[i], "/behaviors")) {
            reload_behavior(s, changed[i]);
        }
        relaunch |= in_folder(ui, changed[i], "/scenes") ||
                    in_folder(ui, changed[i], "/prototypes");
    }
    tree_watch_changes_free(changed);
    if (!relaunch) {
        return;
    }
    free_world(ui);
    char error[256];
    if (!open_world(ui, error, sizeof error)) {
        report_error(s, "scenes/Studio.yaml", error);
    }
}

studio_ui *studio_ui_new(rml_context *context, const char *project_root, char *error, size_t size)
{
    studio_ui *ui = xcalloc(1, sizeof *ui);
    chibi_project project = { 0 };
    char manifest_error[256];
    if (chibi_project_open(&project, project_root, manifest_error, sizeof manifest_error) == 0 &&
        project.ui) {
        ui->default_page = xstrdup(project.ui);
    }
    chibi_project_free(&project);
    ui_data_init(&ui->data, context, studio_sources_new(), on_emit, ui);
    size_t length = strlen(project_root) + sizeof "/content";
    ui->content_root = xmalloc(length);
    snprintf(ui->content_root, length, "%s/content", project_root);
    ui->registry = (game_prototype_registry){ .components = GAME_REGISTRY_COMPONENTS,
                                              .component_count = GAME_REGISTRY_COMPONENT_COUNT };
    ui->listener = rml_listener_create(on_event, ui);
    if (!open_world(ui, error, size)) {
        studio_ui_free(ui);
        return NULL;
    }
    tree_watch_poll(&ui->files, ui->content_root, ".yaml", 0);
    return ui;
}

/* Frees the world; kit hosts that showed panels must be gone, since they
 * hold the listener. */
void studio_ui_free(studio_ui *ui)
{
    if (!ui) {
        return;
    }
    free_world(ui);
    free(ui->page);
    free(ui->default_page);
    studio_sources_free(&ui->sources);
    ui_data_free(&ui->data);
    tree_watch_free(&ui->files);
    rml_listener_destroy(ui->listener);
    free(ui->content_root);
    free(ui);
}

static game_save *settings(studio_ui *ui)
{
    return game_behavior_save(game_prototype_preview_behaviors(ui->world));
}

void studio_ui_frame(studio *s)
{
    if (!s->ui) {
        return;
    }
    follow_disk(s);
    game_prototype_preview_tick(s->ui->world, s->time);
    ui_data_sync(&s->ui->data, settings(s->ui));
    ui_data_show_entities(
        &s->ui->data,
        engine_ui(game_prototype_preview_runtime(s->ui->world))
    );
    studio_sources_sync(s, &s->ui->sources, &s->ui->data);
    studio_prefs_share(&s->prefs, settings(s->ui), &s->ui->prefs_seen);
}

static panel *find_panel(studio_ui *ui, const char *name)
{
    for (int i = 0; i < acount(ui->panels); i++) {
        if (!strcmp(ui->panels[i].name, name)) {
            return &ui->panels[i];
        }
    }
    return NULL;
}

/* Sends a panel's events to its entity's behaviors, an input's value first;
 * emit('studio.<id>') runs Studio's command `id` instead, and
 * emit('studio.<id>', index) runs it on the tab or area `index` names. */
static void sync_new_project(studio *s, bool read_input);

static void send_activations(studio *s, ui_mount *mount, ecs_entity entity)
{
    studio_ui *ui = s->ui;
    ui_activation a;
    while (ui_mount_take(mount, &a)) {
        if (!strcmp(ui_mount_name(mount), "NewProject") &&
            (!strncmp(a.event, "newProject.", 11) || !strcmp(a.event, "Close"))) {
            sync_new_project(s, true);
            bool confirmation = s->new_project.confirm_discard;
            studio_new_project_action(s, !strcmp(a.event, "Close") ? "cancel" : a.event + 11);
            sync_new_project(s, false);
            if (!confirmation && s->new_project.confirm_discard) {
                game_entities_request_event(
                    game_prototype_preview_entities(ui->world),
                    entity,
                    "ConfirmDiscard",
                    (flow_inputs){ 0 }
                );
            } else if (s->new_project.error[0]) {
                game_entities_request_event(
                    game_prototype_preview_entities(ui->world),
                    entity,
                    "InvalidName",
                    (flow_inputs){ 0 }
                );
            }
            continue;
        }
        int command = strncmp(a.event, "studio.", 7)
                        ? -1
                        : studio_command_named(a.event + 7);
        if (command >= 0) {
            studio_command_run_with(
                s,
                (studio_command)command,
                a.has_value ? (int)a.value : -1
            );
            continue;
        }
        if (a.has_value && a.id[0]) {
            char selector[160];
            snprintf(selector, sizeof selector, "#%s", a.id);
            game_ui_set_value(
                engine_ui(game_prototype_preview_runtime(ui->world)),
                ecs_entity_to_u64(entity),
                selector,
                a.value
            );
        }
        game_entities_request_event(
            game_prototype_preview_entities(ui->world),
            entity,
            a.event,
            (flow_inputs){ 0 }
        );
    }
}

/* Reports a panel's asset error once when it appears. */
static void report(studio *s, panel *p, ui_mount *mount)
{
    const char *error = ui_mount_error(mount);
    if (!strcmp(error, p->error)) {
        return;
    }
    snprintf(p->error, sizeof p->error, "%s", error);
    if (error[0]) {
        kit_toast(
            s->kit,
            0,
            &(kit_toast_desc){ .type = KIT_TOAST_ERROR, .title = p->name, .description = error }
        );
        studio_record(s, KIT_TOAST_ERROR, error);
    }
}

/* The panel's GameUi, or NULL when it has none. */
static const GameUi *panel_ui(studio_ui *ui, const panel *p)
{
    ecs_world *world = game_prototype_preview_world(ui->world);
    ecs_entity entity = game_prototype_preview_entity_at(ui->world, p->index);
    return GAME_GameUi_has(world, entity) ? GAME_GameUi_get_const(world, entity) : NULL;
}

bool studio_ui_shortcut_focus(studio *s)
{
    if (!s || !s->ui || !s->backend || !s->kit || s->new_project.open ||
        kit_has_popup(s->kit) || kit_command_is_open(s->kit) ||
        kit_accepts_text(s->kit)) {
        return false;
    }
    studio_ui *ui = s->ui;
    const char *modal_names[] = { "Settings", "Collaboration" };
    for (size_t i = 0; i < sizeof modal_names / sizeof *modal_names; i++) {
        panel *p = find_panel(ui, modal_names[i]);
        const GameUi *component = p ? panel_ui(ui, p) : NULL;
        if (component && !component->hidden) {
            return false;
        }
    }

    rml_context *context = rml_backend_context(s->backend);
    rml_element *focus = context ? rml_context_focus_element(context) : NULL;
    rml_element *button = focus;
    while (button && strcmp(rml_element_tag_name(button), "button") &&
           !rml_element_is_class_set(button, "btn")) {
        button = rml_element_parent(button);
    }
    if (!button) {
        return false;
    }
    ui_mount *mount = mount_holding(ui, focus);
    if (!mount) {
        return false;
    }
    const char *name = ui_mount_name(mount);
    return !strcmp(name, "TopBar") || !strcmp(name, "AreaHeader") ||
           !strcmp(name, "StatusBar");
}

/* Mounts the panel's document in `host` and runs its events. */
static void show_panel(studio *s, panel *p, ui_mount **mount, rml_element *host, const char *document)
{
    studio_ui *ui = s->ui;
    ecs_entity entity = game_prototype_preview_entity_at(ui->world, p->index);
    if (*mount && strcmp(ui_mount_name(*mount), document) != 0) {
        ui_mount_free(*mount);
        *mount = NULL;
    }
    if (!*mount) {
        *mount = ui_mount_new(ui->content_root, document);
    }
    ui_data_bind_self(&ui->data, host, ecs_entity_to_u64(entity));
    if (ui_mount_sync(*mount, host, ui->listener, s->time)) {
        kit_style_sheet(s->kit, document, ui_mount_style(*mount));
    }
    game_ui *model = engine_ui(game_prototype_preview_runtime(ui->world));
    ui_mount_measure(*mount, model, ecs_entity_to_u64(entity));
    ui_mount_apply(*mount, model, ecs_entity_to_u64(entity));
    send_activations(s, *mount, entity);
    report(s, p, *mount);
}

/* The document panel `name` shows, with the panel in `out`; NULL when it
 * has none. */
static const char *panel_document(studio *s, const char *name, panel **out)
{
    panel *p = s->ui ? find_panel(s->ui, name) : NULL;
    const GameUi *component = p ? panel_ui(s->ui, p) : NULL;
    *out = p;
    return component && component->document && component->document[0]
             ? component->document
             : NULL;
}

rml_element *studio_ui_panel(studio *s, const char *key, const char *name)
{
    panel *p;
    const char *document = panel_document(s, name, &p);
    if (!document) {
        return NULL;
    }
    rml_element *host = kit_host(s->kit, key, "panel-host");
    show_panel(s, p, &p->mount, host, document);
    return host;
}

/* The alias takes when the mount binds the host to the data model, as a
 * widget's data-alias-* attributes do. */
rml_element *studio_ui_area_header(studio *s, int area, const char *name)
{
    panel *p;
    const char *document = panel_document(s, name, &p);
    if (!document) {
        return NULL;
    }
    rml_element *host = kit_host(s->kit, "head", "panel-host");
    char alias[32];
    snprintf(alias, sizeof alias, "editor.areas[%d]", area);
    kit_attr(s->kit, "data-alias-area", alias);
    show_panel(s, p, &s->ui->headers[area], host, document);
    return host;
}

void studio_ui_area_detach(studio *s, int area)
{
    if (!s->ui) {
        return;
    }
    ui_mount_detach(s->ui->headers[area]);
    ui_mount_detach(s->ui->areas[area]);
}

const char *studio_ui_page(const studio_ui *ui)
{
    return ui->page ? ui->page : "";
}

int studio_ui_panel_count(const studio *s)
{
    return s->ui ? acount(s->ui->panels) : 0;
}

const char *studio_ui_panel_name(const studio *s, int index)
{
    return s->ui->panels[index].name;
}

void studio_ui_area(studio *s, int area, const char *name)
{
    panel *p;
    const char *document = panel_document(s, name, &p);
    if (document) {
        show_panel(s, p, &s->ui->areas[area], kit_host(s->kit, "panel", "area-panel"), document);
    }
}

static void show_modal(studio *s, const char *name)
{
    panel *p = s->ui ? find_panel(s->ui, name) : NULL;
    const GameUi *component = p ? panel_ui(s->ui, p) : NULL;
    if (component && !component->hidden && component->document &&
        component->document[0]) {
        show_panel(s, p, &p->mount, kit_modal_host(s->kit, name), component->document);
        return;
    }
    if (p) {
        ui_mount_detach(p->mount);
    }
    if (!strcmp(name, "Settings")) {
        s->shortcut_recording = 0;
    }
}

void studio_ui_modal(studio *s, const char *name)
{
    if (!s->ui) {
        return;
    }
    show_modal(s, name);
    /* The top bar's modal pass also hosts the on-demand Collaboration panel. */
    if (!strcmp(name, "Settings")) {
        show_modal(s, "Collaboration");
    }
}

void studio_ui_show(studio *s, const char *name)
{
    panel *p = s->ui ? find_panel(s->ui, name) : NULL;
    if (p && panel_ui(s->ui, p)) {
        ecs_entity entity = game_prototype_preview_entity_at(s->ui->world, p->index);
        GAME_GameUi_get(game_prototype_preview_world(s->ui->world), entity)->hidden = false;
    }
}

/* Project operations own their state; the authored panel owns its controls. */
static void sync_new_project(studio *s, bool read_input)
{
    ui_data *data = &s->ui->data;
    yyjson_mut_val *root = yyjson_mut_doc_get_root(data->doc);
    yyjson_mut_val *page = yyjson_mut_obj_get(root, "new_project");
    yyjson_mut_val *name = yyjson_mut_obj_get(page, "name");
    const char *text = yyjson_mut_get_str(name);
    studio_new_project *dialog = &s->new_project;
    if (read_input && !dialog->focus && !dialog->waiting &&
        !dialog->confirm_discard && strcmp(dialog->name, text)) {
        snprintf(dialog->name, sizeof dialog->name, "%s", text);
        dialog->error[0] = '\0';
        dialog->created_root[0] = '\0';
    }
    yyjson_mut_val *error = yyjson_mut_obj_get(page, "error");
    yyjson_mut_val *waiting = yyjson_mut_obj_get(page, "waiting");
    yyjson_mut_val *confirmation = yyjson_mut_obj_get(page, "confirm_discard");
    bool changed = strcmp(text, dialog->name) ||
                   strcmp(yyjson_mut_get_str(error), dialog->error) ||
                   yyjson_mut_get_bool(waiting) != dialog->waiting ||
                   yyjson_mut_get_bool(confirmation) != dialog->confirm_discard;
    if (changed) {
        ui_data_set_text(data->doc, name, dialog->name);
        ui_data_set_text(data->doc, error, dialog->error);
        /* Attribute bindings survive a modal host being reordered. Keep the
           error markup escaped so names and filesystem paths stay text. */
        char markup[sizeof dialog->error * 6 + 1];
        size_t length = 0;
        for (const char *p = dialog->error; *p; p++) {
            const char *escaped = *p == '&' ? "&amp;"
                                : *p == '<' ? "&lt;"
                                : *p == '>' ? "&gt;"
                                            : NULL;
            if (escaped) {
                length += (size_t)snprintf(markup + length, sizeof markup - length, "%s", escaped);
            } else {
                markup[length++] = *p;
            }
        }
        markup[length] = '\0';
        ui_data_set_text(data->doc, yyjson_mut_obj_get(page, "error_markup"), markup);
        yyjson_mut_set_bool(waiting, dialog->waiting);
        yyjson_mut_set_bool(confirmation, dialog->confirm_discard);
        rml_data_model_dirty(data->model, "new_project");
    }
}

void studio_ui_new_project(studio *s)
{
    panel *p;
    const char *document = panel_document(s, "NewProject", &p);
    if (!document) {
        return;
    }
    if (!s->new_project.open) {
        ui_mount_detach(p->mount);
        return;
    }
    sync_new_project(s, true);
    show_panel(s, p, &p->mount, kit_modal_host(s->kit, "new-project"), document);
    s->new_project.focus = false;
}

void studio_ui_collaboration_action(studio *s, studio_command command)
{
    if (!s->ui) {
        return;
    }
    if ((command == STUDIO_COMMAND_COLLABORATION_HOST ||
         command == STUDIO_COMMAND_COLLABORATION_JOIN) &&
        studio_collab_busy(s)) {
        return;
    }
    const char *action = NULL;
    const char *field = NULL;
    const char *progress = NULL;
    switch (command) {
        case STUDIO_COMMAND_COLLABORATION_HOST:
            action = "host";
            field = "token";
            progress = "Creating room...";
            break;
        case STUDIO_COMMAND_COLLABORATION_JOIN:
            action = "join";
            field = "invite";
            progress = "Joining room...";
            break;
        case STUDIO_COMMAND_COLLABORATION_LEAVE:
            action = "leave";
            progress = "Leaving room...";
            break;
        case STUDIO_COMMAND_COLLABORATION_INVITE_VIEWER:
            action = "invite_viewer";
            progress = "Creating viewer invite...";
            break;
        case STUDIO_COMMAND_COLLABORATION_INVITE_EDITOR:
            action = "invite_editor";
            progress = "Creating editor invite...";
            break;
        default: return;
    }

    ui_data *data = &s->ui->data;
    yyjson_mut_val *root = yyjson_mut_doc_get_root(data->doc);
    yyjson_mut_val *collaboration = yyjson_mut_obj_get(root, "collaboration");
    yyjson_mut_val *value = field ? yyjson_mut_obj_get(collaboration, field) : NULL;
    const char *argument = value && yyjson_mut_is_str(value)
                             ? yyjson_mut_get_str(value)
                             : "";
    char invite[4096];
    if (command == STUDIO_COMMAND_COLLABORATION_JOIN) {
        while (*argument == ' ' || *argument == '\t' || *argument == '\r' ||
               *argument == '\n') {
            argument++;
        }
        size_t length = strlen(argument);
        while (length &&
               (argument[length - 1] == ' ' || argument[length - 1] == '\t' ||
                argument[length - 1] == '\r' || argument[length - 1] == '\n')) {
            length--;
        }
        if (length >= sizeof invite) {
            length = sizeof invite - 1;
        }
        memcpy(invite, argument, length);
        invite[length] = '\0';
        argument = invite;
    }
    if (command == STUDIO_COMMAND_COLLABORATION_HOST ||
        command == STUDIO_COMMAND_COLLABORATION_JOIN) {
        s->collaboration.busy = true;
    }
    if (command == STUDIO_COMMAND_COLLABORATION_HOST) {
        s->collaboration.admin_token_expanded = false;
    }
    if (command == STUDIO_COMMAND_COLLABORATION_LEAVE) {
        studio_collab_unfollow(s);
    }
    studio_collab_web_status(progress, 0);
    studio_collab_control(action, argument);
    if (value) {
        ui_data_set_text(data->doc, value, "");
        rml_data_model_dirty(data->model, "collaboration");
    }
}

void studio_ui_show_settings(studio *s, const char *query)
{
    if (!s->ui) {
        return;
    }
    studio_sources_search_settings(&s->ui->data, query);
    studio_ui_show(s, "Settings");
}
