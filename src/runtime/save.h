#ifndef CHIBI_GAME_SAVE_H
#define CHIBI_GAME_SAVE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct game_save game_save;

/* Each world starts with an isolated in-memory store. Hosts may bind a native
 * file (or browser localStorage key) before launching authored behaviors. */
game_save *game_save_new();
void game_save_free(game_save *save);
bool game_save_open(game_save *save, const char *path);
/* Open the explicit path when supplied, otherwise a stable project-name key.
 * Native hosts supply an existing writable directory; browsers use localStorage
 * and ignore directory. Changing the project name selects a different store. */
bool game_save_open_project(
    game_save *save,
    const char *project_name,
    const char *directory,
    const char *override
);
const char *game_save_error(const game_save *save);
const char *game_save_get(const game_save *save, const char *key);
bool game_save_set(game_save *save, const char *key, const char *text);
/* Numbers share the text store, using finite JSON decimal text. A missing or
 * malformed number returns false without changing the output. */
bool game_save_get_number(const game_save *save, const char *key, double *number);
bool game_save_set_number(game_save *save, const char *key, double number);
bool game_save_remove(game_save *save, const char *key);
/* The entries in the order their keys were first set. */
int game_save_count(const game_save *save);
const char *game_save_key(const game_save *save, int index);
/* Bumped by every change to the entries, so views can refresh. */
uint64_t game_save_revision(const game_save *save);
bool game_save_commit(game_save *save);
void game_save_discard(game_save *save);
/* Owned canonical JSON of committed state; import is atomic and never writes
 * the host's storage. Used for checkpoints and isolated scenario inputs. */
char *game_save_export(const game_save *save);
bool game_save_import(game_save *save, const char *json);

#endif
