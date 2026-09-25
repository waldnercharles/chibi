/* The editor's authored UI: the Studio project (projects/studio) run as a
 * small world whose GameUi entities are panels. A panel's document mounts in
 * a kit host the way a game mounts it over its canvas, so its widgets,
 * behaviors and data-setting bindings work as they do in games. Studio's
 * preferences are that world's saved keys. */
#include "assets/behavior_loader.h"
#include "assets/scene_document.h"
#include "editor/studio/studio_internal.h"
#include "render/ui.h"
#include "runtime/generated.h"
#include "runtime/scenes/prototype_preview.h"
#include "support/array.h"
#include "support/text_file.h"
#include "support/xalloc.h"

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
    app_behavior_fixtures_load(game_prototype_preview_live(ui->world), ui->world, ui->content_root);
    if (!launch(ui, error, size)) {
        studio_ui_free(ui);
        return NULL;
    }
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

void studio_ui_panel(studio *s, const char *key, const char *name)
{
    studio_ui *ui = s->ui;
    panel *p = ui ? find_panel(ui, name) : NULL;
    if (!p) {
        return;
    }
    ecs_world *world = game_prototype_preview_world(ui->world);
    ecs_entity entity = game_prototype_preview_entity_at(ui->world, p->index);
    const GameUi *component = GAME_GameUi_has(world, entity)
                                ? GAME_GameUi_get_const(world, entity)
                                : NULL;
    const char *document = component && component->document ? component->document : "";
    if (p->mount && strcmp(ui_mount_name(p->mount), document) != 0) {
        ui_mount_free(p->mount);
        p->mount = NULL;
    }
    if (!document[0]) {
        return;
    }
    if (!p->mount) {
        p->mount = ui_mount_new(ui->content_root, document);
    }
    rml_element *host = kit_host(s->kit, key, "panel-host");
    if (ui_mount_sync(p->mount, host, ui->listener, s->time)) {
        kit_style_sheet(s->kit, document, ui_mount_style(p->mount));
    }
    ui_mount_apply(p->mount, engine_ui(game_prototype_preview_runtime(ui->world)), ecs_entity_to_u64(entity));
    ui_mount_settings(p->mount, settings(ui));
    send_activations(ui, p, entity);
    report(s, p);
}
