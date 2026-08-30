#ifndef CBLUEPRINT_GAME_QUERY_EDITOR_H
#define CBLUEPRINT_GAME_QUERY_EDITOR_H

#include "../ecs/ecs.h"

#include <stddef.h>

typedef struct game_query_editor game_query_editor;

game_query_editor *game_query_editor_new(const ecs_query_expr *expression);
void game_query_editor_free(game_query_editor *editor);
const ecs_query_expr *game_query_editor_expression(const game_query_editor *editor);
int game_query_editor_set(game_query_editor *editor, const ecs_query_expr *expression);
int game_query_editor_add(
    game_query_editor *editor,
    const int *path,
    int depth,
    ecs_query_expr_kind kind,
    ecs_component_id component
);
int game_query_editor_set_kind(game_query_editor *editor, const int *path, int depth, ecs_query_expr_kind kind);
int game_query_editor_set_component(game_query_editor *editor, const int *path, int depth, ecs_component_id component);
int game_query_editor_remove(game_query_editor *editor, const int *path, int depth);
int game_query_editor_move(game_query_editor *editor, const int *path, int depth, int direction);
int game_query_editor_undo(game_query_editor *editor);
int game_query_editor_redo(game_query_editor *editor);
int game_query_editor_validate(const ecs_query_expr *expression, char *error, size_t error_size);
int game_query_editor_preview(ecs_world *world, const ecs_query_expr *expression);

#endif
