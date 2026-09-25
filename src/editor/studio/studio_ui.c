/* The editor's authored UI: the Studio project (projects/studio) run as a
 * small world whose GameUi entities are panels. A panel's document mounts in
 * a kit host the way a game mounts it over its canvas, so its widgets,
 * behaviors and data-setting bindings work as they do in games. Studio's
 * preferences are that world's saved keys. */
#include "assets/behavior_loader.h"
#include "assets/scene_document.h"
#include "editor/studio/studio_internal.h"
#include "graph/document/serialize.h"
#include "render/ui.h"
#include "runtime/generated.h"
#include "runtime/scenes/prototype_preview.h"
#include "support/array.h"
#include "support/file_watch.h"
#include "support/text_file.h"
#include "support/xalloc.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    game_prototype_registry registry;
    game_prototype_preview *world;
    game_prototype **prototypes;
    panel *panels;
    file_watch *behaviors; /* the panel behaviors, recompiled when edited */
    rml_listener *listener;
    uint64_t prefs_seen; /* the settings revision Studio's preferences match */
};

static void on_event(void *user, const rml_event *event)
{
    studio_ui *ui = user;
    for (int i = 0; i < acount(ui->panels); i++) {
        if (ui->panels[i].mount && ui_mount_holds(ui->panels[i].mount, event->target)) {
            ui_mount_listen(ui->panels[i].mount, event);
            return;
        }
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

/* Watches each behavior file the world loaded. */
static void watch_behaviors(studio_ui *ui)
{
    char folder[1024];
    snprintf(folder, sizeof folder, "%s/behaviors", ui->content_root);
    DIR *dir = opendir(folder);
    struct dirent *entry;
    while (dir && (entry = readdir(dir))) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        file_watch w = { 0 };
        char path[sizeof w.path];
        snprintf(path, sizeof path, "%s/%s", folder, entry->d_name);
        file_watch_changed(&w, path, 0);
        apush(ui->behaviors, w);
    }
    if (dir) {
        closedir(dir);
    }
}

/* Recompiles edited panel behaviors into the running world, keeping their
 * state; a behavior that fails keeps its last valid version. */
static void reload_behaviors(studio *s)
{
    studio_ui *ui = s->ui;
    for (int i = 0; i < acount(ui->behaviors); i++) {
        file_watch *w = &ui->behaviors[i];
        if (!file_watch_changed(w, w->path, s->time)) {
            continue;
        }
        char *text = text_file_read(w->path);
        behavior_asset *asset = text ? behavior_asset_load_json(text) : NULL;
        free(text);
        char message[256] = "is not a behavior";
        if (!asset ||
            game_prototype_preview_update_behavior(ui->world, asset->name, asset, false, message, sizeof message) <
                0) {
            char error[1024];
            snprintf(error, sizeof error, "%s %s", w->path, message);
            kit_toast(
                s->kit,
                0,
                &(kit_toast_desc){ .type = KIT_TOAST_ERROR,
                                   .title = "Studio UI",
                                   .description = error }
            );
            studio_record(s, KIT_TOAST_ERROR, error);
        }
        behavior_asset_free(asset);
    }
}

studio_ui *studio_ui_new(const char *project_root, char *error, size_t size)
{
    studio_ui *ui = xcalloc(1, sizeof *ui);
    size_t length = strlen(project_root) + sizeof "/content";
    ui->content_root = xmalloc(length);
    snprintf(ui->content_root, length, "%s/content", project_root);
    ui->registry = (game_prototype_registry){ .components = GAME_REGISTRY_COMPONENTS,
                                              .component_count = GAME_REGISTRY_COMPONENT_COUNT };
    ui->world = game_prototype_preview_new(&ui->registry);
    ui->listener = rml_listener_create(on_event, ui);
    app_behavior_fixtures_load(game_prototype_preview_live(ui->world), NULL, ui->content_root);
    if (!launch(ui, error, size)) {
        studio_ui_free(ui);
        return NULL;
    }
    watch_behaviors(ui);
    return ui;
}

/* Frees the world; kit hosts that showed panels must be gone, since they
 * hold the listener. */
void studio_ui_free(studio_ui *ui)
{
    if (!ui) {
        return;
    }
    for (int i = 0; i < acount(ui->panels); i++) {
        free(ui->panels[i].name);
        ui_mount_free(ui->panels[i].mount);
    }
    afree(ui->panels);
    game_prototype_preview_free(ui->world);
    for (int i = 0; i < acount(ui->prototypes); i++) {
        game_prototype_free(ui->prototypes[i]);
    }
    afree(ui->prototypes);
    afree(ui->behaviors);
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
    reload_behaviors(s);
    game_prototype_preview_tick(s->ui->world, s->time);
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

/* Sends the panel's activations to its entity, an input's value first. */
static void send_activations(studio_ui *ui, panel *p, ecs_entity entity)
{
    ui_activation a;
    while (ui_mount_take(p->mount, &a)) {
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
static void report(studio *s, panel *p)
{
    const char *error = ui_mount_error(p->mount);
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

/* The panel's entity and its GameUi, or NULL when it has none. */
static const GameUi *panel_ui(studio_ui *ui, const panel *p, ecs_entity *entity)
{
    ecs_world *world = game_prototype_preview_world(ui->world);
    *entity = game_prototype_preview_entity_at(ui->world, p->index);
    return GAME_GameUi_has(world, *entity) ? GAME_GameUi_get_const(world, *entity)
                                           : NULL;
}

/* Mounts the panel's document in `host` and runs its settings and events. */
static void show_panel(studio *s, panel *p, rml_element *host, ecs_entity entity, const char *document)
{
    studio_ui *ui = s->ui;
    if (p->mount && strcmp(ui_mount_name(p->mount), document) != 0) {
        ui_mount_free(p->mount);
        p->mount = NULL;
    }
    if (!p->mount) {
        p->mount = ui_mount_new(ui->content_root, document);
    }
    if (ui_mount_sync(p->mount, host, ui->listener, s->time)) {
        kit_style_sheet(s->kit, document, ui_mount_style(p->mount));
    }
    ui_mount_apply(p->mount, engine_ui(game_prototype_preview_runtime(ui->world)), ecs_entity_to_u64(entity));
    ui_mount_settings(p->mount, settings(ui));
    send_activations(ui, p, entity);
    report(s, p);
}

void studio_ui_panel(studio *s, const char *key, const char *name)
{
    panel *p = s->ui ? find_panel(s->ui, name) : NULL;
    ecs_entity entity;
    const GameUi *component = p ? panel_ui(s->ui, p, &entity) : NULL;
    if (component && component->document && component->document[0]) {
        show_panel(s, p, kit_host(s->kit, key, "panel-host"), entity, component->document);
    }
}

void studio_ui_modal(studio *s, const char *name)
{
    panel *p = s->ui ? find_panel(s->ui, name) : NULL;
    ecs_entity entity;
    const GameUi *component = p ? panel_ui(s->ui, p, &entity) : NULL;
    if (component && !component->hidden && component->document &&
        component->document[0]) {
        show_panel(s, p, kit_modal_host(s->kit, name), entity, component->document);
    }
}

void studio_ui_show(studio *s, const char *name)
{
    panel *p = s->ui ? find_panel(s->ui, name) : NULL;
    ecs_entity entity;
    if (p && panel_ui(s->ui, p, &entity)) {
        GAME_GameUi_get(game_prototype_preview_world(s->ui->world), entity)->hidden = false;
    }
}
