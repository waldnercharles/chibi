#ifndef CHIBI_APP_SCENE_HISTORY_H
#define CHIBI_APP_SCENE_HISTORY_H

#include "ecs/ecs.h"
#include "runtime/components.h"

#include <stdbool.h>

typedef struct scene_history scene_history;

scene_history *scene_history_new();
void scene_history_free(scene_history *history);
void scene_history_clear(scene_history *history);
/* Explicit scopes only; nesting is invalid. Empty scopes preserve redo.
 * IDs survive replay. Zero means an ungrouped head or an empty stack.
 * Replay one entry at a time while the nonzero head ID matches. On failure,
 * reverse exactly the number of completed entries, not the entire group. */
void scene_history_begin_group(scene_history *history);
void scene_history_end_group(scene_history *history);
uint64_t scene_history_group_id(const scene_history *history, bool redo);
/* Records an already-applied transform edit. A no-op preserves redo. */
bool scene_history_push(scene_history *history, ecs_entity entity, GameTransform before, GameTransform after);
typedef struct
{
    ecs_component_id component;
    int field;
    pin_value before;
    pin_value after;
} scene_field_edit;

/* Records already-applied scalar edits as one undo step. Owns copied values and
 * nominal type names. Unsupported values or duplicate fields reject the batch;
 * empty/no-op batches preserve redo. Replay validates every field before writing. */
bool scene_history_push_fields(
    scene_history *history,
    ecs_entity entity,
    const scene_field_edit *fields,
    int count
);
bool scene_history_push_field(
    scene_history *history,
    ecs_entity entity,
    ecs_component_id component,
    int field,
    pin_value before,
    pin_value after
);
typedef struct
{
    int index;
    const char *name;
    const char *prototype_name;
    const char *base_json;
    const char *state_json;
    bool created;
    const char *retained_json;
} scene_actor_edit;

/* The app owns actor replay. History owns copies of all strings. Peek returns
 * NULL unless the selected stack's head is an actor edit; outputs then remain
 * unchanged. present says whether replay must restore or remove the actor. */
bool scene_history_push_actor(scene_history *history, ecs_entity entity, const scene_actor_edit *edit);
const scene_actor_edit *scene_history_peek_actor(
    const scene_history *history,
    bool redo,
    ecs_entity *entity,
    bool *present
);
/* Call only after successful replay. Restoration supplies the new full handle
 * and rebinds this actor's older/newer edits; removal supplies a zero handle.
 * The peeked pointer is borrowed until the next history mutation. */
void scene_history_complete_actor(scene_history *history, bool redo, ecs_entity replacement);

typedef struct
{
    const char *const *before;
    int before_count;
    const char *const *after;
    int after_count;
} scene_behavior_edit;

/* Ordered names are copied and owned by history. Invalid lists (negative count,
 * missing/empty names or duplicates) and unchanged lists preserve both stacks.
 * The app restores before on Undo or after on Redo, then completes only success. */
bool scene_history_push_behaviors(scene_history *history, ecs_entity entity, const scene_behavior_edit *edit);
const scene_behavior_edit *scene_history_peek_behaviors(
    const scene_history *history,
    bool redo,
    ecs_entity *entity
);
void scene_history_complete_behaviors(scene_history *history, bool redo);

typedef struct
{
    ecs_component_id component;
    const char *state_json;
    bool added;
    const char *retained_before_json;
    const char *retained_after_json;
} scene_component_edit;

/* Owns the captured single-component JSON. The app replays presence changes
 * and completes only after successful application; failed replay keeps history. */
bool scene_history_push_component(scene_history *history, ecs_entity entity, const scene_component_edit *edit);
const scene_component_edit *scene_history_peek_component(
    const scene_history *history,
    bool redo,
    ecs_entity *entity,
    bool *present
);
void scene_history_complete_component(scene_history *history, bool redo);

bool scene_history_can_undo(const scene_history *history);
bool scene_history_can_redo(const scene_history *history);
/* Returns 1 after applying, 0 when empty, or -1 if the entity/component/field
 * is missing, its scalar schema no longer matches, or the head requires app-owned actor/behavior replay. Failure preserves both
 * stacks and all component values. out_entity is
 * optional and is assigned only after applying. Entities are never recreated. */
int scene_history_undo(scene_history *history, ecs_world *world, ecs_entity *out_entity);
int scene_history_redo(scene_history *history, ecs_world *world, ecs_entity *out_entity);

char *scene_history_checkpoint(const scene_history *history);
bool scene_history_restore_checkpoint(
    scene_history *history,
    const char *json,
    const uint64_t *old_entities,
    const uint64_t *new_entities,
    int count
);

#endif
