/* Where an applied command left its document's history, so its Undo is offered
 * only while the change is still that document's latest edit. */
#include "editor/commands/internal.h"
#include "editor/workspace/editor_internal.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>

static const char *scene_key(const app_scene_authoring *scene)
{
    return scene->current_scene_tab_key ? scene->current_scene_tab_key
                                        : APP_SCENE_DOCUMENT_ID;
}

/* The undo stack of the edited prototype or graph, or NULL. */
static char **undo_stack(app_scene_authoring *scene, const command_mark *mark)
{
    if (mark->edited == COMMAND_EDITED_GRAPH) {
        return scene->graph_editor->undo;
    }
    scene_prototype_asset *asset = find_prototype(scene, mark->name);
    return asset ? asset->undo : NULL;
}

/* The prototype or graph document as saved, hashed. */
static void document_hash(app_scene_authoring *scene, const command_mark *mark, char out[17])
{
    scene_prototype_asset *asset = mark->edited == COMMAND_EDITED_PROTOTYPE
                                     ? find_prototype(scene, mark->name)
                                     : NULL;
    char *text = mark->edited == COMMAND_EDITED_GRAPH
                   ? ed_snapshot(scene->graph_editor)
               : asset ? game_prototype_save(asset->prototype)
                       : xstrdup("");
    command_hash(text, out);
    free(text);
}

command_mark command_mark_take(app_scene_authoring *scene, command_edited edited, const char *name)
{
    command_mark mark = { .edited = edited };
    if (edited == COMMAND_EDITED_SCENE) {
        mark.name = xstrdup(scene_key(scene));
        mark.position = scene_history_group_id(scene->history, false);
        return mark;
    }
    mark.name = xstrdup(name ? name : "");
    char **undo = undo_stack(scene, &mark);
    mark.position = (uint64_t)acount(undo);
    mark.top = acount(undo) ? undo[acount(undo) - 1] : NULL;
    document_hash(scene, &mark, mark.hash);
    return mark;
}

bool command_mark_latest(app_scene_authoring *scene, const command_mark *mark)
{
    if (mark->edited == COMMAND_EDITED_NONE) {
        return false;
    }
    if (mark->edited == COMMAND_EDITED_SCENE) {
        return !strcmp(scene_key(scene), mark->name) &&
               scene_history_group_id(scene->history, false) == mark->position;
    }
    char **undo = undo_stack(scene, mark);
    return (uint64_t)acount(undo) == mark->position && acount(undo) &&
           undo[acount(undo) - 1] == mark->top;
}

int command_mark_undo(app_scene_authoring *scene, const command_mark *mark)
{
    if (!command_mark_latest(scene, mark)) {
        return -1;
    }
    if (mark->edited == COMMAND_EDITED_SCENE) {
        finish_transform_edit(scene, false);
        return replay_scene_edit(scene, false);
    }
    /* The stack alone can match after an undo and a new edit; the document
       must still be exactly what the command left. */
    char hash[17];
    document_hash(scene, mark, hash);
    if (strcmp(hash, mark->hash)) {
        return -1;
    }
    if (mark->edited == COMMAND_EDITED_GRAPH) {
        editor_undo(scene->graph_editor);
        return 0;
    }
    if (selected_prototype(scene) != find_prototype(scene, mark->name) &&
        app_scene_authoring_open_prototype(scene, mark->name)) {
        return -1;
    }
    return prototype_undo(scene);
}

void command_mark_free(command_mark *mark)
{
    free(mark->name);
    *mark = (command_mark){ 0 };
}
