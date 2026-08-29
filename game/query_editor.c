#include "query_editor.h"

#include "../core/array.h"
#include "../core/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

struct game_query_editor
{
    ecs_query_expr *expression;
    ecs_query_expr **undo;
    ecs_query_expr **redo;
};

typedef enum
{
    QUERY_EDIT_ADD,
    QUERY_EDIT_REMOVE,
    QUERY_EDIT_MOVE,
    QUERY_EDIT_KIND,
    QUERY_EDIT_COMPONENT,
} query_edit;

static ecs_query_expr *copy_edit(
    const ecs_query_expr *source,
    const int *path,
    int depth,
    int level,
    query_edit edit,
    ecs_query_expr_kind kind,
    ecs_component_id component,
    int direction
)
{
    if (level == depth) {
        if (edit == QUERY_EDIT_REMOVE) {
            return NULL;
        }
        ecs_query_expr *
            result = edit == QUERY_EDIT_KIND
                       ? ecs_query_expr_new(kind)
                       : (ecs_query_expr_get_kind(source) == ECS_QUERY_HAS
                              ? ecs_query_expr_has(ecs_query_expr_get_component(source))
                              : ecs_query_expr_new(ecs_query_expr_get_kind(source)));
        if (edit == QUERY_EDIT_COMPONENT) {
            ecs_query_expr_free(result);
            result = ecs_query_expr_has(component);
        }
        for (int i = 0; i < ecs_query_expr_child_count(source); i++) {
            assert(
                ecs_query_expr_add(result, ecs_query_expr_clone(ecs_query_expr_child(source, i))) == ECS_OK
            );
        }
        if (edit == QUERY_EDIT_ADD) {
            ecs_query_expr *child = kind == ECS_QUERY_HAS
                                      ? ecs_query_expr_has(component)
                                      : ecs_query_expr_new(kind);
            if (kind != ECS_QUERY_HAS) {
                assert(ecs_query_expr_add(child, ecs_query_expr_has(component)) == ECS_OK);
            }
            assert(ecs_query_expr_add(result, child) == ECS_OK);
        }
        return result;
    }

    ecs_query_expr *result = ecs_query_expr_new(ecs_query_expr_get_kind(source));
    int count = ecs_query_expr_child_count(source);
    int selected = path[level];
    assert(selected >= 0 && selected < count);
    for (int i = 0; i < count; i++) {
        if (level + 1 == depth && edit == QUERY_EDIT_REMOVE && i == selected) {
            continue;
        }
        const ecs_query_expr *child = ecs_query_expr_child(source, i);
        ecs_query_expr *copy;
        if (level + 1 == depth && edit == QUERY_EDIT_MOVE &&
            (i == selected || i == selected + direction)) {
            int other = selected + direction;
            copy = ecs_query_expr_clone(
                ecs_query_expr_child(source, i == selected ? other : selected)
            );
        } else if (i == selected) {
            copy = copy_edit(child, path, depth, level + 1, edit, kind, component, direction);
        } else {
            copy = ecs_query_expr_clone(child);
        }
        if (copy) {
            assert(ecs_query_expr_add(result, copy) == ECS_OK);
        }
    }
    return result;
}

static void remember(game_query_editor *editor)
{
    apush(editor->undo, ecs_query_expr_clone(editor->expression));
    for (int i = 0; i < acount(editor->redo); i++) {
        ecs_query_expr_free(editor->redo[i]);
    }
    asetlen(editor->redo, 0);
}

static int replace(game_query_editor *editor, ecs_query_expr *expression)
{
    if (!expression || game_query_editor_validate(expression, NULL, 0) != 0) {
        ecs_query_expr_free(expression);
        return -1;
    }
    remember(editor);
    ecs_query_expr_free(editor->expression);
    editor->expression = expression;
    return 0;
}

game_query_editor *game_query_editor_new(const ecs_query_expr *expression)
{
    game_query_editor *editor = (game_query_editor *)xcalloc(1, sizeof *editor);
    editor->expression = expression ? ecs_query_expr_clone(expression)
                                    : ecs_query_expr_new(ECS_QUERY_ALL);
    return editor;
}

void game_query_editor_free(game_query_editor *editor)
{
    if (!editor) {
        return;
    }
    ecs_query_expr_free(editor->expression);
    for (int i = 0; i < acount(editor->undo); i++) {
        ecs_query_expr_free(editor->undo[i]);
    }
    for (int i = 0; i < acount(editor->redo); i++) {
        ecs_query_expr_free(editor->redo[i]);
    }
    afree(editor->undo);
    afree(editor->redo);
    free(editor);
}

const ecs_query_expr *game_query_editor_expression(const game_query_editor *editor)
{
    return editor ? editor->expression : NULL;
}

int game_query_editor_set(game_query_editor *editor, const ecs_query_expr *expression)
{
    return replace(editor, expression ? ecs_query_expr_clone(expression) : ecs_query_expr_new(ECS_QUERY_ALL));
}

int game_query_editor_add(
    game_query_editor *editor,
    const int *path,
    int depth,
    ecs_query_expr_kind kind,
    ecs_component_id component
)
{
    assert(depth >= 0);
    ecs_query_expr *result = copy_edit(editor->expression, path, depth, 0, QUERY_EDIT_ADD, kind, component, 0);
    return replace(editor, result);
}

int game_query_editor_set_kind(game_query_editor *editor, const int *path, int depth, ecs_query_expr_kind kind)
{
    if (depth < 0 || kind == ECS_QUERY_HAS) {
        return -1;
    }
    return replace(
        editor,
        copy_edit(editor->expression, path, depth, 0, QUERY_EDIT_KIND, kind, 1, 0)
    );
}

int game_query_editor_set_component(game_query_editor *editor, const int *path, int depth, ecs_component_id component)
{
    if (depth < 0 || !component) {
        return -1;
    }
    return replace(
        editor,
        copy_edit(editor->expression, path, depth, 0, QUERY_EDIT_COMPONENT, ECS_QUERY_HAS, component, 0)
    );
}

int game_query_editor_remove(game_query_editor *editor, const int *path, int depth)
{
    if (depth <= 0) {
        return -1;
    }
    return replace(
        editor,
        copy_edit(editor->expression, path, depth, 0, QUERY_EDIT_REMOVE, ECS_QUERY_HAS, 1, 0)
    );
}

int game_query_editor_move(game_query_editor *editor, const int *path, int depth, int direction)
{
    if (depth <= 0 || (direction != -1 && direction != 1)) {
        return -1;
    }
    int parent_depth = depth - 1;
    const ecs_query_expr *parent = editor->expression;
    for (int i = 0; i < parent_depth; i++) {
        parent = ecs_query_expr_child(parent, path[i]);
    }
    int index = path[depth - 1];
    if (index + direction < 0 ||
        index + direction >= ecs_query_expr_child_count(parent)) {
        return -1;
    }
    return replace(
        editor,
        copy_edit(editor->expression, path, depth, 0, QUERY_EDIT_MOVE, ECS_QUERY_HAS, 1, direction)
    );
}

int game_query_editor_undo(game_query_editor *editor)
{
    if (!acount(editor->undo)) {
        return -1;
    }
    apush(editor->redo, editor->expression);
    editor->expression = alast(editor->undo);
    asetlen(editor->undo, acount(editor->undo) - 1);
    return 0;
}

int game_query_editor_redo(game_query_editor *editor)
{
    if (!acount(editor->redo)) {
        return -1;
    }
    apush(editor->undo, editor->expression);
    editor->expression = alast(editor->redo);
    asetlen(editor->redo, acount(editor->redo) - 1);
    return 0;
}

int game_query_editor_validate(const ecs_query_expr *expression, char *error, size_t error_size)
{
    if (!expression) {
        return -1;
    }
    ecs_query_expr_kind kind = ecs_query_expr_get_kind(expression);
    int count = ecs_query_expr_child_count(expression);
    if ((kind == ECS_QUERY_ALL || kind == ECS_QUERY_ANY) && !count) {
        if (error && error_size) {
            snprintf(error, error_size, "%s group is empty", kind == ECS_QUERY_ALL ? "All" : "Any");
        }
        return -1;
    }
    if (kind == ECS_QUERY_NOT && count != 1) {
        if (error && error_size) {
            snprintf(error, error_size, "Not group requires exactly one term");
        }
        return -1;
    }
    for (int i = 0; i < count; i++) {
        if (game_query_editor_validate(ecs_query_expr_child(expression, i), error, error_size) !=
            0) {
            return -1;
        }
    }
    return 0;
}

int game_query_editor_preview(ecs_world *world, const ecs_query_expr *expression)
{
    if (game_query_editor_validate(expression, NULL, 0) != 0) {
        return -1;
    }
    ecs_query query;
    if (ecs_query_begin_expression(world, &query, expression) != ECS_OK) {
        return -1;
    }
    int count = 0;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity) > 0) {
        count++;
    }
    ecs_query_end(&query);
    return count;
}
