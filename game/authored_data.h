#ifndef CBLUEPRINT_GAME_AUTHORED_DATA_H
#define CBLUEPRINT_GAME_AUTHORED_DATA_H

#include <stddef.h>

typedef enum
{
    GAME_DATA_PLAYER,
    GAME_DATA_ENEMY,
    GAME_DATA_WAVE,
    GAME_DATA_ARENA,
    GAME_DATA_WEAPON,
    GAME_DATA_PROJECTILE,
    GAME_DATA_PROGRESSION,
    GAME_DATA_SUPPORT,
    GAME_DATA_MATERIAL,
} game_data_kind;

typedef struct game_authored_data game_authored_data;
typedef int (*game_authored_data_apply_fn)(const game_authored_data *, void *);

game_authored_data *game_authored_data_load(
    const char *path,
    game_data_kind kind,
    char *error,
    size_t error_size
);
game_authored_data *game_authored_data_from_json(
    const char *json,
    game_data_kind kind,
    char *error,
    size_t error_size
);
void game_authored_data_free(game_authored_data *data);
const char *game_authored_data_name(const game_authored_data *data);
const char *game_authored_data_json(const game_authored_data *data);
const char *game_authored_data_path(const game_authored_data *data);
int game_authored_data_set_path(game_authored_data *data, const char *path);
int game_authored_data_set_json(game_authored_data *data, const char *json, char *error, size_t error_size);
int game_authored_data_save(const game_authored_data *data, const char *path);
int game_authored_data_apply(game_authored_data *data, game_authored_data_apply_fn apply, void *user);
int game_authored_data_reload(game_authored_data *data, char *error, size_t error_size);
int game_authored_data_revert(game_authored_data *data);
int game_authored_data_dirty(const game_authored_data *data);
const char *game_authored_data_kind_name(game_data_kind kind);
const char *game_authored_data_type_name(game_data_kind kind);
int game_authored_data_read_number(const game_authored_data *data, const char *field, double *value);

#endif
