#ifndef CHIBI_GAME_SAVE_H
#define CHIBI_GAME_SAVE_H

#include "graph/model/registry.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct game_save game_save;

/* Each world starts with an isolated in-memory store. Hosts may bind a native
 * file (or browser localStorage key) before launching authored behaviors. */
game_save *game_save_new();
void game_save_free(game_save *save);
bool game_save_open(game_save *save, const char *path);
const char *game_save_error(const game_save *save);
const char *game_save_get(const game_save *save, const char *key);
bool game_save_set(game_save *save, const char *key, const char *text);
bool game_save_remove(game_save *save, const char *key);
/* Bumped by every change to the entries, so views can refresh. */
uint64_t game_save_revision(const game_save *save);
bool game_save_commit(game_save *save);
void game_save_discard(game_save *save);
/* Owned canonical JSON of committed state; import is atomic and never writes
 * the host's storage. Used for checkpoints and isolated scenario inputs. */
char *game_save_export(const game_save *save);
bool game_save_import(game_save *save, const char *json);

extern const registry_pack GAME_SAVE_PACK;

#endif
