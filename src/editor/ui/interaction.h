#ifndef CHIBI_EDITOR_INTERACTION_H
#define CHIBI_EDITOR_INTERACTION_H
#include <stdbool.h>
typedef struct editor_interaction editor_interaction;
/* Observed controls are serialized once and checked again before input. */
char *editor_interaction_snapshot();
editor_interaction *editor_interaction_start(
    const char *observation,
    const char *action,
    char *error,
    int size
);
/* 0 pending, 1 finished, -1 invalidated. Advances through normal ImGui input. */
int editor_interaction_frame(editor_interaction *input);
void editor_interaction_free(editor_interaction *input);
#endif
