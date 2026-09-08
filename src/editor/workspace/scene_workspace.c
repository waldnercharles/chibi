#include "editor/ui/ui_shell.h"
#include "editor/workspace/scene_internal.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static scene_location current_location(app_scene_authoring *scene)
{
    scene_location location = {
        .kind = scene->document.kind,
        .document = scene->document.id,
        .scene_path = scene->scene_path,
        .system_path = scene->document.kind == APP_EDITOR_DOCUMENT_SYSTEM
                         ? scene->system_path
                         : NULL,
        .prototype = selected_prototype(scene)
                       ? selected_prototype(scene)->prototype->name
                       : NULL,
        .content = scene->content_selected_path,
        .tab = editor_ui_navigator_tab(
            scene->navigator_scene ? EDITOR_UI_PANE_WORLD_OUTLINER : EDITOR_UI_PANE_MY_BEHAVIOR_ASSET
        ),
        .camera = scene->camera.view,
    };
    if (scene->document.kind == APP_EDITOR_DOCUMENT_SCENE ||
        scene->document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR) {
        location.entity = selected_scene_entity(scene);
        game_behavior_attachment_info attachment;
        if (location.entity.slot && scene->selected_attachment >= 0 &&
            game_behavior_attachment_info_at(
                scene->live->behaviors,
                location.entity,
                scene->selected_attachment,
                &attachment
            ) == 0) {
            location.attachment = attachment.asset_name;
        }
    }
    if (scene->document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR ||
        scene->document.kind == APP_EDITOR_DOCUMENT_SYSTEM) {
        location.graph = editor_capture_location(scene->graph_editor);
    }
    return location;
}

void app_scene_authoring_navigation_sync(app_scene_authoring *scene)
{
    if (scene->navigation_replaying) {
        return;
    }
    scene_location location = current_location(scene);
    scene_navigation_record(scene->navigation, &location);
    free(location.graph.nodes);
    editor_ui_set_navigation_available(
        scene_navigation_can_move(scene->navigation, false),
        scene_navigation_can_move(scene->navigation, true)
    );
}

bool app_scene_authoring_can_navigate(const app_scene_authoring *scene, bool forward)
{
    return scene_navigation_can_move(scene->navigation, forward);
}

static bool same_scene_path(const char *a, const char *b)
{
    return a && b ? strcmp(a, b) == 0 : a == b;
}

static int location_attachment(app_scene_authoring *scene, const scene_location *location)
{
    if (!location->attachment) {
        return -1;
    }
    int count = game_behavior_attachment_count_for_entity(
        scene->live->behaviors,
        location->entity
    );
    for (int i = 0; i < count; i++) {
        game_behavior_attachment_info attachment;
        if (game_behavior_attachment_info_at(scene->live->behaviors, location->entity, i, &attachment) ==
                0 &&
            strcmp(attachment.asset_name, location->attachment) == 0) {
            return i;
        }
    }
    return -1;
}

static bool location_available(app_scene_authoring *scene, const scene_location *location)
{
    if (location->prototype && prototype_index(scene, location->prototype) < 0) {
        return false;
    }
    if (location->entity.slot && !ecs_entity_is_alive(scene->world, location->entity)) {
        return false;
    }
    if (location->attachment && location_attachment(scene, location) < 0) {
        return false;
    }
    if (location->tab == EDITOR_UI_PANE_CONTENT && location->content) {
        struct stat info;
        if (stat(location->content, &info) != 0) {
            return false;
        }
    }
    switch (location->kind) {
        case APP_EDITOR_DOCUMENT_SCENE:
            return same_scene_path(location->scene_path, scene->scene_path);
        case APP_EDITOR_DOCUMENT_PROTOTYPE:
            return prototype_index(scene, location->document) >= 0;
        case APP_EDITOR_DOCUMENT_BEHAVIOR: {
            game_live_definition
                *definition = game_live_find_definition(scene->live, location->document);
            return definition && editor_can_visit_location(
                                     scene->graph_editor,
                                     definition->working,
                                     location->document,
                                     &location->graph
                                 );
        }
        case APP_EDITOR_DOCUMENT_SYSTEM: {
            struct stat info;
            const behavior_asset
                *source = scene->system_document &&
                                  same_scene_path(scene->system_path, location->system_path)
                            ? game_system_document_behavior_asset(scene->system_document)
                            : NULL;
            return location->system_path && stat(location->system_path, &info) == 0 &&
                   editor_can_visit_location(
                       scene->graph_editor,
                       source,
                       location->system_path,
                       &location->graph
                   );
        }
    }
    return false;
}

static int restore_location(app_scene_authoring *scene, const scene_location *location)
{
    if (!location_available(scene, location)) {
        return -1;
    }
    switch (location->kind) {
        case APP_EDITOR_DOCUMENT_SCENE:
            scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
            break;
        case APP_EDITOR_DOCUMENT_PROTOTYPE:
            open_prototype(scene, prototype_index(scene, location->document));
            break;
        case APP_EDITOR_DOCUMENT_BEHAVIOR:
            if (open_behavior_asset(scene, location->document) != 0) {
                return -1;
            }
            break;
        case APP_EDITOR_DOCUMENT_SYSTEM:
            if (open_system_asset(scene, location->system_path) != 0) {
                return -1;
            }
            break;
    }
    if (location->graph.graph_id &&
        editor_restore_location(scene->graph_editor, &location->graph) != 0) {
        return -1;
    }
    scene->selected_prototype = location->prototype
                                  ? prototype_index(scene, location->prototype)
                                  : -1;
    if (location->kind == APP_EDITOR_DOCUMENT_SCENE ||
        location->kind == APP_EDITOR_DOCUMENT_BEHAVIOR) {
        scene->selected = -1;
        scene->runtime_selected = (ecs_entity){ 0 };
        for (int i = 0; i < acount(scene->actors); i++) {
            scene_actor *actor = &scene->actors[i];
            actor->marked = ecs_entity_to_u64(actor->entity) ==
                            ecs_entity_to_u64(location->entity);
            if (actor->marked) {
                scene->selected = i;
            }
        }
        if (location->entity.slot && scene->selected < 0) {
            scene->runtime_selected = location->entity;
        }
        scene->selected_attachment = location_attachment(scene, location);
    }
    free(scene->content_selected_path);
    scene->content_selected_path = location->content ? xstrdup(location->content)
                                                     : NULL;
    scene->navigator_scene = location->tab == EDITOR_UI_PANE_WORLD_OUTLINER;
    editor_ui_restore_navigator_tab(location->tab);
    if (location->camera.width > 0 && location->camera.height > 0) {
        scene->camera.view = location->camera;
        scene->camera.saved = true;
        if (scene->active_runtime_target == APP_RUNTIME_SCENE && scene->render_viewport) {
            engine_viewport_set_view(scene->render_viewport, &location->camera);
        }
    }
    return 0;
}

int app_scene_authoring_navigate(app_scene_authoring *scene, bool forward)
{
    app_scene_authoring_navigation_sync(scene);
    scene->navigation_replaying = true;
    int skipped = 0;
    int result = -1;
    const scene_location *location;
    while ((location = scene_navigation_step(scene->navigation, forward))) {
        if (restore_location(scene, location) == 0) {
            result = 0;
            break;
        }
        scene_navigation_discard_current(scene->navigation, forward);
        skipped++;
    }
    scene->navigation_replaying = false;
    scene->navigation_error[0] = 0;
    if (skipped) {
        snprintf(
            scene->navigation_error,
            sizeof scene->navigation_error,
            "Skipped %d unavailable location%s: an item was deleted or its "
            "Scene was closed.",
            skipped,
            skipped == 1 ? "" : "s"
        );
        snprintf(scene->message, sizeof scene->message, "%s", scene->navigation_error);
    }
    editor_ui_set_navigation_available(
        scene_navigation_can_move(scene->navigation, false),
        scene_navigation_can_move(scene->navigation, true)
    );
    return result;
}
